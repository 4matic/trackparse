/** trackparse/tracklist API surface (SPEC.md R13). */
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import * as root from "../src/index.js";
import { parse, SPEC_VERSION } from "../src/index.js";
import * as tracklist from "../src/tracklist/index.js";
import {
  applyEdits,
  type Edit,
  parseTracklist,
  type TracklistOptions,
} from "../src/tracklist/index.js";
import { specDir } from "./runner.js";

const pkg = JSON.parse(readFileSync(join(specDir, "..", "js", "package.json"), "utf8")) as {
  exports: Record<string, unknown>;
};

describe("subpath export trackparse/tracklist", () => {
  it("exports exactly parseTracklist and applyEdits at runtime", () => {
    expect(Object.keys(tracklist).sort()).toEqual(["applyEdits", "parseTracklist"]);
  });

  it("the root entry does not export the tracklist functions", () => {
    expect("parseTracklist" in root).toBe(false);
    expect("applyEdits" in root).toBe(false);
    expect("createListParser" in root).toBe(false);
  });

  it("package.json maps ./tracklist like . (types first)", () => {
    expect(pkg.exports["./tracklist"]).toEqual({
      import: { types: "./dist/tracklist.d.ts", default: "./dist/tracklist.js" },
      require: { types: "./dist/tracklist.d.cts", default: "./dist/tracklist.cjs" },
    });
  });

  it("parseTracklist returns every key", () => {
    const tl = parseTracklist("00:00 Noisia - Stigma\n");
    expect(Object.keys(tl).sort()).toEqual(["entries", "specVersion", "stats", "warnings"]);
    expect(tl.specVersion).toBe(SPEC_VERSION);
    expect(Object.keys(tl.entries[0] ?? {}).sort()).toEqual(
      ["eol", "kind", "line", "parts", "raw", "time", "track"].sort(),
    );
    expect(tl.entries[0]?.track?.mode).toBe("tracklist");
  });

  it("mode: tracklist is accepted by parse() as a single-line mode", () => {
    const r = parse("Strobe - Radio Edit", { mode: "tracklist" });
    expect(r.mode).toBe("tracklist");
    expect(r.artists.map((a) => a.name)).toEqual(["Strobe"]);
    expect(r.title).toBe("Radio Edit");
    expect(parse("Strobe - Radio Edit").title).toBe("Strobe");
    // `auto` never resolves to tracklist (R2.1).
    expect(parse("00:00 A - B").mode).not.toBe("tracklist");
  });

  it("parse() has no public list-context switch: a tight dash never splits on its own", () => {
    const opts = { mode: "tracklist", listContext: true } as unknown as Parameters<typeof parse>[1];
    expect(parse("Optiv-Inception", opts).artists).toEqual([]);
    const tl = parseTracklist("0:00 A - B\n1:00 Optiv-Inception");
    expect(tl.entries[1]?.track?.artists.map((a) => a.name)).toEqual(["Optiv"]);
  });

  it("options pass through: knownArtists, markerWords, audioSeconds, mode", () => {
    const text = "0:00 Chase & Status - Blind Faith\n5:00 Station ident\n9:00 X - Y";
    const tl = parseTracklist(text, {
      knownArtists: ["Chase & Status"],
      markerWords: ["ident"],
      audioSeconds: 400,
    });
    expect(tl.entries[0]?.track?.artists.map((a) => a.name)).toEqual(["Chase & Status"]);
    expect(tl.entries[1]?.kind).toBe("marker");
    expect(tl.warnings.map((w) => [w.code, w.line])).toEqual([["time-after-audio-end", 3]]);
    expect(
      parseTracklist("0:00 Strobe - Radio Edit", { mode: "clean" }).entries[0]?.track?.title,
    ).toBe("Strobe");
  });
});

describe("bad input, options and edits never throw (R0.8, R13.9.6)", () => {
  const badOptions: unknown[] = [
    undefined,
    null,
    5,
    "x",
    [],
    { mode: 7 },
    { mode: "nope" },
    { audioSeconds: "100" },
    { audioSeconds: Number.NaN },
    { audioSeconds: -1 },
    { markerWords: "intro" },
    { markerWords: [1, null] },
    { knownArtists: 5, keywords: "x", splitAnd: 3 },
  ];
  it.each(badOptions)("options %s", (options) => {
    const opts = options as TracklistOptions;
    const tl = parseTracklist("00:00 Noisia - Stigma\r\n03:00 Phace - Plastic", opts);
    expect(tl.entries).toHaveLength(2);
    expect(tl.entries[0]?.track?.title).toBe("Stigma");
    expect(applyEdits("0:00 a", [{ op: "shift", seconds: 1 }], opts).text).toBe("0:01 a");
  });

  it("options whose getters throw", () => {
    const hostile = new Proxy(
      {},
      {
        get() {
          throw new Error("boom");
        },
      },
    ) as TracklistOptions;
    expect(parseTracklist("00:00 Noisia - Stigma", hostile).entries[0]?.track?.title).toBe(
      "Stigma",
    );
    expect(applyEdits("0:00 a", [{ op: "shift", seconds: 1 }], hostile).text).toBe("0:01 a");
  });

  it("odd text values", () => {
    for (const bad of [undefined, null, 42, {}, [], "\ud800", "\r\r\n\n"]) {
      expect(() => parseTracklist(bad as unknown as string)).not.toThrow();
      expect(() => applyEdits(bad as unknown as string, [{ op: "sort-by-time" }])).not.toThrow();
    }
    expect(parseTracklist(undefined as unknown as string).entries).toEqual([]);
  });

  const badEdits: unknown[] = [
    null,
    5,
    "set-time",
    [],
    { op: "set-time", line: 1, seconds: Number.NaN },
    { op: "set-time", line: 1, seconds: Number.POSITIVE_INFINITY },
    { op: "set-time", line: 9, seconds: 10 },
    { op: "set-time", line: 1.5, seconds: 10 },
    { op: "set-time", line: 3, seconds: 10 },
    { op: "set-time", line: 1, seconds: 1e300 },
    { op: "shift", seconds: "5" },
    { op: "shift", seconds: 5, lines: [1, 99] },
    { op: "shift", seconds: 5, lines: "1" },
    { op: "shift", seconds: 5, from: 0 },
    { op: "set-rest", line: 1, rest: "a\nb" },
    { op: "set-rest", line: 1, rest: 5 },
    { op: "insert", after: -1, text: "x" },
    { op: "insert", after: 1, text: "a\r" },
    { op: "delete", line: 0 },
    { op: "explode" },
    { line: 1 },
  ];
  it.each(badEdits)("edit %j changes nothing and warns edit-invalid", (edit) => {
    const text = "0:10 A - B\n1:20 C - D\nnote";
    const r = applyEdits(text, [edit as Edit]);
    expect(r.text).toBe(text);
    expect(r.changed).toEqual([]);
    expect(r.warnings.map((w) => [w.code, w.edit])).toEqual([["edit-invalid", 0]]);
  });

  it("non-array edits are treated as no edits", () => {
    expect(applyEdits("0:00 a", "x" as unknown as Edit[])).toEqual({
      text: "0:00 a",
      changed: [],
      warnings: [],
    });
  });
});
