/**
 * Validate every fixture file under spec/fixtures/** (handwritten and generated) against
 * spec/schema/fixture-file.schema.json and check that case ids are unique repo-wide.
 * `generated/summary.json` is generator metadata, not a fixture file, and is skipped.
 * Exit code 1 on any failure.
 */
import { readFileSync, readdirSync, statSync } from "node:fs";
import { join, relative } from "node:path";
import Ajv2020 from "ajv/dist/2020.js";
import { FIXTURES_ROOT, GENERATED_DIR, SPEC_ROOT } from "./data.js";

function walk(dir: string): string[] {
  const out: string[] = [];
  for (const name of readdirSync(dir).sort()) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) out.push(...walk(p));
    else if (name.endsWith(".json")) out.push(p);
  }
  return out;
}

export interface ValidationReport {
  files: number;
  cases: number;
  errors: string[];
}

export function validateFixtures(): ValidationReport {
  const schema = JSON.parse(readFileSync(join(SPEC_ROOT, "schema", "fixture-file.schema.json"), "utf8"));
  const ajv = new Ajv2020({ allErrors: true, strict: false });
  const validate = ajv.compile(schema);
  const errors: string[] = [];
  const seen = new Map<string, string>();
  let files = 0;
  let cases = 0;
  for (const file of walk(FIXTURES_ROOT)) {
    if (file === join(GENERATED_DIR, "summary.json")) continue;
    const rel = relative(SPEC_ROOT, file);
    files++;
    let data: unknown;
    try {
      data = JSON.parse(readFileSync(file, "utf8"));
    } catch (e) {
      errors.push(`${rel}: invalid JSON (${(e as Error).message})`);
      continue;
    }
    if (!validate(data)) {
      for (const err of (validate.errors ?? []).slice(0, 20))
        errors.push(`${rel}: ${err.instancePath || "/"} ${err.message ?? ""}`);
      continue;
    }
    for (const c of (data as { cases: { id: string }[] }).cases) {
      cases++;
      const prev = seen.get(c.id);
      if (prev) errors.push(`${rel}: duplicate case id ${c.id} (also in ${prev})`);
      else seen.set(c.id, rel);
    }
  }
  return { files, cases, errors };
}

const isMain = process.argv[1] && import.meta.url.endsWith(process.argv[1].split("/").pop()!);
if (isMain) {
  const r = validateFixtures();
  if (r.errors.length) {
    console.error(`fixture validation FAILED (${r.errors.length} problems):`);
    for (const e of r.errors) console.error(`  ${e}`);
    process.exit(1);
  }
  console.log(`fixtures valid: ${r.files} files, ${r.cases} cases, all ids unique`);
}
