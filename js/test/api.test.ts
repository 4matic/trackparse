import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import {
  allArtists,
  createParser,
  format,
  normalize,
  parse,
  parseArtists,
  SPEC_VERSION,
} from "../src/index.js";
import { specDir } from "./runner.js";

describe("public API", () => {
  it("SPEC_VERSION mirrors spec/VERSION", () => {
    expect(SPEC_VERSION).toBe(readFileSync(join(specDir, "VERSION"), "utf8").trim());
  });

  it("parse returns every key", () => {
    const r = parse("");
    expect(Object.keys(r).sort()).toEqual(
      [
        "artists",
        "flags",
        "fullTitle",
        "input",
        "junk",
        "mode",
        "position",
        "timestamp",
        "title",
        "versions",
        "warnings",
        "year",
      ].sort(),
    );
    expect(r.mode).toBe("clean");
  });

  it("never throws on odd runtime input", () => {
    for (const bad of [undefined, null, 42, {}, [], "\ud800"]) {
      expect(() => parse(bad as unknown as string)).not.toThrow();
      expect(() => parseArtists(bad as unknown as string)).not.toThrow();
      expect(() => normalize(bad as unknown as string)).not.toThrow();
    }
  });

  it("allArtists merges remixers and dedups by R0.4 key", () => {
    const r = parse("Noisia & Phace - Stigma (Noisia Remix) [Mefjus Remix]");
    expect(allArtists(r).map((a) => [a.name, a.role])).toEqual([
      ["Noisia", "primary"],
      ["Phace", "primary"],
      ["Mefjus", "remixer"],
    ]);
  });

  it("createParser compiles keywords once and merges per-call options", () => {
    const p = createParser({ keywords: { versionHeads: { rmx2: "remix" } } });
    expect(p.parse("A - B (X Rmx2)").versions[0]?.type).toBe("remix");
    expect(parse("A - B (X Rmx2)").versions).toEqual([]);
    const withKnown = p.parse("Chase & Status - Blind Faith", { knownArtists: ["Chase & Status"] });
    expect(withKnown.artists.map((a) => a.name)).toEqual(["Chase & Status"]);
    expect(p.parseArtists("A feat. B").map((a) => a.role)).toEqual(["primary", "featured"]);
  });

  it("keywords extend junk, genres, descriptors and feat markers", () => {
    const p = createParser({
      keywords: {
        junk: { "visualiser hd": "video" },
        genres: ["neurohop"],
        descriptors: ["festival"],
        featMarkers: ["featuring:"],
      },
    });
    expect(p.parse("A - B (Visualiser HD)").junk).toEqual([
      { raw: "Visualiser HD", kind: "video" },
    ]);
    expect(p.parse("A - B [Neurohop]").junk).toEqual([{ raw: "Neurohop", kind: "genre" }]);
    expect(p.parse("A - B (Festival Mix)").versions[0]?.modifiers).toEqual(["festival"]);
    expect(p.parse("A featuring: C - B").artists.map((a) => a.role)).toEqual([
      "primary",
      "featured",
    ]);
  });

  it("format renders defaults", () => {
    expect(format(parse("01. Noisia - Stigma (VIP)"), { position: true })).toBe(
      "01. Noisia - Stigma (VIP)",
    );
  });
});

describe("bad options never throw (R0.8)", () => {
  const bad: unknown[] = [
    { knownArtists: "Chase & Status" },
    { knownArtists: 5 },
    { knownArtists: { a: 1 } },
    { keywords: "x" },
    { keywords: [1] },
    { keywords: { junk: "x", versionHeads: 5, genres: "g", descriptors: "abc", featMarkers: {} } },
    { mode: 7, splitAnd: 5, uploader: 3 },
  ];
  it.each(bad)("%j", (options) => {
    const opts = options as Parameters<typeof parse>[1];
    expect(parse("Chase & Status - Blind Faith (Loadstar Remix)", opts).title).toBe("Blind Faith");
    expect(createParser(opts).parse("A - T", opts).title).toBe("T");
  });

  it("ignores non-string entries but keeps valid ones", () => {
    const opts = { knownArtists: [1, null, "Chase & Status"] } as unknown as Parameters<
      typeof parse
    >[1];
    expect(parse("Chase & Status - X", opts).artists.map((a) => a.name)).toEqual([
      "Chase & Status",
    ]);
  });
});
