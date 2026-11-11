//! Turn messy track names into structured data: artists with roles, remixes and versions,
//! junk, and whole tracklists.
//!
//! trackparse is driven by a written spec. Behaviour is defined by [`SPEC.md`] and 13,130 shared
//! fixtures, and this crate gives byte-identical output to the JavaScript (npm `trackparse`) and
//! Python (PyPI `trackparse`) packages for every input. Rule IDs such as `R6.2` in these docs
//! refer to the spec.
//!
//! - User documentation: <https://4matic.github.io/trackparse/>
//! - The spec: <https://4matic.github.io/trackparse/reference/spec/>
//!
//! [`SPEC.md`]: https://github.com/4matic/trackparse/blob/main/spec/SPEC.md
//!
//! # Single tracks
//!
//! [`parse`] takes any string and returns a [`ParsedTrack`]. It never panics and never returns
//! an error: what it recognises goes into a field, what it does not stays in the title.
//!
//! ```
//! use trackparse::{parse, ArtistRole, JunkKind, VersionType};
//!
//! let track = parse("Noisia feat. Foreign Beggars - Shellshock (Rockwell Remix) [Official Video]");
//!
//! assert_eq!(track.artists[0].name, "Noisia");
//! assert_eq!(track.artists[1].name, "Foreign Beggars");
//! assert_eq!(track.artists[1].role, ArtistRole::Featured);
//! assert_eq!(track.title, "Shellshock");
//! assert_eq!(track.full_title, "Shellshock (Rockwell Remix)");
//! assert_eq!(track.versions[0].version_type, VersionType::Remix);
//! assert_eq!(track.versions[0].artists[0].name, "Rockwell");
//! assert_eq!(track.junk[0].kind, JunkKind::Video);
//! ```
//!
//! [`parse_with`] takes [`ParseOptions`]; a [`Parser`] keeps resolved options for many calls.
//! [`parse_artists`] splits a credit string, [`format_track`] writes a track back as a string,
//! [`normalize`] cleans a string without parsing it, and [`all_artists`] lists every credited
//! name, remixers included.
//!
//! ```
//! use trackparse::{format_track_with, parse_with, FeatPlacement, FormatOptions, ParseOptions};
//!
//! let options = ParseOptions::new().known_artists(["Chase & Status"]);
//! let track = parse_with("Chase & Status - Blind Faith ft. Liam Bailey", &options);
//! assert_eq!(track.artists.len(), 2);
//!
//! let format = FormatOptions::new().feat(FeatPlacement::Artist);
//! assert_eq!(format_track_with(&track, &format), "Chase & Status ft. Liam Bailey - Blind Faith");
//! ```
//!
//! # Tracklists
//!
//! The [`tracklist`] module reads a whole pasted tracklist: one entry per line, with its time,
//! its kind and the parsed track, plus warnings about the times. [`tracklist::apply_edits`]
//! rewrites times and lines without touching anything else.
//!
//! ```
//! use trackparse::tracklist::{apply_edits, parse_tracklist, Edit, WarningCode};
//!
//! let text = "00:00 Noisia - Stigma\n03:45 Phace & Misanthrop - Desert Orgy\n02:10 Mefjus - Suicide Bassline";
//! let list = parse_tracklist(text);
//! assert_eq!(list.stats.tracks, 3);
//! assert_eq!(list.entries[1].time.as_ref().unwrap().seconds, 225.0);
//! assert_eq!(list.entries[1].track.as_ref().unwrap().title, "Desert Orgy");
//! assert_eq!(list.warnings[0].code, WarningCode::TimeOutOfOrder);
//!
//! let fixed = apply_edits(text, &[Edit::sort_by_time()]);
//! assert_eq!(
//!     fixed.text,
//!     "00:00 Noisia - Stigma\n02:10 Mefjus - Suicide Bassline\n03:45 Phace & Misanthrop - Desert Orgy"
//! );
//! ```
//!
//! # JSON
//!
//! Fields are snake_case in Rust. `to_json()` on every output type writes the spec's JSON form:
//! camelCase keys (`fullTitle`, `unknownArtist`), every key present, in the schema's order. It is
//! the same text the JavaScript and Python packages produce, and needs no dependency.
//!
//! ```
//! let artists = trackparse::parse_artists("Noisia & Phace");
//! assert_eq!(
//!     artists[1].to_json(),
//!     r#"{"name":"Phace","role":"primary","joiner":"&","source":"artist"}"#
//! );
//! ```
//!
//! With the `serde` feature the output types implement `Serialize` and `Deserialize` with the
//! same keys, so `serde_json::to_string(&track)` gives the text of `track.to_json()`. The
//! options and [`tracklist::Edit`] implement both traits too.
//!
//! # Guarantees
//!
//! - Total: any `&str` gives a result. No panics, no `Result` (R0.8).
//! - Linear time: no regular expressions, no backtracking.
//! - No `unsafe`. One dependency (`unicode-normalization`); `serde` only with the feature.
//! - The Unicode tables the rules depend on are compiled in, so the output does not change
//!   with the compiler version.

