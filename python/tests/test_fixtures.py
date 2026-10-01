"""Runs every shared fixture in spec/fixtures (handwritten + generated).

- Each file is validated against spec/schema/fixture-file.schema.json.
- Every ParsedTrack produced is validated against parsed-track.schema.json.
- status pass / xfail / ambiguous per spec/fixtures/README.md.
- tests/skip.json lists port-specific known failures; a skipped case that starts passing
  fails the run, so the list can only shrink.
"""

from __future__ import annotations

import json
from typing import Any, Optional

import pytest
from jsonschema import Draft202012Validator

from runner import (
    SPEC_DIR,
    CaseRun,
    LoadedFile,
    describe_failure,
    load_fixture_files,
    load_skip_list,
    run_case,
)


def _schema(name: str) -> Draft202012Validator:
    schema = json.loads((SPEC_DIR / "schema" / name).read_text(encoding="utf-8"))
    return Draft202012Validator(schema)


FILE_VALIDATOR = _schema("fixture-file.schema.json")
TRACK_VALIDATOR = _schema("parsed-track.schema.json")
FILES = load_fixture_files()
SKIP = load_skip_list()


def _errors(validator: Draft202012Validator, value: Any) -> list[str]:
    return [
        f"{'/'.join(map(str, e.path)) or '$'}: {e.message}" for e in validator.iter_errors(value)
    ]


def problem_of(c: dict[str, Any], run: CaseRun) -> Optional[str]:
    """Everything wrong with one case, or None. Skip-listed cases are inverted."""
    schema_errors: list[str] = []
    for track in run.parsed:
        schema_errors.extend(_errors(TRACK_VALIDATOR, track.to_dict()))
    problem = (
        "output violates parsed-track schema: " + "; ".join(schema_errors)
        if schema_errors
        else run.failure
    )
    reason = SKIP.get(c["id"])
    if reason is not None:
        if problem is None:
            return f"skip-listed ({reason}) but now passes: remove it from tests/skip.json"
        return None
    return problem


def test_fixtures_found() -> None:
    assert len(FILES) > 0
    assert sum(len(f.data["cases"]) for f in FILES) > 10_000


@pytest.mark.parametrize("file", FILES, ids=[f.name for f in FILES])
def test_fixture_file_schema(file: LoadedFile) -> None:
    errors = _errors(FILE_VALIDATOR, file.data)
    assert not errors, "\n".join(errors[:20])


GENERATED = [f for f in FILES if f.generated]
HANDWRITTEN = [(f, c) for f in FILES if not f.generated for c in f.data["cases"]]


@pytest.mark.parametrize("file", GENERATED, ids=[f.name for f in GENERATED])
def test_generated_file(file: LoadedFile) -> None:
    failures: list[str] = []
    for c in file.data["cases"]:
        problem = problem_of(c, run_case(file.data, c))
        if problem is not None:
            failures.append(describe_failure(c, problem))
    head = "\n".join(failures[:20])
    assert not failures, f"{len(failures)} failing case(s); first ones:\n{head}"


@pytest.mark.parametrize(("file", "case"), HANDWRITTEN, ids=[c["id"] for _, c in HANDWRITTEN])
def test_handwritten_case(file: LoadedFile, case: dict[str, Any]) -> None:
    problem = problem_of(case, run_case(file.data, case))
    if problem is not None:
        pytest.fail(describe_failure(case, problem), pytrace=False)


def test_skip_list_ids_exist() -> None:
    ids = {c["id"] for f in FILES for c in f.data["cases"]}
    stale = [i for i in SKIP if i not in ids]
    assert stale == [], f"stale skip.json entries: {stale}"


def test_case_ids_unique() -> None:
    seen: set = set()
    dupes: list[str] = []
    for f in FILES:
        for c in f.data["cases"]:
            if c["id"] in seen:
                dupes.append(c["id"])
            seen.add(c["id"])
    assert dupes == []


def test_skip_list_has_no_duplicate_ids() -> None:
    dupes: list[str] = []

    def hook(pairs: list[Any]) -> dict[str, Any]:
        seen: dict[str, Any] = {}
        for k, v in pairs:
            if k in seen:
                dupes.append(k)
            seen[k] = v
        return seen

    from runner import HERE

    json.loads((HERE / "skip.json").read_text(encoding="utf-8"), object_pairs_hook=hook)
    assert dupes == []
