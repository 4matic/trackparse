/** R5: classification of a group's inner text (also used for dash suffixes and pipe segments). */
import type { Ctx } from "./context.js";
import { type Credit, type ListOptions, type PositionedYear, splitCredits } from "./credits.js";
import { scan } from "./scanner.js";
import { isUnknownToken, type RunInfo } from "./tables.js";
import type { ArtistSource, JunkKind, VersionDelimiter, VersionType } from "./types.js";
import {
  asciiLower,
  dedupKey,
  inVocab,
  isAllDigits,
  splitWords,
  type WordSpan,
  wordKey,
  wordSpans,
  yearOf,
} from "./words.js";

export interface VersionDraft {
  type: VersionType;
  raw: string;
  artists: Credit[];
  modifiers: string[];
  descriptor: string | null;
  year: number | null;
  unknownArtist: boolean;
  delimiter: VersionDelimiter;
  pos: number;
  /** R5.7.2 ambiguousMixCredit; emitted only when the version is used. */
  ambiguousMix: boolean;
  /** Recognised by the R5.7.3 prefix form (R6.2 condition (ii) does not apply). */
  prefixForm: boolean;
}

export type Classification =
  | { kind: "junk"; junkKind: JunkKind }
  | { kind: "feat"; credits: Credit[]; unknown: boolean; years: PositionedYear[] }
  | { kind: "producer"; credits: Credit[]; unknown: boolean; years: PositionedYear[] }
  | { kind: "flag"; flag: "explicit" | "clean" }
  | { kind: "year"; year: number }
  | { kind: "versions"; versions: VersionDraft[] }
  | { kind: "unknown" };

/** Where the classified text sits: credits' source, versions' delimiter, absolute offset. */
export interface Site {
  source: ArtistSource;
  delimiter: VersionDelimiter;
  pos: number;
}

const EXPLICIT_FLAGS = new Set(["explicit", "explicit version", "dirty", "dirty version"]);
const CLEAN_FLAGS = new Set(["clean", "clean version", "radio clean"]);

/** R5: classify `inner` (already trimmed and whitespace-collapsed). */
export function classify(inner: string, site: Site, ctx: Ctx): Classification {
  const words = splitWords(inner);
  if (words.length === 0) return { kind: "unknown" };
  const junkKind = junkKindOf(words, ctx);
  if (junkKind) return { kind: "junk", junkKind };
  const credit = featOrProducer(inner, site, ctx);
  if (credit) return credit;
  const flag = flagOf(words);
  if (flag) return { kind: "flag", flag };
  if (words.length === 1 && isAllDigits(words[0] as string)) {
    const year = yearOf(words[0] as string);
    if (year !== null) return { kind: "year", year };
  }
  const versions = multiVersion(inner, site, ctx) ?? singleVersion(inner, site, ctx);
  if (versions) return { kind: "versions", versions };
  return { kind: "unknown" };
}

// ---------------------------------------------------------------------------
// R5.1 Junk

/** R5.1: the junk kind if the whole word list is junk vocabulary, else null. */
export function junkKindOf(words: readonly string[], ctx: Ctx): JunkKind | null {
  let firstKind: JunkKind | null = null;
  let i = 0;
  while (i < words.length) {
    const m = ctx.t.junkOrGenre.matchAt(words, i);
    if (m) {
      firstKind ??= m.entry.value;
      i += m.length;
      continue;
    }
    const w = words[i] as string;
    // R5.1: connectors and year words may sit between junk phrases: `Official Video 2015`.
    if (ctx.t.junkConnectors.has(w) || ctx.t.junkConnectors.has(wordKey(w)) || yearOf(w) !== null) {
      i++;
      continue;
    }
    firstKind = null;
    break;
  }
  if (firstKind) return firstKind;
  const last = words[words.length - 1];
  if (words.length >= 2 && last !== undefined && inVocab(last, ctx.t.labelSuffixes)) return "label";
  return null;
}

// ---------------------------------------------------------------------------
// R5.2 Feat, R5.3 Producer

function featOrProducer(inner: string, site: Site, ctx: Ctx): Classification | null {
  const spans = wordSpans(inner);
  const words = spans.map((s) => s.text);
  const feat = ctx.t.featMarkers.matchAt(words, 0);
  const bracketOnly = feat ? null : ctx.t.bracketOnlyFeat.matchAt(words, 0);
  const featLen = feat?.length ?? bracketOnly?.length ?? 0;
  if (featLen > 0 && words.length > featLen) {
    const next = words[featLen] as string;
    if (!bracketOnly || !inVocab(next, ctx.t.stopwords)) {
      const r = creditsAfter(inner, spans, featLen, "featured", site, ctx);
      return { kind: "feat", ...r };
    }
  }
  const prod = ctx.t.producerMarkers.matchAt(words, 0);
  if (prod) {
    let len = prod.length;
    if (words[len] !== undefined && wordKey(words[len] as string) === "by") len++;
    if (words.length > len) {
      const r = creditsAfter(inner, spans, len, "producer", site, ctx);
      return { kind: "producer", ...r };
    }
  }
  return null;
}

