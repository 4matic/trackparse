// Shared by scripts/compare-ports.mjs and scripts/fuzz-ports.mjs: how each port runs a list of
// cases and how full outputs are compared.
//
// A case is { i, fn, input, options, formatOptions, edits } with the spec's camelCase option
// names (fn is parse, parseArtists, format, normalize, parseTracklist or applyEdits). Each
// runner resolves to a Map from i to { ok: <output> } or { error: <message> }.
//
// JS is the reference and runs in this process from js/dist. Python gets one process per call
// (scripts/dump_py.py, JSONL over stdin/stdout). Rust and Go read one case per line and write
// one JSON result per line, in order (rust/examples/dump.rs, go/internal/cmd/dump).

import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

export const root = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");

const distEntry = join(root, "js", "dist", "index.js");
const distTracklist = join(root, "js", "dist", "tracklist.js");
for (const entry of [distEntry, distTracklist]) {
  if (!existsSync(entry)) {
    console.error(`missing ${relative(root, entry)}: run \`pnpm --filter trackparse build\` first`);
    process.exit(2);
  }
}
const js = await import(pathToFileURL(distEntry).href);
const jsTracklist = await import(pathToFileURL(distTracklist).href);

/** One case through the built JS package. */
export function runJs(c) {
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
      case "parseTracklist":
        return { ok: jsTracklist.parseTracklist(c.input, c.options) };
      case "applyEdits":
        return { ok: jsTracklist.applyEdits(c.input, c.edits, c.options) };
    }
  } catch (err) {
    return { error: `${err?.name ?? "Error"}: ${err?.message ?? String(err)}` };
  }
  throw new Error(`unknown fn ${c.fn}`);
}

/** Every case through the Python port, in one process. */
export function runPython(all) {
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
      const { i, fn, input, options, formatOptions, edits } = c;
      // Edits keep non-JSON numbers (fixtures write infinity as 1e999) via a tagged string.
      const line = JSON.stringify({ i, fn, input, options, formatOptions, edits }, (_k, v) =>
        typeof v === "number" && !Number.isFinite(v) ? { $number: String(v) } : v,
      );
      child.stdin.write(`${line}\n`);
    }
    child.stdin.end();
  });
}

/**
 * A port that reads one case per line and writes one JSON result per line, in order (Rust and
 * Go). Such ports never throw: every line is a result.
 */
export function runLines(name, command, args, cwd) {
  return (all) =>
    new Promise((resolveRun, reject) => {
      const child = spawn(command, args, { cwd, stdio: ["pipe", "pipe", "inherit"] });
      const chunks = [];
      child.stdout.on("data", (chunk) => chunks.push(chunk));
      child.on("error", reject);
      child.on("close", (code) => {
        if (code !== 0) return reject(new Error(`the ${name} dump exited with code ${code}`));
        const lines = Buffer.concat(chunks).toString("utf8").split("\n");
        const byIndex = new Map();
        all.forEach((c, k) => byIndex.set(c.i, lines[k] ? { ok: JSON.parse(lines[k]) } : undefined));
        resolveRun(byIndex);
      });
      child.stdin.on("error", reject);
      for (const c of all) {
        const { fn, input, options, formatOptions, edits } = c;
        // Non-finite numbers travel as 1e999 / -1e999, which both JSON readers take as infinity.
        const line = JSON.stringify({ fn, input, options, formatOptions, edits }, (_k, v) =>
          typeof v === "number" && !Number.isFinite(v) ? `\u0000num:${v}` : v,
        )
          .replaceAll('"\\u0000num:Infinity"', "1e999")
          .replaceAll('"\\u0000num:-Infinity"', "-1e999");
        child.stdin.write(`${line}\n`);
      }
      child.stdin.end();
    });
}

/** JSON with object keys sorted recursively, so key order never counts as a difference. */
export function canonical(value) {
  return JSON.stringify(value, (_key, v) =>
    v && typeof v === "object" && !Array.isArray(v)
      ? Object.fromEntries(Object.keys(v).sort().map((k) => [k, v[k]]))
      : v,
  );
}

/** Canonical form of a result: outputs compare by value, failures only by the fact they threw. */
export function comparable(rec) {
  if (!rec) return "<missing>";
  if ("error" in rec) return "<threw>";
  return canonical(rec.ok === undefined ? null : rec.ok);
}

/** First differing path between two JSON values, for a compact report. */
export function firstDiff(a, b, path = "$") {
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

/** A one-line description of how a port's result differs from the JS one. */
export function describeDiff(port, j, p) {
  if (!p) return `${port.name} produced no output`;
  if (j.error || p.error) return `js ${j.error ?? "ok"} | ${port.key} ${p.error ?? "ok"}`;
  return firstDiff(j.ok, p.ok).replace(" py=", ` ${port.key}=`);
}
