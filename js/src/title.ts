/** R8: the title side. */
import { classify, type VersionDraft } from "./classify.js";
import type { Ctx } from "./context.js";
import { type Credit, type PositionedYear, splitCredits } from "./credits.js";
import { type PositionedJunk, trailingJunkPhrase } from "./mode.js";
import {
  cutSkel,
  groupsOf,
  PH,
  removeGroups,
  renderSkel,
  type Segment,
  type Skel,
  sliceSkel,
  splitAtSpacedDashes,
  trimSkel,
} from "./scanner.js";
import { type Peeled, peelSuffixes } from "./separator.js";
import { isUnknownToken } from "./tables.js";
import type { Flags } from "./types.js";
import { collapseSpaces, type WordSpan, wordSpans } from "./words.js";

/** Everything the title side (and R6.2 peeling) can extract. */
export interface Extracted {
  credits: Credit[];
  versions: VersionDraft[];
  junk: PositionedJunk[];
  years: PositionedYear[];
  flags: Pick<Flags, "explicit" | "clean" | "unknownArtist">;
}

export function emptyExtracted(): Extracted {
  return {
    credits: [],
    versions: [],
    junk: [],
    years: [],
    flags: { explicit: false, clean: false, unknownArtist: false },
  };
}

/** Record R6.2 / R8.2 peeled dash suffixes. */
export function addPeeled(out: Extracted, peeled: readonly Peeled[]): void {
  for (const p of peeled) {
    const c = p.cls;
    if (c.kind === "versions") out.versions.push(...c.versions);
    else if (c.kind === "junk") out.junk.push({ raw: p.raw, kind: c.junkKind, pos: p.pos });
    else if (c.kind === "flag") out.flags[c.flag] = true;
    else out.years.push({ year: c.year, pos: p.pos });
  }
}

export interface TitleSideInput {
  skel: Skel;
  /** An artist side was split off (R8.2 peels without conditions). */
  relaxed: boolean;
  youtube: boolean;
}

export interface TitleSideResult {
  title: string;
  unknownTitle: boolean;
}

export function parseTitleSide(input: TitleSideInput, out: Extracted, ctx: Ctx): TitleSideResult {
  let skel = input.skel;
  const removed = extractGroups(skel, out, ctx); // R8.1

  // R8.2
  const segments = splitAtSpacedDashes(skel);
  const { remaining, peeled } = peelSuffixes(segments, input.relaxed, ctx);
  addPeeled(out, peeled);
  const lastKept = remaining[remaining.length - 1] as Segment;
  skel = sliceSkel(skel, 0, lastKept.end);

  skel = extractUnbracketedCredits(skel, out, ctx); // R8.3
  skel = removeGroups(skel, removed);
  if (input.youtube) skel = stripTrailingJunk(skel, out, ctx); // R8.4

  const title = cleanupTitle(renderSkel(skel)); // R8.5
  return { title, unknownTitle: title.length > 0 && isUnknownToken(title, ctx.t) }; // R8.6
}

/** R8.1: classify every group; returns the positions of the groups to remove. */
function extractGroups(skel: Skel, out: Extracted, ctx: Ctx): Set<number> {
  const removed = new Set<number>();
  for (const { group } of groupsOf(skel)) {
    const site = { source: "title" as const, delimiter: group.open, pos: group.pos };
    const c = classify(group.inner, site, ctx);
    switch (c.kind) {
      case "versions":
        out.versions.push(...c.versions);
        break;
      case "junk":
        out.junk.push({ raw: group.inner, kind: c.junkKind, pos: group.pos });
        break;
      case "flag":
        out.flags[c.flag] = true;
        break;
      case "year":
        out.years.push({ year: c.year, pos: group.pos });
        break;
      case "feat":
      case "producer":
        out.credits.push(...c.credits);
        out.years.push(...c.years);
        if (c.unknown) out.flags.unknownArtist = true;
        break;
      case "unknown":
        continue;
    }
    removed.add(group.pos);
  }
  return removed;
}

interface MarkerHit {
  start: number;
  end: number;
  marker: string;
  role: "featured" | "producer";
}

/**
 * R8.3 markers, left to right, each with a space on both sides: feat markers from
 * `#titleMarkers` (`feat.`, `ft.`, `featuring` — undotted `feat`/`ft` stay text) and
 * producer markers from `#titleProducer` (`prod.`, `prod. by`, `produced by`, `prod.by`).
 */
