import {
  EMPHASIS_LEADS,
  type EolAtom,
  EOLS,
  type FrameAtom,
  FRAMES,
  type GapAtom,
  GAPS,
  type LeadAtom,
  type PositionStyleAtom,
  POSITION_STYLES,
  TIME_FORMS,
  type TimeFormAtom,
  TRACK_COUNTS,
  TRACK_SETS,
  type TrackSetAtom,
  type WrapAtom,
  WRAPS,
} from "../atoms.js";
import type { Scenario } from "../scenario.js";
import { renderTracklist, tracklistProblem, type TracklistComposition } from "../tracklist.js";

/** Tracklists pasted with Markdown: bullets and emphasis around times and track lines (SPEC R13.2, R13.12). */
export const tracklistEmphasis: Scenario = {
  name: "tracklist-emphasis",
  fn: "parseTracklist",
  description:
    "parseTracklist over 1–6 line texts with Markdown: list bullets before the time, emphasis runs around the time and its brackets (**T**, **[T]**, [**T**], **[**T**], __T__, *T*, _[T]_, an opener without a closer) and emphasis around each track line (**, __, *, _), crossed with time forms, gaps, positions and line endings. The expected track never changes: markers are left out of the line text (SPEC R13.2.1, R13.2.3, R13.4.1, R13.12).",
  dims: [
    { name: "time", values: TIME_FORMS.filter((t) => t.fmt !== null) },
    { name: "lead", values: EMPHASIS_LEADS },
    { name: "wrap", values: WRAPS },
    { name: "gap", values: GAPS },
    { name: "pos", values: POSITION_STYLES },
    { name: "eol", values: EOLS },
    { name: "count", values: TRACK_COUNTS.filter((c) => c.n <= 2) },
    { name: "set", values: TRACK_SETS },
  ],
  sample: 0,
  buildCase: (p) => {
    const n = (p.count as (typeof TRACK_COUNTS)[number]).n;
    const c: TracklistComposition = {
      time: p.time as TimeFormAtom,
      lead: p.lead as LeadAtom,
      gap: p.gap as GapAtom,
      pos: p.pos as PositionStyleAtom,
      eol: p.eol as EolAtom,
      // Extra header/blank/note lines carry no emphasis; the tracklist scenario covers them.
      frame: FRAMES[0] as FrameAtom,
      order: "zero",
      tracks: (p.set as TrackSetAtom).tracks.slice(0, n),
      wrap: p.wrap as WrapAtom,
    };
    return tracklistProblem(c) === null ? renderTracklist(c) : null;
  },
};
