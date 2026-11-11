//! Multi-line tracklists (SPEC R13): [`parse_tracklist`] and [`apply_edits`].
//!
//! A tracklist is the text people paste under a mix: one track per line, usually with a cue
//! time, sometimes with headers, links and notes in between. [`parse_tracklist`] gives one
//! [`TracklistEntry`] per line and never changes or drops a line; [`apply_edits`] rewrites the
//! text (set a time, shift, sort, insert, delete) and leaves every other byte alone.
//!
//! Guides: <https://4matic.github.io/trackparse/guides/tracklists/> and
//! <https://4matic.github.io/trackparse/guides/editing-tracklists/>.
//!
//! ```
//! use trackparse::tracklist::{parse_tracklist, EntryKind};
//!
//! let list = parse_tracklist("Tracklist:\n\n00:00 Intro\n01:30 Noisia - Stigma\n");
//! let kinds: Vec<EntryKind> = list.entries.iter().map(|e| e.kind).collect();
//! assert_eq!(kinds, [EntryKind::Header, EntryKind::Blank, EntryKind::Marker, EntryKind::Track]);
//! assert_eq!(list.stats.timed, 2);
//! assert!(list.warnings.is_empty());
//! ```
//!
//! Rule IDs (`R13.2`…) refer to the spec. The output is byte-identical to `trackparse/tracklist`
//! in the JavaScript package and `trackparse.tracklist` in Python.

mod edits;
mod emphasis;
mod kinds;
mod options;
mod parse;
mod positions;
mod split;
mod times;
mod types;
mod warnings;

pub use options::TracklistOptions;
pub use types::{
    Edit, EditResult, EditWarning, EditWarningCode, EntryKind, Eol, Parts, Severity, Stats,
    Suggestion, TimeValue, Tracklist, TracklistEntry, TracklistWarning, WarningCode,
};

use crate::json::Json;
use crate::types::ToJson;

/// Parse a multi-line tracklist with the default options (R13).
///
/// The text is split at LF, CRLF and CR only; every line becomes one entry, and the entries'
/// `raw + eol`, joined, are the text again (R13.1.2). Never panics.
///
/// ```
/// use trackparse::tracklist::{parse_tracklist, EntryKind};
///
/// let list = parse_tracklist("00:00 Noisia - Stigma\n[03:45] Phace & Misanthrop - Desert Orgy\n");
/// assert_eq!(list.stats.tracks, 2);
/// let second = &list.entries[1];
/// assert_eq!(second.kind, EntryKind::Track);
/// assert_eq!(second.time.as_ref().map(|t| t.seconds), Some(225.0));
/// let track = second.track.as_ref().unwrap();
/// assert_eq!(track.artists.len(), 2);
/// assert_eq!(track.title, "Desert Orgy");
/// ```
pub fn parse_tracklist(text: &str) -> Tracklist {
    parse_tracklist_with(text, &TracklistOptions::default())
}

/// [`parse_tracklist`] with options (R13.10).
///
/// ```
/// use trackparse::tracklist::{parse_tracklist_with, TracklistOptions, WarningCode};
///
/// let options = TracklistOptions::new().audio_seconds(3600.0);
/// let list = parse_tracklist_with("00:00 A - B\n1:02:03 C - D", &options);
/// assert_eq!(list.warnings[0].code, WarningCode::TimeAfterAudioEnd);
/// assert_eq!(list.warnings[0].line, 2);
/// ```
pub fn parse_tracklist_with(text: &str, options: &TracklistOptions) -> Tracklist {
    parse::parse_tracklist(text, &options::resolve_typed(options)).into_tracklist()
}

/// Apply edits to a tracklist text (R13.9), in order, each to the result of the previous one.
///
/// Lines no edit wrote stay byte-identical. Never panics: an edit that cannot be applied changes
/// nothing and is reported as [`EditWarningCode::EditInvalid`].
///
/// ```
/// use trackparse::tracklist::{apply_edits, Edit};
///
/// let text = "00:00 Noisia - Stigma\n03:45 Phace - Cold Champagne\n";
/// let edits = [
///     Edit::shift_from(30.0, 2),
///     Edit::set_rest(1, "Noisia - Stigma (Neosignal Remix)"),
/// ];
/// let result = apply_edits(text, &edits);
/// assert_eq!(result.text, "00:00 Noisia - Stigma (Neosignal Remix)\n04:15 Phace - Cold Champagne\n");
/// assert_eq!(result.changed, [1, 2]);
/// assert!(result.warnings.is_empty());
/// ```
pub fn apply_edits(text: &str, edits: &[Edit]) -> EditResult {
    let edits: Vec<Json> = edits.iter().map(ToJson::to_json_value).collect();
    edits::apply_edits(text, &edits).into_result()
}

/// [`apply_edits`] with options. The options are accepted for symmetry with the other ports
/// (R13.10); edits do not depend on them today.
///
/// ```
/// use trackparse::tracklist::{apply_edits, apply_edits_with, Edit, TracklistOptions};
///
/// let (text, edits) = ("00:00 A - B\n01:00 C - D", [Edit::shift(5.0)]);
/// let result = apply_edits_with(text, &edits, &TracklistOptions::new());
/// assert_eq!(result.text, "00:05 A - B\n01:05 C - D");
/// assert_eq!(result, apply_edits(text, &edits));
/// ```
pub fn apply_edits_with(text: &str, edits: &[Edit], options: &TracklistOptions) -> EditResult {
    let _ = options;
    apply_edits(text, edits)
}

/// `parseTracklist` for the JSON door (`wire`): loose options, the pipeline's own tracks.
pub(crate) fn wire_parse(text: &str, options: &Json) -> Json {
    parse::parse_tracklist(text, &options::resolve_json(options)).to_json_value()
}

/// `applyEdits` for the JSON door (`wire`): `edits` is anything; only an array has edits.
pub(crate) fn wire_edits(text: &str, edits: &Json) -> Json {
    edits::apply_edits(text, edits.as_array().unwrap_or(&[])).to_json_value()
}
