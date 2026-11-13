#!/usr/bin/env node
// Random differential run: generated single-track inputs and tracklists with edits through the
// built JS package (the reference), the Python port, the Rust port and the Go port, comparing the
// FULL JSON outputs, like scripts/compare-ports.mjs does for the fixture inputs.
//
// Usage (from the repo root):
//   pnpm --filter trackparse build
//   node scripts/fuzz-ports.mjs                         # all ports, 20,000 tracks, seed 1
//   node scripts/fuzz-ports.mjs --only=go --n=500000 --seed=7
//   node scripts/fuzz-ports.mjs --only=rust --n=100000 --lists=50000 --out=diffs.jsonl
//
// Flags:
//   --n=N        single-track cases (parse, parseArtists, format, normalize); default 20000
//   --lists=N    tracklists; each gives a parseTracklist and an applyEdits case with random
//                edits (invalid ones included); default 58% of --n (290,000 for 500,000)
//   --seed=N     generator seed (mulberry32); default 1. The same seed gives the same cases.
//   --only=X     python, rust or go; default all three
//   --batch=N    cases per port process; default 20000
//   --out=FILE   write every differing case as JSONL (the case and each port's output)
//
// The inputs are what the JSON door can carry faithfully: no lone surrogates (Rust and Go read
// them as U+FFFD), no NaN or -0 in options or edits (JSON.stringify changes them). Option values
// include odd types and strings, integer-like keyword keys and unknown names, as the R0.8
// fallbacks must agree too. Go and Rust are built once (go build, cargo build --release) and
// their binaries run per batch; Python runs scripts/dump_py.py per batch.
// Exits 1 if any case differs.

import { execFileSync } from "node:child_process";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { comparable, describeDiff, root, runJs, runLines, runPython } from "./lib/ports.mjs";

const MAX_SHOWN = 20;

function flag(name, fallback) {
  const arg = process.argv.find((a) => a.startsWith(`--${name}=`));
  return arg ? arg.slice(name.length + 3) : fallback;
}
const nTracks = Number(flag("n", "20000"));
const nLists = Number(flag("lists", String(Math.round(nTracks * 0.58))));
const seed = Number(flag("seed", "1"));
const only = flag("only", "");
const batchSize = Number(flag("batch", "20000"));
const outFile = flag("out", "");

// ---------------------------------------------------------------------------------------------
// Generator

