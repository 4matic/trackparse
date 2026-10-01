"""Low-level text helpers shared by every rule: the R0.2 whitespace set, R0.3 word keys,
R0.4 dedup keys, R0.6 letters/digits and token-sequence (phrase) matching.

Python strings are sequences of code points, so indices here are code-point indices (R0.5).
"""

from __future__ import annotations

import unicodedata
from collections.abc import Iterable, Mapping
from collections.abc import Set as AbstractSet
from typing import (
    Generic,
    NamedTuple,
    Optional,
    TypeVar,
)

V = TypeVar("V")


def is_whitespace_code(cp: int) -> bool:
    """R0.2: the exact whitespace code-point set. Never use ``str.isspace`` / ``\\s``."""
    return (
        (0x09 <= cp <= 0x0D)
        or cp == 0x20
        or cp == 0x85
        or cp == 0xA0
        or cp == 0x1680
        or (0x2000 <= cp <= 0x200A)
        or cp == 0x2028
        or cp == 0x2029
        or cp == 0x202F
        or cp == 0x205F
        or cp == 0x3000
    )


_WS = frozenset(
    chr(c)
    for c in [*range(9, 14), 32, 133, 160, 5760, *range(8192, 8203), 8232, 8233, 8239, 8287, 12288]
)


def is_whitespace(ch: Optional[str]) -> bool:
    """R0.2 test on one code point (``None`` / empty → False)."""
    return ch is not None and ch in _WS


def char_at(s: str, i: int) -> Optional[str]:
    """``s[i]`` or ``None`` when out of range (JS ``s[i]`` → ``undefined``; never negative)."""
    return s[i] if 0 <= i < len(s) else None


def r0_trim(s: str) -> str:
    """Trim R0.2 whitespace from both ends."""
    a = 0
    b = len(s)
    while a < b and s[a] in _WS:
        a += 1
    while b > a and s[b - 1] in _WS:
        b -= 1
    return s[a:b]


def r0_trim_end(s: str) -> str:
    b = len(s)
    while b > 0 and s[b - 1] in _WS:
        b -= 1
    return s[:b]


# JS String.prototype.trim(): WhiteSpace (incl. U+FEFF and Zs) + LineTerminator. Used only where
# the JS reference trims raw, unnormalized option strings (keyword keys), to stay byte-identical.
_JS_TRIM = frozenset(
    chr(c)
    for c in [
        9,
        10,
        11,
        12,
        13,
        32,
        160,
        5760,
        *range(8192, 8203),
        8232,
        8233,
        8239,
        8287,
        12288,
        65279,
    ]
)


def js_trim(s: str) -> str:
    a = 0
    b = len(s)
    while a < b and s[a] in _JS_TRIM:
        a += 1
    while b > a and s[b - 1] in _JS_TRIM:
        b -= 1
    return s[a:b]


def is_digit(ch: Optional[str]) -> bool:
    """R0.6: ASCII digits only."""
    return ch is not None and len(ch) == 1 and "0" <= ch <= "9"


def is_all_digits(s: str) -> bool:
    if len(s) == 0:
        return False
    return all("0" <= c <= "9" for c in s)


def is_letter(ch: Optional[str]) -> bool:
    """R0.6: Unicode general category L (``str.isalpha`` on one code point)."""
    return ch is not None and len(ch) == 1 and ch.isalpha()


_ASCII_LOWER = {c: c + 32 for c in range(65, 91)}


def ascii_lower(s: str) -> str:
    """ASCII-only lowercase (A–Z → a–z). Never ``str.lower()`` for word keys."""
    return s.translate(_ASCII_LOWER)


_KEY_STRIPPABLE = ".,!?:;"


def strip_key_punct(lower: str) -> str:
    """R0.3: the lowercased word with one trailing ``. , ! ? : ;`` removed."""
    return lower[:-1] if lower and lower[-1] in _KEY_STRIPPABLE else lower


def word_key(word: str) -> str:
    """R0.3 word key (stripped form)."""
    return strip_key_punct(ascii_lower(word))


def word_matches(word: str, token: str) -> bool:
    """R0.3: the unstripped lowercase form is compared first (entries like ``feat.``)."""
    lower = ascii_lower(word)
    return lower == token or strip_key_punct(lower) == token


def in_vocab(word: str, vocab: AbstractSet[str]) -> bool:
    lower = ascii_lower(word)
    return lower in vocab or strip_key_punct(lower) in vocab


def lookup_vocab(word: str, vocab: Mapping[str, V]) -> Optional[V]:
    lower = ascii_lower(word)
    v = vocab.get(lower)
    return v if v is not None else vocab.get(strip_key_punct(lower))


def collapse_spaces(s: str) -> str:
    """Collapse runs of R0.2 whitespace to one U+0020 and trim."""
    out: list[str] = []
    pending = False
    for ch in s:
        if ch in _WS:
            pending = len(out) > 0
        else:
            if pending:
                out.append(" ")
            pending = False
            out.append(ch)
    return "".join(out)


def split_words(s: str) -> list[str]:
    """R0.7: maximal runs of non-whitespace code points."""
    words: list[str] = []
    start = -1
    for i, ch in enumerate(s):
        if ch in _WS:
            if start >= 0:
                words.append(s[start:i])
            start = -1
        elif start < 0:
            start = i
    if start >= 0:
        words.append(s[start:])
    return words


