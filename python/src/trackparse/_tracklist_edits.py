"""R13.9: apply_edits. Edits apply in order, each to the result of the previous one."""

from __future__ import annotations

import math
from collections.abc import Mapping
from typing import Any, NamedTuple, Optional

from ._tracklist_options import js_number
from ._tracklist_split import has_line_break, split_lines
from ._tracklist_times import TimeScan, format_time, scan_time
from ._tracklist_types import EditResult, EditWarning, EditWarningCode, Eol
from ._words import is_whitespace

#: A key absent from an edit (JS ``undefined``); a present ``None`` is JS ``null``.
_MISSING: Any = object()


class _WorkLine:
    __slots__ = ("changed", "eol", "raw", "scan")

    def __init__(self, raw: str, eol: Eol, changed: bool, scan: Optional[TimeScan] = None) -> None:
        self.raw = raw
        self.eol: Eol = eol
        #: Written by an edit (R13.9.1).
        self.changed = changed
        #: ``scan_time(raw)``, kept in step with ``raw`` (each line is scanned once per write).
        self.scan = scan_time(raw) if scan is None else scan


class _InvalidEdit(Exception):
    pass


class _EditCtx:
    """Edit state. Every edit validates its input before it mutates anything, so an invalid
    edit (R13.9.6) only has to drop the warnings it added: no snapshot of the lines is
    needed."""

    __slots__ = ("eols", "index", "lines", "one_digit", "warnings", "wide_hours")

    def __init__(self, lines: list[_WorkLine]) -> None:
        self.lines = lines
        self.index = 0
        self.warnings: list[EditWarning] = []
        #: R13.9.2: times with one-digit hours and with wider hours, over all lines.
        self.one_digit = 0
        self.wide_hours = 0
        #: R13.9.5: how many lines end with each non-empty eol (first-seen order is not needed).
        self.eols: dict[Eol, int] = {}
        for line in lines:
            _count_line(self, line, 1)


def _count_hours(ctx: _EditCtx, line: _WorkLine, by: int) -> None:
    """Add (``by`` = 1) or remove (``by`` = -1) a line's time from the hour-width counts."""
    style = line.scan.style
    if style is None or not style.hours:
        return
    if style.hour_width == 1:
        ctx.one_digit += by
    else:
        ctx.wide_hours += by


def _count_eol(ctx: _EditCtx, eol: Eol, by: int) -> None:
    if eol != "":
        ctx.eols[eol] = ctx.eols.get(eol, 0) + by


def _count_line(ctx: _EditCtx, line: _WorkLine, by: int) -> None:
    """A line enters (``by`` = 1) or leaves (``by`` = -1) the text."""
    _count_hours(ctx, line, by)
    _count_eol(ctx, line.eol, by)


def _set_eol(ctx: _EditCtx, line: _WorkLine, eol: Eol) -> None:
    _count_eol(ctx, line.eol, -1)
    line.eol = eol
    line.changed = True
    _count_eol(ctx, eol, 1)


def _set_raw(ctx: _EditCtx, line: _WorkLine, raw: str) -> None:
    _count_hours(ctx, line, -1)
    line.raw = raw
    line.scan = scan_time(raw)
    line.changed = True
    _count_hours(ctx, line, 1)


def _warn_edit(ctx: _EditCtx, code: EditWarningCode, line: Optional[int]) -> None:
    ctx.warnings.append(EditWarning(code, ctx.index, line))


def _field(edit: Mapping[str, Any], key: str) -> Any:
    return edit.get(key, _MISSING)


def _has(edit: Mapping[str, Any], key: str) -> bool:
    return _field(edit, key) is not _MISSING


def _js_integer(value: object) -> Optional[int]:
    """JS ``Number.isInteger(value)`` → the integer, else None."""
    n = js_number(value)
    if n is None or not math.isfinite(n) or not n.is_integer():
        return None
    return int(n)


def _line_index(ctx: _EditCtx, value: object) -> int:
    """0-based index of an existing 1-based line number, else _InvalidEdit."""
    n = _js_integer(value)
    if n is None or n < 1 or n > len(ctx.lines):
        raise _InvalidEdit
    return n - 1


def _finite(value: object) -> float:
    n = js_number(value)
    if n is None or not math.isfinite(n):
        raise _InvalidEdit
    return n


def _single_line(value: object) -> str:
    if not isinstance(value, str) or has_line_break(value):
        raise _InvalidEdit
    return value


def _one_digit_hours(ctx: _EditCtx) -> bool:
    """R13.9.2: do the text's times with hours use one-digit hours? (All, at least one.)"""
    return ctx.one_digit > 0 and ctx.wide_hours == 0


class _Retime(NamedTuple):
    #: 0-based line index.
    at: int
    raw: str
    clamped: bool
    upgraded: bool


