/** ASCII digit scanning shared by R4.1 (prefix.ts) and the tracklist time scanner (R13.2). */
import { isDigit } from "./words.js";

/** Length of the run of ASCII digits starting at `from`. */
export function digitRun(s: string, from: number): number {
  let i = from;
  while (isDigit(s[i])) i++;
  return i - from;
}

/** Exactly two digits (not followed by a third) with a value of at most 59, else null. */
export function twoDigitsUpTo59(s: string, at: number): number | null {
  if (!isDigit(s[at]) || !isDigit(s[at + 1]) || isDigit(s[at + 2])) return null;
  const n = Number(s.slice(at, at + 2));
  return n <= 59 ? n : null;
}
