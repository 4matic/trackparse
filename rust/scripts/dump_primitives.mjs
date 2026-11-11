#!/usr/bin/env node
// Expected values for the R0 primitive unit tests (src/words.rs), taken from the JS reference:
// the functions of js/src/words.ts run on tricky code points and strings.
//
//   node scripts/dump_primitives.mjs            # writes tests/data/primitives.json
//   node scripts/dump_primitives.mjs --check    # fails if that file is stale
//   node scripts/dump_primitives.mjs --all out.json
//       every Unicode scalar value and 200,000 random strings around Σ (large; for a one-off
//       sweep, not committed):
//       TRACKPARSE_PRIMITIVES=out.json cargo test --lib primitives
//
// Needs Node 24+ (it imports the TypeScript source directly). Regenerate when Node's Unicode
// version changes, together with src/unicode_tables.rs (scripts/gen_data.mjs).
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const words = await import(pathToFileURL(join(here, "../../js/src/words.ts")).href);
const args = process.argv.slice(2);
const all = args[0] === "--all";
const check = args[0] === "--check";
const outFile = all ? args[1] : join(here, "../tests/data/primitives.json");

const isScalar = (cp) => cp < 0xd800 || cp > 0xdfff;

// --- code points -----------------------------------------------------------------------------
const cps = new Set();
if (all) {
  for (let cp = 0; cp <= 0x10ffff; cp++) if (isScalar(cp)) cps.add(cp);
} else {
  for (let cp = 0; cp < 0x250; cp++) cps.add(cp);
  const tricky = [
    0x2b0, 0x2c6, 0x300, 0x301, 0x345, 0x36f, 0x370, 0x37a, 0x386, 0x3a3, 0x3c2, 0x3c3, 0x3f4, 0x410, 0x430,
    0x5d0, 0x621, 0x640, 0x660, 0x903, 0x904, 0xe01, 0xe31, 0x10a0, 0x10d0, 0x13a0, 0x13f8, 0x1680, 0x167f,
    0x1681, 0x16ee, 0x180e, 0x1c80, 0x1c90, 0x1d2c, 0x1e9e, 0x1f88, 0x1fbc, 0x1ffc, 0x1fff, 0x2000, 0x200a,
    0x200b, 0x200c, 0x200d, 0x2010, 0x2015, 0x2028, 0x2029, 0x202a, 0x202f, 0x2030, 0x205f, 0x2060, 0x2071,
    0x2102, 0x2126, 0x212a, 0x212b, 0x2160, 0x2170, 0x2183, 0x2184, 0x24b6, 0x24d0, 0x2c00, 0x2e2f, 0x3000,
    0x3001, 0x3005, 0x3006, 0x3007, 0x3021, 0x3031, 0x3041, 0x30a1, 0x3400, 0x4e00, 0x9fff, 0xa000, 0xa640,
    0xa78d, 0xa7f2, 0xab70, 0xac00, 0xd7a3, 0xd7ff, 0xe000, 0xf8ff, 0xf900, 0xfb00, 0xfb01, 0xfb13, 0xfe58,
    0xfeff, 0xff08, 0xff21, 0xff41, 0xff70, 0xfffd, 0xffff, 0x10000, 0x10140, 0x10400, 0x10428, 0x104b0,
    0x10570, 0x10c80, 0x118a0, 0x16e40, 0x16fe0, 0x1d400, 0x1d41a, 0x1d6a8, 0x1e900, 0x1e922, 0x1f130,
    0x1f600, 0x1f1eb, 0x20000, 0x2ffff, 0x30000, 0xe0001, 0xe0100, 0x10fffd, 0x10ffff,
  ];
  for (const cp of tricky) cps.add(cp);
  for (let cp = 0x250; cp <= 0x10ffff; cp += 251) if (isScalar(cp)) cps.add(cp);
}
const cpList = [...cps].sort((a, b) => a - b);
const bits = (test) => cpList.map((cp) => (test(cp) ? "1" : "0")).join("");
const chr = String.fromCodePoint;
const codePoints = {
  // Runs of consecutive code points as [first, count], to keep the file small.
  runs: (() => {
    const runs = [];
    for (const cp of cpList) {
      const last = runs[runs.length - 1];
      if (last && last[0] + last[1] === cp) last[1]++;
      else runs.push([cp, 1]);
    }
    return runs;
  })(),
  whitespace: bits((cp) => words.isWhitespaceCode(cp)),
  letter: bits((cp) => words.isLetterAt(chr(cp), 0)),
  upper: bits((cp) => words.isUpperAt(chr(cp), 0)),
  jsSpace: bits((cp) => chr(cp).trim() === ""),
};

