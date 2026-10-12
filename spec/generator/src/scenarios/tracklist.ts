import {
  type EolAtom,
  EOLS,
  type FrameAtom,
  FRAMES,
  type GapAtom,
  GAPS,
  type LeadAtom,
  LEADS,
  type PositionStyleAtom,
  POSITION_STYLES,
  TIME_FORMS,
  TIME_ORDERS,
  type TimeFormAtom,
  TRACK_COUNTS,
  TRACK_SETS,
  type TrackSetAtom,
} from "../atoms.js";
import type { Scenario } from "../scenario.js";
import { renderTracklist, tracklistProblem, type TracklistComposition } from "../tracklist.js";

/** Multi-line tracklists (fn parseTracklist): times, gaps, positions, line endings, line kinds (SPEC R13). */
export const tracklist: Scenario = {
  name: "tracklist",
  fn: "parseTracklist",
  description:
    "parseTracklist over 1–6 line texts: every R13.2 time form (incl. fractions, minutes above 59, hours) × lead/brackets × gap separators × dotted/padded/bare positions × LF/CRLF/CR/mixed line endings with and without a final terminator × header/blank/marker/note/link lines × time orders producing no-start-at-zero, time-duplicate and time-missing (SPEC R13, R6.10).",
  dims: [
    { name: "time", values: TIME_FORMS },
    { name: "lead", values: LEADS },
    { name: "gap", values: GAPS },
    { name: "pos", values: POSITION_STYLES },
    { name: "eol", values: EOLS },
    { name: "frame", values: FRAMES },
    { name: "order", values: TIME_ORDERS },
    { name: "count", values: TRACK_COUNTS },
    { name: "set", values: TRACK_SETS },
  ],
  sample: 700,
  buildCase: (p, ignore) => {
    const time = p.time as TimeFormAtom;
    if (time.fmt === null) {
      // Untimed list: lead, gap and time order have nothing to act on.
      ignore("lead");
      ignore("gap");
      ignore("order");
    }
    const n = (p.count as (typeof TRACK_COUNTS)[number]).n;
    const c: TracklistComposition = {
      time,
      lead: p.lead as LeadAtom,
      gap: p.gap as GapAtom,
      pos: p.pos as PositionStyleAtom,
      eol: p.eol as EolAtom,
      frame: p.frame as FrameAtom,
      order: time.fmt === null ? "zero" : (p.order as (typeof TIME_ORDERS)[number]).order,
      tracks: (p.set as TrackSetAtom).tracks.slice(0, n),
    };
    return tracklistProblem(c) === null ? renderTracklist(c) : null;
  },
};
