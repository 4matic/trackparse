import {
  type ArtistAtom,
  type Atom,
  type ModeAtom,
  PREFIXES,
  type PrefixAtom,
  type SepAtom,
  type TitleAtom,
  artistById,
  junkById,
  modeById,
  sepById,
  versionById,
} from "../atoms.js";
import type { Composition } from "../model.js";
import { base, type Scenario } from "../scenario.js";
import { artists, isNone, single, titles, withNone } from "./pools.js";

export interface TailAtom extends Atom {
  apply: (c: Composition) => void;
}

export const TAILS: TailAtom[] = [
  { id: "plain", apply: () => {} },
  {
    id: "dash-vip",
    apply: (c) => {
      c.versions.push({ atom: versionById("vip-bare"), delim: "-", credits: [] });
    },
  },
  {
    id: "paren-remix",
    apply: (c) => {
      c.versions.push({ atom: versionById("remix"), delim: "(", credits: [single(artistById("enei"))] });
    },
  },
  {
    id: "dash-radio-edit",
    apply: (c) => {
      c.versions.push({ atom: versionById("radio-edit"), delim: "-", credits: [] });
    },
  },
  {
    id: "square-junk",
    apply: (c) => {
      c.junk.push({ atom: junkById("official-video"), form: "square" });
    },
  },
  {
    id: "dash-junk",
    apply: (c) => {
      c.junk.push({ atom: junkById("hd"), form: "dash" });
    },
  },
];

const NUMERIC_AND_FRIENDS = artists([
  "2-unlimited", "808-state", "50-cent", "21-savage", "m83", "311", "112", "blink-182", "noisia",
  "culture-shock", "zveri", "sigur-ros", "utada", "asap-rocky", "mr-oizo", "deadmau5",
]);

/** Timestamps and positions (R4.1, R4.2) × numeric artist names that must not be eaten. */
export const prefixes: Scenario = {
  name: "prefixes",
  description:
    "Leading timestamps and track positions in every R4.1/R4.2 form (incl. filename-only forms) combined with numeric artist names (2 Unlimited, 808 State, 311\u2026) that must survive, with and without further dash suffixes (SPEC R4).",
  dims: [
    { name: "prefix", values: withNone(PREFIXES) },
    { name: "artist", values: NUMERIC_AND_FRIENDS },
    { name: "tail", values: TAILS },
    { name: "sep", values: ["hyphen", "en-dash", "asym-right", "pipe", "unspaced"].map(sepById) },
    { name: "mode", values: ["clean", "youtube", "filename-mp3", "filename-mp3-us"].map(modeById) },
    { name: "title", values: titles(["diplodocus", "one-more-time", "gruppa-krovi", "99-problems", "hey-jude", "deja-vu"]) },
  ],
  sample: 250,
  assert: { position: true },
  build: (p) => {
    const c = base({
      main: single(p.artist as ArtistAtom),
      prefix: isNone(p.prefix!) ? null : (p.prefix as PrefixAtom),
      sep: p.sep as SepAtom,
      mode: p.mode as ModeAtom,
      title: p.title as TitleAtom,
    });
    (p.tail as TailAtom).apply(c);
    return c;
  },
};
