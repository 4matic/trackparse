import {
  type ArtistAtom,
  type Atom,
  type ModeAtom,
  SPACED_SEPS,
  type SepAtom,
  type TitleAtom,
  VERSIONS,
  type VersionAtom,
  junkById,
  modeById,
  slotCount,
} from "../atoms.js";
import type { Composition } from "../model.js";
import { base, type Scenario } from "../scenario.js";
import { ARTISTS_MIXED, REMIXERS, TITLES_MIXED, isNone, single, withNone } from "./pools.js";

interface ItemAtom extends Atom {
  version?: VersionAtom;
  year?: number;
}

const ITEMS: ItemAtom[] = [
  ...VERSIONS.map((v) => ({ id: v.id, version: v })),
  { id: "year-2015", year: 2015 },
  { id: "year-1987", year: 1987 },
];

interface TitleTail extends Atom {
  text: string;
}
const TITLE_TAILS: TitleTail[] = [
  { id: "part-two", text: " - Part Two" },
  { id: "interlude", text: " - Interlude" },
];

/** Dash suffix peeling (R6.2, R8.2) with and without an artist side, incl. non-peelable tails. */
export const dashSuffix: Scenario = {
  name: "dash-suffix",
  description:
    "Dash-suffix versions and years (`Artist - Title - Radio Edit`, `Title - Remastered 2015`) with and without an artist side, non-peelable dashed title tails that must stay in the title, and trailing dash junk (SPEC R6.2, R6.3, R6.8, R8.2).",
  dims: [
    { name: "artist", values: withNone(ARTISTS_MIXED.filter((_, i) => i % 2 === 0)) },
    { name: "title", values: TITLES_MIXED.slice(0, 12) },
    { name: "item", values: ITEMS },
    { name: "remixer", values: withNone(REMIXERS.filter((_, i) => i % 5 === 2).slice(0, 8)) },
    { name: "titleTail", values: withNone(TITLE_TAILS) },
    { name: "junk", values: withNone(["official-video", "hd", "ncs-release", "lyrics"].map(junkById)) },
    { name: "sep", values: SPACED_SEPS.slice(0, 4) },
    { name: "mode", values: ["clean", "youtube", "youtube-yt"].map(modeById) },
  ],
  sample: 150,
  assert: { yearFlags: true, fullVersions: true },
  build: (p, ignore) => {
    const item = p.item as ItemAtom;
    let title = p.title as TitleAtom;
    if (!isNone(p.titleTail!)) {
      if (isNone(p.artist!)) return null; // `T - Part Two - VIP` alone would split artist/title at the first dash
      const tail = p.titleTail as TitleTail;
      title = { ...title, id: `${title.id}-${tail.id}`, text: title.text + tail.text };
    }
    const c: Composition = base({
      main: isNone(p.artist!) ? null : single(p.artist as ArtistAtom),
      title,
      sep: p.sep as SepAtom,
      mode: p.mode as ModeAtom,
    });
    if (item.version) {
      const slots = slotCount(item.version);
      if (slots === 0) ignore("remixer");
      if (slots === 1 && isNone(p.remixer!)) return null;
      c.versions.push({
        atom: item.version,
        delim: "-",
        credits: slots ? [single(p.remixer as ArtistAtom)] : [],
      });
    } else {
      ignore("remixer");
      c.dashYear = item.year!;
    }
    if (!isNone(p.junk!)) c.junk.push({ atom: p.junk as never, form: "dash" });
    return c;
  },
};
