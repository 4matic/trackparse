//! The JSON door into the crate: every public function called with JSON arguments, returning JSON.
//!
//! The shared fixtures, the cross-port diff and fuzzers use it. Options arrive as loose JSON and
//! are resolved with the same fallbacks as the JS reference (a bad value means the default,
//! SPEC R0.8), which the typed Rust API cannot even express.

use crate::json::Json;

/// A fixture-style call. `func` is one of `parse`, `parseArtists`, `format`, `normalize`,
/// `parseTracklist`, `applyEdits`. Unknown functions return `null`. Never panics.
pub fn call(func: &str, input: &str, options: &Json, format_options: &Json, edits: &Json) -> Json {
    let _ = (input, options, format_options, edits);
    match func {
        // Filled in as the port proceeds.
        _ => Json::Null,
    }
}
