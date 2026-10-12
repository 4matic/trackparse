/** R13.2: the tracklist time scanner (separate from R4.1) and R13.9.2 time formatting. */
import { digitRun } from "../digits.js";
import { isDigit, isWhitespace } from "../words.js";
import type { EntryParts, TracklistTime } from "./types.js";

/** The written shape of a time, kept by edits (R13.9.2). */
export interface TimeStyle {
  hours: boolean;
  /** Digits written for the hours (minimum width), 0 without hours. */
  hourWidth: number;
  /** Digits written for the minutes (minimum width). */
  minuteWidth: number;
  /** R13.9.2: two-part time already at 60 minutes or more; it stays two-part when it grows. */
  longMinutes: boolean;
  fracSep: "" | "." | ",";
  fracDigits: number;
}

export interface TimeScan {
  parts: EntryParts;
  time: TracklistTime | null;
  style: TimeStyle | null;
  /** R13.2.4: a `digits:digits` shape after lead that fails R13.2.2. */
  invalid: boolean;
}

/** R13.2.3 separators after the time. */
function isGapSeparator(ch: string | undefined): boolean {
  return ch === "-" || ch === "–" || ch === "—" || ch === "|" || ch === "." || ch === ":";
}

function skipWhitespace(s: string, from: number): number {
  let i = from;
  while (i < s.length && isWhitespace(s[i])) i++;
  return i;
}

/** R13.2.1 bullets of a line without a time. */
function isBullet(ch: string | undefined): boolean {
  return ch === "-" || ch === "–" || ch === "—" || ch === "*" || ch === "•";
}

/**
 * R13.1.3 / R13.2.1: a line without a time. `lead` holds an optional bullet: R0.2 whitespace, one
 * of `-` `–` `—` `*` `•`, then at least one R0.2 whitespace (all of it); `rest` is the remainder.
 */
function untimed(raw: string, invalid: boolean): TimeScan {
  const at = skipWhitespace(raw, 0);
  let lead = "";
  if (isBullet(raw[at]) && isWhitespace(raw[at + 1]))
    lead = raw.slice(0, skipWhitespace(raw, at + 1));
  return {
    parts: { lead, time: "", gap: "", rest: raw.slice(lead.length) },
    time: null,
    style: null,
    invalid,
  };
}

/** `int.frac` parsed as one decimal literal, so every port gets the same double. */
export function secondsOf(whole: number, frac: string): number {
  return frac.length === 0 ? whole : Number(`${whole}.${frac}`);
}

interface Shape {
  /** The digit runs between the colons (2 or more). */
  parts: string[];
  end: number;
}

/** R13.2.4: the maximal `digits(:digits)+` run at `at`, or null. */
function shapeAt(s: string, at: number): Shape | null {
  const na = digitRun(s, at);
  if (na === 0 || s[at + na] !== ":" || !isDigit(s[at + na + 1])) return null;
  const parts = [s.slice(at, at + na)];
  let end = at + na;
  while (s[end] === ":" && isDigit(s[end + 1])) {
    const n = digitRun(s, end + 1);
    parts.push(s.slice(end + 1, end + 1 + n));
    end += 1 + n;
  }
  return { parts, end };
}

/** R13.2.2 validity: whole seconds of `A:B` / `A:B:C` (no other part count), or null. */
export function shapeSeconds(parts: readonly string[]): number | null {
  if (parts.length < 2 || parts.length > 3) return null;
  const [a, b, c] = parts as [string, string, string | undefined];
  if (c !== undefined) {
    if (a.length < 1 || a.length > 2 || b.length !== 2 || c.length !== 2) return null;
    const m = Number(b);
    const sec = Number(c);
    if (m > 59 || sec > 59) return null;
    return Number(a) * 3600 + m * 60 + sec;
  }
  if (a.length < 1 || a.length > 3 || b.length !== 2) return null;
  const sec = Number(b);
  if (sec > 59) return null;
  return Number(a) * 60 + sec;
}

