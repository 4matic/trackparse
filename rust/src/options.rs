//! R12 options: the typed public options and the resolved form the pipeline reads.
//!
//! Two doors lead to the resolved form: the typed structs below, and loose JSON (`wire`), which
//! is resolved with exactly the fallbacks of the JS reference (a bad value means the default,
//! R0.8).

use crate::json::Json;
use crate::types::{string_enum, JunkKind, Mode, VersionType};

string_enum! {
    /// R2: the requested mode. `Auto` resolves to clean, youtube or filename (never tracklist).
    ModeOption {
        Auto => "auto", Clean => "clean", Youtube => "youtube", Filename => "filename",
        Tracklist => "tracklist",
    }
}

string_enum! {
    /// R7.3: when `and` splits a credit list.
    SplitAnd { Auto => "auto", Always => "always", Never => "never" }
}

string_enum! {
    /// R11.2: where featured artists are rendered.
    FeatPlacement { Source => "source", Artist => "artist", Title => "title", Omit => "omit" }
}

string_enum! {
    /// R11.5: joiners as written, or mapped to their canonical form.
    JoinerStyle { Original => "original", Canonical => "canonical" }
}

string_enum! {
    /// R11.3: render versions or leave them out.
    VersionsOption { All => "all", None => "none" }
}

/// Additive vocabulary (R12). Keys are lowercased with ASCII rules.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct KeywordOptions {
    /// Extra version heads: key → version type.
    pub version_heads: Vec<(String, VersionType)>,
    pub descriptors: Vec<String>,
    pub genres: Vec<String>,
    /// Extra junk phrases: phrase → kind.
    pub junk: Vec<(String, JunkKind)>,
    pub feat_markers: Vec<String>,
}

impl KeywordOptions {
    pub fn new() -> Self {
        Self::default()
    }

    /// Add a version head (`"rerub"` → `VersionType::Remix`).
    pub fn version_head(mut self, key: impl Into<String>, version_type: VersionType) -> Self {
        self.version_heads.push((key.into(), version_type));
        self
    }

    pub fn descriptor(mut self, word: impl Into<String>) -> Self {
        self.descriptors.push(word.into());
        self
    }

    pub fn genre(mut self, word: impl Into<String>) -> Self {
        self.genres.push(word.into());
        self
    }

    /// Add a junk phrase (`"free download"` → `JunkKind::Promo`).
    pub fn junk(mut self, phrase: impl Into<String>, kind: JunkKind) -> Self {
        self.junk.push((phrase.into(), kind));
        self
    }

    pub fn feat_marker(mut self, marker: impl Into<String>) -> Self {
        self.feat_markers.push(marker.into());
        self
    }
}

/// Parse options (R12). `ParseOptions::default()` is what [`crate::parse`] uses.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParseOptions {
    pub mode: ModeOption,
    /// youtube mode: channel name used as the artist when no separator is found (R6.8).
    pub uploader: Option<String>,
    /// Names that must never be split (R7.3a).
    pub known_artists: Vec<String>,
    pub split_and: SplitAnd,
    pub keywords: Option<KeywordOptions>,
}

impl Default for ParseOptions {
    fn default() -> Self {
        ParseOptions {
            mode: ModeOption::Auto,
            uploader: None,
            known_artists: Vec::new(),
            split_and: SplitAnd::Auto,
            keywords: None,
        }
    }
}

impl ParseOptions {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn mode(mut self, mode: ModeOption) -> Self {
        self.mode = mode;
        self
    }

    pub fn uploader(mut self, uploader: impl Into<String>) -> Self {
        self.uploader = Some(uploader.into());
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
}

/// Format options (R11).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FormatOptions {
    pub feat: FeatPlacement,
    /// Marker written in front of featured lists instead of the one in the input.
    pub feat_marker: Option<String>,
    pub joiners: JoinerStyle,
    pub versions: VersionsOption,
    pub producers: bool,
    pub position: bool,
}

impl Default for FormatOptions {
    fn default() -> Self {
        FormatOptions {
            feat: FeatPlacement::Source,
            feat_marker: None,
            joiners: JoinerStyle::Original,
            versions: VersionsOption::All,
            producers: true,
            position: false,
        }
    }
}

impl FormatOptions {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn feat(mut self, feat: FeatPlacement) -> Self {
        self.feat = feat;
        self
    }

    pub fn feat_marker(mut self, marker: impl Into<String>) -> Self {
        self.feat_marker = Some(marker.into());
        self
    }

    pub fn joiners(mut self, joiners: JoinerStyle) -> Self {
        self.joiners = joiners;
        self
    }

    pub fn versions(mut self, versions: VersionsOption) -> Self {
        self.versions = versions;
        self
    }

    pub fn producers(mut self, producers: bool) -> Self {
        self.producers = producers;
        self
    }

    pub fn position(mut self, position: bool) -> Self {
        self.position = position;
        self
    }
}

// ---------------------------------------------------------------------------
// Resolved forms

