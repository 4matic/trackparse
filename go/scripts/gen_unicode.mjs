#!/usr/bin/env node
// Writes go/unicode_gen.go: the Unicode tables of the Go port, all taken from the JS reference
// engine (Node) so the module's Unicode version is Node's, never the user's Go toolchain's:
//
//   - general categories L and Lu (SPEC R0.6): /\p{L}/u and /\p{Lu}/u, as range tables;
//   - String.prototype.toLowerCase() (R0.4 dedup key): runs of code points with a common delta,
//     the multi-code-point results, and the properties Cased and Case_Ignorable that the
//     final-sigma rule reads;
//   - String.prototype.normalize("NFC" | "NFD") (R1, R0.4): full canonical decompositions,
//     canonical combining class ranks, composition pairs and the NFC quick-check sets. Hangul
//     syllables are left to the algorithm in unicode.go.
//
// How the canonical combining classes are derived (Node has no API for them):
//   1. Only code points with NFD(c) == c can appear in decomposed text, so only they get a class.
//   2. P1 = U+0334 and P240 = U+0345 are probes with NFD(P240 + P1) == P1 + P240, so
//      ccc(P1) < ccc(P240). Canonical ordering swaps two adjacent non-starters exactly when the
//      first has the larger class, and never moves a starter (class 0). So c is a non-starter iff
//      NFD(c + P1) == P1 + c (then ccc(c) > ccc(P1) > 0) or NFD(P240 + c) == c + P240 (then
//      0 < ccc(c) < ccc(P240)). A non-starter that fails both would need
//      ccc(P240) <= ccc(c) <= ccc(P1), which is impossible.
//   3. The non-starters are sorted with the comparator "a before b iff NFD(b + a) == a + b"
//      (a has the smaller class), equal when neither order is swapped, and numbered 1, 2, ...
//      by class. The numbers are ranks, not the Unicode values: normalization only observes the
//      order of classes and whether two are equal, and ranks keep both. The test with
//      TRACKPARSE_UNICODE_FULL=1 checks every ordered pair of non-starters against Node.
//
// Composition pairs: a primary composite is a code point c with NFD(c) != c and NFC(c) == c.
// Its pair is (NFC of all but the last code point of NFD(c), that last code point); the
// generator checks that the first part is one code point and that NFC(first + last) == c.
//
//   node go/scripts/gen_unicode.mjs                 write unicode_gen.go and the test data
//   node go/scripts/gen_unicode.mjs --check         exit 1 if either is stale
//   node go/scripts/gen_unicode.mjs --cases <dir>   write the exhaustive cases to <dir>/cases.jsonl
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const goDir = resolve(here, "..");
const UNICODE = process.versions.unicode;

const S_BASE = 0xac00;
const S_COUNT = 11172;
const isSurrogate = (cp) => cp >= 0xd800 && cp <= 0xdfff;
const isHangulSyllable = (cp) => cp >= S_BASE && cp < S_BASE + S_COUNT;
const chr = (cp) => String.fromCodePoint(cp);
const cps = (s) => [...s].map((c) => c.codePointAt(0));
const NFD = (s) => s.normalize("NFD");
const NFC = (s) => s.normalize("NFC");

function* scalars() {
  for (let cp = 0; cp < 0x110000; cp++) if (!isSurrogate(cp)) yield cp;
}

/** Inclusive ranges of the code points for which `test` holds. */
function rangesOf(test) {
  const out = [];
  let start = -1;
  for (let cp = 0; cp <= 0x110000; cp++) {
    const hit = cp < 0x110000 && !isSurrogate(cp) && test(cp);
    if (hit && start < 0) start = cp;
    if (!hit && start >= 0) {
      out.push([start, cp - 1]);
      start = -1;
    }
  }
  return out;
}
const regexRanges = (re) => rangesOf((cp) => re.test(chr(cp)));

