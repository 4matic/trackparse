#!/usr/bin/env node
// amalgamate.mjs: writes the single-file distribution of the C library (sqlite style).
//
//   node c/scripts/amalgamate.mjs [--out DIR]     (default: c/dist)
//
// DIR/trackparse.c    every c/src/*.c and the internal headers they include, in dependency order,
//                     in one translation unit with TRACKPARSE_AMALGAMATION defined, so every
//                     internal function and table (TP_PRIVATE, TP_PRIVATE_DATA) is static and no
//                     symbol but the public trackparse_* API leaves the object file.
// DIR/trackparse.h    a copy of c/include/trackparse.h
// DIR/trackparse.hpp  a copy of c/include/trackparse.hpp (when it exists)
//
// wire/ (the JSON door) is not part of it. Node is needed to write the amalgamation, never to
// build it: the release tarball ships dist/trackparse.c and CMake uses it when present.
//
// Checks, each a failure (exit 1) with a message naming the files:
// - every file-scope name a .c file keeps to itself (static functions and variables, typedef
//   names, struct/union/enum tags) is defined in one file only, since the amalgamation puts
//   them all in one scope;
// - every `#include "..."` resolves to a file under c/src (or the public header).
// Macros a .c file defines are #undef'd at the end of that file in the amalgamation, so they
// cannot leak into the next one.

import {
  copyFileSync,
  existsSync,
  mkdirSync,
  readdirSync,
  readFileSync,
  writeFileSync,
} from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const cDir = resolve(here, "..");
const srcDir = join(cDir, "src");
const includeDir = join(cDir, "include");

let outDir = join(cDir, "dist");
const args = process.argv.slice(2);
for (let i = 0; i < args.length; i++) {
  if (args[i] === "--out" && i + 1 < args.length) outDir = resolve(args[++i]);
  else if (args[i].startsWith("--out=")) outDir = resolve(args[i].slice(6));
  else {
    console.error(`amalgamate.mjs: unknown argument ${args[i]}\nusage: amalgamate.mjs [--out DIR]`);
    process.exit(2);
  }
}

const problems = [];
const read = (p) => readFileSync(p, "utf8").replace(/\r\n/g, "\n");
const includeRe = /^\s*#\s*include\s+"([^"]+)"\s*$/;

// The order of the .c files does not matter for correctness (every shared declaration is in a
// header that comes first), so it is alphabetical, except rt.c goes last: it includes
// <windows.h> on Windows, whose macros must not reach the pipeline files.
const cFiles = readdirSync(srcDir)
  .filter((f) => f.endsWith(".c"))
  .sort((a, b) => (a === "rt.c") - (b === "rt.c") || (a < b ? -1 : a > b ? 1 : 0));

// ---------------------------------------------------------------------------------------------
// Headers: inlined once each, at their first include, depth first (so a header's own includes
// come before it).

const emitted = new Set();
const headerMacros = new Set();
const out = [];

function emitHeader(name, from) {
  if (name === "trackparse.h") return; // the public header is included once, at the top
  if (emitted.has(name)) return;
  const path = join(srcDir, name);
  if (!existsSync(path)) {
    problems.push(`${from}: #include "${name}" is not a file in c/src`);
    return;
  }
  emitted.add(name);
  const lines = read(path).split("\n");
  if (lines.at(-1) === "") lines.pop();
  for (const line of lines) {
    const d = /^\s*#\s*define\s+([A-Za-z_]\w*)/.exec(line);
    if (d) headerMacros.add(d[1]);
  }
  const body = [];
  for (const line of lines) {
    const m = includeRe.exec(line);
    if (m) {
      // Nested headers go before this one (the recursion appends them to `out` before this
      // header's body); leave a marker where the include was.
      emitHeader(m[1], name);
      body.push(`/* #include "${m[1]}" (inlined above) */`);
    } else {
      body.push(line);
    }
  }
  out.push(`/* ${"=".repeat(88)} */`, `/* src/${name} */`, "", ...body, "");
}

// ---------------------------------------------------------------------------------------------
// File-local names of each .c file, for the duplicate check.

// Removes comments and string/char literals (keeping line breaks), so the scan below sees code.
function stripCode(text) {
  let r = "";
  let i = 0;
  while (i < text.length) {
    const c = text[i];
    const n = text[i + 1];
    if (c === "/" && n === "*") {
      const end = text.indexOf("*/", i + 2);
      const stop = end < 0 ? text.length : end + 2;
      r += text.slice(i, stop).replace(/[^\n]/g, " ");
      i = stop;
    } else if (c === "/" && n === "/") {
      while (i < text.length && text[i] !== "\n") i++;
    } else if (c === '"' || c === "'") {
      r += c;
      i++;
      while (i < text.length && text[i] !== c && text[i] !== "\n") {
        if (text[i] === "\\") i++;
        i++;
      }
      r += c;
      i++;
    } else {
      r += c;
      i++;
    }
  }
  return r;
}

