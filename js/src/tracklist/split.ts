/** R13.1: line split (LF, CRLF, CR only) with the lossless guarantee. */
import type { Eol } from "./types.js";

export interface Line {
  raw: string;
  eol: Eol;
}

/**
 * R13.1.1: split at LF, CRLF and CR. U+2028/U+2029, U+0085 and other R0.2 characters are not line
 * breaks. A trailing empty segment after the final terminator is not a line; `""` has no lines.
 * R13.1.2: `lines.map(l => l.raw + l.eol).join("") === text`.
 */
export function splitLines(text: string): Line[] {
  const lines: Line[] = [];
  let start = 0;
  for (let i = 0; i < text.length; i++) {
    const c = text.charCodeAt(i);
    if (c === 0x0a) {
      lines.push({ raw: text.slice(start, i), eol: "\n" });
      start = i + 1;
    } else if (c === 0x0d) {
      if (text.charCodeAt(i + 1) === 0x0a) {
        lines.push({ raw: text.slice(start, i), eol: "\r\n" });
        i++;
      } else {
        lines.push({ raw: text.slice(start, i), eol: "\r" });
      }
      start = i + 1;
    }
  }
  if (start < text.length) lines.push({ raw: text.slice(start), eol: "" });
  return lines;
}

export function joinLines(lines: readonly Line[]): string {
  let out = "";
  for (const l of lines) out += l.raw + l.eol;
  return out;
}

/** A line break inside a single-line value (R13.9.5, R13.9.6). */
export function hasLineBreak(s: string): boolean {
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c === 0x0a || c === 0x0d) return true;
  }
  return false;
}
