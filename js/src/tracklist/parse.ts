/** R13.1–R13.8: parseTracklist. */
import { SPEC_VERSION } from "../data.generated.js";
import { createListParser, type LineProbe } from "../parser.js";
import type { ParsedTrack } from "../types.js";
import { hasMarkerWord, hasSeparator, isBlank, isHeader, isLink, markerTable } from "./kinds.js";
import { resolveOptions } from "./options.js";
import { type BareCandidate, bareCandidate, confirmPositions } from "./positions.js";
import { type Line, splitLines } from "./split.js";
import { scanTime, type TimeScan } from "./times.js";
import type { EntryKind, Tracklist, TracklistEntry, TracklistOptions } from "./types.js";
import { tracklistWarnings } from "./warnings.js";

interface Row {
  line: Line;
  scan: TimeScan;
  blank: boolean;
  /** R13.4.2 */
  candidate: boolean;
  dash: boolean;
  probe: LineProbe | null;
  bare: BareCandidate | null;
  track: ParsedTrack | null;
  /** R13.3: the track part has a separator. */
  separated: boolean;
  /** R13.3: the track part has a marker word. */
  marker: boolean;
}

export function parseTracklist(text: string, options?: TracklistOptions): Tracklist {
  const opts = resolveOptions(options);
  const markers = markerTable(opts.markerWords);
  const lines = splitLines(typeof text === "string" ? text : "");

  const lp = createListParser(opts.parse);

  // R13.2 time scan. R13.4.2: a candidate line has a time or a spaced dash in the skeleton R6
  // searches (after R2/R4); the probe runs R1/R2/R4 once and is finished below (R13.4.1).
  const rows: Row[] = lines.map((line) => {
    const scan = scanTime(line.raw);
    const blank = isBlank(line.raw);
    const row: Row = {
      line,
      scan,
      blank,
      candidate: false,
      dash: false,
      probe: null,
      bare: null,
      track: null,
      separated: false,
      marker: false,
    };
    if (blank) return row;
    const probe = lp.probe(scan.parts.rest);
    row.probe = probe;
    row.dash = probe.spacedDash;
    row.candidate = scan.time !== null || row.dash;
    row.bare = bareCandidate(scan.parts.rest, probe.position); // R13.5
    return row;
  });
  let candidates = 0;
  let dashes = 0;
  for (const r of rows) {
    if (!r.candidate) continue;
    candidates++;
    if (r.dash) dashes++;
  }
  const listContext = candidates > 0 && 2 * dashes >= candidates;

  // R13.5: confirmed bare positions are removed before parsing. R13.4.1: one parse per line.
  const confirmed = confirmPositions(
    rows.map((r) => r.bare),
    rows.map((r) => r.candidate),
  );
  rows.forEach((r, i) => {
    if (r.blank || !r.probe) return;
    const bare = confirmed[i] ? r.bare : null;
    const probe = bare ? lp.probe(bare.stripped) : r.probe;
    r.marker = hasMarkerWord(bare ? bare.stripped : r.scan.parts.rest, markers);
    // R13.4.2: the R6.10 tight dash only on timed lines without a marker word.
    const track = probe.finish(listContext && r.scan.time !== null && !r.marker);
    if (bare) track.position = { raw: bare.raw, number: bare.number };
    r.track = track;
    r.separated = hasSeparator(track, probe.empty);
  });

  // R13.3
  const entries: TracklistEntry[] = rows.map((r, i) => {
    const kind = kindOf(r);
    const keep = kind === "track" || kind === "marker";
    return {
      line: i + 1,
      kind,
      eol: r.line.eol,
      raw: r.line.raw,
      parts: r.scan.parts,
      time: r.scan.time,
      track: keep ? r.track : null,
    };
  });

  const warnings = tracklistWarnings({
    entries,
    invalid: rows.map((r) => r.scan.invalid),
    // R13.7 line-unparsed: a track entry whose track part has no separator (R13.3).
    unparsed: rows.map((r, i) => (entries[i] as TracklistEntry).kind === "track" && !r.separated),
    audioSeconds: opts.audioSeconds,
  });
  return { specVersion: SPEC_VERSION, entries, warnings, stats: statsOf(entries) };
}

function kindOf(r: Row): EntryKind {
  if (r.blank || !r.track) return "blank";
  const marker = !r.separated && r.marker;
  if (r.scan.time) return marker ? "marker" : "track";
  const rest = r.scan.parts.rest;
  if (isLink(rest)) return "link";
  if (isHeader(rest)) return "header";
  if (marker) return "marker";
  return r.separated ? "track" : "note";
}

function statsOf(entries: readonly TracklistEntry[]): Tracklist["stats"] {
  let tracks = 0;
  let markers = 0;
  let timed = 0;
  let firstTime: number | null = null;
  let lastTime: number | null = null;
  for (const e of entries) {
    if (e.kind === "track") tracks++;
    if (e.kind === "marker") markers++;
    if (e.time) {
      timed++;
      firstTime ??= e.time.seconds;
      lastTime = e.time.seconds;
    }
  }
  return { lines: entries.length, tracks, markers, timed, firstTime, lastTime };
}
