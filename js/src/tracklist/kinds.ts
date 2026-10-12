/** R13.3 line kinds and R13.4.2 candidate lines. */
import { headerWordsData, markerWordsData } from "../data.generated.js";
import { normalize } from "../normalize.js";
import { stringList } from "../tables.js";
import type { ParsedTrack } from "../types.js";
import { asciiLower, inVocab, isWhitespace, PhraseTable, splitWords } from "../words.js";

const headerWords: ReadonlySet<string> = new Set(headerWordsData.words);
let defaultMarkers: PhraseTable<true> | null = null;

/** `data/marker-words.json` plus `options.markerWords` (R13.10). */
export function markerTable(extra: unknown): PhraseTable<true> {
  const more = stringList(extra);
  if (more.length === 0) {
    defaultMarkers ??= new PhraseTable(markerWordsData.words.map((w) => [w, true] as const));
    return defaultMarkers;
  }
  return new PhraseTable([...markerWordsData.words, ...more].map((w) => [w, true] as const));
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

/** R13.3.2: any word (or multi-word entry) of the track part is a marker word. */
export function hasMarkerWord(trackPart: string, markers: PhraseTable<true>): boolean {
  const words = splitWords(trackPart);
  for (let i = 0; i < words.length; i++) if (markers.matchAt(words, i)) return true;
  return false;
}

/** The track part has an artist/title separator: its parse did not warn `noSeparator`. */
export function hasSeparator(track: ParsedTrack): boolean {
  return !track.warnings.includes("noSeparator") && normalize(track.input).length > 0;
}
