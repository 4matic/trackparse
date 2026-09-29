import {
  type ArtistAtom,
  type Atom,
  FEAT_MARKERS,
  FEAT_PLACEMENTS,
  type FeatMarkerAtom,
  type FeatPlacement,
  type ModeAtom,
  type SepAtom,
  type TitleAtom,
  artistById,
  modeById,
  producerMarkerById,
  sepById,
} from "../atoms.js";
import type { ProducerUse } from "../model.js";
import { base, type Scenario } from "../scenario.js";
import { ARTISTS_MIXED, ARTISTS_SMALL, TITLES_MIXED, creditFrom, isNone, joiners, single, withNone } from "./pools.js";

export interface ProducerAtom extends Atom {
  use: ProducerUse;
}

const prod = (id: string, marker: string, form: ProducerUse["form"], artistId: string): ProducerAtom => ({
  id,
  use: { marker: producerMarkerById(marker), credit: single(artistById(artistId)), form },
});

export const PRODUCERS: ProducerAtom[] = [
  prod("prod-paren-camo", "prod-dot", "paren", "camo"),
  prod("prod-cap-square-enei", "prod-dot-cap", "square", "enei"),
  prod("prod-by-paren-netsky", "prod-by", "paren", "netsky"),
  prod("prod-by-cap-square-krooked", "prod-by-cap", "square", "krooked"),
  prod("produced-by-paren-gydra", "produced-by", "paren", "gydra"),
  prod("prod-nodot-paren-phace", "prod", "paren", "phace"),
  prod("prodby-tight-paren-mefjus", "prodby-tight", "paren", "mefjus"),
  prod("prod-inline-kino", "prod-dot", "inline", "kino"),
  prod("prod-by-inline-mo", "prod-by", "inline", "mo"),
  prod("produced-by-inline-deadmau5", "produced-by", "inline", "deadmau5"),
];

/** Feat marker × placement × featured list shape × separator/mode (R5.2, R7.1, R7.2, R8.1, R8.3, R9.1). */
export const featPlacement: Scenario = {
  name: "feat-placement",
  description:
    "Featured credits in every placement (artist side unbracketed/bracketed, title bracketed/unbracketed) with every feat marker, 1\u20132 featured names, optional producer credit, across separators and modes (SPEC R5.2, R5.3, R7.1, R7.2, R8.1, R8.3, R9.1).",
  dims: [
    { name: "primary", values: ARTISTS_MIXED.slice(0, 22) },
    { name: "placement", values: FEAT_PLACEMENTS },
    { name: "marker", values: FEAT_MARKERS },
    { name: "f1", values: [...ARTISTS_MIXED.slice(10), ...ARTISTS_SMALL.slice(0, 6)] },
    { name: "fj", values: withNone(joiners(["amp", "comma", "x", "and", "with", "vs-dot", "times", "b2b"])) },
    { name: "f2", values: withNone(ARTISTS_SMALL.slice(4)) },
    { name: "producer", values: withNone(PRODUCERS) },
    { name: "sep", values: ["hyphen", "en-dash", "pipe", "asym-left", "unspaced"].map(sepById) },
    { name: "mode", values: ["clean", "youtube", "youtube-yt", "filename-mp3"].map(modeById) },
    { name: "title", values: TITLES_MIXED.slice(0, 14) },
  ],
  sample: 250,
  build: (p, ignore) => {
    const featCredit = creditFrom(p, ignore, "f1", [["fj", "f2"]]);
    const placement = (p.placement as Atom & { placement: FeatPlacement }).placement;
    return base({
      main: single(p.primary as ArtistAtom),
      feat: { placement, marker: p.marker as FeatMarkerAtom, credit: featCredit },
      producer: isNone(p.producer!) ? null : (p.producer as ProducerAtom).use,
      sep: p.sep as SepAtom,
      mode: p.mode as ModeAtom,
      title: p.title as TitleAtom,
    });
  },
};