/// `options.mode` as the pipeline sees it. `Other` is any value that is neither missing nor one
/// of the five names: JS then skips the R2.1 youtube detection but still detects filenames.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Requested {
    Auto,
    Mode(Mode),
    Other,
}

/// `options.splitAnd`: anything but `auto` / `never` (or missing) behaves as `always` in JS.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum SplitAndMode {
    Auto,
    Always,
    Never,
}

/// `options.keywords` with the maps in JS `Object.entries` order. Types and kinds stay strings:
/// JS passes any string value through to the output.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub(crate) struct Keywords {
    pub version_heads: Vec<(String, String)>,
    pub descriptors: Vec<String>,
    pub genres: Vec<String>,
    pub junk: Vec<(String, String)>,
    pub feat_markers: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct Resolved {
    pub mode: Requested,
    pub uploader: Option<String>,
    pub known_artists: Vec<String>,
    pub split_and: SplitAndMode,
    /// `None`: the default tables.
    pub keywords: Option<Keywords>,
}

impl Default for Resolved {
    fn default() -> Self {
        Resolved {
            mode: Requested::Auto,
            uploader: None,
            known_artists: Vec::new(),
            split_and: SplitAndMode::Auto,
            keywords: None,
        }
    }
}

/// A canonical array index (`"0"`, `"17"`; not `"01"`): JS lists such keys first, ascending.
fn array_index(key: &str) -> Option<u64> {
    if key.is_empty() || key.len() > 10 || !key.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    if key.len() > 1 && key.starts_with('0') {
        return None;
    }
    key.parse::<u64>().ok().filter(|n| *n <= 0xffff_fffe)
}

/// `Object.entries` order of an object built from `pairs`: a repeated key keeps its first place
/// and takes the last value; integer-like keys come first in ascending order, then the others in
/// insertion order.
pub(crate) fn js_entries<V: Clone>(pairs: &[(String, V)]) -> Vec<(String, V)> {
    let mut unique: Vec<(String, V)> = Vec::with_capacity(pairs.len());
    let mut index: std::collections::HashMap<&str, usize> = std::collections::HashMap::new();
    for (k, v) in pairs {
        match index.get(k.as_str()).and_then(|&i| unique.get_mut(i)) {
            Some(slot) => slot.1 = v.clone(),
            None => {
                index.insert(k.as_str(), unique.len());
                unique.push((k.clone(), v.clone()));
            }
        }
    }
    let mut ints: Vec<(u64, (String, V))> = Vec::new();
    let mut rest: Vec<(String, V)> = Vec::new();
    for pair in unique {
        match array_index(&pair.0) {
            Some(n) => ints.push((n, pair)),
            None => rest.push(pair),
        }
    }
    ints.sort_by_key(|(n, _)| *n);
    ints.into_iter().map(|(_, p)| p).chain(rest).collect()
}

/// R0.8: option lists may be anything at runtime; only arrays of strings count.
pub(crate) fn string_list(value: Option<&Json>) -> Vec<String> {
    match value {
        Some(Json::Arr(items)) => {
            items.iter().filter_map(|v| v.as_str().map(str::to_string)).collect()
        }
        _ => Vec::new(),
    }
}

/// R0.8: option maps may be anything at runtime; only plain objects' string values count.
pub(crate) fn string_entries(value: Option<&Json>) -> Vec<(String, String)> {
    match value {
        Some(Json::Obj(entries)) => js_entries(entries)
            .into_iter()
            .filter_map(|(k, v)| v.as_str().map(|s| (k, s.to_string())))
            .collect(),
        _ => Vec::new(),
    }
}

impl Keywords {
    fn from_json(value: &Json) -> Keywords {
        Keywords {
            version_heads: string_entries(value.get("versionHeads")),
            descriptors: string_list(value.get("descriptors")),
            genres: string_list(value.get("genres")),
            junk: string_entries(value.get("junk")),
            feat_markers: string_list(value.get("featMarkers")),
        }
    }

    fn from_typed(k: &KeywordOptions) -> Keywords {
        let heads: Vec<(String, String)> =
            k.version_heads.iter().map(|(key, t)| (key.clone(), t.as_str().to_string())).collect();
        let junk: Vec<(String, String)> =
            k.junk.iter().map(|(key, kind)| (key.clone(), kind.as_str().to_string())).collect();
        Keywords {
            version_heads: js_entries(&heads),
            descriptors: k.descriptors.clone(),
            genres: k.genres.clone(),
            junk: js_entries(&junk),
            feat_markers: k.feat_markers.clone(),
        }
    }
}