def _retime(ctx: _EditCtx, index: int, value: float, one_digit: bool) -> _Retime:
    """R13.9.2 / R13.9.3: the rewritten line for a new value, or _InvalidEdit."""
    scan = ctx.lines[index].scan
    if scan.style is None or scan.time is None:
        raise _InvalidEdit
    clamped = value < 0
    f = format_time(0.0 if clamped else value, scan.style, one_digit)
    if f is None:
        raise _InvalidEdit
    p = scan.parts
    return _Retime(index, p.lead + f[0] + p.gap + p.rest, clamped, f[1])


def _commit(ctx: _EditCtx, changes: list[_Retime]) -> None:
    for c in changes:
        _set_raw(ctx, ctx.lines[c.at], c.raw)
        if c.clamped:
            _warn_edit(ctx, "time-clamped", c.at + 1)
        if c.upgraded:
            _warn_edit(ctx, "style-upgraded", c.at + 1)


#: Lines whose neighbourhood ``_canonical`` must check after an edit; None = every line.
_Touched = Optional[list[int]]


def _set_time(ctx: _EditCtx, edit: Mapping[str, Any]) -> _Touched:
    index = _line_index(ctx, _field(edit, "line"))
    value = _finite(_field(edit, "seconds"))
    _commit(ctx, [_retime(ctx, index, value, _one_digit_hours(ctx))])
    return []  # a timed line is never empty and keeps its eol


def _shift(ctx: _EditCtx, edit: Mapping[str, Any]) -> _Touched:
    delta = _finite(_field(edit, "seconds"))
    if _has(edit, "lines"):
        lst = _field(edit, "lines")
        if not isinstance(lst, (list, tuple)):
            raise _InvalidEdit
        targets = sorted({_line_index(ctx, v) for v in lst})
    else:
        frm = _line_index(ctx, _field(edit, "from")) if _has(edit, "from") else 0
        targets = list(range(frm, len(ctx.lines)))
    one_digit = _one_digit_hours(ctx)
    changes: list[_Retime] = []
    for i in targets:
        time = ctx.lines[i].scan.time
        if time is not None:
            changes.append(_retime(ctx, i, time.seconds + delta, one_digit))
    _commit(ctx, changes)
    return []


def _sort_by_time(ctx: _EditCtx) -> _Touched:
    """R13.9.4: stable sort of blocks (a timed line plus the untimed lines after it)."""
    lines = ctx.lines
    first = next((i for i, line in enumerate(lines) if line.scan.time is not None), -1)
    if first < 0:
        return []
    blocks: list[list[Any]] = []  # [seconds, from, to]
    for i in range(first, len(lines)):
        t = lines[i].scan.time
        if t is not None:
            blocks.append([t.seconds, i, i + 1])
        else:
            blocks[-1][2] = i + 1
    ordered = sorted(blocks, key=lambda b: b[0])  # stable for equal values
    order = list(range(first))
    for b in ordered:
        order.extend(range(b[1], b[2]))
    old = ctx.lines
    ctx.lines = [
        # eol values stay in their slots
        _WorkLine(old[src].raw, old[slot].eol, old[src].changed or src != slot, old[src].scan)
        for slot, src in enumerate(order)
    ]
    return None


def _set_rest(ctx: _EditCtx, edit: Mapping[str, Any]) -> _Touched:
    index = _line_index(ctx, _field(edit, "line"))
    rest = _single_line(_field(edit, "rest"))
    line = ctx.lines[index]
    p = line.scan.parts
    # R13.9 set-rest: a non-empty rest after a time is preceded by whitespace; a gap that does
    # not end with R0.2 whitespace (``""``, ``]``, ``)``) gets one space.
    spaced = len(p.gap) > 0 and is_whitespace(p.gap[-1])
    gap = p.gap + " " if p.time != "" and not spaced and rest != "" else p.gap
    _set_raw(ctx, line, p.lead + p.time + gap + rest)
    return [index]


def _common_eol(ctx: _EditCtx) -> Eol:
    """R13.9.5: the most common non-empty eol (ties: the one seen first in line order),
    ``"\\n"`` if none. Counts are kept up to date; only a tie needs a look at the lines."""
    best = 0
    top: list[Eol] = []
    for eol, n in ctx.eols.items():
        if n <= 0 or n < best:
            continue
        if n > best:
            top = []
        best = n
        top.append(eol)
    if len(top) == 0:
        return "\n"
    if len(top) == 1:
        return top[0]
    for line in ctx.lines:
        if line.eol in top:
            return line.eol
    return "\n"


