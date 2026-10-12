"""R13.10: tracklist options, sanitised so bad values are ignored (R0.8)."""

from __future__ import annotations

import math
from collections.abc import Mapping
from typing import Any, NamedTuple, Optional

_MODES = frozenset(("auto", "clean", "youtube", "filename", "tracklist"))
_SPLIT_AND = frozenset(("auto", "always", "never"))


class ResolvedOptions(NamedTuple):
    #: Options for the per-line parse (``_parser.ListParser``), snake_case keys.
    parse: dict[str, Any]
    audio_seconds: Optional[float]
    marker_words: Any


def js_number(value: object) -> Optional[float]:
    """A JS ``number`` view of ``value``: ``None`` unless it is an int or float (never a bool).
    Ints too large for a double become ±inf, as a JS number literal would."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    try:
        return float(value)
    except OverflowError:
        return math.inf if value > 0 else -math.inf


def resolve_options(
    mode: object,
    known_artists: object,
    keywords: object,
    split_and: object,
    audio_seconds: object,
    marker_words: object,
) -> ResolvedOptions:
    parse: dict[str, Any] = {
        "mode": mode if isinstance(mode, str) and mode in _MODES else "tracklist",
    }
    if isinstance(known_artists, (list, tuple)):
        parse["known_artists"] = known_artists
    if isinstance(split_and, str) and split_and in _SPLIT_AND:
        parse["split_and"] = split_and
    if isinstance(keywords, Mapping):
        parse["keywords"] = keywords
    audio = js_number(audio_seconds)
    return ResolvedOptions(
        parse=parse,
        audio_seconds=audio if audio is not None and math.isfinite(audio) and audio >= 0 else None,
        marker_words=marker_words,
    )
