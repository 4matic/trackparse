/** R13.7 tracklist warnings and the `time-out-of-order` suggestion search. */
import { normalize } from "../normalize.js";
import type { ParsedTrack } from "../types.js";
import { isDigit } from "../words.js";
import { secondsOf } from "./times.js";
import type {
  TimeSuggestion,
  TracklistEntry,
  TracklistTime,
  TracklistWarning,
  TracklistWarningCode,
} from "./types.js";

/** Table order of R13.7; warnings of one line are emitted in this order. */
const ORDER: readonly TracklistWarningCode[] = [
  "time-missing",
  "time-out-of-order",
  "time-duplicate",
  "no-start-at-zero",
  "time-after-audio-end",
  "invalid-time",
  "position-gap",
  "position-duplicate",
  "line-unparsed",
];

export interface WarningInput {
  entries: readonly TracklistEntry[];
  /** R13.2.4 per entry. */
  invalid: readonly boolean[];
  audioSeconds: number | null;
}

interface Slot {
  /** Index of the digit in `raw`. */
  at: number;
  field: number;
  place: number;
  digit: number;
}

/** Outcome of one line's digit search: no candidate, exactly one, or several. */
type Search = { kind: "none" } | { kind: "one"; time: TracklistTime } | { kind: "many" };

/**
 * R13.7 suggestion search on one raw time: change 1, then 2 digits (fraction digits excluded);
 * keep candidates that are valid times (R13.2.2: fields after the first at most 59) with
 * `lo < t` (when `lo` exists) and `t < hi` (when `hi` exists). Only the smallest number of
 * changes with any candidate counts. Bounded and arithmetic: at most 7 digits, 9·7 + 81·21
 * candidates.
 */
export function searchTime(raw: string, lo: number | null, hi: number | null): Search {
  let fracStart = raw.length;
  for (let i = 0; i < raw.length; i++) {
    if (raw[i] === "." || raw[i] === ",") {
      fracStart = i;
      break;
    }
  }
  const frac = raw.slice(fracStart + 1);
  const values: number[] = [0];
  const slots: Slot[] = [];
  for (let i = 0; i < fracStart; i++) {
    const ch = raw[i] as string;
    if (ch === ":") values.push(0);
    else if (isDigit(ch)) {
      const f = values.length - 1;
      values[f] = (values[f] as number) * 10 + Number(ch);
      slots.push({ at: i, field: f, place: 0, digit: Number(ch) });
    }
  }
  // Place value of each digit inside its field.
  for (let k = slots.length - 1, place = 1, field = -1; k >= 0; k--) {
    const slot = slots[k] as Slot;
    if (slot.field !== field) {
      field = slot.field;
      place = 1;
    }
    slot.place = place;
    place *= 10;
  }
  const mult = values.length === 3 ? [3600, 60, 1] : [60, 1];
  const valueWith = (changes: readonly [Slot, number][]): number | null => {
    const v = [...values];
    for (const [slot, d] of changes) {
      v[slot.field] = (v[slot.field] as number) + (d - slot.digit) * slot.place;
    }
    let whole = 0;
    for (let f = 0; f < v.length; f++) {
      if (f > 0 && (v[f] as number) > 59) return null;
      whole += (v[f] as number) * (mult[f] as number);
    }
    const t = secondsOf(whole, frac);
    return (lo === null || lo < t) && (hi === null || t < hi) ? t : null;
  };
  const render = (changes: readonly [Slot, number][]): string => {
    const chars = [...raw];
    for (const [slot, d] of changes) chars[slot.at] = String(d);
    return chars.join("");
  };

  let found: [Slot, number][] | null = null;
  let count = 0;
  for (const slot of slots) {
    for (let d = 0; d <= 9; d++) {
      if (d === slot.digit) continue;
      const change: [Slot, number][] = [[slot, d]];
      if (valueWith(change) === null) continue;
      count++;
      found = change;
    }
  }
  if (count === 0) {
    for (let x = 0; x < slots.length && count < 2; x++) {
      for (let y = x + 1; y < slots.length && count < 2; y++) {
        const a = slots[x] as Slot;
        const b = slots[y] as Slot;
        for (let da = 0; da <= 9 && count < 2; da++) {
          if (da === a.digit) continue;
          for (let db = 0; db <= 9 && count < 2; db++) {
            if (db === b.digit) continue;
            const change: [Slot, number][] = [
              [a, da],
              [b, db],
            ];
            if (valueWith(change) === null) continue;
            count++;
            found = change;
          }
        }
      }
    }
  }
  if (count === 0 || found === null) return { kind: "none" };
  if (count > 1) return { kind: "many" };
  return { kind: "one", time: { raw: render(found), seconds: valueWith(found) as number } };
}

