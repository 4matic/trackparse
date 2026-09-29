import {
  type ArtistAtom,
  type Atom,
  DELIMS,
  type Delim,
  EXTRAS,
  type ExtraAtom,
  FEAT_MARKERS,
  FEAT_PLACEMENTS,
  type FeatMarkerAtom,
  type FeatPlacement,
  JUNK_FORMS,
  type JunkAtom,
  type JunkForm,
  type ModeAtom,
  type PrefixAtom,
  type SepAtom,
  TITLES,
  type TitleAtom,
  VERSIONS,
  type VersionAtom,
  junkById,
  modeById,
  prefixById,
  sepById,
} from "../atoms.js";
import { base, type Scenario } from "../scenario.js";
import { PRODUCERS, type ProducerAtom } from "./feat-placement.js";
import { ARTISTS_MIXED, ARTISTS_SMALL, REMIXERS, creditFrom, isNone, joiners, single, withNone } from "./pools.js";
import { versionCredits } from "./versions.js";

const KS_VERSIONS = VERSIONS.filter((_, i) => i % 5 === 0 || i % 7 === 3);
const KS_JUNK = [
  "official-video", "official-audio", "lyrics", "hd", "4k", "free-download", "ncs-release", "hospital-records",
  "dubstep", "official-video-hd", "letra", "ch-monstercat", "ch-ukf", "premiere",
].map(junkById);

/** Everything at once: pairwise over all dimensions plus a large seeded random sample. */
export const kitchenSink: Scenario = {
  name: "kitchen-sink",
  description:
    "All dimensions at once (mode, prefix, credit list, feat placement/marker, separator, title, version/delimiter/remixer, extras, junk/form, producer): a deterministic greedy pairwise covering array plus a seeded random sample of valid compositions.",
  dims: [
    { name: "mode", values: ["clean", "youtube", "youtube-yt", "youtube-sc", "filename-mp3", "filename-flac-us"].map(modeById) },
    {
      name: "prefix",
      values: withNone(
        ["pos-01-dot", "pos-1-dot", "pos-01-space", "pos-3-dash", "ts-square", "ts-dash", "ts-pos-a", "file-01-hyphen"].map(
          prefixById,
        ),
      ),
    },
    { name: "a1", values: ARTISTS_MIXED },
    { name: "j1", values: withNone(joiners(["amp", "comma", "x", "vs-dot", "and", "with", "b2b", "times"])) },
    { name: "a2", values: withNone(ARTISTS_SMALL) },
    { name: "placement", values: withNone(FEAT_PLACEMENTS) },
    { name: "marker", values: FEAT_MARKERS },
    { name: "fa", values: [...ARTISTS_SMALL.slice(7), ...ARTISTS_MIXED.slice(3, 9)] },
    { name: "sep", values: ["hyphen", "en-dash", "em-dash", "pipe", "unspaced", "asym-left"].map(sepById) },
    { name: "title", values: TITLES },
    { name: "version", values: withNone(KS_VERSIONS) },
    { name: "delim", values: DELIMS },
    { name: "remixer", values: withNone(REMIXERS.filter((_, i) => i % 4 === 1).slice(0, 12)) },
    { name: "extra", values: withNone(EXTRAS.filter((_, i) => i % 2 === 0)) },
    { name: "junk", values: withNone(KS_JUNK) },
    { name: "junkForm", values: JUNK_FORMS },
    { name: "producer", values: withNone(PRODUCERS.filter((_, i) => i % 2 === 0)) },
  ],
  sample: (cfg) => cfg.kitchenSinkSample,
  build: (p, ignore) => {
    const main = creditFrom(p, ignore, "a1", [["j1", "a2"]]);
    const c = base({
      main,
      prefix: isNone(p.prefix!) ? null : (p.prefix as PrefixAtom),
      sep: p.sep as SepAtom,
      mode: p.mode as ModeAtom,
      title: p.title as TitleAtom,
      producer: isNone(p.producer!) ? null : (p.producer as ProducerAtom).use,
      extras: isNone(p.extra!) ? [] : [p.extra as ExtraAtom],
    });
    // feat (canonical marker/fa when absent)
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
    // version
    const delim = (p.delim as Atom & { delim: Delim }).delim;
    if (isNone(p.version!)) {
      ignore("delim");
      ignore("remixer");
    } else {
      const va = p.version as VersionAtom;
      const credits = versionCredits(va, p, ignore, "remixer");
      if (!credits) return null;
      c.versions.push({ atom: va, delim, credits });
    }
    // junk
    const form = (p.junkForm as Atom & { form: JunkForm }).form;
    if (isNone(p.junk!)) ignore("junkForm");
    else c.junk.push({ atom: p.junk as JunkAtom, form });
    return c;
  },
};
