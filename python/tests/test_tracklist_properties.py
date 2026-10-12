"""SPEC.md R13.11 tracklist invariants as Hypothesis properties (mirrors
js/test/tracklist.properties.test.ts)."""

from __future__ import annotations

import json
import time
from typing import Any, Callable, Optional

from hypothesis import HealthCheck, given, settings
from hypothesis import strategies as st
from jsonschema import Draft202012Validator
from referencing import Registry, Resource

from runner import SPEC_DIR
from trackparse import parse
from trackparse.tracklist import apply_edits, parse_tracklist


def _load(name: str) -> dict[str, Any]:
    data: dict[str, Any] = json.loads((SPEC_DIR / "schema" / name).read_text(encoding="utf-8"))
    return data


_TRACKLIST = _load("tracklist.schema.json")
_TRACK = _load("parsed-track.schema.json")
_REGISTRY: Registry = Registry().with_resources(
    [
        (_TRACKLIST["$id"], Resource.from_contents(_TRACKLIST)),
        (_TRACK["$id"], Resource.from_contents(_TRACK)),
    ]
)
TRACKLIST_VALIDATOR = Draft202012Validator(_TRACKLIST, registry=_REGISTRY)
EDIT_RESULT_VALIDATOR = Draft202012Validator(
    {"$ref": f"{_TRACKLIST['$id']}#/$defs/editResult"}, registry=_REGISTRY
)

# Fragments that exercise the line splitter, the time scanner and the line kinds.
FRAGMENTS = [
    "\n", "\r\n", "\r", "\n\r", " ", " ", "\u0085", " ", "  ", "\t", " ",
    "00:00", "0:00 ", "04:37 ", "1:02:03 ", "01:02:03.5 ", "125:30 ", "12:34,250", "1:75 ",
    "0:61:00 ", "1:2", "[", "]", "(", ")", "[00:10] ", "(1:00)", " - ", " – ", "-", "|", ".",
    ":", "=> ", "==>", "1 ", "2 ", "01. ", "3) ", "Artist", "Title", "Noisia - Stigma", "Intro",
    "interview", "Tracklist:", "Part 2", "https://x.y", "www.", "_-_", "Ab-Cd", "ID", "(VIP)",
    "​", "𝄞", "\ud800",
]  # fmt: skip

noisy_text = st.lists(st.one_of(st.sampled_from(FRAGMENTS), st.text(max_size=4)), max_size=40).map(
    "".join
)
any_text = st.one_of(st.text(max_size=80), noisy_text)


def _pad2(n: int) -> str:
    return str(n).rjust(2, "0")


def write_time(style: str, seconds: int, frac: int) -> str:
    h = seconds // 3600
    m = (seconds % 3600) // 60
    s = seconds % 60
    if style == "M:SS":
        return f"{seconds // 60}:{_pad2(s)}"
    if style == "MM:SS":
        return f"{_pad2(seconds // 60)}:{_pad2(s)}"
    if style == "H:MM:SS":
        return f"{h}:{_pad2(m)}:{_pad2(s)}"
    if style == "HH:MM:SS":
        return f"{_pad2(h)}:{_pad2(m)}:{_pad2(s)}"
    if style == "MM:SS.d":
        return f"{_pad2(seconds // 60)}:{_pad2(s)}.{frac % 10}"
    return f"{seconds // 60}:{_pad2(s)},{str(frac).rjust(3, '0')}"


REST_TEXT = [
    "Noisia - Stigma",
    "01. Phace - Plastic Acid",
    "2 Unlimited - No Limit",
    "Intro",
    "Paperclip interview",
    "Mefjus - Suicide Bassline (VIP)",
    "",
    "ID - ID",
]


@st.composite
def timed_line(draw: Any) -> str:
    style = draw(st.sampled_from(["M:SS", "MM:SS", "H:MM:SS", "HH:MM:SS", "MM:SS.d", "M:SS,ddd"]))
    t = write_time(style, draw(st.integers(0, 3599)), draw(st.integers(0, 999)))
    gap = draw(st.sampled_from([" ", "  ", " - ", " | ", " => "]))
    rest = draw(st.sampled_from(REST_TEXT))
    lead = draw(st.sampled_from(["", " ", "["]))
    return f"[{t}]{gap}{rest}" if lead == "[" else f"{lead}{t}{gap}{rest}"


list_line = st.one_of(
    timed_line(),
    timed_line(),
    timed_line(),
    timed_line(),
    timed_line(),
    timed_line(),
    st.sampled_from(["", "Tracklist:", "a note", "https://example.com"]),
)


@st.composite
def timed_list(draw: Any) -> str:
    lines = draw(st.lists(list_line, min_size=1, max_size=12))
    eol = draw(st.sampled_from(["\n", "\r\n"]))
    trailing = draw(st.booleans())
    return eol.join(lines) + (eol if trailing else "")


