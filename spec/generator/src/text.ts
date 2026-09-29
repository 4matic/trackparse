/**
 * Small text utilities and *conservative safety predicates* for atoms.
 *
 * These are NOT a parser. They answer "could this piece of text possibly be
 * recognised as something other than plain text by some rule of SPEC.md?" and
 * err on the side of "yes" so that ambiguous atoms/compositions are rejected.
 */
import { DATA } from "./data.js";

const TRAILING_PUNCT = ".,!?:;";

export function asciiLower(s: string): string {
  return s.replace(/[A-Z]/g, (c) => c.toLowerCase());
}

/** R0.3 word-key comparison of one input word against one vocabulary word. */
export function wordMatches(word: string, entryWord: string): boolean {
  const lower = asciiLower(word);
  if (lower === entryWord) return true;
  const last = lower.slice(-1);
  if (last && TRAILING_PUNCT.includes(last)) return lower.slice(0, -1) === entryWord;
  return false;
}

/** R0.3 word key (strip one trailing punctuation char). */
export function wordKey(word: string): string {
  const lower = asciiLower(word);
  const last = lower.slice(-1);
  return last && TRAILING_PUNCT.includes(last) ? lower.slice(0, -1) : lower;
}

export function words(s: string): string[] {
  return s.split(" ").filter((w) => w.length > 0);
}

/** R0.4 dedup key. */
export function dedupKey(s: string): string {
  return s
    .toLowerCase()
    .normalize("NFD")
    .replace(/[\u0300-\u036F]/g, "")
    .replace(/\s+/g, " ")
    .trim();
}

export function isYearWord(w: string): boolean {
  if (!/^[0-9]{4}$/.test(w)) return false;
  const n = Number(w);
  return n >= 1900 && n <= 2099;
}

/** Does the vocabulary entry (possibly multi-word) match ws starting at index i? */
function entryAt(ws: string[], i: number, entry: string): number {
  const ew = entry.split(" ");
  if (i + ew.length > ws.length) return 0;
  for (let k = 0; k < ew.length; k++) {
    if (!wordMatches(ws[i + k]!, ew[k]!)) return 0;
  }
  return ew.length;
}

/** Does any entry end exactly at the last word of ws? */
function anyEntryEndsAt(ws: string[], entries: string[]): boolean {
  return entries.some((e) => {
    const n = e.split(" ").length;
    return n <= ws.length && entryAt(ws, ws.length - n, e) === n;
  });
}

function anyEntryStartsAt(ws: string[], entries: string[]): boolean {
  return entries.some((e) => entryAt(ws, 0, e) > 0);
}

const JUNK_AND_GENRES = [...Object.keys(DATA.junkPhrases), ...DATA.genres];

/** R5.1 tokenisation test (greedy, longest first). */
export function tokenizesAsJunk(text: string): boolean {
  const ws = words(text);
  if (ws.length === 0) return false;
  let i = 0;
  let phrases = 0;
  while (i < ws.length) {
    let best = 0;
    for (const p of JUNK_AND_GENRES) best = Math.max(best, entryAt(ws, i, p));
    if (best > 0) {
      i += best;
      phrases++;
      continue;
    }
    // R5.1: connectors and year words may sit between junk phrases (`Official Video 2015`).
    if (DATA.junkConnectors.includes(ws[i]!) || isYearWord(wordKey(ws[i]!))) {
      i++;
      continue;
    }
    return false;
  }
  return phrases > 0;
}

const FLAG_PHRASES = ["explicit", "explicit version", "dirty", "dirty version", "clean", "clean version", "radio clean"];
const ALL_FEAT = [...DATA.featMarkers, ...DATA.bracketOnlyMarkers];
const PRODUCER = [...DATA.producerMarkers];
const HEADS = Object.keys(DATA.heads);
const PREFIX_FORMS = Object.keys(DATA.prefixForms);
const RUN_WORDS = [...Object.keys(DATA.typeCapable), ...DATA.descriptors, ...DATA.genres, ...HEADS];

