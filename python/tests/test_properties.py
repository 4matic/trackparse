"""SPEC.md "Invariants" as Hypothesis properties (mirrors js/test/properties.test.ts)."""

from __future__ import annotations

import json
import time
from dataclasses import replace
from typing import Any, Optional

from hypothesis import HealthCheck, assume, given, settings
from hypothesis import strategies as st
from jsonschema import Draft202012Validator

from runner import SPEC_DIR
from trackparse import ParsedTrack, format_track, normalize, parse
from trackparse._words import dedup_key

TRACK_VALIDATOR = Draft202012Validator(
    json.loads((SPEC_DIR / "schema" / "parsed-track.schema.json").read_text(encoding="utf-8"))
)

# Characters that exercise every scanner branch.
TRICKY = [
    " ", "  ", "-", " - ", " – ", "—", "(", ")", "[", "]", "{", "}", '"', "'", "“", "|",
    " | ", ",", ";", "&", " & ", " x ", " feat. ", " ft ", "prod. by ", " by ", ".", ":", "_",
    "0", "1", "01. ", "3:45 ", "Remix", " Mix", "VIP", "Official Video", "ID", "​",
    " ", "", "é", "é", "𝄞", "\ud800", "×", "／", ".mp3",
]  # fmt: skip

noisy_string = st.lists(st.one_of(st.sampled_from(TRICKY), st.text(max_size=4)), max_size=24).map(
    "".join
)
any_string = st.one_of(st.text(max_size=60), noisy_string)

ARTISTS = ["Noisia", "Phace", "Fox Stevenson", "Sub Focus", "Mefjus", "Camo & Krooked",
           "Alix Perez"]  # fmt: skip
TITLES = ["Tentacles", "Bruises", "Out the Blue", "Dead Limit", "Stigma", "Blind Faith"]
REMIXERS = ["Skrillex", "Magnetude", "Wilkinson", "Break"]
#: R8.7 record labels after a spaced bullet.
RECORD_LABELS = ["VISION", "HOSPITAL", "GEMINI GEMINI", "SIX EIGHT", "Ø"]
#: R8.7.4 notes: tail segments after the label (junk kind `note`).
LABEL_NOTES = ["VISION RADIO RELOAD", "HOSPITAL", "S06E39"]
#: R8.7.4 junk groups inside a tail segment.
TAIL_JUNK_GROUPS = ["[HD]", "(Official Video)"]
#: R8.7.4 tail segments made only of a junk phrase.
TAIL_JUNK_PHRASES = ["OUT NOW", "FREE DOWNLOAD"]
#: R8.7.3 title parts that leave the title empty (`A - (VIP) • L`, `A - [HD] • L`).
EMPTY_TITLES = ["(VIP)", "[HD]"]


@st.composite
def track(draw: Any, labels: bool = True) -> str:
    """A track-shaped input; with `labels`, an R8.7 bullet suffix in some of the cases."""

    def sometimes(items: list[str], percent: int) -> Optional[str]:
        if draw(st.integers(0, 99)) >= percent:
            return None
        return draw(st.sampled_from(items))

    artists = draw(st.lists(st.sampled_from(ARTISTS), min_size=1, max_size=3))
    joiner = draw(st.sampled_from([" & ", ", ", " x ", " vs. "]))
    feat: Optional[str] = draw(st.one_of(st.none(), st.sampled_from(ARTISTS)))
    title = draw(st.sampled_from(TITLES))
    remix: Optional[str] = draw(st.one_of(st.none(), st.sampled_from(REMIXERS)))
    label = sometimes(RECORD_LABELS, 40) if labels else None
    label_group = sometimes(TAIL_JUNK_GROUPS, 25)
    notes = draw(st.lists(st.sampled_from(LABEL_NOTES), max_size=2))
    phrase = sometimes(TAIL_JUNK_PHRASES, 25) if labels else None
    phrase_at = draw(st.integers(min_value=0, max_value=2))
    empty = sometimes(EMPTY_TITLES, 15)
    unique = list(dict.fromkeys(artists))
    s = joiner.join(unique)
    if feat and feat not in unique:
        s += f" feat. {feat}"
    # R8.7: the empty title only comes with a label, as in `A - [HD] • L`.
    if label and empty:
        s += f" - {empty}"
    else:
        s += f" - {title}"
        if remix:
            s += f" ({remix} Remix)"
    tail: list[str] = []
    if label:
        tail = [f"{label} {label_group}" if label_group else label, *notes]
    if phrase:
        tail.insert(phrase_at % (len(tail) + 1), phrase)
    for segment in tail:
        s += f" • {segment}"
    return s


def without_input(t: ParsedTrack) -> ParsedTrack:
    return replace(t, input="")


FAST = settings(max_examples=300, deadline=None, suppress_health_check=[HealthCheck.too_slow])


@FAST
@given(any_string)
def test_1_totality(s: str) -> None:
    parse(s)
    parse(s, mode="youtube", uploader=s)
    parse(s, mode="filename", known_artists=[s])


