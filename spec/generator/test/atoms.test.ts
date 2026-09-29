import { describe, expect, it } from "vitest";
import {
  ALL_VERSIONS,
  ARTISTS,
  EXTRAS,
  FEAT_MARKERS,
  JOINERS,
  JUNK,
  MODES,
  PREFIXES,
  SEPARATORS,
  TITLES,
  TRANSFORMS,
  artistById,
  versionById,
} from "../src/atoms.js";
import { DATA } from "../src/data.js";
import { REMIXERS } from "../src/scenarios/pools.js";
import { artistProblem, mightClassify, remixerProblem, titleProblem, trailingJunkAmbiguous, words } from "../src/text.js";

const VERSION_TYPES = new Set([
  "remix", "bootleg", "vip", "edit", "flip", "refix", "rework", "mashup", "blend", "dub", "mix", "extended", "radio",
  "club", "original", "instrumental", "acapella", "live", "acoustic", "remaster", "demo", "reprise", "cover", "version",
  "spedUp", "slowed", "nightcore",
]);
const JUNK_KINDS = new Set(["video", "audio", "lyrics", "quality", "promo", "platform", "label", "genre", "other"]);

function uniqueLowerIds(pool: { id: string }[]) {
  const ids = pool.map((a) => a.id);
  expect(new Set(ids).size).toBe(ids.length);
  for (const id of ids) expect(id).toMatch(/^[a-z0-9][a-z0-9-]*$/);
}

describe("atom pools", () => {
  it("have unique lowercase ASCII ids", () => {
    for (const pool of [ARTISTS, TITLES, JOINERS, FEAT_MARKERS, ALL_VERSIONS, EXTRAS, JUNK, SEPARATORS, PREFIXES, MODES, TRANSFORMS])
      uniqueLowerIds(pool);
  });

  it("are large enough", () => {
    expect(ARTISTS.length).toBeGreaterThanOrEqual(60);
    expect(TITLES.length).toBeGreaterThanOrEqual(40);
    expect(ALL_VERSIONS.length).toBeGreaterThanOrEqual(30);
    expect(REMIXERS.length).toBeGreaterThanOrEqual(40);
  });

  it("artists are NFC, atomic credit names and expect exactly themselves", () => {
    for (const a of ARTISTS) {
      expect(a.text.normalize("NFC"), a.id).toBe(a.text);
      expect(artistProblem(a.text, true), a.id).toBeNull();
      expect(a.expect).toEqual([{ name: a.text }]);
    }
    // traps really are traps: they would be joiner/marker-free only by the whole-word rule
    expect(artistProblem("Featurecast")).toBeNull();
    expect(artistProblem("Malcolm X")).toBeNull();
    expect(artistProblem("Mumford & Sons")).not.toBeNull();
    expect(artistProblem("Mumford & Sons", true)).toBeNull();
    expect(artistProblem("Florence + The Machine", true)).toBe("joiner:+");
  });

  it("dedup keys are distinct across the artist pool", async () => {
    const { dedupKey } = await import("../src/text.js");
    const keys = ARTISTS.map((a) => dedupKey(a.text));
    expect(new Set(keys).size).toBe(keys.length);
  });

  it("titles are NFC plain titles that no rule can pick apart", () => {
    for (const t of TITLES) {
      expect(t.text.normalize("NFC"), t.id).toBe(t.text);
      expect(titleProblem(t.text), t.id).toBeNull();
    }
    // the checker itself catches the classic traps
    expect(titleProblem("Live Forever")).toBe("classifies");
    expect(titleProblem("1999")).toBe("classifies");
    expect(titleProblem("Hollywood Video")).not.toBeNull();
    expect(titleProblem("Satisfaction (Reprise)")).toBe("group-classifies");
  });

  it("remixer pool only contains names safe as a head-scan credit span", () => {
    for (const r of REMIXERS) expect(remixerProblem(r.text), r.id).toBeNull();
    expect(remixerProblem("Deep House")).not.toBeNull();
    expect(remixerProblem("Liquid")).not.toBeNull();
  });

  it("version atoms carry valid expectations", () => {
    for (const v of ALL_VERSIONS) {
      for (const p of v.parts) {
        expect(VERSION_TYPES.has(p.type), v.id).toBe(true);
        expect(p.modifiers.every((m) => m === m.toLowerCase())).toBe(true);
        const bare = p.tpl.replace("{R}", "Noisia");
        expect(mightClassify(bare), v.id).toBe(true); // must be recognisable as a version at all
      }
    }
    expect(versionById("extended-vip-mix").parts[0]).toMatchObject({ type: "vip", modifiers: ["extended"] });
    expect(versionById("live-at-wembley").parts[0]).toMatchObject({ type: "live", year: 1986, descriptor: "at Wembley" });
    expect(versionById("mix").parts[0]!.type).toBe("remix"); // `Levela Mix` → remix
    expect(versionById("mix-bare").parts[0]!.type).toBe("mix");
    expect(versionById("vip-bare").peelAlone).toBe(false);
    expect(versionById("remastered-2015").peelAlone).toBe(true);
    // R6.2 (ii) does not apply to prefix-form versions.
    expect(versionById("sped-up").peelAlone).toBe(false);
    expect(versionById("japanese-version").peelAlone).toBe(true);
  });

  it("junk atoms: valid kinds; trailing forms are unambiguous (R8.4)", () => {
    for (const j of JUNK) {
      expect(JUNK_KINDS.has(j.kind), j.id).toBe(true);
      if (j.forms.includes("trailing")) expect(trailingJunkAmbiguous([], words(j.text)), j.id).toBe(false);
    }
    expect(trailingJunkAmbiguous([], ["Official", "Video"])).toBe(true); // "Video" alone is also a phrase
    expect(trailingJunkAmbiguous(["Full"], ["HD"])).toBe(true); // "full hd"
  });

  it("prefix atoms: timestamp seconds and position numbers match their raw text", () => {
    for (const p of PREFIXES) {
      if (p.timestamp) {
        const parts = p.timestamp.raw.split(":").map(Number);
        const secs = parts.reduce((a, b) => a * 60 + b, 0);
        expect(p.timestamp.seconds, p.id).toBe(secs);
        expect(p.text).toContain(p.timestamp.raw);
      }
      if (p.position) {
        expect(p.position.number, p.id).toBe(Number.parseInt(p.position.raw, 10));
        expect(p.text).toContain(p.position.raw);
      }
    }
  });

  it("platform suffixes and extensions come from spec/data", () => {
    for (const m of MODES) {
      if (m.platform) expect(DATA.platformSuffixes).toContain(m.platform.text);
      if (m.ext) expect(DATA.extensions).toContain(m.ext);
    }
  });

  it("separator dash variants are all in the R1.5 dash set", () => {
    const R15 = (c: number) => (c >= 0x2010 && c <= 0x2015) || [0x2212, 0xfe58, 0xfe63, 0xff0d, 0x2d].includes(c);
    for (const s of SEPARATORS) {
      if (s.kind === "pipe") continue;
      const dashes = [...s.text.trim()];
      for (const d of dashes) expect(R15(d.codePointAt(0)!), s.id).toBe(true);
    }
  });

  it("artistById throws on unknown ids", () => {
    expect(() => artistById("nope")).toThrow();
  });
});
