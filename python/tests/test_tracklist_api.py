"""trackparse.tracklist API surface (SPEC.md R13; mirrors js/test/tracklist.api.test.ts)."""

from __future__ import annotations

import dataclasses
from typing import Any

import pytest

import trackparse
import trackparse.tracklist as tracklist
from trackparse import SPEC_VERSION, parse
from trackparse.tracklist import (
    EditResult,
    Parts,
    TimeValue,
    Tracklist,
    TracklistEntry,
    apply_edits,
    parse_tracklist,
)


def test_public_functions() -> None:
    public = {n for n in tracklist.__all__ if not n[0].isupper()}
    assert public == {"apply_edits", "parse_tracklist"}
    for name in tracklist.__all__:
        assert hasattr(tracklist, name)


def test_root_does_not_export_tracklist_functions() -> None:
    for name in ("parse_tracklist", "apply_edits", "ListParser"):
        assert name not in trackparse.__all__
    assert not hasattr(trackparse, "parse_tracklist")


def test_parse_tracklist_returns_every_key() -> None:
    tl = parse_tracklist("00:00 Noisia - Stigma\n")
    assert isinstance(tl, Tracklist)
    d = tl.to_dict()
    assert list(d) == ["specVersion", "entries", "warnings", "stats"]
    assert d["specVersion"] == SPEC_VERSION
    assert list(d["entries"][0]) == ["line", "kind", "eol", "raw", "parts", "time", "track"]
    assert list(d["stats"]) == ["lines", "tracks", "markers", "timed", "firstTime", "lastTime"]
    entry = tl.entries[0]
    assert isinstance(entry, TracklistEntry)
    assert entry.parts == Parts("", "00:00", " ", "Noisia - Stigma")
    assert entry.time == TimeValue("00:00", 0)
    assert entry.track is not None and entry.track.mode == "tracklist"


def test_results_are_frozen_with_tuples() -> None:
    tl = parse_tracklist("0:00 A - B\n0:00 C - D")
    assert isinstance(tl.entries, tuple) and isinstance(tl.warnings, tuple)
    with pytest.raises(dataclasses.FrozenInstanceError):
        tl.entries[0].raw = "x"  # type: ignore[misc]
    r = apply_edits("0:00 a", [{"op": "set-time", "line": 1, "seconds": 5}])
    assert isinstance(r, EditResult)
    assert isinstance(r.changed, tuple) and isinstance(r.warnings, tuple)


def test_tracklist_mode_in_parse() -> None:
    r = parse("Strobe - Radio Edit", mode="tracklist")
    assert r.mode == "tracklist"
    assert [a.name for a in r.artists] == ["Strobe"]
    assert r.title == "Radio Edit"
    assert parse("Strobe - Radio Edit").title == "Strobe"
    # `auto` never resolves to tracklist (R2.1).
    assert parse("00:00 A - B").mode != "tracklist"


def test_no_public_list_context_switch() -> None:
    assert parse("Optiv-Inception", mode="tracklist").artists == ()
    with pytest.raises(TypeError):
        parse("Optiv-Inception", mode="tracklist", list_context=True)  # type: ignore[call-arg]
    tl = parse_tracklist("0:00 A - B\n1:00 Optiv-Inception")
    track = tl.entries[1].track
    assert track is not None and [a.name for a in track.artists] == ["Optiv"]


def test_options_pass_through() -> None:
    text = "0:00 Chase & Status - Blind Faith\n5:00 Station ident\n9:00 X - Y"
    tl = parse_tracklist(
        text, known_artists=["Chase & Status"], marker_words=["ident"], audio_seconds=400
    )
    first = tl.entries[0].track
    assert first is not None and [a.name for a in first.artists] == ["Chase & Status"]
    assert tl.entries[1].kind == "marker"
    assert [(w.code, w.line) for w in tl.warnings] == [("time-after-audio-end", 3)]
    clean = parse_tracklist("0:00 Strobe - Radio Edit", mode="clean").entries[0].track
    assert clean is not None and clean.title == "Strobe"


def test_fractional_seconds_are_correctly_rounded() -> None:
    tl = parse_tracklist("0:07.500 A - B\n0:08,1 C - D\n0:09.000 E - F")
    seconds = [e.time.seconds for e in tl.entries if e.time is not None]
    assert seconds == [7.5, 8.1, 9]
    assert tl.to_dict()["entries"][2]["time"]["seconds"] == 9
    assert isinstance(tl.to_dict()["entries"][2]["time"]["seconds"], int)


def test_edit_keys_are_camel_case() -> None:
    text = "0:00 a\n1:00 b\n2:00 c"
    r = apply_edits(text, [{"op": "shift", "seconds": 10, "from": 2}])
    assert r.text == "0:00 a\n1:10 b\n2:10 c"
    assert r.changed == (2, 3)
    r = apply_edits(text, [{"op": "insert", "after": 3, "text": "3:00 d"}])
    assert r.to_dict() == {"text": text + "\n3:00 d", "changed": [3, 4], "warnings": []}


@pytest.mark.parametrize(
    "edits",
    [
        [None],
        [{"op": "nope", "line": 2}],
        [{"op": "set-time", "line": 1, "seconds": float("inf")}],
        [{"op": "set-time", "line": 1, "seconds": True}],
        [{"op": "set-time", "line": 1, "seconds": 10**400}],
        [{"op": "shift", "seconds": 1, "lines": None}],
        [{"op": "set-rest", "line": 1, "rest": "a\nb"}],
        [{"op": "delete", "line": 2.5}],
    ],
)
def test_invalid_edits_change_nothing(edits: list[Any]) -> None:
    text = "0:00 a\n1:00 b"
    r = apply_edits(text, edits)
    assert r.text == text
    assert r.changed == ()
    assert [w.code for w in r.warnings] == ["edit-invalid"]


def test_invalid_edit_line_field() -> None:
    r = apply_edits(
        "a", [{"op": "delete", "line": 3.0}, {"op": "insert", "after": 9, "text": "\n"}]
    )
    assert [(w.edit, w.line) for w in r.warnings] == [(0, 3), (1, 9)]
    assert all(isinstance(w.line, int) for w in r.warnings)


@pytest.mark.parametrize("bad", [None, 1, b"0:00 a", ["0:00 a"]])
def test_non_str_text_raises_type_error(bad: Any) -> None:
    with pytest.raises(TypeError):
        parse_tracklist(bad)
    with pytest.raises(TypeError):
        apply_edits(bad, [])


def test_never_raises_on_odd_strings() -> None:
    for text in ["", "\r", "\n\n", "\ud800", "0:00 x", "[1:00", "99:59:59.999 x" * 50]:
        parse_tracklist(text)
        apply_edits(text, [{"op": "shift", "seconds": 1e308}, {"op": "sort-by-time"}])
