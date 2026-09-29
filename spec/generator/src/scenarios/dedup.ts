import {
  type ArtistAtom,
  type Atom,
  type TitleAtom,
  featMarkerById,
  modeById,
  producerMarkerById,
  versionById,
} from "../atoms.js";
import type { CreditList } from "../model.js";
import { base, type Scenario } from "../scenario.js";
import { artistProblem, dedupKey } from "../text.js";
import { artists, isNone, joiners, titles, withNone } from "./pools.js";

interface FormAtom extends Atom {
  apply: (s: string) => string;
}

const FORMS: FormAtom[] = [
  { id: "exact", apply: (s) => s },
  { id: "upper", apply: (s) => s.toUpperCase() },
  { id: "lower", apply: (s) => s.toLowerCase() },
  { id: "no-diacritics", apply: (s) => s.normalize("NFD").replace(/[\u0300-\u036F]/g, "").normalize("NFC") },
];

type Placement = "title-paren" | "title-inline" | "artist-inline" | "artist-square" | "producer" | "remixer";
const PLACEMENTS: (Atom & { p: Placement })[] = (
  ["title-paren", "title-inline", "artist-inline", "artist-square", "producer", "remixer"] as Placement[]
).map((p) => ({ id: p, p }));

const PRIMARIES = artists([
  "noisia", "culture-shock", "fox-stevenson", "sigur-ros", "beyonce", "tiesto", "motley-crue", "bjork",
  "lyapis", "zveri", "asap-rocky", "mr-oizo",
]);

/**
 * R9.2 dedup: the same artist credited again as featured (case/diacritic variants) is dropped;
 * producer credits and self-remixes are separate classes and are kept.
 */
export const dedupScenario: Scenario = {
  name: "dedup",
  description:
    "The primary artist credited a second time (exact, upper-, lower-case, diacritics stripped) as featured on either side \u2014 dropped by R9.2 using R0.4 keys \u2014 or as producer or remixer, which are separate credit classes and kept (SPEC R0.4, R9.2).",
  dims: [
    { name: "primary", values: PRIMARIES },
    { name: "form", values: FORMS },
    { name: "placement", values: PLACEMENTS },
    { name: "other", values: withNone(artists(["phace"])) },
    { name: "title", values: titles(["diplodocus", "gruppa-krovi"]) },
  ],
  sample: 0,
  build: (p) => {
    const prim = p.primary as ArtistAtom;
    const form = p.form as FormAtom;
    const text = form.apply(prim.text);
    if (form.id !== "exact" && text === prim.text) return null; // no-op variant
    if (dedupKey(text) !== dedupKey(prim.text)) return null; // not a dup under R0.4 (e.g. ø)
    if (artistProblem(text, true)) return null;
    const dup: ArtistAtom = { ...prim, id: `${prim.id}-${form.id}`, text, expect: [{ name: text }] };
    const list: CreditList = isNone(p.other!)
      ? { names: [dup], joiners: [] }
      : { names: [dup, p.other as ArtistAtom], joiners: joiners(["amp"]) };
    const c = base({
      main: { names: [prim], joiners: [] },
      title: p.title as TitleAtom,
      mode: modeById("clean"),
      allowDup: true,
    });
    const placement = (p.placement as Atom & { p: Placement }).p;
    switch (placement) {
      case "title-paren":
      case "title-inline":
      case "artist-inline":
      case "artist-square":
        c.feat = { placement, marker: featMarkerById("ft-dot"), credit: list };
        break;
      case "producer":
        c.producer = { marker: producerMarkerById("prod-by"), credit: list, form: "paren" };
        break;
      case "remixer":
        c.versions.push({ atom: versionById("remix"), delim: "(", credits: [list] });
        break;
    }
    return c;
  },
};
