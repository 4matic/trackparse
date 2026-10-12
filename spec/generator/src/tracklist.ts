/**
 * Tracklist model (SPEC R13): a parseTracklist case is a TracklistComposition of atoms
 * (time form, lead, gap, position style, line endings, extra line kinds, time order, track
 * parts). `renderTracklist()` builds the multi-line input and the expected (partial) Tracklist
 * *from the atoms alone*; the per-line ParsedTrack comes from the single-line model
 * (`render()` in model.ts). `tracklistProblem()` rejects compositions whose expected output
 * would depend on rules the atoms do not encode, citing the SPEC rule.
 */
import {
  type EolAtom,
  type FrameAtom,
  type FrameLine,
  type GapAtom,
  type LeadAtom,
  type PositionStyleAtom,
  type TimeFormAtom,
  type TimeOrder,
  type TrackAtom,
  modeById,
  sepById,
} from "./atoms.js";
import { DATA } from "./data.js";
import { type Composition, type Rendered, problem, render, single } from "./model.js";
import { wordMatches, words } from "./text.js";

export interface TracklistComposition {
  time: TimeFormAtom;
  lead: LeadAtom;
  gap: GapAtom;
  pos: PositionStyleAtom;
  eol: EolAtom;
  frame: FrameAtom;
  order: TimeOrder;
  /** The track parts, in line order. */
  tracks: TrackAtom[];
}

export type LineKind = "track" | "marker" | "header" | "link" | "note" | "blank";

export interface ExpTime {
  raw: string;
  seconds: number;
}

export interface ExpEntry {
  line: number;
  kind: LineKind;
  eol: string;
  raw: string;
  parts: { lead: string; time: string; gap: string; rest: string };
  time: ExpTime | null;
  track: Record<string, unknown> | null;
}

export interface ExpWarning {
  code: string;
  line: number;
  severity: "info" | "warning";
  suggestion: null;
}

/** Internal per-line plan, before serialization. */
interface Planned {
  frame: FrameLine;
  /** Text after the gap (or the whole raw when untimed). */
  rest: string;
  /** Bare position candidate number (R13.5), if any. */
  bare: number | null;
  /** Whether the line carries a time. */
  timed: boolean;
}

// ---------------------------------------------------------------- track parts

/**
 * The single-line composition of a track part. Tracklist mode (R13.6) differs from `clean` only
 * in R6.10 (no R6.2 peel after the first spaced dash, tight-dash rules without one) and in
 * skipping R2's youtube/filename steps. A spaced-dash track part with bracketed versions and no
 * dash suffix touches none of those, so the clean-mode expectation is the tracklist one.
 */
export function trackComposition(t: TrackAtom): Composition {
  return {
    mode: modeById("clean"),
    prefix: null,
    main: single(t.artist),
    feat: null,
    sep: sepById("hyphen"),
    title: t.title,
    producer: null,
    versions: t.version
      ? [{ atom: t.version.atom, delim: t.version.delim, credits: t.version.remixer ? [single(t.version.remixer)] : [] }]
      : [],
    extras: [],
    dashYear: null,
    junk: [],
    splitAnd: "auto",
    transform: null,
  };
}

/** Why a track atom cannot be a tracklist track part, or null. */
export function trackProblem(t: TrackAtom): string | null {
  const c = trackComposition(t);
  const p = problem(c);
  if (p) return p;
  if (/^[0-9]/.test(t.artist.text)) return "artist starts with a digit (R4.2 / R13.5)";
  const r = render(c, "tracklist");
  const text = r.input;
  if (text.split(" - ").length !== 2) return "exactly one spaced dash (R6.10)";
  if (/^\s|\s$/.test(text)) return "untrimmed";
  // R13.3 untimed lines: never a link or a header.
  if (/^(https?:\/\/|www\.)/i.test(text)) return "looks like a link";
  if (text.endsWith(":")) return "ends with ':' (header)";
  if (DATA.headerWords.some((h) => wordMatches(words(text)[0]!, h))) return "first word is a header word";
  // R13.2: never a time at the start of the line.
  if (/^[0-9]+:/.test(text)) return "starts like a time";
  return null;
}

function trackExpectation(t: TrackAtom, position: { raw: string; number: number } | null): Record<string, unknown> {
  const r = render(trackComposition(t), "tracklist");
  return { position, artists: r.expected.artists, title: r.expected.title, versions: r.expected.versions };
}

function trackRules(t: TrackAtom): string[] {
  // R6.3 → R6.10 (tracklist mode keeps the first spaced dash as the separator).
  return render(trackComposition(t), "tracklist").rules.filter((x) => x !== "R6.3" && x !== "R6.1");
}

// ---------------------------------------------------------------- times

function timeAt(tf: TimeFormAtom, slot: number): ExpTime {
  const whole = tf.steps[slot]!;
  const frac = tf.fractions ? tf.fractions[slot % tf.fractions.length]! : null;
  return { raw: tf.fmt!(whole) + (frac?.text ?? ""), seconds: whole + (frac?.value ?? 0) };
}

