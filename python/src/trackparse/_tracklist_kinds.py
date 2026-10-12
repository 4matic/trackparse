"""R13.3 line kinds and R13.4.2 candidate lines."""

from __future__ import annotations

from typing import Optional

from ._data import HEADER_WORDS_DATA, MARKER_WORDS_DATA
from ._tables import _str_list
from ._words import PhraseTable, ascii_lower, in_vocab, is_whitespace, split_words
from .types import ParsedTrack

_HEADER_WORDS: frozenset[str] = frozenset(HEADER_WORDS_DATA["words"])
_default_markers: Optional[PhraseTable[bool]] = None


def marker_table(extra: object) -> PhraseTable[bool]:
    """``data/marker-words.json`` plus ``marker_words`` (R13.10). Entries are split into words
    the same way as the track part (R13.3.5), so ``shout-out`` matches the words ``shout``,
    ``out``."""
    global _default_markers
    more = _str_list(extra)
    if len(more) == 0:
        if _default_markers is None:
            _default_markers = PhraseTable(
                (" ".join(marker_words(w)), True) for w in MARKER_WORDS_DATA["words"]
            )
        return _default_markers
    return PhraseTable(
        (" ".join(marker_words(w)), True) for w in [*MARKER_WORDS_DATA["words"], *more]
    )


_BRACKETS = "()[]{}"


def _strip_brackets(word: str) -> str:
    a = 0
    b = len(word)
    while a < b and word[a] in _BRACKETS:
        a += 1
    while b > a and word[b - 1] in _BRACKETS:
        b -= 1
    return word[a:b]


def marker_words(text: str) -> list[str]:
    """R13.3.5 marker words: split on R0.2 whitespace, ``/`` and ``-``, then strip leading and
    trailing ``()[]{}`` from each piece; empty pieces are dropped. Word keys (R0.3) are applied
    on matching."""
    out: list[str] = []
    current: list[str] = []
    for ch in text:
        if ch == "/" or ch == "-" or is_whitespace(ch):
            word = _strip_brackets("".join(current))
            if word:
                out.append(word)
            current = []
        else:
            current.append(ch)
    word = _strip_brackets("".join(current))
    if word:
        out.append(word)
    return out


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
    """R13.3.5: any marker word (or multi-word entry) among the track part's marker words."""
    words = marker_words(track_part)
    return any(markers.match_at(words, i) is not None for i in range(len(words)))


def has_separator(track: ParsedTrack, empty: bool) -> bool:
    """R13.3.4: the track part has a separator: it is not empty after R1 (``empty``), its parse
    did not warn ``noSeparator``, and the artist side is not empty (at least one primary artist,
    or ``flags.unknown_artist``)."""
    if empty or "noSeparator" in track.warnings:
        return False
    return track.flags.unknown_artist or any(a.role == "primary" for a in track.artists)
