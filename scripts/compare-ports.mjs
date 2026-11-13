#!/usr/bin/env node
// Cross-port byte check: runs every fixture case (spec/fixtures/**) through the built JS package,
// the Python port, the Rust port and the Go port, and diffs the FULL outputs (not the partial
// fixture expectations). JS is the reference; each other port is compared with it.
//
// Usage (from the repo root):
//   pnpm --filter trackparse build        # builds js/dist
//   node scripts/compare-ports.mjs        # needs `uv` (Python), `cargo` (Rust) and `go` (Go)
//   node scripts/compare-ports.mjs --only=python   # or --only=rust, --only=go
//
// Rust runs `cargo run --release --example dump` in rust/ and Go runs `go run ./cmd/dump` in go/
// (both JSONL in, one JSON result per line, the same protocol).
// Options are merged like js/test/runner.ts ({...defaults.options, ...case.options}, same for
// formatOptions). Python gets one process for all cases (JSONL over stdin/stdout); dump_py.py maps
// the camelCase options to snake_case kwargs and serialises with to_dict() (ParsedTrack, and the
// Tracklist / EditResult of trackparse.tracklist for parseTracklist / applyEdits).
// Prints counts and the first 30 differences; exits 1 if any case differs. The port runners and
// the comparison are in scripts/lib/ports.mjs, shared with scripts/fuzz-ports.mjs.

import { readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative } from "node:path";
import { describeDiff, comparable, root, runJs, runLines, runPython } from "./lib/ports.mjs";

const fixturesDir = join(root, "spec", "fixtures");
const MAX_SHOWN = 30;

function walk(dir) {
  const out = [];
  for (const entry of readdirSync(dir).sort()) {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) out.push(...walk(full));
    else if (entry.endsWith(".json")) out.push(full);
  }
  return out;
}

const FNS = new Set(["parse", "parseArtists", "format", "normalize", "parseTracklist", "applyEdits"]);
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
      edits: c.edits ?? [],
    });
  }
}

const runRust = runLines("Rust", "cargo", ["run", "--quiet", "--release", "--example", "dump"], join(root, "rust"));
const runGo = runLines("Go", "go", ["run", "./cmd/dump"], join(root, "go"));

const only = process.argv.find((a) => a.startsWith("--only="))?.slice(7);
const ports = [
  { name: "Python", key: "py", run: runPython },
  { name: "Rust", key: "rs", run: runRust },
  { name: "Go", key: "go", run: runGo },
].filter((p) => !only || p.name.toLowerCase() === only);

const jsResults = cases.map(runJs);
let total = 0;
for (const port of ports) {
  const other = await port.run(cases);
  const diffs = [];
  const byFn = {};
  let bothThrew = 0;
  cases.forEach((c, k) => {
    const j = jsResults[k];
    const p = other.get(c.i);
    byFn[c.fn] ??= { total: 0, differ: 0 };
    byFn[c.fn].total++;
    if (j.error && p?.error) bothThrew++;
    if (comparable(j) === comparable(p)) return;
    byFn[c.fn].differ++;
    diffs.push({ c, detail: describeDiff(port, j, p) });
  });

  console.log(`compared ${cases.length} cases (JS dist vs ${port.name})`);
  for (const [fn, s] of Object.entries(byFn)) {
    console.log(`  ${fn.padEnd(15)} ${s.total - s.differ}/${s.total} identical`);
  }
  if (bothThrew) console.log(`  (${bothThrew} cases threw in both ports)`);
  console.log(`differences: ${diffs.length}`);
  for (const { c, detail } of diffs.slice(0, MAX_SHOWN)) {
    console.log(`\n- ${c.file} ${c.id} [${c.fn}] ${JSON.stringify(c.input)}`);
    console.log(`  ${detail}`);
  }
  if (diffs.length > MAX_SHOWN) console.log(`\n... and ${diffs.length - MAX_SHOWN} more`);
  total += diffs.length;
}
process.exit(total ? 1 : 0);
