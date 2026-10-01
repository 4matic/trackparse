"""Pipeline: R1 → R2 → R4 → R3/R6 → R7 → R8 → R9, plus R10 parse_artists."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from dataclasses import replace
from typing import Any, Optional, cast

from ._artists import parse_artist_side
from ._assemble import by_pos, dedup_artists, full_title_of, to_version
from ._context import Ctx, warn
from ._mode import Piece, prelude
from ._normalize import normalize
from ._prefix import strip_prefixes
from ._scanner import Skel, scan, trim_skel
from ._separator import find_separator
from ._tables import Tables, js_entries, tables_for
from ._title import Extracted, add_peeled, parse_title_side
from ._words import r0_trim_end
from .options import KeywordOptions
from .types import (
    Artist,
    Flags,
    Junk,
    JunkKind,
    Mode,
    ParsedTrack,
    Position,
    Timestamp,
    Warning,
)

#: Offset used for uploader text so it sorts before everything from the input.
_UPLOADER_BASE = -1_000_000_000

_OPTION_KEYS = ("mode", "uploader", "known_artists", "split_and", "keywords")


def _skel_of(piece: Piece, ctx: Ctx) -> Skel:
    r = scan(piece.text, piece.base)
    if r.unbalanced:
        warn(ctx, "unbalancedBrackets")
    return trim_skel(r.skel)


def _uploader_skel(uploader: object) -> Optional[Skel]:
    """R6.8: uploader minus a trailing `` - Topic`` and ``VEVO``."""
    if not isinstance(uploader, str):
        return None
    u = normalize(uploader)
    if u.endswith(" - Topic"):
        u = u[: -len(" - Topic")]
    if u.endswith("VEVO"):
        u = r0_trim_end(u[: -len("VEVO")])
    if len(u) == 0:
        return None
    return scan(u, _UPLOADER_BASE).skel


def _empty_track(inp: str, mode: str) -> ParsedTrack:
    return ParsedTrack(
        input=inp,
        mode=cast(Mode, mode),
        position=None,
        timestamp=None,
        artists=(),
        title="",
        full_title="",
        versions=(),
        year=None,
        flags=Flags(),
        junk=(),
        warnings=(),
    )


def _run(inp: str, options: Mapping[str, Any], t: Tables) -> ParsedTrack:
    ctx = Ctx(t, options)
    norm = normalize(inp)
    requested = options.get("mode")
    if requested is None:
        requested = "auto"
    pre = prelude(norm, requested, ctx)
    if len(norm) == 0:
        return _empty_track(inp, pre.mode)  # R9.5

    # R4
    prefix = strip_prefixes(pre.working.text, pre.working.base, pre.mode, ctx)
    working = _skel_of(Piece(prefix.rest, prefix.base), ctx)

    out = Extracted()
    artist_skel: Optional[Skel]
    if pre.pipe_title is not None:
        # R2.4 step 3: `artist | title`, R6 skipped.
        artist_skel = working if len(working.text) > 0 else None
        title_skel = _skel_of(pre.pipe_title, ctx)
        relaxed = artist_skel is not None
    else:
        sep = find_separator(working, pre.mode, _uploader_skel(options.get("uploader")), ctx)
        add_peeled(out, sep.peeled)
        artist_skel = sep.artist
        title_skel = sep.title
        relaxed = sep.split

    artist_side = parse_artist_side(artist_skel, ctx) if artist_skel is not None else None
    title_side = parse_title_side(title_skel, relaxed, pre.mode == "youtube", out, ctx)

    # R9
    versions = by_pos(out.versions)
    for v in versions:
        if v.ambiguous_mix:
            warn(ctx, "ambiguousMixCredit")
    artist_credits = artist_side.credits if artist_side is not None else []
    artists = dedup_artists([*artist_credits, *by_pos(out.credits)])
    final_versions = tuple(to_version(v) for v in versions)
    # R9.6: the first year in text order.
    years = by_pos([*(artist_side.years if artist_side is not None else []), *out.years])
    junk = by_pos([*pre.junk, *(artist_side.junk if artist_side is not None else []), *out.junk])
    flags = Flags(
        explicit=out.explicit or (artist_side is not None and artist_side.explicit),
        clean=out.clean or (artist_side is not None and artist_side.clean),
        unknown_artist=(artist_side is not None and artist_side.unknown_artist)
        or out.unknown_artist,
        unknown_title=title_side.unknown_title,
    )
    ts = prefix.timestamp
    pos = prefix.position
    return ParsedTrack(
        input=inp,
        mode=cast(Mode, pre.mode),
        position=None if pos is None else Position(raw=pos[0], number=pos[1]),
        timestamp=None if ts is None else Timestamp(raw=ts[0], seconds=ts[1]),
        artists=tuple(artists),
        title=title_side.title,
        full_title=full_title_of(title_side.title, final_versions),
        versions=final_versions,
        year=years[0].year if years else None,
        flags=flags,
        junk=tuple(Junk(raw=j.raw, kind=cast(JunkKind, j.kind)) for j in junk),
        warnings=cast(tuple[Warning, ...], tuple(ctx.warnings)),
    )


def _run_artists(inp: str, options: Mapping[str, Any], t: Tables) -> list[Artist]:
    ctx = Ctx(t, options)
    norm = normalize(inp)
    if len(norm) == 0:
        return []
    side = parse_artist_side(scan(norm, 0).skel, ctx)
    return [replace(a, source="artist") for a in dedup_artists(side.credits)]


def _check_input(inp: object, fn: str) -> str:
    if not isinstance(inp, str):
        raise TypeError(f"{fn}() expects str input, got {type(inp).__name__}")
    return inp


def _safe_options(options: Mapping[str, Any]) -> dict[str, Any]:
    # A `None` per-call value must not override a create_parser() base option.
    return {k: v for k, v in options.items() if k in _OPTION_KEYS and v is not None}


def _merge_keywords(a: Mapping[str, Any], b: Mapping[str, Any]) -> dict[str, Any]:
    """Like the JS mergeKeywords: maps merged (later wins), lists concatenated."""

    def kw(o: Mapping[str, Any]) -> Mapping[str, Any]:
        k = o.get("keywords")
        return k if isinstance(k, Mapping) else {}

    def as_map(v: Any) -> dict[str, Any]:
        return dict(js_entries(v)) if isinstance(v, Mapping) else {}

    def as_list(v: Any) -> list[Any]:
        return list(v) if isinstance(v, (list, tuple)) else []

    x = kw(a)
    y = kw(b)
    return {
        "version_heads": {**as_map(x.get("version_heads")), **as_map(y.get("version_heads"))},
        "descriptors": [*as_list(x.get("descriptors")), *as_list(y.get("descriptors"))],
        "genres": [*as_list(x.get("genres")), *as_list(y.get("genres"))],
        "junk": {**as_map(x.get("junk")), **as_map(y.get("junk"))},
        "feat_markers": [*as_list(x.get("feat_markers")), *as_list(y.get("feat_markers"))],
    }


class Parser:
    """A parser with ``keywords`` compiled once. Per-call options are merged over the base
    options; a per-call ``None`` does not override a base value."""

    def __init__(self, **options: Any) -> None:
        self._base = _safe_options(options)
        self._tables = tables_for(self._base.get("keywords"))

    def _resolve(self, call: Mapping[str, Any]) -> tuple[dict[str, Any], Tables]:
        per_call = _safe_options(call)
        merged = {**self._base, **per_call}
        if per_call.get("keywords"):
            return merged, tables_for(_merge_keywords(self._base, per_call))
        return merged, self._tables

    def parse(
        self,
        input: str,
        *,
        mode: Optional[str] = None,
        uploader: Optional[str] = None,
        known_artists: Optional[Sequence[str]] = None,
        split_and: Optional[str] = None,
        keywords: Optional[KeywordOptions] = None,
    ) -> ParsedTrack:
        """Parse one track string (see :func:`trackparse.parse`)."""
        inp = _check_input(input, "parse")
        merged, tables = self._resolve(
            {
                "mode": mode,
                "uploader": uploader,
                "known_artists": known_artists,
                "split_and": split_and,
                "keywords": keywords,
            }
        )
        return _run(inp, merged, tables)

    def parse_artists(
        self,
        input: str,
        *,
        mode: Optional[str] = None,
        uploader: Optional[str] = None,
        known_artists: Optional[Sequence[str]] = None,
        split_and: Optional[str] = None,
        keywords: Optional[KeywordOptions] = None,
    ) -> list[Artist]:
        """Split an artist string into credits (R10)."""
        inp = _check_input(input, "parse_artists")
        merged, tables = self._resolve(
            {
                "mode": mode,
                "uploader": uploader,
                "known_artists": known_artists,
                "split_and": split_and,
                "keywords": keywords,
            }
        )
        return _run_artists(inp, merged, tables)


_default_parser: Optional[Parser] = None


def default_parser() -> Parser:
    global _default_parser
    if _default_parser is None:
        _default_parser = Parser()
    return _default_parser
