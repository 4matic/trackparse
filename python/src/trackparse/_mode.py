"""R2: mode resolution, platform suffixes, filename handling and youtube pipes."""

from __future__ import annotations

from typing import NamedTuple, Optional

from ._classify import junk_kind_of
from ._context import Ctx, warn
from ._scanner import (
    Segment,
    Skel,
    groups_of,
    has_spaced_dash,
    render_skel,
    scan,
    slice_skel,
    start_pos,
)
from ._words import ascii_lower, collapse_spaces, r0_trim, split_words, utf16_len, word_spans


class PositionedJunk(NamedTuple):
    raw: str
    kind: str
    pos: int


class Piece(NamedTuple):
    """A contiguous piece of the normalized string and its absolute offset."""

    text: str
    base: int


class Prelude(NamedTuple):
    mode: str
    #: The working string (R2.4 step 2/4), or the artist piece in the `artist | title` case.
    working: Piece
    #: R2.4 step 3: the title piece when two dash-less pipe segments remain.
    pipe_title: Optional[Piece]
    junk: list[PositionedJunk]


def _strip_platform_suffixes(s: str, ctx: Ctx, junk: list[PositionedJunk]) -> str:
    """R2.2: strip trailing platform suffixes (longest first, repeatedly)."""
    stripped = True
    while stripped:
        stripped = False
        lower = ascii_lower(s)
        for suffix in ctx.t.platform_suffixes:
            if not lower.endswith(ascii_lower(suffix)):
                continue
            start = len(s) - len(suffix)
            junk.append(PositionedJunk(s[start + 3 :], "platform", utf16_len(s[:start]) + 3))
            s = s[:start]
            stripped = True
            break
    return s


def _extension_of(s: str, ctx: Ctx) -> Optional[str]:
    dot = s.rfind(".")
    if dot < 0:
        return None
    ext = ascii_lower(s[dot + 1 :])
    return ext if ext in ctx.t.extensions else None


def split_pipes(skel: Skel) -> list[Segment]:
    """Top-level `` | `` / `` || `` separators on a skeleton → trimmed segment ranges."""
    t = skel.text
    n = len(t)
    cuts: list[tuple[int, int]] = []
    i = 1
    while i < n:
        if t[i - 1] == " " and t[i] == "|":
            bars = 2 if i + 1 < n and t[i + 1] == "|" else 1
            if i + bars < n and t[i + bars] == " ":
                cuts.append((i - 1, i + bars + 1))
                i += bars
        i += 1
    segments: list[Segment] = []
    start = 0
    for a, b in [*cuts, (n, n)]:
        s = start
        e = a
        while s < e and t[s] == " ":
            s += 1
        while e > s and t[e - 1] == " ":
            e -= 1
        segments.append(Segment(slice_skel(skel, s, e), s, e))
        start = b
    return segments


class TrailingJunk(NamedTuple):
    start: int
    kind: str


def trailing_junk_phrase(text: str, ctx: Ctx) -> Optional[TrailingJunk]:
    """R8.4 test: the skeleton ends with an unbracketed junk phrase (genres excluded)."""
    spans = word_spans(text)
    m = ctx.t.junk_phrases.match_ending([s.text for s in spans], len(spans))
    if m is None:
        return None
    return TrailingJunk(spans[len(spans) - m.length].start, m.entry.value)


def _is_junk_text(text: str, ctx: Ctx) -> Optional[str]:
    words = split_words(text)
    return junk_kind_of(words, ctx) if words else None


def _looks_like_youtube(skel: Skel, ctx: Ctx) -> bool:
    """R2.1 youtube signals (besides platform suffixes)."""
    if len(split_pipes(skel)) > 1:
        return True
    for _, group in groups_of(skel):
        kind = _is_junk_text(group.inner, ctx)
        if kind and kind != "genre":
            return True
    return trailing_junk_phrase(skel.text, ctx) is not None


def prelude(normalized: str, requested: object, ctx: Ctx) -> Prelude:
    junk: list[PositionedJunk] = []
    s = _strip_platform_suffixes(normalized, ctx, junk)
    had_platform = len(junk) > 0

    if requested in ("clean", "youtube", "filename"):
        mode = str(requested)
    elif _extension_of(s, ctx):
        mode = "filename"
    else:
        mode = "clean"

    if mode == "filename":
        # R2.3: strip the extension, trim, then `_` → space when there is no space.
        if _extension_of(s, ctx):
            s = s[: s.rfind(".")]
        s = r0_trim(s)
        if " " not in s and "_" in s:
            s = r0_trim(s.replace("_", " "))

    skel, unbalanced = scan(s, 0)
    if unbalanced:
        warn(ctx, "unbalancedBrackets")
    if (
        (requested == "auto" or requested is None)
        and mode == "clean"
        and (had_platform or _looks_like_youtube(skel, ctx))
    ):
        mode = "youtube"

    whole = Piece(s, 0)
    if mode != "youtube":
        return Prelude(mode, whole, None, junk)
    working, pipe_title = _split_youtube_pipes(skel, ctx, junk)
    return Prelude(mode, working, pipe_title, junk)


def _piece_of(seg: Segment) -> Piece:
    return Piece(render_skel(seg.skel), start_pos(seg.skel))


def _split_youtube_pipes(
    skel: Skel, ctx: Ctx, junk: list[PositionedJunk]
) -> tuple[Piece, Optional[Piece]]:
    """R2.4"""
    segments = split_pipes(skel)
    if len(segments) <= 1:
        return Piece(render_skel(skel), 0), None

    remaining: list[Segment] = []
    for seg in segments:
        text = collapse_spaces(render_skel(seg.skel))
        # R2.4 step 1: a segment with a spaced dash is an artist/title candidate, never junk.
        kind = None if has_spaced_dash(seg.skel.text) else _is_junk_text(text, ctx)
        if kind:
            junk.append(PositionedJunk(text, kind, start_pos(seg.skel)))
        elif len(text) > 0:
            remaining.append(seg)

    def push_other(keep: Optional[Segment], title: Optional[Segment] = None) -> None:
        for seg in remaining:
            if seg is keep or seg is title:
                continue
            junk.append(
                PositionedJunk(collapse_spaces(render_skel(seg.skel)), "other", start_pos(seg.skel))
            )

    with_dash = next((seg for seg in remaining if has_spaced_dash(seg.skel.text)), None)
    if with_dash is not None:
        push_other(with_dash)
        return _piece_of(with_dash), None
    if len(remaining) == 2:
        warn(ctx, "ambiguousSeparator")
        return _piece_of(remaining[0]), _piece_of(remaining[1])
    if remaining:
        push_other(remaining[0])
        return _piece_of(remaining[0]), None
    # Every segment was junk: nothing left to parse.
    return Piece("", 0), None
