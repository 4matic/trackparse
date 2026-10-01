"""R6: artist/title separator, including R6.2 suffix peeling (shared with R8.2)."""

from __future__ import annotations

from typing import NamedTuple, Optional, Union

from ._classify import (
    UNKNOWN,
    Classification,
    Flag,
    Junk,
    Site,
    Versions,
    Year,
    classify,
)
from ._context import Ctx, is_known_artist, warn
from ._scanner import (
    PH,
    Segment,
    Skel,
    concat_skel,
    has_group,
    render_skel,
    slice_skel,
    split_at_spaced_dashes,
    start_pos,
    trim_skel,
)
from ._words import (
    char_at,
    collapse_spaces,
    in_vocab,
    is_letter,
    is_whitespace,
    split_words,
    year_of,
)

Peelable = Union[Junk, Flag, Year, Versions]


class Peeled(NamedTuple):
    cls: Peelable
    raw: str
    pos: int


def _classify_segment(seg: Skel, ctx: Ctx) -> Classification:
    """R6.2 classification of a bare dash segment: placeholders make it Unknown."""
    if has_group(seg):
        return UNKNOWN
    text = collapse_spaces(seg.text)
    return classify(text, Site("title", "-", start_pos(seg)), ctx)


def peel_suffixes(
    segments: list[Segment], relaxed: bool, ctx: Ctx
) -> tuple[list[Segment], list[Peeled]]:
    """R6.2 / R8.2: peel classifiable suffixes off the right end. ``relaxed`` drops conditions
    (i)–(iv) (R8.2 with an artist side). Returns the remaining segments and the peeled
    suffixes, left to right."""
    remaining = list(segments)
    peeled: list[Peeled] = []
    while len(remaining) >= 2:
        last = remaining[-1]
        cls = _classify_segment(last.skel, ctx)
        if not isinstance(cls, (Junk, Flag, Year, Versions)):
            break
        words = split_words(last.skel.text)
        # (ii) does not apply to prefix-form versions (`Andy C b2b Hedex - Live at Printworks`).
        prefix_form = isinstance(cls, Versions) and all(v.prefix_form for v in cls.versions)
        ok = (
            relaxed
            or len(remaining) - 1 >= 2
            or (len(words) >= 2 and not prefix_form)
            or any(year_of(w) is not None for w in words)
            or isinstance(cls, Junk)
        )
        if not ok:
            break
        peeled.insert(0, Peeled(cls, collapse_spaces(last.skel.text), start_pos(last.skel)))
        remaining.pop()
    return remaining, peeled


class SeparatorResult(NamedTuple):
    artist: Optional[Skel]
    title: Skel
    #: A separator split the string (R6.3–R6.7): R8.2 peels without conditions.
    split: bool
    peeled: list[Peeled]


def _non_empty(s: Skel) -> Optional[Skel]:
    t = trim_skel(s)
    return t if len(t.text) > 0 else None


def _split_at(s: Skel, cut_start: int, cut_end: int) -> tuple[Optional[Skel], Skel]:
    return _non_empty(slice_skel(s, 0, cut_start)), trim_skel(slice_skel(s, cut_end))


def find_separator(skel: Skel, mode: str, uploader: Optional[Skel], ctx: Ctx) -> SeparatorResult:
    segments = split_at_spaced_dashes(skel, True)
    remaining, peeled = peel_suffixes(segments, False, ctx)

    # R6.3
    if len(remaining) >= 2:
        first = remaining[0]
        second = remaining[1]
        last_seg = remaining[-1]
        artist = first.skel if len(first.skel.text) > 0 else None
        title = slice_skel(skel, second.start, last_seg.end)
        return SeparatorResult(artist, title, artist is not None, peeled)

    rest = remaining[0].skel
    other = _split_without_spaced_dash(rest, mode, ctx)
    if other is not None:
        return SeparatorResult(other[0], other[1], other[0] is not None, peeled)

    # R6.8: the uploader fallback does not warn noSeparator.
    artist = uploader if mode == "youtube" else None
    if len(rest.text) > 0 and artist is None:
        warn(ctx, "noSeparator")
    return SeparatorResult(artist, rest, False, peeled)


def _split_without_spaced_dash(
    s: Skel, mode: str, ctx: Ctx
) -> Optional[tuple[Optional[Skel], Skel]]:
    asym = _asymmetric_dash(s.text)
    if asym >= 0:
        warn(ctx, "asymmetricDashSplit")
        return _split_at(s, asym, asym + 1)
    if mode == "youtube":
        quoted = _quoted_title(s)
        if quoted is not None:
            warn(ctx, "quotedTitleSplit")
            return quoted
        by = _by_split(s, ctx)
        if by >= 0:
            warn(ctx, "bySplit")
            left, right = _split_at(s, by, by + 4)
            return (right if len(right.text) > 0 else None, left if left is not None else s)
    if mode == "youtube" or mode == "filename":
        dash = _unspaced_dash(s, ctx)
        if dash >= 0:
            warn(ctx, "unspacedDashSplit")
            return _split_at(s, dash, dash + 1)
    return None


def _asymmetric_dash(t: str) -> int:
    """R6.4: the first ``-`` with a space on exactly one side."""
    for i in range(1, len(t) - 1):
        if t[i] != "-":
            continue
        if is_whitespace(t[i - 1]) != is_whitespace(t[i + 1]):
            return i
    return -1


def _quoted_title(s: Skel) -> Optional[tuple[Optional[Skel], Skel]]:
    """R6.5: ``Artist "Title" (extra)``."""
    t = s.text
    open_ = t.find('"')
    if open_ <= 0:
        return None
    close = t.find('"', open_ + 1)
    if close < 0:
        return None
    artist = trim_skel(slice_skel(s, 0, open_))
    last = artist.text[-1:]
    if last == ":" or last == "-":
        artist = trim_skel(slice_skel(artist, 0, len(artist.text) - 1))
    title = trim_skel(concat_skel(slice_skel(s, open_ + 1, close), slice_skel(s, close + 1)))
    return (artist if len(artist.text) > 0 else None, title)


def _by_split(s: Skel, ctx: Ctx) -> int:
    """R6.6: index of the space before the last usable `` by ``. The right side must be
    non-empty and not a single stopword (``Stand by Me``)."""
    t = s.text
    i = t.rfind(" by ")
    while i > 0:
        # Placeholder-only words (groups such as `(Official Video)`) don't count here.
        right = [w for w in split_words(t[i + 4 :]) if w != PH]
        if right and not (len(right) == 1 and in_vocab(right[0], ctx.t.stopwords)):
            return i
        # JS lastIndexOf(" by ", i - 1): the last occurrence starting at or before i - 1.
        i = t.rfind(" by ", 0, i - 1 + 4) if i - 1 >= 0 else -1
    return -1


def _unspaced_dash(s: Skel, ctx: Ctx) -> int:
    """R6.7: ``Jay-Z-Numb`` → the chosen unspaced dash index."""
    t = s.text
    last_candidate = -1
    for i in range(1, len(t) - 1):
        if t[i] != "-" or is_whitespace(t[i - 1]) or is_whitespace(t[i + 1]):
            continue
        nxt = char_at(t, i + 1)
        if not (nxt == '"' or is_letter(nxt)):
            continue
        if ctx.known_keys and is_known_artist(render_skel(slice_skel(s, 0, i)), ctx):
            return i
        last_candidate = i
    return last_candidate