function titleMarkers(s: Skel, ctx: Ctx): MarkerHit[] {
  const spans = wordSpans(s.text);
  const words = spans.map((w) => w.text);
  const hits: MarkerHit[] = [];
  for (let i = 0; i < spans.length; i++) {
    const span = spans[i] as WordSpan;
    if (s.text[span.start - 1] !== " ") continue;
    let role: MarkerHit["role"] = "featured";
    let lastIndex = -1;
    const feat = ctx.t.titleFeatMarkers.matchAt(words, i);
    if (feat) {
      lastIndex = i + feat.length - 1;
    } else {
      const prod = ctx.t.titleProducerMarkers.matchAt(words, i);
      if (!prod) continue;
      role = "producer";
      lastIndex = i + prod.length - 1;
    }
    const last = spans[lastIndex] as WordSpan;
    if (s.text[last.end] !== " ") continue;
    hits.push({
      start: span.start,
      end: last.end,
      marker: s.text.slice(span.start, last.end),
      role,
    });
    i = lastIndex;
  }
  return hits;
}

/**
 * R8.3: `Title feat. X`, `Title prod. by Y` → credits, removed from the title. A list runs to
 * the next placeholder, the next marker of the other kind, or the end; later feat markers
 * inside a featured list are joiners (R7.2).
 */
function extractUnbracketedCredits(input: Skel, out: Extracted, ctx: Ctx): Skel {
  const markers = titleMarkers(input, ctx);
  const cuts: [number, number][] = [];
  let i = 0;
  while (i < markers.length) {
    const hit = markers[i] as MarkerHit;
    let listEnd = input.text.indexOf(PH, hit.end);
    if (listEnd < 0) listEnd = input.text.length;
    const other = markers.find((m, k) => k > i && m.role !== hit.role && m.start >= hit.end);
    if (other && other.start < listEnd) listEnd = other.start;
    const list = trimSkel(sliceSkel(input, hit.end, listEnd));
    if (list.text.length > 0) {
      const r = splitCredits(
        list,
        {
          role: hit.role,
          source: "title",
          lead: hit.marker,
          splitWith: false,
          featMarkersJoin: hit.role === "featured",
        },
        ctx,
      );
      out.credits.push(...r.credits);
      out.years.push(...r.years);
      if (r.unknown) out.flags.unknownArtist = true;
      cuts.push([hit.start, listEnd]);
    } else {
      listEnd = hit.end;
    }
    while (i < markers.length && (markers[i] as MarkerHit).start < listEnd) i++;
  }
  let skel = input;
  for (const [start, end] of cuts.reverse()) skel = cutSkel(skel, start, end);
  return skel;
}

/** R8.4: strip trailing unbracketed junk phrases (optionally after a lone `-` / `|`). */
function stripTrailingJunk(input: Skel, out: Extracted, ctx: Ctx): Skel {
  let skel = trimSkel(input);
  for (;;) {
    const hit = trailingJunkPhrase(skel.text, ctx);
    if (!hit) return skel;
    let before = trimSkel(sliceSkel(skel, 0, hit.start));
    const lastChar = before.text[before.text.length - 1];
    const lone = before.text.length === 1 || before.text[before.text.length - 2] === " ";
    if ((lastChar === "-" || lastChar === "|") && lone) {
      before = trimSkel(sliceSkel(before, 0, before.text.length - 1));
    }
    if (before.text.length === 0) return skel; // never strip the whole title
    const raw = collapseSpaces(skel.text.slice(hit.start));
    out.junk.push({ raw, kind: hit.kind, pos: skel.pos[hit.start] ?? 0 });
    skel = before;
  }
}

function isLoneEdge(word: string | undefined): boolean {
  return word === "-" || word === "|";
}

function stripLoneEdges(s: string): string {
  const words = s.split(" ");
  while (words.length > 0 && isLoneEdge(words[0])) words.shift();
  while (words.length > 0 && isLoneEdge(words[words.length - 1])) words.pop();
  return words.join(" ");
}

/** R8.5 */
export function cleanupTitle(raw: string): string {
  let title = stripLoneEdges(collapseSpaces(raw));
  const q = title[0];
  if (title.length >= 2 && (q === '"' || q === "'") && title[title.length - 1] === q) {
    title = stripLoneEdges(collapseSpaces(title.slice(1, -1)));
  }
  return title;
}
