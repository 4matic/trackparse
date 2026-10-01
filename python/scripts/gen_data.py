"""Compile spec/data/*.json (and spec/VERSION) into src/trackparse/_data.py.

    python scripts/gen_data.py          write the file
    python scripts/gen_data.py --check  exit 1 if the committed file is stale

Mirrors js/scripts/gen-data.ts. Output is deterministic: files in sorted order, keys in file
order, `$comment` keys dropped.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
SPEC_DIR = HERE.parent.parent / "spec"
DATA_DIR = SPEC_DIR / "data"
OUT_FILE = HERE.parent / "src" / "trackparse" / "_data.py"


def const_name(stem: str) -> str:
    return stem.replace("-", "_").upper() + "_DATA"


def strip_comments(value: Any) -> Any:
    if isinstance(value, list):
        return [strip_comments(v) for v in value]
    if isinstance(value, dict):
        return {k: strip_comments(v) for k, v in value.items() if k != "$comment"}
    return value


def literal(value: Any, indent: int = 0) -> str:
    """A Python literal for a JSON value, one item per line (stable diffs)."""
    pad = "    " * (indent + 1)
    end = "    " * indent
    if isinstance(value, bool):
        return "True" if value else "False"
    if value is None:
        return "None"
    if isinstance(value, (int, float, str)):
        return json.dumps(value, ensure_ascii=False)
    if isinstance(value, list):
        if not value:
            return "[]"
        items = "".join(f"{pad}{literal(v, indent + 1)},\n" for v in value)
        return f"[\n{items}{end}]"
    if isinstance(value, dict):
        if not value:
            return "{}"
        items = "".join(
            f"{pad}{json.dumps(k, ensure_ascii=False)}: {literal(v, indent + 1)},\n"
            for k, v in value.items()
        )
        return f"{{\n{items}{end}}}"
    raise TypeError(f"unsupported JSON value: {value!r}")


def render() -> str:
    version = (SPEC_DIR / "VERSION").read_text(encoding="utf-8").strip()
    lines = [
        "# GENERATED from spec/data — do not edit. Run `python scripts/gen_data.py` to regenerate.",
        '"""Word lists compiled from spec/data (see SPEC.md). Generated; never edit by hand."""',
        "",
        "from __future__ import annotations",
        "",
        "from typing import Any",
        "",
        f"SPEC_VERSION = {json.dumps(version)}  # x-release-please-version",
        "",
    ]
    for path in sorted(DATA_DIR.glob("*.json")):
        data = strip_comments(json.loads(path.read_text(encoding="utf-8")))
        lines.append(f"# spec/data/{path.name}")
        lines.append(f"{const_name(path.stem)}: dict[str, Any] = {literal(data)}")
        lines.append("")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    content = render()
    if "--check" in argv:
        try:
            current = OUT_FILE.read_text(encoding="utf-8")
        except OSError:
            current = ""
        if current != content:
            print(
                "src/trackparse/_data.py is out of date. Run `python scripts/gen_data.py`.",
                file=sys.stderr,
            )
            return 1
        print("src/trackparse/_data.py is up to date.")
        return 0
    OUT_FILE.write_text(content, encoding="utf-8", newline="\n")
    print(f"wrote {OUT_FILE}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
