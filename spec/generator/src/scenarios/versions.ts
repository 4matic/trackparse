import {
  ALL_VERSIONS,
  type ArtistAtom,
  type Atom,
  DELIMS,
  type Delim,
  EXTRAS,
  type ExtraAtom,
  type ModeAtom,
  type TitleAtom,
  type VersionAtom,
  modeById,
  slotCount,
} from "../atoms.js";
import type { CreditList } from "../model.js";
import type { Choice } from "../scenario.js";
import { base, type Scenario } from "../scenario.js";
import { ARTISTS_SMALL, REMIXERS, TITLES_MIXED, creditFrom, isNone, joiners, single, withNone } from "./pools.js";

const REMIXER_POOL = REMIXERS.filter((_, i) => i % 3 === 0);
const REMIXER_SECOND = REMIXERS.filter((_, i) => i % 7 === 1).slice(0, 8);

/**
 * Credits for a version atom from the remixer dims (names given; rj/r2 optional).
 * Returns null when a credit slot has no remixer.
 */
export function versionCredits(
  va: VersionAtom,
  p: Choice,
  ignore: (dim: string) => void,
  r1: string,
  rj?: string,
  r2?: string,
): CreditList[] | null {
  const slots = slotCount(va);
  const extra = [rj, r2].filter((d): d is string => d !== undefined);
  if (slots === 0) {
    [r1, ...extra].forEach(ignore);
    return [];
  }
  if (isNone(p[r1]!)) return null;
  if (slots === 1) return [rj && r2 ? creditFrom(p, ignore, r1, [[rj, r2]]) : single(p[r1] as ArtistAtom)];
  // multi-version: one name per slot, no joiner
  if (!r2 || isNone(p[r2]!)) return null;
  if (rj) ignore(rj);
  return [single(p[r1] as ArtistAtom), single(p[r2] as ArtistAtom)];
}

/** Every version atom × delimiter × remixer credit shapes × extras (R5.5\u2013R5.8, R6.2, R8.1, R9.4). */
export const versions: Scenario = {
  name: "versions",
  description:
    "Every version/remix form (head scan, head-by, prefix forms, generic heads, modifiers, years, descriptors, unknown credit, multi-version groups) in (), [], {} and as dash suffix, with 0\u20132 remixers and year/flag/unknown extra groups (SPEC R5.4\u2013R5.8, R6.2, R8.1, R9.4).",
  dims: [
    { name: "version", values: ALL_VERSIONS },
    { name: "delim", values: DELIMS },
    { name: "r1", values: withNone(REMIXER_POOL) },
    { name: "rj", values: withNone(joiners(["amp", "x", "and", "comma", "vs", "b2b", "with", "x-upper"])) },
    { name: "r2", values: withNone(REMIXER_SECOND) },
    { name: "extra", values: withNone(EXTRAS) },
    { name: "artist", values: ARTISTS_SMALL.slice(0, 8) },
    { name: "mode", values: ["clean", "youtube"].map(modeById) },
    { name: "title", values: TITLES_MIXED.slice(0, 14) },
  ],
  sample: 200,
  assert: { yearFlags: true, fullVersions: true },
  build: (p, ignore) => {
    const va = p.version as VersionAtom;
    const credits = versionCredits(va, p, ignore, "r1", "rj", "r2");
    if (!credits) return null;
    const delim = (p.delim as Atom & { delim: Delim }).delim;
    return base({
      main: single(p.artist as ArtistAtom),
      versions: [{ atom: va, delim, credits }],
      extras: isNone(p.extra!) ? [] : [p.extra as ExtraAtom],
      mode: p.mode as ModeAtom,
      title: p.title as TitleAtom,
    });
  },
};
