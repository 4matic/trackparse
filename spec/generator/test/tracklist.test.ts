import { describe, expect, it } from "vitest";
import {
  EOLS,
  type EolAtom,
  FRAMES,
  GAPS,
  LEADS,
  LINK_TEXT,
  NOTE_TEXT,
  POSITION_STYLES,
  TIME_FORMS,
  TIME_ORDERS,
  TRACKS,
  TRACK_COUNTS,
  TRACK_SETS,
  byId,
  trackById,
} from "../src/atoms.js";
import { DATA } from "../src/data.js";
import { wordMatches } from "../src/text.js";
import {
  type TracklistComposition,
  expectTracklist,
  timesFor,
  trackProblem,
  tracklistProblem,
} from "../src/tracklist.js";
import { generateAll } from "../src/write.js";

const T = (id: string) => byId(TIME_FORMS, id);
const comp = (over: Partial<Omit<TracklistComposition, "tracks">> & { tracks?: string[] } = {}): TracklistComposition => ({
  time: over.time ?? T("m-ss"),
  lead: over.lead ?? byId(LEADS, "plain"),
  gap: over.gap ?? byId(GAPS, "space"),
  pos: over.pos ?? byId(POSITION_STYLES, "none"),
  eol: over.eol ?? byId(EOLS, "lf"),
  frame: over.frame ?? byId(FRAMES, "none"),
  order: over.order ?? "zero",
  tracks: (over.tracks ?? ["noisia-diplodocus", "phace-hologram"]).map(trackById),
});

/** R13.2.2 shapes, written out by hand (not the generator's own formatter). */
const TIME_SHAPE = /^([0-9]{1,3}:[0-5][0-9]|[0-9]{1,2}:[0-5][0-9]:[0-5][0-9])([.,][0-9]{1,3})?$/;

