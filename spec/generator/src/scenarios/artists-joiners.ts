import { ARTISTS, type ModeAtom, type TitleAtom, modeById } from "../atoms.js";
import { base, type Scenario } from "../scenario.js";
import { ALL_JOINERS, ARTISTS_MIXED, ARTISTS_SMALL, SPLIT_AND, TITLES_MIXED, creditFrom, withNone } from "./pools.js";

/** Main credit lists of 1\u20133 names × every joiner × splitAnd (R7.3). */
export const artistsJoiners: Scenario = {
  name: "artists-joiners",
  description:
    "Artist-side credit lists: up to three artists joined by every joiner in data/joiners.json (plus the non-splitting uppercase X), under every splitAnd setting (SPEC R7.3).",
  dims: [
    { name: "a1", values: ARTISTS },
    { name: "j1", values: ALL_JOINERS },
    { name: "a2", values: ARTISTS_MIXED.slice(0, 16) },
    { name: "j2", values: withNone(ALL_JOINERS) },
    { name: "a3", values: withNone(ARTISTS_SMALL) },
    { name: "splitAnd", values: SPLIT_AND },
    { name: "mode", values: ["clean", "youtube", "filename-mp3"].map(modeById) },
    { name: "title", values: TITLES_MIXED.slice(0, 10) },
  ],
  sample: 150,
  build: (p, ignore) => {
    const main = creditFrom(p, ignore, "a1", [
      ["j1", "a2"],
      ["j2", "a3"],
    ]);
    let splitAnd = (p.splitAnd as (typeof SPLIT_AND)[number]).value;
    // splitAnd only matters when an `and` joiner is present.
    if (!main.joiners.some((j) => j.kind === "and")) {
      splitAnd = "auto";
      ignore("splitAnd");
    }
    return base({ main, splitAnd, title: p.title as TitleAtom, mode: p.mode as ModeAtom });
  },
};
