/** R4: leading timestamp and track-position prefixes. */
import { type Ctx, matchKnownAt } from "./context.js";
import { hasSpacedDash, scan } from "./scanner.js";
import type { Mode, Position, Timestamp } from "./types.js";
import { isDigit } from "./words.js";

export interface PrefixResult {
  rest: string;
  /** Absolute offset of `rest`. */
  base: number;
  timestamp: Timestamp | null;
  position: Position | null;
}

function digitRun(s: string, from: number): number {
  let i = from;
  while (isDigit(s[i])) i++;
  return i - from;
}

function twoDigitsUpTo59(s: string, at: number): number | null {
  if (!isDigit(s[at]) || !isDigit(s[at + 1]) || isDigit(s[at + 2])) return null;
  const n = Number(s.slice(at, at + 2));
  return n <= 59 ? n : null;
}

interface TimeMatch {
  raw: string;
  seconds: number;
  end: number;
}

/** `H:MM:SS`, `HH:MM:SS`, `M:SS`, `MM:SS` starting at `at`. */
function matchTime(s: string, at: number): TimeMatch | null {
  const lead = digitRun(s, at);
  if (lead < 1 || lead > 2 || s[at + lead] !== ":") return null;
  const first = Number(s.slice(at, at + lead));
  const mid = twoDigitsUpTo59(s, at + lead + 1);
  if (mid === null) return null;
  const afterMid = at + lead + 3;
  if (s[afterMid] === ":") {
    const sec = twoDigitsUpTo59(s, afterMid + 1);
    if (sec !== null) {
      const end = afterMid + 3;
      return { raw: s.slice(at, end), seconds: first * 3600 + mid * 60 + sec, end };
    }
  }
  if (lead === 2 && first > 59) return null; // MM is 00–59
  return { raw: s.slice(at, afterMid), seconds: first * 60 + mid, end: afterMid };
}

/** After a prefix: skip one space, then an optional `- ` (spaced dash). */
function skipSpaceAndDash(s: string, at: number): number {
  let i = at;
  if (s[i] === " ") i++;
  if (s[i] === "-" && s[i + 1] === " ") i += 2;
  return i;
}

/** R4.1 */
function matchTimestamp(s: string): { ts: Timestamp; end: number } | null {
  const bracket = s[0] === "[" ? "]" : s[0] === "(" ? ")" : null;
  const t = matchTime(s, bracket ? 1 : 0);
  if (!t) return null;
  let end = t.end;
  if (bracket) {
    if (s[end] !== bracket) return null;
    end++;
  }
  if (end < s.length && s[end] !== " ") return null;
  return { ts: { raw: t.raw, seconds: t.seconds }, end: skipSpaceAndDash(s, end) };
}

function containsSpacedDash(s: string): boolean {
  return hasSpacedDash(scan(s).skel.text);
}

/** R4.2: end offset of the position prefix, or null. Forms are tried (a), (b), (c), (d)/(e). */
function matchPosition(s: string, mode: Mode): number | null {
  const n = digitRun(s, 0);
  if (n < 1 || n > 3) return null;
  const c = s[n];
  const zeroPadded = s[0] === "0" && n >= 2;
  // (a) `01. `, `1) `
  if ((c === "." || c === ")") && (s[n + 1] === " " || n + 1 === s.length)) {
    return skipSpaceAndDash(s, n + 1);
  }
  // (b) `01 `: a position in every mode.
  if (zeroPadded && c === " ") return skipSpaceAndDash(s, n);
  // (c) `3 - Artist - Title`; in filename mode `N - ` is always a position (`311 - Amber.mp3`).
  if (c === " " && s[n + 1] === "-" && s[n + 2] === " ") {
    if (mode === "filename" || containsSpacedDash(s.slice(n + 3))) return n + 3;
    return null;
  }
  if (mode === "filename") {
    // (e) `01-Track`, `03_name`, `01.Track`
    if ((c === "-" || c === "_" || c === ".") && s[n + 1] !== undefined && s[n + 1] !== " ") {
      return n + 1;
    }
    // (e) `3 My Song.mp3`: only when the remainder has no spaced dash.
    if (c === " " && !containsSpacedDash(s.slice(n + 1))) return skipSpaceAndDash(s, n);
  }
  // (d) `2 Unlimited`, `50 Cent`: never a position outside filename mode.
  return null;
}

export function stripPrefixes(str: string, base: number, mode: Mode, ctx: Ctx): PrefixResult {
  let rest = str;
  let offset = base;
  let timestamp: Timestamp | null = null;
  let position: Position | null = null;

  const ts = matchTimestamp(rest);
  if (ts) {
    timestamp = ts.ts;
    rest = rest.slice(ts.end);
    offset += ts.end;
  }
  if (matchKnownAt(rest, 0, ctx, "-") === 0) {
    const end = matchPosition(rest, mode);
    if (end !== null && end < rest.length) {
      const raw = rest.slice(0, digitRun(rest, 0));
      position = { raw, number: Number(raw) };
      rest = rest.slice(end);
      offset += end;
    }
  }
  return { rest, base: offset, timestamp, position };
}
