/** SPEC.md R13.11 tracklist invariants: property tests with fast-check. */
import { readFileSync } from "node:fs";
import { join } from "node:path";
import Ajv2020 from "ajv/dist/2020.js";
import fc from "fast-check";
import { describe, expect, it } from "vitest";
import { parse } from "../src/index.js";
import { applyEdits, type Edit, parseTracklist } from "../src/tracklist/index.js";
import { specDir } from "./runner.js";

const ajv = new Ajv2020({ allErrors: true, strict: false });
const loadSchema = (name: string) =>
  JSON.parse(readFileSync(join(specDir, "schema", name), "utf8")) as { $id: string };
ajv.addSchema(loadSchema("parsed-track.schema.json"));
const tracklistSchema = loadSchema("tracklist.schema.json");
ajv.addSchema(tracklistSchema);
const validateTracklist = ajv.getSchema(tracklistSchema.$id) as ReturnType<typeof ajv.compile>;
const validateEditResult = ajv.getSchema(`${tracklistSchema.$id}#/$defs/editResult`) as ReturnType<
  typeof ajv.compile
>;

/** Fragments that exercise the line splitter, the time scanner and the line kinds. */
const fragment = fc.constantFrom(
  "\n",
  "\r\n",
  "\r",
  "\n\r",
  " ",
  " ",
  "\u0085",
  " ",
  "  ",
  "\t",
  " ",
  "00:00",
  "0:00 ",
  "04:37 ",
  "1:02:03 ",
  "01:02:03.5 ",
  "125:30 ",
  "12:34,250",
  "1:75 ",
  "0:61:00 ",
  "1:2",
  "[",
  "]",
  "(",
  ")",
  "[00:10] ",
  "(1:00)",
  " - ",
  " – ",
  "-",
  "|",
  ".",
  ":",
  "=> ",
  "==>",
  "1 ",
  "2 ",
  "01. ",
  "3) ",
  "Artist",
  "Title",
  "Noisia - Stigma",
  "Intro",
  "interview",
  "Tracklist:",
  "Part 2",
  "https://x.y",
  "www.",
  "_-_",
  "Ab-Cd",
  "ID",
  "(VIP)",
  "​",
  "𝄞",
  "\ud800",
);
const noisyText = fc
  .array(fc.oneof(fragment, fc.string({ maxLength: 4 })), { maxLength: 40 })
  .map((a) => a.join(""));
const anyText = fc.oneof(fc.string({ unit: "binary", maxLength: 80 }), noisyText);

// Realistic timed lists for the edit properties.
const pad2 = (n: number) => String(n).padStart(2, "0");
const timeStyle = fc.constantFrom("M:SS", "MM:SS", "H:MM:SS", "HH:MM:SS", "MM:SS.d", "M:SS,ddd");
function writeTime(style: string, seconds: number, frac: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  switch (style) {
    case "M:SS":
      return `${Math.floor(seconds / 60)}:${pad2(s)}`;
    case "MM:SS":
      return `${pad2(Math.floor(seconds / 60))}:${pad2(s)}`;
    case "H:MM:SS":
      return `${h}:${pad2(m)}:${pad2(s)}`;
    case "HH:MM:SS":
      return `${pad2(h)}:${pad2(m)}:${pad2(s)}`;
    case "MM:SS.d":
      return `${pad2(Math.floor(seconds / 60))}:${pad2(s)}.${frac % 10}`;
    default:
      return `${Math.floor(seconds / 60)}:${pad2(s)},${String(frac).padStart(3, "0")}`;
  }
}
const restText = fc.constantFrom(
  "Noisia - Stigma",
  "01. Phace - Plastic Acid",
  "2 Unlimited - No Limit",
  "Intro",
  "Paperclip interview",
  "Mefjus - Suicide Bassline (VIP)",
  "",
  "ID - ID",
);
const listLine = fc.oneof(
  {
    weight: 6,
    arbitrary: fc
      .record({
        style: timeStyle,
        seconds: fc.integer({ min: 0, max: 3599 }),
        frac: fc.integer({ min: 0, max: 999 }),
        gap: fc.constantFrom(" ", "  ", " - ", " | ", " => "),
        rest: restText,
        lead: fc.constantFrom("", " ", "["),
      })
      .map((l) => {
        const time = writeTime(l.style, l.seconds, l.frac);
        return l.lead === "[" ? `[${time}]${l.gap}${l.rest}` : `${l.lead}${time}${l.gap}${l.rest}`;
      }),
  },
  { weight: 1, arbitrary: fc.constantFrom("", "Tracklist:", "a note", "https://example.com") },
);
const timedList = fc
  .record({
    lines: fc.array(listLine, { minLength: 1, maxLength: 12 }),
    eol: fc.constantFrom("\n", "\r\n"),
    trailing: fc.boolean(),
  })
  .map(({ lines, eol, trailing }) => lines.join(eol) + (trailing ? eol : ""));

