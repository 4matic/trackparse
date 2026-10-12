"""R13.3 line kinds and R13.4.2 candidate lines."""

from __future__ import annotations

from typing import Optional

from ._data import HEADER_WORDS_DATA, MARKER_WORDS_DATA
from ._normalize import normalize
from ._tables import _str_list
from ._words import PhraseTable, ascii_lower, in_vocab, is_whitespace, split_words
from .types import ParsedTrack

_HEADER_WORDS: frozenset[str] = frozenset(HEADER_WORDS_DATA["words"])
_default_markers: Optional[PhraseTable[bool]] = None


def marker_table(extra: object) -> PhraseTable[bool]:
    """``data/marker-words.json`` plus ``marker_words`` (R13.10)."""
    global _default_markers
    more = _str_list(extra)
    if len(more) == 0:
        if _default_markers is None:
            _default_markers = PhraseTable((w, True) for w in MARKER_WORDS_DATA["words"])
        return _default_markers
    return PhraseTable((w, True) for w in [*MARKER_WORDS_DATA["words"], *more])


def is_blank(raw: str) -> bool:
    """R13.3.1"""
    return all(is_whitespace(c) for c in raw)


def _trim_ws(s: str) -> str:
    a = 0
    b = len(s)
    while a < b and is_whitespace(s[a]):
        a += 1
    while b > a and is_whitespace(s[b - 1]):
        b -= 1
    return s[a:b]


def is_link(rest: str) -> bool:
    """R13.3.3 link: ``http://``, ``https://`` or ``www.`` (ASCII case-insensitive)."""
    t = ascii_lower(_trim_ws(rest)[:8])
    return t.startswith("http://") or t.startswith("https://") or t.startswith("www.")


def is_header(rest: str) -> bool:
    """R13.3.3 header: ends with ``:``, or a header word followed by at most one more word."""
    t = _trim_ws(rest)
    if t.endswith(":"):
        return True
    words = split_words(t)
    return 1 <= len(words) <= 2 and in_vocab(words[0], _HEADER_WORDS)


def has_marker_word(track_part: str, markers: PhraseTable[bool]) -> bool:
    """R13.3.2: any word (or multi-word entry) of the track part is a marker word."""
    words = split_words(track_part)
    return any(markers.match_at(words, i) is not None for i in range(len(words)))


def has_separator(track: ParsedTrack) -> bool:
    """The track part has an artist/title separator: its parse did not warn ``noSeparator``."""
    return "noSeparator" not in track.warnings and len(normalize(track.input)) > 0
