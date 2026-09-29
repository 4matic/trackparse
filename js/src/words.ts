/**
 * Low-level text helpers shared by every rule: the R0.2 whitespace set, R0.3 word keys,
 * R0.4 dedup keys, R0.6 letters/digits and token-sequence (phrase) matching.
 */

/** R0.2: the exact whitespace code-point set. Never use `\s`. */
export function isWhitespaceCode(cp: number): boolean {
  return (
    (cp >= 0x09 && cp <= 0x0d) ||
    cp === 0x20 ||
    cp === 0x85 ||
    cp === 0xa0 ||
    cp === 0x1680 ||
    (cp >= 0x2000 && cp <= 0x200a) ||
    cp === 0x2028 ||
    cp === 0x2029 ||
    cp === 0x202f ||
    cp === 0x205f ||
    cp === 0x3000
  );
}

export function isWhitespace(ch: string | undefined): boolean {
  return ch !== undefined && ch.length > 0 && isWhitespaceCode(ch.charCodeAt(0));
}

/** R0.6: ASCII digits only. */
export function isDigit(ch: string | undefined): boolean {
  return ch !== undefined && ch.length === 1 && ch >= "0" && ch <= "9";
}

export function isAllDigits(s: string): boolean {
  if (s.length === 0) return false;
  for (let i = 0; i < s.length; i++) if (!isDigit(s[i])) return false;
  return true;
}

const ALPHABETIC = /^\p{Alphabetic}$/u;

/** R0.6: Unicode Alphabetic, tested on the code point starting at `index`. */
export function isLetterAt(s: string, index: number): boolean {
  const cp = s.codePointAt(index);
  if (cp === undefined) return false;
  return ALPHABETIC.test(String.fromCodePoint(cp));
}

/** ASCII-only lowercase (A–Z → a–z). */
export function asciiLower(s: string): string {
  let out = "";
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    out += c >= 65 && c <= 90 ? String.fromCharCode(c + 32) : s[i];
  }
  return out;
}

const KEY_STRIPPABLE = ".,!?:;";

/** R0.3: the lowercased word with one trailing `. , ! ? : ;` removed. */
export function stripKeyPunct(lower: string): string {
  const last = lower[lower.length - 1];
  return last !== undefined && KEY_STRIPPABLE.includes(last) ? lower.slice(0, -1) : lower;
}

/** R0.3 word key (stripped form). */
export function wordKey(word: string): string {
  return stripKeyPunct(asciiLower(word));
}

/**
 * R0.3: does `word` match the vocabulary token `token`? The unstripped lowercase form is
 * compared first so entries ending in punctuation (`feat.`) match as written.
 */
export function wordMatches(word: string, token: string): boolean {
  const lower = asciiLower(word);
  return lower === token || stripKeyPunct(lower) === token;
}

export function inVocab(word: string, vocab: ReadonlySet<string>): boolean {
  const lower = asciiLower(word);
  return vocab.has(lower) || vocab.has(stripKeyPunct(lower));
}

export function lookupVocab<V>(word: string, vocab: ReadonlyMap<string, V>): V | undefined {
  const lower = asciiLower(word);
  return vocab.get(lower) ?? vocab.get(stripKeyPunct(lower));
}

/** Collapse runs of R0.2 whitespace to one U+0020 and trim. */
export function collapseSpaces(s: string): string {
  let out = "";
  let pendingSpace = false;
  for (const ch of s) {
    if (isWhitespaceCode(ch.codePointAt(0) ?? 0)) {
      pendingSpace = out.length > 0;
    } else {
      if (pendingSpace) out += " ";
      pendingSpace = false;
      out += ch;
    }
  }
  return out;
}

/** R0.7: maximal runs of non-whitespace code points. */
export function splitWords(s: string): string[] {
  const words: string[] = [];
  let current = "";
  for (const ch of s) {
    if (isWhitespaceCode(ch.codePointAt(0) ?? 0)) {
      if (current) words.push(current);
      current = "";
    } else {
      current += ch;
    }
  }
  if (current) words.push(current);
  return words;
}

/** A word with its UTF-16 span inside the string it was taken from. */
export interface WordSpan {
  text: string;
  start: number;
  end: number;
}

export function wordSpans(s: string): WordSpan[] {
  const spans: WordSpan[] = [];
  let start = -1;
  for (let i = 0; i <= s.length; i++) {
    const boundary = i === s.length || isWhitespace(s[i]);
    if (boundary) {
      if (start >= 0) spans.push({ text: s.slice(start, i), start, end: i });
      start = -1;
    } else if (start < 0) {
      start = i;
    }
  }
  return spans;
}

