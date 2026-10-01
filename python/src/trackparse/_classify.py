"""R5: classification of a group's inner text (also used for dash suffixes and pipe segments)."""

from __future__ import annotations

from typing import NamedTuple, Optional, Union

from ._context import Ctx
from ._credits import Credit, ListOptions, ListResult, PositionedYear, split_credits
from ._scanner import scan
from ._tables import RunInfo, is_unknown_token
from ._words import (
    WordSpan,
    ascii_lower,
    dedup_key,
    in_vocab,
    is_all_digits,
    split_words,
    utf16_len,
    word_key,
    word_spans,
    year_of,
)


class VersionDraft:
    __slots__ = (
        "ambiguous_mix",
        "artists",
        "delimiter",
        "descriptor",
        "modifiers",
        "pos",
        "prefix_form",
        "raw",
        "type",
        "unknown_artist",
        "year",
    )

    def __init__(self, raw: str, type: str, site: Site) -> None:
        self.type = type
        self.raw = raw
        self.artists: list[Credit] = []
        self.modifiers: list[str] = []
        self.descriptor: Optional[str] = None
        self.year: Optional[int] = None
        self.unknown_artist = False
        self.delimiter = site.delimiter
        self.pos = site.pos
        #: R5.7.2 ambiguousMixCredit; emitted only when the version is used.
        self.ambiguous_mix = False
        #: Recognised by the R5.7.3 prefix form (R6.2 condition (ii) does not apply).
        self.prefix_form = False


class Junk(NamedTuple):
    junk_kind: str
    kind: str = "junk"


class Feat(NamedTuple):
    credits: list[Credit]
    unknown: bool
    years: list[PositionedYear]
    kind: str = "feat"


class Producer(NamedTuple):
    credits: list[Credit]
    unknown: bool
    years: list[PositionedYear]
    kind: str = "producer"


class Flag(NamedTuple):
    flag: str  # "explicit" | "clean"
    kind: str = "flag"


class Year(NamedTuple):
    year: int
    kind: str = "year"


class Versions(NamedTuple):
    versions: list[VersionDraft]
    kind: str = "versions"


class Unknown(NamedTuple):
    kind: str = "unknown"


Classification = Union[Junk, Feat, Producer, Flag, Year, Versions, Unknown]
UNKNOWN = Unknown()


class Site(NamedTuple):
    """Where the classified text sits: credits' source, versions' delimiter, absolute offset."""

    source: str
    delimiter: str
    pos: int


_EXPLICIT_FLAGS = frozenset(["explicit", "explicit version", "dirty", "dirty version"])
_CLEAN_FLAGS = frozenset(["clean", "clean version", "radio clean"])


def classify(inner: str, site: Site, ctx: Ctx) -> Classification:
    """R5: classify ``inner`` (already trimmed and whitespace-collapsed)."""
    words = split_words(inner)
    if not words:
        return UNKNOWN
    junk_kind = junk_kind_of(words, ctx)
    if junk_kind is not None:
        return Junk(junk_kind)
    credit = _feat_or_producer(inner, site, ctx)
    if credit is not None:
        return credit
    flag = _flag_of(words)
    if flag is not None:
        return Flag(flag)
    if len(words) == 1 and is_all_digits(words[0]):
        year = year_of(words[0])
        if year is not None:
            return Year(year)
    versions = _multi_version(inner, site, ctx)
    if versions is None:
        versions = _single_version(inner, site, ctx)
    if versions is not None:
        return Versions(versions)
    return UNKNOWN


# ---------------------------------------------------------------------------
# R5.1 Junk


def junk_kind_of(words: list[str], ctx: Ctx) -> Optional[str]:
    """R5.1: the junk kind if the whole word list is junk vocabulary, else None."""
    first_kind: Optional[str] = None
    i = 0
    n = len(words)
    while i < n:
        m = ctx.t.junk_or_genre.match_at(words, i)
        if m is not None:
            if first_kind is None:
                first_kind = m.entry.value
            i += m.length
            continue
        w = words[i]
        # R5.1: connectors and year words may sit between junk phrases: `Official Video 2015`.
        if (
            w in ctx.t.junk_connectors
            or word_key(w) in ctx.t.junk_connectors
            or year_of(w) is not None
        ):
            i += 1
            continue
        first_kind = None
        break
    if first_kind:
        return first_kind
    if n >= 2 and in_vocab(words[-1], ctx.t.label_suffixes):
        return "label"
    return None


# ---------------------------------------------------------------------------
# R5.2 Feat, R5.3 Producer


