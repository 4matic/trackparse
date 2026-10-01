"""Option types. They mirror spec/schema/options.schema.json with snake_case keys.

Fixture JSON and the JS API use camelCase (``knownArtists``); the Python API takes keyword
arguments (``known_artists=...``) and a ``keywords`` dict with snake_case keys.
"""

from __future__ import annotations

from collections.abc import Sequence
from typing import Literal, TypedDict

from .types import JunkKind, ModeOption, VersionType

SplitAnd = Literal["auto", "always", "never"]
FeatPlacement = Literal["source", "artist", "title", "omit"]
JoinerStyle = Literal["original", "canonical"]
VersionsStyle = Literal["all", "none"]

__all__ = [
    "FeatPlacement",
    "FormatOptions",
    "JoinerStyle",
    "KeywordOptions",
    "ParseOptions",
    "SplitAnd",
    "VersionsStyle",
]


class KeywordOptions(TypedDict, total=False):
    """Additive vocabulary (R12). Keys are lowercased with ASCII rules."""

    #: Extra version heads: key -> version type.
    version_heads: dict[str, VersionType]
    descriptors: list[str]
    genres: list[str]
    #: Extra junk phrases: phrase -> kind.
    junk: dict[str, JunkKind]
    feat_markers: list[str]


class ParseOptions(TypedDict, total=False):
    """Keyword arguments accepted by ``parse``, ``parse_artists`` and ``create_parser``."""

    mode: ModeOption
    #: youtube mode: channel name used as the artist when no separator is found (R6.8).
    uploader: str
    #: Names that must never be split (R7.3a).
    known_artists: Sequence[str]
    split_and: SplitAnd
    keywords: KeywordOptions


class FormatOptions(TypedDict, total=False):
    """Keyword arguments accepted by ``format_track`` (R11)."""

    feat: FeatPlacement
    feat_marker: str
    joiners: JoinerStyle
    versions: VersionsStyle
    producers: bool
    position: bool