def line_count(text: str) -> int:
    return len(parse_tracklist(text).entries)


def raw_lines(text: str) -> list[str]:
    return [e.raw + e.eol for e in parse_tracklist(text).entries]


def _errors(validator: Draft202012Validator, value: Any) -> list[str]:
    return [
        f"{'/'.join(map(str, e.path)) or '$'}: {e.message}" for e in validator.iter_errors(value)
    ]


def fast(examples: int = 100) -> settings:
    return settings(
        max_examples=examples, deadline=None, suppress_health_check=[HealthCheck.too_slow]
    )


# 1. Lossless (R13.1.2) and parts (R13.1.3)


@fast(1500)
@given(any_text)
def test_lossless_and_parts(text: str) -> None:
    tl = parse_tracklist(text)
    assert "".join(e.raw + e.eol for e in tl.entries) == text
    for e in tl.entries:
        p = e.parts
        assert p.lead + p.time + p.gap + p.rest == e.raw
        if e.time is None:
            assert p.rest == e.raw
    assert tl.stats.lines == len(tl.entries)


def test_only_lf_crlf_cr_break_lines() -> None:
    assert line_count("a b c\u0085d") == 1
    assert line_count("a\r\nb\rc\nd\n") == 4
    assert line_count("") == 0


# 2. Totality


@fast(800)
@given(any_text)
def test_any_text_matches_schema(text: str) -> None:
    errors = _errors(TRACKLIST_VALIDATOR, parse_tracklist(text).to_dict())
    assert not errors, errors[:5]


field = st.one_of(
    st.integers(-3, 15),
    st.floats(),
    st.text(max_size=6),
    st.sampled_from([None, float("nan"), float("inf"), 1e300, -1e300, [1, 2], True, 10**400]),
)
_EDIT_KEYS = ("line", "seconds", "lines", "from", "rest", "after", "text")


@st.composite
def edit(draw: Any) -> Any:
    if draw(st.integers(0, 9)) == 0:
        return draw(
            st.one_of(st.none(), st.integers(), st.text(max_size=4), st.lists(st.integers()))
        )
    e: dict[str, Any] = {
        "op": draw(
            st.sampled_from(
                ["set-time", "shift", "sort-by-time", "set-rest", "insert", "delete", "x"]
            )
        )
    }
    for key in _EDIT_KEYS:
        if not draw(st.booleans()):
            continue
        if key == "lines":
            e[key] = draw(st.one_of(st.lists(field, max_size=4), field))
        elif key in ("rest", "text"):
            e[key] = draw(st.one_of(st.text(max_size=8), st.sampled_from(["a\nb", "c\rd"]), field))
        else:
            e[key] = draw(field)
    return e


@fast(800)
@given(st.one_of(timed_list(), any_text), st.lists(edit(), max_size=6))
def test_any_edit_list_returns(text: str, edits: list[Any]) -> None:
    r = apply_edits(text, edits)
    errors = _errors(EDIT_RESULT_VALIDATOR, r.to_dict())
    assert not errors, errors[:5]
    n = line_count(r.text)
    assert all(line <= n for line in r.changed)


def test_bad_options_are_ignored() -> None:
    text = "0:00 A - B\n1:00 C - D"
    base = parse_tracklist(text).to_dict()
    bad: dict[str, Any] = {
        "mode": "nope",
        "known_artists": "x",
        "keywords": [1],
        "split_and": 3,
        "audio_seconds": float("nan"),
        "marker_words": "Intro",
    }
    assert parse_tracklist(text, **bad).to_dict() == base
    assert apply_edits(text, "nope").to_dict() == {"text": text, "changed": [], "warnings": []}  # type: ignore[arg-type]


def _best(f: Callable[[], object], runs: int = 3) -> float:
    """Best of a few runs, so a busy CI worker does not fail the run; a blowup still would."""
    best = float("inf")
    for _ in range(runs):
        t0 = time.perf_counter()
        f()
        best = min(best, time.perf_counter() - t0)
    return best


def test_ten_thousand_lines_are_fast() -> None:
    lines = []
    for i in range(10_000):
        s = (i * 37) % 7200
        t = f"{_pad2(s // 3600)}:{_pad2((s % 3600) // 60)}:{_pad2(s % 60)}"
        lines.append(f"{t} {i % 100} Artist {i} & Other - Title {i} (VIP Mix)")
    text = "\r\n".join(lines)
    small = "\r\n".join(lines[:1000])
    parse_tracklist(small)  # warm-up
    assert len(parse_tracklist(text).entries) == 10_000
    # R13.11.2: the JS reference does this well under one second. CPython runs the full
    # single-line parser per line (~0.4 ms), so assert linear scaling plus a generous ceiling:
    # a super-linear blowup still fails.
    big = _best(lambda: parse_tracklist(text), runs=2)
    assert big < 20 * _best(lambda: parse_tracklist(small))
    assert big < 15.0
    assert (
        _best(lambda: apply_edits(text, [{"op": "shift", "seconds": 5}, {"op": "sort-by-time"}]))
        < 2.0
    )


