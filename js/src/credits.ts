/** R7.3–R7.5: splitting a credit list into names. */
import { type Ctx, matchKnownAt } from "./context.js";
import { renderSkel, type Skel, sliceSkel, trimSkel } from "./scanner.js";
import { isUnknownToken } from "./tables.js";
import type { Artist, ArtistRole, ArtistSource } from "./types.js";
import {
  collapseSpaces,
  inVocab,
  isAllDigits,
  isDigit,
  isWhitespace,
  wordKey,
  wordMatches,
  yearOf,
} from "./words.js";

/** An artist credit plus its absolute offset, used for ordering (R9.1). */
export interface Credit extends Artist {
  pos: number;
  /** Id of the credit list it came from (R7.6 joiner inheritance in R9.2). */
  list: number;
}

/** A year with its absolute offset (R9.6: the track year is the first in text order). */
export interface PositionedYear {
  year: number;
  pos: number;
}

export interface ListOptions {
  role: ArtistRole;
  source: ArtistSource;
  /** Joiner of the first name: `null`, or the feat / producer marker as written. */
  lead: string | null;
  /** R7.3: `with` splits only in the main list of the artist side. */
  splitWith: boolean;
  /** R7.2: later feat markers act as joiners inside a featured list. */
  featMarkersJoin: boolean;
}

export interface ListResult {
  credits: Credit[];
  /** R7.5: an unknown token was dropped. */
  unknown: boolean;
  /** R7.5: dropped year-only names. */
  years: PositionedYear[];
}

interface JoinerHit {
  start: number;
  end: number;
  raw: string;
}

const AND_LIKE = new Set(["&", "and", "+"]);

function wordAt(text: string, start: number): number {
  let end = start;
  while (end < text.length && !isWhitespace(text[end])) end++;
  return end;
}

/** The word after a joiner; a tight joiner (`,` `;`) ends it, since the name ends there. */
function nextWord(text: string, from: number): string {
  let a = from;
  while (a < text.length && isWhitespace(text[a])) a++;
  let b = a;
  while (b < text.length && !isWhitespace(text[b]) && text[b] !== "," && text[b] !== ";") b++;
  return text.slice(a, b);
}

/** R7.3b: the leftmost joiner occurrence at or after `from`, honouring the guards. */
function findJoiner(
  text: string,
  from: number,
  splitAtComma: boolean,
  opts: ListOptions,
  ctx: Ctx,
): JoinerHit | null {
  for (let p = from; p < text.length; p++) {
    const ch = text[p] as string;
    if (ch === "," || ch === ";") {
      if (ch === "," && isDigit(text[p - 1]) && isDigit(text[p + 1])) continue; // `1,000`
      return { start: p, end: p + 1, raw: ch };
    }
    if (p === 0 || text[p - 1] !== " " || isWhitespace(ch)) continue;
    const end = wordAt(text, p);
    if (end >= text.length || text[end] !== " ") continue; // spaced joiners need a space after
    const word = text.slice(p, end);
    const hit = spacedJoiner(word, text, end, splitAtComma, opts, ctx);
    if (hit) return { start: p, end, raw: word };
  }
  return null;
}

function spacedJoiner(
  word: string,
  text: string,
  end: number,
  splitAtComma: boolean,
  opts: ListOptions,
  ctx: Ctx,
): boolean {
  if (opts.featMarkersJoin && ctx.t.featMarkers.matchAt([word], 0)) return true;
  for (const j of ctx.t.spacedJoiners) {
    const matches = j.caseSensitive ? word === j.raw : wordMatches(word, j.raw);
    if (!matches) continue;
    if (j.raw === "with" && !opts.splitWith) return false;
    if (j.isAnd) {
      if (ctx.splitAnd === "never") return false;
      if (ctx.splitAnd === "auto" && !splitAtComma) return false;
    }
    if (AND_LIKE.has(j.raw) && inVocab(nextWord(text, end), ctx.t.noSplitBefore)) return false;
    return true;
  }
  return false;
}

/** R7.4: clean one raw name. */
export function cleanName(raw: string): string {
  let name = collapseSpaces(raw);
  const first = name[0];
  if (name.length >= 2 && (first === '"' || first === "'") && name[name.length - 1] === first) {
    name = collapseSpaces(name.slice(1, -1));
  }
  while (name.endsWith(",") || name.endsWith(";")) name = name.slice(0, -1).trimEnd();
  const space = name.indexOf(" ");
  if (space > 0 && wordKey(name.slice(0, space)) === "by") name = name.slice(space + 1);
  return collapseSpaces(name);
}

interface RawName {
  name: string;
  joiner: string | null;
  pos: number;
}

/** R7.4: strip one pair of wrapping quotes from the whole list (no other such quote inside). */
function unwrapList(list: Skel): Skel {
  const t = trimSkel(list);
  const q = t.text[0];
  if (t.text.length < 2 || (q !== '"' && q !== "'") || t.text[t.text.length - 1] !== q) return t;
  if (t.text.indexOf(q, 1) !== t.text.length - 1) return t;
  return trimSkel(sliceSkel(t, 1, t.text.length - 1));
}

/** R7.3: split `input` into credits. */
export function splitCredits(input: Skel, opts: ListOptions, ctx: Ctx): ListResult {
  const list = unwrapList(input); // R7.4: `"Camo & Krooked"`
  const text = list.text;
  const raw: RawName[] = [];
  let joiner: string | null = opts.lead;
  let splitAtComma = false;
  let start = 0;
  while (start <= text.length) {
    while (start < text.length && isWhitespace(text[start])) start++;
    const known = matchKnownAt(text, start, ctx, "-"); // R7.3a
    const hit = findJoiner(text, start + known, splitAtComma, opts, ctx);
    const end = hit ? hit.start : text.length;
    raw.push({
      name: cleanName(renderSkel(sliceSkel(list, start, end))),
      joiner,
      pos: list.pos[start] ?? list.pos[list.pos.length - 1] ?? 0,
    });
    if (!hit) break;
    if (hit.raw === "," || hit.raw === ";") splitAtComma = true; // R7.3: `and` after a tight joiner
    joiner = hit.raw;
    start = hit.end;
  }
  return finishNames(raw, opts, ctx);
}

/**
 * R7.4 empty drops, R7.5 year/unknown drops. R7.6: the first survivor takes the lead joiner
 * (`null`, or the feat/producer marker).
 */
function finishNames(raw: RawName[], opts: ListOptions, ctx: Ctx): ListResult {
  const named = raw.filter((r) => r.name.length > 0);
  let unknown = false;
  const years: PositionedYear[] = [];
  const kept = named.filter((r) => {
    if (isUnknownToken(r.name, ctx.t)) {
      unknown = true;
      return false;
    }
    const year = isAllDigits(r.name) ? yearOf(r.name) : null;
    if (year !== null && named.length > 1) {
      years.push({ year, pos: r.pos });
      return false;
    }
    return true;
  });
  const list = ctx.listSeq++;
  const credits = kept.map(
    (r, i): Credit => ({
      name: r.name,
      role: opts.role,
      joiner: i === 0 ? opts.lead : r.joiner,
      source: opts.source,
      pos: r.pos,
      list,
    }),
  );
  return { credits, unknown, years };
}