/** Times of the timed lines in order (R13.2.5), per the time order atom. */
export function timesFor(tf: TimeFormAtom, order: TimeOrder, count: number): ExpTime[] {
  const out: ExpTime[] = [];
  for (let k = 0; k < count; k++) out.push(timeAt(tf, order === "late" ? k + 1 : k));
  if (order === "dup" && count >= 2) out[count - 1] = { ...out[count - 2]! };
  return out;
}

// ---------------------------------------------------------------- planning

function isTimed(c: TracklistComposition, fl: FrameLine, trackIndex: number): boolean {
  if (c.time.fmt === null) return false;
  if (fl.kind === "marker") return fl.timed;
  if (fl.kind !== "track") return false;
  return !(c.order === "missing" && trackIndex === c.tracks.length - 1);
}

function plan(c: TracklistComposition): Planned[] {
  const out: Planned[] = [];
  for (const fl of c.frame.lines(c.tracks.length)) {
    if (fl.kind === "track") {
      const t = c.tracks[fl.index]!;
      const n = c.pos.first + fl.index;
      const posText = c.pos.raw ? `${c.pos.raw(n)}${c.pos.after}` : "";
      const part = render(trackComposition(t), "tracklist").input;
      out.push({ frame: fl, rest: posText + part, bare: c.pos.bare ? n : null, timed: isTimed(c, fl, fl.index) });
    } else {
      out.push({ frame: fl, rest: fl.text, bare: null, timed: isTimed(c, fl, -1) });
    }
  }
  return out;
}

function eolsFor(e: EolAtom, count: number): string[] {
  return Array.from({ length: count }, (_, i) => (i === count - 1 && !e.final ? "" : e.cycle[i % e.cycle.length]!));
}

/** R13.4.2 candidate line: non-blank with a time or a spaced dash. */
function isCandidate(p: Planned, raw: string): boolean {
  return p.frame.kind !== "blank" && (p.timed || raw.includes(" - "));
}

// ---------------------------------------------------------------- constraints

/** Every reason this tracklist composition is unsafe/ambiguous; null = OK. */
export function tracklistProblem(c: TracklistComposition): string | null {
  const n = c.tracks.length;
  if (n < c.frame.minTracks) return "frame needs more track lines";
  if (new Set(c.tracks.map((t) => t.id)).size !== n) return "repeated track";
  for (const t of c.tracks) {
    const p = trackProblem(t);
    if (p) return `track ${t.id}: ${p}`;
  }
  const lines = plan(c);
  if (lines.length > 6) return "more than 6 lines";
  const timedCount = lines.filter((l) => l.timed).length;
  if (c.time.fmt !== null) {
    if (c.time.steps.length < timedCount + 1) return "time form has too few steps";
    if (c.order === "dup" && timedCount < 2) return "time-duplicate needs two timed lines";
    if (c.order === "missing" && timedCount < 1) return "time-missing needs another timed line (R13.7)";
  }
  const raws = lines.map((l) => rawOf(c, l));
  const eols = eolsFor(c.eol, lines.length);
  // R13.1.1: an empty last line without a terminator is not an entry.
  if (raws[raws.length - 1] === "" && eols[eols.length - 1] === "") return "empty unterminated last line";
  // R13.1.1: CR followed by an empty line ending in LF/CRLF would read as one CRLF.
  for (let i = 0; i + 1 < lines.length; i++)
    if (eols[i] === "\r" && raws[i + 1] === "" && eols[i + 1]!.startsWith("\n")) return "CR + empty line + LF";
  // R13.5: every bare position needs a candidate neighbour with N ± 1. Without one the digits
  // stay in the track part (R4.2 in tracklist mode works like clean, so they join the artist);
  // the track atoms' expectations do not model that, so such compositions are rejected.
  const cands = lines.map((l, i) => (isCandidate(l, raws[i]!) ? i : -1)).filter((i) => i >= 0);
  for (let k = 0; k < cands.length; k++) {
    const b = lines[cands[k]!]!.bare;
    if (b === null) continue;
    const near = [cands[k - 1], cands[k + 1]].filter((i): i is number => i !== undefined).map((i) => lines[i]!.bare);
    if (!near.some((x) => x !== null && Math.abs(x - b) === 1)) return "bare position without a neighbour (R13.5)";
  }
  // Non-track lines: their kind must follow from R13.3 without parsing.
  for (const l of lines) {
    if (l.frame.kind === "track") continue;
    const ws = words(l.rest.trim());
    const hasMarkerWord = ws.some((w) => DATA.markerWords.some((m) => wordMatches(w, m)));
    if (l.rest.includes("-")) return "dash in a non-track line";
    if (l.frame.kind === "marker" && !hasMarkerWord) return "marker without a marker word";
    if ((l.frame.kind === "note" || l.frame.kind === "link" || l.frame.kind === "header") && hasMarkerWord)
      return "marker word in a non-marker line";
    const headerLike = l.rest.trim().endsWith(":") || (ws.length > 0 && ws.length <= 2 && DATA.headerWords.some((h) => wordMatches(ws[0]!, h)));
    if ((l.frame.kind === "header") !== headerLike) return "header test mismatch";
  }
  return null;
}

