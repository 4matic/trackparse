/** R7: the artist side. */
import { classify } from "./classify.js";
import type { Ctx } from "./context.js";
import { type Credit, type PositionedYear, splitCredits } from "./credits.js";
import type { PositionedJunk } from "./mode.js";
import { groupsOf, PH, removeGroups, type Skel, sliceSkel, trimSkel } from "./scanner.js";
import { wordSpans } from "./words.js";

export type { PositionedYear };

export interface ArtistSideResult {
  credits: Credit[];
  junk: PositionedJunk[];
  years: PositionedYear[];
  unknownArtist: boolean;
  /** R7.1: artist-side Flag groups. */
  explicit: boolean;
  clean: boolean;
}

/** R7.2: leftmost unbracketed feat marker with a space (or placeholder) left and a space right. */
export function findFeatMarker(
  s: Skel,
  ctx: Ctx,
  from = 0,
): { start: number; end: number; marker: string } | null {
  const spans = wordSpans(s.text);
  const words = spans.map((w) => w.text);
  for (let i = 0; i < spans.length; i++) {
    const span = spans[i];
    if (!span || span.start < from) continue;
    const before = s.text[span.start - 1];
    if (before !== " " && before !== PH) continue;
    const m = ctx.t.featMarkers.matchAt(words, i);
    if (!m) continue;
    const last = spans[i + m.length - 1];
    if (!last || s.text[last.end] !== " ") continue;
    return { start: span.start, end: last.end, marker: s.text.slice(span.start, last.end) };
  }
  return null;
}

/** R7.1–R7.5 over an artist-side skeleton. */
export function parseArtistSide(side: Skel, ctx: Ctx): ArtistSideResult {
  const result: ArtistSideResult = {
    credits: [],
    junk: [],
    years: [],
    unknownArtist: false,
    explicit: false,
    clean: false,
  };
  const removed = new Set<number>();

  // R7.1
  for (const { group } of groupsOf(side)) {
    const site = { source: "artist" as const, delimiter: group.open, pos: group.pos };
    const c = classify(group.inner, site, ctx);
    if (c.kind === "feat" || c.kind === "producer") {
      result.credits.push(...c.credits);
      result.years.push(...c.years);
      if (c.unknown) result.unknownArtist = true;
    } else if (c.kind === "flag") {
      result[c.flag] = true;
    } else if (c.kind === "year") {
      result.years.push({ year: c.year, pos: group.pos });
    } else if (c.kind === "junk") {
      result.junk.push({ raw: group.inner, kind: c.junkKind, pos: group.pos });
    } else {
      continue;
    }
    removed.add(group.pos);
  }
  const text = removeGroups(side, removed);

  // R7.2
  const feat = findFeatMarker(text, ctx);
  const main = feat ? trimSkel(sliceSkel(text, 0, feat.start)) : trimSkel(text);
  const mainList = splitCredits(
    main,
    { role: "primary", source: "artist", lead: null, splitWith: true, featMarkersJoin: false },
    ctx,
  );
  result.credits.push(...mainList.credits);
  result.years.push(...mainList.years);
  if (mainList.unknown) result.unknownArtist = true;
  if (feat) {
    const featured = splitCredits(
      trimSkel(sliceSkel(text, feat.end)),
      {
        role: "featured",
        source: "artist",
        lead: feat.marker,
        splitWith: false,
        featMarkersJoin: true,
      },
      ctx,
    );
    result.credits.push(...featured.credits);
    result.years.push(...featured.years);
    if (featured.unknown) result.unknownArtist = true;
  }
  result.credits.sort((a, b) => a.pos - b.pos);
  return result;
}
