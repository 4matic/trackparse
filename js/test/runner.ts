/** Loads and runs the shared fixtures (spec/fixtures). Used by the vitest suite and conformance. */
import { readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { format, normalize, type ParsedTrack, parse, parseArtists } from "../src/index.js";
import type { FormatOptions, ParseOptions } from "../src/types.js";
import { match } from "./match.js";

const here = dirname(fileURLToPath(import.meta.url));
export const specDir = resolve(here, "../../spec");
export const fixturesDir = join(specDir, "fixtures");

export type Fn = "parse" | "parseArtists" | "format" | "normalize";

export interface FixtureCase {
  id: string;
  fn?: Fn;
  input: string;
  options?: ParseOptions;
  formatOptions?: FormatOptions;
  expected?: unknown;
  expectedAny?: unknown[];
  match?: "partial" | "exact";
  status?: "pass" | "xfail" | "ambiguous";
  rules?: string[];
  tags?: string[];
  note?: string;
}

export interface FixtureFile {
  description: string;
  defaults?: { fn?: Fn; options?: ParseOptions; formatOptions?: FormatOptions };
  cases: FixtureCase[];
}

export interface LoadedFile {
  /** Path relative to spec/fixtures, e.g. `handwritten/brackets.json`. */
  name: string;
  generated: boolean;
  data: FixtureFile;
}

function walk(dir: string): string[] {
  let entries: string[];
  try {
    entries = readdirSync(dir);
  } catch {
    return [];
  }
  const out: string[] = [];
  for (const entry of entries.sort()) {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) out.push(...walk(full));
    else if (entry.endsWith(".json")) out.push(full);
  }
  return out;
}

/** Fixture files (JSON files with a `cases` array; metadata such as generated/summary.json is skipped). */
export function loadFixtureFiles(): LoadedFile[] {
  const files = walk(fixturesDir).map((full) => {
    const name = relative(fixturesDir, full).split("\\").join("/");
    return {
      name,
      generated: name.startsWith("generated/"),
      data: JSON.parse(readFileSync(full, "utf8")) as FixtureFile,
    };
  });
  return files.filter((f) => Array.isArray((f.data as { cases?: unknown }).cases));
}

export function loadSkipList(): Record<string, string> {
  return JSON.parse(readFileSync(join(here, "skip.json"), "utf8")) as Record<string, string>;
}

export interface CaseRun {
  fn: Fn;
  /** Every ParsedTrack produced while running the case (for schema validation). */
  parsed: ParsedTrack[];
  actual: unknown;
  /** null when the case behaves as its status demands, else a failure description. */
  failure: string | null;
  /** Raw mismatch (null if the output matches the expectation). */
  mismatch: string | null;
}

export function runCase(file: FixtureFile, c: FixtureCase): CaseRun {
  const fn: Fn = c.fn ?? file.defaults?.fn ?? "parse";
  const options: ParseOptions = { ...file.defaults?.options, ...c.options };
  const formatOptions: FormatOptions = { ...file.defaults?.formatOptions, ...c.formatOptions };
  const parsed: ParsedTrack[] = [];
  let actual: unknown;
  try {
    switch (fn) {
      case "parse": {
        const r = parse(c.input, options);
        parsed.push(r);
        actual = r;
        break;
      }
      case "parseArtists":
        actual = parseArtists(c.input, options);
        break;
      case "format": {
        const r = parse(c.input, options);
        parsed.push(r);
        actual = format(r, formatOptions);
        break;
      }
      case "normalize":
        actual = normalize(c.input);
        break;
    }
  } catch (err) {
    const msg = `threw: ${err instanceof Error ? (err.stack ?? err.message) : String(err)}`;
    return { fn, parsed, actual: undefined, failure: msg, mismatch: msg };
  }

  const status = c.status ?? "pass";
  const mode = c.match ?? "partial";
  let mismatch: string | null;
  if (status === "ambiguous") {
    const options = c.expectedAny ?? [];
    const diffs = options.map((e) => match(e, actual, mode));
    mismatch = diffs.some((d) => d === null)
      ? null
      : `no expectedAny entry matched: ${diffs.join(" | ")}`;
  } else {
    mismatch = match(c.expected, actual, mode);
  }
  let failure = mismatch;
  if (status === "xfail") failure = mismatch === null ? "xfail case unexpectedly passes" : null;
  return { fn, parsed, actual, failure, mismatch };
}
