"""R7.3–R7.5: splitting a credit list into names."""

from __future__ import annotations

from typing import NamedTuple, Optional

from ._context import Ctx, match_known_at
from ._scanner import Skel, render_skel, slice_skel, trim_skel
from ._tables import is_unknown_token
from ._words import (
    char_at,
    collapse_spaces,
    in_vocab,
    is_all_digits,
    is_digit,
    is_whitespace,
    r0_trim_end,
    word_key,
    word_matches,
    year_of,
)


class Credit:
    """An artist credit plus its absolute offset, used for ordering (R9.1)."""

    __slots__ = ("joiner", "list", "name", "pos", "role", "source")

    def __init__(
        self, name: str, role: str, joiner: Optional[str], source: str, pos: int, list: int
    ) -> None:
        self.name = name
        self.role = role
        self.joiner = joiner
        self.source = source
        self.pos = pos
        #: Id of the credit list it came from (R7.6 joiner inheritance in R9.2).
        self.list = list


class PositionedYear(NamedTuple):
    """A year with its absolute offset (R9.6: the track year is the first in text order)."""

    year: int
    pos: int


class ListOptions(NamedTuple):
    role: str
    source: str
    #: Joiner of the first name: ``None``, or the feat / producer marker as written.
    lead: Optional[str]
    #: R7.3: ``with`` splits only in the main list of the artist side.
    split_with: bool
    #: R7.2: later feat markers act as joiners inside a featured list.
    feat_markers_join: bool


class ListResult(NamedTuple):
    credits: list[Credit]
    #: R7.5: an unknown token was dropped.
    unknown: bool
    #: R7.5: dropped year-only names.
    years: list[PositionedYear]


class _JoinerHit(NamedTuple):
    start: int
    end: int
    raw: str


_AND_LIKE = frozenset(["&", "and", "+"])


def _word_at(text: str, start: int) -> int:
    end = start
    n = len(text)
    while end < n and not is_whitespace(text[end]):
        end += 1
    return end


def _next_word(text: str, frm: int) -> str:
    """The word after a joiner; a tight joiner (``,`` ``;``) ends it, since the name ends there."""
    n = len(text)
    a = frm
    while a < n and is_whitespace(text[a]):
        a += 1
    b = a
    while b < n and not is_whitespace(text[b]) and text[b] != "," and text[b] != ";":
        b += 1
    return text[a:b]


def _find_joiner(
    text: str, frm: int, split_at_comma: bool, opts: ListOptions, ctx: Ctx
) -> Optional[_JoinerHit]:
    """R7.3b: the leftmost joiner occurrence at or after ``frm``, honouring the guards."""
    n = len(text)
    for p in range(max(frm, 0), n):
        ch = text[p]
        if ch == "," or ch == ";":
            if ch == "," and is_digit(char_at(text, p - 1)) and is_digit(char_at(text, p + 1)):
                continue  # `1,000`
            return _JoinerHit(p, p + 1, ch)
        if p == 0 or text[p - 1] != " " or is_whitespace(ch):
            continue
        end = _word_at(text, p)
        if end >= n or text[end] != " ":
            continue  # spaced joiners need a space after
        word = text[p:end]
        if _spaced_joiner(word, text, end, split_at_comma, opts, ctx):
            return _JoinerHit(p, end, word)
    return None


def _spaced_joiner(
    word: str, text: str, end: int, split_at_comma: bool, opts: ListOptions, ctx: Ctx
) -> bool:
    if opts.feat_markers_join and ctx.t.feat_markers.match_at([word], 0) is not None:
        return True
    for j in ctx.t.spaced_joiners:
        matches = word == j.raw if j.case_sensitive else word_matches(word, j.raw)
        if not matches:
            continue
        if j.raw == "with" and not opts.split_with:
            return False
        if j.is_and:
            if ctx.split_and == "never":
                return False
            if ctx.split_and == "auto" and not split_at_comma:
                return False
        return not (j.raw in _AND_LIKE and in_vocab(_next_word(text, end), ctx.t.no_split_before))
    return False


def clean_name(raw: str) -> str:
    """R7.4: clean one raw name."""
    name = collapse_spaces(raw)
    first = name[:1]
    if len(name) >= 2 and (first == '"' or first == "'") and name[-1] == first:
        name = collapse_spaces(name[1:-1])
    while name.endswith(",") or name.endswith(";"):
        name = r0_trim_end(name[:-1])
    space = name.find(" ")
    if space > 0 and word_key(name[:space]) == "by":
        name = name[space + 1 :]
    return collapse_spaces(name)


class _RawName(NamedTuple):
    name: str
    joiner: Optional[str]
    pos: int


def _unwrap_list(lst: Skel) -> Skel:
    """R7.4: strip one pair of wrapping quotes from the whole list (no other such quote inside)."""
    t = trim_skel(lst)
    text = t.text
    q = text[:1]
    if len(text) < 2 or (q != '"' and q != "'") or text[-1] != q:
        return t
    if text.find(q, 1) != len(text) - 1:
        return t
    return trim_skel(slice_skel(t, 1, len(text) - 1))


def split_credits(inp: Skel, opts: ListOptions, ctx: Ctx) -> ListResult:
    """R7.3: split ``inp`` into credits."""
    lst = _unwrap_list(inp)  # R7.4: `"Camo & Krooked"`
    text = lst.text
    n = len(text)
    raw: list[_RawName] = []
    joiner = opts.lead
    split_at_comma = False
    start = 0
    while start <= n:
        while start < n and is_whitespace(text[start]):
            start += 1
        known = match_known_at(text, start, ctx, "-")  # R7.3a
        hit = _find_joiner(text, start + known, split_at_comma, opts, ctx)
        end = hit.start if hit is not None else n
        pos = lst.pos[start] if start < len(lst.pos) else lst.pos[-1] if lst.pos else 0
        raw.append(_RawName(clean_name(render_skel(slice_skel(lst, start, end))), joiner, pos))
        if hit is None:
            break
        if hit.raw == "," or hit.raw == ";":
            split_at_comma = True  # R7.3: `and` after a tight joiner
        joiner = hit.raw
        start = hit.end
    return _finish_names(raw, opts, ctx)


def _finish_names(raw: list[_RawName], opts: ListOptions, ctx: Ctx) -> ListResult:
    """R7.4 empty drops, R7.5 year/unknown drops. R7.6: the first survivor takes the lead
    joiner (``None``, or the feat/producer marker)."""
    named = [r for r in raw if len(r.name) > 0]
    unknown = False
    years: list[PositionedYear] = []
    kept: list[_RawName] = []
    for r in named:
        if is_unknown_token(r.name, ctx.t):
            unknown = True
            continue
        year = year_of(r.name) if is_all_digits(r.name) else None
        if year is not None and len(named) > 1:
            years.append(PositionedYear(year, r.pos))
            continue
        kept.append(r)
    list_id = ctx.list_seq
    ctx.list_seq += 1
    credits = [
        Credit(r.name, opts.role, opts.lead if i == 0 else r.joiner, opts.source, r.pos, list_id)
        for i, r in enumerate(kept)
    ]
    return ListResult(credits, unknown, years)
