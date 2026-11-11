//! R13.1–R13.8: parse_tracklist.

use crate::data::SPEC_VERSION;
use crate::json::Json;
use crate::parser::{LineProbe, ListParser};
use crate::types::{Position, RawTrack};
use crate::words::{chars, string, tail};

use super::kinds::{
    has_marker_word, has_separator, is_blank, is_header, is_link, layered_prefix, marker_table,
};
use super::options::ResolvedOptions;
use super::positions::{bare_candidate, confirm_positions, BareCandidate};
use super::split::{split_lines, Line};
use super::times::{scan_time, TimeScan};
use super::types::{tracklist_json, EntryKind, Stats, Tracklist, TracklistEntry, TracklistWarning};
use super::warnings::{tracklist_warnings, WarningInput};

struct Row<'a> {
    line: Line,
    scan: TimeScan,
    /// R13.4.1 line text: rest without emphasis markers (R13.12) and a layered prefix (R13.13).
    text: String,
    /// R13.13
    layered: bool,
    blank: bool,
    /// R13.4.2
    candidate: bool,
    dash: bool,
    probe: Option<LineProbe<'a>>,
    bare: Option<BareCandidate>,
    track: Option<RawTrack>,
    /// R13.3: the track part has a separator.
    separated: bool,
    /// R13.3: the track part has a marker word.
    marker: bool,
}

/// A parsed tracklist with the pipeline's own tracks (see `RawTrack`): `entries[i].track` is
/// empty and `tracks[i]` holds it. The wire writes this form; the typed API converts it.
pub(crate) struct RawTracklist {
    entries: Vec<TracklistEntry>,
    tracks: Vec<Option<RawTrack>>,
    warnings: Vec<TracklistWarning>,
    stats: Stats,
}

impl RawTracklist {
    pub(crate) fn into_tracklist(self) -> Tracklist {
        let mut entries = self.entries;
        for (entry, track) in entries.iter_mut().zip(self.tracks) {
            entry.track = track.map(RawTrack::into_track);
        }
        Tracklist {
            spec_version: SPEC_VERSION.to_string(),
            entries,
            warnings: self.warnings,
            stats: self.stats,
        }
    }

    /// The spec's JSON form, written exactly as the JS reference would.
    pub(crate) fn to_json_value(&self) -> Json {
        let entries = self
            .entries
            .iter()
            .zip(&self.tracks)
            .map(|(e, t)| e.json_with(t.as_ref().map_or(Json::Null, RawTrack::to_json_value)))
            .collect();
        tracklist_json(SPEC_VERSION, entries, &self.warnings, &self.stats)
    }
}

