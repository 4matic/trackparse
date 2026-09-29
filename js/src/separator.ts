/** R6: artist/title separator, including R6.2 suffix peeling (shared with R8.2). */
import { type Classification, classify } from "./classify.js";
import { type Ctx, isKnownArtist, warn } from "./context.js";
import {
  concatSkel,
  hasGroup,
  PH,
  renderSkel,
  type Segment,
  type Skel,
  sliceSkel,
  splitAtSpacedDashes,
  trimSkel,
} from "./scanner.js";
import type { Mode } from "./types.js";
import { collapseSpaces, inVocab, isLetterAt, isWhitespace, splitWords, yearOf } from "./words.js";

export type Peelable = Extract<Classification, { kind: "junk" | "flag" | "year" | "versions" }>;

export interface Peeled {
  cls: Peelable;
  raw: string;
  pos: number;
}

/** R6.2 classification of a bare dash segment: placeholders make it Unknown. */
function classifySegment(seg: Skel, ctx: Ctx): Classification {
  if (hasGroup(seg)) return { kind: "unknown" };
  const text = collapseSpaces(seg.text);
  return classify(text, { source: "title", delimiter: "-", pos: seg.pos[0] ?? 0 }, ctx);
}

function isPeelable(c: Classification): c is Peelable {
  return c.kind === "junk" || c.kind === "flag" || c.kind === "year" || c.kind === "versions";
}

/**
 * R6.2 / R8.2: peel classifiable suffixes off the right end. `relaxed` drops conditions
 * (i)–(iv) (R8.2 with an artist side). Returns the remaining segments and the peeled
 * suffixes, left to right.
 */
export function peelSuffixes(
  segments: Segment[],
  relaxed: boolean,
  ctx: Ctx,
): { remaining: Segment[]; peeled: Peeled[] } {
  const remaining = [...segments];
  const peeled: Peeled[] = [];
  while (remaining.length >= 2) {
    const last = remaining[remaining.length - 1] as Segment;
    const cls = classifySegment(last.skel, ctx);
    if (!isPeelable(cls)) break;
    const words = splitWords(last.skel.text);
    // (ii) does not apply to prefix-form versions (`Andy C b2b Hedex - Live at Printworks`).
    const prefixForm = cls.kind === "versions" && cls.versions.every((v) => v.prefixForm);
    const ok =
      relaxed ||
      remaining.length - 1 >= 2 ||
      (words.length >= 2 && !prefixForm) ||
      words.some((w) => yearOf(w) !== null) ||
      cls.kind === "junk";
    if (!ok) break;
    peeled.unshift({ cls, raw: collapseSpaces(last.skel.text), pos: last.skel.pos[0] ?? 0 });
    remaining.pop();
  }
  return { remaining, peeled };
}

export interface SeparatorResult {
  artist: Skel | null;
  title: Skel;
  /** A separator split the string (R6.3–R6.7): R8.2 peels without conditions. */
  split: boolean;
  peeled: Peeled[];
}

export interface SeparatorInput {
  skel: Skel;
  mode: Mode;
  uploader: Skel | null;
}

function nonEmpty(s: Skel): Skel | null {
  const t = trimSkel(s);
  return t.text.length > 0 ? t : null;
}

function splitAt(s: Skel, cutStart: number, cutEnd: number): { artist: Skel | null; title: Skel } {
  return { artist: nonEmpty(sliceSkel(s, 0, cutStart)), title: trimSkel(sliceSkel(s, cutEnd)) };
}

export function findSeparator(input: SeparatorInput, ctx: Ctx): SeparatorResult {
  const { skel, mode } = input;
  const segments = splitAtSpacedDashes(skel, true);
  const { remaining, peeled } = peelSuffixes(segments, false, ctx);

  // R6.3
  if (remaining.length >= 2) {
    const first = remaining[0] as Segment;
    const second = remaining[1] as Segment;
    const lastSeg = remaining[remaining.length - 1] as Segment;
    const artist = first.skel.text.length > 0 ? first.skel : null;
    const title = sliceSkel(skel, second.start, lastSeg.end);
    return { artist, title, split: artist !== null, peeled };
  }

  const rest = (remaining[0] as Segment).skel;
  const other = splitWithoutSpacedDash(rest, mode, ctx);
  if (other) return { ...other, split: other.artist !== null, peeled };

  // R6.8: the uploader fallback does not warn noSeparator.
  const artist = mode === "youtube" ? input.uploader : null;
  if (rest.text.length > 0 && artist === null) warn(ctx, "noSeparator");
  return { artist, title: rest, split: false, peeled };
}

