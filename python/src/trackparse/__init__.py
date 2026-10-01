"""trackparse: spec-driven parser for music track strings.

Behaviour is defined by spec/SPEC.md (https://github.com/4matic/trackparse/blob/main/spec/SPEC.md);
rule IDs (R6.2…) in comments refer to it. Results are frozen dataclasses with snake_case
attributes; ``to_dict()`` gives the spec's canonical camelCase JSON.
"""

from __future__ import annotations

from collections.abc import Sequence
from typing import Any, Optional

from ._data import SPEC_VERSION
from ._format import all_artists, format_track
from ._normalize import normalize
from ._parser import Parser, default_parser
from .options import FormatOptions, KeywordOptions, ParseOptions
from .types import (
    Artist,
    ArtistRole,
    ArtistSource,
    Flags,
    Junk,
    JunkKind,
    Mode,
    ModeOption,
    ParsedTrack,
    Position,
    Timestamp,
    Version,
    VersionDelimiter,
    VersionType,
    Warning,
)

__version__ = "0.1.0"  # x-release-please-version

__all__ = [
    "SPEC_VERSION",
    "Artist",
    "ArtistRole",
    "ArtistSource",
    "Flags",
    "FormatOptions",
    "Junk",
    "JunkKind",
    "KeywordOptions",
    "Mode",
    "ModeOption",
    "ParseOptions",
    "ParsedTrack",
    "Parser",
    "Position",
    "Timestamp",
    "Version",
    "VersionDelimiter",
    "VersionType",
    "Warning",
    "__version__",
    "all_artists",
    "create_parser",
    "format_track",
    "normalize",
    "parse",
    "parse_artists",
]


def parse(
    input: str,
    *,
    mode: ModeOption = "auto",
    uploader: Optional[str] = None,
    known_artists: Sequence[str] = (),
    split_and: str = "auto",
    keywords: Optional[KeywordOptions] = None,
) -> ParsedTrack:
    """Parse a track string into a :class:`ParsedTrack`.

    Total for any ``str`` (never raises on string input); raises ``TypeError`` for non-``str``.
    """
    return default_parser().parse(
        input,
        mode=mode,
        uploader=uploader,
        known_artists=known_artists,
        split_and=split_and,
        keywords=keywords,
    )


def parse_artists(
    input: str,
    *,
    mode: ModeOption = "auto",
    uploader: Optional[str] = None,
    known_artists: Sequence[str] = (),
    split_and: str = "auto",
    keywords: Optional[KeywordOptions] = None,
) -> list[Artist]:
    """Split an artist string into credits (R10). Every result has ``source == "artist"``."""
    return default_parser().parse_artists(
        input,
        mode=mode,
        uploader=uploader,
        known_artists=known_artists,
        split_and=split_and,
        keywords=keywords,
    )


def create_parser(**options: Any) -> Parser:
    """A :class:`Parser` with ``keywords`` compiled once.

    Accepts the same keyword arguments as :func:`parse`. Per-call options passed to
    ``Parser.parse`` / ``Parser.parse_artists`` are merged over these; ``None`` does not
    override, and per-call ``keywords`` are merged with the base ones.
    """
    return Parser(**options)