describe("tracklist atoms", () => {
  it("have unique lowercase ASCII ids", () => {
    for (const pool of [TIME_FORMS, LEADS, GAPS, POSITION_STYLES, EOLS, FRAMES, TIME_ORDERS, TRACK_COUNTS, TRACKS, TRACK_SETS]) {
      const ids = pool.map((a) => a.id);
      expect(new Set(ids).size).toBe(ids.length);
      for (const id of ids) expect(id).toMatch(/^[a-z0-9][a-z0-9-]*$/);
    }
  });

  it("time forms: valid R13.2.2 shapes, increasing from 0, seconds match the raw text", () => {
    for (const tf of TIME_FORMS) {
      if (tf.fmt === null) continue;
      expect(tf.steps[0], tf.id).toBe(0);
      expect(tf.steps.length).toBeGreaterThanOrEqual(7);
      const times = timesFor(tf, "zero", tf.steps.length);
      for (let k = 0; k < times.length; k++) {
        const t = times[k]!;
        expect(t.raw, tf.id).toMatch(TIME_SHAPE);
        // Independent recomputation from the raw text.
        const [clock, frac] = t.raw.split(/[.,]/);
        const parts = clock!.split(":").map(Number);
        const whole = parts.reduce((a, b) => a * 60 + b, 0);
        const fraction = frac === undefined ? 0 : Number(`0.${frac}`);
        expect(t.seconds, `${tf.id} ${t.raw}`).toBe(whole + fraction);
        // Fractions are exact binary values (so every port computes the same number).
        expect(Number.isInteger(fraction * 1000)).toBe(true);
        if (k > 0) expect(t.seconds).toBeGreaterThan(times[k - 1]!.seconds);
      }
    }
    expect(timesFor(T("long-min"), "zero", 5).map((t) => [t.raw, t.seconds])).toEqual([
      ["00:00", 0], ["30:05", 1805], ["60:05", 3605], ["75:30", 4530], ["125:30", 7530],
    ]);
    expect(timesFor(T("hh-mm-ss"), "zero", 7)[6]).toEqual({ raw: "10:20:30", seconds: 37230 });
    expect(timesFor(T("h-mm-ss"), "zero", 5)[4]).toEqual({ raw: "1:00:10", seconds: 3610 });
    expect(timesFor(T("frac-dot"), "zero", 2)).toEqual([
      { raw: "00:00.000", seconds: 0 }, { raw: "03:45.500", seconds: 225.5 },
    ]);
    expect(timesFor(T("frac-comma"), "late", 2)).toEqual([
      { raw: "3:45,5", seconds: 225.5 }, { raw: "7:32,25", seconds: 452.25 },
    ]);
    expect(timesFor(T("m-ss"), "dup", 3).map((t) => t.raw)).toEqual(["0:00", "3:45", "3:45"]);
  });

  it("leads and gaps follow R13.2.1 / R13.2.3", () => {
    for (const l of LEADS) {
      expect(l.text, l.id).toMatch(/^[ \t]*[[(]?$/);
      expect(l.close).toBe(l.text.endsWith("[") ? "]" : l.text.endsWith("(") ? ")" : "");
    }
    for (const g of GAPS) expect(g.text, g.id).toMatch(/^[ \t]*([-–—|.:][ \t]*)?(={1,2}>[ \t]*)?$/);
    for (const g of GAPS) expect(g.text.length, g.id).toBeGreaterThan(0);
  });

  it("track parts are safe: the single-line model fully determines their tracklist ParsedTrack", () => {
    for (const t of TRACKS) expect(trackProblem(t), t.id).toBeNull();
    for (const s of TRACK_SETS) expect(new Set(s.tracks.map((t) => t.id)).size, s.id).toBe(4);
  });

  it("note, link and header texts get their kind from R13.3 alone", () => {
    const hasMarker = (s: string) => s.split(" ").some((w) => DATA.markerWords.some((m) => wordMatches(w, m)));
    expect(hasMarker(NOTE_TEXT)).toBe(false);
    expect(hasMarker(LINK_TEXT)).toBe(false);
    expect(hasMarker("Intro")).toBe(true);
    expect(hasMarker("Outro")).toBe(true);
    expect(hasMarker("Break")).toBe(true);
    expect(DATA.headerWords).toContain("part");
    expect(DATA.headerWords.some((h) => wordMatches(NOTE_TEXT.split(" ")[0]!, h))).toBe(false);
  });
});

describe("tracklist model", () => {
  it("renders a plain two-line list exactly (hand-computed)", () => {
    const r = expectTracklist(comp());
    expect(r.input).toBe("0:00 Noisia - Diplodocus\n3:45 Phace - Hologram (Enei Remix)");
    expect(r.entries).toEqual([
      {
        line: 1,
        kind: "track",
        eol: "\n",
        raw: "0:00 Noisia - Diplodocus",
        parts: { lead: "", time: "0:00", gap: " ", rest: "Noisia - Diplodocus" },
        time: { raw: "0:00", seconds: 0 },
        track: { position: null, artists: ["Noisia"], title: "Diplodocus", versions: [] },
      },
      {
        line: 2,
        kind: "track",
        eol: "",
        raw: "3:45 Phace - Hologram (Enei Remix)",
        parts: { lead: "", time: "3:45", gap: " ", rest: "Phace - Hologram (Enei Remix)" },
        time: { raw: "3:45", seconds: 225 },
        track: {
          position: null,
          artists: ["Phace"],
          title: "Hologram",
          versions: [{ type: "remix", raw: "Enei Remix", artists: ["Enei"], delimiter: "(" }],
        },
      },
    ]);
    expect(r.warnings).toEqual([]);
    expect(r.stats).toEqual({ lines: 2, tracks: 2, markers: 0, timed: 2, firstTime: 0, lastTime: 225 });
  });

  it("brackets close inside the gap; positions go to the track", () => {
    const r = expectTracklist(
      comp({ lead: byId(LEADS, "spaces-square"), gap: byId(GAPS, "dash"), pos: byId(POSITION_STYLES, "dotted-pad") }),
    );
    expect(r.entries[1]!.raw).toBe("  [3:45] - 02. Phace - Hologram (Enei Remix)");
    expect(r.entries[1]!.parts).toEqual({ lead: "  [", time: "3:45", gap: "] - ", rest: "02. Phace - Hologram (Enei Remix)" });
    expect(r.entries[1]!.track!.position).toEqual({ raw: "02", number: 2 });
  });

  it("bare positions (R13.5) are stripped from the track part and need a neighbour", () => {
    const bare = byId(POSITION_STYLES, "bare-9");
    const r = expectTracklist(comp({ pos: bare, tracks: ["noisia-diplodocus", "phace-hologram", "mefjus-stigma"] }));
    expect(r.entries.map((e) => e.parts.rest)).toEqual([
      "9 Noisia - Diplodocus", "10 Phace - Hologram (Enei Remix)", "11 Mefjus - Stigma",
    ]);
    expect(r.entries.map((e) => e.track!.position)).toEqual([
      { raw: "9", number: 9 }, { raw: "10", number: 10 }, { raw: "11", number: 11 },
    ]);
    expect(r.entries[0]!.track!.artists).toEqual(["Noisia"]);
    // A single bare line has no neighbour.
    expect(tracklistProblem(comp({ pos: bare, tracks: ["noisia-diplodocus"] }))).toMatch(/R13\.5/);
    // A timed marker between the tracks is the nearest candidate line: no neighbour.
    expect(tracklistProblem(comp({ pos: bare, frame: byId(FRAMES, "break-mid") }))).toMatch(/R13\.5/);
    // Untimed, the marker is not a candidate line, so the tracks are neighbours again.
    expect(tracklistProblem(comp({ pos: bare, frame: byId(FRAMES, "break-mid"), time: T("untimed") }))).toBeNull();
    // Blank lines are never candidates.
    expect(tracklistProblem(comp({ pos: bare, frame: byId(FRAMES, "blank-mid") }))).toBeNull();
  });

  it("time orders produce exactly their R13.7 warning", () => {
    const late = expectTracklist(comp({ order: "late" }));
    expect(late.warnings).toEqual([{ code: "no-start-at-zero", line: 1, severity: "info", suggestion: null }]);
    expect(late.stats.firstTime).toBe(225);

    const dup = expectTracklist(comp({ order: "dup", frame: byId(FRAMES, "outro") }));
    expect(dup.entries[2]!.raw).toBe("3:45 Outro");
    expect(dup.warnings).toEqual([{ code: "time-duplicate", line: 3, severity: "warning", suggestion: null }]);

    const missing = expectTracklist(comp({ order: "missing", lead: byId(LEADS, "square") }));
    expect(missing.entries[1]!).toMatchObject({
      kind: "track",
      raw: "Phace - Hologram (Enei Remix)",
      parts: { lead: "", time: "", gap: "", rest: "Phace - Hologram (Enei Remix)" },
      time: null,
    });
    expect(missing.warnings).toEqual([{ code: "time-missing", line: 2, severity: "warning", suggestion: null }]);
    expect(missing.stats).toMatchObject({ timed: 1, firstTime: 0, lastTime: 0 });
    // time-missing needs another timed entry; time-duplicate needs two timed entries.
    expect(tracklistProblem(comp({ order: "missing", tracks: ["noisia-diplodocus"] }))).not.toBeNull();
    expect(tracklistProblem(comp({ order: "dup", tracks: ["noisia-diplodocus"] }))).not.toBeNull();
  });

  it("untimed lists: tracks by spaced dash, markers by marker word, no warnings", () => {
    const r = expectTracklist(comp({ time: T("untimed"), frame: byId(FRAMES, "full") }));
    expect(r.entries.map((e) => e.kind)).toEqual(["header", "blank", "marker", "track", "track", "note"]);
    expect(r.entries[2]!.track).toEqual({ position: null, artists: [], title: "Intro", versions: [] });
    expect(r.entries[5]!.track).toBeNull();
    expect(r.warnings).toEqual([]);
    expect(r.stats).toEqual({ lines: 6, tracks: 2, markers: 1, timed: 0, firstTime: null, lastTime: null });
  });

  it("line endings: kept per line, final terminator optional, CR + empty LF line rejected", () => {
    const crEnd = expectTracklist(comp({ eol: byId(EOLS, "cr-end") }));
    expect(crEnd.input.endsWith("\r")).toBe(true);
    expect(crEnd.entries.map((e) => e.eol)).toEqual(["\r", "\r"]);
    const mixed = expectTracklist(comp({ eol: byId(EOLS, "mixed"), tracks: ["noisia-diplodocus", "phace-hologram", "mefjus-stigma"] }));
    expect(mixed.entries.map((e) => e.eol)).toEqual(["\r\n", "\n", ""]);
    const crlf: EolAtom = { id: "cr-lf", cycle: ["\r", "\n"], final: true };
    expect(tracklistProblem(comp({ eol: crlf, frame: byId(FRAMES, "blank-mid") }))).toMatch(/CR/);
  });

  it("rejects more than six lines and frames needing more tracks", () => {
    const four = ["noisia-diplodocus", "phace-hologram", "mefjus-stigma", "camo-levitate"];
    expect(tracklistProblem(comp({ frame: byId(FRAMES, "full"), tracks: four }))).toMatch(/6 lines/);
    expect(tracklistProblem(comp({ frame: byId(FRAMES, "blank-mid"), tracks: ["noisia-diplodocus"] }))).not.toBeNull();
  });
});

describe("generated tracklist cases", () => {
  const file = JSON.parse(generateAll().files.get("tracklist.json")!) as {
    defaults: { fn: string };
    cases: {
      id: string;
      input: string;
      expected: {
        entries: {
          line: number;
          kind: string;
          eol: string;
          raw: string;
          parts: { lead: string; time: string; gap: string; rest: string };
          time: { raw: string; seconds: number } | null;
          track: unknown;
        }[];
        warnings: { line: number; code: string }[];
        stats: Record<string, number | null>;
      };
    }[];
  };

  it("are lossless and internally consistent (R13.1.2, R13.1.3, R13.8)", () => {
    expect(file.defaults.fn).toBe("parseTracklist");
    for (const c of file.cases) {
      const { entries, warnings, stats } = c.expected;
      expect(c.id).toMatch(/^gen:tracklist:/);
      expect(entries.map((e) => e.raw + e.eol).join(""), c.id).toBe(c.input);
      expect(entries.length).toBeGreaterThanOrEqual(1);
      expect(entries.length).toBeLessThanOrEqual(6);
      entries.forEach((e, i) => {
        expect(e.line).toBe(i + 1);
        expect(e.parts.lead + e.parts.time + e.parts.gap + e.parts.rest, c.id).toBe(e.raw);
        expect(e.time?.raw ?? "", c.id).toBe(e.parts.time);
        expect(e.track === null, c.id).toBe(e.kind !== "track" && e.kind !== "marker");
        if (i < entries.length - 1) expect(e.eol).not.toBe("");
        expect(/[\r\n]/.test(e.raw)).toBe(false);
      });
      const timed = entries.filter((e) => e.time);
      expect(stats).toEqual({
        lines: entries.length,
        tracks: entries.filter((e) => e.kind === "track").length,
        markers: entries.filter((e) => e.kind === "marker").length,
        timed: timed.length,
        firstTime: timed[0]?.time!.seconds ?? null,
        lastTime: timed[timed.length - 1]?.time!.seconds ?? null,
      });
      const lines = warnings.map((w) => w.line);
      expect(lines, c.id).toEqual([...lines].sort((a, b) => a - b));
    }
  });

  it("cover every dimension value and every warning code generated", () => {
    const ids = file.cases.map((c) => c.id);
    for (const pool of [TIME_FORMS, POSITION_STYLES, EOLS, FRAMES, TRACK_SETS])
      for (const a of pool) expect(ids.some((id) => id.split(":")[2]!.split(".").includes(a.id)), a.id).toBe(true);
    const codes = new Set(file.cases.flatMap((c) => c.expected.warnings.map((w) => w.code)));
    expect([...codes].sort()).toEqual(["no-start-at-zero", "time-duplicate", "time-missing"]);
  });
});