def _insert(ctx: _EditCtx, edit: Mapping[str, Any]) -> _Touched:
    after = _js_integer(_field(edit, "after"))
    if after is None or after < 0 or after > len(ctx.lines):
        raise _InvalidEdit
    text = _single_line(_field(edit, "text"))
    eol = _common_eol(ctx)
    last = ctx.lines[-1] if ctx.lines else None
    # R13.9.5: inserting into an empty text keeps it without a trailing newline.
    line = _WorkLine(text, "" if len(ctx.lines) == 0 else eol, True)
    if after == len(ctx.lines) and last is not None and last.eol == "":
        _set_eol(ctx, last, eol)
        line.eol = ""
    ctx.lines.insert(after, line)
    _count_line(ctx, line, 1)
    return [after]


def _remove(ctx: _EditCtx, edit: Mapping[str, Any]) -> _Touched:
    index = _line_index(ctx, _field(edit, "line"))
    gone = ctx.lines.pop(index)
    _count_line(ctx, gone, -1)
    last = ctx.lines[-1] if ctx.lines else None
    if index == len(ctx.lines) and last is not None and last.eol != gone.eol:
        _set_eol(ctx, last, gone.eol)  # R13.9.5: the deleted last line's eol moves up
    return [index]


def _merge_at(ctx: _EditCtx, k: int) -> None:
    """A ``"\\r"`` line followed by an empty ``"\\n"`` line reads back as one CRLF line."""
    lines = ctx.lines
    if k < 0 or k + 1 >= len(lines):
        return
    prev = lines[k]
    nxt = lines[k + 1]
    if prev.eol != "\r" or nxt.raw != "" or nxt.eol != "\n":
        return
    _set_eol(ctx, prev, "\r\n")
    del lines[k + 1]
    _count_line(ctx, nxt, -1)


def _canonical(ctx: _EditCtx, touched: _Touched) -> None:
    """R13.9.5: keep the work lines equal to what ``split_lines(join(lines))`` would give: a
    ``"\\r"`` followed by an empty ``"\\n"`` line reads back as one CRLF, and an empty last
    line without eol is not a line. The lines are canonical before each edit, so only the
    neighbourhood of the lines the edit touched (and the end) is checked; None checks every
    line (sort-by-time)."""
    lines = ctx.lines
    if touched is None:
        starts: list[int] = list(range(len(lines) - 2, -1, -1))
    else:
        found = {len(lines) - 2}
        for t in touched:
            found.add(t - 1)
            found.add(t)
        starts = sorted(found, reverse=True)
    # Descending, so a merge at k never moves a pair still to be checked.
    for k in starts:
        _merge_at(ctx, k)
    if lines and lines[-1].raw == "" and lines[-1].eol == "":
        _count_line(ctx, lines.pop(), -1)


def _apply_one(ctx: _EditCtx, edit: object) -> _Touched:
    if not isinstance(edit, Mapping):
        raise _InvalidEdit
    op = _field(edit, "op")
    if not isinstance(op, str):
        raise _InvalidEdit
    if op == "set-time":
        return _set_time(ctx, edit)
    if op == "shift":
        return _shift(ctx, edit)
    if op == "sort-by-time":
        return _sort_by_time(ctx)
    if op == "set-rest":
        return _set_rest(ctx, edit)
    if op == "insert":
        return _insert(ctx, edit)
    if op == "delete":
        return _remove(ctx, edit)
    raise _InvalidEdit


def _invalid_line(edit: object) -> Optional[int]:
    """R13.9.6: an ``edit-invalid`` warning's line is the edit's integer ``line``, else
    ``after``, else None."""
    if not isinstance(edit, Mapping):
        return None
    try:
        for key in ("line", "after"):
            n = _js_integer(_field(edit, key))
            if n is not None:
                return n
        return None
    except Exception:
        return None


def apply_edits_list(text: str, edits: object) -> EditResult:
    """R13.9: apply ``edits`` to ``text``. Never raises (R13.9.6): an invalid edit changes
    nothing and warns ``edit-invalid``."""
    lst: list[Any] = list(edits) if isinstance(edits, (list, tuple)) else []
    if len(lst) == 0:
        return EditResult(text, (), ())
    ctx = _EditCtx([_WorkLine(line.raw, line.eol, False) for line in split_lines(text)])
    for i, edit in enumerate(lst):
        ctx.index = i
        warnings_before = len(ctx.warnings)
        try:
            # Validation raises before any mutation, so the lines need no rollback (R13.9.6).
            _canonical(ctx, _apply_one(ctx, edit))
        except Exception:
            del ctx.warnings[warnings_before:]
            _warn_edit(ctx, "edit-invalid", _invalid_line(edit))
    changed = tuple(i + 1 for i, line in enumerate(ctx.lines) if line.changed)
    return EditResult(
        "".join(line.raw + line.eol for line in ctx.lines), changed, tuple(ctx.warnings)
    )
