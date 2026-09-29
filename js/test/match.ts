/**
 * The fixture matcher from spec/fixtures/README.md.
 * Returns `null` on a match, otherwise a short description of the first mismatch.
 */

type Json = unknown;

function isObject(v: Json): v is Record<string, Json> {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

function show(v: Json): string {
  return JSON.stringify(v) ?? String(v);
}

/** `match: "partial"` (README rules 1–4). */
export function matchPartial(expected: Json, actual: Json, path = "$"): string | null {
  if (Array.isArray(expected)) {
    if (!Array.isArray(actual)) return `${path}: expected an array, got ${show(actual)}`;
    if (actual.length !== expected.length) {
      return `${path}: expected ${expected.length} items, got ${actual.length}: ${show(actual)}`;
    }
    for (let i = 0; i < expected.length; i++) {
      const e = expected[i];
      const a = actual[i];
      // Rule 4: artist shorthand.
      if (typeof e === "string" && isObject(a) && "name" in a) {
        if (a.name !== e) return `${path}[${i}].name: expected ${show(e)}, got ${show(a.name)}`;
        continue;
      }
      const diff = matchPartial(e, a, `${path}[${i}]`);
      if (diff) return diff;
    }
    return null;
  }
  if (isObject(expected)) {
    if (!isObject(actual)) return `${path}: expected an object, got ${show(actual)}`;
    for (const key of Object.keys(expected)) {
      if (!(key in actual)) return `${path}.${key}: missing`;
      const diff = matchPartial(expected[key], actual[key], `${path}.${key}`);
      if (diff) return diff;
    }
    return null;
  }
  if (expected === null)
    return actual === null ? null : `${path}: expected null, got ${show(actual)}`;
  return expected === actual ? null : `${path}: expected ${show(expected)}, got ${show(actual)}`;
}

/** Deep equality of complete values (README rule 5). */
export function matchExact(expected: Json, actual: Json, path = "$"): string | null {
  if (Array.isArray(expected)) {
    if (!Array.isArray(actual) || actual.length !== expected.length) {
      return `${path}: expected ${show(expected)}, got ${show(actual)}`;
    }
    for (let i = 0; i < expected.length; i++) {
      const diff = matchExact(expected[i], actual[i], `${path}[${i}]`);
      if (diff) return diff;
    }
    return null;
  }
  if (isObject(expected)) {
    if (!isObject(actual)) return `${path}: expected an object, got ${show(actual)}`;
    const ek = Object.keys(expected).sort();
    const ak = Object.keys(actual).sort();
    if (ek.join("\0") !== ak.join("\0")) {
      return `${path}: expected keys ${show(ek)}, got ${show(ak)}`;
    }
    for (const key of ek) {
      const diff = matchExact(expected[key], actual[key], `${path}.${key}`);
      if (diff) return diff;
    }
    return null;
  }
  return expected === actual ? null : `${path}: expected ${show(expected)}, got ${show(actual)}`;
}

export function match(expected: Json, actual: Json, mode: "partial" | "exact" = "partial") {
  return mode === "exact" ? matchExact(expected, actual) : matchPartial(expected, actual);
}
