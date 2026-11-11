//! R13.10: tracklist options, sanitised so bad values are ignored (R0.8).

use crate::json::Json;
use crate::options::{
    string_list, KeywordOptions, ModeOption, ParseOptions, Requested, Resolved, SplitAnd,
    SplitAndMode,
};
use crate::types::Mode;

/// R13.10: options of [`parse_tracklist_with`](super::parse_tracklist_with).
#[derive(Debug, Clone, PartialEq)]
pub struct TracklistOptions {
    /// Per-line parse mode; default [`ModeOption::Tracklist`].
    pub mode: ModeOption,
    pub known_artists: Vec<String>,
    pub split_and: SplitAnd,
    pub keywords: Option<KeywordOptions>,
    /// Length of the audio in seconds; enables `time-after-audio-end`. A negative or non-finite
    /// value is ignored.
    pub audio_seconds: Option<f64>,
    /// Extra R13.3 marker words.
    pub marker_words: Vec<String>,
}

impl Default for TracklistOptions {
    fn default() -> Self {
        TracklistOptions {
            mode: ModeOption::Tracklist,
            known_artists: Vec::new(),
            split_and: SplitAnd::Auto,
            keywords: None,
            audio_seconds: None,
            marker_words: Vec::new(),
        }
    }
}

impl TracklistOptions {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn mode(mut self, mode: ModeOption) -> Self {
        self.mode = mode;
        self
    }

    pub fn known_artists<I, S>(mut self, names: I) -> Self
    where
        I: IntoIterator<Item = S>,
        S: Into<String>,
    {
        self.known_artists = names.into_iter().map(Into::into).collect();
        self
    }

    pub fn split_and(mut self, split_and: SplitAnd) -> Self {
        self.split_and = split_and;
        self
    }

    pub fn keywords(mut self, keywords: KeywordOptions) -> Self {
        self.keywords = Some(keywords);
        self
    }

    pub fn audio_seconds(mut self, seconds: f64) -> Self {
        self.audio_seconds = Some(seconds);
        self
    }

    pub fn marker_words<I, S>(mut self, words: I) -> Self
    where
        I: IntoIterator<Item = S>,
        S: Into<String>,
    {
        self.marker_words = words.into_iter().map(Into::into).collect();
        self
    }
}

pub(crate) struct ResolvedOptions {
    /// Options for the per-line parse.
    pub parse: Resolved,
    pub audio_seconds: Option<f64>,
    pub marker_words: Vec<String>,
}

/// A JS `number` view of a JSON value: `None` unless it is a number (a bool is not one).
pub(crate) fn js_number(value: Option<&Json>) -> Option<f64> {
    match value {
        Some(Json::Num(n)) => Some(*n),
        _ => None,
    }
}

fn audio(seconds: Option<f64>) -> Option<f64> {
    seconds.filter(|a| a.is_finite() && *a >= 0.0)
}

/// Loose JSON options, with the fallbacks of the JS reference.
pub(crate) fn resolve_json(options: &Json) -> ResolvedOptions {
    // `mode`, `splitAnd`, `knownArtists` and `keywords` are passed on only when well-formed;
    // `uploader` is not a tracklist option.
    let mut parse = Resolved::from_json(options);
    parse.uploader = None;
    parse.mode = match options.get("mode").and_then(Json::as_str) {
        Some("auto") => Requested::Auto,
        Some(name) => {
            Mode::from_name(name).map_or(Requested::Mode(Mode::Tracklist), Requested::Mode)
        }
        None => Requested::Mode(Mode::Tracklist),
    };
    parse.split_and = match options.get("splitAnd").and_then(Json::as_str) {
        Some("always") => SplitAndMode::Always,
        Some("never") => SplitAndMode::Never,
        _ => SplitAndMode::Auto,
    };
    ResolvedOptions {
        parse,
        audio_seconds: audio(js_number(options.get("audioSeconds"))),
        marker_words: string_list(options.get("markerWords")),
    }
}

pub(crate) fn resolve_typed(options: &TracklistOptions) -> ResolvedOptions {
    let parse = ParseOptions {
        mode: options.mode,
        uploader: None,
        known_artists: options.known_artists.clone(),
        split_and: options.split_and,
        keywords: options.keywords.clone(),
    };
    ResolvedOptions {
        parse: Resolved::from_typed(&parse),
        audio_seconds: audio(options.audio_seconds),
        marker_words: options.marker_words.clone(),
    }
}
