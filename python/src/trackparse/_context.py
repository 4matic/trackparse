"""Per-call parse context: resolved options + vocabulary tables."""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from ._tables import Tables
from ._words import char_at, dedup_key, is_whitespace, utf16_len


class Ctx:
    __slots__ = ("known_keys", "known_max_len", "list_seq", "split_and", "t", "warnings")

    def __init__(self, t: Tables, options: Mapping[str, Any]) -> None:
        raw_known = options.get("known_artists")
        known: list[str] = []
        if isinstance(raw_known, (list, tuple)):
            known = [k for k in raw_known if isinstance(k, str)]
        self.t = t
        self.known_keys: frozenset[str] = frozenset(
            k for k in (dedup_key(s) for s in known) if len(k) > 0
        )
        #: Longest known_artists entry in UTF-16 units, like the JS reference (bounds R7.3a).
        self.known_max_len: int = max((utf16_len(k) for k in known), default=0)
        split_and = options.get("split_and")
        self.split_and: Any = "auto" if split_and is None else split_and
        self.warnings: list[str] = []
        #: Counter giving every split credit list an id (R7.6 joiner inheritance across R9.2).
        self.list_seq = 0


def warn(ctx: Ctx, w: str) -> None:
    if w not in ctx.warnings:
        ctx.warnings.append(w)


def match_known_at(text: str, start: int, ctx: Ctx, extra_boundary: str = "") -> int:
    """R7.3a: length of the longest known_artists entry matching ``text`` at ``start``
    (dedup-key comparison), followed by end, whitespace, ``,`` or ``;`` (or ``extra_boundary``:
    R4.2 also accepts ``-``, so ``3 Doors Down-Kryptonite`` is protected). 0 when none matches.

    The search window is ``2 * longest + 8`` UTF-16 units, exactly as in the JS reference.
    """
    if not ctx.known_keys:
        return 0
    budget = ctx.known_max_len * 2 + 8
    limit = start
    used = 0
    n = len(text)
    while limit < n:
        w = 2 if text[limit] > "￿" else 1
        if used + w > budget:
            break
        used += w
        limit += 1
    for end in range(limit, start, -1):
        nxt = char_at(text, end)
        boundary = (
            end == n
            or nxt == ","
            or nxt == ";"
            or is_whitespace(nxt)
            or (extra_boundary != "" and nxt == extra_boundary)
        )
        if not boundary:
            continue
        if dedup_key(text[start:end]) in ctx.known_keys:
            return end - start
    return 0


def is_known_artist(text: str, ctx: Ctx) -> bool:
    return len(ctx.known_keys) > 0 and dedup_key(text) in ctx.known_keys
