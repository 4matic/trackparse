"""R13.5 bare positions."""

from __future__ import annotations

from typing import NamedTuple, Optional

from ._digits import digit_run
from ._words import is_whitespace


class BareCandidate(NamedTuple):
    raw: str
    number: int
    #: The track part with the digits and the following space removed.
    stripped: str


def bare_candidate(rest: str, position_taken: bool) -> Optional[BareCandidate]:
    """R13.5: a track part starting with 1–3 ASCII digits and a space (U+0020 only), where R4.2
    took no position (``position_taken``), is a bare candidate. The remainder must be
    non-empty."""
    if position_taken:
        return None
    n = digit_run(rest, 0)
    if n < 1 or n > 3 or rest[n : n + 1] != " ":
        return None
    stripped = rest[n + 1 :]
    if all(is_whitespace(c) for c in stripped):
        return None
    raw = rest[:n]
    return BareCandidate(raw, int(raw), stripped)


def confirm_positions(
    bare: list[Optional[BareCandidate]], candidate_line: list[bool]
) -> list[bool]:
    """R13.5: candidate ``i`` is a position when the nearest preceding or the nearest following
    candidate line (R13.4.2) also has a bare candidate equal to ``N - 1`` or ``N + 1``. Linear
    time."""
    n = len(bare)
    prev = [-1] * n
    nxt = [-1] * n
    last = -1
    for i in range(n):
        prev[i] = last
        if candidate_line[i]:
            last = i
    last = -1
    for i in range(n - 1, -1, -1):
        nxt[i] = last
        if candidate_line[i]:
            last = i

    def adjacent(b: BareCandidate, j: int) -> bool:
        o = bare[j] if j >= 0 else None
        return o is not None and (o.number == b.number - 1 or o.number == b.number + 1)

    return [
        b is not None and (adjacent(b, prev[i]) or adjacent(b, nxt[i])) for i, b in enumerate(bare)
    ]
