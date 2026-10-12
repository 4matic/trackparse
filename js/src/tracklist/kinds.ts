/** R13.3 line kinds and R13.4.2 candidate lines. */
import { headerWordsData, markerWordsData } from "../data.generated.js";
import { stringList } from "../tables.js";
import type { ParsedTrack } from "../types.js";
import { asciiLower, inVocab, isWhitespace, PhraseTable, splitWords } from "../words.js";

const headerWords: ReadonlySet<string> = new Set(headerWordsData.words);
let defaultMarkers: PhraseTable<true> | null = null;

/**
 * `data/marker-words.json` plus `options.markerWords` (R13.10). Entries are split into words the
 * same way as the track part (R13.3), so `shout-out` matches the words `shout`, `out`.
 */
export function markerTable(extra: unknown): PhraseTable<true> {
  const more = stringList(extra);
  const entry = (w: string) => [markerWords(w).join(" "), true] as const;
  if (more.length === 0) {
    defaultMarkers ??= new PhraseTable(markerWordsData.words.map(entry));
    return defaultMarkers;
  }
  return new PhraseTable([...markerWordsData.words, ...more].map(entry));
}

const isMarkerBreak = (ch: string) => ch === "/" || ch === "-" || isWhitespace(ch);
const isBracket = (ch: string | undefined) =>
  ch === "(" || ch === ")" || ch === "[" || ch === "]" || ch === "{" || ch === "}";

/**
 * R13.3 marker words: split on R0.2 whitespace, `/` and `-`, then strip leading and trailing
 * `()[]{}` from each piece; empty pieces are dropped. Word keys (R0.3) are applied on matching.
 */
export function markerWords(text: string): string[] {
  const out: string[] = [];
  let current = "";
  const flush = () => {
    let a = 0;
    let b = current.length;
    while (a < b && isBracket(current[a])) a++;
    while (b > a && isBracket(current[b - 1])) b--;
    if (b > a) out.push(current.slice(a, b));
    current = "";
  };
  for (const ch of text) {
    if (isMarkerBreak(ch)) flush();
    else current += ch;
  }
  flush();
  return out;
}

/** R13.3.1 */
export function isBlank(raw: string): boolean {
  for (let i = 0; i < raw.length; i++) if (!isWhitespace(raw[i])) return false;
  return true;
}

function trimWs(s: string): string {
  let a = 0;
  let b = s.length;
  while (a < b && isWhitespace(s[a])) a++;
  while (b > a && isWhitespace(s[b - 1])) b--;
  return s.slice(a, b);
}

/** R13.3.3 link: `http://`, `https://` or `www.` (ASCII case-insensitive). */
export function isLink(rest: string): boolean {
  const t = asciiLower(trimWs(rest).slice(0, 8));
  return t.startsWith("http://") || t.startsWith("https://") || t.startsWith("www.");
}

/** R13.3.3 header: ends with `:`, or a header word followed by at most one more word. */
export function isHeader(rest: string): boolean {
  const t = trimWs(rest);
  if (t.endsWith(":")) return true;
  const words = splitWords(t);
  const first = words[0];
  return first !== undefined && words.length <= 2 && inVocab(first, headerWords);
}

/** R13.3: any marker word (or multi-word entry) among the track part's marker words. */
export function hasMarkerWord(trackPart: string, markers: PhraseTable<true>): boolean {
  const words = markerWords(trackPart);
  for (let i = 0; i < words.length; i++) if (markers.matchAt(words, i)) return true;
  return false;
}

/**
 * R13.3: the track part has a separator: it is not empty after R1 (`empty`), its parse did not
 * warn `noSeparator`, and the artist side is not empty (at least one primary artist, or
 * `flags.unknownArtist`).
 */
export function hasSeparator(track: ParsedTrack, empty: boolean): boolean {
  if (empty || track.warnings.includes("noSeparator")) return false;
  return track.flags.unknownArtist || track.artists.some((a) => a.role === "primary");
}
