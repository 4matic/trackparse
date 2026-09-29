import {
  type ArtistAtom,
  type Atom,
  JUNK,
  JUNK_FORMS,
  type JunkAtom,
  type JunkForm,
  type ModeAtom,
  type TitleAtom,
  junkById,
  modeById,
} from "../atoms.js";
import type { JunkUse } from "../model.js";
import { base, type Scenario } from "../scenario.js";
import { ARTISTS_MIXED, TITLES_MIXED, isNone, single, withNone } from "./pools.js";
import { TAILS, type TailAtom } from "./prefixes.js";

const JUNK2 = [
  "hd", "official-video", "lyrics", "ncs-release", "free-download", "ch-monstercat", "ch-trap-nation", "4k",
].map(junkById);

/** Junk groups/phrases/pipes/platform suffixes in youtube, clean and auto mode (R2.1, R2.2, R2.4, R5.1, R6.2, R8.4). */
export const junkYoutube: Scenario = {
  name: "junk-youtube",
  description:
    "Junk vocabulary in every form (bracket groups, dash suffixes, trailing unbracketed phrases, pipe segments, channel names) with platform suffixes, in youtube, clean and auto mode (auto resolution asserted) (SPEC R2.1, R2.2, R2.4, R5.1, R6.2, R8.4).",
  dims: [
    { name: "junk1", values: JUNK },
    { name: "form1", values: JUNK_FORMS },
    { name: "junk2", values: withNone(JUNK2) },
    { name: "form2", values: JUNK_FORMS },
    {
      name: "mode",
      values: [
        "youtube", "youtube-yt", "youtube-ytm", "youtube-sc", "youtube-sc-dash", "youtube-bandcamp", "youtube-topic",
        "clean", "auto", "auto-yt", "auto-sc",
      ].map(modeById),
    },
    { name: "tail", values: TAILS.filter((t) => !t.id.includes("junk")) },
    { name: "artist", values: ARTISTS_MIXED.slice(0, 12) },
    { name: "title", values: TITLES_MIXED.slice(0, 16) },
  ],
  sample: 300,
  assert: { junk: true },
  build: (p, ignore) => {
    const form = (a: Atom) => (a as Atom & { form: JunkForm }).form;
    const junk: JunkUse[] = [{ atom: p.junk1 as JunkAtom, form: form(p.form1!) }];
    if (isNone(p.junk2!)) {
      ignore("form2");
    } else {
      if (p.junk2!.id === p.junk1!.id) return null;
      junk.push({ atom: p.junk2 as JunkAtom, form: form(p.form2!) });
    }
    const c = base({
      main: single(p.artist as ArtistAtom),
      junk,
      mode: p.mode as ModeAtom,
      title: p.title as TitleAtom,
    });
    (p.tail as TailAtom).apply(c);
    return c;
  },
};
