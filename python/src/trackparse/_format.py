"""R11: render a ParsedTrack back to a string."""

from __future__ import annotations

from collections.abc import Sequence
from typing import NamedTuple, Optional

from ._assemble import render_version
from ._tables import Tables, get_default_tables
from ._words import dedup_key, word_matches
from .types import Artist, ParsedTrack


class _Resolved(NamedTuple):
    feat: object
    feat_marker: Optional[str]
    canonical: bool
    versions: bool
    producers: object
    position: object


def _is_feat_marker(raw: str, t: Tables) -> bool:
    return (
        t.feat_markers.match_at([raw], 0) is not None
        or t.bracket_only_feat.match_at([raw], 0) is not None
    )


def _canonical_joiner(raw: str, t: Tables, feat_lead: bool = False) -> str:
    """R11.5: map a raw joiner to its canonical form through data/joiners.json; feat markers
    become ``featCanonical``. A featured list's lead marker is always a feat marker (``with`` →
    ``feat.``), while between names the joiner table wins (``Sigala with Ella Eyre`` keeps
    ``with``)."""
    if feat_lead and _is_feat_marker(raw, t):
        return t.feat_canonical
    tight = t.tight_joiners.get(raw)
    if tight is not None:
        return tight
    for j in t.spaced_joiners:
        if (raw == j.raw) if j.case_sensitive else word_matches(raw, j.raw):
            return j.canonical
    return t.feat_canonical if _is_feat_marker(raw, t) else raw


def _join_sep(joiner: Optional[str], o: _Resolved, t: Tables) -> str:
    """R11.1: ``" " + joiner + " "``; tight joiners as the raw char + ``" "``; a missing joiner
    as ``&``."""
    if joiner is None:
        j = "&"
    elif o.canonical:
        j = _canonical_joiner(joiner, t)
    else:
        j = joiner
    return f"{j} " if j in t.tight_joiners else f" {j} "


def _join_names(lst: Sequence[Artist], o: _Resolved, t: Tables) -> str:
    """Names of one credit list joined by their own joiners (the first joiner is not
    rendered)."""
    return "".join(
        a.name if i == 0 else _join_sep(a.joiner, o, t) + a.name for i, a in enumerate(lst)
    )


def _lead_marker(first: Artist, o: _Resolved, t: Tables, fallback: str) -> str:
    """The marker in front of a featured list."""
    if o.feat_marker is not None:
        return o.feat_marker
    raw = first.joiner if first.joiner is not None else fallback
    return _canonical_joiner(raw, t, True) if o.canonical else raw


def format_track(
    track: ParsedTrack,
    *,
    feat: str = "source",
    feat_marker: Optional[str] = None,
    joiners: str = "original",
    versions: str = "all",
    producers: bool = True,
    position: bool = False,
) -> str:
    """R11: render ``track`` as ``Artists - Title (versions) (prod. ...)``."""
    t = get_default_tables()
    o = _Resolved(
        feat="source" if feat is None else feat,
        feat_marker=feat_marker,
        canonical=joiners == "canonical",
        versions=("all" if versions is None else versions) == "all",
        producers=True if producers is None else producers,
        position=False if position is None else position,
    )
    artists = list(track.artists)
    primaries = [a for a in artists if a.role == "primary"]
    featured = [a for a in artists if a.role == "featured"]
    producer_list = [a for a in artists if a.role == "producer"]

    artist_feat: list[Artist] = []
    title_feat: list[Artist] = []
    if o.feat == "source":
        artist_feat = [a for a in featured if a.source == "artist"]
        title_feat = [a for a in featured if a.source != "artist"]
    elif o.feat == "artist":
        artist_feat = featured
    elif o.feat == "title":
        title_feat = featured

    # R11.1 / R11.2
    artist_part = _join_names(primaries, o, t)
    if artist_feat:
        lead = _lead_marker(artist_feat[0], o, t, t.feat_canonical)
        names = _join_names(artist_feat, o, t)
        artist_part = f"{artist_part} {lead} {names}" if artist_part else f"{lead} {names}"

    # R11.3
    pieces: list[str] = []
    if track.title:
        pieces.append(track.title)
    if title_feat:
        lead = _lead_marker(title_feat[0], o, t, t.feat_canonical)
        pieces.append(f"({lead} {_join_names(title_feat, o, t)})")
    if o.versions:
        for v in track.versions:
            pieces.append(render_version(v.raw, v.delimiter))
    if o.producers and producer_list:
        first = producer_list[0]
        marker = first.joiner if first.joiner is not None else "prod."
        pieces.append(f"({marker} {_join_names(producer_list, o, t)})")
    title_part = " ".join(pieces)

    # R11.4
    result = f"{artist_part} - {title_part}" if artist_part else title_part
    if o.position and track.position is not None:
        result = f"{track.position.raw}. {result}"
    return result


def all_artists(track: ParsedTrack) -> list[Artist]:
    """All credited names: track artists plus every version's remixers, deduped by R0.4 key."""
    seen = set()
    out: list[Artist] = []
    for a in [*track.artists, *(x for v in track.versions for x in v.artists)]:
        key = dedup_key(a.name)
        if key in seen:
            continue
        seen.add(key)
        out.append(a)
    return out
