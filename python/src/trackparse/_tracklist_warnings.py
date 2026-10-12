"""R13.7 tracklist warnings and the ``time-out-of-order`` suggestion search."""

from __future__ import annotations

from typing import Optional, Union

from ._normalize import normalize
from ._tracklist_times import seconds_of
from ._tracklist_types import (
    Severity,
    Suggestion,
    TimeValue,
    TracklistEntry,
    TracklistWarning,
    TracklistWarningCode,
)
from ._words import is_digit
from .types import ParsedTrack

#: Table order of R13.7; warnings of one line are emitted in this order.
_ORDER: tuple[TracklistWarningCode, ...] = (
    "time-missing",
    "time-out-of-order",
    "time-duplicate",
    "no-start-at-zero",
    "time-after-audio-end",
    "invalid-time",
    "position-gap",
    "position-duplicate",
    "line-unparsed",
)
_RANK = {c: i for i, c in enumerate(_ORDER)}


class _Slot:
    __slots__ = ("at", "digit", "field", "place")

    def __init__(self, at: int, field: int, digit: int) -> None:
        #: Index of the digit in ``raw``.
        self.at = at
        self.field = field
        self.place = 0
        self.digit = digit


#: Outcome of one line's digit search: None (no candidate), one TimeValue, or "many".
Search = Union[None, TimeValue, str]
_MANY = "many"


def search_time(raw: str, lo: Optional[float], hi: Optional[float]) -> Search:
    """R13.7 suggestion search on one raw time: change 1, then 2 digits (fraction digits
    excluded); keep candidates that are valid times (R13.2.2: fields after the first at most
    59) with ``lo < t`` (when ``lo`` exists) and ``t < hi`` (when ``hi`` exists). Only the
    smallest number of changes with any candidate counts."""
    frac_start = len(raw)
    for i, ch in enumerate(raw):
        if ch == "." or ch == ",":
            frac_start = i
            break
    frac = raw[frac_start + 1 :]
    values: list[int] = [0]
    slots: list[_Slot] = []
    for i in range(frac_start):
        ch = raw[i]
        if ch == ":":
            values.append(0)
        elif is_digit(ch):
            f = len(values) - 1
            values[f] = values[f] * 10 + int(ch)
            slots.append(_Slot(i, f, int(ch)))
    # Place value of each digit inside its field.
    place = 1
    field = -1
    for slot in reversed(slots):
        if slot.field != field:
            field = slot.field
            place = 1
        slot.place = place
        place *= 10
    mult = (3600, 60, 1) if len(values) == 3 else (60, 1)

    def value_with(changes: list[tuple[_Slot, int]]) -> Optional[float]:
        v = list(values)
        for s, d in changes:
            v[s.field] += (d - s.digit) * s.place
        whole = 0
        for f, x in enumerate(v):
            if f > 0 and x > 59:
                return None
            whole += x * mult[f]
        t = seconds_of(whole, frac)
        return t if (lo is None or lo < t) and (hi is None or t < hi) else None

    def render(changes: list[tuple[_Slot, int]]) -> str:
        chars = list(raw)
        for s, d in changes:
            chars[s.at] = str(d)
        return "".join(chars)

    found: Optional[list[tuple[_Slot, int]]] = None
    count = 0
    for slot in slots:
        for d in range(10):
            if d == slot.digit:
                continue
            change = [(slot, d)]
            if value_with(change) is None:
                continue
            count += 1
            found = change
    if count == 0:
        for x in range(len(slots)):
            if count >= 2:
                break
            for y in range(x + 1, len(slots)):
                if count >= 2:
                    break
                a = slots[x]
                b = slots[y]
                for da in range(10):
                    if count >= 2:
                        break
                    if da == a.digit:
                        continue
                    for db in range(10):
                        if count >= 2:
                            break
                        if db == b.digit:
                            continue
                        change = [(a, da), (b, db)]
                        if value_with(change) is None:
                            continue
                        count += 1
                        found = change
    if count == 0 or found is None:
        return None
    if count > 1:
        return _MANY
    seconds = value_with(found)
    assert seconds is not None
    return TimeValue(render(found), seconds)


class _Timed:
    __slots__ = ("line", "time")

    def __init__(self, line: int, time: TimeValue) -> None:
        self.line = line
        self.time = time


def suggest_fix(times: list[_Timed], k: int) -> Optional[Suggestion]:
    """R13.7 suggestion for the out-of-order timed entry ``k`` (index into ``times``): search
    its own time between its neighbours first; only when that finds no candidate at all,
    search the previous timed entry's time between the one before it and entry ``k``."""
    cur = times[k]
    prev = times[k - 1]
    nxt = times[k + 1] if k + 1 < len(times) else None
    own = search_time(cur.time.raw, prev.time.seconds, nxt.time.seconds if nxt else None)
    if isinstance(own, TimeValue):
        return Suggestion(cur.line, own.raw, own.seconds)
    if own == _MANY:
        return None
    before = times[k - 2] if k >= 2 else None
    back = search_time(prev.time.raw, before.time.seconds if before else None, cur.time.seconds)
    return Suggestion(prev.line, back.raw, back.seconds) if isinstance(back, TimeValue) else None


def _unparsed(t: ParsedTrack) -> bool:
    return "noSeparator" in t.warnings


def _empty_part(t: ParsedTrack) -> bool:
    """R13.3: the track part is empty after R1 (a line that is only a time)."""
    return len(normalize(t.input)) == 0


def tracklist_warnings(
    entries: list[TracklistEntry], invalid: list[bool], audio_seconds: Optional[float]
) -> list[TracklistWarning]:
    per_line: list[list[TracklistWarning]] = [[] for _ in entries]

    def add(i: int, code: TracklistWarningCode, suggestion: Optional[Suggestion] = None) -> None:
        severity: Severity = "info" if code == "no-start-at-zero" else "warning"
        per_line[i].append(TracklistWarning(code, entries[i].line, severity, suggestion))

    timed = [i for i, e in enumerate(entries) if e.time is not None]

    for i, e in enumerate(entries):
        if e.kind == "track" and e.time is None and len(timed) > 0:
            add(i, "time-missing")
        if invalid[i]:
            add(i, "invalid-time")
        # R13.3 / R13.7: an empty track part (a time-only line) is unparsed too.
        if (
            e.kind == "track"
            and e.track is not None
            and (_unparsed(e.track) or _empty_part(e.track))
        ):
            add(i, "line-unparsed")

    times: list[_Timed] = []
    for i in timed:
        t = entries[i].time
        assert t is not None
        times.append(_Timed(entries[i].line, t))
    for k, i in enumerate(timed):
        s = times[k].time.seconds
        if k == 0 and s != 0:
            add(i, "no-start-at-zero")
        if k > 0:
            p = times[k - 1].time.seconds
            if s < p:
                add(i, "time-out-of-order", suggest_fix(times, k))
            elif s == p:
                add(i, "time-duplicate")
        if audio_seconds is not None and s > audio_seconds:
            add(i, "time-after-audio-end")

    prev_pos: Optional[int] = None
    for i, e in enumerate(entries):
        pos = e.track.position if e.kind == "track" and e.track is not None else None
        if pos is None:
            continue
        if prev_pos is not None:
            if pos.number == prev_pos:
                add(i, "position-duplicate")
            elif pos.number != prev_pos + 1:
                add(i, "position-gap")
        prev_pos = pos.number

    out: list[TracklistWarning] = []
    for ws in per_line:
        out.extend(sorted(ws, key=lambda w: _RANK[w.code]))
    return out
