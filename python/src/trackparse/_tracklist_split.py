"""R13.1: line split (LF, CRLF, CR only) with the lossless guarantee."""

from __future__ import annotations

from typing import NamedTuple

from ._tracklist_types import Eol


class Line(NamedTuple):
    raw: str
    eol: Eol


def split_lines(text: str) -> list[Line]:
    """R13.1.1: split at LF, CRLF and CR. U+2028/U+2029, U+0085 and other R0.2 characters are
    not line breaks. A trailing empty segment after the final terminator is not a line; ``""``
    has no lines. R13.1.2: ``"".join(l.raw + l.eol for l in lines) == text``."""
    lines: list[Line] = []
    start = 0
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "\n":
            lines.append(Line(text[start:i], "\n"))
            start = i + 1
        elif c == "\r":
            if i + 1 < n and text[i + 1] == "\n":
                lines.append(Line(text[start:i], "\r\n"))
                i += 1
            else:
                lines.append(Line(text[start:i], "\r"))
            start = i + 1
        i += 1
    if start < n:
        lines.append(Line(text[start:], ""))
    return lines


def has_line_break(s: str) -> bool:
    """A line break inside a single-line value (R13.9.5, R13.9.6)."""
    return "\n" in s or "\r" in s
