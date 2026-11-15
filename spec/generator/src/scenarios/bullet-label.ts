import {
  type ArtistAtom,
  type Atom,
  type BulletAtom,
  DELIMS,
  type Delim,
  EXTRAS,
  type ExtraAtom,
  FEAT_MARKERS,
  FEAT_PLACEMENTS,
  type FeatMarkerAtom,
  type FeatPlacement,
  LABELS,
  NOTES,
  type JunkAtom,
  type JunkForm,
  type ModeAtom,
  type PrefixAtom,
  SPACED_SEPS,
  type SepAtom,
  type TitleAtom,
  type TransformAtom,
  VERSIONS,
  type VersionAtom,
  junkById,
  modeById,
  prefixById,
  transformById,
} from "../atoms.js";
import { base, type Scenario } from "../scenario.js";
import { PRODUCERS, type ProducerAtom } from "./feat-placement.js";
import { ARTISTS_MIXED, ARTISTS_SMALL, REMIXERS, TITLES_MIXED, creditFrom, isNone, joiners, single, withNone } from "./pools.js";
import { versionCredits } from "./versions.js";

interface TitleTail extends Atom {
  text: string;
}

/** Dashed title tails that stay in the title (Unknown segments, R6.2 / R8.2 do not peel them). */
const TITLE_TAILS: TitleTail[] = [
  { id: "louder", text: " - Louder" },
  { id: "part-two", text: " - Part Two" },
];

const BL_VERSIONS = VERSIONS.filter((_, i) => i % 4 === 1);
const BL_JUNK = ["official-video", "hd", "free-download", "ncs-release", "official", "ch-ukf"].map(junkById);
const BL_JUNK_FORMS: (Atom & { form: JunkForm })[] = (["paren", "square", "dash", "pipe"] as const).map((form) => ({
  id: form,
  form,
}));

/**
 * R8.7 bullet suffix: `Artist - Title (…) - Suffix • LABEL • NOTE`. The head is any composition
 * the model already encodes with an artist side and a spaced dash; the label and notes are opaque
 * names, so the expectation is the head's own plus `label` and junk kind `note`.
 */
export const bulletLabel: Scenario = {
  name: "bullet-label",
  description:
    "Record labels after a spaced bullet (`Artist - Title (Remix) • LABEL • NOTE`): the first bullet segment is `label`, later ones junk kind `note`, and the head before the bullet parses as it would alone (artist joiners, feat on either side, producers, versions in groups and as dash suffixes, dashed titles, year/flag groups, bracket, dash and pipe junk, prefixes, platforms, auto mode, Unicode transforms) (SPEC R8.7, R6, R2.1, R9.7).",
  dims: [
    { name: "mode", values: ["clean", "youtube", "youtube-yt", "youtube-sc", "auto", "auto-yt"].map(modeById) },
    { name: "prefix", values: withNone(["pos-01-dot", "pos-3-dash", "ts-square", "ts-dash"].map(prefixById)) },
    { name: "a1", values: ARTISTS_MIXED.filter((_, i) => i % 3 === 0) },
    { name: "j1", values: withNone(joiners(["amp", "comma", "x", "vs-dot", "b2b", "and"])) },
    { name: "a2", values: withNone(ARTISTS_SMALL.slice(0, 6)) },
    { name: "placement", values: withNone(FEAT_PLACEMENTS) },
    { name: "marker", values: FEAT_MARKERS },
    { name: "fa", values: ARTISTS_SMALL.slice(7, 12) },
    { name: "sep", values: SPACED_SEPS.slice(0, 4) },
    { name: "title", values: TITLES_MIXED.slice(0, 12) },
    { name: "titleTail", values: withNone(TITLE_TAILS) },
    { name: "version", values: withNone(BL_VERSIONS) },
    { name: "delim", values: DELIMS },
    { name: "remixer", values: withNone(REMIXERS.filter((_, i) => i % 6 === 3).slice(0, 6)) },
    { name: "extra", values: withNone(EXTRAS.filter((_, i) => i % 3 === 0)) },
    { name: "junk", values: withNone(BL_JUNK) },
    { name: "junkForm", values: BL_JUNK_FORMS },
    { name: "producer", values: withNone(PRODUCERS.filter((_, i) => i % 3 === 0)) },
    {
      name: "transform",
      values: ["none", "nfd", "zwsp", "bom", "fullwidth-brackets", "curly-apostrophe"].map(transformById),
    },
    { name: "label", values: LABELS },
    { name: "note1", values: withNone(NOTES) },
    { name: "note2", values: withNone(NOTES) },
  ],
  sample: 60,
  assert: { junk: true, yearFlags: true },
  build: (p, ignore) => {
    let title = p.title as TitleAtom;
    if (!isNone(p.titleTail!)) {
      const tail = p.titleTail as TitleTail;
      title = { ...title, id: `${title.id}-${tail.id}`, text: title.text + tail.text };
    }
    const transform = p.transform as TransformAtom;
    const c = base({
      main: creditFrom(p, ignore, "a1", [["j1", "a2"]]),
      prefix: isNone(p.prefix!) ? null : (p.prefix as PrefixAtom),
      sep: p.sep as SepAtom,
      mode: p.mode as ModeAtom,
      title,
      producer: isNone(p.producer!) ? null : (p.producer as ProducerAtom).use,
      extras: isNone(p.extra!) ? [] : [p.extra as ExtraAtom],
      transform: transform.id === "none" ? null : transform,
      label: (p.label as BulletAtom).text,
      notes: [],
    });
    if (isNone(p.placement!)) {
      ignore("marker");
      ignore("fa");
    } else {
      c.feat = {
        placement: (p.placement as Atom & { placement: FeatPlacement }).placement,
        marker: p.marker as FeatMarkerAtom,
        credit: single(p.fa as ArtistAtom),
      };
    }
    if (isNone(p.version!)) {
      ignore("delim");
      ignore("remixer");
    } else {
      const va = p.version as VersionAtom;
      const credits = versionCredits(va, p, ignore, "remixer");
      if (!credits) return null;
      c.versions.push({ atom: va, delim: (p.delim as Atom & { delim: Delim }).delim, credits });
    }
    if (isNone(p.junk!)) ignore("junkForm");
    else c.junk.push({ atom: p.junk as JunkAtom, form: (p.junkForm as Atom & { form: JunkForm }).form });
    if (isNone(p.note1!)) ignore("note2");
    else {
      c.notes!.push((p.note1 as BulletAtom).text);
      if (!isNone(p.note2!)) c.notes!.push((p.note2 as BulletAtom).text);
    }
    return c;
  },
};
