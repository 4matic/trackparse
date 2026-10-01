"""The fixture matcher (spec/fixtures/README.md)."""

from __future__ import annotations

from match import match, match_exact, match_partial


def test_partial_ignores_unlisted_keys() -> None:
    assert match_partial({"a": 1}, {"a": 1, "b": 2}) is None


def test_partial_missing_key() -> None:
    assert match_partial({"a": 1}, {"b": 1}) == "$.a: missing"


def test_explicit_null_must_be_null() -> None:
    assert match_partial({"a": None}, {"a": None}) is None
    assert match_partial({"a": None}, {"a": 0}) is not None


def test_arrays_same_length_in_order() -> None:
    assert match_partial([1, 2], [1, 2]) is None
    assert match_partial([1, 2], [2, 1]) is not None
    assert match_partial([1], [1, 2]) is not None
    assert match_partial([1], "x") is not None


def test_scalars_are_strict() -> None:
    assert match_partial("a", "A") is not None
    assert match_partial(True, 1) is not None
    assert match_partial(1, True) is not None
    assert match_partial(False, 0) is not None
    assert match_partial(1, 1) is None
    assert match_partial(1, "1") is not None
    assert match_exact(True, 1) is not None
    assert match_exact(0, False) is not None


def test_artist_shorthand() -> None:
    actual = [{"name": "Noisia", "role": "primary"}, {"name": "X", "role": "featured"}]
    assert match_partial(["Noisia", {"name": "X", "role": "featured"}], actual) is None
    assert match_partial(["Noisia", "Y"], actual) == '$[1].name: expected "Y", got "X"'
    # Shorthand only applies to objects with a name.
    assert match_partial(["a"], [{"title": "a"}]) is not None


def test_exact_requires_same_keys_and_no_shorthand() -> None:
    assert match_exact({"a": 1}, {"a": 1}) is None
    assert match_exact({"a": 1}, {"a": 1, "b": 2}) is not None
    assert match_exact(["Noisia"], [{"name": "Noisia"}]) is not None


def test_match_dispatch() -> None:
    assert match({"a": 1}, {"a": 1, "b": 2}) is None
    assert match({"a": 1}, {"a": 1, "b": 2}, "exact") is not None
