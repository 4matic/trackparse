"""Loads and runs the shared fixtures (spec/fixtures). Used by the pytest suite and by
scripts/conformance.py. Imports only the standard library and ``trackparse`` (installed or
from src), so it also runs against an installed wheel with no dev dependencies."""

from __future__ import annotations

import json
import traceback
from pathlib import Path
from typing import Any, Callable, NamedTuple, Optional

import trackparse
from match import match

HERE = Path(__file__).resolve().parent
SPEC_DIR = HERE.parent.parent / "spec"
FIXTURES_DIR = SPEC_DIR / "fixtures"

_PARSE_KEYS = {
    "mode": "mode",
    "uploader": "uploader",
    "knownArtists": "known_artists",
    "splitAnd": "split_and",
    "keywords": "keywords",
}
_KEYWORD_KEYS = {
    "versionHeads": "version_heads",
    "descriptors": "descriptors",
    "genres": "genres",
    "junk": "junk",
    "featMarkers": "feat_markers",
}
_FORMAT_KEYS = {
    "feat": "feat",
    "featMarker": "feat_marker",
    "joiners": "joiners",
    "versions": "versions",
    "producers": "producers",
    "position": "position",
}


class LoadedFile(NamedTuple):
    #: Path relative to spec/fixtures, e.g. ``handwritten/brackets-version.json``.
    name: str
    generated: bool
    data: dict[str, Any]


def _walk(d: Path) -> list[Path]:
    if not d.is_dir():
        return []
    out: list[Path] = []
    for entry in sorted(d.iterdir(), key=lambda p: p.name):
        if entry.is_dir():
            out.extend(_walk(entry))
        elif entry.name.endswith(".json"):
            out.append(entry)
    return out


def load_fixture_files(fixtures_dir: Path = FIXTURES_DIR) -> list[LoadedFile]:
    """Fixture files (JSON with a ``cases`` array; metadata such as generated/summary.json is
    skipped), sorted by path."""
    files: list[LoadedFile] = []
    for full in _walk(fixtures_dir):
        name = full.relative_to(fixtures_dir).as_posix()
        data = json.loads(full.read_text(encoding="utf-8"))
        if isinstance(data, dict) and isinstance(data.get("cases"), list):
            files.append(LoadedFile(name, name.startswith("generated/"), data))
    return files


def load_skip_list(path: Path = HERE / "skip.json") -> dict[str, str]:
    data: dict[str, str] = json.loads(path.read_text(encoding="utf-8"))
    return data


def parse_kwargs(options: dict[str, Any]) -> dict[str, Any]:
    """camelCase fixture options → snake_case keyword arguments (incl. nested keywords)."""
    out: dict[str, Any] = {}
    for key, value in options.items():
        name = _PARSE_KEYS.get(key, key)
        if name == "keywords" and isinstance(value, dict):
            value = {_KEYWORD_KEYS.get(k, k): v for k, v in value.items()}
        out[name] = value
    return out


def format_kwargs(options: dict[str, Any]) -> dict[str, Any]:
    return {_FORMAT_KEYS.get(k, k): v for k, v in options.items()}


class CaseRun(NamedTuple):
    fn: str
    #: Every ParsedTrack produced while running the case (for schema validation).
    parsed: list[Any]
    actual: Any
    #: None when the case behaves as its status demands, else a failure description.
    failure: Optional[str]
    #: Raw mismatch (None if the output matches the expectation).
    mismatch: Optional[str]


def run_case(file: dict[str, Any], c: dict[str, Any]) -> CaseRun:
    defaults = file.get("defaults") or {}
    fn: str = c.get("fn") or defaults.get("fn") or "parse"
    options = parse_kwargs({**(defaults.get("options") or {}), **(c.get("options") or {})})
    fmt = format_kwargs({**(defaults.get("formatOptions") or {}), **(c.get("formatOptions") or {})})
    parsed: list[Any] = []
    actual: Any = None
    try:
        if fn == "parse":
            r = trackparse.parse(c["input"], **options)
            parsed.append(r)
            actual = r.to_dict()
        elif fn == "parseArtists":
            actual = [a.to_dict() for a in trackparse.parse_artists(c["input"], **options)]
        elif fn == "format":
            r = trackparse.parse(c["input"], **options)
            parsed.append(r)
            actual = trackparse.format_track(r, **fmt)
        elif fn == "normalize":
            actual = trackparse.normalize(c["input"])
        else:
            msg = f"unknown fn {fn!r}"
            return CaseRun(fn, parsed, None, msg, msg)
    except Exception:
        msg = "threw: " + traceback.format_exc()
        return CaseRun(fn, parsed, None, msg, msg)

    status = c.get("status", "pass")
    mode = c.get("match", "partial")
    mismatch: Optional[str]
    if status == "ambiguous":
        diffs = [match(e, actual, mode) for e in c.get("expectedAny", [])]
        mismatch = (
            None
            if any(d is None for d in diffs)
            else "no expectedAny entry matched: " + " | ".join(str(d) for d in diffs)
        )
    else:
        mismatch = match(c.get("expected"), actual, mode)
    failure = mismatch
    if status == "xfail":
        failure = "xfail case unexpectedly passes" if mismatch is None else None
    return CaseRun(fn, parsed, actual, failure, mismatch)


def describe_failure(c: dict[str, Any], problem: str) -> str:
    return f"{c['id']}\n  input: {json.dumps(c['input'], ensure_ascii=False)}\n  {problem}"


Validator = Callable[[Any], list[str]]
