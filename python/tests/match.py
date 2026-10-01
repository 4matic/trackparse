"""The fixture matcher from spec/fixtures/README.md (stdlib only).

Returns ``None`` on a match, otherwise a short description of the first mismatch. Scalars use
strict equality that also distinguishes types (``True`` is not ``1``, ``1`` is not ``1.0``
only when the types differ in JSON terms: bool vs number), like JS ``===``.
"""

from __future__ import annotations

import json
from typing import Any, Optional


def _show(v: Any) -> str:
    try:
        return json.dumps(v, ensure_ascii=False)
    except (TypeError, ValueError):
        return repr(v)


def _scalar_equal(expected: Any, actual: Any) -> bool:
    """JS ``===`` on JSON scalars: bools never equal numbers; ints and floats compare by value."""
    if isinstance(expected, bool) or isinstance(actual, bool):
        return isinstance(expected, bool) and isinstance(actual, bool) and expected == actual
    if isinstance(expected, (int, float)) and isinstance(actual, (int, float)):
        return expected == actual
    if type(expected) is not type(actual):
        return False
    return bool(expected == actual)


def match_partial(expected: Any, actual: Any, path: str = "$") -> Optional[str]:
    """``match: "partial"`` (README rules 1–4)."""
    if isinstance(expected, list):
        if not isinstance(actual, list):
            return f"{path}: expected an array, got {_show(actual)}"
        if len(actual) != len(expected):
            return f"{path}: expected {len(expected)} items, got {len(actual)}: {_show(actual)}"
        for i, (e, a) in enumerate(zip(expected, actual)):
            # Rule 4: artist shorthand.
            if isinstance(e, str) and isinstance(a, dict) and "name" in a:
                if not _scalar_equal(e, a["name"]):
                    return f"{path}[{i}].name: expected {_show(e)}, got {_show(a['name'])}"
                continue
            diff = match_partial(e, a, f"{path}[{i}]")
            if diff:
                return diff
        return None
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            return f"{path}: expected an object, got {_show(actual)}"
        for key in expected:
            if key not in actual:
                return f"{path}.{key}: missing"
            diff = match_partial(expected[key], actual[key], f"{path}.{key}")
            if diff:
                return diff
        return None
    if expected is None:
        return None if actual is None else f"{path}: expected null, got {_show(actual)}"
    if _scalar_equal(expected, actual):
        return None
    return f"{path}: expected {_show(expected)}, got {_show(actual)}"


def match_exact(expected: Any, actual: Any, path: str = "$") -> Optional[str]:
    """Deep equality of complete values (README rule 5)."""
    if isinstance(expected, list):
        if not isinstance(actual, list) or len(actual) != len(expected):
            return f"{path}: expected {_show(expected)}, got {_show(actual)}"
        for i, (e, a) in enumerate(zip(expected, actual)):
            diff = match_exact(e, a, f"{path}[{i}]")
            if diff:
                return diff
        return None
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            return f"{path}: expected an object, got {_show(actual)}"
        ek = sorted(expected)
        ak = sorted(actual)
        if ek != ak:
            return f"{path}: expected keys {_show(ek)}, got {_show(ak)}"
        for key in ek:
            diff = match_exact(expected[key], actual[key], f"{path}.{key}")
            if diff:
                return diff
        return None
    if expected is None:
        return None if actual is None else f"{path}: expected null, got {_show(actual)}"
    if _scalar_equal(expected, actual):
        return None
    return f"{path}: expected {_show(expected)}, got {_show(actual)}"


def match(expected: Any, actual: Any, mode: str = "partial") -> Optional[str]:
    return match_exact(expected, actual) if mode == "exact" else match_partial(expected, actual)