/** R13.2: lead, time and gap at the start of `raw`; the rest is everything after. */
export function scanTime(raw: string): TimeScan {
  // R13.2.1 lead: whitespace, then an optional `[` / `(`.
  let i = skipWhitespace(raw, 0);
  const closer = raw[i] === "[" ? "]" : raw[i] === "(" ? ")" : null;
  if (closer) i++;
  const leadEnd = i;

  // R13.2.2 time
  const shape = shapeAt(raw, i);
  if (!shape) return untimed(raw, false);
  const whole = shapeSeconds(shape.parts);
  if (whole === null) return untimed(raw, true); // R13.2.4
  let j = shape.end;
  let fracSep: TimeStyle["fracSep"] = "";
  let frac = "";
  const sep = raw[j];
  if (sep === "." || sep === ",") {
    const n = digitRun(raw, j + 1);
    if (n >= 1 && n <= 3) {
      fracSep = sep;
      frac = raw.slice(j + 1, j + 1 + n);
      j += 1 + n;
    }
  }
  const timeEnd = j;

  // R13.2.3 gap
  if (closer) {
    if (raw[j] !== closer) return untimed(raw, false);
    j++;
  } else if (j < raw.length && !isWhitespace(raw[j]) && !isGapSeparator(raw[j])) {
    return untimed(raw, false);
  }
  j = skipWhitespace(raw, j);
  if (isGapSeparator(raw[j])) j = skipWhitespace(raw, j + 1);
  if (raw.startsWith("==>", j)) j = skipWhitespace(raw, j + 3);
  else if (raw.startsWith("=>", j)) j = skipWhitespace(raw, j + 2);

  const timeRaw = raw.slice(leadEnd, timeEnd);
  const hours = shape.parts.length === 3;
  const first = shape.parts[0] as string;
  // R13.9.2 long-minute style: two parts with 60 minutes or more.
  const longMinutes = !hours && Number(first) >= 60;
  return {
    parts: {
      lead: raw.slice(0, leadEnd),
      time: timeRaw,
      gap: raw.slice(timeEnd, j),
      rest: raw.slice(j),
    },
    time: { raw: timeRaw, seconds: secondsOf(whole, frac) },
    style: {
      hours,
      hourWidth: hours ? first.length : 0,
      // R13.9.2: a long-minute time's minimum minute width is min(width, 2), so it never
      // keeps 3 minute digits once it is short again (`125:30` − 7000 s → `08:50`).
      minuteWidth: hours
        ? (shape.parts[1] as string).length
        : longMinutes
          ? Math.min(first.length, 2)
          : first.length,
      longMinutes,
      fracSep,
      fracDigits: frac.length,
    },
    invalid: false,
  };
}

const pad = (n: number, width: number) => String(n).padStart(width, "0");

export interface FormattedTime {
  raw: string;
  upgraded: boolean;
}

/**
 * R13.9.2: write `seconds` (finite, ≥ 0) in `style`. Values are rounded half up to the fraction
 * precision (`floor(v·10^d + 0.5)`). A value of an hour or more without hours is upgraded to
 * `HH:MM:SS` (`H:MM:SS` when `oneDigitHours`), except in long-minute style. Returns null when the value cannot be written as
 * a valid R13.2.2 time (100 hours or more, more than 999 long-style minutes, R13.9.2).
 */
export function formatTime(
  seconds: number,
  style: TimeStyle,
  oneDigitHours: boolean,
): FormattedTime | null {
  const scale = 10 ** style.fracDigits;
  const units = Math.floor(seconds * scale + 0.5);
  if (!Number.isSafeInteger(units) || units < 0) return null;
  const whole = Math.floor(units / scale);
  const fracUnits = units - whole * scale;
  const frac = style.fracDigits > 0 ? style.fracSep + pad(fracUnits, style.fracDigits) : "";
  const sec = whole % 60;
  // The written time must stay valid under R13.2.2 (hours are 1–2 digits).
  if (whole >= 100 * 3600) return null;
  if (style.hours) {
    const h = Math.floor(whole / 3600);
    const m = Math.floor((whole % 3600) / 60);
    return {
      raw: `${pad(h, style.hourWidth)}:${pad(m, style.minuteWidth)}:${pad(sec, 2)}${frac}`,
      upgraded: false,
    };
  }
  if (style.longMinutes) {
    const m = Math.floor(whole / 60);
    if (m > 999) return null; // R13.2.2: two-part minutes are 1–3 digits
    return { raw: `${pad(m, style.minuteWidth)}:${pad(sec, 2)}${frac}`, upgraded: false };
  }
  if (whole >= 3600) {
    const h = Math.floor(whole / 3600);
    const m = Math.floor((whole % 3600) / 60);
    return {
      raw: `${pad(h, oneDigitHours ? 1 : 2)}:${pad(m, 2)}:${pad(sec, 2)}${frac}`,
      upgraded: true,
    };
  }
  const m = Math.floor(whole / 60);
  return { raw: `${pad(m, style.minuteWidth)}:${pad(sec, 2)}${frac}`, upgraded: false };
}
