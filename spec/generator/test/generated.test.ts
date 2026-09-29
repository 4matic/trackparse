import { readFileSync } from "node:fs";
import { join } from "node:path";
import { beforeAll, describe, expect, it } from "vitest";
import { SPEC_ROOT } from "../src/data.js";
import { loadConfig } from "../src/scenario.js";
import { type Generated, generateAll } from "../src/write.js";

let first: Generated;
let second: Generated;

beforeAll(() => {
  first = generateAll(loadConfig());
  second = generateAll(loadConfig());
});

interface FixtureCase {
  id: string;
  input: string;
  /** Effective options: defaults.options merged with the case's own. */
  options: { mode: string };
  expected: Record<string, unknown>;
  rules: string[];
}

type ExpArtist = string | { name: string; role: string; joiner: string | null; source: string };
const nameOf = (a: ExpArtist): string => (typeof a === "string" ? a : a.name);

function allCases(g: Generated): FixtureCase[] {
  const out: FixtureCase[] = [];
  for (const [name, content] of g.files) {
    if (name === "summary.json") continue;
    const file = JSON.parse(content) as { defaults: { options: object }; cases: FixtureCase[] };
    for (const c of file.cases) out.push({ ...c, options: { ...file.defaults.options, ...c.options } as { mode: string } });
  }
  return out;
}

describe("generated fixtures", () => {
  it("are deterministic (two runs byte-identical)", () => {
    expect([...first.files.keys()]).toEqual([...second.files.keys()]);
    for (const [name, content] of first.files) expect(second.files.get(name), name).toBe(content);
  });

  it("have the target size (cases and bytes)", () => {
    const n = allCases(first).length;
    expect(n).toBeGreaterThanOrEqual(8000);
    expect(n).toBeLessThanOrEqual(15000);
    let bytes = 0;
    for (const content of first.files.values()) bytes += Buffer.byteLength(content);
    expect(bytes).toBeLessThanOrEqual(6.5e6);
  });

  it("have unique, schema-shaped ids and non-empty inputs", () => {
    const cases = allCases(first);
    const ids = new Set<string>();
    for (const c of cases) {
      expect(c.id).toMatch(/^gen:[a-z-]+:[a-z0-9:._-]+$/);
      expect(ids.has(c.id), c.id).toBe(false);
      ids.add(c.id);
      expect(c.input.trim().length, c.id).toBeGreaterThan(0);
      expect(["clean", "youtube", "filename", "auto"]).toContain(c.options.mode);
      for (const r of c.rules) expect(r).toMatch(/^R[0-9]+(\.[0-9]+[a-z]?)*$/);
    }
  });

  it("cases are sorted by id and serialized one per line", () => {
    for (const [name, content] of first.files) {
      if (name === "summary.json") continue;
      const lines = content.split("\n");
      const caseLines = lines.filter((l) => l.startsWith('    {"id":'));
      const parsed = JSON.parse(content) as { cases: FixtureCase[] };
      expect(caseLines.length).toBe(parsed.cases.length);
      const ids = parsed.cases.map((c) => c.id);
      expect(ids).toEqual([...ids].sort());
      expect(content.endsWith("}\n")).toBe(true);
    }
  });

  it("expected artists are well-formed and never duplicated (R9.2 invariant)", () => {
    for (const c of allCases(first)) {
      const artists = c.expected.artists as ExpArtist[];
      // R9.2: producers are a separate class, deduped only among producers.
      const keys = artists.map(
        (a) =>
          `${typeof a !== "string" && a.role === "producer" ? "p" : "c"}:` +
          nameOf(a).toLowerCase().normalize("NFD").replace(/[\u0300-\u036F]/g, ""),
      );
      expect(new Set(keys).size, c.id).toBe(keys.length);
      for (const a of artists) {
        expect(nameOf(a).length).toBeGreaterThan(0);
        expect(nameOf(a)).toBe(nameOf(a).trim());
        // The string shorthand is used only for the first primary (joiner null, source artist).
        if (typeof a !== "string") {
          expect(["primary", "featured", "producer"]).toContain(a.role);
          expect(a.role === "primary" && a.joiner === null && a.source === "artist", c.id).toBe(false);
        }
      }
      if (artists.length) expect(typeof artists[0] === "string" || artists[0]!.role !== "primary").toBe(true);
    }
  });

  it("fullTitle is title + versions (R9.4)", () => {
    for (const c of allCases(first)) {
      const e = c.expected as { title: string; fullTitle?: string; versions: { raw: string; delimiter: string }[] };
      // fullTitle is serialized only when there are versions (otherwise it equals title).
      if (e.versions.length === 0) {
        expect(e.fullTitle, c.id).toBeUndefined();
        continue;
      }
      const close: Record<string, string> = { "(": ")", "[": "]", "{": "}" };
      const rebuilt =
        e.title +
        e.versions.map((v) => (v.delimiter === "-" ? ` - ${v.raw}` : ` ${v.delimiter}${v.raw}${close[v.delimiter]}`)).join("");
      expect(e.fullTitle, c.id).toBe(rebuilt);
    }
  });

  it("files on disk are up to date (same check as `pnpm check`)", () => {
    for (const [name, content] of first.files) {
      const disk = readFileSync(join(SPEC_ROOT, "fixtures", "generated", name), "utf8");
      expect(disk === content, `${name} is stale; run pnpm gen`).toBe(true);
    }
  });

  it("spot-check rendered cases against hand-computed expectations", () => {
    const byId = new Map(allCases(first).map((c) => [c.id, c]));
    const find = (pred: (c: FixtureCase) => boolean) => allCases(first).find(pred);

    // numeric artist with no prefix: R4.2(d) never a position
    const numeric = find((c) => c.input.startsWith("808 State - ") && c.options.mode === "clean");
    if (numeric) expect(numeric.expected.position).toBeNull();

    // every `(with <x>)` case keeps the marker as joiner
    const withCase = find((c) => / \(with [^)]+\)/.test(c.input) && c.id.startsWith("gen:feat-placement:"));
    expect(withCase).toBeDefined();
    const featured = (withCase!.expected.artists as ExpArtist[]).find((a) => typeof a !== "string" && a.role === "featured");
    expect(typeof featured === "string" ? null : featured?.joiner).toBe("with");

    // platform junk is always last
    for (const c of allCases(first)) {
      const junk = (c.expected.junk ?? []) as { kind: string }[];
      const idx = junk.findIndex((j) => j.kind === "platform");
      if (idx >= 0) expect(idx, c.id).toBe(junk.length - 1);
    }
    expect(byId.size).toBe(allCases(first).length);
  });
});