class WordSpan(NamedTuple):
    """A word with its code-point span inside the string it was taken from."""

    text: str
    start: int
    end: int


def word_spans(s: str) -> list[WordSpan]:
    spans: list[WordSpan] = []
    start = -1
    for i, ch in enumerate(s):
        if ch in _WS:
            if start >= 0:
                spans.append(WordSpan(s[start:i], start, i))
            start = -1
        elif start < 0:
            start = i
    if start >= 0:
        spans.append(WordSpan(s[start:], start, len(s)))
    return spans


def dedup_key(s: str) -> str:
    """R0.4 dedup key: full lowercase → NFD → drop U+0300–U+036F → collapse/trim."""
    decomposed = unicodedata.normalize("NFD", s.lower())
    return collapse_spaces("".join(ch for ch in decomposed if not ("̀" <= ch <= "ͯ")))


def year_of(word: str) -> Optional[int]:
    """Year word: 4 ASCII digits in 1900–2099 (word key, so ``2015,`` counts)."""
    key = word_key(word)
    if len(key) != 4 or not is_all_digits(key):
        return None
    n = int(key)
    return n if 1900 <= n <= 2099 else None


def utf16_len(s: str) -> int:
    """Length in UTF-16 code units. Positions are kept in UTF-16 units so that every
    order-of-appearance comparison (R9) matches the JS reference exactly."""
    n = len(s)
    for ch in s:
        if ch > "￿":
            n += 1
    return n


# ---------------------------------------------------------------------------
# Phrase tables: multi-word vocabulary matched as token sequences, longest first.


class PhraseEntry(Generic[V]):
    __slots__ = ("key", "tokens", "value")

    def __init__(self, key: str, tokens: list[str], value: V) -> None:
        self.key = key
        self.tokens = tokens
        self.value = value


class PhraseMatch(Generic[V]):
    __slots__ = ("entry", "length")

    def __init__(self, entry: PhraseEntry[V], length: int) -> None:
        self.entry = entry
        #: Number of words consumed.
        self.length = length


class PhraseTable(Generic[V]):
    def __init__(self, entries: Iterable[tuple[str, V]]) -> None:
        self._by_first: dict[str, list[PhraseEntry[V]]] = {}
        self._by_last: dict[str, list[PhraseEntry[V]]] = {}
        self._keys: dict[str, V] = {}
        for raw_key, value in entries:
            self.add(raw_key, value)

    def add(self, raw_key: str, value: V) -> None:
        key = js_trim(ascii_lower(raw_key))
        tokens = split_words(key)
        if not tokens or key in self._keys:
            return
        self._keys[key] = value
        entry = PhraseEntry(key, tokens, value)
        _insert_sorted(self._by_first, tokens[0], entry)
        _insert_sorted(self._by_last, tokens[-1], entry)

    def has(self, key: str) -> bool:
        return key in self._keys

    def match_at(self, words: list[str], start: int) -> Optional[PhraseMatch[V]]:
        """Longest entry matching ``words[start …]``."""
        if not 0 <= start < len(words):
            return None
        best: Optional[PhraseMatch[V]] = None
        for entry in _candidates(self._by_first, words[start]):
            n = len(entry.tokens)
            if best is not None and n <= best.length:
                continue
            if start + n > len(words):
                continue
            if _tokens_match(words, start, entry.tokens):
                best = PhraseMatch(entry, n)
        return best

    def match_ending(self, words: list[str], end: int) -> Optional[PhraseMatch[V]]:
        """Longest entry matching ``words[… end-1]`` (end exclusive)."""
        if not 0 <= end - 1 < len(words):
            return None
        best: Optional[PhraseMatch[V]] = None
        for entry in _candidates(self._by_last, words[end - 1]):
            n = len(entry.tokens)
            if best is not None and n <= best.length:
                continue
            if end - n < 0:
                continue
            if _tokens_match(words, end - n, entry.tokens):
                best = PhraseMatch(entry, n)
        return best

    def match_whole(self, words: list[str]) -> Optional[PhraseMatch[V]]:
        """The whole word list is exactly one entry."""
        m = self.match_at(words, 0)
        return m if m is not None and m.length == len(words) else None


def _insert_sorted(m: dict[str, list[PhraseEntry[V]]], k: str, e: PhraseEntry[V]) -> None:
    lst = m.setdefault(k, [])
    lst.append(e)
    lst.sort(key=lambda x: -len(x.tokens))  # stable, like the JS sort


def _candidates(m: dict[str, list[PhraseEntry[V]]], word: str) -> list[PhraseEntry[V]]:
    lower = ascii_lower(word)
    stripped = strip_key_punct(lower)
    a = m.get(lower, [])
    if stripped == lower:
        return a
    b = m.get(stripped, [])
    if not a:
        return b
    if not b:
        return a
    return a + b


def _tokens_match(words: list[str], start: int, tokens: list[str]) -> bool:
    for k, t in enumerate(tokens):
        i = start + k
        if i >= len(words) or not word_matches(words[i], t):
            return False
    return True
