//! R12 options: the typed public options and the resolved form the pipeline reads.
//!
//! Two doors lead to the resolved form: the typed structs below, and loose JSON (`wire`), which
//! is resolved with exactly the fallbacks of the JS reference (a bad value means the default,
//! R0.8).

use crate::json::Json;
use crate::types::{string_enum, JunkKind, Mode, VersionType};

string_enum! {
    /// The mode to parse in (R2). `Auto` picks clean, youtube or filename from the string
    /// itself, never tracklist.
    ///
    /// ```
    /// use trackparse::{parse_with, Mode, ModeOption, ParseOptions};
    ///
    /// let input = "Noisia - Stigma (Official Video)";
    /// assert_eq!(parse_with(input, &ParseOptions::new()).mode, Mode::Youtube);
    /// let clean = ParseOptions::new().mode(ModeOption::Clean);
    /// assert_eq!(parse_with(input, &clean).mode, Mode::Clean);
    /// assert_eq!(ModeOption::default(), ModeOption::Auto);
    /// ```
    #[non_exhaustive]
    ModeOption {
        Auto => "auto", Clean => "clean", Youtube => "youtube", Filename => "filename",
        Tracklist => "tracklist",
    }
}

string_enum! {
    /// When the word `and` splits a credit list (R7.3). `&` and the other joiners always split.
    ///
    /// ```
    /// use trackparse::{parse_artists_with, ParseOptions, SplitAnd};
    ///
    /// let count = |split_and| {
    ///     let options = ParseOptions::new().split_and(split_and);
    ///     parse_artists_with("Above and Beyond", &options).len()
    /// };
    /// assert_eq!(count(SplitAnd::Auto), 1);
    /// assert_eq!(count(SplitAnd::Always), 2);
    /// assert_eq!(count(SplitAnd::Never), 1);
    /// ```
    #[non_exhaustive]
    SplitAnd { Auto => "auto", Always => "always", Never => "never" }
}

string_enum! {
    /// Where [`format_track_with`](crate::format_track_with) writes featured artists (R11.2).
    ///
    /// ```
    /// use trackparse::{format_track_with, parse, FeatPlacement, FormatOptions};
    ///
    /// let track = parse("Sub Focus - Out the Blue ft. Alice Gold");
    /// let with = |feat| format_track_with(&track, &FormatOptions::new().feat(feat));
    /// assert_eq!(with(FeatPlacement::Source), "Sub Focus - Out the Blue (ft. Alice Gold)");
    /// assert_eq!(with(FeatPlacement::Artist), "Sub Focus ft. Alice Gold - Out the Blue");
    /// assert_eq!(with(FeatPlacement::Title), "Sub Focus - Out the Blue (ft. Alice Gold)");
    /// assert_eq!(with(FeatPlacement::Omit), "Sub Focus - Out the Blue");
    /// ```
    #[non_exhaustive]
    FeatPlacement { Source => "source", Artist => "artist", Title => "title", Omit => "omit" }
}

string_enum! {
    /// Joiners as written, or mapped to their canonical form (R11.5).
    ///
    /// ```
    /// use trackparse::{format_track_with, parse, FormatOptions, JoinerStyle};
    ///
    /// let track = parse("Noisia & The Upbeats ft Tasha Baxter - Dead Limit");
/// assert_eq!(format_track_with(&track, &FormatOptions::new()), track.input);
    /// let options = FormatOptions::new().joiners(JoinerStyle::Canonical);
    /// assert_eq!(
    ///     format_track_with(&track, &options),
    ///     "Noisia & The Upbeats feat. Tasha Baxter - Dead Limit"
    /// );
    /// ```
    #[non_exhaustive]
    JoinerStyle { Original => "original", Canonical => "canonical" }
}

string_enum! {
    /// Whether [`format_track_with`](crate::format_track_with) writes the versions (R11.3).
    ///
    /// ```
    /// use trackparse::{format_track_with, parse, FormatOptions, VersionsOption};
    ///
    /// let track = parse("Fox Stevenson - Bruises (Magnetude Remix)");
    /// let options = FormatOptions::new().versions(VersionsOption::None);
    /// assert_eq!(format_track_with(&track, &options), "Fox Stevenson - Bruises");
    /// ```
    #[non_exhaustive]
    VersionsOption { All => "all", None => "none" }
}

#[allow(clippy::derivable_impls)] // the enum comes from a macro
impl Default for ModeOption {
    fn default() -> Self {
        ModeOption::Auto
    }
}

