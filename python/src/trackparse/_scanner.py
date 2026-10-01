"""R3: top-level group scanner and skeleton.

A ``Skel`` is a skeleton string plus, for every code point, its absolute offset in the
normalized input. Groups are looked up by the offset of their placeholder, so any slice of a
skeleton still knows which groups it contains. Offsets also give every extracted item (junk,
versions, credits) its position for the "order of appearance" rules.

Offsets are counted in UTF-16 units (an astral code point advances by 2) so that every
ordering comparison is identical to the JS reference; indices into ``text`` are code points.
"""

from __future__ import annotations

from typing import NamedTuple, Optional

from ._words import char_at, collapse_spaces, is_whitespace

#: R3.4 placeholder code point.
PH = ""

_CLOSER = {"(": ")", "[": "]", "{": "}"}


class Group:
    __slots__ = ("closed", "end", "inner", "open", "pos", "raw")

    def __init__(self, open: str, inner: str, raw: str, closed: bool, pos: int, end: int) -> None:
        self.open = open
        #: R3.5: inner text, trimmed and whitespace-collapsed.
        self.inner = inner
        #: The group exactly as written, brackets included.
        self.raw = raw
        self.closed = closed
        #: Absolute offset of the opener.
        self.pos = pos
        #: Absolute offset just past the group.
        self.end = end


class Skel:
    __slots__ = ("groups", "pos", "text")

    def __init__(self, text: str, pos: list[int], groups: dict[int, Group]) -> None:
        self.text = text
        self.pos = pos
        self.groups = groups


class ScanResult(NamedTuple):
    skel: Skel
    unbalanced: bool


def scan(s: str, base: int = 0) -> ScanResult:
    """R3.1–R3.5: scan ``s`` (whose first unit sits at absolute offset ``base``)."""
    text: list[str] = []
    pos: list[int] = []
    groups: dict[int, Group] = {}
    stack: list[str] = []
    group_start = -1
    group_off = 0
    # Absolute UTF-16 offset of each code point of `s` (plus the end), for group bookkeeping.
    offs: list[int] = []
    off = base
    for ch in s:
        offs.append(off)
        off += 2 if ch > "￿" else 1
    offs.append(off)
    for i, ch in enumerate(s):
        if not stack:
            if ch in _CLOSER:
                stack.append(_CLOSER[ch])
                group_start = i
                group_off = offs[i]
            else:
                text.append(ch)
                pos.append(offs[i])
            continue
        if ch in _CLOSER:
            stack.append(_CLOSER[ch])
        elif ch == stack[-1]:
            stack.pop()
            if not stack:
                _add_group(s, group_start, i + 1, True, offs, groups)
                text.append(PH)
                pos.append(group_off)
        # R3.2: a non-matching closer is literal text inside the group.
    unbalanced = len(stack) > 0
    if unbalanced:
        # R3.3: the group runs to the end of the string.
        _add_group(s, group_start, len(s), False, offs, groups)
        text.append(PH)
        pos.append(group_off)
    return ScanResult(Skel("".join(text), pos, groups), unbalanced)


def _add_group(
    s: str, start: int, end: int, closed: bool, offs: list[int], groups: dict[int, Group]
) -> None:
    inner = s[start + 1 : end - 1 if closed else end]
    groups[offs[start]] = Group(
        s[start], collapse_spaces(inner), s[start:end], closed, offs[start], offs[end]
    )


# ---------------------------------------------------------------------------
# Skeleton operations


def slice_skel(s: Skel, start: int, end: Optional[int] = None) -> Skel:
    n = len(s.text)
    if end is None:
        end = n
    a = max(0, min(start, n))
    b = max(a, min(end, n))
    return Skel(s.text[a:b], s.pos[a:b], s.groups)


def trim_skel(s: Skel) -> Skel:
    t = s.text
    a = 0
    b = len(t)
    while a < b and is_whitespace(t[a]):
        a += 1
    while b > a and is_whitespace(t[b - 1]):
        b -= 1
    return slice_skel(s, a, b)


def concat_skel(a: Skel, b: Skel) -> Skel:
    return Skel(a.text + b.text, a.pos + b.pos, a.groups)


def cut_skel(s: Skel, start: int, end: int) -> Skel:
    """Remove the code points in [start, end)."""
    return concat_skel(slice_skel(s, 0, start), slice_skel(s, end))


def group_at(s: Skel, index: int) -> Optional[Group]:
    """The group behind the placeholder at ``index``, if it is one."""
    if char_at(s.text, index) != PH:
        return None
    return s.groups.get(s.pos[index])


def groups_of(s: Skel) -> list[tuple[int, Group]]:
    """Groups in the skeleton, left to right, with their index."""
    out: list[tuple[int, Group]] = []
    if PH not in s.text:
        return out
    for i, ch in enumerate(s.text):
        if ch == PH:
            g = s.groups.get(s.pos[i])
            if g is not None:
                out.append((i, g))
    return out


def has_group(s: Skel) -> bool:
    return PH in s.text


def render_skel(s: Skel) -> str:
    """Substitute groups back (R3.4 reassembly). Whitespace is not touched."""
    if PH not in s.text:
        return s.text
    out: list[str] = []
    for i, ch in enumerate(s.text):
        g = s.groups.get(s.pos[i]) if ch == PH else None
        out.append(g.raw if g is not None else ch)
    return "".join(out)


def start_pos(s: Skel, fallback: int = 0) -> int:
    """Absolute offset of the first code point (or ``fallback`` for an empty skeleton)."""
    return s.pos[0] if s.pos else fallback


def remove_groups(s: Skel, remove: set[int]) -> Skel:
    """Remove the placeholders of the given groups."""
    if not remove:
        return s
    text: list[str] = []
    pos: list[int] = []
    for ch, p in zip(s.text, s.pos):
        if ch == PH and p in remove:
            continue
        text.append(ch)
        pos.append(p)
    return Skel("".join(text), pos, s.groups)


def is_spaced_dash_at(text: str, i: int) -> bool:
    """R6.1: ``-`` with a space immediately on both sides."""
    return 0 < i < len(text) - 1 and text[i] == "-" and text[i - 1] == " " and text[i + 1] == " "


def has_spaced_dash(text: str) -> bool:
    return " - " in text


class Segment:
    __slots__ = ("end", "skel", "start")

    def __init__(self, skel: Skel, start: int, end: int) -> None:
        self.skel = skel
        #: Trimmed range inside the parent skeleton.
        self.start = start
        self.end = end


def _trimmed_segment(s: Skel, start: int, end: int) -> Segment:
    t = s.text
    a = start
    b = max(start, end)
    while a < b and is_whitespace(char_at(t, a)):
        a += 1
    while b > a and is_whitespace(char_at(t, b - 1)):
        b -= 1
    return Segment(slice_skel(s, a, b), a, b)


def split_at_spaced_dashes(s: Skel, allow_leading: bool = False) -> list[Segment]:
    """R6.1 / R8.2: split a skeleton at every spaced dash into trimmed segments. With
    ``allow_leading``, a string starting with ``- `` yields an empty first segment (R6.3)."""
    segments: list[Segment] = []
    t = s.text
    start = 0
    if allow_leading and t[:2] == "- ":
        segments.append(Segment(slice_skel(s, 0, 0), 0, 0))
        start = 2
    i = max(start, 1)
    n = len(t)
    while i < n - 1:
        if t[i] == "-" and t[i - 1] == " " and t[i + 1] == " ":
            segments.append(_trimmed_segment(s, start, i - 1))
            start = i + 2
        i += 1
    segments.append(_trimmed_segment(s, start, n))
    return segments
