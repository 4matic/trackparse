"""Conformance report: pass/total per fixture file, per rule ID and overall.

A case "passes" when it behaves as its status demands (skip.json is ignored here, failing
skip-listed cases are counted separately). Exits 0, or with ``--strict`` exits 1 when any case
fails that is not covered by tests/skip.json (CI uses this against the installed wheel).

Imports only the standard library and ``trackparse`` (the installed package, or src/ in a dev
checkout via ``uv run``); the fixture runner lives in tests/runner.py and tests/match.py, which
are stdlib-only too. Fixtures are located relative to this file (../../spec/fixtures).
"""

from __future__ import annotations

import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
# The stdlib-only runner/matcher. Appended (not prepended) so `import trackparse` still
# resolves to the installed package.
sys.path.append(str(HERE.parent / "tests"))

import trackparse  # noqa: E402
from runner import load_fixture_files, load_skip_list, run_case  # noqa: E402

Tally = list[int]  # [pass, total]


def _bump(m: dict[str, Tally], key: str, ok: bool) -> None:
    t = m.setdefault(key, [0, 0])
    t[1] += 1
    if ok:
        t[0] += 1


def _pct(t: Tally) -> str:
    return "-" if t[1] == 0 else f"{100 * t[0] / t[1]:.1f}%"


def _row(label: str, t: Tally, width: int) -> str:
    return f"  {label.ljust(width)} {t[0]:>6} / {t[1]:<6} {_pct(t)}"


def _rule_key(rule: str) -> tuple[tuple[int, int, str], ...]:
    parts = []
    for p in rule[1:].split("."):
        digits = ""
        for ch in p:
            if "0" <= ch <= "9":
                digits += ch
            else:
                break
        parts.append((0 if digits else 1, int(digits) if digits else 0, p[len(digits) :]))
    return tuple(parts)


def main() -> int:
    files = load_fixture_files()
    skip = load_skip_list()
    by_file: dict[str, Tally] = {}
    by_rule: dict[str, Tally] = {}
    overall: Tally = [0, 0]
    skipped = 0
    for f in files:
        for c in f.data["cases"]:
            ok = run_case(f.data, c).failure is None
            if not ok and c["id"] in skip:
                skipped += 1
            _bump(by_file, f.name, ok)
            for rule in c.get("rules", []):
                _bump(by_rule, rule, ok)
            overall[1] += 1
            if ok:
                overall[0] += 1

    width = max([10, *(len(k) for k in by_file)])
    print(
        f"trackparse (python {trackparse.__version__}, spec {trackparse.SPEC_VERSION}) conformance"
    )
    print(f"package: {Path(trackparse.__file__).parent}\n")
    print("By file:")
    for name, t in by_file.items():
        print(_row(name, t, width))
    print("\nBy rule:")
    for rule in sorted(by_rule, key=_rule_key):
        print(_row(rule, by_rule[rule], 10))
    print(f"\nOverall: {overall[0]} / {overall[1]} ({_pct(overall)})")
    print(f"Failing cases covered by tests/skip.json: {skipped}")
    uncovered = overall[1] - overall[0] - skipped
    if "--strict" in sys.argv[1:] and uncovered:
        print(f"FAIL: {uncovered} case(s) failing outside tests/skip.json")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