#![warn(missing_docs)]

pub mod json;
pub mod tracklist;
#[doc(hidden)]
pub mod wire;

mod artists;
mod assemble;
mod classify;
mod context;
mod credits;
#[rustfmt::skip]
mod data;
mod digits;
mod format;
mod mode;
mod normalize;
mod options;
mod parser;
mod prefix;
mod scanner;
mod separator;
#[cfg(feature = "serde")]
mod serde_support;
mod tables;
mod title;
mod types;
#[rustfmt::skip]
mod unicode_tables;
mod value;
mod words;

pub use data::SPEC_VERSION;
pub use options::{
    FeatPlacement, FormatOptions, JoinerStyle, KeywordOptions, ModeOption, ParseOptions, SplitAnd,
    VersionsOption,
};
pub use types::{
    Artist, ArtistRole, ArtistSource, Flags, Junk, JunkKind, Mode, ParsedTrack, Position,
    Timestamp, ToJson, UnknownName, Version, VersionDelimiter, VersionType, Warning,
};

use options::{Resolved, ResolvedFormat};
use parser::Engine;

/// The examples of the README, run as doc tests (they use the `serde` feature).
#[cfg(all(doctest, feature = "serde"))]
#[doc = include_str!("../README.md")]
pub struct ReadmeDoctests;

/// Parse one track string with the default options (mode `auto`).
///
/// Never panics: any string gives a [`ParsedTrack`] (SPEC R0.8).
///
/// ```
/// let track = trackparse::parse("Sub Focus - Out the Blue ft. Alice Gold (2015)");
/// assert_eq!(track.title, "Out the Blue");
/// assert_eq!(track.year, Some(2015));
/// assert_eq!(track.artists.len(), 2);
/// ```
pub fn parse(input: &str) -> ParsedTrack {
    Engine::shared().run(input, false).into_track()
}

/// Parse one track string with options (R12).
///
/// To parse many strings with the same options, build a [`Parser`] once instead.
///
/// ```
/// use trackparse::{parse_with, Mode, ModeOption, ParseOptions};
///
/// let options = ParseOptions::new().mode(ModeOption::Youtube);
/// let track = parse_with("Noisia - Stigma (Official Video)", &options);
/// assert_eq!(track.mode, Mode::Youtube);
/// assert_eq!(track.title, "Stigma");
/// assert_eq!(track.junk[0].raw, "Official Video");
/// ```
pub fn parse_with(input: &str, options: &ParseOptions) -> ParsedTrack {
    Parser::new(options).parse(input)
}

/// Split a credit string into artists (R10).
///
/// ```
/// let artists = trackparse::parse_artists("Noisia & The Upbeats feat. Foreign Beggars");
/// let names: Vec<&str> = artists.iter().map(|a| a.name.as_str()).collect();
/// assert_eq!(names, ["Noisia", "The Upbeats", "Foreign Beggars"]);
/// ```
pub fn parse_artists(input: &str) -> Vec<Artist> {
    Engine::shared().run_artists(input)
}

/// [`parse_artists`] with options (`known_artists`, `split_and`, `keywords`).
///
/// ```
/// use trackparse::{parse_artists_with, ParseOptions};
///
/// let options = ParseOptions::new().known_artists(["Above & Beyond"]);
/// let artists = parse_artists_with("Above & Beyond & Zoë Johnston", &options);
/// let names: Vec<&str> = artists.iter().map(|a| a.name.as_str()).collect();
/// assert_eq!(names, ["Above & Beyond", "Zoë Johnston"]);
/// ```
pub fn parse_artists_with(input: &str, options: &ParseOptions) -> Vec<Artist> {
    Parser::new(options).parse_artists(input)
}