// --- strings ---------------------------------------------------------------------------------
const strings = new Set([
  "", " ", "Noisia", "  The  Upbeats ", "FEAT.", "Feat", "ft.", "Remix,", "remix!!", "a.", ".", "..", "É.", "İ.",
  "İstanbul", "ISTANBUL", "ıstanbul", "İ", "STRAẞE", "Straße", "ǅ", "ǆ", "Ǆ", "ᾈ", "ᾀ", "ΟΔΥΣΣΕΑΣ",
  "Σίσυφος", "ΣΊΣΥΦΟΣ", "Σ", "ΑΣ", "ΑΣ ", "ΑΣΑ", "ΑΣ.", "ΑΣ'", "ΑΣ’Α", "ΑΣ́", "ΆΣ", "Α­Σ", "Σ Σ",
  "aΣ", "aΣb", "1Σ", ":Σ", "ΑΣ-Β", "Α.Σ", "Α Σ", "ΑΣ‍", "Beyoncé", "Beyoncé", "BEYONCÉ", "Björk",
  "Björk", "Tiësto", "TIËSTO", "Sigur Rós", "Mötley Crüe", "Кино", "КИНО", "Ёлка", "ёлка", "Й", "й",
  "한국어", "각", "각", "が", "が", "ｆｕｌｌ", "Ａ", "ﬁnal", "ŉ", "ǰ", "ΐ", "ᾳ", "ᾼ", "Å", "Å", "Ω",
  "Ⅷ", "ⅷ", "Ⓐ", "ⓐ", "𝐀𝐁", "𐐀𐐨", "🎵", "👨‍👩‍👧", "🇫🇮", "à́b", "ậ", "ậ", "a̴",
  "a҃", "a᪰", "a⃝", "aͅ", "é́", "ạ̈", "ﾊﾟ", " x ", "\u0085x\u0085",
  "﻿x﻿", "᠎x᠎", "​x​", " x ", "　x　", "\tx\n", "\u000bx\u000c",
  " x ", " x ", " x", "a   b　c", "a\u0085b", "a﻿b", "a​b",
  "2015", "2015,", "2015.", "2015..", "1899", "1900", "2099", "2100", "02015", "201", "２０１５", "20１5", "2015a",
  "١٩٩٩", "feat.", "w/", "b2b", "P!nk", "A$AP", "AC/DC", "x", "X", "&", "?", "??", "ID?", "Ä:", "Z;", "[Z]", "Ź.",
  "ẞ", "ß", "ſ", "K", "Å", "Ǆungla", "ΌΣΟΣ", "ὈΔΥΣΣΕΎΣ", "Ꭰ", "ꭰ", "Ა", "ა", "Ⴀ", "Ɥ", "Ꟶ", "𞤀", "𑢠", "Ꙁ", "Ⱥ",
  // toLowerCase(): special casing, scripts with late case pairs, and Σ in and out of final position.
  "İΣ", "Σİ", "İΣİ", "İİ", "i̇Σ", "ẞΣ", "ΣẞΣ", "STRAẞΣ ẞΣ", "ᎠΣ", "ᎠᎡᎢ", "ΣᏴ", "ꭰΣ", "ᏸΣ", "ᲐᲑΣ", "ΣᲐ", "ႠΣ", "აΣ",
  "𞤀Σ", "𞤀𞤁𞤂", "Σ𞤢", "𞤢Σ", "𞥃Σ𞤀", "ΑΣ𞤀", "ΑΣ ᲐΣ", "ǅΣ", "ʰΣ", "ΑʰΣ", "ΑΣʰ", "ΑΣʰΑ", "ᾼΣ", "ΑΣͅ", "ΑΣͅΑ", "ⒶΣ",
  "ⅧΣ", "ΑΣΣ", "ΣΣ", "ΣΣΣ", "ΑΣΣ.", "ΑΣ·Σ", "ΑΣ·", "ΑΣ:Α", "ΑΣ'Α", "ΑΣ1", "1Σ1", "ΑΣ\u200b", "ΑΣ\u200bΑ", "Α\u00adΣ\u00ad",
  "𝐀Σ", "Σ𝐀", "ΑΣ🎵", "🎵Σ", "aΣ a", "AΣ", "ÅΣ", "ΌΣ ΌΣΟ", "Ꟑ", "Ƛ", "Ꟛ", "Ꟗ", "Ꞔ", "Ᲊ", "𐵐", "𖺠",
]);
if (!all) {
  // Sigma in context: a small alphabet, fixed seed.
  let seed = 7;
  const rnd = () => {
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    return seed / 0x7fffffff;
  };
  const alphabet = ["Σ", "Α", "α", " ", "'", ".", "́", "­", "a", "1", ":", "-", "‍", "𝐀", "ͅ"];
  // A second, later alphabet (the first one's strings stay as they were).
  const wide = [...alphabet, "Σ", "İ", "ẞ", "Ꭰ", "ꭰ", "Ა", "ა", "𞤀", "𞤢", "ʰ", "·", "ǅ", "Ⓐ"];
  const draw = (letters) => {
    let s = "";
    const n = 1 + Math.floor(rnd() * 6);
    for (let k = 0; k < n; k++) s += letters[Math.floor(rnd() * letters.length)];
    return s;
  };
  for (let i = 0; i < 160; i++) {
    let s = "";
    const n = 1 + Math.floor(rnd() * 6);
    for (let k = 0; k < n; k++) s += alphabet[Math.floor(rnd() * alphabet.length)];
    strings.add(s);
  }
  for (let i = 0; i < 400; i++) strings.add(draw(wide));
}
if (all) {
  // Σ between code points drawn from all cased and case-ignorable ones, and a few others.
  let seed = 11;
  const rnd = (n) => {
    // mulberry32
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return Math.floor((((t ^ (t >>> 14)) >>> 0) / 4294967296) * n);
  };
  const pool = [];
  for (const cp of cpList) if (/^[\p{Cased}\p{Case_Ignorable}]$/u.test(chr(cp))) pool.push(chr(cp));
  const plain = ["Σ", "Σ", "Σ", "Α", " ", "1", "-", "İ", "ẞ", "가", "🎵"];
  for (let i = 0; i < 200000; i++) {
    let s = "";
    for (let k = 1 + rnd(6); k > 0; k--) s += rnd(2) ? plain[rnd(plain.length)] : pool[rnd(pool.length)];
    strings.add(s);
  }
}
const record = (s) => ({
  s,
  utf16Len: s.length,
  asciiLower: words.asciiLower(s),
  lower: s.toLowerCase(),
  wordKey: words.wordKey(s),
  dedupKey: words.dedupKey(s),
  collapse: words.collapseSpaces(s),
  words: words.splitWords(s),
  jsTrim: s.trim(),
  jsTrimEnd: s.trimEnd(),
  year: words.yearOf(s),
});