function rawOf(c: TracklistComposition, l: Planned): string {
  // Times are filled in later; the shape (lead/gap present or not) is all constraints need.
  return l.timed ? `${c.lead.text}0:00${c.lead.close}${c.gap.text}${l.rest}` : l.rest;
}

// ---------------------------------------------------------------- rendering

export interface RenderedTracklist {
  input: string;
  entries: ExpEntry[];
  warnings: ExpWarning[];
  stats: Record<string, number | null>;
}

export function expectTracklist(c: TracklistComposition): RenderedTracklist {
  const lines = plan(c);
  const eols = eolsFor(c.eol, lines.length);
  const times = c.time.fmt === null ? [] : timesFor(c.time, c.order, lines.filter((l) => l.timed).length);
  let slot = 0;
  const entries: ExpEntry[] = lines.map((l, i) => {
    const time = l.timed ? times[slot++]! : null;
    const parts = time
      ? { lead: c.lead.text, time: time.raw, gap: c.lead.close + c.gap.text, rest: l.rest }
      : { lead: "", time: "", gap: "", rest: l.rest };
    const raw = parts.lead + parts.time + parts.gap + parts.rest;
    let track: Record<string, unknown> | null = null;
    const fl = l.frame;
    if (fl.kind === "track") {
      const n = c.pos.first + fl.index;
      track = trackExpectation(c.tracks[fl.index]!, c.pos.raw ? { raw: c.pos.raw(n), number: n } : null);
    } else if (fl.kind === "marker") {
      // R13.4.1: the marker text alone, no separator → R6.8, the whole text is the title.
      track = { position: null, artists: [], title: fl.text, versions: [] };
    }
    return { line: i + 1, kind: fl.kind, eol: eols[i]!, raw, parts, time, track };
  });

  // R13.7, in line order. The compositions allow at most one warning per line.
  const warnings: ExpWarning[] = [];
  const anyTimed = entries.some((e) => e.time !== null);
  let prev: number | null = null;
  let first = true;
  for (const e of entries) {
    if (e.time) {
      if (first && e.time.seconds !== 0) warnings.push(warn("no-start-at-zero", e.line, "info"));
      if (prev !== null && e.time.seconds === prev) warnings.push(warn("time-duplicate", e.line, "warning"));
      if (prev !== null && e.time.seconds < prev) throw new Error("time-out-of-order is never generated");
      prev = e.time.seconds;
      first = false;
    } else if (e.kind === "track" && anyTimed) {
      warnings.push(warn("time-missing", e.line, "warning"));
    }
  }
  const lineSet = new Set(warnings.map((w) => w.line));
  if (lineSet.size !== warnings.length) throw new Error("two warnings on one line are never generated");

  const timed = entries.filter((e) => e.time !== null);
  const stats = {
    lines: entries.length,
    tracks: entries.filter((e) => e.kind === "track").length,
    markers: entries.filter((e) => e.kind === "marker").length,
    timed: timed.length,
    firstTime: timed[0]?.time!.seconds ?? null,
    lastTime: timed[timed.length - 1]?.time!.seconds ?? null,
  };
  const input = entries.map((e) => e.raw + e.eol).join("");
  return { input, entries, warnings, stats };
}

function warn(code: string, line: number, severity: ExpWarning["severity"]): ExpWarning {
  return { code, line, severity, suggestion: null };
}

function rulesOf(c: TracklistComposition, r: RenderedTracklist): string[] {
  const s = new Set<string>(["R13.1.1", "R13.1.2", "R13.1.3", "R13.3", "R13.8"]);
  if (c.time.fmt !== null) for (const x of ["R13.2.1", "R13.2.3", "R13.2.5", ...c.time.rules]) s.add(x);
  if (c.tracks.length) {
    s.add("R13.4.1");
    s.add("R6.10");
  }
  c.pos.rules.forEach((x) => s.add(x));
  c.frame.rules.forEach((x) => s.add(x));
  for (const t of c.tracks) trackRules(t).forEach((x) => s.add(x));
  if (r.warnings.length) s.add("R13.7");
  return [...s].sort(ruleCmp);
}

function ruleCmp(a: string, b: string): number {
  const pa = a.slice(1).split(".").map((x) => parseInt(x, 10));
  const pb = b.slice(1).split(".").map((x) => parseInt(x, 10));
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const d = (pa[i] ?? -1) - (pb[i] ?? -1);
    if (d !== 0) return d;
  }
  return a < b ? -1 : a > b ? 1 : 0;
}

export function renderTracklist(c: TracklistComposition): Rendered {
  const r = expectTracklist(c);
  return {
    input: r.input,
    options: {},
    expected: { entries: r.entries, warnings: r.warnings, stats: r.stats },
    rules: rulesOf(c, r),
    tags: ["tracklist", `time-${c.time.id}`, `frame-${c.frame.id}`, `order-${c.order}`],
  };
}