function splitWithoutSpacedDash(
  s: Skel,
  mode: Mode,
  ctx: Ctx,
): { artist: Skel | null; title: Skel } | null {
  const asym = asymmetricDash(s.text);
  if (asym >= 0) {
    warn(ctx, "asymmetricDashSplit");
    return splitAt(s, asym, asym + 1);
  }
  if (mode === "youtube") {
    const quoted = quotedTitle(s);
    if (quoted) {
      warn(ctx, "quotedTitleSplit");
      return quoted;
    }
    const by = bySplit(s, ctx);
    if (by >= 0) {
      warn(ctx, "bySplit");
      const parts = splitAt(s, by, by + 4);
      return { artist: parts.title.text.length > 0 ? parts.title : null, title: parts.artist ?? s };
    }
  }
  if (mode === "youtube" || mode === "filename") {
    const dash = unspacedDash(s, ctx);
    if (dash >= 0) {
      warn(ctx, "unspacedDashSplit");
      return splitAt(s, dash, dash + 1);
    }
  }
  return null;
}

/** R6.4: the first `-` with a space on exactly one side. */
function asymmetricDash(t: string): number {
  for (let i = 1; i < t.length - 1; i++) {
    if (t[i] !== "-") continue;
    const left = isWhitespace(t[i - 1]);
    const right = isWhitespace(t[i + 1]);
    if (left !== right) return i;
  }
  return -1;
}

/** R6.5: `Artist "Title" (extra)`. */
function quotedTitle(s: Skel): { artist: Skel | null; title: Skel } | null {
  const t = s.text;
  const open = t.indexOf('"');
  if (open <= 0) return null;
  const close = t.indexOf('"', open + 1);
  if (close < 0) return null;
  let artist = trimSkel(sliceSkel(s, 0, open));
  const last = artist.text[artist.text.length - 1];
  if (last === ":" || last === "-") artist = trimSkel(sliceSkel(artist, 0, artist.text.length - 1));
  const title = trimSkel(concatSkel(sliceSkel(s, open + 1, close), sliceSkel(s, close + 1)));
  return { artist: artist.text.length > 0 ? artist : null, title };
}

/**
 * R6.6: index of the space before the last usable ` by `. The right side must be non-empty and
 * not a single stopword (`Stand by Me`).
 */
function bySplit(s: Skel, ctx: Ctx): number {
  const t = s.text;
  for (let i = t.lastIndexOf(" by "); i > 0; i = t.lastIndexOf(" by ", i - 1)) {
    // Placeholder-only words (groups such as `(Official Video)`) don't count here.
    const right = splitWords(t.slice(i + 4)).filter((w) => w !== PH);
    if (right.length === 0) continue;
    if (right.length === 1 && inVocab(right[0] as string, ctx.t.stopwords)) continue;
    return i;
  }
  return -1;
}

/** R6.7: `Jay-Z-Numb` → the chosen unspaced dash index. */
function unspacedDash(s: Skel, ctx: Ctx): number {
  const t = s.text;
  let lastCandidate = -1;
  for (let i = 1; i < t.length - 1; i++) {
    if (t[i] !== "-" || isWhitespace(t[i - 1]) || isWhitespace(t[i + 1])) continue;
    if (!(t[i + 1] === '"' || isLetterAt(t, i + 1))) continue;
    if (ctx.knownKeys.size > 0 && isKnownArtist(renderSkel(sliceSkel(s, 0, i)), ctx)) return i;
    lastCandidate = i;
  }
  return lastCandidate;
}
