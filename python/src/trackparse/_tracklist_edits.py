"""R13.9: apply_edits. Edits apply in order, each to the result of the previous one."""

from __future__ import annotations

import math
from collections.abc import Mapping
from typing import Any, NamedTuple, Optional

from ._tracklist_options import js_number
from ._tracklist_split import has_line_break, split_lines
from ._tracklist_times import TimeScan, format_time, scan_time
from ._tracklist_types import EditResult, EditWarning, EditWarningCode, Eol

#: A key absent from an edit (JS ``undefined``); a present ``None`` is JS ``null``.
_MISSING: Any = object()


class _WorkLine:
    __slots__ = ("changed", "eol", "raw")

    def __init__(self, raw: str, eol: Eol, changed: bool) -> None:
        self.raw = raw
        self.eol: Eol = eol
        #: Written by an edit (R13.9.1).
        self.changed = changed

    def copy(self) -> _WorkLine:
        return _WorkLine(self.raw, self.eol, self.changed)


class _InvalidEdit(Exception):
    pass


class _EditCtx:
    __slots__ = ("index", "lines", "warnings")

    def __init__(self, lines: list[_WorkLine]) -> None:
        self.lines = lines
        self.index = 0
        self.warnings: list[EditWarning] = []


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


def _one_digit_hours(scans: list[TimeScan]) -> bool:
    """R13.9.2: do the text's times with hours use one-digit hours? (All, at least one.)"""
    one = 0
    more = 0
    for s in scans:
        if s.style is None or not s.style.hours:
            continue
        if s.style.hour_width == 1:
            one += 1
        else:
            more += 1
    return one > 0 and more == 0


class _Retime(NamedTuple):
    #: 0-based line index.
    at: int
    raw: str
    clamped: bool
    upgraded: bool


def _retime(index: int, value: float, scans: list[TimeScan], one_digit: bool) -> _Retime:
    """R13.9.2 / R13.9.3: the rewritten line for a new value, or _InvalidEdit."""
    scan = scans[index]
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
        line = ctx.lines[c.at]
        line.raw = c.raw
        line.changed = True
        if c.clamped:
            _warn_edit(ctx, "time-clamped", c.at + 1)
        if c.upgraded:
            _warn_edit(ctx, "style-upgraded", c.at + 1)


def _scans_of(lines: list[_WorkLine]) -> list[TimeScan]:
    return [scan_time(line.raw) for line in lines]


def _set_time(ctx: _EditCtx, edit: Mapping[str, Any]) -> None:
    index = _line_index(ctx, _field(edit, "line"))
    value = _finite(_field(edit, "seconds"))
    scans = _scans_of(ctx.lines)
    _commit(ctx, [_retime(index, value, scans, _one_digit_hours(scans))])


def _shift(ctx: _EditCtx, edit: Mapping[str, Any]) -> None:
    delta = _finite(_field(edit, "seconds"))
    scans = _scans_of(ctx.lines)
    if _has(edit, "lines"):
        lst = _field(edit, "lines")
        if not isinstance(lst, (list, tuple)):
            raise _InvalidEdit
        targets = sorted({_line_index(ctx, v) for v in lst})
    else:
        frm = _line_index(ctx, _field(edit, "from")) if _has(edit, "from") else 0
        targets = list(range(frm, len(ctx.lines)))
    one_digit = _one_digit_hours(scans)
    changes: list[_Retime] = []
    for i in targets:
        time = scans[i].time
        if time is not None:
            changes.append(_retime(i, time.seconds + delta, scans, one_digit))
    _commit(ctx, changes)


def _sort_by_time(ctx: _EditCtx) -> None:
    """R13.9.4: stable sort of blocks (a timed line plus the untimed lines after it)."""
    scans = _scans_of(ctx.lines)
    first = next((i for i, s in enumerate(scans) if s.time is not None), -1)
    if first < 0:
        return
    blocks: list[list[Any]] = []  # [seconds, from, to]
    for i in range(first, len(scans)):
        t = scans[i].time
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
        _WorkLine(old[src].raw, old[slot].eol, old[src].changed or src != slot)
        for slot, src in enumerate(order)
    ]


