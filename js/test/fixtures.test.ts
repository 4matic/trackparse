/**
 * Runs every shared fixture in spec/fixtures (handwritten + generated).
 *
 * - Each file is validated against spec/schema/fixture-file.schema.json.
 * - Every ParsedTrack produced is validated against parsed-track.schema.json.
 * - status pass / xfail / ambiguous per spec/fixtures/README.md.
 * - js/test/skip.json lists port-specific known failures; a skipped case that starts
 *   passing fails the run, so the list can only shrink.
 */
import { readFileSync } from "node:fs";
import { join } from "node:path";
import Ajv2020 from "ajv/dist/2020.js";
import { describe, expect, it } from "vitest";
import {
  type CaseRun,
  type FixtureCase,
  type LoadedFile,
  loadFixtureFiles,
  loadSkipList,
  runCase,
  specDir,
} from "./runner.js";

const ajv = new Ajv2020({ allErrors: true, strict: false });
const loadSchema = (name: string) =>
  JSON.parse(readFileSync(join(specDir, "schema", name), "utf8")) as object;
const validateFixtureFile = ajv.compile(loadSchema("fixture-file.schema.json"));
const validateTrack = ajv.compile(loadSchema("parsed-track.schema.json"));

const files = loadFixtureFiles();
const skip = loadSkipList();

/** Everything wrong with one case, or null. Skip-listed cases are inverted. */
function problemOf(c: FixtureCase, run: CaseRun): string | null {
  const schemaErrors: string[] = [];
  for (const track of run.parsed) {
    if (!validateTrack(track)) schemaErrors.push(ajv.errorsText(validateTrack.errors));
  }
  const problem =
    schemaErrors.length > 0
      ? `output violates parsed-track schema: ${schemaErrors.join("; ")}`
      : run.failure;
  const reason = skip[c.id];
  if (reason !== undefined) {
    return problem === null
      ? `skip-listed (${reason}) but now passes: remove it from test/skip.json`
      : null;
  }
  return problem;
}

function describeFailure(c: FixtureCase, problem: string): string {
  return `${c.id}\n  input: ${JSON.stringify(c.input)}\n  ${problem}`;
}

function registerFile(file: LoadedFile) {
  describe(file.name, () => {
    it("validates against fixture-file.schema.json", () => {
      const ok = validateFixtureFile(file.data);
      expect(ok, ajv.errorsText(validateFixtureFile.errors)).toBe(true);
    });

    const cases = Array.isArray(file.data.cases) ? file.data.cases : [];
    if (file.generated) {
      // Thousands of cases: one test per file, collecting failures.
      it(`${cases.length} generated cases`, () => {
        const failures: string[] = [];
        for (const c of cases) {
          const problem = problemOf(c, runCase(file.data, c));
          if (problem !== null) failures.push(describeFailure(c, problem));
        }
        const head = failures.slice(0, 20).join("\n");
        expect(failures.length, `${failures.length} failing case(s); first ones:\n${head}`).toBe(0);
      });
      return;
    }

    for (const c of cases) {
      const skipped = skip[c.id] !== undefined;
      if (c.status === "xfail" && !skipped) {
        // Known-wrong expectation: the port must still fail it.
        it.fails(`${c.id} [xfail]`, () => {
          const run = runCase(file.data, c);
          if (run.mismatch !== null) throw new Error(run.mismatch);
        });
        continue;
      }
      it(skipped ? `${c.id} [skip]` : c.id, () => {
        const problem = problemOf(c, runCase(file.data, c));
        if (problem !== null) throw new Error(describeFailure(c, problem));
      });
    }
  });
}

for (const file of files) registerFile(file);

describe("skip.json", () => {
  it("lists only existing case ids", () => {
    const ids = new Set(files.flatMap((f) => (f.data.cases ?? []).map((c) => c.id)));
    const stale = Object.keys(skip).filter((id) => !ids.has(id));
    expect(stale, "stale skip.json entries").toEqual([]);
  });
  it("case ids are unique across all fixtures", () => {
    const seen = new Set<string>();
    const dupes: string[] = [];
    for (const f of files) {
      for (const c of f.data.cases ?? []) {
        if (seen.has(c.id)) dupes.push(c.id);
        seen.add(c.id);
      }
    }
    expect(dupes).toEqual([]);
  });
});
