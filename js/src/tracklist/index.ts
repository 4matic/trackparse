/**
 * trackparse/tracklist — multi-line tracklists (SPEC.md R13): parseTracklist and applyEdits.
 * Rule IDs (R13.2…) in comments refer to spec/SPEC.md.
 */
export { applyEdits } from "./edits.js";
export { parseTracklist } from "./parse.js";
export type {
  Edit,
  EditResult,
  EditWarning,
  EditWarningCode,
  EntryKind,
  EntryParts,
  Eol,
  TimeSuggestion,
  Tracklist,
  TracklistEntry,
  TracklistOptions,
  TracklistStats,
  TracklistTime,
  TracklistWarning,
  TracklistWarningCode,
} from "./types.js";