const lineCount = (text: string) => parseTracklist(text).entries.length;
const rawLines = (text: string) => parseTracklist(text).entries.map((e) => e.raw + e.eol);

describe("R13.11 tracklist invariants", () => {
  it("1. lossless, and parts concatenate to raw, for any string", () => {
    fc.assert(
      fc.property(anyText, (text) => {
        const tl = parseTracklist(text);
        expect(tl.entries.map((e) => e.raw + e.eol).join("")).toBe(text);
        for (const e of tl.entries) {
          expect(e.parts.lead + e.parts.time + e.parts.gap + e.parts.rest).toBe(e.raw);
          if (e.time === null) expect(e.parts.rest).toBe(e.raw);
        }
        expect(tl.stats.lines).toBe(tl.entries.length);
      }),
      { numRuns: 1500 },
    );
  });

  it("1. U+2028, U+2029 and U+0085 are not line breaks", () => {
    expect(lineCount("a b c\u0085d")).toBe(1);
    expect(lineCount("a\r\nb\rc\nd\n")).toBe(4);
    expect(lineCount("")).toBe(0);
  });

  it("2. totality: any text validates against tracklist.schema.json", () => {
    fc.assert(
      fc.property(anyText, (text) => {
        const tl = parseTracklist(text);
        if (!validateTracklist(tl)) throw new Error(ajv.errorsText(validateTracklist.errors));
      }),
      { numRuns: 800 },
    );
  });

  it("2. totality: any edit list returns without throwing", () => {
    const field = fc.oneof(
      fc.integer({ min: -3, max: 15 }),
      fc.double(),
      fc.string({ maxLength: 6 }),
      fc.constantFrom(null, undefined, Number.NaN, Number.POSITIVE_INFINITY, 1e300, -1e300, [1, 2]),
    );
    const edit = fc.oneof(
      fc.record(
        {
          op: fc.constantFrom(
            "set-time",
            "shift",
            "sort-by-time",
            "set-rest",
            "insert",
            "delete",
            "x",
          ),
          line: field,
          seconds: field,
          lines: fc.oneof(fc.array(field, { maxLength: 4 }), field),
          from: field,
          rest: fc.oneof(fc.string({ maxLength: 8 }), fc.constantFrom("a\nb", "c\rd"), field),
          after: field,
          text: fc.oneof(fc.string({ maxLength: 8 }), fc.constantFrom("a\nb"), field),
        },
        { requiredKeys: ["op"] },
      ),
      fc.anything(),
    );
    fc.assert(
      fc.property(fc.oneof(timedList, anyText), fc.array(edit, { maxLength: 6 }), (text, edits) => {
        const r = applyEdits(text, edits as Edit[]);
        if (!validateEditResult(r)) throw new Error(ajv.errorsText(validateEditResult.errors));
        const n = lineCount(r.text);
        for (const line of r.changed) expect(line).toBeLessThanOrEqual(n);
      }),
      { numRuns: 800 },
    );
  });

  it("2. totality: 10 000 lines finish well under one second", () => {
    const lines: string[] = [];
    for (let i = 0; i < 10_000; i++) {
      const s = (i * 37) % 7200;
      const time = `${pad2(Math.floor(s / 3600))}:${pad2(Math.floor((s % 3600) / 60))}:${pad2(s % 60)}`;
      lines.push(`${time} ${i % 100} Artist ${i} & Other - Title ${i} (VIP Mix)`);
    }
    const text = lines.join("\r\n");
    parseTracklist(text.slice(0, 5000)); // warm-up
    // Best of three, so a busy CI worker does not fail the run; a super-linear blowup still would.
    const best = (f: () => unknown) => {
      let min = Number.POSITIVE_INFINITY;
      for (let k = 0; k < 3; k++) {
        const t0 = performance.now();
        f();
        min = Math.min(min, performance.now() - t0);
      }
      return min;
    };
    expect(parseTracklist(text).entries).toHaveLength(10_000);
    expect(best(() => parseTracklist(text))).toBeLessThan(1000);
    expect(
      best(() => applyEdits(text, [{ op: "shift", seconds: 5 }, { op: "sort-by-time" }])),
    ).toBeLessThan(1000);
  });

  it("3. identity: applyEdits(t, []) returns t", () => {
    fc.assert(
      fc.property(anyText, (text) => {
        expect(applyEdits(text, [])).toEqual({ text, changed: [], warnings: [] });
      }),
    );
  });

  it("3. locality: an edit on one line leaves every other line byte for byte", () => {
    fc.assert(
      fc.property(
        timedList,
        fc.nat(),
        fc.integer({ min: -4000, max: 4000 }),
        fc.constantFrom("set-time", "set-rest", "shift"),
        (text, pick, seconds, op) => {
          const before = rawLines(text);
          const line = (pick % before.length) + 1;
          const edit: Edit =
            op === "set-time"
              ? { op: "set-time", line, seconds }
              : op === "set-rest"
                ? { op: "set-rest", line, rest: "New - Rest" }
                : { op: "shift", seconds, lines: [line] };
          const r = applyEdits(text, [edit]);
          const after = rawLines(r.text);
          expect(after).toHaveLength(before.length);
          after.forEach((l, i) => {
            if (i !== line - 1) expect(l).toBe(before[i]);
          });
          for (const c of r.changed) expect(c).toBe(line);
          if (r.warnings.some((w) => w.code === "edit-invalid")) expect(r.text).toBe(text);
        },
      ),
    );
  });

  it("3. locality: insert and delete keep the other lines", () => {
    fc.assert(
      fc.property(timedList, fc.nat(), (text, pick) => {
        const before = parseTracklist(text).entries.map((e) => e.raw);
        const at = pick % (before.length + 1);
        const ins = applyEdits(text, [{ op: "insert", after: at, text: "00:00 New - Line" }]);
        const expected = [...before.slice(0, at), "00:00 New - Line", ...before.slice(at)];
        expect(parseTracklist(ins.text).entries.map((e) => e.raw)).toEqual(expected);
        expect(ins.changed).toContain(at + 1);
        const line = (pick % before.length) + 1;
        const del = applyEdits(text, [{ op: "delete", line }]);
        const kept = before.filter((_, i) => i !== line - 1);
        const got = parseTracklist(del.text).entries.map((e) => e.raw);
        // Deleting the last line moves its eol ("" here) up: an empty line that becomes last
        // without an eol is not a line (R13.9.5), so trailing empty lines may disappear.
        while (kept.at(-1) === "" && got.length < kept.length && line === before.length) kept.pop();
        expect(got).toEqual(kept);
      }),
    );
  });

  it("4. round trip: shift(+s) then shift(-s) restores the text", () => {
    fc.assert(
      fc.property(
        timedList,
        fc.integer({ min: 1, max: 5000 }),
        fc.option(fc.integer({ min: 1, max: 12 }), { nil: undefined }),
        (text, s, from) => {
          const n = lineCount(text);
          const target = from === undefined || from > n ? {} : { from };
          const up = applyEdits(text, [{ op: "shift", seconds: s, ...target }]);
          const down = applyEdits(up.text, [{ op: "shift", seconds: -s, ...target }]);
          const lossy = [...up.warnings, ...down.warnings].some(
            (w) => w.code === "time-clamped" || w.code === "style-upgraded",
          );
          // Widths are minimum widths (R13.9.2): `0:00` + 600 s is `10:00`, whose style then has
          // two minute digits, so the way back writes `00:00`. A time whose digit count grew is
          // outside the round trip (R13.9.7), like clamps and upgrades.
          const widths = (t: string) => parseTracklist(t).entries.map((e) => e.time?.raw.length);
          const grew = widths(up.text).some((w, i) => w !== widths(text)[i]);
          if (!lossy && !grew) expect(down.text).toBe(text);
        },
      ),
    );
  });

  it("4. sort keeps the eol slots and is idempotent", () => {
    fc.assert(
      fc.property(timedList, (text) => {
        const once = applyEdits(text, [{ op: "sort-by-time" }]);
        const eols = (t: string) => parseTracklist(t).entries.map((e) => e.eol);
        const sorted = parseTracklist(once.text).entries;
        // An empty line sorted into the last slot (eol "") is not a line any more (R13.1.1).
        if (sorted.length === eols(text).length) expect(eols(once.text)).toEqual(eols(text));
        expect(applyEdits(once.text, [{ op: "sort-by-time" }]).text).toBe(once.text);
      }),
    );
  });

  it("5. a one-line text's track equals parse(rest, { mode: 'tracklist' })", () => {
    const oneLine = fc
      .oneof(
        noisyText,
        fc.tuple(listLine, noisyText).map(([a, b]) => a + b),
      )
      .map((s) => s.replace(/[\r\n]/g, " "));
    fc.assert(
      fc.property(oneLine, (text) => {
        const e = parseTracklist(text).entries[0];
        if (!e?.track) return;
        expect(e.track).toEqual(parse(e.parts.rest, { mode: "tracklist" }));
      }),
      { numRuns: 1500 },
    );
  });
});
