/**
 * Usage: tsx src/cli.ts gen     write spec/fixtures/generated/*.json
 *        tsx src/cli.ts check   regenerate in memory and byte-compare; exit 1 on drift
 */
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { join, relative } from "node:path";
import { GENERATED_DIR, SPEC_ROOT } from "./data.js";
import { generateAll } from "./write.js";

function onDisk(): string[] {
  return existsSync(GENERATED_DIR) ? readdirSync(GENERATED_DIR).filter((f) => f.endsWith(".json")).sort() : [];
}

function main(cmd: string | undefined): number {
  const t0 = Date.now();
  const { results, files } = generateAll();
  const ms = Date.now() - t0;
  const total = results.reduce((a, r) => a + r.cases.length, 0);

  if (cmd === "gen") {
    mkdirSync(GENERATED_DIR, { recursive: true });
    for (const f of onDisk()) if (!files.has(f)) rmSync(join(GENERATED_DIR, f));
    let bytes = 0;
    for (const [name, content] of files) {
      writeFileSync(join(GENERATED_DIR, name), content);
      bytes += Buffer.byteLength(content);
    }
    for (const r of results) {
      const s = r.stats;
      console.log(
        `${r.name.padEnd(16)} ${String(r.cases.length).padStart(6)} cases  (${s.strategy}; product ${s.productSize}, pairwise ${s.pairwiseCases}, sampled ${s.sampledCases}, infeasible pairs ${s.infeasiblePairs}, dup inputs ${s.duplicateInputsDropped})`,
      );
    }
    console.log(`total ${total} cases, ${(bytes / 1e6).toFixed(2)} MB → ${relative(SPEC_ROOT, GENERATED_DIR)} (${ms} ms)`);
    return 0;
  }

  if (cmd === "check") {
    const drift: string[] = [];
    for (const [name, content] of files) {
      const p = join(GENERATED_DIR, name);
      if (!existsSync(p)) drift.push(`${name}: missing`);
      else if (readFileSync(p, "utf8") !== content) drift.push(`${name}: differs`);
    }
    for (const f of onDisk()) if (!files.has(f)) drift.push(`${f}: stale (not produced by the generator)`);
    if (drift.length) {
      console.error("Generated fixtures are out of date (run `pnpm gen:fixtures`):");
      for (const d of drift) console.error(`  ${d}`);
      return 1;
    }
    console.log(`generated fixtures up to date (${files.size} files, ${total} cases, ${ms} ms)`);
    return 0;
  }

  console.error("usage: cli.ts <gen|check>");
  return 2;
}

process.exit(main(process.argv[2]));
