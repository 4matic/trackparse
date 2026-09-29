#!/usr/bin/env node
/**
 * Runtime compatibility check: runs every shared fixture against the BUILT package (dist/, both
 * the ESM and the CJS entry) with plain Node and no dev dependencies, so it works on Node
 * versions older than the tooling supports (the package targets Node >= 18).
 *
 * Usage: node scripts/compat.mjs   (after `pnpm build`)
 * Mirrors test/match.ts and test/runner.ts; keep them in sync.
 */
import { readdirSync, readFileSync, statSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const fixturesDir = resolve(here, "../../spec/fixtures");
const skip = JSON.parse(readFileSync(join(here, "../test/skip.json"), "utf8"));

const isObject = (v) => typeof v === "object" && v !== null && !Array.isArray(v);
const show = (v) => JSON.stringify(v) ?? String(v);

function matchPartial(expected, actual, path = "$") {
  if (Array.isArray(expected)) {
    if (!Array.isArray(actual) || actual.length !== expected.length) {
      return `${path}: expected ${show(expected)}, got ${show(actual)}`;
    }
    for (let i = 0; i < expected.length; i++) {
      const e = expected[i];
      const a = actual[i];
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
  return expected === actual ? null : `${path}: expected ${show(expected)}, got ${show(actual)}`;
}

function matchExact(expected, actual, path = "$") {
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
    if (ek.join("\0") !== ak.join("\0"))
      return `${path}: expected keys ${show(ek)}, got ${show(ak)}`;
    for (const key of ek) {
      const diff = matchExact(expected[key], actual[key], `${path}.${key}`);
      if (diff) return diff;
    }
    return null;
  }
  return expected === actual ? null : `${path}: expected ${show(expected)}, got ${show(actual)}`;
}

const match = (e, a, mode) => (mode === "exact" ? matchExact(e, a) : matchPartial(e, a));

function walk(dir) {
  return readdirSync(dir)
    .sort()
    .flatMap((entry) => {
      const full = join(dir, entry);
      if (statSync(full).isDirectory()) return walk(full);
      return entry.endsWith(".json") ? [full] : [];
    });
}

function run(api, file, c) {
  const fn = c.fn ?? file.defaults?.fn ?? "parse";
  const options = { ...file.defaults?.options, ...c.options };
  const formatOptions = { ...file.defaults?.formatOptions, ...c.formatOptions };
  let actual;
  try {
    if (fn === "parse") actual = api.parse(c.input, options);
    else if (fn === "parseArtists") actual = api.parseArtists(c.input, options);
    else if (fn === "format") actual = api.format(api.parse(c.input, options), formatOptions);
    else actual = api.normalize(c.input);
  } catch (err) {
    return `threw: ${err instanceof Error ? err.message : String(err)}`;
  }
  const status = c.status ?? "pass";
  const mode = c.match ?? "partial";
  const mismatch =
    status === "ambiguous"
      ? (c.expectedAny ?? []).some((e) => match(e, actual, mode) === null)
        ? null
        : "no expectedAny entry matched"
      : match(c.expected, actual, mode);
  if (status === "xfail") return mismatch === null ? "xfail case unexpectedly passes" : null;
  return mismatch;
}

const files = walk(fixturesDir)
  .map((full) => ({
    name: relative(fixturesDir, full),
    data: JSON.parse(readFileSync(full, "utf8")),
  }))
  .filter((f) => Array.isArray(f.data.cases));

const builds = {
  esm: await import(new URL("../dist/index.js", import.meta.url).href),
  cjs: createRequire(import.meta.url)("../dist/index.cjs"),
};

let failed = 0;
for (const [kind, api] of Object.entries(builds)) {
  let total = 0;
  const failures = [];
  for (const file of files) {
    for (const c of file.data.cases) {
      if (c.id in skip) continue;
      total++;
      const failure = run(api, file.data, c);
      if (failure) failures.push(`${file.name} ${c.id}: ${failure}`);
    }
  }
  console.log(
    `${kind}: ${total - failures.length}/${total} fixtures pass on Node ${process.version}`,
  );
  for (const f of failures.slice(0, 20)) console.log(`  ${f}`);
  failed += failures.length;
}
process.exit(failed === 0 ? 0 : 1);
