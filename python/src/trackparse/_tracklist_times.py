"""R13.2: the tracklist time scanner (separate from R4.1) and R13.9.2 time formatting."""

from __future__ import annotations

import math
from typing import NamedTuple, Optional

from ._digits import digit_run
from ._tracklist_types import Parts, TimeValue
from ._words import char_at, is_digit, is_whitespace

#: JS ``Number.MAX_SAFE_INTEGER``.
MAX_SAFE_INTEGER = 2**53 - 1


class TimeStyle(NamedTuple):
    """The written shape of a time, kept by edits (R13.9.2)."""

    hours: bool
    #: Digits written for the hours (minimum width), 0 without hours.
    hour_width: int
    #: Digits written for the minutes (minimum width).
    minute_width: int
    #: R13.9.2: two-part time already at 60 minutes or more; it stays two-part when it grows.
    long_minutes: bool
    frac_sep: str
    frac_digits: int


class TimeScan(NamedTuple):
    parts: Parts
    time: Optional[TimeValue]
    style: Optional[TimeStyle]
    #: R13.2.4: a ``digits:digits`` shape after lead that fails R13.2.2.
    invalid: bool


def _is_gap_separator(ch: Optional[str]) -> bool:
    """R13.2.3 separators after the time."""
    return ch is not None and ch in ("-", "–", "—", "|", ".", ":")


def _skip_whitespace(s: str, frm: int) -> int:
    i = frm
    n = len(s)
    while i < n and is_whitespace(s[i]):
        i += 1
    return i


def _is_bullet(ch: Optional[str]) -> bool:
    """R13.2.1 bullets of a line without a time."""
    return ch == "-" or ch == "–" or ch == "—" or ch == "*" or ch == "•"


def _untimed(raw: str, invalid: bool) -> TimeScan:
    """R13.1.3 / R13.2.1: a line without a time. ``lead`` holds an optional bullet: R0.2
    whitespace, one of ``-`` ``–`` ``—`` ``*`` ``•``, then at least one R0.2 whitespace (all of
    it); ``rest`` is the remainder."""
    at = _skip_whitespace(raw, 0)
    lead_end = 0
    if _is_bullet(char_at(raw, at)) and is_whitespace(char_at(raw, at + 1)):
        lead_end = _skip_whitespace(raw, at + 1)
    return TimeScan(Parts(raw[:lead_end], "", "", raw[lead_end:]), None, None, invalid)


def seconds_of(whole: int, frac: str) -> float:
    """R13.2.2: ``whole.frac`` parsed as one decimal literal (correctly rounded, like JS
    ``Number()``), so every port gets the same double. Integral values stay ``int`` so the JSON
    form matches the JS reference (``7``, not ``7.0``)."""
    if len(frac) == 0:
        return whole
    v = float(f"{whole}.{frac}")
    return int(v) if v.is_integer() else v


def _shape_at(s: str, at: int) -> Optional[tuple[list[str], int]]:
    """R13.2.4: the maximal ``digits(:digits)+`` run at ``at`` (its digit runs and end)."""
    na = digit_run(s, at)
    if na == 0 or char_at(s, at + na) != ":" or not is_digit(char_at(s, at + na + 1)):
        return None
    parts = [s[at : at + na]]
    end = at + na
    while char_at(s, end) == ":" and is_digit(char_at(s, end + 1)):
        n = digit_run(s, end + 1)
        parts.append(s[end + 1 : end + 1 + n])
        end += 1 + n
    return parts, end


def shape_seconds(parts: list[str]) -> Optional[int]:
    """R13.2.2 validity: whole seconds of ``A:B`` / ``A:B:C`` (no other part count), or None."""
    if len(parts) < 2 or len(parts) > 3:
        return None
    a = parts[0]
    b = parts[1]
    if len(parts) == 3:
        c = parts[2]
        if len(a) < 1 or len(a) > 2 or len(b) != 2 or len(c) != 2:
            return None
        m = int(b)
        sec = int(c)
        if m > 59 or sec > 59:
            return None
        return int(a) * 3600 + m * 60 + sec
    if len(a) < 1 or len(a) > 3 or len(b) != 2:
        return None
    sec = int(b)
    if sec > 59:
        return None
    return int(a) * 60 + sec


