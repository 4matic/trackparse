/** SPEC.md "Invariants": property tests with fast-check. */
import { readFileSync } from "node:fs";
import { join } from "node:path";
import Ajv2020 from "ajv/dist/2020.js";
import fc from "fast-check";
import { describe, expect, it } from "vitest";
import { format, normalize, type ParsedTrack, parse } from "../src/index.js";
import { dedupKey } from "../src/words.js";
import { specDir } from "./runner.js";

const ajv = new Ajv2020({ allErrors: true, strict: false });
const validateTrack = ajv.compile(
  JSON.parse(readFileSync(join(specDir, "schema", "parsed-track.schema.json"), "utf8")) as object,
);

/** Characters that exercise every scanner branch. */
const tricky = fc.constantFrom(
  " ",
  "  ",
  "-",
  " - ",
  " – ",
  "—",
  "(",
  ")",
  "[",
  "]",
  "{",
  "}",
  '"',
  "'",
  "“",
  "|",
  " | ",
  ",",
  ";",
  "&",
  " & ",
  " x ",
  " feat. ",
  " ft ",
  "prod. by ",
  " by ",
  ".",
  ":",
  "_",
  "0",
  "1",
  "01. ",
  "3:45 ",
  "Remix",
  " Mix",
  "VIP",
  "Official Video",
  "ID",
  "​",
  " ",
  "",
  "é",
  "é",
  "𝄞",
  "\ud800",
  "×",
  "／",
  ".mp3",
);
const noisyString = fc
  .array(fc.oneof(tricky, fc.string({ maxLength: 4 })), { maxLength: 24 })
  .map((a) => a.join(""));
const anyString = fc.oneof(fc.string({ unit: "binary", maxLength: 60 }), noisyString);

// Track-shaped inputs for the metamorphic and round-trip properties.
const artist = fc.constantFrom(
  "Noisia",
  "Phace",
  "Fox Stevenson",
  "Sub Focus",
  "Mefjus",
  "Camo & Krooked",
  "Alix Perez",
);
const titleWords = fc.constantFrom(
  "Tentacles",
  "Bruises",
  "Out the Blue",
  "Dead Limit",
  "Stigma",
  "Blind Faith",
);
const remixer = fc.constantFrom("Skrillex", "Magnetude", "Wilkinson", "Break");
const track = fc
  .record({
    artists: fc.array(artist, { minLength: 1, maxLength: 3 }),
    joiner: fc.constantFrom(" & ", ", ", " x ", " vs. "),
    feat: fc.option(artist, { nil: undefined }),
    title: titleWords,
    remix: fc.option(remixer, { nil: undefined }),
  })
  .map(({ artists, joiner, feat, title, remix }) => {
    const unique = [...new Set(artists)];
    let s = unique.join(joiner);
    if (feat && !unique.includes(feat)) s += ` feat. ${feat}`;
    s += ` - ${title}`;
    if (remix) s += ` (${remix} Remix)`;
    return s;
  });

const withoutInput = (t: ParsedTrack) => ({ ...t, input: "" });

