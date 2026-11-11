//! trackparse: a spec-driven parser for music track strings.
//!
//! Behaviour is defined by `spec/SPEC.md`; rule IDs (`R6.2`…) in comments refer to it. The output
//! is byte-identical to the JavaScript and Python packages for every input.
//!
//! ```
//! use trackparse::{parse, ArtistRole, VersionType};
//!
//! let track = parse("Noisia feat. Foreign Beggars - Shellshock (Rockwell Remix)");
//! assert_eq!(track.artists[0].name, "Noisia");
//! assert_eq!(track.artists[1].role, ArtistRole::Featured);
//! assert_eq!(track.title, "Shellshock");
//! assert_eq!(track.versions[0].version_type, VersionType::Remix);
//! ```

pub mod json;
pub mod options;
pub mod types;
pub mod value;
#[doc(hidden)]
pub mod wire;

mod artists;
mod assemble;
mod classify;
mod context;
mod credits;
#[allow(dead_code)] // generated: the R13 word lists are read by the tracklist module
#[rustfmt::skip]
mod data;
mod digits;
mod format;
mod mode;
mod normalize;
mod parser;
mod prefix;
mod scanner;
mod separator;
mod tables;
mod title;
#[rustfmt::skip]
mod unicode_tables;
mod words;

pub use data::SPEC_VERSION;
pub use options::{
    FeatPlacement, FormatOptions, JoinerStyle, KeywordOptions, ModeOption, ParseOptions, SplitAnd,
    VersionsOption,
};
pub use types::{
    Artist, ArtistRole, ArtistSource, Flags, Junk, JunkKind, Mode, ParsedTrack, Position,
    Timestamp, ToJson, Version, VersionDelimiter, VersionType, Warning,
};

use options::{Resolved, ResolvedFormat};
use parser::Engine;

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

/// A parser with its options resolved and its vocabulary tables built once (JS `createParser`).
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
    pub fn new(options: &ParseOptions) -> Parser {
        Parser { engine: Engine::new(Resolved::from_typed(options)) }
    }

    /// See [`parse`].
    pub fn parse(&self, input: &str) -> ParsedTrack {
        self.engine.run(input, false).into_track()
    }

    /// See [`parse_artists`].
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
