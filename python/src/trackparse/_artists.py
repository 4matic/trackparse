"""R7: the artist side."""

from __future__ import annotations

from typing import NamedTuple, Optional

from ._classify import Feat, Flag, Junk, Producer, Site, Year, classify
from ._context import Ctx
from ._credits import Credit, ListOptions, PositionedYear, split_credits
from ._mode import PositionedJunk
from ._scanner import PH, Skel, groups_of, remove_groups, slice_skel, trim_skel
from ._words import char_at, word_spans


class ArtistSideResult:
    __slots__ = ("clean", "credits", "explicit", "junk", "unknown_artist", "years")

    def __init__(self) -> None:
        self.credits: list[Credit] = []
        self.junk: list[PositionedJunk] = []
        self.years: list[PositionedYear] = []
        self.unknown_artist = False
        #: R7.1: artist-side Flag groups.
        self.explicit = False
        self.clean = False


class FeatMarker(NamedTuple):
    start: int
    end: int
    marker: str


def find_feat_marker(s: Skel, ctx: Ctx, frm: int = 0) -> Optional[FeatMarker]:
    """R7.2: leftmost unbracketed feat marker with a space (or placeholder) left and a space
    right."""
    spans = word_spans(s.text)
    words = [w.text for w in spans]
    for i, span in enumerate(spans):
        if span.start < frm:
            continue
        before = char_at(s.text, span.start - 1)
        if before != " " and before != PH:
            continue
        m = ctx.t.feat_markers.match_at(words, i)
        if m is None:
            continue
        last = spans[i + m.length - 1]
        if char_at(s.text, last.end) != " ":
            continue
        return FeatMarker(span.start, last.end, s.text[span.start : last.end])
    return None


def parse_artist_side(side: Skel, ctx: Ctx) -> ArtistSideResult:
    """R7.1–R7.5 over an artist-side skeleton."""
    result = ArtistSideResult()
    removed: set[int] = set()

    # R7.1
    for _, group in groups_of(side):
        c = classify(group.inner, Site("artist", group.open, group.pos), ctx)
        if isinstance(c, (Feat, Producer)):
            result.credits.extend(c.credits)
            result.years.extend(c.years)
            if c.unknown:
                result.unknown_artist = True
        elif isinstance(c, Flag):
            if c.flag == "explicit":
                result.explicit = True
            else:
                result.clean = True
        elif isinstance(c, Year):
            result.years.append(PositionedYear(c.year, group.pos))
        elif isinstance(c, Junk):
            result.junk.append(PositionedJunk(group.inner, c.junk_kind, group.pos))
        else:
            continue
        removed.add(group.pos)
    text = remove_groups(side, removed)

    # R7.2
    feat = find_feat_marker(text, ctx)
    main = trim_skel(slice_skel(text, 0, feat.start)) if feat is not None else trim_skel(text)
    main_list = split_credits(main, ListOptions("primary", "artist", None, True, False), ctx)
    result.credits.extend(main_list.credits)
    result.years.extend(main_list.years)
    if main_list.unknown:
        result.unknown_artist = True
    if feat is not None:
        featured = split_credits(
            trim_skel(slice_skel(text, feat.end)),
            ListOptions("featured", "artist", feat.marker, False, True),
            ctx,
        )
        result.credits.extend(featured.credits)
        result.years.extend(featured.years)
        if featured.unknown:
            result.unknown_artist = True
    result.credits.sort(key=lambda c: c.pos)
    return result
