/** R13.1–R13.8: parseTracklist. */
import { SPEC_VERSION } from "../data.generated.js";
import { createListParser } from "../parser.js";
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
  track: ParsedTrack | null;
  bare: BareCandidate | null;
}

export function parseTracklist(text: string, options?: TracklistOptions): Tracklist {
  const opts = resolveOptions(options);
  const markers = markerTable(opts.markerWords);
  const lines = splitLines(typeof text === "string" ? text : "");

  const lp = createListParser(opts.parse);

  // R13.2 time scan. R13.4.2: a candidate line has a time or a spaced dash in the skeleton R6
  // searches (after R2/R4); the probe runs R1/R2/R4 only.
  const rows: Row[] = lines.map((line) => {
    const scan = scanTime(line.raw);
    const blank = isBlank(line.raw);
    const row: Row = { line, scan, blank, candidate: false, dash: false, track: null, bare: null };
    if (blank) return row;
    const probe = lp.probe(scan.parts.rest);
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
    if (r.blank) return;
    if (confirmed[i] && r.bare) {
      r.track = lp.parse(r.bare.stripped, listContext);
      r.track.position = { raw: r.bare.raw, number: r.bare.number };
    } else {
      r.track = lp.parse(r.scan.parts.rest, listContext);
    }
  });

  // R13.3
  const entries: TracklistEntry[] = rows.map((r, i) => {
    const kind = kindOf(r, markers);
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
    audioSeconds: opts.audioSeconds,
  });
  return { specVersion: SPEC_VERSION, entries, warnings, stats: statsOf(entries) };
}

function kindOf(r: Row, markers: Parameters<typeof hasMarkerWord>[1]): EntryKind {
  if (r.blank || !r.track) return "blank";
  const separated = hasSeparator(r.track);
  const marker = !separated && hasMarkerWord(r.track.input, markers);
  if (r.scan.time) return marker ? "marker" : "track";
  const rest = r.scan.parts.rest;
  if (isLink(rest)) return "link";
  if (isHeader(rest)) return "header";
  if (marker) return "marker";
  return separated ? "track" : "note";
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
