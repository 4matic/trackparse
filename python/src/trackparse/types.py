"""Public result types. They mirror spec/schema/parsed-track.schema.json.

Every class is a frozen dataclass with snake_case attributes. Sequences are stored as tuples so
results are deeply immutable and hashable. ``to_dict()`` returns the spec's canonical JSON form
(camelCase keys, lists, key order as in the schema), identical to the JS reference output;
``from_dict()`` builds an instance back from that form.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Literal, Optional, cast

Mode = Literal["clean", "youtube", "filename"]
ModeOption = Literal["auto", "clean", "youtube", "filename"]
ArtistRole = Literal["primary", "featured", "remixer", "producer"]
ArtistSource = Literal["artist", "title", "version"]
VersionType = Literal[
    "remix",
    "bootleg",
    "vip",
    "edit",
    "flip",
    "refix",
    "rework",
    "mashup",
    "blend",
    "dub",
    "mix",
    "extended",
    "radio",
    "club",
    "original",
    "instrumental",
    "acapella",
    "live",
    "acoustic",
    "remaster",
    "demo",
    "reprise",
    "cover",
    "version",
    "spedUp",
    "slowed",
    "nightcore",
]
VersionDelimiter = Literal["(", "[", "{", "-"]
JunkKind = Literal[
    "video", "audio", "lyrics", "quality", "promo", "platform", "label", "genre", "other"
]
Warning = Literal[
    "noSeparator",
    "ambiguousSeparator",
    "unspacedDashSplit",
    "asymmetricDashSplit",
    "bySplit",
    "quotedTitleSplit",
    "ambiguousMixCredit",
    "unbalancedBrackets",
]

__all__ = [
    "Artist",
    "ArtistRole",
    "ArtistSource",
    "Flags",
    "Junk",
    "JunkKind",
    "Mode",
    "ModeOption",
    "ParsedTrack",
    "Position",
    "Timestamp",
    "Version",
    "VersionDelimiter",
    "VersionType",
    "Warning",
]


@dataclass(frozen=True)
class Artist:
    """One credited name."""

    name: str
    role: ArtistRole
    #: Raw joiner / marker text that preceded the name in its credit list; ``None`` for the first.
    joiner: Optional[str]
    source: ArtistSource

    def to_dict(self) -> dict[str, Any]:
        return {"name": self.name, "role": self.role, "joiner": self.joiner, "source": self.source}

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Artist:
        return cls(name=d["name"], role=d["role"], joiner=d["joiner"], source=d["source"])


@dataclass(frozen=True)
class Version:
    """A recognised version/remix group or dash suffix."""

    type: VersionType
    raw: str
    artists: tuple[Artist, ...]
    modifiers: tuple[str, ...]
    descriptor: Optional[str]
    year: Optional[int]
    unknown_artist: bool
    delimiter: VersionDelimiter

    def to_dict(self) -> dict[str, Any]:
        return {
            "type": self.type,
            "raw": self.raw,
            "artists": [a.to_dict() for a in self.artists],
            "modifiers": list(self.modifiers),
            "descriptor": self.descriptor,
            "year": self.year,
            "unknownArtist": self.unknown_artist,
            "delimiter": self.delimiter,
        }

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Version:
        return cls(
            type=d["type"],
            raw=d["raw"],
            artists=tuple(Artist.from_dict(a) for a in d["artists"]),
            modifiers=tuple(d["modifiers"]),
            descriptor=d["descriptor"],
            year=d["year"],
            unknown_artist=d["unknownArtist"],
            delimiter=d["delimiter"],
        )


@dataclass(frozen=True)
class Junk:
    """Removed noise with its kind."""

    raw: str
    kind: JunkKind

    def to_dict(self) -> dict[str, Any]:
        return {"raw": self.raw, "kind": self.kind}

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Junk:
        return cls(raw=d["raw"], kind=d["kind"])


@dataclass(frozen=True)
class Flags:
    explicit: bool = False
    clean: bool = False
    unknown_artist: bool = False
    unknown_title: bool = False

    def to_dict(self) -> dict[str, Any]:
        return {
            "explicit": self.explicit,
            "clean": self.clean,
            "unknownArtist": self.unknown_artist,
            "unknownTitle": self.unknown_title,
        }

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Flags:
        return cls(
            explicit=d["explicit"],
            clean=d["clean"],
            unknown_artist=d["unknownArtist"],
            unknown_title=d["unknownTitle"],
        )


@dataclass(frozen=True)
class Position:
    """Track number prefix (R4.2)."""

    raw: str
    number: int

    def to_dict(self) -> dict[str, Any]:
        return {"raw": self.raw, "number": self.number}

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Position:
        return cls(raw=d["raw"], number=d["number"])


@dataclass(frozen=True)
class Timestamp:
    """Leading cue time (R4.1)."""

    raw: str
    seconds: int

    def to_dict(self) -> dict[str, Any]:
        return {"raw": self.raw, "seconds": self.seconds}

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Timestamp:
        return cls(raw=d["raw"], seconds=d["seconds"])


@dataclass(frozen=True)
class ParsedTrack:
    """Output of :func:`trackparse.parse`. See SPEC.md "Output model"."""

    input: str
    mode: Mode
    position: Optional[Position]
    timestamp: Optional[Timestamp]
    artists: tuple[Artist, ...]
    title: str
    full_title: str
    versions: tuple[Version, ...]
    year: Optional[int]
    flags: Flags
    junk: tuple[Junk, ...]
    warnings: tuple[Warning, ...]

    def to_dict(self) -> dict[str, Any]:
        """The canonical JSON form (camelCase keys), identical to the JS reference output."""
        return {
            "input": self.input,
            "mode": self.mode,
            "position": None if self.position is None else self.position.to_dict(),
            "timestamp": None if self.timestamp is None else self.timestamp.to_dict(),
            "artists": [a.to_dict() for a in self.artists],
            "title": self.title,
            "fullTitle": self.full_title,
            "versions": [v.to_dict() for v in self.versions],
            "year": self.year,
            "flags": self.flags.to_dict(),
            "junk": [j.to_dict() for j in self.junk],
            "warnings": list(self.warnings),
        }

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> ParsedTrack:
        position = d["position"]
        timestamp = d["timestamp"]
        return cls(
            input=d["input"],
            mode=d["mode"],
            position=None if position is None else Position.from_dict(position),
            timestamp=None if timestamp is None else Timestamp.from_dict(timestamp),
            artists=tuple(Artist.from_dict(a) for a in d["artists"]),
            title=d["title"],
            full_title=d["fullTitle"],
            versions=tuple(Version.from_dict(v) for v in d["versions"]),
            year=d["year"],
            flags=Flags.from_dict(d["flags"]),
            junk=tuple(Junk.from_dict(j) for j in d["junk"]),
            warnings=cast(tuple[Warning, ...], tuple(d["warnings"])),
        )
