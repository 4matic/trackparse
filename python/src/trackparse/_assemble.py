"""R9: assembly and dedup."""

from __future__ import annotations

from collections.abc import Sequence
from typing import Optional, Protocol, TypeVar, cast

from ._classify import VersionDraft
from ._credits import Credit
from ._words import dedup_key
from .types import Artist, ArtistRole, ArtistSource, Version, VersionDelimiter, VersionType

_PRECEDENCE = {"primary": 2, "featured": 1, "producer": 0, "remixer": 0}


def to_artist(c: Credit) -> Artist:
    return Artist(
        name=c.name,
        role=cast(ArtistRole, c.role),
        joiner=c.joiner,
        source=cast(ArtistSource, c.source),
    )


class _Entry:
    __slots__ = ("alive", "joiner", "list", "name", "role", "source")

    def __init__(self, c: Credit) -> None:
        self.name = c.name
        self.role = c.role
        self.joiner = c.joiner
        self.source = c.source
        self.list = c.list
        self.alive = True


class _Lists:
    """Alive members of every credit list, in entry order (R7.6 bookkeeping)."""

    def __init__(self, entries: list[_Entry]) -> None:
        self._members: dict[int, list[_Entry]] = {}
        for e in entries:
            self._members.setdefault(e.list, []).append(e)

    def pass_joiner(self, e: _Entry, joiner: Optional[str]) -> None:
        """R7.6: ``e`` leaves its list. If it was the list's first remaining name, the next
        remaining name of that list takes ``joiner``."""
        members = [x for x in self._members.get(e.list, []) if x.alive]
        if not members or members[0] is not e:
            return
        if len(members) > 1:
            members[1].joiner = joiner

    def move(self, e: _Entry, new_list: int, entries: list[_Entry]) -> None:
        """``e`` joins ``new_list`` (kept in global entry order, like the JS filter)."""
        old = self._members.get(e.list, [])
        if e in old:
            old.remove(e)
        e.list = new_list
        order = {id(x): i for i, x in enumerate(entries)}
        lst = self._members.setdefault(new_list, [])
        lst.append(e)
        lst.sort(key=lambda x: order[id(x)])


def dedup_artists(credits: Sequence[Credit]) -> list[Artist]:
    """R9.2: keep the first occurrence. A later lower/equal-precedence duplicate is removed
    (R7.6 joiner inheritance); a later higher-precedence duplicate (role upgrade) gives the kept
    entry its role, joiner and source, and the kept entry moves into that occurrence's list."""
    entries = [_Entry(c) for c in credits]
    lists = _Lists(entries)
    index: dict[str, _Entry] = {}
    for e in entries:
        # R9.2: producers are a separate credit class, deduped only among themselves.
        key = ("p:" if e.role == "producer" else "c:") + dedup_key(e.name)
        kept = index.get(key)
        if kept is None:
            index[key] = e
            continue
        if _PRECEDENCE.get(e.role, 0) > _PRECEDENCE.get(kept.role, 0):
            lists.pass_joiner(kept, kept.joiner)
            kept.role = e.role
            kept.joiner = e.joiner
            kept.source = e.source
            e.alive = False
            lists.move(kept, e.list, entries)
        else:
            lists.pass_joiner(e, e.joiner)
            e.alive = False
    return [
        Artist(
            name=e.name,
            role=cast(ArtistRole, e.role),
            joiner=e.joiner,
            source=cast(ArtistSource, e.source),
        )
        for e in entries
        if e.alive
    ]


def to_version(v: VersionDraft) -> Version:
    return Version(
        type=cast(VersionType, v.type),
        raw=v.raw,
        artists=tuple(to_artist(a) for a in v.artists),
        modifiers=tuple(v.modifiers),
        descriptor=v.descriptor,
        year=v.year,
        unknown_artist=v.unknown_artist,
        delimiter=cast(VersionDelimiter, v.delimiter),
    )


def render_version(raw: str, delimiter: str) -> str:
    """R9.4: a version rendered with its delimiter."""
    if delimiter == "(":
        return f"({raw})"
    if delimiter == "[":
        return f"[{raw}]"
    if delimiter == "{":
        return f"{{{raw}}}"
    return f"- {raw}"


def full_title_of(title: str, versions: Sequence[Version]) -> str:
    parts = [title, *(render_version(v.raw, v.delimiter) for v in versions)]
    return " ".join(p for p in parts if len(p) > 0)


class _HasPos(Protocol):
    @property
    def pos(self) -> int: ...


T = TypeVar("T", bound=_HasPos)


def by_pos(items: Sequence[T]) -> list[T]:
    """Stable sort by the ``pos`` attribute (like the JS sort)."""
    return sorted(items, key=lambda x: x.pos)