def scan_time(raw: str) -> TimeScan:
    """R13.2: lead, time and gap at the start of ``raw``; the rest is everything after."""
    # R13.2.1 lead: whitespace, then an optional `[` / `(`.
    i = _skip_whitespace(raw, 0)
    first_ch = char_at(raw, i)
    closer = "]" if first_ch == "[" else ")" if first_ch == "(" else None
    if closer is not None:
        i += 1
    lead_end = i

    # R13.2.2 time
    shape = _shape_at(raw, i)
    if shape is None:
        return _untimed(raw, False)
    shape_parts, shape_end = shape
    whole = shape_seconds(shape_parts)
    if whole is None:
        return _untimed(raw, True)  # R13.2.4
    j = shape_end
    frac_sep = ""
    frac = ""
    sep = char_at(raw, j)
    if sep == "." or sep == ",":
        n = digit_run(raw, j + 1)
        if 1 <= n <= 3:
            frac_sep = sep
            frac = raw[j + 1 : j + 1 + n]
            j += 1 + n
    time_end = j

    # R13.2.3 gap
    if closer is not None:
        if char_at(raw, j) != closer:
            return _untimed(raw, False)
        j += 1
    elif j < len(raw) and not is_whitespace(raw[j]) and not _is_gap_separator(raw[j]):
        return _untimed(raw, False)
    j = _skip_whitespace(raw, j)
    if _is_gap_separator(char_at(raw, j)):
        j = _skip_whitespace(raw, j + 1)
    if raw.startswith("==>", j):
        j = _skip_whitespace(raw, j + 3)
    elif raw.startswith("=>", j):
        j = _skip_whitespace(raw, j + 2)

    time_raw = raw[lead_end:time_end]
    hours = len(shape_parts) == 3
    first = shape_parts[0]
    # R13.9.2 long-minute style: two parts with 60 minutes or more.
    long_minutes = not hours and int(first) >= 60
    if hours:
        minute_width = len(shape_parts[1])
    elif long_minutes:
        # R13.9.2: a long-minute time's minimum minute width is min(width, 2), so it never
        # keeps 3 minute digits once it is short again (`125:30` − 7000 s → `08:50`).
        minute_width = min(len(first), 2)
    else:
        minute_width = len(first)
    return TimeScan(
        Parts(raw[:lead_end], time_raw, raw[time_end:j], raw[j:]),
        TimeValue(time_raw, seconds_of(whole, frac)),
        TimeStyle(
            hours=hours,
            hour_width=len(first) if hours else 0,
            minute_width=minute_width,
            long_minutes=long_minutes,
            frac_sep=frac_sep,
            frac_digits=len(frac),
        ),
        False,
    )


def _pad(n: int, width: int) -> str:
    return str(n).rjust(width, "0")


def format_time(
    seconds: float, style: TimeStyle, one_digit_hours: bool
) -> Optional[tuple[str, bool]]:
    """R13.9.2: write ``seconds`` (finite, ≥ 0) in ``style`` → ``(raw, upgraded)``. Values are
    rounded half up to the fraction precision (``floor(v·10^d + 0.5)``). A value of an hour or
    more without hours is upgraded to ``HH:MM:SS`` (``H:MM:SS`` when ``one_digit_hours``),
    except in long-minute style. None when the value cannot be written as a valid R13.2.2 time
    (100 hours or more, more than 999 long-style minutes, units outside the safe integers)."""
    scale = 10**style.frac_digits
    x = float(seconds) * scale + 0.5
    if not math.isfinite(x):
        return None
    units = math.floor(x)
    if units > MAX_SAFE_INTEGER or units < 0:
        return None
    whole = units // scale
    frac_units = units - whole * scale
    frac = style.frac_sep + _pad(frac_units, style.frac_digits) if style.frac_digits > 0 else ""
    sec = whole % 60
    # The written time must stay valid under R13.2.2 (hours are 1–2 digits).
    if whole >= 100 * 3600:
        return None
    if style.hours:
        h = whole // 3600
        m = (whole % 3600) // 60
        return (
            f"{_pad(h, style.hour_width)}:{_pad(m, style.minute_width)}:{_pad(sec, 2)}{frac}",
            False,
        )
    if style.long_minutes:
        m = whole // 60
        if m > 999:
            return None  # R13.2.2: two-part minutes are 1–3 digits
        return f"{_pad(m, style.minute_width)}:{_pad(sec, 2)}{frac}", False
    if whole >= 3600:
        h = whole // 3600
        m = (whole % 3600) // 60
        return f"{_pad(h, 1 if one_digit_hours else 2)}:{_pad(m, 2)}:{_pad(sec, 2)}{frac}", True
    m = whole // 60
    return f"{_pad(m, style.minute_width)}:{_pad(sec, 2)}{frac}", False
