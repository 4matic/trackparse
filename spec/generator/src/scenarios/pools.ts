import {
  ARTISTS,
  type ArtistAtom,
  type Atom,
  JOINERS,
  NONE,
  TITLES,
  type TitleAtom,
  artistById,
  byId,
  joinerById,
  titleById,
} from "../atoms.js";
import type { JoinerAtom } from "../atoms.js";
import { type CreditList, type SplitAnd, single } from "../model.js";
import type { Choice } from "../scenario.js";
import { remixerProblem } from "../text.js";

export const ids = <T extends Atom>(f: (id: string) => T, list: string[]): T[] => list.map(f);
export const artists = (list: string[]): ArtistAtom[] => ids(artistById, list);
export const titles = (list: string[]): TitleAtom[] => ids(titleById, list);
export const withNone = <T extends Atom>(xs: T[]): Atom[] => [NONE, ...xs];
export const isNone = (a: Atom): boolean => a.id === "none";

/** Artists usable as `<name> Remix` credit spans. */
export const REMIXERS: ArtistAtom[] = ARTISTS.filter((a) => remixerProblem(a.text) === null);

/** A diverse cross-section used where the full pool would blow up pairwise. */
export const ARTISTS_MIXED = artists([
  "noisia", "culture-shock", "fox-stevenson", "teddy-killerz", "black-sun-empire", "the-upbeats",
  "zveri", "lyapis", "katya-chekhova", "sigur-ros", "beyonce", "mo", "tiesto", "utada", "bts",
  "asap-rocky", "pink", "will-i-am", "mr-oizo", "ant-shift", "ac-dc", "featurecast", "ftampa",
  "andy-c", "feed-me", "the-xx", "malcolm-x", "jay-z", "blink-182", "mumford-sons", "2-unlimited",
  "808-state", "50-cent", "m83", "311",
]);

export const ARTISTS_SMALL = artists([
  "mefjus", "phace", "gydra", "camo", "krooked", "kino", "royksopp", "sakamoto", "deadmau5",
  "kesha", "withered-hand", "xample", "the-prodigy", "andromedik",
]);

export const TITLES_MIXED = titles([
  "diplodocus", "bruises", "one-more-time", "around-the-world", "gruppa-krovi", "hoppipolla",
  "cafe-del-mar", "hanataba", "dont-stop-me-now", "party-like-1999", "99-problems", "up-tempo",
  "stand-by-me", "spy-vs-spy", "cat-x-mouse", "salt-and-pepper", "satisfaction", "song-part-2",
  "voices-part-ii", "whats-up",
]);

export const TITLES_SMALL = titles([
  "stigma", "levitate", "hey-jude", "kukla-kolduna", "deja-vu", "bomnal", "rock-n-roll-star",
  "7-rings", "mr-brightside", "the-reaper",
]);

export const ALL_TITLES = TITLES;
export const ALL_JOINERS = JOINERS;
export const joiners = (list: string[]) => ids(joinerById, list);

export const SPLIT_AND: (Atom & { value: SplitAnd })[] = [
  { id: "and-auto", value: "auto" },
  { id: "and-always", value: "always" },
  { id: "and-never", value: "never" },
];

/**
 * Build a credit list from `first` plus optional (joinerDim, nameDim) pairs. The list
 * ends at the first pair where either side is NONE; that pair and all later ones are
 * reported as ignored.
 */
export function creditFrom(
  p: Choice,
  ignore: (dim: string) => void,
  first: string,
  rest: [string, string][],
): CreditList {
  const c: CreditList = { names: [p[first] as ArtistAtom], joiners: [] };
  let ended = false;
  for (const [jd, nd] of rest) {
    if (ended || isNone(p[jd]!) || isNone(p[nd]!)) {
      ended = true;
      ignore(jd);
      ignore(nd);
      continue;
    }
    c.joiners.push(p[jd] as JoinerAtom);
    c.names.push(p[nd] as ArtistAtom);
  }
  return c;
}

export { single, byId };
