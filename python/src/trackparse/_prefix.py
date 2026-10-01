"""R4: leading timestamp and track-position prefixes."""

from __future__ import annotations

from typing import NamedTuple, Optional

from ._context import Ctx, match_known_at
from ._scanner import has_spaced_dash, scan
from ._words import char_at, is_digit


class PrefixResult(NamedTuple):
    rest: str
    #: Absolute offset of ``rest`` (prefixes are ASCII, so code points == UTF-16 units).
    base: int
    timestamp: Optional[tuple[str, int]]
    position: Optional[tuple[str, int]]


def _digit_run(s: str, frm: int) -> int:
    i = frm
    while is_digit(char_at(s, i)):
        i += 1
    return i - frm


def _two_digits_up_to_59(s: str, at: int) -> Optional[int]:
    if (
        not is_digit(char_at(s, at))
        or not is_digit(char_at(s, at + 1))
        or is_digit(char_at(s, at + 2))
    ):
        return None
    n = int(s[at : at + 2])
    return n if n <= 59 else None


class _TimeMatch(NamedTuple):
    raw: str
    seconds: int
    end: int


def _match_time(s: str, at: int) -> Optional[_TimeMatch]:
    """``H:MM:SS``, ``HH:MM:SS``, ``M:SS``, ``MM:SS`` starting at ``at``."""
    lead = _digit_run(s, at)
    if lead < 1 or lead > 2 or char_at(s, at + lead) != ":":
        return None
    first = int(s[at : at + lead])
    mid = _two_digits_up_to_59(s, at + lead + 1)
    if mid is None:
        return None
    after_mid = at + lead + 3
    if char_at(s, after_mid) == ":":
        sec = _two_digits_up_to_59(s, after_mid + 1)
        if sec is not None:
            end = after_mid + 3
            return _TimeMatch(s[at:end], first * 3600 + mid * 60 + sec, end)
    if lead == 2 and first > 59:
        return None  # MM is 00–59
    return _TimeMatch(s[at:after_mid], first * 60 + mid, after_mid)


def _skip_space_and_dash(s: str, at: int) -> int:
    """After a prefix: skip one space, then an optional ``- `` (spaced dash)."""
    i = at
    if char_at(s, i) == " ":
        i += 1
    if char_at(s, i) == "-" and char_at(s, i + 1) == " ":
        i += 2
    return i


def _match_timestamp(s: str) -> Optional[tuple[tuple[str, int], int]]:
    """R4.1"""
    first = char_at(s, 0)
    bracket = "]" if first == "[" else ")" if first == "(" else None
    t = _match_time(s, 1 if bracket else 0)
    if t is None:
        return None
    end = t.end
    if bracket:
        if char_at(s, end) != bracket:
            return None
        end += 1
    if end < len(s) and s[end] != " ":
        return None
    return (t.raw, t.seconds), _skip_space_and_dash(s, end)


def _contains_spaced_dash(s: str) -> bool:
    return has_spaced_dash(scan(s).skel.text)


def _match_position(s: str, mode: str) -> Optional[int]:
    """R4.2: end offset of the position prefix, or None. Forms (a), (b), (c), (d)/(e)."""
    n = _digit_run(s, 0)
    if n < 1 or n > 3:
        return None
    c = char_at(s, n)
    zero_padded = s[0] == "0" and n >= 2
    # (a) `01. `, `1) `
    if (c == "." or c == ")") and (char_at(s, n + 1) == " " or n + 1 == len(s)):
        return _skip_space_and_dash(s, n + 1)
    # (b) `01 `: a position in every mode.
    if zero_padded and c == " ":
        return _skip_space_and_dash(s, n)
    # (c) `3 - Artist - Title`; in filename mode `N - ` is always a position (`311 - Amber.mp3`).
    if c == " " and char_at(s, n + 1) == "-" and char_at(s, n + 2) == " ":
        if mode == "filename" or _contains_spaced_dash(s[n + 3 :]):
            return n + 3
        return None
    if mode == "filename":
        # (e) `01-Track`, `03_name`, `01.Track`
        nxt = char_at(s, n + 1)
        if (c == "-" or c == "_" or c == ".") and nxt is not None and nxt != " ":
            return n + 1
        # (e) `3 My Song.mp3`: only when the remainder has no spaced dash.
        if c == " " and not _contains_spaced_dash(s[n + 1 :]):
            return _skip_space_and_dash(s, n)
    # (d) `2 Unlimited`, `50 Cent`: never a position outside filename mode.
    return None


def strip_prefixes(s: str, base: int, mode: str, ctx: Ctx) -> PrefixResult:
    rest = s
    offset = base
    timestamp: Optional[tuple[str, int]] = None
    position: Optional[tuple[str, int]] = None

    ts = _match_timestamp(rest)
    if ts is not None:
        timestamp = ts[0]
        rest = rest[ts[1] :]
        offset += ts[1]
    if match_known_at(rest, 0, ctx, "-") == 0:
        end = _match_position(rest, mode)
        if end is not None and end < len(rest):
            raw = rest[: _digit_run(rest, 0)]
            position = (raw, int(raw))
            rest = rest[end:]
            offset += end
    return PrefixResult(rest, offset, timestamp, position)
