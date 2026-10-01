"""Public API surface (mirrors js/test/api.test.ts plus Python specifics)."""

from __future__ import annotations

import dataclasses
import json
from typing import Any

import pytest

import trackparse
from runner import SPEC_DIR
from trackparse import (
    SPEC_VERSION,
    Artist,
    Flags,
    Junk,
    ParsedTrack,
    Position,
    Timestamp,
    Version,
    all_artists,
    create_parser,
    format_track,
    normalize,
    parse,
    parse_artists,
)


def test_spec_version_mirrors_spec() -> None:
    assert (SPEC_DIR / "VERSION").read_text(encoding="utf-8").strip() == SPEC_VERSION


def test_version_string() -> None:
    assert isinstance(trackparse.__version__, str)
    assert trackparse.__version__.count(".") == 2


def test_all_exports_exist() -> None:
    for name in trackparse.__all__:
        assert hasattr(trackparse, name), name


def test_parse_returns_every_key_in_schema_order() -> None:
    r = parse("")
    assert list(r.to_dict()) == [
        "input", "mode", "position", "timestamp", "artists", "title", "fullTitle",
        "versions", "year", "flags", "junk", "warnings",
    ]  # fmt: skip
    assert r.mode == "clean"
    assert r.artists == () and r.versions == () and r.junk == () and r.warnings == ()


def test_to_dict_is_canonical_json() -> None:
    r = parse("01. Fox Stevenson ft. X & Y - Bruises (Magnetude Remix) [Official Video]")
    d = r.to_dict()
    assert d["position"] == {"raw": "01", "number": 1}
    assert d["versions"][0] == {
        "type": "remix",
        "raw": "Magnetude Remix",
        "artists": [{"name": "Magnetude", "role": "remixer", "joiner": None, "source": "version"}],
        "modifiers": [],
        "descriptor": None,
        "year": None,
        "unknownArtist": False,
        "delimiter": "(",
    }
    assert d["flags"] == {
        "explicit": False,
        "clean": False,
        "unknownArtist": False,
        "unknownTitle": False,
    }
    json.dumps(d)  # plain JSON types only


@pytest.mark.parametrize(
    "s",
    [
        "",
        "[1:02:03] 01. A & B ft. C - T (X Remix) [Official Video] (2015) (Explicit) prod. by D",
        "Hey Jude - Remastered 2015",
        "Band | Song",
    ],
)
def test_round_trip_from_dict(s: str) -> None:
    r = parse(s)
    assert ParsedTrack.from_dict(r.to_dict()) == r
    assert ParsedTrack.from_dict(json.loads(json.dumps(r.to_dict()))) == r


def test_component_round_trips() -> None:
    a = Artist(name="A", role="primary", joiner=None, source="artist")
    assert Artist.from_dict(a.to_dict()) == a
    v = Version("remix", "X Remix", (a,), ("vip",), None, 2015, False, "(")
    assert Version.from_dict(v.to_dict()) == v
    assert Junk.from_dict({"raw": "x", "kind": "other"}) == Junk("x", "other")
    assert Flags.from_dict(Flags(explicit=True).to_dict()) == Flags(explicit=True)
    assert Position.from_dict({"raw": "01", "number": 1}) == Position("01", 1)
    assert Timestamp.from_dict({"raw": "3:45", "seconds": 225}) == Timestamp("3:45", 225)


def test_results_are_immutable() -> None:
    r = parse("Noisia - Stigma (VIP)")
    with pytest.raises(dataclasses.FrozenInstanceError):
        r.title = "x"  # type: ignore[misc]
    with pytest.raises(dataclasses.FrozenInstanceError):
        r.artists[0].name = "x"  # type: ignore[misc]
    assert isinstance(r.artists, tuple)
    assert isinstance(r.versions[0].modifiers, tuple)
    hash(r)


@pytest.mark.parametrize("bad", [None, 42, b"bytes", ["a"], {}])
def test_non_str_input_raises_type_error(bad: Any) -> None:
    with pytest.raises(TypeError):
        parse(bad)
    with pytest.raises(TypeError):
        parse_artists(bad)
    with pytest.raises(TypeError):
        normalize(bad)


