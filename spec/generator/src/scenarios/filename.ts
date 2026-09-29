import {
  ARTISTS,
  type ArtistAtom,
  type Atom,
  type ModeAtom,
  type PrefixAtom,
  type SepAtom,
  type TitleAtom,
  artistById,
  featMarkerById,
  modeById,
  prefixById,
  sepById,
} from "../atoms.js";
import type { FeatUse } from "../model.js";
import { base, type Scenario } from "../scenario.js";
import { TITLES_MIXED, TITLES_SMALL, isNone, single, withNone } from "./pools.js";
import { TAILS, type TailAtom } from "./prefixes.js";

interface FeatAtom extends Atom {
  use: FeatUse;
}

const FEATS: FeatAtom[] = [
  {
    id: "ft-inline-foreign-beggars",
    use: { placement: "artist-inline", marker: featMarkerById("ft-dot"), credit: single(artistById("foreign-beggars")) },
  },
  {
    id: "feat-paren-kino",
    use: { placement: "title-paren", marker: featMarkerById("feat-dot"), credit: single(artistById("kino")) },
  },
  {
    id: "feat-inline-title-camo",
    use: { placement: "title-inline", marker: featMarkerById("feat-dot"), credit: single(artistById("camo")) },
  },
];

/** Filenames: extensions, underscores, filename-only position forms, unspaced dashes (R2.3, R4.2e, R6.7). */
export const filename: Scenario = {
  name: "filename",
  description:
    "Filename mode: extension stripping, underscore-for-space names, filename-only position prefixes (01-, 01_, 01.), unspaced dash separators and numeric artists (SPEC R2.3, R4.2, R6.7).",
  dims: [
    { name: "artist", values: ARTISTS.filter((_, i) => i % 2 === 0) },
    { name: "title", values: [...TITLES_MIXED.slice(0, 12), ...TITLES_SMALL.slice(0, 6)] },
    {
      name: "mode",
      values: [
        "filename-mp3", "filename-flac", "filename-m4a", "filename-opus", "filename-wav", "filename-webm",
        "filename-mp3-us", "filename-flac-us",
      ].map(modeById),
    },
    {
      name: "prefix",
      values: withNone(
        ["pos-01-dot", "pos-1-dot", "pos-003-dot", "pos-12-paren", "file-01-hyphen", "file-01-underscore", "file-01-dot"].map(
          prefixById,
        ),
      ),
    },
    { name: "tail", values: TAILS },
    { name: "sep", values: ["hyphen", "unspaced", "asym-left", "en-dash"].map(sepById) },
    { name: "feat", values: withNone(FEATS) },
  ],
  sample: 150,
  assert: { position: true },
  build: (p) => {
    const c = base({
      main: single(p.artist as ArtistAtom),
      prefix: isNone(p.prefix!) ? null : (p.prefix as PrefixAtom),
      feat: isNone(p.feat!) ? null : (p.feat as FeatAtom).use,
      sep: p.sep as SepAtom,
      mode: p.mode as ModeAtom,
      title: p.title as TitleAtom,
    });
    (p.tail as TailAtom).apply(c);
    return c;
  },
};
