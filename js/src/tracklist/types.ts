/** Public tracklist types. They mirror spec/schema/tracklist.schema.json and options.schema.json. */
import type { KeywordOptions, ModeOption, ParsedTrack } from "../types.js";

export type EntryKind = "track" | "marker" | "header" | "link" | "note" | "blank";
export type Eol = "\n" | "\r\n" | "\r" | "";

/** R13.1.3: `lead + time + gap + rest === raw`. */
export interface EntryParts {
  lead: string;
  time: string;
  gap: string;
  rest: string;
}

/** R13.2.5: `raw` without brackets; `seconds` may carry a fraction. */
export interface TracklistTime {
  raw: string;
  seconds: number;
}

export interface TracklistEntry {
  /** 1-based line number. */
  line: number;
  kind: EntryKind;
  eol: Eol;
  raw: string;
  parts: EntryParts;
  time: TracklistTime | null;
  track: ParsedTrack | null;
}

export type TracklistWarningCode =
  | "time-missing"
  | "time-out-of-order"
  | "time-duplicate"
  | "no-start-at-zero"
  | "time-after-audio-end"
  | "invalid-time"
  | "position-gap"
  | "position-duplicate"
  | "line-unparsed";

export interface TracklistWarning {
  code: TracklistWarningCode;
  line: number;
  severity: "info" | "warning";
  suggestion: TimeSuggestion | null;
}

/** R13.7: a one- or two-digit fix for `time-out-of-order`; `line` is the line to change. */
export interface TimeSuggestion {
  line: number;
  raw: string;
  seconds: number;
}

export interface TracklistStats {
  lines: number;
  tracks: number;
  markers: number;
  timed: number;
  firstTime: number | null;
  lastTime: number | null;
}

export interface Tracklist {
  specVersion: string;
  entries: TracklistEntry[];
  warnings: TracklistWarning[];
  stats: TracklistStats;
}

/** R13.10 */
export interface TracklistOptions {
  /** Per-line parse mode; default `"tracklist"`. */
  mode?: ModeOption;
  knownArtists?: string[];
  splitAnd?: "auto" | "always" | "never";
  keywords?: KeywordOptions;
  /** Length of the audio in seconds; enables `time-after-audio-end`. */
  audioSeconds?: number;
  /** Extra R13.3 marker words. */
  markerWords?: string[];
}

/** R13.9 */
export type Edit =
  | { op: "set-time"; line: number; seconds: number }
  | { op: "shift"; seconds: number; lines?: number[]; from?: number }
  | { op: "sort-by-time" }
  | { op: "set-rest"; line: number; rest: string }
  | { op: "insert"; after: number; text: string }
  | { op: "delete"; line: number };

export type EditWarningCode = "edit-invalid" | "time-clamped" | "style-upgraded";

export interface EditWarning {
  code: EditWarningCode;
  /** Index of the edit in the `edits` array. */
  edit: number;
  line: number | null;
}

export interface EditResult {
  text: string;
  /** Line numbers (in the final text) of lines an edit wrote; sorted, unique. */
  changed: number[];
  warnings: EditWarning[];
}