#[allow(clippy::derivable_impls)] // the enum comes from a macro
impl Default for SplitAnd {
    fn default() -> Self {
        SplitAnd::Auto
    }
}

#[allow(clippy::derivable_impls)] // the enum comes from a macro
impl Default for FeatPlacement {
    fn default() -> Self {
        FeatPlacement::Source
    }
}

#[allow(clippy::derivable_impls)] // the enum comes from a macro
impl Default for JoinerStyle {
    fn default() -> Self {
        JoinerStyle::Original
    }
}

#[allow(clippy::derivable_impls)] // the enum comes from a macro
impl Default for VersionsOption {
    fn default() -> Self {
        VersionsOption::All
    }
}

/// Extra vocabulary, added to the built-in word lists (R12). Keys are matched with ASCII
/// lowercasing.
///
/// ```
/// use trackparse::{parse_with, JunkKind, KeywordOptions, ParseOptions, VersionType};
///
/// let keywords = KeywordOptions::new()
///     .version_head("rerub", VersionType::Remix)
///     .junk("out now", JunkKind::Promo);
/// let options = ParseOptions::new().keywords(keywords);
/// let track = parse_with("Noisia - Stigma (Phace Rerub) [Out Now]", &options);
/// assert_eq!(track.versions[0].version_type, VersionType::Remix);
/// assert_eq!(track.versions[0].artists[0].name, "Phace");
/// assert_eq!(track.junk[0].kind, JunkKind::Promo);
/// ```
#[derive(Debug, Clone, PartialEq, Eq, Default)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
#[cfg_attr(feature = "serde", serde(default, rename_all = "camelCase"))]
#[non_exhaustive]
pub struct KeywordOptions {
    /// Extra version heads: word and the version type it means. A JSON object (`versionHeads`).
    #[cfg_attr(feature = "serde", serde(with = "crate::serde_support::pairs"))]
    pub version_heads: Vec<(String, VersionType)>,
    /// Extra descriptor words (`Extended`, `Deluxe`).
    pub descriptors: Vec<String>,
    /// Extra genre names.
    pub genres: Vec<String>,
    /// Extra junk phrases: phrase and its kind. A JSON object.
    #[cfg_attr(feature = "serde", serde(with = "crate::serde_support::pairs"))]
    pub junk: Vec<(String, JunkKind)>,
    /// Extra markers that introduce featured artists (`featMarkers` in JSON).
    pub feat_markers: Vec<String>,
}

impl KeywordOptions {
    /// No extra words.
    pub fn new() -> Self {
        Self::default()
    }

    /// Add a version head (`"rerub"` means [`VersionType::Remix`]).
    pub fn version_head(mut self, key: impl Into<String>, version_type: VersionType) -> Self {
        self.version_heads.push((key.into(), version_type));
        self
    }

    /// Add a descriptor word.
    pub fn descriptor(mut self, word: impl Into<String>) -> Self {
        self.descriptors.push(word.into());
        self
    }

    /// Add a genre name.
    pub fn genre(mut self, word: impl Into<String>) -> Self {
        self.genres.push(word.into());
        self
    }

    /// Add a junk phrase (`"free download"` is [`JunkKind::Promo`]).
    pub fn junk(mut self, phrase: impl Into<String>, kind: JunkKind) -> Self {
        self.junk.push((phrase.into(), kind));
        self
    }

    /// Add a marker that introduces featured artists.
    pub fn feat_marker(mut self, marker: impl Into<String>) -> Self {
        self.feat_markers.push(marker.into());
        self
    }
}

