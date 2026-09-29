/** R9: assembly and dedup. */
import type { VersionDraft } from "./classify.js";
import type { Credit } from "./credits.js";
import type { Artist, ArtistRole, Version } from "./types.js";
import { dedupKey } from "./words.js";

const PRECEDENCE: Record<ArtistRole, number> = { primary: 2, featured: 1, producer: 0, remixer: 0 };

export function toArtist(c: Credit | Artist): Artist {
  return { name: c.name, role: c.role, joiner: c.joiner, source: c.source };
}

interface Entry {
  a: Artist;
  list: number;
  alive: boolean;
}

/**
 * R7.6: `e` leaves its list. If it was the list's first remaining name, the next remaining name
 * of that list takes `joiner`.
 */
function passJoiner(entries: readonly Entry[], e: Entry, joiner: string | null): void {
  const members = entries.filter((x) => x.alive && x.list === e.list);
  if (members[0] !== e) return;
  const next = members[1];
  if (next) next.a.joiner = joiner;
}

/**
 * R9.2: keep the first occurrence. A later lower/equal-precedence duplicate is removed (R7.6
 * joiner inheritance); a later higher-precedence duplicate (role upgrade) gives the kept
 * entry its role, joiner and source, and the kept entry moves into that occurrence's list.
 */
export function dedupArtists(credits: readonly (Credit | Artist)[]): Artist[] {
  const entries: Entry[] = credits.map((c, i) => ({
    a: toArtist(c),
    list: "list" in c ? c.list : -1 - i,
    alive: true,
  }));
  const index = new Map<string, Entry>();
  for (const e of entries) {
    // R9.2: producers are a separate credit class, deduped only among themselves.
    const key = `${e.a.role === "producer" ? "p" : "c"}:${dedupKey(e.a.name)}`;
    const kept = index.get(key);
    if (!kept) {
      index.set(key, e);
      continue;
    }
    if (PRECEDENCE[e.a.role] > PRECEDENCE[kept.a.role]) {
      passJoiner(entries, kept, kept.a.joiner);
      kept.a.role = e.a.role;
      kept.a.joiner = e.a.joiner;
      kept.a.source = e.a.source;
      kept.list = e.list;
      e.alive = false;
    } else {
      passJoiner(entries, e, e.a.joiner);
      e.alive = false;
    }
  }
  return entries.filter((e) => e.alive).map((e) => e.a);
}

export function toVersion(v: VersionDraft): Version {
  return {
    type: v.type,
    raw: v.raw,
    artists: v.artists.map(toArtist),
    modifiers: [...v.modifiers],
    descriptor: v.descriptor,
    year: v.year,
    unknownArtist: v.unknownArtist,
    delimiter: v.delimiter,
  };
}

/** R9.4: a version rendered with its delimiter. */
export function renderVersion(v: Pick<Version, "raw" | "delimiter">): string {
  switch (v.delimiter) {
    case "(":
      return `(${v.raw})`;
    case "[":
      return `[${v.raw}]`;
    case "{":
      return `{${v.raw}}`;
    default:
      return `- ${v.raw}`;
  }
}

export function fullTitleOf(title: string, versions: readonly Version[]): string {
  return [title, ...versions.map(renderVersion)].filter((s) => s.length > 0).join(" ");
}

export function byPos<T extends { pos: number }>(items: readonly T[]): T[] {
  return [...items].sort((a, b) => a.pos - b.pos);
}
