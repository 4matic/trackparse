"""R8: the title side."""

from __future__ import annotations

from typing import NamedTuple, Optional

from ._classify import (
    Feat,
    Flag,
    Junk,
    Producer,
    Site,
    VersionDraft,
    Versions,
    Year,
    classify,
    junk_kind_of,
    phrase_kind_of,
)
from ._context import Ctx
from ._credits import Credit, ListOptions, PositionedYear, split_credits
from ._mode import PositionedJunk
from ._scanner import (
    PH,
    Skel,
    cut_skel,
    groups_of,
    is_spaced_bullet_at,
    is_spaced_dash_at,
    remove_groups,
    render_skel,
    slice_skel,
    split_at_spaced_dashes,
    trim_skel,
)
from ._separator import Peeled, peel_suffixes
from ._tables import is_unknown_token
from ._words import char_at, collapse_spaces, split_words, word_spans


class Extracted:
    """Everything the title side (and R6.2 peeling) can extract."""

    __slots__ = ("clean", "credits", "explicit", "junk", "unknown_artist", "versions", "years")

    def __init__(self) -> None:
        self.credits: list[Credit] = []
        self.versions: list[VersionDraft] = []
        self.junk: list[PositionedJunk] = []
        self.years: list[PositionedYear] = []
        self.explicit = False
        self.clean = False
        self.unknown_artist = False

    def set_flag(self, flag: str) -> None:
        if flag == "explicit":
            self.explicit = True
        else:
            self.clean = True


def add_peeled(out: Extracted, peeled: list[Peeled]) -> None:
    """Record R6.2 / R8.2 peeled dash suffixes."""
    for p in peeled:
        c = p.cls
        if isinstance(c, Versions):
            out.versions.extend(c.versions)
        elif isinstance(c, Junk):
            out.junk.append(PositionedJunk(p.raw, c.junk_kind, p.pos))
        elif isinstance(c, Flag):
            out.set_flag(c.flag)
        else:
            out.years.append(PositionedYear(c.year, p.pos))


class TitleSideResult(NamedTuple):
    title: str
    unknown_title: bool


def parse_title_side(
    skel: Skel, relaxed: bool, youtube: bool, out: Extracted, ctx: Ctx
) -> TitleSideResult:
    """``relaxed``: an artist side was split off (R8.2 peels without conditions)."""
    removed = _extract_groups(skel, out, ctx)  # R8.1

    # R8.2
    segments = split_at_spaced_dashes(skel)
    remaining, peeled = peel_suffixes(segments, relaxed, ctx)
    add_peeled(out, peeled)
    skel = slice_skel(skel, 0, remaining[-1].end)

    skel = _extract_unbracketed_credits(skel, out, ctx)  # R8.3
    skel = remove_groups(skel, removed)
    if youtube:
        skel = _strip_trailing_junk(skel, out, ctx)  # R8.4

    title = cleanup_title(render_skel(skel))  # R8.5
    return TitleSideResult(title, len(title) > 0 and is_unknown_token(title, ctx.t))  # R8.6


def _extract_groups(skel: Skel, out: Extracted, ctx: Ctx) -> set[int]:
    """R8.1: classify every group; returns the positions of the groups to remove."""
    removed: set[int] = set()
    for _, group in groups_of(skel):
        c = classify(group.inner, Site("title", group.open, group.pos), ctx)
        if isinstance(c, Versions):
            out.versions.extend(c.versions)
        elif isinstance(c, Junk):
            out.junk.append(PositionedJunk(group.inner, c.junk_kind, group.pos))
        elif isinstance(c, Flag):
            out.set_flag(c.flag)
        elif isinstance(c, Year):
            out.years.append(PositionedYear(c.year, group.pos))
        elif isinstance(c, (Feat, Producer)):
            out.credits.extend(c.credits)
            out.years.extend(c.years)
            if c.unknown:
                out.unknown_artist = True
        else:
            continue
        removed.add(group.pos)
    return removed


class _MarkerHit(NamedTuple):
    start: int
    end: int
    marker: str
    role: str  # "featured" | "producer"


def _title_markers(s: Skel, ctx: Ctx) -> list[_MarkerHit]:
    """R8.3 markers, left to right, each with a space on both sides: feat markers from
    ``#titleMarkers`` (``feat.``, ``ft.``, ``featuring``; undotted ``feat``/``ft`` stay text)
    and producer markers from ``#titleProducer``."""
    spans = word_spans(s.text)
    words = [w.text for w in spans]
    hits: list[_MarkerHit] = []
    i = 0
    while i < len(spans):
        span = spans[i]
        if char_at(s.text, span.start - 1) != " ":
            i += 1
            continue
        role = "featured"
        feat = ctx.t.title_feat_markers.match_at(words, i)
        if feat is not None:
            last_index = i + feat.length - 1
        else:
            prod = ctx.t.title_producer_markers.match_at(words, i)
            if prod is None:
                i += 1
                continue
            role = "producer"
            last_index = i + prod.length - 1
        last = spans[last_index]
        if char_at(s.text, last.end) != " ":
            i += 1
            continue
        hits.append(_MarkerHit(span.start, last.end, s.text[span.start : last.end], role))
        i = last_index + 1
    return hits