const COMBINING_START = 0x300;
const COMBINING_END = 0x36f;

/** R0.4 dedup key. */
export function dedupKey(s: string): string {
  let out = "";
  for (const ch of s.toLowerCase().normalize("NFD")) {
    const cp = ch.codePointAt(0) ?? 0;
    if (cp >= COMBINING_START && cp <= COMBINING_END) continue;
    out += ch;
  }
  return collapseSpaces(out);
}

/** Year word: 4 ASCII digits in 1900–2099 (word key, so `2015,` counts). */
export function yearOf(word: string): number | null {
  const key = wordKey(word);
  if (key.length !== 4 || !isAllDigits(key)) return null;
  const n = Number(key);
  return n >= 1900 && n <= 2099 ? n : null;
}

// ---------------------------------------------------------------------------
// Phrase tables: multi-word vocabulary matched as token sequences, longest first.

export interface PhraseEntry<V> {
  key: string;
  tokens: string[];
  value: V;
}

export interface PhraseMatch<V> {
  entry: PhraseEntry<V>;
  /** Number of words consumed. */
  length: number;
}

export class PhraseTable<V> {
  private readonly byFirst = new Map<string, PhraseEntry<V>[]>();
  private readonly byLast = new Map<string, PhraseEntry<V>[]>();
  private readonly keys = new Map<string, V>();

  constructor(entries: Iterable<readonly [string, V]>) {
    for (const [rawKey, value] of entries) this.add(rawKey, value);
  }

  add(rawKey: string, value: V): void {
    const key = asciiLower(rawKey).trim();
    const tokens = splitWords(key);
    const first = tokens[0];
    const last = tokens[tokens.length - 1];
    if (first === undefined || last === undefined || this.keys.has(key)) return;
    this.keys.set(key, value);
    const entry: PhraseEntry<V> = { key, tokens, value };
    insertSorted(this.byFirst, first, entry);
    insertSorted(this.byLast, last, entry);
  }

  has(key: string): boolean {
    return this.keys.has(key);
  }

  /** Longest entry matching words[start …]. */
  matchAt(words: readonly string[], start: number): PhraseMatch<V> | null {
    const word = words[start];
    if (word === undefined) return null;
    let best: PhraseMatch<V> | null = null;
    for (const entry of candidates(this.byFirst, word)) {
      const n = entry.tokens.length;
      if (best && n <= best.length) continue;
      if (start + n > words.length) continue;
      if (tokensMatch(words, start, entry.tokens)) best = { entry, length: n };
    }
    return best;
  }

  /** Longest entry matching words[… end-1] (end exclusive). */
  matchEnding(words: readonly string[], end: number): PhraseMatch<V> | null {
    const word = words[end - 1];
    if (word === undefined) return null;
    let best: PhraseMatch<V> | null = null;
    for (const entry of candidates(this.byLast, word)) {
      const n = entry.tokens.length;
      if (best && n <= best.length) continue;
      if (end - n < 0) continue;
      if (tokensMatch(words, end - n, entry.tokens)) best = { entry, length: n };
    }
    return best;
  }

  /** The whole word list is exactly one entry. */
  matchWhole(words: readonly string[]): PhraseMatch<V> | null {
    const m = this.matchAt(words, 0);
    return m && m.length === words.length ? m : null;
  }
}

function insertSorted<V>(map: Map<string, PhraseEntry<V>[]>, k: string, e: PhraseEntry<V>): void {
  const list = map.get(k) ?? [];
  list.push(e);
  list.sort((a, b) => b.tokens.length - a.tokens.length);
  map.set(k, list);
}

function candidates<V>(map: Map<string, PhraseEntry<V>[]>, word: string): PhraseEntry<V>[] {
  const lower = asciiLower(word);
  const stripped = stripKeyPunct(lower);
  const a = map.get(lower) ?? [];
  if (stripped === lower) return a;
  const b = map.get(stripped) ?? [];
  return a.length === 0 ? b : b.length === 0 ? a : [...a, ...b];
}

function tokensMatch(words: readonly string[], start: number, tokens: readonly string[]): boolean {
  for (let k = 0; k < tokens.length; k++) {
    const w = words[start + k];
    const t = tokens[k];
    if (w === undefined || t === undefined || !wordMatches(w, t)) return false;
  }
  return true;
}