function creditsAfter(
  inner: string,
  spans: readonly WordSpan[],
  markerWords: number,
  role: "featured" | "producer",
  site: Site,
  ctx: Ctx,
): { credits: Credit[]; unknown: boolean; years: PositionedYear[] } {
  const lastMarker = spans[markerWords - 1] as WordSpan;
  const firstName = spans[markerWords] as WordSpan;
  const lead = inner.slice((spans[0] as WordSpan).start, lastMarker.end);
  return listFromText(inner.slice(firstName.start), site.pos + 1 + firstName.start, ctx, {
    role,
    source: site.source,
    lead,
    splitWith: false,
    featMarkersJoin: role === "featured",
  });
}

/** Split a plain string (nested brackets protected by a fresh scan) as a credit list. */
export function listFromText(text: string, base: number, ctx: Ctx, opts: ListOptions) {
  return splitCredits(scan(text, base).skel, opts, ctx);
}

// ---------------------------------------------------------------------------
// R5.4 Flag

function flagOf(words: readonly string[]): "explicit" | "clean" | null {
  const key = words.map(wordKey).join(" ");
  if (EXPLICIT_FLAGS.has(key)) return "explicit";
  if (CLEAN_FLAGS.has(key)) return "clean";
  return null;
}

// ---------------------------------------------------------------------------
// R5.6 Multi-version

/** Split at top-level ` / ` or `; ` (outside nested brackets). */
function splitMultiParts(inner: string): string[] {
  const parts: string[] = [];
  let depth = 0;
  let start = 0;
  for (let i = 0; i < inner.length; i++) {
    const ch = inner[i];
    if (ch === "(" || ch === "[" || ch === "{") depth++;
    else if ((ch === ")" || ch === "]" || ch === "}") && depth > 0) depth--;
    else if (depth === 0 && ch === "/" && inner[i - 1] === " " && inner[i + 1] === " ") {
      parts.push(inner.slice(start, i - 1));
      start = i + 2;
    } else if (depth === 0 && ch === ";" && inner[i + 1] === " ") {
      parts.push(inner.slice(start, i));
      start = i + 2;
    }
  }
  parts.push(inner.slice(start));
  return parts.map((p) => p.trim());
}

function multiVersion(inner: string, site: Site, ctx: Ctx): VersionDraft[] | null {
  const parts = splitMultiParts(inner);
  if (parts.length < 2) return null;
  const out: VersionDraft[] = [];
  for (const part of parts) {
    const v = part ? singleVersion(part, site, ctx) : null;
    if (!v) return null;
    out.push(...v);
  }
  return out;
}

// ---------------------------------------------------------------------------
// R5.7 Version

function singleVersion(raw: string, site: Site, ctx: Ctx): VersionDraft[] | null {
  const spans = wordSpans(raw);
  const words = spans.map((s) => s.text);
  const v = headBy(raw, spans, words, site, ctx) ?? headScan(raw, words, site, ctx);
  const result = v ?? prefixForm(raw, words, site, ctx);
  return result ? [result] : null;
}

function baseVersion(raw: string, type: VersionType, site: Site): VersionDraft {
  return {
    type,
    raw,
    artists: [],
    modifiers: [],
    descriptor: null,
    year: null,
    unknownArtist: false,
    delimiter: site.delimiter,
    pos: site.pos,
    ambiguousMix: false,
    prefixForm: false,
  };
}

function remixerOpts(): ListOptions {
  return {
    role: "remixer",
    source: "version",
    lead: null,
    splitWith: false,
    featMarkersJoin: false,
  };
}

/** R9.2: remixers are deduped within their own version only. */
function dedupRemixers(credits: Credit[]): Credit[] {
  const seen = new Set<string>();
  return credits.filter((c) => {
    const k = dedupKey(c.name);
    if (seen.has(k)) return false;
    seen.add(k);
    return true;
  });
}

function setCredits(v: VersionDraft, text: string, site: Site, ctx: Ctx): void {
  const r = listFromText(text, site.pos, ctx, remixerOpts());
  v.artists = dedupRemixers(r.credits);
  if (r.unknown) v.unknownArtist = true;
  // R7.5: a dropped year-only name sets the version's year if it is still null.
  const dropped = r.years[0];
  if (dropped) v.year ??= dropped.year;
}

/** R5.7.1: `Remix by Skrillex`. */
function headBy(
  raw: string,
  spans: readonly WordSpan[],
  words: readonly string[],
  site: Site,
  ctx: Ctx,
): VersionDraft | null {
  const head = ctx.t.heads.matchAt(words, 0);
  if (!head) return null;
  const by = words[head.length];
  const rest = spans[head.length + 1];
  if (by === undefined || wordKey(by) !== "by" || rest === undefined) return null;
  const v = baseVersion(raw, head.entry.value, site);
  setCredits(v, raw.slice(rest.start), site, ctx);
  return v;
}