def test_1_totality_linear_time() -> None:
    blocks = ["(", "a - ", "A & B, ", "feat. X ", "[Remix] ", "-a", " by x", '"q" ', "x" * 7,
              "prod. A feat. B ", "a by me ", "(x) - ", "Remix - ", "Official Video ",
              "x - Official Video "]  # fmt: skip
    for block in blocks:
        inp = (block * (10_000 // len(block) + 1))[:10_000]
        parse(inp)  # warm-up
        t0 = time.perf_counter()
        parse(inp, mode="youtube")
        elapsed = time.perf_counter() - t0
        # Generous bound: catches super-linear blowups, not CI noise.
        assert elapsed < 1.0, (block, elapsed)


def test_1_repeated_trailing_junk_is_linear() -> None:
    # R8.4 once rescanned the whole title per stripped phrase (quadratic). 40k code points of
    # trailing junk took ~10 s before the fix; linear code needs well under a second.
    inp = "A - T " + "Official Video " * 2_666
    t0 = time.perf_counter()
    track = parse(inp, mode="youtube")
    elapsed = time.perf_counter() - t0
    assert track.title == "T"
    assert len(track.junk) == 2_666
    assert elapsed < 1.0, elapsed


@settings(max_examples=1000, deadline=None)
@given(any_string)
def test_2_normalize_idempotent(s: str) -> None:
    assert normalize(normalize(s)) == normalize(s)


@FAST
@given(track(), st.integers(min_value=0))
def test_3_noise_insensitive(s: str, seed: int) -> None:
    base = without_input(parse(s))
    cut = seed % (len(s) + 1)
    noisy = [
        "  ".join(s.split(" ")),
        " – ".join(s.split(" - ")),
        " — ".join(s.split(" - ")),
        f"{s[:cut]}​{s[cut:]}",
    ]
    for n in noisy:
        assert without_input(parse(n)) == base


@FAST
@given(any_string)
def test_4_schema(s: str) -> None:
    errors = [e.message for e in TRACK_VALIDATOR.iter_errors(parse(s).to_dict())]
    assert errors == []


@FAST
@given(st.one_of(any_string, track()))
def test_5_artist_hygiene(s: str) -> None:
    keys = set()
    for a in parse(s).artists:
        assert len(a.name) > 0
        assert a.name == a.name.strip(" ")
        # R9.2: producers are deduped only among producers.
        k = ("p:" if a.role == "producer" else "c:") + dedup_key(a.name)
        assert k not in keys, f"duplicate {a.name}"
        keys.add(k)


quiet = track().filter(lambda s: len(parse(s).warnings) == 0)
#: Without a bullet suffix (R8.7), for the cases that need the string to end in a version group.
quiet_unlabelled = track(labels=False).filter(lambda s: len(parse(s).warnings) == 0)


@FAST
@given(quiet)
def test_7_prefix_position(s: str) -> None:
    a = parse(s)
    b = parse(f"01. {s}")
    assert b.position is not None and b.position.to_dict() == {"raw": "01", "number": 1}
    assert replace(without_input(b), position=None) == without_input(a)


@FAST
@given(quiet)
def test_7_append_official_video(s: str) -> None:
    a = parse(s)
    b = parse(f"{s} (Official Video)")
    assert [j.to_dict() for j in b.junk] == [
        *(j.to_dict() for j in a.junk),
        {"raw": "Official Video", "kind": "video"},
    ]
    assert replace(without_input(b), junk=a.junk, mode=a.mode) == without_input(a)


@FAST
@given(quiet_unlabelled)
def test_7_bracket_swap(s: str) -> None:
    assume(s.endswith(" Remix)"))
    a = parse(s)
    i = s.rindex("(")
    b = parse(f"{s[:i]}[{s[i + 1 : -1]}]")
    vb = tuple(replace(v, delimiter="(") for v in b.versions)
    assert replace(without_input(b), full_title=a.full_title, versions=vb) == without_input(a)


@FAST
@given(quiet_unlabelled)
def test_7_case_swap(s: str) -> None:
    assume(s.endswith(" Remix)"))
    a = parse(s)
    b = parse(s[: -len(" Remix)")] + " REMIX)")
    vb = tuple(
        replace(v, raw=a.versions[i].raw if i < len(a.versions) else "")
        for i, v in enumerate(b.versions)
    )
    assert replace(without_input(b), full_title=a.full_title, versions=vb) == without_input(a)


_Shape = tuple[list[tuple[str, str]], Optional[str], list[tuple[str, list[str]]], Optional[str]]


def _shape(t: ParsedTrack, both: bool) -> _Shape:
    return (
        [(x.name, x.role) for x in t.artists],
        t.title if both else None,
        [(v.type, [x.name for x in v.artists]) for v in t.versions],
        t.label if both else None,
    )


@FAST
@given(track())
def test_8_round_trip(s: str) -> None:
    a = parse(s)
    if a.warnings:
        return
    b = parse(format_track(a))
    # R11.6: with a label, the label and the title only count when the formatted string has an
    # artist part and the title is not empty (`A - [HD] • L` formats to `A - • L`).
    artist_part = any(
        x.role == "primary" or (x.role == "featured" and x.source == "artist") for x in a.artists
    )
    both = (artist_part and a.title != "") or a.label is None
    assert _shape(b, both) == _shape(a, both)
