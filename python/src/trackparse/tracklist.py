"""trackparse.tracklist: multi-line tracklists (SPEC.md R13): parse_tracklist and apply_edits.

Rule IDs (R13.2…) in comments refer to spec/SPEC.md. Results are frozen dataclasses;
``to_dict()`` gives the spec's canonical camelCase JSON (spec/schema/tracklist.schema.json).
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any, Optional

from ._tracklist_edits import apply_edits_list
from ._tracklist_options import resolve_options
from ._tracklist_parse import parse_tracklist_resolved
from ._tracklist_types import (
    EditResult,
    EditWarning,
    EditWarningCode,
    EntryKind,
    Eol,
    Parts,
    Severity,
    Stats,
    Suggestion,
    TimeValue,
    Tracklist,
    TracklistEntry,
    TracklistWarning,
    TracklistWarningCode,
)
from .options import KeywordOptions
from .types import ModeOption

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
    "apply_edits",
    "parse_tracklist",
]


def _check_text(text: object, fn: str) -> str:
    if not isinstance(text, str):
        raise TypeError(f"{fn}() expects str input, got {type(text).__name__}")
    return text


def parse_tracklist(
    text: str,
    *,
    mode: ModeOption = "tracklist",
    known_artists: Sequence[str] = (),
    keywords: Optional[KeywordOptions] = None,
    split_and: str = "auto",
    audio_seconds: Optional[float] = None,
    marker_words: Sequence[str] = (),
) -> Tracklist:
    """Parse a block of text line by line (R13.1–R13.8).

    Total for any ``str`` (never raises on string input; bad option values are ignored, R13.10);
    raises ``TypeError`` for non-``str`` text.
    """
    opts = resolve_options(mode, known_artists, keywords, split_and, audio_seconds, marker_words)
    return parse_tracklist_resolved(_check_text(text, "parse_tracklist"), opts)


def apply_edits(
    text: str,
    edits: Sequence[Mapping[str, Any]],
    *,
    mode: ModeOption = "tracklist",
    known_artists: Sequence[str] = (),
    keywords: Optional[KeywordOptions] = None,
    split_and: str = "auto",
    audio_seconds: Optional[float] = None,
    marker_words: Sequence[str] = (),
) -> EditResult:
    """Apply ``edits`` (dicts with the spec's camelCase keys: ``op``, ``line``, ``seconds``,
    ``lines``, ``from``, ``after``, ``text``, ``rest``) in order (R13.9).

    Never raises for ``str`` text and any edit list: an invalid edit changes nothing and warns
    ``edit-invalid`` (R13.9.6). The options are accepted for symmetry with
    :func:`parse_tracklist`; edits do not depend on them (R13.10). Raises ``TypeError`` for
    non-``str`` text.
    """
    del mode, known_artists, keywords, split_and, audio_seconds, marker_words
    return apply_edits_list(_check_text(text, "apply_edits"), edits)