interface RunItem {
  start: number;
  end: number;
  info: RunInfo;
  year: number | null;
}

/** Walk left from `end` collecting the modifier run (R5.7.2). */
function collectRun(words: readonly string[], end: number, ctx: Ctx): RunItem[] {
  const items: RunItem[] = [];
  let j = end;
  while (j > 0) {
    const m = ctx.t.runWords.matchEnding(words, j);
    if (m) {
      items.unshift({ start: j - m.length, end: j, info: m.entry.value, year: null });
      j -= m.length;
      continue;
    }
    const year = yearOf(words[j - 1] as string);
    if (year === null) break;
    items.unshift({ start: j - 1, end: j, info: {}, year });
    j--;
  }
  return items;
}

/** R5.7.2: `Extended VIP Mix`, `Skrillex Remix`, `Taylor's Version`. */
function headScan(
  raw: string,
  words: readonly string[],
  site: Site,
  ctx: Ctx,
): VersionDraft | null {
  const head = ctx.t.heads.matchEnding(words, words.length);
  if (!head) return null;
  const headType = head.entry.value;
  const headStart = words.length - head.length;
  const run = collectRun(words, headStart, ctx);
  const creditEnd = run[0]?.start ?? headStart;
  const creditWords = words.slice(0, creditEnd);
  const left = run[run.length - 1];

  // R5.7.2 type resolution. Generic = canonical type mix/edit/version, any spelling.
  const generic = ctx.t.genericHeadTypes.has(headType);
  let type: VersionType = headType;
  let typeItem: RunItem | null = null;
  let viaMix = false;
  const nearestHead = generic ? nearestNonGenericHead(run, ctx) : null;
  if (generic && left?.info.typeCapable) {
    type = left.info.typeCapable;
    typeItem = left;
  } else if (nearestHead?.info.head) {
    // `2011 Remastered Version` → remaster
    type = nearestHead.info.head;
    typeItem = nearestHead;
  } else if (headType === "mix" && creditWords.length > 0) {
    type = "remix";
    viaMix = true;
  }
  const v = baseVersion(raw, type, site);

  const descriptors: string[] = [];
  for (const item of run) {
    const text = words.slice(item.start, item.end).join(" ");
    if (item.year !== null) {
      v.year ??= item.year;
      continue;
    }
    const modifier = item.info.typeCapable ?? item.info.descriptor;
    if (modifier !== undefined) {
      if (item !== typeItem && !v.modifiers.includes(modifier)) v.modifiers.push(modifier);
    } else if (item.info.genre) {
      descriptors.push(text);
    }
  }

  if (creditWords.length > 0) {
    const credit = creditWords.join(" ");
    const lastWord = asciiLower(creditWords[creditWords.length - 1] as string);
    if (isUnknownToken(credit, ctx.t)) {
      v.unknownArtist = true;
    } else if (type === "version" || type === "cover" || isAllJunkPhrases(creditWords, ctx)) {
      // R5.7.2: `Japanese Version`, `Taylor's Version`, `Adele Cover`, `Video Edit`.
      descriptors.unshift(credit);
    } else {
      const name = lastWord.endsWith("'s") ? credit.slice(0, -2) : credit;
      setCredits(v, name, site, ctx);
      const lastName = v.artists[v.artists.length - 1];
      if (viaMix && lastName && splitWords(lastName.name).length >= 3) {
        v.ambiguousMix = true;
      }
    }
  }
  v.descriptor = descriptors.length > 0 ? descriptors.join(" ") : null;
  return v;
}

/** R5.7.2 type rule 2: the non-generic head in the run nearest the (generic) head. */
function nearestNonGenericHead(run: readonly RunItem[], ctx: Ctx): RunItem | null {
  for (let i = run.length - 1; i >= 0; i--) {
    const head = (run[i] as RunItem).info.head;
    if (head !== undefined && !ctx.t.genericHeadTypes.has(head)) return run[i] as RunItem;
  }
  return null;
}

/** R5.7.2: the credit span is made entirely of junk phrases (`Video` in `Video Edit`). */
function isAllJunkPhrases(words: readonly string[], ctx: Ctx): boolean {
  let i = 0;
  while (i < words.length) {
    const m = ctx.t.junkPhrases.matchAt(words, i);
    if (!m) return false;
    i += m.length;
  }
  return words.length > 0;
}

/** R5.7.3: `Live at Wembley 1986`, `Remastered 2015`, `Sped Up`. */
function prefixForm(
  raw: string,
  words: readonly string[],
  site: Site,
  ctx: Ctx,
): VersionDraft | null {
  const m = ctx.t.prefixForms.matchAt(words, 0);
  if (!m) return null;
  const v = baseVersion(raw, m.entry.value, site);
  v.prefixForm = true;
  const rest: string[] = [];
  for (const w of words.slice(m.length)) {
    const year = v.year === null ? yearOf(w) : null;
    if (year !== null) v.year = year;
    else rest.push(w);
  }
  v.descriptor = rest.length > 0 ? rest.join(" ") : null;
  return v;
}