def test_total_on_odd_strings() -> None:
    for s in ["\ud800", "", "(((", ")))", " - ", "-", "|", "\x00", "​" * 5]:
        parse(s)
        parse_artists(s)
        normalize(s)


def test_options_are_keyword_only() -> None:
    with pytest.raises(TypeError):
        parse("A - B", "youtube")  # type: ignore[misc]


def test_all_artists_merges_remixers() -> None:
    r = parse("Noisia & Phace - Stigma (Noisia Remix) [Mefjus Remix]")
    assert [(a.name, a.role) for a in all_artists(r)] == [
        ("Noisia", "primary"),
        ("Phace", "primary"),
        ("Mefjus", "remixer"),
    ]


def test_parse_artists_source_is_artist() -> None:
    artists = parse_artists("A feat. B (prod. C)")
    assert [a.role for a in artists] == ["primary", "featured", "producer"]
    assert all(a.source == "artist" for a in artists)
    assert isinstance(artists, list)


def test_create_parser_compiles_keywords_and_merges() -> None:
    p = create_parser(keywords={"version_heads": {"rmx2": "remix"}})
    assert p.parse("A - B (X Rmx2)").versions[0].type == "remix"
    assert parse("A - B (X Rmx2)").versions == ()
    with_known = p.parse("Chase & Status - Blind Faith", known_artists=["Chase & Status"])
    assert [a.name for a in with_known.artists] == ["Chase & Status"]
    assert [a.role for a in p.parse_artists("A feat. B")] == ["primary", "featured"]


def test_create_parser_none_does_not_override() -> None:
    p = create_parser(mode="youtube", known_artists=["Chase & Status"])
    r = p.parse("Chase & Status - Blind Faith", mode=None, known_artists=None)
    assert r.mode == "youtube"
    assert [a.name for a in r.artists] == ["Chase & Status"]
    assert p.parse("A - B", mode="clean").mode == "clean"


def test_create_parser_merges_keywords_per_call() -> None:
    p = create_parser(keywords={"version_heads": {"rmx2": "remix"}})
    r = p.parse("A - B (X Rmx2) [Neurohop]", keywords={"genres": ["neurohop"]})
    assert r.versions[0].type == "remix"
    assert [j.to_dict() for j in r.junk] == [{"raw": "Neurohop", "kind": "genre"}]


def test_keywords_extend_vocabulary() -> None:
    p = create_parser(
        keywords={
            "junk": {"visualiser hd": "video"},
            "genres": ["neurohop"],
            "descriptors": ["festival"],
            "feat_markers": ["featuring:"],
        }
    )
    assert [j.to_dict() for j in p.parse("A - B (Visualiser HD)").junk] == [
        {"raw": "Visualiser HD", "kind": "video"}
    ]
    assert [j.to_dict() for j in p.parse("A - B [Neurohop]").junk] == [
        {"raw": "Neurohop", "kind": "genre"}
    ]
    assert p.parse("A - B (Festival Mix)").versions[0].modifiers == ("festival",)
    assert [a.role for a in p.parse("A featuring: C - B").artists] == ["primary", "featured"]


def test_bad_options_fall_back() -> None:
    r = parse("A - B", known_artists=[1, None], keywords={"genres": 5})  # type: ignore[list-item, typeddict-item]
    assert [a.name for a in r.artists] == ["A"]


def test_format_track_defaults_and_options() -> None:
    assert format_track(parse("01. Noisia - Stigma (VIP)"), position=True) == (
        "01. Noisia - Stigma (VIP)"
    )
    r = parse("A; B ft. C - T (D Remix)")
    assert format_track(r, joiners="canonical") == "A, B feat. C - T (D Remix)"
    assert format_track(r, versions="none", feat="omit") == "A; B - T"
    assert format_track(r, feat="title", feat_marker="with") == "A; B - T (with C) (D Remix)"