def _feat_or_producer(inner: str, site: Site, ctx: Ctx) -> Optional[Classification]:
    spans = word_spans(inner)
    words = [s.text for s in spans]
    feat = ctx.t.feat_markers.match_at(words, 0)
    bracket_only = None if feat is not None else ctx.t.bracket_only_feat.match_at(words, 0)
    if feat is not None:
        feat_len = feat.length
    elif bracket_only is not None:
        feat_len = bracket_only.length
    else:
        feat_len = 0
    if feat_len > 0 and len(words) > feat_len:
        nxt = words[feat_len]
        if bracket_only is None or not in_vocab(nxt, ctx.t.stopwords):
            r = _credits_after(inner, spans, feat_len, "featured", site, ctx)
            return Feat(r.credits, r.unknown, r.years)
    prod = ctx.t.producer_markers.match_at(words, 0)
    if prod is not None:
        length = prod.length
        if length < len(words) and word_key(words[length]) == "by":
            length += 1
        if len(words) > length:
            r = _credits_after(inner, spans, length, "producer", site, ctx)
            return Producer(r.credits, r.unknown, r.years)
    return None


def _credits_after(
    inner: str, spans: list[WordSpan], marker_words: int, role: str, site: Site, ctx: Ctx
) -> ListResult:
    last_marker = spans[marker_words - 1]
    first_name = spans[marker_words]
    lead = inner[spans[0].start : last_marker.end]
    # Offsets are UTF-16 units (see _scanner).
    base = site.pos + 1 + utf16_len(inner[: first_name.start])
    return list_from_text(
        inner[first_name.start :],
        base,
        ctx,
        ListOptions(role, site.source, lead, False, role == "featured"),
    )


def list_from_text(text: str, base: int, ctx: Ctx, opts: ListOptions) -> ListResult:
    """Split a plain string (nested brackets protected by a fresh scan) as a credit list."""
    return split_credits(scan(text, base).skel, opts, ctx)


# ---------------------------------------------------------------------------
# R5.4 Flag


def _flag_of(words: list[str]) -> Optional[str]:
    key = " ".join(word_key(w) for w in words)
    if key in _EXPLICIT_FLAGS:
        return "explicit"
    if key in _CLEAN_FLAGS:
        return "clean"
    return None


# ---------------------------------------------------------------------------
# R5.6 Multi-version


def _split_multi_parts(inner: str) -> list[str]:
    """Split at top-level `` / `` or ``; `` (outside nested brackets)."""
    parts: list[str] = []
    depth = 0
    start = 0
    n = len(inner)
    for i, ch in enumerate(inner):
        if ch == "(" or ch == "[" or ch == "{":
            depth += 1
        elif (ch == ")" or ch == "]" or ch == "}") and depth > 0:
            depth -= 1
        elif (
            depth == 0
            and ch == "/"
            and i > 0
            and inner[i - 1] == " "
            and i + 1 < n
            and inner[i + 1] == " "
        ):
            parts.append(inner[start : i - 1])
            start = i + 2
        elif depth == 0 and ch == ";" and i + 1 < n and inner[i + 1] == " ":
            parts.append(inner[start:i])
            start = i + 2
    parts.append(inner[start:])
    # `inner` is whitespace-collapsed, so trimming U+0020 is the R0.2 trim here.
    return [p.strip(" ") for p in parts]


def _multi_version(inner: str, site: Site, ctx: Ctx) -> Optional[list[VersionDraft]]:
    parts = _split_multi_parts(inner)
    if len(parts) < 2:
        return None
    out: list[VersionDraft] = []
    for part in parts:
        v = _single_version(part, site, ctx) if part else None
        if v is None:
            return None
        out.extend(v)
    return out


# ---------------------------------------------------------------------------
# R5.7 Version


def _single_version(raw: str, site: Site, ctx: Ctx) -> Optional[list[VersionDraft]]:
    spans = word_spans(raw)
    words = [s.text for s in spans]
    v = _head_by(raw, spans, words, site, ctx)
    if v is None:
        v = _head_scan(raw, words, site, ctx)
    if v is None:
        v = _prefix_form(raw, words, site, ctx)
    return [v] if v is not None else None


def _remixer_opts() -> ListOptions:
    return ListOptions("remixer", "version", None, False, False)


def _dedup_remixers(credits: list[Credit]) -> list[Credit]:
    """R9.2: remixers are deduped within their own version only."""
    seen = set()
    out: list[Credit] = []
    for c in credits:
        k = dedup_key(c.name)
        if k in seen:
            continue
        seen.add(k)
        out.append(c)
    return out


def _set_credits(v: VersionDraft, text: str, site: Site, ctx: Ctx) -> None:
    r = list_from_text(text, site.pos, ctx, _remixer_opts())
    v.artists = _dedup_remixers(r.credits)
    if r.unknown:
        v.unknown_artist = True
    # R7.5: a dropped year-only name sets the version's year if it is still null.
    if r.years and v.year is None:
        v.year = r.years[0].year


def _head_by(
    raw: str, spans: list[WordSpan], words: list[str], site: Site, ctx: Ctx
) -> Optional[VersionDraft]:
    """R5.7.1: ``Remix by Skrillex``."""
    head = ctx.t.heads.match_at(words, 0)
    if head is None:
        return None
    if head.length + 1 >= len(words) or word_key(words[head.length]) != "by":
        return None
    rest = spans[head.length + 1]
    v = VersionDraft(raw, head.entry.value, site)
    _set_credits(v, raw[rest.start :], site, ctx)
    return v


