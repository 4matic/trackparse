/**
 * Conformance report: pass/total per fixture file, per rule ID and overall.
 * A case "passes" when it behaves as its status demands (skip.json is ignored here).
 * Always exits 0.
 */
import { loadFixtureFiles, loadSkipList, runCase } from "../test/runner.js";

interface Tally {
  pass: number;
  total: number;
}

const bump = (map: Map<string, Tally>, key: string, ok: boolean) => {
  const t = map.get(key) ?? { pass: 0, total: 0 };
  t.total++;
  if (ok) t.pass++;
  map.set(key, t);
};

const pct = (t: Tally) => (t.total === 0 ? "-" : `${((100 * t.pass) / t.total).toFixed(1)}%`);
const row = (label: string, t: Tally, width: number) =>
  `  ${label.padEnd(width)} ${String(t.pass).padStart(6)} / ${String(t.total).padEnd(6)} ${pct(t)}`;

function compareRules(a: string, b: string): number {
  const pa = a.slice(1).split(".");
  const pb = b.slice(1).split(".");
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const x = pa[i] ?? "";
    const y = pb[i] ?? "";
    const d = Number.parseInt(x, 10) - Number.parseInt(y, 10);
    if (!Number.isNaN(d) && d !== 0) return d;
    if (x !== y) return x < y ? -1 : 1;
  }
  return 0;
}

const files = loadFixtureFiles();
const skip = loadSkipList();
const byFile = new Map<string, Tally>();
const byRule = new Map<string, Tally>();
const overall: Tally = { pass: 0, total: 0 };
let skipped = 0;

for (const file of files) {
  for (const c of file.data.cases ?? []) {
    const ok = runCase(file.data, c).failure === null;
    if (!ok && skip[c.id] !== undefined) skipped++;
    bump(byFile, file.name, ok);
    for (const rule of c.rules ?? []) bump(byRule, rule, ok);
    overall.total++;
    if (ok) overall.pass++;
  }
}

const fileWidth = Math.max(10, ...[...byFile.keys()].map((k) => k.length));
console.log("trackparse (js) conformance\n");
console.log("By file:");
for (const [name, t] of byFile) console.log(row(name, t, fileWidth));
console.log("\nBy rule:");
for (const rule of [...byRule.keys()].sort(compareRules)) {
  console.log(row(rule, byRule.get(rule) as Tally, 10));
}
console.log(`\nOverall: ${overall.pass} / ${overall.total} (${pct(overall)})`);
console.log(`Failing cases covered by test/skip.json: ${skipped}`);