function localNames(text) {
  const code = stripCode(text);
  const names = new Set();
  // File-scope statics start at column 0. Take everything up to the first "(", "[", "=", ";" or
  // "{" (joining continuation lines), and the last identifier before it is the name.
  const lines = code.split("\n");
  for (let i = 0; i < lines.length; i++) {
    if (!/^static\b/.test(lines[i])) continue;
    let decl = lines[i];
    for (let j = i + 1; !/[([=;{]/.test(decl) && j < lines.length; j++) decl += ` ${lines[j]}`;
    const head = decl.split(/[([=;{]/)[0];
    // A function pointer variable: static T (*name)(...)
    const fp = /^static[^(]*$/.test(head) && /\(\s*\*\s*([A-Za-z_]\w*)\s*\)/.exec(decl);
    const ids = head.match(/[A-Za-z_]\w*/g) || [];
    const name = fp && decl.indexOf("(*") === head.length ? fp[1] : ids.at(-1);
    if (name && name !== "static") names.add(name);
  }
  // Tags and typedef names.
  for (const m of code.matchAll(/^(?:typedef\s+)?(?:struct|union|enum)\s+([A-Za-z_]\w*)\s*\{/gm)) {
    names.add(`tag ${m[1]}`);
  }
  // typedef ... ; at brace depth 0: the name is the function pointer's or the last identifier
  // outside the braces.
  for (const m of code.matchAll(/^typedef\b/gm)) {
    let depth = 0;
    let outside = "";
    let i = m.index + 7;
    for (; i < code.length; i++) {
      const c = code[i];
      if (c === "{") depth++;
      else if (c === "}") depth--;
      else if (c === ";" && depth === 0) break;
      else if (depth === 0) outside += c;
    }
    const fp = /\(\s*\*\s*([A-Za-z_]\w*)\s*\)/.exec(outside);
    const ids = fp ? [fp[1]] : outside.replace(/\([^)]*\)/g, " ").match(/[A-Za-z_]\w*/g) || [];
    if (ids.length) names.add(ids.at(-1));
  }
  return names;
}

function definedMacros(text) {
  const names = [];
  for (const line of text.split("\n")) {
    const d = /^\s*#\s*define\s+([A-Za-z_]\w*)/.exec(line);
    if (d && !names.includes(d[1])) names.push(d[1]);
  }
  return names;
}

// ---------------------------------------------------------------------------------------------

// The library version, from the public header (c/ alone, as in the release tarball, has no spec/).
const publicHeader = read(join(includeDir, "trackparse.h"));
const versionMatch = /#define TRACKPARSE_VERSION "([^"]+)"/.exec(publicHeader);
if (!versionMatch) problems.push("include/trackparse.h: no TRACKPARSE_VERSION");
const version = versionMatch ? versionMatch[1] : "?";
const header = [
  "/* trackparse.c: the trackparse C library in one file (the amalgamation).",
  ` * Version ${version}. https://4matic.github.io/trackparse/`,
  " *",
  " * Code generated by c/scripts/amalgamate.mjs from c/src. DO NOT EDIT: change c/src and",
  " * regenerate.",
  " *",
  " * Use: compile this file with your program, next to trackparse.h (C11; on GCC and Clang also",
  " * -ffp-contract=off, on MSVC /fp:precise /utf-8). Define TRACKPARSE_STATIC (or",
  " * TRACKPARSE_AMALGAMATION) for every file that includes trackparse.h, or at least on Windows,",
  " * where the header otherwise declares the functions as DLL imports. To build a shared library",
  " * from this file define TRACKPARSE_BUILDING instead and compile with -fvisibility=hidden: only",
  " * the trackparse_* functions are exported.",
  " *",
  " * Every internal function and table is static here (TP_PRIVATE). Link with -lm and, outside",
  " * Windows, -pthread. */",
  "",
  "/* Not building a library: the API needs no import or export decoration. */",
  "#if !defined(TRACKPARSE_BUILDING) && !defined(TRACKPARSE_STATIC)",
  "#define TRACKPARSE_STATIC",
  "#endif",
  "/* The public header first, so TRACKPARSE_API keeps its library meaning (export when building",
  " * a shared library); then TRACKPARSE_AMALGAMATION makes the internals static (rt.h). */",
  '#include "trackparse.h"',
  "#ifndef TRACKPARSE_AMALGAMATION",
  "#define TRACKPARSE_AMALGAMATION 1",
  "#endif",
  "",
];
out.push(...header);

const owners = new Map(); // name -> first file
for (const file of cFiles) {
  const text = read(join(srcDir, file));
  for (const name of localNames(text)) {
    if (owners.has(name)) {
      problems.push(
        `file-local name "${name.replace(/^tag /, "struct/enum tag ")}" is defined in both ` +
          `src/${owners.get(name)} and src/${file}: rename one (the amalgamation is one scope)`,
      );
    } else {
      owners.set(name, file);
    }
  }
  const lines = text.split("\n");
  if (lines.at(-1) === "") lines.pop();
  const body = [];
  for (const line of lines) {
    const m = includeRe.exec(line);
    if (m) {
      if (m[1] !== "trackparse.h") emitHeader(m[1], file);
      body.push(`/* #include "${m[1]}" (inlined above) */`);
    } else {
      body.push(line);
    }
  }
  const undefs = definedMacros(text)
    .filter((n) => !headerMacros.has(n))
    .map((n) => `#undef ${n}`);
  // Headers this file pulled in are already in `out`; its body follows them.
  out.push(`/* ${"=".repeat(88)} */`, `/* src/${file} */`, "", ...body, "");
  if (undefs.length) out.push(`/* end of src/${file}: its macros stay local */`, ...undefs, "");
}

if (problems.length) {
  console.error(`amalgamate.mjs: ${problems.length} problem(s):`);
  for (const p of problems) console.error(`  ${p}`);
  process.exit(1);
}

mkdirSync(outDir, { recursive: true });
writeFileSync(join(outDir, "trackparse.c"), `${out.join("\n")}\n`);
copyFileSync(join(includeDir, "trackparse.h"), join(outDir, "trackparse.h"));
const hpp = join(includeDir, "trackparse.hpp");
if (existsSync(hpp)) copyFileSync(hpp, join(outDir, "trackparse.hpp"));
console.log(
  `amalgamate.mjs: ${join(outDir, "trackparse.c")} (${cFiles.length} source files, ` +
    `${emitted.size} headers)`,
);
