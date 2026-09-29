import {
  ARTISTS,
  type ArtistAtom,
  type Atom,
  type ModeAtom,
  SPACED_SEPS,
  type SepAtom,
  TITLES,
  TRANSFORMS,
  type TitleAtom,
  type TransformAtom,
  artistById,
  featMarkerById,
  junkById,
  modeById,
  versionById,
} from "../atoms.js";
import { type Composition, renderInput } from "../model.js";
import { base, type Scenario } from "../scenario.js";
import { artists, isNone, single, withNone } from "./pools.js";

interface Mod extends Atom {
  apply: (c: Composition) => void;
}

const TAILS: Mod[] = [
  { id: "plain", apply: () => {} },
  {
    id: "paren-remix-tiesto",
    apply: (c) => c.versions.push({ atom: versionById("remix"), delim: "(", credits: [single(artistById("tiesto"))] }),
  },
  {
    id: "square-remix-zveri",
    apply: (c) => c.versions.push({ atom: versionById("rmx"), delim: "[", credits: [single(artistById("zveri"))] }),
  },
  {
    id: "curly-vip",
    apply: (c) => c.versions.push({ atom: versionById("vip-bare"), delim: "{", credits: [] }),
  },
  {
    id: "dnb-remix-utada",
    apply: (c) => c.versions.push({ atom: versionById("dnb-remix"), delim: "(", credits: [single(artistById("utada"))] }),
  },
  { id: "square-junk", apply: (c) => c.junk.push({ atom: junkById("official-video"), form: "square" }) },
  {
    id: "dash-extended",
    apply: (c) => c.versions.push({ atom: versionById("extended-mix"), delim: "-", credits: [] }),
  },
];

const FEATS: Mod[] = [
  {
    id: "title-feat-mo",
    apply: (c) => {
      c.feat = { placement: "title-paren", marker: featMarkerById("feat-dot"), credit: single(artistById("mo")) };
    },
  },
  {
    id: "artist-ft-kino",
    apply: (c) => {
      c.feat = { placement: "artist-inline", marker: featMarkerById("ft-dot"), credit: single(artistById("kino")) };
    },
  },
  {
    id: "title-with-beyonce",
    apply: (c) => {
      c.feat = { placement: "title-square", marker: featMarkerById("with"), credit: single(artistById("beyonce")) };
    },
  },
];

const UNICODE_ARTISTS = [
  ...ARTISTS.filter((a) => a.cat === "cyrillic" || a.cat === "diacritics" || a.cat === "cjk"),
  ...artists(["noisia", "asap-rocky", "mr-oizo", "will-i-am"]),
];
const UNICODE_TITLES = TITLES.filter((t) =>
  ["cyrillic", "diacritics", "cjk", "apostrophe", "group"].includes(t.cat),
);

/** R1 normalization: NFD, invisible chars, fullwidth brackets, quotes, dash and space variants. */
export const unicode: Scenario = {
  name: "unicode",
  description:
    "Cyrillic, diacritic and CJK names/titles under every R1 normalization: NFD input, zero-width/format characters, fullwidth brackets, curly apostrophes, exotic spaces and dash variants as separators. Expected values are the NFC/ASCII-punctuation forms (SPEC R1).",
  dims: [
    { name: "artist", values: UNICODE_ARTISTS },
    { name: "title", values: UNICODE_TITLES },
    { name: "transform", values: TRANSFORMS },
    { name: "sep", values: SPACED_SEPS },
    { name: "tail", values: TAILS },
    { name: "feat", values: withNone(FEATS) },
    { name: "mode", values: ["clean", "youtube"].map(modeById) },
  ],
  sample: 300,
  build: (p, ignore) => {
    const t = p.transform as TransformAtom;
    const c = base({
      main: single(p.artist as ArtistAtom),
      sep: p.sep as SepAtom,
      mode: p.mode as ModeAtom,
      title: p.title as TitleAtom,
      transform: null,
    });
    (p.tail as Mod).apply(c);
    if (!isNone(p.feat!)) (p.feat as Mod).apply(c);
    if (t.id !== "none") {
      // A transform that leaves this input unchanged would just duplicate the `none` case.
      const plain = renderInput(c);
      if (t.apply(plain) === plain) ignore("transform");
      else c.transform = t;
    }
    return c;
  },
};