export function isUnknownToken(s: string): boolean {
  if (DATA.unknownCaseSensitive.includes(s)) return true;
  return DATA.unknownCaseInsensitive.includes(asciiLower(s));
}

/**
 * Conservative: true if `text` (a group's inner text or a bare dash segment) might
 * classify as anything but Unknown under R5.
 */
export function mightClassify(text: string): boolean {
  const ws = words(text);
  if (ws.length === 0) return true;
  if (tokenizesAsJunk(text)) return true;
  if (ws.length >= 2 && DATA.labelSuffixes.some((l) => wordMatches(ws[ws.length - 1]!, l))) return true;
  if (anyEntryStartsAt(ws, ALL_FEAT) || anyEntryStartsAt(ws, PRODUCER)) return true;
  if (FLAG_PHRASES.some((f) => entryAt(ws, 0, f) === ws.length)) return true;
  if (ws.length === 1 && isYearWord(ws[0]!)) return true;
  if (text.includes(" / ") || text.includes("; ")) return true;
  if (anyEntryEndsAt(ws, HEADS)) return true;
  if (anyEntryStartsAt(ws, PREFIX_FORMS)) return true;
  if (ws.length >= 2 && HEADS.some((h) => wordMatches(ws[0]!, h)) && wordMatches(ws[1]!, "by")) return true;
  return false;
}

/** True if some junk phrase is a suffix of `preceding + junk` other than exactly `junk` (R8.4 ambiguity). */
export function trailingJunkAmbiguous(preceding: string[], junkWords: string[]): boolean {
  return memoized(`t:${preceding.join(" ")}|${junkWords.join(" ")}`, () =>
    trailingJunkAmbiguousUncached(preceding, junkWords) ? "y" : null,
  ) !== null;
}

function trailingJunkAmbiguousUncached(preceding: string[], junkWords: string[]): boolean {
  const all = [...preceding, ...junkWords];
  for (const p of Object.keys(DATA.junkPhrases)) {
    const n = p.split(" ").length;
    if (n === junkWords.length) continue;
    if (n <= all.length && entryAt(all, all.length - n, p) === n) return true;
  }
  return false;
}

/** Does text end with a junk phrase (R8.4 test)? */
export function endsWithJunkPhrase(text: string): boolean {
  return anyEntryEndsAt(words(text), Object.keys(DATA.junkPhrases));
}

function hasWord(ws: string[], entries: string[]): boolean {
  for (let i = 0; i < ws.length; i++) for (const e of entries) if (entryAt(ws, i, e) > 0) return true;
  return false;
}

/** Top-level groups of a string (simple balanced scan); null if unbalanced. */
export function topGroups(s: string): { inner: string; open: string }[] | null {
  const pairs: Record<string, string> = { "(": ")", "[": "]", "{": "}" };
  const out: { inner: string; open: string }[] = [];
  const stack: string[] = [];
  let start = -1;
  let open = "";
  for (let i = 0; i < s.length; i++) {
    const c = s[i]!;
    if (pairs[c]) {
      if (stack.length === 0) {
        start = i;
        open = c;
      }
      stack.push(pairs[c]!);
    } else if (")]}".includes(c)) {
      if (stack[stack.length - 1] !== c) return null;
      stack.pop();
      if (stack.length === 0) out.push({ inner: s.slice(start + 1, i).trim(), open });
    }
  }
  return stack.length === 0 ? out : null;
}

export function stripGroups(s: string): string {
  let depth = 0;
  let out = "";
  for (const c of s) {
    if ("([{".includes(c)) depth++;
    else if (")]}".includes(c)) depth--;
    else if (depth === 0) out += c;
  }
  return out;
}

const SPACED_JOINER_WORDS = DATA.spacedJoiners;