/**
 * R13.7 suggestion for the out-of-order timed entry `k` (index into `times`): search its own time
 * between its neighbours first; only when that finds no candidate at all, search the previous
 * timed entry's time between the one before it and entry `k`.
 */
export function suggestFix(
  times: readonly { line: number; time: TracklistTime }[],
  k: number,
): TimeSuggestion | null {
  const cur = times[k] as { line: number; time: TracklistTime };
  const prev = times[k - 1] as { line: number; time: TracklistTime };
  const next = times[k + 1];
  const own = searchTime(cur.time.raw, prev.time.seconds, next ? next.time.seconds : null);
  if (own.kind === "one") return { line: cur.line, ...own.time };
  if (own.kind === "many") return null;
  const before = times[k - 2];
  const back = searchTime(prev.time.raw, before ? before.time.seconds : null, cur.time.seconds);
  return back.kind === "one" ? { line: prev.line, ...back.time } : null;
}

const unparsed = (t: ParsedTrack) => t.warnings.includes("noSeparator");
/** R13.3: the track part is empty after R1 (a line that is only a time). */
const emptyPart = (t: ParsedTrack) => normalize(t.input).length === 0;

export function tracklistWarnings(input: WarningInput): TracklistWarning[] {
  const { entries, invalid, audioSeconds } = input;
  const perLine: TracklistWarning[][] = entries.map(() => []);
  const add = (i: number, code: TracklistWarningCode, suggestion: TimeSuggestion | null = null) => {
    const line = (entries[i] as TracklistEntry).line;
    const severity = code === "no-start-at-zero" ? "info" : "warning";
    (perLine[i] as TracklistWarning[]).push({ code, line, severity, suggestion });
  };

  const timed: number[] = [];
  entries.forEach((e, i) => {
    if (e.time) timed.push(i);
  });

  entries.forEach((e, i) => {
    if (e.kind === "track" && e.time === null && timed.length > 0) add(i, "time-missing");
    if (invalid[i]) add(i, "invalid-time");
    // R13.3 / R13.7: an empty track part (a time-only line) is unparsed too.
    if (e.kind === "track" && e.track && (unparsed(e.track) || emptyPart(e.track))) {
      add(i, "line-unparsed");
    }
  });

  const times = timed.map((i) => {
    const e = entries[i] as TracklistEntry;
    return { line: e.line, time: e.time as TracklistTime };
  });
  timed.forEach((i, k) => {
    const t = (times[k] as { time: TracklistTime }).time;
    if (k === 0 && t.seconds !== 0) add(i, "no-start-at-zero");
    if (k > 0) {
      const p = (times[k - 1] as { time: TracklistTime }).time.seconds;
      if (t.seconds < p) add(i, "time-out-of-order", suggestFix(times, k));
      else if (t.seconds === p) add(i, "time-duplicate");
    }
    if (audioSeconds !== null && t.seconds > audioSeconds) add(i, "time-after-audio-end");
  });

  let prevPos: number | null = null;
  entries.forEach((e, i) => {
    const pos = e.kind === "track" ? e.track?.position : null;
    if (!pos) return;
    if (prevPos !== null) {
      if (pos.number === prevPos) add(i, "position-duplicate");
      else if (pos.number !== prevPos + 1) add(i, "position-gap");
    }
    prevPos = pos.number;
  });

  const rank = (c: TracklistWarningCode) => ORDER.indexOf(c);
  return perLine.flatMap((ws) => ws.sort((a, b) => rank(a.code) - rank(b.code)));
}
