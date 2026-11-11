#!/usr/bin/env node
// Compiles spec/data/*.json and spec/VERSION into src/data.rs, and the Unicode tables into
// src/unicode_tables.rs: the categories L and Lu (SPEC R0.6) and the lowercase mapping of
// `String.prototype.toLowerCase()` with the two properties its final-sigma rule reads (R0.4).
//
//   node scripts/gen_data.mjs          write the files
//   node scripts/gen_data.mjs --check  exit 1 if a committed file is stale
import { readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const specDir = resolve(here, "../../spec");
const dataDir = join(specDir, "data");

/** A Rust string literal. */
function str(s) {
  let out = '"';
  for (const ch of s) {
    const cp = ch.codePointAt(0);
    if (ch === '"' || ch === "\\") out += `\\${ch}`;
    else if (cp >= 0x20 && cp < 0x7f) out += ch;
    else out += `\\u{${cp.toString(16)}}`;
  }
  return `${out}"`;
}

function value(v, indent) {
  const pad = "    ".repeat(indent);
  if (v === null) return "V::Null";
  if (typeof v === "boolean") return `V::Bool(${v})`;
  if (typeof v === "number") return `V::Num(${Number.isInteger(v) ? `${v}.0` : v})`;
  if (typeof v === "string") return `V::Str(${str(v)})`;
  if (Array.isArray(v)) {
    if (v.length === 0) return "V::Arr(&[])";
    return `V::Arr(&[\n${v.map((x) => `${pad}    ${value(x, indent + 1)},\n`).join("")}${pad}])`;
  }
  const entries = Object.entries(v).filter(([k]) => k !== "$comment");
  if (entries.length === 0) return "V::Obj(&[])";
  return `V::Obj(&[\n${entries.map(([k, x]) => `${pad}    (${str(k)}, ${value(x, indent + 1)}),\n`).join("")}${pad}])`;
}

function renderData() {
  const version = readFileSync(join(specDir, "VERSION"), "utf8").trim();
  const lines = [
    "// GENERATED from spec/data — do not edit. Run `node scripts/gen_data.mjs` to regenerate.",
    "#![allow(clippy::all)]",
    "",
    "use crate::value::V;",
    "",
    "/// The version of the spec this crate implements.",
    `pub const SPEC_VERSION: &str = ${str(version)}; // x-release-please-version`,
    "",
  ];
  for (const file of readdirSync(dataDir).filter((f) => f.endsWith(".json")).sort()) {
    const name = `${file.slice(0, -5).replace(/-/g, "_").toUpperCase()}_DATA`;
    lines.push(`/// spec/data/${file}`);
    lines.push(`pub static ${name}: V = ${value(JSON.parse(readFileSync(join(dataDir, file), "utf8")), 0)};`);
    lines.push("");
  }
  return lines.join("\n");
}

/** Inclusive code point ranges matching `re`. */
function ranges(re) {
  const out = [];
  let start = -1;
  for (let cp = 0; cp <= 0x110000; cp++) {
    const hit = cp < 0x110000 && !(cp >= 0xd800 && cp <= 0xdfff) && re.test(String.fromCodePoint(cp));
    if (hit && start < 0) start = cp;
    if (!hit && start >= 0) {
      out.push([start, cp - 1]);
      start = -1;
    }
  }
  return out;
}

/**
 * `toLowerCase()` of every scalar value that does not map to itself, as two tables: runs of code
 * points `first, first + step, …, last` that all map to themselves plus `delta`, and the few that
 * map to several code points. Runs never overlap.
 */
function lowercase() {
  const runs = [];
  const multi = [];
  let run = null;
  for (let cp = 0; cp < 0x110000; cp++) {
    if (cp >= 0xd800 && cp <= 0xdfff) continue;
    const lower = [...String.fromCodePoint(cp).toLowerCase()].map((c) => c.codePointAt(0));
    if (lower.length === 1 && lower[0] === cp) continue;
    if (lower.length !== 1) {
      multi.push([cp, lower]);
      run = null; // a run must not step over a code point of the other table
      continue;
    }
    const delta = lower[0] - cp;
    if (run && run.delta === delta && (run.step === 0 ? cp - run.last <= 2 : cp - run.last === run.step)) {
      run.step = cp - run.last;
      run.last = cp;
    } else {
      run = { first: cp, last: cp, step: 0, delta };
      runs.push(run);
    }
  }
  for (const r of runs) if (r.step === 0) r.step = 1;
  return { runs, multi };
}

const UNICODE_HEADER = `// Unicode ${process.versions.unicode}`;
function renderUnicode() {
  const table = (name, doc, rs) =>
    `/// ${doc}\npub static ${name}: &[(u32, u32)] = &[\n${rs.map(([a, b]) => `    (0x${a.toString(16)}, 0x${b.toString(16)}),\n`).join("")}];\n`;
  const hex = (n) => `0x${n.toString(16)}`;
  const { runs, multi } = lowercase();
  return [
    "// GENERATED — do not edit. Run `node scripts/gen_data.mjs` to regenerate.",
    "// General category tables for SPEC R0.6, taken from the JS reference engine's \\p{L} and \\p{Lu},",
    "// and its `toLowerCase()` for the R0.4 dedup key (`str::to_lowercase` follows the compiler's",
    "// Unicode version, so it cannot be used).",
    UNICODE_HEADER,
    "",
    table("LETTER", "General category L (Lu, Ll, Lt, Lm, Lo), inclusive ranges.", ranges(/^\p{L}$/u)),
    table("UPPERCASE_LETTER", "General category Lu, inclusive ranges.", ranges(/^\p{Lu}$/u)),
    table("CASED", "Property Cased (the final-sigma rule), inclusive ranges.", ranges(/^\p{Cased}$/u)),
    table("CASE_IGNORABLE", "Property Case_Ignorable (the final-sigma rule), inclusive ranges.", ranges(/^\p{Case_Ignorable}$/u)),
    "/// JS `toLowerCase()` of one code point: `(first, last, step, delta)`. Every code point",
    "/// `first + k * step <= last` maps to itself plus `delta`. Sorted by `first`, no overlaps.",
    `pub static LOWERCASE: &[(u32, u32, u8, i32)] = &[\n${runs.map((r) => `    (${hex(r.first)}, ${hex(r.last)}, ${r.step}, ${r.delta}),\n`).join("")}];`,
    "",
    "/// Code points whose lowercase is several code points. Sorted.",
    `pub static LOWERCASE_MULTI: &[(u32, &[u32])] = &[\n${multi.map(([cp, to]) => `    (${hex(cp)}, &[${to.map(hex).join(", ")}]),\n`).join("")}];`,
    "",
  ].join("\n");
}

const check = process.argv.includes("--check");
let stale = false;
function emit(file, content, sameHeader) {
  const path = resolve(here, "../src", file);
  let current = "";
  try {
    current = readFileSync(path, "utf8");
  } catch {}
  if (check) {
    // The Unicode tables depend on the Node version: only compare like with like.
    if (sameHeader && !current.includes(sameHeader)) return console.log(`${file}: skipped (${sameHeader.slice(3)} here)`);
    if (current !== content) {
      console.error(`${file} is stale: run node scripts/gen_data.mjs`);
      stale = true;
    }
  } else if (current !== content) {
    writeFileSync(path, content);
    console.log(`wrote ${path}`);
  }
}
emit("data.rs", renderData());
emit("unicode_tables.rs", renderUnicode(), UNICODE_HEADER);
if (stale) process.exit(1);