describe("invariants", () => {
  it("1. totality: parse never throws", () => {
    fc.assert(
      fc.property(anyString, (s) => {
        parse(s);
        parse(s, { mode: "youtube", uploader: s });
        parse(s, { mode: "filename", knownArtists: [s] });
      }),
      { numRuns: 1500 },
    );
  });

  it("1. totality: 10 000 code points parse well under 100 ms", () => {
    const blocks = [
      "(",
      "a - ",
      "A & B, ",
      "feat. X ",
      "[Remix] ",
      "-a",
      " by x",
      '"q" ',
      "x".repeat(7),
      "prod. A feat. B ",
      "a by me ",
      "(x) - ",
      "Official Video ",
      "x - Official Video ",
      "Remix - ",
    ];
    for (const block of blocks) {
      const input = block.repeat(Math.ceil(10_000 / block.length)).slice(0, 10_000);
      parse(input); // warm-up
      const t0 = performance.now();
      parse(input, { mode: "youtube" });
      expect(performance.now() - t0, JSON.stringify(block)).toBeLessThan(1000); // generous bound: catches super-linear blowups, not CI noise
    }
  });

  it("1. repeated trailing junk is linear", () => {
    // R8.4 once rescanned the whole title per stripped phrase (quadratic): 40k code points of
    // trailing junk took ~1.7 s before the fix; linear code needs well under a second.
    const input = `A - T ${"Official Video ".repeat(2_666)}`;
    const t0 = performance.now();
    const track = parse(input, { mode: "youtube" });
    expect(track.title).toBe("T");
    expect(track.junk).toHaveLength(2_666);
    expect(performance.now() - t0).toBeLessThan(1000);
  });

  it("2. normalize is idempotent", () => {
    fc.assert(
      fc.property(anyString, (s) => normalize(normalize(s)) === normalize(s)),
      { numRuns: 2000 },
    );
  });

  it("3. noise-insensitive", () => {
    fc.assert(
      fc.property(track, fc.nat(), (s, seed) => {
        const base = withoutInput(parse(s));
        const noisy = [
          s.split(" ").join("  "),
          s.split(" - ").join(" – "),
          s.split(" - ").join(" — "),
          `${s.slice(0, seed % (s.length + 1))}\u200b${s.slice(seed % (s.length + 1))}`,
        ];
        for (const n of noisy) expect(withoutInput(parse(n))).toEqual(base);
      }),
    );
  });

  it("4. every output validates against the schema", () => {
    fc.assert(
      fc.property(anyString, (s) => {
        const ok = validateTrack(parse(s));
        if (!ok) throw new Error(ajv.errorsText(validateTrack.errors));
      }),
      { numRuns: 1500 },
    );
  });

  it("5. artist hygiene", () => {
    fc.assert(
      fc.property(fc.oneof(anyString, track), (s) => {
        const r = parse(s);
        const keys = new Set<string>();
        for (const a of r.artists) {
          expect(a.name.length).toBeGreaterThan(0);
          expect(a.name).toBe(a.name.trim());
          // R9.2: producers are deduped only among producers.
          const k = `${a.role === "producer" ? "p" : "c"}:${dedupKey(a.name)}`;
          expect(keys.has(k), `duplicate ${a.name}`).toBe(false);
          keys.add(k);
        }
      }),
      { numRuns: 1500 },
    );
  });

  describe("7. metamorphic", () => {
    const quiet = track.filter((s) => parse(s).warnings.length === 0);

    it("prefixing `01. ` only sets position", () => {
      fc.assert(
        fc.property(quiet, (s) => {
          const a = parse(s);
          const b = parse(`01. ${s}`);
          expect(b.position).toEqual({ raw: "01", number: 1 });
          expect({ ...withoutInput(b), position: null }).toEqual(withoutInput(a));
        }),
      );
    });

    it("appending ` (Official Video)` only adds junk", () => {
      fc.assert(
        fc.property(quiet, (s) => {
          const a = parse(s);
          const b = parse(`${s} (Official Video)`);
          expect(b.junk).toEqual([...a.junk, { raw: "Official Video", kind: "video" }]);
          expect({ ...withoutInput(b), junk: a.junk, mode: a.mode }).toEqual(withoutInput(a));
        }),
      );
    });

    it("(X Remix) ↔ [X Remix] changes only the delimiter", () => {
      fc.assert(
        fc.property(
          quiet.filter((s) => s.endsWith(" Remix)")),
          (s) => {
            const a = parse(s);
            const b = parse(
              `${s.slice(0, s.lastIndexOf("("))}[${s.slice(s.lastIndexOf("(") + 1, -1)}]`,
            );
            const vb = b.versions.map((v) => ({ ...v, delimiter: "(" }));
            expect({ ...withoutInput(b), fullTitle: a.fullTitle, versions: vb }).toEqual(
              withoutInput(a),
            );
          },
        ),
      );
    });

    it("REMIX ↔ remix changes only raw", () => {
      fc.assert(
        fc.property(
          quiet.filter((s) => s.endsWith(" Remix)")),
          (s) => {
            const a = parse(s);
            const b = parse(s.replace(/ Remix\)$/, " REMIX)"));
            const vb = b.versions.map((v, i) => ({ ...v, raw: a.versions[i]?.raw ?? "" }));
            expect({ ...withoutInput(b), fullTitle: a.fullTitle, versions: vb }).toEqual(
              withoutInput(a),
            );
          },
        ),
      );
    });
  });

  it("8. round-trip (R11.6)", () => {
    fc.assert(
      fc.property(track, (s) => {
        const a = parse(s);
        if (a.warnings.length > 0) return;
        const b = parse(format(a));
        const shape = (t: ParsedTrack) => ({
          artists: t.artists.map((x) => [x.name, x.role]),
          title: t.title,
          versions: t.versions.map((v) => [v.type, v.artists.map((x) => x.name)]),
        });
        expect(shape(b)).toEqual(shape(a));
      }),
    );
  });
});