/** Word-level reason why a name cannot be used as an atomic credit name, or null. */
const memo = new Map<string, string | null>();
function memoized(key: string, f: () => string | null): string | null {
  if (memo.has(key)) return memo.get(key)!;
  const v = f();
  memo.set(key, v);
  return v;
}

export function artistProblem(name: string, allowGuarded = false): string | null {
  return memoized(`a${allowGuarded ? 1 : 0}:${name}`, () => artistProblemUncached(name, allowGuarded));
}

function artistProblemUncached(name: string, allowGuarded: boolean): string | null {
  if (name !== name.trim() || name.length === 0) return "untrimmed";
  if (/[,;|"()[\]{}_]/.test(name)) return "forbidden-char";
  if (name.includes(" - ")) return "spaced-dash";
  if (/^-|-$/.test(name)) return "edge-dash";
  if (isUnknownToken(name)) return "unknown-token";
  const ws = words(name);
  if (ws.length === 1 && isYearWord(ws[0]!)) return "year";
  if (wordMatches(ws[0]!, "by")) return "leading-by";
  for (let i = 0; i < ws.length; i++) {
    const w = ws[i]!;
    for (const j of SPACED_JOINER_WORDS) {
      const hit = j.caseSensitive ? w === j.raw : wordMatches(w, j.raw);
      if (!hit) continue;
      const next = ws[i + 1];
      const guarded =
        ["&", "and", "+"].includes(j.raw) && next !== undefined && DATA.noSplitBefore.some((g) => wordMatches(next, g));
      if (!(allowGuarded && guarded)) return `joiner:${j.raw}`;
    }
  }
  if (hasWord(ws, ALL_FEAT)) return "feat-marker";
  if (hasWord(ws, PRODUCER)) return "producer-marker";
  if (/^['"].*['"]$/.test(name)) return "wrapped-quotes";
  return null;
}

/** Can this name be the credit span of a head-scan version (`<name> Remix`)? */
export function remixerProblem(name: string): string | null {
  return memoized(`r:${name}`, () => remixerProblemUncached(name));
}

function remixerProblemUncached(name: string): string | null {
  const p = artistProblem(name, true);
  if (p) return p;
  const ws = words(name);
  const last = ws[ws.length - 1]!;
  if (isYearWord(last)) return "ends-year";
  if (/'s?$/i.test(last)) return "possessive";
  if (anyEntryEndsAt(ws, RUN_WORDS)) return "ends-run-word";
  if (mightClassify(name)) return "classifies";
  return null;
}

/** Why a title cannot be used as a plain title, or null. */
export function titleProblem(title: string): string | null {
  if (title !== title.trim() || title.length === 0) return "untrimmed";
  if (/[|_"]/.test(title)) return "forbidden-char";
  if (title.includes(" - ") || /^-|-$/.test(title)) return "dash";
  if (/^'.*'$/.test(title)) return "wrapped-quotes";
  if (isUnknownToken(title)) return "unknown-token";
  const groups = topGroups(title);
  if (groups === null) return "unbalanced";
  for (const g of groups) if (mightClassify(g.inner)) return "group-classifies";
  const bare = stripGroups(title).replace(/\s+/g, " ").trim();
  const ws = words(title);
  if (hasWord(ws, ALL_FEAT.filter((m) => !DATA.bracketOnlyMarkers.includes(m)))) return "feat-marker";
  if (hasWord(ws, PRODUCER) || hasWord(ws, ["produced by"])) return "producer-marker";
  if (bare.length > 0 && mightClassify(bare)) return "classifies";
  if (mightClassify(title)) return "classifies";
  if (endsWithJunkPhrase(title)) return "trailing-junk";
  if (hasWord(ws, DATA.bracketOnlyMarkers) && wordMatches(ws[0]!, "with")) return "leading-with";
  return null;
}

export function startsWithDigitRun(s: string): RegExpMatchArray | null {
  return s.match(/^([0-9]{1,3})( |$)/);
}