let state = seed >>> 0;
/** mulberry32 */
function next() {
  state = (state + 0x6d2b79f5) >>> 0;
  let t = state;
  t = Math.imul(t ^ (t >>> 15), t | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return (t ^ (t >>> 14)) >>> 0;
}
const below = (n) => (n <= 0 ? 0 : next() % n);
const pick = (items) => items[below(items.length)];
const chance = (percent) => below(100) < percent;
const times = (n, f) => Array.from({ length: n }, f);

const TRICKY = [
  " ", "  ", "-", " - ", " – ", "—", "(", ")", "[", "]", "{", "}", '"', "'", "“", "”", "|", " | ",
  ",", ";", "&", " & ", " x ", " X ", " feat. ", " ft ", " ft. ", " featuring ", "prod. by ",
  " prod. ", " by ", " vs ", " and ", " with ", ".", ":", "_", "0", "1", "01. ", "3:45 ",
  "[03:45] ", "Remix", " Mix", "VIP", " Edit", " Bootleg", "Original Mix", "Extended Mix",
  "Official Video", "Official Audio", "Lyrics", "HD", "4K", "Free Download", "OUT NOW", "ID",
  "​", " ", "　", "﻿", "é", "é", "𝄞", "�", "×", "／", ".mp3",
  ".flac", "2015", "(2015)", "[Explicit]", "(Clean)", "Topic", "VEVO", "Unknown Artist",
  "Track 01", "Untitled", "a", "Ab", "x",
];

/** One random code point (never a surrogate), biased to the ranges the rules look at. */
function anyChar() {
  let cp;
  switch (below(10)) {
    case 0: case 1: case 2: case 3: cp = below(0x80); break;
    case 4: cp = below(0x100); break;
    case 5: cp = 0x300 + below(0x70); break; // combining marks
    case 6: cp = 0x2000 + below(0x70); break; // spaces, dashes, quotes, invisibles
    case 7: cp = below(0x10000); break;
    case 8: cp = 0xff00 + below(0xf0); break; // fullwidth forms
    default: cp = below(0x110000);
  }
  if (cp >= 0xd800 && cp <= 0xdfff) cp = 0xfffd;
  return String.fromCodePoint(cp);
}
const randomChars = (max) => times(below(max + 1), anyChar).join("");
const noisyString = () => times(below(25), () => (chance(50) ? pick(TRICKY) : randomChars(4))).join("");

const ARTISTS = [
  "Noisia", "Phace", "Fox Stevenson", "Sub Focus", "Mefjus", "Camo & Krooked", "Alix Perez",
  "Chase & Status", "Above & Beyond", "Foreign Beggars", "Black Sun Empire", "Jay-Z", "AC/DC",
  "Simon & Garfunkel", "The Prototypes", "Kino", "Кино", "宇多田ヒカル", "DJ Fresh", "A$AP Rocky",
];
const TITLES = [
  "Tentacles", "Bruises", "Out the Blue", "Dead Limit", "Stigma", "Blind Faith", "Shellshock",
  "Untitled", "Intro", "Love & Hate", "I Love You (Part 2)", "Mr. Brightside", "Hi-Fi", "2 Much",
];
const VERSION_WORDS = ["Remix", "VIP", "Edit", "Bootleg", "Flip", "Refix", "Dub", "Mix", "Rework", "Version", "Instrumental", "Sped Up"];
const JOINERS = [" & ", ", ", " x ", " vs. ", " and ", " + ", " / ", " with "];

/** A track-shaped input, in one of the shapes the modes look for. */
function trackString() {
  const artists = [...new Set(times(1 + below(3), () => pick(ARTISTS)))];
  let s = artists.join(pick(JOINERS));
  if (chance(40)) s += `${pick([" feat. ", " ft. ", " featuring ", " (feat. "])}${pick(ARTISTS)}${chance(30) ? ")" : ""}`;
  s += `${pick([" - ", " – ", " — ", " -", "-", " | ", " ~ ", ": "])}${pick(TITLES)}`;
  for (let k = below(3); k > 0; k--) {
    const open = pick(["(", "[", "{", " - "]);
    const close = { "(": ")", "[": "]", "{": "}", " - ": "" }[open];
    const inner = pick([
      `${pick(ARTISTS)} ${pick(VERSION_WORDS)}`,
      pick(VERSION_WORDS),
      `${pick(["Original", "Extended", "Radio", "Club"])} Mix`,
      pick(["Official Video", "Official Audio", "Lyric Video", "HD", "Free Download", "OUT NOW"]),
      `feat. ${pick(ARTISTS)}`,
      `prod. by ${pick(ARTISTS)}`,
      String(1990 + below(40)),
      noisyString(),
    ]);
    s += ` ${open}${inner}${close}`;
  }
  if (chance(20)) s = `${pick(["01. ", "1 - ", "A1 ", "[03:45] ", "12) ", "07 "])}${s}`;
  if (chance(15)) s += pick([".mp3", ".flac", " (2015)", " [Explicit]", " Remix"]);
  return s;
}

function anyString() {
  switch (below(4)) {
    case 0: return randomChars(60);
    case 1: return noisyString();
    default: return trackString();
  }
}

const ODD = [5, 0, -1, true, false, null, {}, [], "", " "];

function stringList() {
  if (chance(10)) return pick(ODD);
  return times(below(4), () => (chance(85) ? pick([pick(ARTISTS), pick(TRICKY), anyString(), "a", ""]) : pick(ODD)));
}

const VERSION_TYPES = [
  "remix", "bootleg", "vip", "edit", "flip", "dub", "mix", "spedUp", "version", "original", "x", "", "Remix", "constructor",
];
const JUNK_KINDS = ["video", "audio", "lyrics", "quality", "promo", "other", "weird", "", "__proto__"];
const KEYS = [
  "rerub", "2", "10", "01", "-1", "0", "4294967294", "4294967295", "1.5", " vip ", "VIP", "feat.",
  "mix", "official", "out now", "a b", "", " ", "x.", "toString", "Ab",
];

function objectOf(values) {
  if (chance(10)) return pick(ODD);
  const out = {};
  for (let k = below(5); k > 0; k--) out[chance(80) ? pick(KEYS) : anyString()] = chance(90) ? pick(values) : pick(ODD);
  return out;
}

function keywords() {
  if (chance(5)) return pick(ODD);
  const k = {};
  if (chance(50)) k.versionHeads = objectOf(VERSION_TYPES);
  if (chance(30)) k.descriptors = stringList();
  if (chance(30)) k.genres = stringList();
  if (chance(50)) k.junk = objectOf(JUNK_KINDS);
  if (chance(40)) k.featMarkers = chance(70) ? times(below(3), () => pick(["with", "&", "-", "x", "w/", "", "Feat"])) : stringList();
  return k;
}

const MODES = [undefined, "auto", "clean", "youtube", "filename", "tracklist", "nope", 3, null, ""];
const SPLIT_AND = [undefined, "auto", "always", "never", "x", true, null];

function parseOptions(input) {
  const o = {};
  if (chance(70)) o.mode = pick(MODES);
  if (chance(30)) o.uploader = pick(["", "Noisia", "NoisiaVEVO", "Noisia - Topic", pick(ARTISTS), anyString(), input, 5, null]);
  if (chance(30)) o.knownArtists = chance(30) && input ? [input.slice(0, below(input.length + 1))] : stringList();
  if (chance(30)) o.splitAnd = pick(SPLIT_AND);
  if (chance(15)) o.keywords = keywords();
  for (const key of Object.keys(o)) if (o[key] === undefined) delete o[key];
  return o;
}

function formatOptions() {
  const o = {};
  if (chance(50)) o.feat = pick(["source", "artist", "title", "omit", "nope", null, 1]);
  if (chance(30)) o.featMarker = pick(["ft.", "feat.", "&", "", 5, null, anyString()]);
  if (chance(40)) o.joiners = pick(["original", "canonical", "x", null]);
  if (chance(40)) o.versions = pick(["all", "none", "x", null]);
  if (chance(30)) o.producers = pick([true, false, 0, 1, "no", null]);
  if (chance(40)) o.position = pick([true, false, 1, 0, "yes", null]);
  return o;
}

// Tracklists: the fragments of the property tests, and realistic timed lists.
const LIST_FRAGMENTS = [
  "\n", "\r\n", "\r", "\n\r", " ", " ", "\u0085", " ", "  ", "\t", "　", "00:00",
  "0:00 ", "04:37 ", "1:02:03 ", "01:02:03.5 ", "125:30 ", "12:34,250", "1:75 ", "0:61:00 ", "1:2",
  "[", "]", "(", ")", "[00:10] ", "(1:00)", " - ", " – ", "-", "|", ".", ":", "=> ", "==>", "1 ",
  "2 ", "01. ", "3) ", "Artist", "Title", "Noisia - Stigma", "Intro", "interview", "Tracklist:",
  "Part 2", "https://x.y", "www.", "_-_", "Ab-Cd", "ID", "(VIP)", "**", "*", "__", "_", "***",
  "* ", "- ", "• ", "**[", "]**", "w/ ", "W/", "​", "𝄞", "🎵", "é", "Кино", "a", "7", "\u0000",
  '"', "\\",
];
const noisyText = () => times(below(41), () => pick(LIST_FRAGMENTS)).join("");

const pad = (n, w = 2) => String(n).padStart(w, "0");
function writeTime(style, seconds, frac) {
  const h = Math.floor(seconds / 3600), m = Math.floor((seconds % 3600) / 60), s = seconds % 60;
  switch (style) {
    case 0: return `${Math.floor(seconds / 60)}:${pad(s)}`;
    case 1: return `${pad(Math.floor(seconds / 60))}:${pad(s)}`;
    case 2: return `${h}:${pad(m)}:${pad(s)}`;
    case 3: return `${pad(h)}:${pad(m)}:${pad(s)}`;
    case 4: return `${pad(Math.floor(seconds / 60))}:${pad(s)}.${frac % 10}`;
    default: return `${Math.floor(seconds / 60)}:${pad(s)},${pad(frac, 3)}`;
  }
}

function listLine(clock) {
  if (below(7) === 0) return pick(["", "Tracklist:", "a note", "https://example.com", "Part 2", "ID - ID", noisyString()]);
  const seconds = chance(80) ? clock : below(4 * 3600);
  const t = writeTime(below(6), seconds, below(1000));
  const gap = pick([" ", "  ", " - ", " | ", " => ", ". ", "\t"]);
  const rest = chance(70) ? trackString() : pick(["Intro", "Paperclip interview", "", "w/ " + trackString(), `**${trackString()}**`]);
  const lead = pick(["", "", " ", "[", "**", "- ", "• ", `${1 + below(40)}. `]);
  if (lead === "[") return `[${t}]${gap}${rest}`;
  if (lead === "**") return `**${t}**${gap}${rest}`;
  return `${lead}${t}${gap}${rest}`;
}

function timedList() {
  const eol = pick(["\n", "\r\n", "\r"]);
  let clock = below(300);
  const lines = times(1 + below(30), () => {
    clock += chance(5) ? -below(600) : below(400);
    if (clock < 0) clock = 0;
    return listLine(clock);
  });
  return lines.join(eol) + (chance(50) ? eol : "");
}

const anyText = () => (chance(70) ? timedList() : noisyText());

function tracklistOptions() {
  const o = {};
  if (chance(30)) o.mode = pick(MODES);
  if (chance(20)) o.knownArtists = stringList();
  if (chance(20)) o.splitAnd = pick(SPLIT_AND);
  if (chance(10)) o.keywords = keywords();
  if (chance(30)) o.audioSeconds = pick([0, 600, 3600, 7200, -1, 1.5, Infinity, "600", null, true]);
  if (chance(20)) o.markerWords = chance(80) ? times(below(3), () => pick(["Break", "ID", "Intro", "", " ", "x"])) : stringList();
  for (const key of Object.keys(o)) if (o[key] === undefined) delete o[key];
  return o;
}

const FIELD_VALUES = [
  -3, 0, 1, 2, 5, 15, 1.5, -0.25, 1e300, -1e300, Infinity, -Infinity, null, true, false, "x", "",
  [1, 2], {}, 9007199254740993, 359999.5, 3600, 86400, "3",
];
const OPS = ["set-time", "shift", "sort-by-time", "set-rest", "insert", "delete", "x"];
const EDIT_TEXTS = ["a\nb", "c\rd", "00:10 New - Line", "w/ x", "", "**00:05** A - B", "1:00:00 X - Y"];

function edits(lineCount) {
  const line = () => (chance(70) ? 1 + below(lineCount + 1) : pick(FIELD_VALUES));
  return times(below(7), () => {
    if (chance(5)) return pick([null, 5, "shift", [], {}]);
    const op = chance(95) ? pick(OPS) : pick(ODD);
    const e = { op };
    if (chance(70)) e.line = line();
    if (chance(70)) e.seconds = chance(70) ? below(20000) - 5000 + pick([0, 0, 0.5, 0.25, 0.125]) : pick(FIELD_VALUES);
    if (chance(30)) e.from = line();
    if (chance(40)) e.after = chance(80) ? below(lineCount + 2) : pick(FIELD_VALUES);
    if (chance(20)) e.lines = chance(80) ? times(below(4), line) : pick(FIELD_VALUES);
    if (chance(50)) e.rest = chance(80) ? pick([...EDIT_TEXTS, trackString()]) : pick(FIELD_VALUES);
    if (chance(50)) e.text = chance(80) ? pick([...EDIT_TEXTS, listLine(below(3600))]) : pick(FIELD_VALUES);
    return e;
  });
}

const empty = { options: {}, formatOptions: {}, edits: [] };
const SINGLE_FNS = ["parse", "parse", "parse", "parse", "parseArtists", "format", "normalize"];

/** The k-th case of the run, generated in order so that a seed fixes every case. */
function* generate() {
  let i = 0;
  for (let k = 0; k < nTracks; k++) {
    const fn = pick(SINGLE_FNS);
    const input = anyString();
    const c = { ...empty, i: i++, fn, input };
    if (fn !== "normalize") c.options = parseOptions(input);
    if (fn === "format") c.formatOptions = formatOptions();
    yield c;
  }
  for (let k = 0; k < nLists; k++) {
    const input = anyText();
    const options = tracklistOptions();
    yield { ...empty, i: i++, fn: "parseTracklist", input, options };
    const lineCount = input.split(/\r\n|\r|\n/).length;
    yield { ...empty, i: i++, fn: "applyEdits", input, options, edits: edits(lineCount) };
  }
}

// ---------------------------------------------------------------------------------------------
// Ports

function buildGo() {
  const dir = mkdtempSync(join(tmpdir(), "trackparse-fuzz-"));
  const bin = join(dir, "dump");
  execFileSync("go", ["build", "-o", bin, "./cmd/dump"], { cwd: join(root, "go"), stdio: "inherit" });
  return runLines("Go", bin, [], root);
}

function buildRust() {
  execFileSync("cargo", ["build", "--quiet", "--release", "--example", "dump"], { cwd: join(root, "rust"), stdio: "inherit" });
  return runLines("Rust", join(root, "rust", "target", "release", "examples", "dump"), [], root);
}

const ports = [
  { name: "Python", key: "py", build: () => runPython },
  { name: "Rust", key: "rs", build: buildRust },
  { name: "Go", key: "go", build: buildGo },
].filter((p) => !only || p.name.toLowerCase() === only);
if (ports.length === 0) {
  console.error(`unknown port ${only}: use --only=python, --only=rust or --only=go`);
  process.exit(2);
}
for (const port of ports) port.run = port.build();

const stats = new Map(ports.map((p) => [p.name, { byFn: {}, diffs: [] }]));
const out = [];
const started = Date.now();
let batch = [];
let done = 0;

async function flush() {
  if (batch.length === 0) return;
  const jsResults = batch.map(runJs);
  for (const port of ports) {
    const other = await port.run(batch);
    const s = stats.get(port.name);
    batch.forEach((c, k) => {
      const j = jsResults[k];
      const p = other.get(c.i);
      s.byFn[c.fn] ??= { total: 0, differ: 0 };
      s.byFn[c.fn].total++;
      if (comparable(j) === comparable(p)) return;
      s.byFn[c.fn].differ++;
      const detail = describeDiff(port, j, p);
      if (s.diffs.length < MAX_SHOWN) s.diffs.push({ c, detail });
      if (outFile) out.push(JSON.stringify({ port: port.key, case: c, js: j, [port.key]: p ?? null, detail }));
    });
  }
  done += batch.length;
  const differing = [...stats.values()].reduce((n, s) => n + Object.values(s.byFn).reduce((m, f) => m + f.differ, 0), 0);
  process.stderr.write(`\r${done} cases, ${differing} differences, ${Math.round((Date.now() - started) / 1000)} s`);
  batch = [];
}

for (const c of generate()) {
  batch.push(c);
  if (batch.length >= batchSize) await flush();
}
await flush();
process.stderr.write("\n");

let total = 0;
console.log(`seed ${seed}: ${nTracks} single-track cases, ${nLists} tracklists (parseTracklist + applyEdits)`);
for (const port of ports) {
  const s = stats.get(port.name);
  console.log(`JS dist vs ${port.name}`);
  let differ = 0;
  for (const [fn, f] of Object.entries(s.byFn)) {
    console.log(`  ${fn.padEnd(15)} ${f.total - f.differ}/${f.total} identical`);
    differ += f.differ;
  }
  console.log(`differences: ${differ}`);
  for (const { c, detail } of s.diffs) {
    console.log(`\n- [${c.fn}] ${JSON.stringify(c.input)} options=${JSON.stringify(c.options)}`);
    if (c.fn === "format") console.log(`  formatOptions=${JSON.stringify(c.formatOptions)}`);
    if (c.fn === "applyEdits") console.log(`  edits=${JSON.stringify(c.edits)}`);
    console.log(`  ${detail}`);
  }
  total += differ;
}
if (outFile) writeFileSync(outFile, out.length ? `${out.join("\n")}\n` : "");
process.exit(total ? 1 : 0);