# 3. Locality and identity (R13.9.1)


@fast()
@given(any_text)
def test_identity(text: str) -> None:
    assert apply_edits(text, []).to_dict() == {"text": text, "changed": [], "warnings": []}


@fast()
@given(
    timed_list(),
    st.integers(min_value=0),
    st.integers(-4000, 4000),
    st.sampled_from(["set-time", "set-rest", "shift"]),
)
def test_locality_single_line(text: str, pick: int, seconds: int, op: str) -> None:
    before = raw_lines(text)
    if not before:  # a list made of one empty line has no lines
        return
    line = pick % len(before) + 1
    e: dict[str, Any]
    if op == "set-time":
        e = {"op": "set-time", "line": line, "seconds": seconds}
    elif op == "set-rest":
        e = {"op": "set-rest", "line": line, "rest": "New - Rest"}
    else:
        e = {"op": "shift", "seconds": seconds, "lines": [line]}
    r = apply_edits(text, [e])
    after = raw_lines(r.text)
    assert len(after) == len(before)
    for i, row in enumerate(after):
        if i != line - 1:
            assert row == before[i]
    assert all(c == line for c in r.changed)
    if any(w.code == "edit-invalid" for w in r.warnings):
        assert r.text == text


@fast()
@given(timed_list(), st.integers(min_value=0))
def test_locality_insert_delete(text: str, pick: int) -> None:
    before = [e.raw for e in parse_tracklist(text).entries]
    if not before:  # a list made of one empty line has no lines
        return
    at = pick % (len(before) + 1)
    ins = apply_edits(text, [{"op": "insert", "after": at, "text": "00:00 New - Line"}])
    expected = [*before[:at], "00:00 New - Line", *before[at:]]
    assert [e.raw for e in parse_tracklist(ins.text).entries] == expected
    assert at + 1 in ins.changed
    line = pick % len(before) + 1
    dele = apply_edits(text, [{"op": "delete", "line": line}])
    kept = [r for i, r in enumerate(before) if i != line - 1]
    got = [e.raw for e in parse_tracklist(dele.text).entries]
    # Deleting the last line moves its eol ("" here) up: an empty line that becomes last
    # without an eol is not a line (R13.9.5), so trailing empty lines may disappear.
    while kept and kept[-1] == "" and len(got) < len(kept) and line == len(before):
        kept.pop()
    assert got == kept


# 4. Round trip (R13.9.7)


@fast()
@given(timed_list(), st.integers(1, 5000), st.one_of(st.none(), st.integers(1, 12)))
def test_shift_round_trip(text: str, s: int, frm: Optional[int]) -> None:
    n = line_count(text)
    target: dict[str, Any] = {} if frm is None or frm > n else {"from": frm}
    up = apply_edits(text, [{"op": "shift", "seconds": s, **target}])
    down = apply_edits(up.text, [{"op": "shift", "seconds": -s, **target}])
    lossy = any(
        w.code in ("time-clamped", "style-upgraded") for w in (*up.warnings, *down.warnings)
    )

    def widths(t: str) -> list[Optional[int]]:
        return [None if e.time is None else len(e.time.raw) for e in parse_tracklist(t).entries]

    grew = widths(up.text) != widths(text)
    if not lossy and not grew:
        assert down.text == text


@fast()
@given(timed_list())
def test_sort_keeps_eol_slots_and_is_idempotent(text: str) -> None:
    once = apply_edits(text, [{"op": "sort-by-time"}])

    def eols(t: str) -> list[str]:
        return [e.eol for e in parse_tracklist(t).entries]

    # An empty line sorted into the last slot (eol "") is not a line any more (R13.1.1).
    if len(eols(once.text)) == len(eols(text)):
        assert eols(once.text) == eols(text)
    assert apply_edits(once.text, [{"op": "sort-by-time"}]).text == once.text


# 5. One line equals parse(rest, mode="tracklist")


@fast(1500)
@given(st.one_of(noisy_text, st.tuples(list_line, noisy_text).map("".join)))
def test_one_line_equals_parse(text: str) -> None:
    text = text.replace("\r", " ").replace("\n", " ")
    entries = parse_tracklist(text).entries
    if not entries or entries[0].track is None:
        return
    e = entries[0]
    assert e.track == parse(e.parts.rest, mode="tracklist")
