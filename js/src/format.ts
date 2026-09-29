/** R11: render a ParsedTrack back to a string. */
import { renderVersion } from "./assemble.js";
import { getDefaultTables, type Tables } from "./tables.js";
import type { Artist, FormatOptions, ParsedTrack } from "./types.js";
import { dedupKey, wordMatches } from "./words.js";

interface Resolved {
  feat: NonNullable<FormatOptions["feat"]>;
  featMarker: string | undefined;
  canonical: boolean;
  versions: boolean;
  producers: boolean;
  position: boolean;
}

function resolve(o: FormatOptions | undefined): Resolved {
  return {
    feat: o?.feat ?? "source",
    featMarker: o?.featMarker,
    canonical: o?.joiners === "canonical",
    versions: (o?.versions ?? "all") === "all",
    producers: o?.producers ?? true,
    position: o?.position ?? false,
  };
}

function isFeatMarker(raw: string, t: Tables): boolean {
  return t.featMarkers.matchAt([raw], 0) !== null || t.bracketOnlyFeat.matchAt([raw], 0) !== null;
}

/**
 * R11.5: map a raw joiner to its canonical form through data/joiners.json; feat markers become
 * `featCanonical`. A featured list's lead marker is always a feat marker (`with` → `feat.`),
 * while between names the joiner table wins (`Sigala with Ella Eyre` keeps `with`).
 */
function canonicalJoiner(raw: string, t: Tables, featLead = false): string {
  if (featLead && isFeatMarker(raw, t)) return t.featCanonical;
  const tight = t.tightJoiners.get(raw);
  if (tight !== undefined) return tight;
  for (const j of t.spacedJoiners) {
    if (j.caseSensitive ? raw === j.raw : wordMatches(raw, j.raw)) return j.canonical;
  }
  return isFeatMarker(raw, t) ? t.featCanonical : raw;
}

/** R11.1: `" " + joiner + " "`; tight joiners as the raw char + `" "`; a missing joiner as `&`. */
function joinSep(joiner: string | null, o: Resolved, t: Tables): string {
  const j = joiner === null ? "&" : o.canonical ? canonicalJoiner(joiner, t) : joiner;
  return t.tightJoiners.has(j) ? `${j} ` : ` ${j} `;
}

/** Names of one credit list joined by their own joiners (the first joiner is not rendered). */
function joinNames(list: readonly Artist[], o: Resolved, t: Tables): string {
  return list.map((a, i) => (i === 0 ? a.name : joinSep(a.joiner, o, t) + a.name)).join("");
}

/** The marker in front of a featured list. */
function leadMarker(first: Artist, o: Resolved, t: Tables, fallback: string): string {
  if (o.featMarker !== undefined) return o.featMarker;
  const raw = first.joiner ?? fallback;
  return o.canonical ? canonicalJoiner(raw, t, true) : raw;
}

export function format(track: ParsedTrack, options?: FormatOptions): string {
  const t = getDefaultTables();
  const o = resolve(options);
  const artists = Array.isArray(track?.artists) ? track.artists : [];
  const primaries = artists.filter((a) => a.role === "primary");
  const featured = artists.filter((a) => a.role === "featured");
  const producers = artists.filter((a) => a.role === "producer");

  let artistFeat: Artist[] = [];
  let titleFeat: Artist[] = [];
  if (o.feat === "source") {
    artistFeat = featured.filter((a) => a.source === "artist");
    titleFeat = featured.filter((a) => a.source !== "artist");
  } else if (o.feat === "artist") {
    artistFeat = featured;
  } else if (o.feat === "title") {
    titleFeat = featured;
  }

  // R11.1 / R11.2
  let artistPart = joinNames(primaries, o, t);
  const artistFeatFirst = artistFeat[0];
  if (artistFeatFirst) {
    const lead = leadMarker(artistFeatFirst, o, t, t.featCanonical);
    const names = joinNames(artistFeat, o, t);
    artistPart = artistPart ? `${artistPart} ${lead} ${names}` : `${lead} ${names}`;
  }

  // R11.3
  const pieces: string[] = [];
  if (track?.title) pieces.push(track.title);
  const titleFeatFirst = titleFeat[0];
  if (titleFeatFirst) {
    const lead = leadMarker(titleFeatFirst, o, t, t.featCanonical);
    pieces.push(`(${lead} ${joinNames(titleFeat, o, t)})`);
  }
  if (o.versions) for (const v of track?.versions ?? []) pieces.push(renderVersion(v));
  const producerFirst = producers[0];
  if (o.producers && producerFirst) {
    const marker = producerFirst.joiner ?? "prod.";
    pieces.push(`(${marker} ${joinNames(producers, o, t)})`);
  }
  const titlePart = pieces.join(" ");

  // R11.4
  let result = artistPart ? `${artistPart} - ${titlePart}` : titlePart;
  if (o.position && track?.position) result = `${track.position.raw}. ${result}`;
  return result;
}

/** All credited names: track artists plus every version's remixers, deduped by R0.4 key. */
export function allArtists(track: ParsedTrack): Artist[] {
  const seen = new Set<string>();
  const out: Artist[] = [];
  const all = [...(track?.artists ?? []), ...(track?.versions ?? []).flatMap((v) => v.artists)];
  for (const a of all) {
    const key = dedupKey(a.name);
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(a);
  }
  return out;
}