def _extract_unbracketed_credits(inp: Skel, out: Extracted, ctx: Ctx) -> Skel:
    """R8.3: ``Title feat. X``, ``Title prod. by Y`` → credits, removed from the title. A list
    runs to the next placeholder, the next marker of the other kind, or the end; later feat
    markers inside a featured list are joiners (R7.2)."""
    markers = _title_markers(inp, ctx)
    cuts: list[tuple[int, int]] = []
    i = 0
    while i < len(markers):
        hit = markers[i]
        list_end = inp.text.find(PH, hit.end)
        if list_end < 0:
            list_end = len(inp.text)
        other = next(
            (
                m
                for k, m in enumerate(markers)
                if k > i and m.role != hit.role and m.start >= hit.end
            ),
            None,
        )
        if other is not None and other.start < list_end:
            list_end = other.start
        lst = trim_skel(slice_skel(inp, hit.end, list_end))
        if len(lst.text) > 0:
            r = split_credits(
                lst,
                ListOptions(hit.role, "title", hit.marker, False, hit.role == "featured"),
                ctx,
            )
            out.credits.extend(r.credits)
            out.years.extend(r.years)
            if r.unknown:
                out.unknown_artist = True
            cuts.append((hit.start, list_end))
        else:
            list_end = hit.end
        while i < len(markers) and markers[i].start < list_end:
            i += 1
    skel = inp
    for start, end in reversed(cuts):
        skel = cut_skel(skel, start, end)
    return skel


def _strip_trailing_junk(inp: Skel, out: Extracted, ctx: Ctx) -> Skel:
    """R8.4: strip trailing unbracketed junk phrases (optionally after a lone ``-`` / ``|``).

    Word spans are computed once and walked backwards, so repeated junk stays linear (R0.8).
    """
    skel = trim_skel(inp)
    spans = word_spans(skel.text)
    words = [span.text for span in spans]
    end = len(spans)  # words still in the title
    while True:
        m = ctx.t.junk_phrases.match_ending(words, end)
        if m is None:
            break
        first = end - m.length
        keep = first
        if keep > 0 and _is_lone_edge(words[keep - 1]):
            keep -= 1
        if keep == 0:
            break  # never strip the whole title
        start = spans[first].start
        raw = collapse_spaces(skel.text[start : spans[end - 1].end])
        pos = skel.pos[start] if start < len(skel.pos) else 0
        out.junk.append(PositionedJunk(raw, m.entry.value, pos))
        end = keep
    if end == len(spans):
        return skel
    return trim_skel(slice_skel(skel, 0, spans[end - 1].end))


def _is_lone_edge(word: str) -> bool:
    return word == "-" or word == "|"


def _strip_lone_edges(s: str) -> str:
    words = s.split(" ")
    a = 0
    b = len(words)
    while a < b and _is_lone_edge(words[a]):
        a += 1
    while b > a and _is_lone_edge(words[b - 1]):
        b -= 1
    return " ".join(words[a:b])


def cleanup_title(raw: str) -> str:
    """R8.5"""
    title = _strip_lone_edges(collapse_spaces(raw))
    q = title[:1]
    if len(title) >= 2 and (q == '"' or q == "'") and title[-1] == q:
        title = _strip_lone_edges(collapse_spaces(title[1:-1]))
    return title


# ---------------------------------------------------------------------------
# R8.7 Bullet suffix


class BulletSuffix(NamedTuple):
    #: The working skeleton before the first cut, trimmed (R8.7.3).
    head: Skel
    label: Optional[str]
    #: Junk from the tail segments, with their positions in the normalized string (R9.7).
    junk: list[PositionedJunk]


def _bullet_segments(skel: Skel) -> Optional[list[Skel]]:
    """R8.7.1–R8.7.2: the head and tail segments when the rule fires, else None. Cuts at every
    spaced bullet after the first spaced dash D, which must have text before it and after it."""
    t = skel.text
    if t[:2] == "- ":
        return None  # R6.1: D at the start, nothing before it
    dash = -1
    for i in range(1, len(t) - 1):
        if is_spaced_dash_at(t, i):
            dash = i
            break
    if dash < 0:
        return None
    parts: list[Skel] = []
    start = 0
    for i in range(dash + 2, len(t) - 1):
        if not is_spaced_bullet_at(t, i):
            continue
        parts.append(trim_skel(slice_skel(skel, start, i)))
        start = i + 1
    if not parts:
        return None
    parts.append(trim_skel(slice_skel(skel, start)))
    if any(len(p.text) == 0 for p in parts):
        return None
    # R8.7.2: the title part between D and the first bullet counts as a segment (`A - • L`).
    if len(parts[0].text) <= dash + 1:
        return None
    return parts


def cut_bullet_suffix(skel: Skel, ctx: Ctx) -> Optional[BulletSuffix]:
    """R8.7: cut a bullet suffix off the working skeleton (after R2 and R4, before R6)."""
    parts = _bullet_segments(skel)
    if parts is None:
        return None
    junk: list[PositionedJunk] = []
    label: Optional[str] = None
    for part in parts[1:]:
        # R8.7.4: junk groups go to junk; every other group stays verbatim.
        removed: set[int] = set()
        for _, group in groups_of(part):
            kind = junk_kind_of(split_words(group.inner), ctx)
            if not kind:
                continue
            junk.append(PositionedJunk(group.inner, kind, group.pos))
            removed.add(group.pos)
        rest = trim_skel(remove_groups(part, removed))
        if len(rest.text) == 0:
            continue
        text = collapse_spaces(render_skel(rest))
        pos = rest.pos[0] if rest.pos else 0
        # R8.7.4: only junk phrases, connectors and year words (no genres, no label suffixes).
        phrase_kind = phrase_kind_of(split_words(text), ctx.t.junk_phrases, ctx)
        if phrase_kind:
            junk.append(PositionedJunk(text, phrase_kind, pos))
        elif label is None:
            label = text
        else:
            junk.append(PositionedJunk(text, "note", pos))
    return BulletSuffix(parts[0], label, junk)
