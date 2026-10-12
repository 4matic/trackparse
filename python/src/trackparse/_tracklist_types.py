"""Public tracklist types (SPEC.md R13). They mirror spec/schema/tracklist.schema.json;
``to_dict()`` gives the canonical JSON with the schema's key order."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Literal, Optional

from .types import ParsedTrack

EntryKind = Literal["track", "marker", "header", "link", "note", "blank"]
Eol = Literal["\n", "\r\n", "\r", ""]
TracklistWarningCode = Literal[
    "time-missing",
    "time-out-of-order",
    "time-duplicate",
    "no-start-at-zero",
    "time-after-audio-end",
    "invalid-time",
    "position-gap",
    "position-duplicate",
    "line-unparsed",
]
Severity = Literal["info", "warning"]
EditWarningCode = Literal["edit-invalid", "time-clamped", "style-upgraded"]

__all__ = [
    "EditResult",
    "EditWarning",
    "EditWarningCode",
    "EntryKind",
    "Eol",
    "Parts",
    "Severity",
    "Stats",
    "Suggestion",
    "TimeValue",
    "Tracklist",
    "TracklistEntry",
    "TracklistWarning",
    "TracklistWarningCode",
]


@dataclass(frozen=True)
class Parts:
    """R13.1.3: ``lead + time + gap + rest == raw``."""

    lead: str
    time: str
    gap: str
    rest: str

    def to_dict(self) -> dict[str, Any]:
        return {"lead": self.lead, "time": self.time, "gap": self.gap, "rest": self.rest}


@dataclass(frozen=True)
class TimeValue:
    """R13.2.5: ``raw`` without brackets; ``seconds`` may carry a fraction."""

    raw: str
    seconds: float

    def to_dict(self) -> dict[str, Any]:
        return {"raw": self.raw, "seconds": self.seconds}


@dataclass(frozen=True)
class TracklistEntry:
    #: 1-based line number.
    line: int
    kind: EntryKind
    eol: Eol
    raw: str
    parts: Parts
    time: Optional[TimeValue]
    #: The line's ParsedTrack (``track`` and ``marker`` entries only).
    track: Optional[ParsedTrack]

    def to_dict(self) -> dict[str, Any]:
        return {
            "line": self.line,
            "kind": self.kind,
            "eol": self.eol,
            "raw": self.raw,
            "parts": self.parts.to_dict(),
            "time": None if self.time is None else self.time.to_dict(),
            "track": None if self.track is None else self.track.to_dict(),
        }


@dataclass(frozen=True)
class Suggestion:
    """R13.7: a one- or two-digit fix for ``time-out-of-order``; ``line`` is the line to
    change."""

    line: int
    raw: str
    seconds: float

    def to_dict(self) -> dict[str, Any]:
        return {"line": self.line, "raw": self.raw, "seconds": self.seconds}


@dataclass(frozen=True)
class TracklistWarning:
    code: TracklistWarningCode
    line: int
    severity: Severity
    suggestion: Optional[Suggestion]

    def to_dict(self) -> dict[str, Any]:
        return {
            "code": self.code,
            "line": self.line,
            "severity": self.severity,
            "suggestion": None if self.suggestion is None else self.suggestion.to_dict(),
        }


@dataclass(frozen=True)
class Stats:
    lines: int
    tracks: int
    markers: int
    timed: int
    first_time: Optional[float]
    last_time: Optional[float]

    def to_dict(self) -> dict[str, Any]:
        return {
            "lines": self.lines,
            "tracks": self.tracks,
            "markers": self.markers,
            "timed": self.timed,
            "firstTime": self.first_time,
            "lastTime": self.last_time,
        }


@dataclass(frozen=True)
class Tracklist:
    """Output of :func:`trackparse.tracklist.parse_tracklist` (R13.8)."""

    spec_version: str
    entries: tuple[TracklistEntry, ...]
    warnings: tuple[TracklistWarning, ...]
    stats: Stats

    def to_dict(self) -> dict[str, Any]:
        """The canonical JSON form (camelCase keys), identical to the JS reference output."""
        return {
            "specVersion": self.spec_version,
            "entries": [e.to_dict() for e in self.entries],
            "warnings": [w.to_dict() for w in self.warnings],
            "stats": self.stats.to_dict(),
        }


@dataclass(frozen=True)
class EditWarning:
    code: EditWarningCode
    #: Index of the edit in the ``edits`` list.
    edit: int
    line: Optional[int]

    def to_dict(self) -> dict[str, Any]:
        return {"code": self.code, "edit": self.edit, "line": self.line}


@dataclass(frozen=True)
class EditResult:
    """Output of :func:`trackparse.tracklist.apply_edits` (R13.9)."""

    text: str
    #: Line numbers (in the final text) of lines an edit wrote; sorted, unique.
    changed: tuple[int, ...]
    warnings: tuple[EditWarning, ...]

    def to_dict(self) -> dict[str, Any]:
        return {
            "text": self.text,
            "changed": list(self.changed),
            "warnings": [w.to_dict() for w in self.warnings],
        }
