"""Run fixture cases through the Python port and print the full outputs as JSONL.

Helper for scripts/compare-ports.mjs; not meant to be run by hand. Run it with the python/ uv
project so `trackparse` is importable:

    uv run --project python python scripts/dump_py.py < cases.jsonl > outputs.jsonl

Each stdin line is {"i": int, "fn": "parse"|"parseArtists"|"format"|"normalize", "input": str,
"options": {...}, "formatOptions": {...}} with the spec's camelCase option names (already merged
with the fixture file defaults). Each stdout line is {"i": int, "ok": <output>} or
{"i": int, "error": str}. Outputs use the spec JSON shape (ParsedTrack.to_dict()).
"""

from __future__ import annotations

import dataclasses
import json
import sys
from typing import Any

import trackparse

PARSE_OPTIONS = {
    "mode": "mode",
    "uploader": "uploader",
    "knownArtists": "known_artists",
    "splitAnd": "split_and",
    "keywords": "keywords",
}
KEYWORD_OPTIONS = {
    "versionHeads": "version_heads",
    "descriptors": "descriptors",
    "genres": "genres",
    "junk": "junk",
    "featMarkers": "feat_markers",
}
FORMAT_OPTIONS = {
    "feat": "feat",
    "featMarker": "feat_marker",
    "joiners": "joiners",
    "versions": "versions",
    "producers": "producers",
    "position": "position",
}


class OptionError(Exception):
    """An option name this harness does not know (a harness bug, not a port difference)."""


def rename(obj: dict[str, Any], mapping: dict[str, str], what: str) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for key, value in obj.items():
        if key not in mapping:
            raise OptionError(
                f"unknown {what} option {key!r}: teach scripts/dump_py.py about it"
            )
        out[mapping[key]] = value
    return out


def parse_kwargs(options: dict[str, Any]) -> dict[str, Any]:
    kwargs = rename(options, PARSE_OPTIONS, "parse")
    if "keywords" in kwargs:
        kwargs["keywords"] = rename(kwargs["keywords"], KEYWORD_OPTIONS, "keywords")
    return kwargs


def to_json(value: Any) -> Any:
    """Spec JSON for a port value: prefer the port's own to_dict(), else plain conversion."""
    if hasattr(value, "to_dict"):
        return value.to_dict()
    if dataclasses.is_dataclass(value) and not isinstance(value, type):
        return {
            f.name: to_json(getattr(value, f.name)) for f in dataclasses.fields(value)
        }
    if isinstance(value, (list, tuple)):
        return [to_json(v) for v in value]
    if isinstance(value, dict):
        return {k: to_json(v) for k, v in value.items()}
    return value


def run(case: dict[str, Any]) -> Any:
    fn = case["fn"]
    text = case["input"]
    kwargs = parse_kwargs(case.get("options") or {})
    if fn == "parse":
        return to_json(trackparse.parse(text, **kwargs))
    if fn == "parseArtists":
        return to_json(trackparse.parse_artists(text, **kwargs))
    if fn == "format":
        track = trackparse.parse(text, **kwargs)
        fmt = rename(case.get("formatOptions") or {}, FORMAT_OPTIONS, "format")
        return trackparse.format_track(track, **fmt)
    if fn == "normalize":
        return trackparse.normalize(text)
    raise ValueError(f"unknown fn {fn!r}")


def main() -> None:
    out = sys.stdout
    for raw in sys.stdin.buffer:
        line = raw.decode("utf-8").strip()
        if not line:
            continue
        case = json.loads(line)
        try:
            record = {"i": case["i"], "ok": run(case)}
        except OptionError as err:
            # A harness problem, not a port difference: stop loudly.
            sys.exit(f"dump_py.py: {err}")
        except Exception as err:  # noqa: BLE001 - report every port failure as a difference
            record = {"i": case["i"], "error": f"{type(err).__name__}: {err}"}
        # ensure_ascii keeps lone surrogates (valid in JS strings) round-trippable.
        out.write(json.dumps(record, ensure_ascii=True) + "\n")
    out.flush()


if __name__ == "__main__":
    main()