/// Options of [`parse_with`](crate::parse_with), [`parse_artists_with`](crate::parse_artists_with)
/// and [`Parser`](crate::Parser) (R12). The default is what [`parse`](crate::parse) uses.
///
/// Build one with the chained setters, or set the public fields on a default value.
///
/// ```
/// use trackparse::{parse_with, ModeOption, ParseOptions, SplitAnd};
///
/// let options = ParseOptions::new()
///     .mode(ModeOption::Youtube)
///     .uploader("Noisia")
///     .known_artists(["Chase & Status"])
///     .split_and(SplitAnd::Never);
/// let track = parse_with("Stigma (Official Video)", &options);
/// assert_eq!(track.artists[0].name, "Noisia");
/// assert_eq!(track.title, "Stigma");
///
/// let mut same = ParseOptions::default();
/// same.mode = ModeOption::Youtube;
/// same.uploader = Some("Noisia".into());
/// same.known_artists = vec!["Chase & Status".into()];
/// same.split_and = SplitAnd::Never;
/// assert_eq!(same, options);
/// ```
#[derive(Debug, Clone, PartialEq, Eq, Default)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
#[cfg_attr(feature = "serde", serde(default, rename_all = "camelCase"))]
#[non_exhaustive]
pub struct ParseOptions {
    /// The mode; default [`ModeOption::Auto`].
    pub mode: ModeOption,
    /// youtube mode: the channel name, used as the artist when the title has no separator
    /// (R6.8).
    #[cfg_attr(feature = "serde", serde(skip_serializing_if = "Option::is_none"))]
    pub uploader: Option<String>,
    /// Names that must never be split (R7.3a): `Chase & Status`, `Above & Beyond`.
    pub known_artists: Vec<String>,
    /// When `and` splits a credit list; default [`SplitAnd::Auto`].
    pub split_and: SplitAnd,
    /// Extra vocabulary.
    #[cfg_attr(feature = "serde", serde(skip_serializing_if = "Option::is_none"))]
    pub keywords: Option<KeywordOptions>,
}

impl ParseOptions {
    /// The default options.
    pub fn new() -> Self {
        Self::default()
    }

    /// Set the mode.
    pub fn mode(mut self, mode: ModeOption) -> Self {
        self.mode = mode;
        self
    }

    /// Set the uploader (the channel name, for youtube mode).
    pub fn uploader(mut self, uploader: impl Into<String>) -> Self {
        self.uploader = Some(uploader.into());
        self
    }

    /// Set the names that must never be split.
    pub fn known_artists<I, S>(mut self, names: I) -> Self
    where
        I: IntoIterator<Item = S>,
        S: Into<String>,
    {
        self.known_artists = names.into_iter().map(Into::into).collect();
        self
    }

    /// Set when `and` splits a credit list.
    pub fn split_and(mut self, split_and: SplitAnd) -> Self {
        self.split_and = split_and;
        self
    }

    /// Set the extra vocabulary.
    pub fn keywords(mut self, keywords: KeywordOptions) -> Self {
        self.keywords = Some(keywords);
        self
    }
}

/// Options of [`format_track_with`](crate::format_track_with) (R11).
///
/// ```
/// use trackparse::{format_track_with, parse, FeatPlacement, FormatOptions};
///
/// let track = parse("03. Sub Focus - Out the Blue ft. Alice Gold (Wilkinson Remix)");
/// let options = FormatOptions::new()
///     .feat(FeatPlacement::Artist)
///     .feat_marker("feat.")
///     .position(true);
/// assert_eq!(
///     format_track_with(&track, &options),
///     "03. Sub Focus feat. Alice Gold - Out the Blue (Wilkinson Remix)"
/// );
/// ```
#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
#[cfg_attr(feature = "serde", serde(default, rename_all = "camelCase"))]
#[non_exhaustive]
pub struct FormatOptions {
    /// Where featured artists go; default [`FeatPlacement::Source`] (where they were found).
    pub feat: FeatPlacement,
    /// Marker written in front of featured lists instead of the one in the input.
    #[cfg_attr(feature = "serde", serde(skip_serializing_if = "Option::is_none"))]
    pub feat_marker: Option<String>,
    /// Joiners as written or canonical; default [`JoinerStyle::Original`].
    pub joiners: JoinerStyle,
    /// Write the versions or not; default [`VersionsOption::All`].
    pub versions: VersionsOption,
    /// Write `prod.` credits; default `true`.
    pub producers: bool,
    /// Write the position in front; default `false`.
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
    /// The default options.
    pub fn new() -> Self {
        Self::default()
    }

    /// Set where featured artists go.
    pub fn feat(mut self, feat: FeatPlacement) -> Self {
        self.feat = feat;
        self
    }

    /// Set the marker written in front of featured lists.
    pub fn feat_marker(mut self, marker: impl Into<String>) -> Self {
        self.feat_marker = Some(marker.into());
        self
    }

    /// Set the joiner style.
    pub fn joiners(mut self, joiners: JoinerStyle) -> Self {
        self.joiners = joiners;
        self
    }

    /// Set whether versions are written.
    pub fn versions(mut self, versions: VersionsOption) -> Self {
        self.versions = versions;
        self
    }

    /// Set whether `prod.` credits are written.
    pub fn producers(mut self, producers: bool) -> Self {
        self.producers = producers;
        self
    }

    /// Set whether the position is written in front.
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