/// Render a parsed track back to a string (R11) with the default format options.
///
/// ```
/// use trackparse::{format_track, parse};
///
/// let track = parse("Sub Focus - Out the Blue ft. Alice Gold");
/// assert_eq!(format_track(&track), "Sub Focus - Out the Blue (ft. Alice Gold)");
/// ```
pub fn format_track(track: &ParsedTrack) -> String {
    format_track_with(track, &FormatOptions::default())
}

/// Render a parsed track back to a string (R11).
///
/// ```
/// use trackparse::{format_track_with, parse, FeatPlacement, FormatOptions};
///
/// let track = parse("Sub Focus - Out the Blue ft. Alice Gold");
/// let options = FormatOptions::new().feat(FeatPlacement::Artist);
/// assert_eq!(format_track_with(&track, &options), "Sub Focus ft. Alice Gold - Out the Blue");
/// ```
pub fn format_track_with(track: &ParsedTrack, options: &FormatOptions) -> String {
    let input = format::FormatInput {
        artists: &track.artists,
        title: &track.title,
        versions: track.versions.iter().map(|v| (v.raw.as_str(), v.delimiter)).collect(),
        position_raw: track.position.as_ref().map(|p| p.raw.as_str()),
    };
    format::format(&input, &ResolvedFormat::from_typed(options))
}

/// Normalize a track string (R1): NFC, ASCII brackets, quotes and dashes, invisible characters
/// removed, whitespace collapsed. Idempotent.
///
/// ```
/// assert_eq!(trackparse::normalize("  Noisia  –  Stigma\u{200b} "), "Noisia - Stigma");
/// ```
pub fn normalize(input: &str) -> String {
    normalize::normalize(input)
}

/// All credited names: the track's artists plus every version's remixers, deduped (R0.4 key).
///
/// ```
/// let track = trackparse::parse("Fox Stevenson - Bruises (Magnetude Remix)");
/// let names: Vec<String> = trackparse::all_artists(&track).into_iter().map(|a| a.name).collect();
/// assert_eq!(names, ["Fox Stevenson", "Magnetude"]);
/// ```
pub fn all_artists(track: &ParsedTrack) -> Vec<Artist> {
    format::all_artists(
        track.artists.iter().chain(track.versions.iter().flat_map(|v| v.artists.iter())),
    )
}

/// A parser with its options resolved and its vocabulary tables built once (`createParser` in
/// JavaScript, `create_parser` in Python). Use it to parse many strings with the same options.
/// It is `Send + Sync`.
///
/// ```
/// use trackparse::{ParseOptions, Parser};
///
/// let parser = Parser::new(&ParseOptions::new().known_artists(["Chase & Status"]));
/// let track = parser.parse("Chase & Status - Blind Faith");
/// assert_eq!(track.artists.len(), 1);
/// assert_eq!(track.artists[0].name, "Chase & Status");
/// ```
pub struct Parser {
    engine: Engine,
}

impl Parser {
    /// Resolve `options` and build the vocabulary tables.
    pub fn new(options: &ParseOptions) -> Parser {
        Parser { engine: Engine::new(Resolved::from_typed(options)) }
    }

    /// [`parse`] with this parser's options.
    pub fn parse(&self, input: &str) -> ParsedTrack {
        self.engine.run(input, false).into_track()
    }

    /// [`parse_artists`] with this parser's options.
    ///
    /// ```
    /// use trackparse::{ParseOptions, Parser, SplitAnd};
    ///
    /// let parser = Parser::new(&ParseOptions::new().split_and(SplitAnd::Always));
    /// assert_eq!(parser.parse_artists("Above and Beyond").len(), 2);
    /// ```
    pub fn parse_artists(&self, input: &str) -> Vec<Artist> {
        self.engine.run_artists(input)
    }
}

impl Default for Parser {
    fn default() -> Self {
        Parser::new(&ParseOptions::default())
    }
}

impl std::fmt::Debug for Parser {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Parser").finish_non_exhaustive()
    }
}
