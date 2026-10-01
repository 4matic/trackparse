"""R1: normalization."""

from __future__ import annotations

import unicodedata

from ._words import is_whitespace

# R1.2 invisible characters removed outright. ZWNJ/ZWJ (U+200C/U+200D) are kept (ZWJ emoji).
# R1.2: U+E000 is reserved as the R3.4 group placeholder, so it can never occur in input.
_REMOVED = frozenset(["​", "⁠", "﻿", "­", ""])

# R1.3 brackets, R1.4 quotes, R1.5 dashes → ASCII.
_CHAR_MAP: dict[str, str] = {
    "（": "(",
    "）": ")",
    "［": "[",
    "【": "[",
    "〔": "[",
    "］": "]",
    "】": "]",
    "〕": "]",
    "｛": "{",
    "｝": "}",
    "“": '"',
    "”": '"',
    "„": '"',
    "«": '"',
    "»": '"',
    "「": '"',
    "」": '"',
    "『": '"',
    "』": '"',
    "＂": '"',
    "‘": "'",
    "’": "'",
    "‚": "'",
    "`": "'",
    "´": "'",
    "−": "-",
    "﹘": "-",
    "﹣": "-",
    "－": "-",
}
# R1.5: U+2010–U+2015 → "-".
for _cp in range(0x2010, 0x2016):
    _CHAR_MAP[chr(_cp)] = "-"


def _collapse_spaced_dash_runs(chars: list[str]) -> list[str]:
    """R1.5 (second half): a run of 2+ ``-`` with whitespace on both sides becomes one ``-``."""
    out: list[str] = []
    i = 0
    n = len(chars)
    while i < n:
        if chars[i] != "-":
            out.append(chars[i])
            i += 1
            continue
        j = i
        while j < n and chars[j] == "-":
            j += 1
        spaced = (
            j - i >= 2
            and i > 0
            and j < n
            and is_whitespace(chars[i - 1])
            and is_whitespace(chars[j])
        )
        if spaced:
            out.append("-")
        else:
            out.extend("-" * (j - i))
        i = j
    return out


def _collapse_whitespace(chars: list[str]) -> str:
    """R1.6: every whitespace code point → one space, runs collapsed, trimmed."""
    out: list[str] = []
    pending = False
    for ch in chars:
        if is_whitespace(ch):
            pending = len(out) > 0
            continue
        if pending:
            out.append(" ")
        pending = False
        out.append(ch)
    return "".join(out)


def normalize(input: str) -> str:
    """R1: normalize a track string. Idempotent; total on any ``str``.

    Raises ``TypeError`` when ``input`` is not a ``str``.
    """
    if not isinstance(input, str):
        raise TypeError(f"normalize() expects str, got {type(input).__name__}")
    chars: list[str] = []
    for ch in unicodedata.normalize("NFC", input):
        if ch in _REMOVED:
            continue
        chars.append(_CHAR_MAP.get(ch, ch))
    # R1.8: removing invisibles can leave a decomposed sequence behind; re-compose so that
    # normalize() stays idempotent.
    return unicodedata.normalize("NFC", _collapse_whitespace(_collapse_spaced_dash_runs(chars)))
