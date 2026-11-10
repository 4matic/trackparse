"""R13.12 Markdown emphasis markers in a tracklist line (js/src/tracklist/emphasis.ts)."""

from __future__ import annotations

from typing import Optional

from ._words import char_at, is_whitespace


def _run_length(s: str, at: int) -> int:
    """Length of the maximal run of ``*`` or ``_`` at ``at`` (0 when there is none)."""
    ch = char_at(s, at)
    j = at
    if ch == "*" or ch == "_":
        n = len(s)
        while j < n and s[j] == ch:
            j += 1
    return j - at


def emphasis_run(s: str, at: int) -> int:
    """R13.2.1 / R13.2.3: the length of an emphasis run at ``at`` (a maximal run of 1 or 2 ``*``
    or ``_``), else 0. Used next to the time, where runs are read by position, not paired."""
    n = _run_length(s, at)
    return n if n <= 2 else 0


def _is_outer(ch: Optional[str]) -> bool:
    """R13.12.2 outer side: line edge, R0.2 whitespace, a bracket, or a ``*`` / ``_``."""
    return ch is None or is_whitespace(ch) or ch in "[]()*_"


def _is_inner(ch: Optional[str]) -> bool:
    """R13.12.2 inner side: a character that is not R0.2 whitespace."""
    return ch is not None and not is_whitespace(ch)


def without_markers(raw: str, frm: int) -> str:
    """R13.12: ``raw[frm:]`` without its emphasis markers. A run of 1 or 2 ``*`` or ``_`` closes
    when the character before it is an inner side and the one after it an outer side, and opens
    the other way round. A closer pairs with the nearest opener of the same characters; that
    opener and every opener after it leave the stack. One stack per run keeps this linear."""
    n = len(raw)
    marked = [False] * n
    stacks: dict[str, list[int]] = {"*": [], "**": [], "_": [], "__": []}
    i = 0
    while i < n:
        length = _run_length(raw, i) or 1
        own = stacks.get(raw[i : i + length])
        before = raw[i - 1] if i > 0 else None
        after = char_at(raw, i + length)
        at: Optional[int] = None
        if own and _is_inner(before) and _is_outer(after):
            at = own.pop()
        if at is not None:
            for m in range(length):
                marked[at + m] = True
                marked[i + m] = True
            for st in stacks.values():
                while st and st[-1] > at:
                    st.pop()
        elif own is not None and _is_outer(before) and _is_inner(after):
            own.append(i)
        i += length
    return "".join(raw[k] for k in range(frm, n) if not marked[k])