impl Resolved {
    /// Loose JSON options, with the JS fallbacks.
    pub(crate) fn from_json(options: &Json) -> Resolved {
        if options.as_object().is_none() {
            return Resolved::default(); // safeOptions: not an object
        }
        let mode = match options.get("mode") {
            None | Some(Json::Null) => Requested::Auto, // `options.mode ?? "auto"`
            Some(Json::Str(s)) if s == "auto" => Requested::Auto,
            Some(Json::Str(s)) => Mode::from_name(s).map_or(Requested::Other, Requested::Mode),
            Some(_) => Requested::Other,
        };
        let split_and = match options.get("splitAnd") {
            None | Some(Json::Null) => SplitAndMode::Auto, // `options?.splitAnd ?? "auto"`
            Some(Json::Str(s)) if s == "auto" => SplitAndMode::Auto,
            Some(Json::Str(s)) if s == "never" => SplitAndMode::Never,
            Some(_) => SplitAndMode::Always,
        };
        // `options?.keywords ? … : baseTables`, then `isRecord`: only a plain object adds words.
        let keywords = match options.get("keywords") {
            Some(k @ Json::Obj(_)) => Some(Keywords::from_json(k)),
            _ => None,
        };
        Resolved {
            mode,
            uploader: options.get("uploader").and_then(Json::as_str).map(str::to_string),
            known_artists: string_list(options.get("knownArtists")),
            split_and,
            keywords,
        }
    }

    pub(crate) fn from_typed(options: &ParseOptions) -> Resolved {
        Resolved {
            mode: match options.mode {
                ModeOption::Auto => Requested::Auto,
                ModeOption::Clean => Requested::Mode(Mode::Clean),
                ModeOption::Youtube => Requested::Mode(Mode::Youtube),
                ModeOption::Filename => Requested::Mode(Mode::Filename),
                ModeOption::Tracklist => Requested::Mode(Mode::Tracklist),
            },
            uploader: options.uploader.clone(),
            known_artists: options.known_artists.clone(),
            split_and: match options.split_and {
                SplitAnd::Auto => SplitAndMode::Auto,
                SplitAnd::Always => SplitAndMode::Always,
                SplitAnd::Never => SplitAndMode::Never,
            },
            keywords: options.keywords.as_ref().map(Keywords::from_typed),
        }
    }
}

/// `options.feat` as R11 reads it: an unknown value places featured artists nowhere.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum FeatMode {
    Source,
    Artist,
    Title,
    Omit,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct ResolvedFormat {
    pub feat: FeatMode,
    pub feat_marker: Option<String>,
    pub canonical: bool,
    pub versions: bool,
    pub producers: bool,
    pub position: bool,
}

/// JS truthiness of a JSON value.
fn truthy(v: &Json) -> bool {
    match v {
        Json::Null => false,
        Json::Bool(b) => *b,
        Json::Num(n) => *n != 0.0 && !n.is_nan(),
        Json::Str(s) => !s.is_empty(),
        Json::Arr(_) | Json::Obj(_) => true,
    }
}

/// JS `String(value)` (what a template literal does with a non-string `featMarker`).
fn js_string(v: &Json) -> String {
    match v {
        Json::Null => "null".to_string(),
        Json::Bool(b) => b.to_string(),
        Json::Num(n) if n.is_nan() => "NaN".to_string(),
        Json::Num(n) if n.is_infinite() => {
            (if *n > 0.0 { "Infinity" } else { "-Infinity" }).to_string()
        }
        Json::Num(_) => v.write(),
        Json::Str(s) => s.clone(),
        // Array.prototype.join: null elements are empty.
        Json::Arr(items) => items
            .iter()
            .map(|i| if matches!(i, Json::Null) { String::new() } else { js_string(i) })
            .collect::<Vec<_>>()
            .join(","),
        Json::Obj(_) => "[object Object]".to_string(),
    }
}

impl ResolvedFormat {
    /// Loose JSON format options, with the JS fallbacks (`??` for missing and `null`).
    pub(crate) fn from_json(o: &Json) -> ResolvedFormat {
        let get = |key: &str| match o.get(key) {
            Some(Json::Null) | None => None,
            Some(v) => Some(v),
        };
        ResolvedFormat {
            feat: match get("feat") {
                None => FeatMode::Source,
                Some(Json::Str(s)) if s == "source" => FeatMode::Source,
                Some(Json::Str(s)) if s == "artist" => FeatMode::Artist,
                Some(Json::Str(s)) if s == "title" => FeatMode::Title,
                Some(_) => FeatMode::Omit,
            },
            // `o.featMarker !== undefined`: an explicit null is rendered as "null".
            feat_marker: o.get("featMarker").map(js_string),
            canonical: get("joiners").and_then(Json::as_str) == Some("canonical"),
            versions: get("versions").map_or(true, |v| v.as_str() == Some("all")),
            producers: get("producers").map_or(true, truthy),
            position: get("position").is_some_and(truthy),
        }
    }

    pub(crate) fn from_typed(o: &FormatOptions) -> ResolvedFormat {
        ResolvedFormat {
            feat: match o.feat {
                FeatPlacement::Source => FeatMode::Source,
                FeatPlacement::Artist => FeatMode::Artist,
                FeatPlacement::Title => FeatMode::Title,
                FeatPlacement::Omit => FeatMode::Omit,
            },
            feat_marker: o.feat_marker.clone(),
            canonical: o.joiners == JoinerStyle::Canonical,
            versions: o.versions == VersionsOption::All,
            producers: o.producers,
            position: o.position,
        }
    }
}
