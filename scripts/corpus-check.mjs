#!/usr/bin/env node
/**
 * Checks parseTracklist (the BUILT js/dist/tracklist.js) against a private tracklist corpus.
 * The corpus is platform data and is never committed (spec/corpus/ is git-ignored).
 *
 * Usage: pnpm --filter trackparse build && node scripts/corpus-check.mjs spec/corpus/neuropunk-podcast.jsonl
 *
 * Input: JSONL, one object per list with `title` and `track_list` (the raw multi-line text).
 * Asserts, for every list and line:
 *   - lossless: entries' raw + eol joined give the input back (R13.1.2);
 *   - parts: lead + time + gap + rest === raw (R13.1.3);
 *   - seconds equal the neuropunk web regex's reading of the line (MM:SS = min:sec, H:MM:SS);
 *   - the bare positions of "Gydra 10 years mix" are found (81 lines, R13.5);
 *   - exactly 10 time-out-of-order warnings, one per list (R13.7);
 *   - exactly 2 of them carry a suggestion: Not Included 4 (its own line) and The Deepspace 10
 *     (the previous line, R13.7 step two).
 * Then reports the track lines that still warn line-unparsed. Exits 1 on a failed assertion.
 */
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

const path = process.argv[2];
if (!path) {
  console.error("usage: node scripts/corpus-check.mjs <corpus.jsonl>");
  process.exit(2);
}
const { parseTracklist } = await import(new URL("../js/dist/tracklist.js", import.meta.url).href);

// The neuropunk web regex, as used in production (unanchored, \s{1,3}, lazy 1–3 digit index).
const WEB =
  /(?<time>([0-9]{1,2}:[0-9]{2}(:[0-9]{2})?))\s{1,3}((?<index>([0-9]{1,3}?))[.)]\s?)?(?<title>.+)/i;
const webSeconds = (time) => {
  const p = time.split(":").map(Number);
  return p.length === 3 ? p[0] * 3600 + p[1] * 60 + p[2] : p[0] * 60 + p[1];
};

const EXPECTED = {
  gydraPositions: 81,
  outOfOrder: 10,
  // R13.7: "list|warning line|suggestion.line|suggestion.raw", sorted.
  suggestions: [
    "Neuropunk special Not Included 4|35|35|01:06:29",
    "The Deepspace 10|30|29|01:02:21",
  ],
};
const failures = [];
const fail = (msg) => {
  if (failures.length < 50) failures.push(msg);
  else if (failures.length === 50) failures.push("… more failures omitted");
};

const lists = readFileSync(resolve(path), "utf8")
  .split("\n")
  .filter((l) => l.trim().length > 0)
  .map((l) => JSON.parse(l));

let lineCount = 0;
let gydraPositions = 0;
const outOfOrder = [];
const unparsed = [];
const t0 = performance.now();
for (const list of lists) {
  const text = list.track_list ?? "";
  const tl = parseTracklist(text);
  lineCount += tl.entries.length;

  const joined = tl.entries.map((e) => e.raw + e.eol).join("");
  if (joined !== text) fail(`${list.title}: not lossless`);

  for (const e of tl.entries) {
    const where = `${list.title} line ${e.line}: ${JSON.stringify(e.raw)}`;
    const p = e.parts;
    if (p.lead + p.time + p.gap + p.rest !== e.raw) fail(`${where}: parts do not concatenate`);
    if (e.kind === "blank") continue;
    const m = WEB.exec(e.raw);
    if (!m) {
      fail(`${where}: web regex does not match`);
    } else if (!e.time || e.time.seconds !== webSeconds(m.groups.time)) {
      fail(`${where}: seconds ${e.time?.seconds} != web ${webSeconds(m.groups.time)}`);
    }
    if (list.title === "Gydra 10 years mix" && e.track?.position) gydraPositions++;
  }
  const ooo = tl.warnings.filter((w) => w.code === "time-out-of-order");
  for (const w of ooo) {
    const raw = tl.entries[w.line - 1].raw;
    const s = w.suggestion;
    outOfOrder.push({
      list: list.title,
      line: w.line,
      raw,
      suggestion: s ? `line ${s.line} ${s.raw}` : null,
      key: s ? `${list.title}|${w.line}|${s.line}|${s.raw}` : null,
    });
  }
  for (const w of tl.warnings) {
    if (w.code !== "line-unparsed") continue;
    unparsed.push({ list: list.title, line: w.line, raw: tl.entries[w.line - 1].raw });
  }
}
const ms = performance.now() - t0;

if (gydraPositions !== EXPECTED.gydraPositions) {
  fail(
    `Gydra 10 years mix: ${gydraPositions} bare positions found, expected ${EXPECTED.gydraPositions}`,
  );
}
const oooLists = new Set(outOfOrder.map((o) => o.list));
if (outOfOrder.length !== EXPECTED.outOfOrder || oooLists.size !== EXPECTED.outOfOrder) {
  fail(
    `time-out-of-order: ${outOfOrder.length} warnings in ${oooLists.size} lists, expected ${EXPECTED.outOfOrder} (one per list)`,
  );
}

const suggestions = outOfOrder
  .map((o) => o.key)
  .filter((k) => k !== null)
  .sort();
if (JSON.stringify(suggestions) !== JSON.stringify(EXPECTED.suggestions)) {
  fail(
    `suggestions: ${JSON.stringify(suggestions)}, expected ${JSON.stringify(EXPECTED.suggestions)}`,
  );
}

console.log(`corpus: ${lists.length} lists, ${lineCount} lines, parsed in ${ms.toFixed(0)} ms`);
console.log(`lossless + parts + web-regex seconds: checked on every line`);
console.log(`Gydra 10 years mix bare positions: ${gydraPositions}`);
console.log(`time-out-of-order: ${outOfOrder.length} (in ${oooLists.size} lists)`);
for (const o of outOfOrder) {
  console.log(
    `  ${o.list} line ${o.line}: ${JSON.stringify(o.raw)} suggestion ${o.suggestion ?? "-"}`,
  );
}
console.log(`line-unparsed track lines: ${unparsed.length}`);
for (const u of unparsed) console.log(`  ${u.list} line ${u.line}: ${JSON.stringify(u.raw)}`);

if (failures.length > 0) {
  console.log(`\nFAILED (${failures.length}):`);
  for (const f of failures) console.log(`  ${f}`);
  process.exit(1);
}
console.log("\nall corpus assertions pass");
