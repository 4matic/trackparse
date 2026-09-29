/**
 * Compiles spec/data/*.json (and spec/VERSION) into src/data.generated.ts.
 *
 *   tsx scripts/gen-data.ts          write the file
 *   tsx scripts/gen-data.ts --check  exit 1 if the committed file is stale
 */
import { readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const specDir = resolve(here, "../../spec");
const dataDir = join(specDir, "data");
const outFile = resolve(here, "../src/data.generated.ts");

function camelCase(stem: string): string {
  return stem.replace(/-([a-z])/g, (_m, c: string) => c.toUpperCase());
}

function stripComments(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(stripComments);
  if (value !== null && typeof value === "object") {
    const out: Record<string, unknown> = {};
    for (const [k, v] of Object.entries(value)) {
      if (k === "$comment") continue;
      out[k] = stripComments(v);
    }
    return out;
  }
  return value;
}

function render(): string {
  const version = readFileSync(join(specDir, "VERSION"), "utf8").trim();
  const files = readdirSync(dataDir)
    .filter((f) => f.endsWith(".json"))
    .sort();
  const lines: string[] = [
    "// GENERATED from spec/data — do not edit. Run `pnpm gen:data` to regenerate.",
    "/* eslint-disable */",
    "",
    `export const SPEC_VERSION = ${JSON.stringify(version)}; // x-release-please-version`,
    "",
  ];
  for (const file of files) {
    const stem = file.slice(0, -".json".length);
    const data = stripComments(JSON.parse(readFileSync(join(dataDir, file), "utf8")));
    lines.push(`/** spec/data/${file} */`);
    lines.push(`export const ${camelCase(stem)}Data = ${JSON.stringify(data, null, 2)} as const;`);
    lines.push("");
  }
  return lines.join("\n");
}

const content = render();
if (process.argv.includes("--check")) {
  let current = "";
  try {
    current = readFileSync(outFile, "utf8");
  } catch {
    // missing file counts as stale
  }
  if (current !== content) {
    console.error("src/data.generated.ts is out of date. Run `pnpm gen:data`.");
    process.exit(1);
  }
  console.log("src/data.generated.ts is up to date.");
} else {
  writeFileSync(outFile, content);
  console.log(`wrote ${outFile}`);
}
