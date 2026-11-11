//! The JSON door into the crate: every public function called with JSON arguments, returning JSON.
//!
//! The shared fixtures, the cross-port diff and fuzzers use it. Options arrive as loose JSON and
//! are resolved with the same fallbacks as the JS reference (a bad value means the default,
//! SPEC R0.8), which the typed Rust API cannot even express.

use crate::format::{format, FormatInput};
use crate::json::Json;
use crate::normalize::normalize;
use crate::options::{Resolved, ResolvedFormat};
use crate::parser::Engine;
use crate::types::{list, RawTrack};

fn format_raw(track: &RawTrack, format_options: &Json) -> String {
    let input = FormatInput {
        artists: &track.artists,
        title: &track.title,
        versions: track.versions.iter().map(|v| (v.raw.as_str(), v.delimiter)).collect(),
        position_raw: track.position.as_ref().map(|p| p.raw.as_str()),
    };
    format(&input, &ResolvedFormat::from_json(format_options))
}

/// A fixture-style call. `func` is one of `parse`, `parseArtists`, `format`, `normalize`,
/// `parseTracklist`, `applyEdits`. Unknown functions return `null`. Never panics.
///
/// `format` parses `input` with `options` first, then formats with `format_options`.
pub fn call(func: &str, input: &str, options: &Json, format_options: &Json, edits: &Json) -> Json {
    match func {
        "parse" => Engine::new(Resolved::from_json(options)).run(input, false).to_json_value(),
        "parseArtists" => list(&Engine::new(Resolved::from_json(options)).run_artists(input)),
        "format" => {
            let track = Engine::new(Resolved::from_json(options)).run(input, false);
            Json::Str(format_raw(&track, format_options))
        }
        "normalize" => Json::Str(normalize(input)),
        "parseTracklist" => crate::tracklist::wire_parse(input, options),
        "applyEdits" => crate::tracklist::wire_edits(input, edits),
        _ => Json::Null,
    }
}
