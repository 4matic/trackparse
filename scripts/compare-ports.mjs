#!/usr/bin/env node
// Cross-port byte check: runs every fixture case (spec/fixtures/**) through the built JS package
// and through the Python port, and diffs the FULL outputs (not the partial fixture expectations).
//
// Usage (from the repo root):
//   pnpm --filter trackparse build        # builds js/dist
//   node scripts/compare-ports.mjs        # needs `uv`; runs scripts/dump_py.py in the python/ project
//
// Options are merged like js/test/runner.ts ({...defaults.options, ...case.options}, same for
// formatOptions). Python gets one process for all cases (JSONL over stdin/stdout); dump_py.py maps
// the camelCase options to snake_case kwargs and serialises with ParsedTrack.to_dict().
// Prints counts and the first 30 differences; exits 1 if any case differs.

import { spawn } from "node:child_process";
import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const fixturesDir = join(root, "spec", "fixtures");
const distEntry = join(root, "js", "dist", "index.js");
const MAX_SHOWN = 30;

if (!existsSync(distEntry)) {
  console.error(`missing ${relative(root, distEntry)}: run \`pnpm --filter trackparse build\` first`);
  process.exit(2);
}
const js = await import(pathToFileURL(distEntry).href);

function walk(dir) {
  const out = [];
  for (const entry of readdirSync(dir).sort()) {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) out.push(...walk(full));
    else if (entry.endsWith(".json")) out.push(full);
  }
  return out;
}

const FNS = new Set(["parse", "parseArtists", "format", "normalize"]);
const cases = [];
for (const full of walk(fixturesDir)) {
  const data = JSON.parse(readFileSync(full, "utf8"));
  if (!Array.isArray(data.cases)) continue; // metadata such as generated/summary.json
  const file = relative(fixturesDir, full).split("\\").join("/");
  const defaults = data.defaults ?? {};
  for (const c of data.cases) {
    const fn = c.fn ?? defaults.fn ?? "parse";
    if (!FNS.has(fn)) continue;
    cases.push({
      i: cases.length,
      id: c.id,
      file,
      fn,
      input: c.input,
      options: { ...defaults.options, ...c.options },
      formatOptions: { ...defaults.formatOptions, ...c.formatOptions },
    });
  }
}

function runJs(c) {
  try {
    switch (c.fn) {
      case "parse":
        return { ok: js.parse(c.input, c.options) };
      case "parseArtists":
        return { ok: js.parseArtists(c.input, c.options) };
      case "format":
        return { ok: js.format(js.parse(c.input, c.options), c.formatOptions) };
      case "normalize":
        return { ok: js.normalize(c.input) };
    }
  } catch (err) {
    return { error: `${err?.name ?? "Error"}: ${err?.message ?? String(err)}` };
  }
  throw new Error(`unknown fn ${c.fn}`);
}

function runPython(all) {
  return new Promise((resolvePy, reject) => {
    const child = spawn("uv", ["run", "--project", "python", "python", "scripts/dump_py.py"], {
      cwd: root,
      stdio: ["pipe", "pipe", "inherit"],
    });
    const chunks = [];
    child.stdout.on("data", (chunk) => chunks.push(chunk));
    child.on("error", reject);
    child.on("close", (code) => {
      if (code !== 0) return reject(new Error(`dump_py.py exited with code ${code}`));
      const byIndex = new Map();
      for (const line of Buffer.concat(chunks).toString("utf8").split("\n")) {
        if (!line.trim()) continue;
        const rec = JSON.parse(line);
        byIndex.set(rec.i, rec);
      }
      resolvePy(byIndex);
    });
    child.stdin.on("error", reject);
    for (const c of all) {
      const { i, fn, input, options, formatOptions } = c;
      child.stdin.write(`${JSON.stringify({ i, fn, input, options, formatOptions })}\n`);
    }
    child.stdin.end();
  });
}

/** JSON with object keys sorted recursively, so key order never counts as a difference. */
function canonical(value) {
  return JSON.stringify(value, (_key, v) =>
    v && typeof v === "object" && !Array.isArray(v)
      ? Object.fromEntries(Object.keys(v).sort().map((k) => [k, v[k]]))
      : v,
  );
}

/** Canonical form of a result: outputs compare by value, failures only by the fact they threw. */
function comparable(rec) {
  if (!rec) return "<missing>";
  if ("error" in rec) return "<threw>";
  return canonical(rec.ok === undefined ? null : rec.ok);
}

/** First differing path between two JSON values, for a compact report. */
function firstDiff(a, b, path = "$") {
  if (canonical(a) === canonical(b)) return null;
  if (a && b && typeof a === "object" && typeof b === "object" && Array.isArray(a) === Array.isArray(b)) {
    const keys = Array.isArray(a)
      ? [...Array(Math.max(a.length, b.length)).keys()]
      : [...new Set([...Object.keys(a), ...Object.keys(b)])].sort();
    for (const k of keys) {
      const d = firstDiff(a[k], b[k], Array.isArray(a) ? `${path}[${k}]` : `${path}.${k}`);
      if (d) return d;
    }
  }
  const show = (v) => (v === undefined ? "<absent>" : canonical(v));
  return `${path}: js=${show(a)} py=${show(b)}`;
}

const py = await runPython(cases);

const diffs = [];
const byFn = {};
let bothThrew = 0;
for (const c of cases) {
  const j = runJs(c);
  const p = py.get(c.i);
  byFn[c.fn] ??= { total: 0, differ: 0 };
  byFn[c.fn].total++;
  if (j.error && p?.error) bothThrew++;
  if (comparable(j) === comparable(p)) continue;
  byFn[c.fn].differ++;
  let detail;
  if (!p) detail = "python produced no output";
  else if (j.error || p.error) detail = `js ${j.error ?? "ok"} | py ${p.error ?? "ok"}`;
  else detail = firstDiff(j.ok, p.ok);
  diffs.push({ c, detail });
}

console.log(`compared ${cases.length} cases (JS dist vs Python)`);
for (const [fn, s] of Object.entries(byFn)) {
  console.log(`  ${fn.padEnd(13)} ${s.total - s.differ}/${s.total} identical`);
}
if (bothThrew) console.log(`  (${bothThrew} cases threw in both ports)`);
console.log(`differences: ${diffs.length}`);
for (const { c, detail } of diffs.slice(0, MAX_SHOWN)) {
  console.log(`\n- ${c.file} ${c.id} [${c.fn}] ${JSON.stringify(c.input)}`);
  console.log(`  ${detail}`);
}
if (diffs.length > MAX_SHOWN) console.log(`\n... and ${diffs.length - MAX_SHOWN} more`);
process.exit(diffs.length ? 1 : 0);