const lines = [
  `"unicode": ${JSON.stringify(process.versions.unicode)}`,
  `"codePoints": ${JSON.stringify(codePoints)}`,
  `"strings": [\n${[...strings].map((s) => JSON.stringify(record(s))).join(",\n")}\n]`,
];
// Every scalar value whose toLowerCase() is not itself; all others must map to themselves.
const lowerSweep = [];
for (let cp = 0; cp <= 0x10ffff; cp++) {
  if (isScalar(cp) && chr(cp).toLowerCase() !== chr(cp)) lowerSweep.push([cp, chr(cp).toLowerCase()]);
}
lines.push(`"lowerSweep": ${JSON.stringify(lowerSweep)}`);
if (all) {
  // Every scalar value whose dedup key is not itself; all others must map to themselves.
  const sweep = cpList.filter((cp) => words.dedupKey(chr(cp)) !== chr(cp)).map((cp) => [cp, words.dedupKey(chr(cp))]);
  lines.push(`"dedupSweep": ${JSON.stringify(sweep)}`);
}
const text = `{\n${lines.join(",\n")}\n}\n`;
if (check) {
  if (readFileSync(outFile, "utf8") !== text) {
    console.error("tests/data/primitives.json is stale: run `node scripts/dump_primitives.mjs`");
    process.exit(1);
  }
} else {
  writeFileSync(outFile, text);
  console.log(`wrote ${outFile}: ${cpList.length} code points, ${strings.size} strings`);
}
