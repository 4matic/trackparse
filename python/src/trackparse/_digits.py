"""ASCII digit scanning shared by R4.1 (_prefix.py) and the tracklist time scanner (R13.2)."""

from __future__ import annotations

from typing import Optional

from ._words import char_at, is_digit


def digit_run(s: str, frm: int) -> int:
    """Length of the run of ASCII digits starting at ``frm``."""
    i = frm
    while is_digit(char_at(s, i)):
        i += 1
    return i - frm


def two_digits_up_to_59(s: str, at: int) -> Optional[int]:
    """Exactly two digits (not followed by a third) with a value of at most 59, else None."""
    if (
        not is_digit(char_at(s, at))
        or not is_digit(char_at(s, at + 1))
        or is_digit(char_at(s, at + 2))
    ):
        return None
    n = int(s[at : at + 2])
    return n if n <= 59 else None