/** toLowerCase() of each code point that changes, as in rust/scripts/gen_data.mjs. */
function lowercase() {
  const runs = [];
  const multi = [];
  let run = null;
  for (const cp of scalars()) {
    const lower = cps(chr(cp).toLowerCase());
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

/** The normalization tables (see the header comment for the method). */
function normalization() {
  const decomp = []; // [cp, [full NFD]] for non-Hangul code points that decompose
  const stable = []; // code points with NFD(c) == c
  for (const cp of scalars()) {
    if (isHangulSyllable(cp)) continue;
    const s = chr(cp);
    const d = NFD(s);
    if (d !== s) decomp.push([cp, cps(d)]);
    else stable.push(cp);
  }

  // Combining class ranks.
  const P1 = "\u0334";
  const P240 = "\u0345";
  if (NFD(P240 + P1) !== P1 + P240) throw new Error("probe order: expected ccc(U+0334) < ccc(U+0345)");
  const nonStarters = stable.filter((cp) => {
    const s = chr(cp);
    return NFD(s + P1) === P1 + s || NFD(P240 + s) === s + P240;
  });
  // cmp < 0 when a has the smaller class.
  const cmp = (a, b) => {
    const sa = chr(a);
    const sb = chr(b);
    if (NFD(sb + sa) === sa + sb) return -1;
    if (NFD(sa + sb) === sb + sa) return 1;
    return 0;
  };
  const sorted = [...nonStarters].sort((a, b) => cmp(a, b) || a - b);
  const rank = new Map();
  let r = 0;
  for (let i = 0; i < sorted.length; i++) {
    if (i === 0 || cmp(sorted[i - 1], sorted[i]) !== 0) r++;
    rank.set(sorted[i], r);
  }
  if (r > 255) throw new Error("more than 255 classes");
  if (rank.get(0x334) !== 1 || rank.get(0x345) !== r) throw new Error("probes are not the extreme classes");
  const ccc = (cp) => rank.get(cp) ?? 0;
  const cccRanges = [];
  for (const cp of [...rank.keys()].sort((a, b) => a - b)) {
    const last = cccRanges[cccRanges.length - 1];
    if (last && last[1] === cp - 1 && last[2] === ccc(cp)) last[1] = cp;
    else cccRanges.push([cp, cp, ccc(cp)]);
  }

  // Composition pairs and the NFC quick-check sets.
  const nfcNo = new Set();
  const pairs = [];
  for (const cp of scalars()) if (NFC(chr(cp)) !== chr(cp)) nfcNo.add(cp);
  for (const [cp, d] of decomp) {
    if (nfcNo.has(cp)) continue;
    // A decomposable code point that NFC keeps starts with a starter: the quick check gives it
    // class 0.
    if (ccc(d[0]) !== 0) throw new Error(`U+${cp.toString(16)}: NFC-stable but starts with a non-starter`);
    const last = d[d.length - 1];
    const first = cps(NFC(String.fromCodePoint(...d.slice(0, -1))));
    if (first.length !== 1 || NFC(chr(first[0]) + chr(last)) !== chr(cp)) {
      throw new Error(`U+${cp.toString(16)}: no composition pair`);
    }
    pairs.push([first[0], last, cp]);
  }
  pairs.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  const second = new Set(pairs.map((p) => p[1]));
  for (let v = 0x1161; v <= 0x1175; v++) second.add(v); // Hangul V and T (algorithmic)
  for (let t = 0x11a8; t <= 0x11c2; t++) second.add(t);

  const quickMin = Math.min(decomp[0][0], cccRanges[0][0], ...nfcNo, ...second);
  return {
    decomp,
    cccRanges,
    rank,
    nonStarters: sorted.sort((a, b) => a - b),
    pairs,
    nfcNo: rangesOf((cp) => nfcNo.has(cp)),
    second: rangesOf((cp) => second.has(cp)),
    quickMin,
  };
}

/** A Go string literal, ASCII only. */
function goStr(arr) {
  let out = '"';
  for (const cp of arr) {
    if (cp === 0x22 || cp === 0x5c) out += `\\${String.fromCharCode(cp)}`;
    else if (cp >= 0x20 && cp < 0x7f) out += String.fromCharCode(cp);
    else if (cp <= 0xffff) out += `\\u${cp.toString(16).padStart(4, "0")}`;
    else out += `\\U${cp.toString(16).padStart(8, "0")}`;
  }
  return `${out}"`;
}
const hex = (n) => `0x${n.toString(16)}`;

/** Composite literal elements, `perLine` to a line. */
function rows(items, perLine) {
  const lines = [];
  for (let i = 0; i < items.length; i += perLine) lines.push(`\t${items.slice(i, i + perLine).join(", ")},`);
  return lines.join("\n");
}

const HEADER_UNICODE = `// Unicode ${UNICODE}`;
function renderGo(t) {
  const ranges = (name, doc, rs) =>
    `// ${name}: ${doc}\nvar ${name} = []uniRange{\n${rows(
      rs.map(([a, b]) => `{${hex(a)}, ${hex(b)}}`),
      6,
    )}\n}\n`;
  const { runs, multi } = t.lower;
  const n = t.norm;
  return [
    "// Code generated by scripts/gen_unicode.mjs; DO NOT EDIT.",
    "// Run `node scripts/gen_unicode.mjs` in go/ to regenerate.",
    "//",
    "// Tables taken from the JS reference engine (Node), so the Unicode version is Node's and not",
    "// the Go toolchain's: categories L and Lu (R0.6), toLowerCase() (R0.4) and NFC/NFD (R1, R0.4).",
    "// The method behind the combining class ranks is described in scripts/gen_unicode.mjs.",
    HEADER_UNICODE,
    "",
    "package trackparse",
    "",
    `// unicodeVersion is the Unicode version of Node's tables these were generated from.`,
    `const unicodeVersion = ${goStr(cps(UNICODE))}`,
    "",
    ranges("uniLetter", "general category L (Lu, Ll, Lt, Lm, Lo), JS /\\p{L}/u.", t.letter),
    ranges("uniUpper", "general category Lu, JS /\\p{Lu}/u.", t.upper),
    ranges("uniCased", "property Cased, read by the final-sigma rule.", t.cased),
    ranges("uniCaseIgnorable", "property Case_Ignorable, read by the final-sigma rule.", t.ignorable),
    "// uniLower: JS toLowerCase() of one code point. Every first + k*step <= last maps to itself",
    "// plus delta. Sorted by first, no overlaps.",
    `var uniLower = []lowerRun{\n${rows(
      runs.map((r) => `{${hex(r.first)}, ${hex(r.last)}, ${r.step}, ${r.delta}}`),
      4,
    )}\n}\n`,
    "// uniLowerMulti: code points whose lowercase is several code points. Sorted.",
    `var uniLowerMulti = []lowerMulti{\n${rows(
      multi.map(([cp, to]) => `{${hex(cp)}, ${goStr(to)}}`),
      4,
    )}\n}\n`,
    "// uniCCC: canonical combining class ranks of the code points that are their own NFD; 0 when",
    "// absent. Ranks keep the order and equality of the classes, which is all normalization uses.",
    `var uniCCC = []cccRange{\n${rows(
      n.cccRanges.map(([a, b, c]) => `{${hex(a)}, ${hex(b)}, ${c}}`),
      5,
    )}\n}\n`,
    "// uniDecomp: full canonical decomposition (NFD) of every code point that has one, except the",
    "// Hangul syllables. Sorted by code point.",
    `var uniDecomp = []decompEntry{\n${rows(
      n.decomp.map(([cp, d]) => `{${hex(cp)}, ${goStr(d)}}`),
      4,
    )}\n}\n`,
    "// uniCompose: canonical composition pairs (first, second) -> primary composite, without the",
    "// Hangul syllables and the composition exclusions. Sorted by first, then second.",
    `var uniCompose = []composePair{\n${rows(
      n.pairs.map(([a, b, c]) => `{${hex(a)}, ${hex(b)}, ${hex(c)}}`),
      4,
    )}\n}\n`,
    ranges("uniNFCNo", "code points that NFC changes on their own (NFC_Quick_Check=No).", n.nfcNo),
    ranges(
      "uniComposeSecond",
      "code points that can compose with what precedes them (NFC_Quick_Check=Maybe).",
      n.second,
    ),
    "// uniQuickMin: every code point below it is a starter that neither normalization form changes",
    "// and that never composes with what precedes it.",
    `const uniQuickMin = ${hex(n.quickMin)}`,
    "",
  ].join("\n");
}

function tables() {
  return {
    letter: regexRanges(/^\p{L}$/u),
    upper: regexRanges(/^\p{Lu}$/u),
    cased: regexRanges(/^\p{Cased}$/u),
    ignorable: regexRanges(/^\p{Case_Ignorable}$/u),
    lower: lowercase(),
    norm: normalization(),
  };
}

// ---------------------------------------------------------------------------
// Test cases: one JSON object per line, {"g": group, "s": input, "c": NFC, "d": NFD,
// "l": toLowerCase, "k": category bits (1 = \p{L}, 2 = \p{Lu}, single code points only)}.
// "c", "d" and "l" are left out when equal to "s".

/** mulberry32: a small seeded PRNG, so the committed sample is reproducible. */
function prng(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function caseLine(g, s, single) {
  const o = { g, s };
  const c = NFC(s);
  const d = NFD(s);
  const l = s.toLowerCase();
  if (c !== s) o.c = c;
  if (d !== s) o.d = d;
  if (l !== s) o.l = l;
  if (single) o.k = (/^\p{L}$/u.test(s) ? 1 : 0) | (/^\p{Lu}$/u.test(s) ? 2 : 0);
  return JSON.stringify(o);
}

function* cases(t, full) {
  const rnd = prng(0x7a11 + (full ? 1 : 0));
  const pick = (arr) => arr[Math.floor(rnd() * arr.length)];
  const int = (lo, hi) => lo + Math.floor(rnd() * (hi - lo + 1));
  const n = t.norm;
  const marks = n.nonStarters;
  const decompCps = n.decomp.map((e) => e[0]);
  const lowerCps = [...t.lower.runs.flatMap((r) => [r.first, r.last]), ...t.lower.multi.map((m) => m[0])];

  // Single code points: all of them, or the interesting ones plus a stride.
  if (full) {
    for (const cp of scalars()) yield caseLine("single", chr(cp), true);
  } else {
    const set = new Set([...decompCps, ...marks, ...lowerCps, ...n.pairs.map((p) => p[1])]);
    for (const [a, b] of [...t.letter, ...t.upper, ...n.nfcNo, ...t.cased, ...t.ignorable]) {
      set.add(a);
      set.add(b);
    }
    for (const cp of scalars()) if (cp % 499 === 0) set.add(cp);
    for (let cp = S_BASE; cp < S_BASE + S_COUNT; cp += 97) set.add(cp);
    for (const cp of [...set].sort((a, b) => a - b)) if (!isSurrogate(cp)) yield caseLine("single", chr(cp), true);
  }

  // Ordered pairs of non-starters, bare and after a starter.
  const bases = ["a", "e", "o", "A", "u", "\u03b1", "\u03c9", "\u0438", "\u0b47", "\u1100", "\u05d0", "\u0627", "\u3099"];
  if (full) {
    let i = 0;
    for (const x of marks) {
      for (const y of marks) {
        const xy = chr(x) + chr(y);
        yield caseLine("pair", xy);
        yield caseLine("pair+base", bases[i++ % bases.length] + xy);
      }
    }
  } else {
    for (let i = 0; i < 3000; i++) yield caseLine("pair+base", pick(bases) + chr(pick(marks)) + chr(pick(marks)));
  }

  // Composition: every pair, blocked and unblocked by a mark in between, and the composite plus
  // marks.
  for (const [a, b, c] of n.pairs) {
    yield caseLine("compose", chr(a) + chr(b));
    yield caseLine("compose", chr(a) + chr(pick(marks)) + chr(b));
    yield caseLine("compose", chr(c) + chr(pick(marks)));
  }

  // Starter + mark + mark triples (starters that decompose or compose are the interesting ones).
  const starters = [...decompCps.filter((cp) => !n.nfcNo.some(([lo, hi]) => cp >= lo && cp <= hi)), ...n.pairs.map((p) => p[0])];
  const triples = full ? 200000 : 2000;
  for (let i = 0; i < triples; i++) yield caseLine("triple", chr(pick(starters)) + chr(pick(marks)) + chr(pick(marks)));

  // Hangul: L+V, LV+T for the whole conjoining jamo blocks, syllables followed by T, and longer
  // jamo runs.
  const L = [];
  const V = [];
  const T = [];
  for (let cp = 0x1100; cp <= 0x115f; cp++) L.push(cp);
  for (let cp = 0x1160; cp <= 0x11a7; cp++) V.push(cp);
  for (let cp = 0x11a8; cp <= 0x11ff; cp++) T.push(cp);
  for (let cp = 0xa960; cp <= 0xa97c; cp++) L.push(cp);
  for (let cp = 0xd7b0; cp <= 0xd7c6; cp++) V.push(cp);
  for (let cp = 0xd7cb; cp <= 0xd7fb; cp++) T.push(cp);
  T.push(0x11a7); // T base itself must not compose
  const lv = [];
  for (let s = S_BASE; s < S_BASE + S_COUNT; s += 28) lv.push(s);
  if (full) {
    for (const l of L) for (const v of V) yield caseLine("hangul", chr(l) + chr(v));
    for (const s of lv) for (const t2 of T) yield caseLine("hangul", chr(s) + chr(t2));
    for (let s = S_BASE; s < S_BASE + S_COUNT; s++) yield caseLine("hangul", chr(s) + chr(pick(T)));
  } else {
    for (let i = 0; i < 800; i++) yield caseLine("hangul", chr(pick(L)) + chr(pick(V)));
    for (let i = 0; i < 800; i++) yield caseLine("hangul", chr(pick(lv)) + chr(pick(T)));
  }
  const jamo = [...L, ...V, ...T, 0x302e, 0x0301];
  for (let i = 0; i < (full ? 50000 : 500); i++) {
    let s = "";
    for (let k = int(2, 6); k > 0; k--) s += chr(pick(jamo));
    yield caseLine("hangul", s);
  }

  // Final sigma.
  const sigma = [
    "\u03a3", "A\u03a3", "\u03a3A", "A\u03a3A", "\u039f\u0394\u039f\u03a3", "\u039f\u0394\u039f\u03a3 A",
    "A\u03a3.", "A.\u03a3", "A\u03a3.A", "A\u03a3'", "A'\u03a3", "A\u03a3\u0301", "A\u0301\u03a3",
    "A\u03a3\u0345", "A\u0345\u03a3", "\u03a3\u03a3", "A\u03a3\u03a3", "A\u03a3\u03a3A", "1\u03a3",
    "A1\u03a3", "\u02b0\u03a3", "A\u02b0\u03a3", "A\u03a3\u02b0", "A\u00ad\u03a3", "A\u03a3\u00ad",
    "A\u03a3\u00adB", "\u0130\u03a3", "a\u03a3 b\u03a3", "\u1f00\u03a3", "\u24b6\u03a3", "A\u03a3\u24b6",
    "\ud801\udc00\u03a3", "A\u200d\u03a3", "A\u03a3\u200d", "A\u03a3\ufe0f", "A:\u03a3:", "A\u03a3:B",
  ];
  for (const s of sigma) yield caseLine("sigma", s);
  const sigmaPool = ["\u03a3", "\u03a3", "A", "a", "1", " ", ".", "'", ":", "\u0301", "\u0345", "\u02b0", "\u00ad", "\u24b6", "\u0130", "\u200d"];
  for (let i = 0; i < (full ? 50000 : 1000); i++) {
    let s = "";
    for (let k = int(1, 8); k > 0; k--) s += pick(sigmaPool);
    yield caseLine("sigma", s);
  }

  // Random strings.
  const span = (lo, hi) => () => int(lo, hi);
  const pools = [
    span(0x20, 0x7e), span(0x20, 0x7e), span(0x20, 0x7e), span(0x9, 0xd),
    span(0xa0, 0x24f), span(0x1e00, 0x1eff),
    span(0x370, 0x3ff), span(0x1f00, 0x1fff), () => 0x3a3,
    span(0x400, 0x52f),
    span(S_BASE, S_BASE + S_COUNT - 1), span(0x1100, 0x11ff), span(0x3130, 0x318f),
    span(0x300, 0x36f), span(0x300, 0x36f), () => pick(marks), () => pick(marks),
    span(0x1f300, 0x1faff), () => 0x200d, () => 0xfe0f, span(0x1f3fb, 0x1f3ff),
    span(0x4e00, 0x9fff), span(0xf900, 0xfaff), span(0x2f800, 0x2fa1f), span(0x3040, 0x30ff),
    () => pick(decompCps), () => pick(lowerCps), () => pick(starters),
  ];
  for (let i = 0; i < (full ? 200000 : 3000); i++) {
    const cpsOut = [];
    for (let k = int(1, 24); k > 0; k--) {
      const cp = pick(pools)();
      if (!isSurrogate(cp)) cpsOut.push(cp);
    }
    yield caseLine("random", String.fromCodePoint(...cpsOut));
  }
}

// ---------------------------------------------------------------------------

const args = process.argv.slice(2);
const casesAt = args.indexOf("--cases");
if (casesAt >= 0) {
  const dir = args[casesAt + 1];
  if (!dir) throw new Error("--cases needs a directory");
  mkdirSync(dir, { recursive: true });
  const lines = [];
  let bytes = 0;
  const path = join(dir, "cases.jsonl");
  writeFileSync(path, "");
  for (const line of cases(tables(), true)) {
    lines.push(line);
    bytes += line.length;
    if (bytes > 1 << 22) {
      writeFileSync(path, `${lines.join("\n")}\n`, { flag: "a" });
      lines.length = 0;
      bytes = 0;
    }
  }
  writeFileSync(path, `${lines.join("\n")}\n`, { flag: "a" });
  writeFileSync(join(dir, "unicode-version.txt"), `${UNICODE}\n`);
  console.log(`wrote ${path}`);
} else {
  const check = args.includes("--check");
  const t = tables();
  let stale = false;
  const emit = (rel, content) => {
    const path = join(goDir, rel);
    let current = "";
    try {
      current = readFileSync(path, "utf8");
    } catch {}
    if (check) {
      // The tables depend on Node's Unicode version: only compare like with like.
      let gen = "";
      try {
        gen = readFileSync(join(goDir, "unicode_gen.go"), "utf8");
      } catch {}
      const other = gen.match(/^\/\/ Unicode (\S+)$/m);
      if (other && other[1] !== UNICODE) {
        console.log(`${rel}: skipped (Unicode ${UNICODE} here)`);
        return;
      }
      if (current !== content) {
        console.error(`${rel} is stale: run node scripts/gen_unicode.mjs`);
        stale = true;
      }
    } else if (current !== content) {
      mkdirSync(dirname(path), { recursive: true });
      writeFileSync(path, content);
      console.log(`wrote ${path}`);
    }
  };
  emit("unicode_gen.go", renderGo(t));
  emit("testdata/unicode/cases.jsonl", `${[...cases(t, false)].join("\n")}\n`);
  if (stale) process.exit(1);
}