def _set_rest(ctx: _EditCtx, edit: Mapping[str, Any]) -> None:
    index = _line_index(ctx, _field(edit, "line"))
    rest = _single_line(_field(edit, "rest"))
    line = ctx.lines[index]
    p = scan_time(line.raw).parts
    line.raw = p.lead + p.time + p.gap + rest
    line.changed = True


def _common_eol(lines: list[_WorkLine]) -> Eol:
    """R13.9.5: the most common non-empty eol (ties: the first seen), ``"\\n"`` if none."""
    counts: dict[Eol, int] = {}
    for line in lines:
        if line.eol != "":
            counts[line.eol] = counts.get(line.eol, 0) + 1
    best: Eol = "\n"
    best_count = 0
    # dicts keep first-seen order, so `>` keeps the first eol among equal counts.
    for eol, n in counts.items():
        if n > best_count:
            best = eol
            best_count = n
    return best


def _insert(ctx: _EditCtx, edit: Mapping[str, Any]) -> None:
    after = _js_integer(_field(edit, "after"))
    if after is None or after < 0 or after > len(ctx.lines):
        raise _InvalidEdit
    text = _single_line(_field(edit, "text"))
    eol = _common_eol(ctx.lines)
    last = ctx.lines[-1] if ctx.lines else None
    # R13.9.5: inserting into an empty text keeps it without a trailing newline.
    line = _WorkLine(text, "" if len(ctx.lines) == 0 else eol, True)
    if after == len(ctx.lines) and last is not None and last.eol == "":
        last.eol = eol
        last.changed = True
        line.eol = ""
    ctx.lines.insert(after, line)


def _remove(ctx: _EditCtx, edit: Mapping[str, Any]) -> None:
    index = _line_index(ctx, _field(edit, "line"))
    gone = ctx.lines.pop(index)
    last = ctx.lines[-1] if ctx.lines else None
    if index == len(ctx.lines) and last is not None and last.eol != gone.eol:
        last.eol = gone.eol  # R13.9.5: the deleted last line's eol moves up
        last.changed = True


def _canonical(lines: list[_WorkLine]) -> list[_WorkLine]:
    """Keep the work lines equal to what ``split_lines(join(lines))`` would give: a ``"\\r"``
    followed by an empty ``"\\n"`` line reads back as one CRLF, and an empty last line without
    eol is not a line."""
    out: list[_WorkLine] = []
    for line in lines:
        prev = out[-1] if out else None
        if prev is not None and prev.eol == "\r" and line.raw == "" and line.eol == "\n":
            prev.eol = "\r\n"
            prev.changed = True
            continue
        out.append(line)
    if out and out[-1].raw == "" and out[-1].eol == "":
        out.pop()
    return out


def _apply_one(ctx: _EditCtx, edit: object) -> None:
    if not isinstance(edit, Mapping):
        raise _InvalidEdit
    op = _field(edit, "op")
    if not isinstance(op, str):
        raise _InvalidEdit
    if op == "set-time":
        _set_time(ctx, edit)
    elif op == "shift":
        _shift(ctx, edit)
    elif op == "sort-by-time":
        _sort_by_time(ctx)
    elif op == "set-rest":
        _set_rest(ctx, edit)
    elif op == "insert":
        _insert(ctx, edit)
    elif op == "delete":
        _remove(ctx, edit)
    else:
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
        before = [line.copy() for line in ctx.lines]
        warnings_before = len(ctx.warnings)
        try:
            _apply_one(ctx, edit)
            ctx.lines = _canonical(ctx.lines)
        except Exception:
            ctx.lines = before
            del ctx.warnings[warnings_before:]
            _warn_edit(ctx, "edit-invalid", _invalid_line(edit))
    changed = tuple(i + 1 for i, line in enumerate(ctx.lines) if line.changed)
    return EditResult(
        "".join(line.raw + line.eol for line in ctx.lines), changed, tuple(ctx.warnings)
    )