pub(crate) fn parse_tracklist(text: &str, opts: &ResolvedOptions) -> RawTracklist {
    let markers = marker_table(&opts.marker_words);
    let lp = ListParser::new(opts.parse.clone());

    // R13.2 time scan. R13.4.2: a candidate line has a time or a spaced dash in the skeleton R6
    // searches (after R2/R4); the probe runs R1/R2/R4 once and is finished below (R13.4.1).
    let mut rows: Vec<Row<'_>> = split_lines(text)
        .into_iter()
        .map(|line| {
            let scan = scan_time(&line.raw);
            let blank = is_blank(&line.raw);
            let scanned = chars(&scan.text);
            let layered = if blank { 0 } else { layered_prefix(&scanned) };
            let mut row = Row {
                line,
                scan,
                text: string(tail(&scanned, layered)),
                layered: layered > 0,
                blank,
                candidate: false,
                dash: false,
                probe: None,
                bare: None,
                track: None,
                separated: false,
                marker: false,
            };
            if blank {
                return row;
            }
            let probe = lp.probe(&row.text); // R13.4.1
            row.dash = probe.spaced_dash;
            row.candidate = row.scan.time.is_some() || row.dash;
            // R13.5 / R13.13.3: a layered line never has a bare position.
            row.bare = if row.layered { None } else { bare_candidate(&row.text, probe.position) };
            row.probe = Some(probe);
            row
        })
        .collect();
    let candidates = rows.iter().filter(|r| r.candidate).count();
    let dashes = rows.iter().filter(|r| r.candidate && r.dash).count();
    let list_context = candidates > 0 && 2 * dashes >= candidates;

    // R13.5: confirmed bare positions are removed before parsing. R13.4.1: one parse per line.
    // R13.13.3: layered lines are not neighbours for the R13.5 search.
    let confirmed = confirm_positions(
        &rows.iter().map(|r| r.bare.as_ref().map(|b| b.number)).collect::<Vec<_>>(),
        &rows.iter().map(|r| r.candidate && !r.layered).collect::<Vec<_>>(),
    );
    for (r, is_position) in rows.iter_mut().zip(confirmed) {
        let Some(own_probe) = r.probe.take() else { continue };
        let bare = if is_position { r.bare.as_ref() } else { None };
        let probe = match bare {
            Some(b) => lp.probe(&b.stripped),
            None => own_probe,
        };
        r.marker = has_marker_word(bare.map_or(&r.text, |b| &b.stripped), &markers);
        let empty = probe.empty;
        // R13.4.2: the R6.10 tight dash only on timed lines without a marker word.
        let mut track = probe.finish(list_context && r.scan.time.is_some() && !r.marker);
        if let Some(b) = bare {
            track.position = Some(Position { raw: b.raw.clone(), number: b.number });
        }
        r.separated = has_separator(&track, empty);
        r.track = Some(track);
    }

    // R13.3
    let mut entries = Vec::with_capacity(rows.len());
    let mut tracks = Vec::with_capacity(rows.len());
    let mut positions = Vec::with_capacity(rows.len());
    let mut invalid = Vec::with_capacity(rows.len());
    let mut unparsed = Vec::with_capacity(rows.len());
    for (i, r) in rows.into_iter().enumerate() {
        let kind = kind_of(&r);
        let keep = kind == EntryKind::Track || kind == EntryKind::Marker;
        let track = if keep { r.track } else { None };
        positions.push(track.as_ref().and_then(|t| t.position.as_ref()).map(|p| p.number));
        invalid.push(r.scan.invalid);
        // R13.7 line-unparsed: a track entry whose track part has no separator (R13.3).
        unparsed.push(kind == EntryKind::Track && !r.separated);
        entries.push(TracklistEntry {
            line: i + 1,
            kind,
            eol: r.line.eol,
            raw: r.line.raw,
            parts: r.scan.parts,
            time: r.scan.time,
            track: None,
            layered: r.layered,
        });
        tracks.push(track);
    }

    let warnings = tracklist_warnings(&WarningInput {
        entries: &entries,
        positions: &positions,
        invalid: &invalid,
        unparsed: &unparsed,
        audio_seconds: opts.audio_seconds,
    });
    let stats = stats_of(&entries);
    RawTracklist { entries, tracks, warnings, stats }
}

fn kind_of(r: &Row<'_>) -> EntryKind {
    if r.blank || r.track.is_none() {
        return EntryKind::Blank;
    }
    let marker = !r.separated && r.marker;
    if r.scan.time.is_some() {
        return if marker { EntryKind::Marker } else { EntryKind::Track };
    }
    if is_link(&r.text) {
        EntryKind::Link
    } else if is_header(&r.text) {
        EntryKind::Header
    } else if marker {
        EntryKind::Marker
    } else if r.separated {
        EntryKind::Track
    } else {
        EntryKind::Note
    }
}

fn stats_of(entries: &[TracklistEntry]) -> Stats {
    let mut stats = Stats { lines: entries.len(), ..Stats::default() };
    for e in entries {
        match e.kind {
            EntryKind::Track => stats.tracks += 1,
            EntryKind::Marker => stats.markers += 1,
            _ => {}
        }
        if let Some(t) = &e.time {
            stats.timed += 1;
            stats.first_time.get_or_insert(t.seconds);
            stats.last_time = Some(t.seconds);
        }
    }
    stats
}