class _RunItem:
    __slots__ = ("end", "info", "start", "year")

    def __init__(self, start: int, end: int, info: RunInfo, year: Optional[int]) -> None:
        self.start = start
        self.end = end
        self.info = info
        self.year = year


_EMPTY_INFO = RunInfo()


def _collect_run(words: list[str], end: int, ctx: Ctx) -> list[_RunItem]:
    """Walk left from ``end`` collecting the modifier run (R5.7.2)."""
    items: list[_RunItem] = []
    j = end
    while j > 0:
        m = ctx.t.run_words.match_ending(words, j)
        if m is not None:
            items.append(_RunItem(j - m.length, j, m.entry.value, None))
            j -= m.length
            continue
        year = year_of(words[j - 1])
        if year is None:
            break
        items.append(_RunItem(j - 1, j, _EMPTY_INFO, year))
        j -= 1
    items.reverse()
    return items


def _head_scan(raw: str, words: list[str], site: Site, ctx: Ctx) -> Optional[VersionDraft]:
    """R5.7.2: ``Extended VIP Mix``, ``Skrillex Remix``, ``Taylor's Version``."""
    head = ctx.t.heads.match_ending(words, len(words))
    if head is None:
        return None
    head_type = head.entry.value
    head_start = len(words) - head.length
    run = _collect_run(words, head_start, ctx)
    credit_end = run[0].start if run else head_start
    credit_words = words[:credit_end]
    left = run[-1] if run else None

    # R5.7.2 type resolution. Generic = canonical type mix/edit/version, any spelling.
    generic = head_type in ctx.t.generic_head_types
    typ = head_type
    type_item: Optional[_RunItem] = None
    via_mix = False
    nearest_head = _nearest_non_generic_head(run, ctx) if generic else None
    if generic and left is not None and left.info.type_capable:
        typ = left.info.type_capable
        type_item = left
    elif nearest_head is not None and nearest_head.info.head:
        # `2011 Remastered Version` → remaster
        typ = nearest_head.info.head
        type_item = nearest_head
    elif head_type == "mix" and len(credit_words) > 0:
        typ = "remix"
        via_mix = True
    v = VersionDraft(raw, typ, site)

    descriptors: list[str] = []
    for item in run:
        text = " ".join(words[item.start : item.end])
        if item.year is not None:
            if v.year is None:
                v.year = item.year
            continue
        modifier = (
            item.info.type_capable if item.info.type_capable is not None else item.info.descriptor
        )
        if modifier is not None:
            if item is not type_item and modifier not in v.modifiers:
                v.modifiers.append(modifier)
        elif item.info.genre:
            descriptors.append(text)

    if credit_words:
        credit = " ".join(credit_words)
        last_word = ascii_lower(credit_words[-1])
        if is_unknown_token(credit, ctx.t):
            v.unknown_artist = True
        elif typ == "version" or typ == "cover" or _is_all_junk_phrases(credit_words, ctx):
            # R5.7.2: `Japanese Version`, `Taylor's Version`, `Adele Cover`, `Video Edit`.
            descriptors.insert(0, credit)
        else:
            name = credit[:-2] if last_word.endswith("'s") else credit
            _set_credits(v, name, site, ctx)
            if via_mix and v.artists and len(split_words(v.artists[-1].name)) >= 3:
                v.ambiguous_mix = True
    v.descriptor = " ".join(descriptors) if descriptors else None
    return v


def _nearest_non_generic_head(run: list[_RunItem], ctx: Ctx) -> Optional[_RunItem]:
    """R5.7.2 type rule 2: the non-generic head in the run nearest the (generic) head."""
    for item in reversed(run):
        head = item.info.head
        if head is not None and head not in ctx.t.generic_head_types:
            return item
    return None


def _is_all_junk_phrases(words: list[str], ctx: Ctx) -> bool:
    """R5.7.2: the credit span is made entirely of junk phrases (``Video`` in ``Video Edit``)."""
    i = 0
    while i < len(words):
        m = ctx.t.junk_phrases.match_at(words, i)
        if m is None:
            return False
        i += m.length
    return len(words) > 0


def _prefix_form(raw: str, words: list[str], site: Site, ctx: Ctx) -> Optional[VersionDraft]:
    """R5.7.3: ``Live at Wembley 1986``, ``Remastered 2015``, ``Sped Up``."""
    m = ctx.t.prefix_forms.match_at(words, 0)
    if m is None:
        return None
    v = VersionDraft(raw, m.entry.value, site)
    v.prefix_form = True
    rest: list[str] = []
    for w in words[m.length :]:
        year = year_of(w) if v.year is None else None
        if year is not None:
            v.year = year
        else:
            rest.append(w)
    v.descriptor = " ".join(rest) if rest else None
    return v
