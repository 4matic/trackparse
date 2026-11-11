#!/usr/bin/env node
// Compiles spec/data/*.json and spec/VERSION into src/data.rs, and the Unicode category tables
// (L and Lu, SPEC R0.6) into src/unicode_tables.rs.
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

const UNICODE_HEADER = `// Unicode ${process.versions.unicode}`;
function renderUnicode() {
  const table = (name, doc, rs) =>
    `/// ${doc}\npub static ${name}: &[(u32, u32)] = &[\n${rs.map(([a, b]) => `    (0x${a.toString(16)}, 0x${b.toString(16)}),\n`).join("")}];\n`;
  return [
    "// GENERATED — do not edit. Run `node scripts/gen_data.mjs` to regenerate.",
    "// General category tables for SPEC R0.6, taken from the JS reference engine's \\p{L} and \\p{Lu}.",
    UNICODE_HEADER,
    "",
    table("LETTER", "General category L (Lu, Ll, Lt, Lm, Lo), inclusive ranges.", ranges(/^\p{L}$/u)),
    table("UPPERCASE_LETTER", "General category Lu, inclusive ranges.", ranges(/^\p{Lu}$/u)),
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
