//! Public types. They mirror `spec/schema/parsed-track.schema.json`; field names are snake_case
//! in Rust and the spec's camelCase in JSON (`to_json()`, and `serde` with that feature).

use crate::json::Json;

/// The error of `FromStr` on the crate's enums: the string is not one of the spec's names.
///
/// ```
/// use trackparse::{ArtistRole, UnknownName};
///
/// let err: UnknownName = "singer".parse::<ArtistRole>().unwrap_err();
/// assert_eq!(err.to_string(), "unknown ArtistRole name: \"singer\"");
/// ```
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UnknownName {
    pub(crate) kind: &'static str,
    pub(crate) name: String,
}

impl UnknownName {
    /// The string that was not recognised.
    pub fn name(&self) -> &str {
        &self.name
    }
}

impl std::fmt::Display for UnknownName {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "unknown {} name: {:?}", self.kind, self.name)
    }
}

impl std::error::Error for UnknownName {}

/// Declares a string-valued enum: `as_str()`, `from_name()`, `ALL`, `Display`, `FromStr`, and
/// with the `serde` feature `Serialize` / `Deserialize` as the spec name.
macro_rules! string_enum {
    ($(#[$meta:meta])* $name:ident { $($variant:ident => $text:literal),+ $(,)? }) => {
        $(#[$meta])*
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
        pub enum $name {
            $(
                #[doc = concat!("`", stringify!($text), "` in the spec and in JSON.")]
                $variant
            ),+
        }

        impl $name {
            /// Every value, in the spec's order.
            pub const ALL: &'static [$name] = &[$($name::$variant),+];

            /// Every name, in the order of `ALL`.
            pub const NAMES: &'static [&'static str] = &[$($text),+];

            /// The name used in the spec and in JSON.
            pub fn as_str(self) -> &'static str {
                match self { $($name::$variant => $text),+ }
            }

            /// The value with that spec name (exact match), or `None`.
            pub fn from_name(name: &str) -> Option<$name> {
                match name { $($text => Some($name::$variant),)+ _ => None }
            }
        }

        impl std::fmt::Display for $name {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                f.write_str(self.as_str())
            }
        }

        impl std::str::FromStr for $name {
            type Err = $crate::types::UnknownName;

            fn from_str(name: &str) -> Result<$name, Self::Err> {
                $name::from_name(name).ok_or_else(|| $crate::types::UnknownName {
                    kind: stringify!($name),
                    name: name.to_string(),
                })
            }
        }

        #[cfg(feature = "serde")]
        impl serde::Serialize for $name {
            fn serialize<S: serde::Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
                serializer.serialize_str(self.as_str())
            }
        }

        #[cfg(feature = "serde")]
        impl<'de> serde::Deserialize<'de> for $name {
            fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<$name, D::Error> {
                let name = <String as serde::Deserialize>::deserialize(deserializer)?;
                $name::from_name(&name)
                    .ok_or_else(|| serde::de::Error::unknown_variant(&name, $name::NAMES))
            }
        }
    };
}
pub(crate) use string_enum;

/// Adds the inherent `to_json()` to types that implement [`ToJson`], so no import is needed.
macro_rules! json_methods {
    ($($name:ty),+ $(,)?) => {
        $(
            impl $name {
                /// The spec's JSON form as compact text: camelCase keys, every key present, in
                /// the schema's order. The same text the JavaScript and Python packages give.
                pub fn to_json(&self) -> String {
                    $crate::types::ToJson::to_json_value(self).write()
                }
            }
        )+
    };
}
pub(crate) use json_methods;

string_enum! {
    /// The mode a string was parsed in (R2). `Tracklist` is only used when asked for (R13.6);
    /// `auto` never resolves to it.
    ///
    /// ```
    /// use trackparse::{parse, Mode};
    ///
    /// assert_eq!(parse("Noisia - Stigma").mode, Mode::Clean);
    /// assert_eq!(parse("Noisia - Stigma (Official Video)").mode, Mode::Youtube);
    /// assert_eq!(parse("01_noisia_-_stigma.mp3").mode, Mode::Filename);
    /// assert_eq!(Mode::Youtube.as_str(), "youtube");
    /// assert_eq!("filename".parse::<Mode>(), Ok(Mode::Filename));
    /// ```
    #[non_exhaustive]
    Mode { Clean => "clean", Youtube => "youtube", Filename => "filename", Tracklist => "tracklist" }
}

string_enum! {
    /// What an artist did on the track (R7).
    ///
    /// ```
    /// use trackparse::{parse, ArtistRole};
    ///
    /// let track = parse("Noisia feat. Foreign Beggars - Shellshock (prod. by Phace)");
    /// let roles: Vec<ArtistRole> = track.artists.iter().map(|a| a.role).collect();
    /// assert_eq!(roles, [ArtistRole::Primary, ArtistRole::Featured, ArtistRole::Producer]);
    /// assert_eq!(ArtistRole::Featured.to_string(), "featured");
    /// ```
    #[non_exhaustive]
    ArtistRole { Primary => "primary", Featured => "featured", Remixer => "remixer", Producer => "producer" }
}

string_enum! {
    /// Where in the string an artist was found: the artist side, the title side, or a version
    /// group.
    ///
    /// ```
    /// use trackparse::{parse, ArtistSource};
    ///
    /// let track = parse("Sub Focus - Out the Blue ft. Alice Gold");
    /// assert_eq!(track.artists[0].source, ArtistSource::Artist);
    /// assert_eq!(track.artists[1].source, ArtistSource::Title);
    /// ```
    #[non_exhaustive]
    ArtistSource { Artist => "artist", Title => "title", Version => "version" }
}

string_enum! {
    /// The type of a version (R5): remix, edit, live, remaster and so on.
    ///
    /// ```
    /// use trackparse::{parse, VersionType};
    ///
    /// let track = parse("Fox Stevenson - Bruises (Magnetude Remix) [Extended Mix]");
    /// let types: Vec<VersionType> = track.versions.iter().map(|v| v.version_type).collect();
    /// assert_eq!(types, [VersionType::Remix, VersionType::Extended]);
    /// assert_eq!(VersionType::SpedUp.as_str(), "spedUp");
    /// ```
    #[non_exhaustive]
    VersionType {
        Remix => "remix", Bootleg => "bootleg", Vip => "vip", Edit => "edit", Flip => "flip",
        Refix => "refix", Rework => "rework", Mashup => "mashup", Blend => "blend", Dub => "dub",
        Mix => "mix", Extended => "extended", Radio => "radio", Club => "club",
        Original => "original", Instrumental => "instrumental", Acapella => "acapella",
        Live => "live", Acoustic => "acoustic", Remaster => "remaster", Demo => "demo",
        Reprise => "reprise", Cover => "cover", Version => "version", SpedUp => "spedUp",
        Slowed => "slowed", Nightcore => "nightcore",
    }
}

string_enum! {
    /// How a version was written: in a bracket group, or after a dash.
    ///
    /// ```
    /// use trackparse::{parse, VersionDelimiter};
    ///
    /// let track = parse("Noisia - Stigma [VIP] - Extended Mix");
    /// assert_eq!(track.versions[0].delimiter, VersionDelimiter::Square);
    /// assert_eq!(track.versions[1].delimiter, VersionDelimiter::Dash);
    /// ```
    VersionDelimiter { Paren => "(", Square => "[", Curly => "{", Dash => "-" }
}

string_enum! {
    /// What kind of noise a junk fragment is (R5).
    ///
    /// ```
    /// use trackparse::{parse, JunkKind};
    ///
    /// let track = parse("Noisia - Stigma [Official Video] [HD] [Free Download]");
    /// let kinds: Vec<JunkKind> = track.junk.iter().map(|j| j.kind).collect();
    /// assert_eq!(kinds, [JunkKind::Video, JunkKind::Quality, JunkKind::Promo]);
    /// ```
    #[non_exhaustive]
    JunkKind {
        Video => "video", Audio => "audio", Lyrics => "lyrics", Quality => "quality",
        Promo => "promo", Platform => "platform", Label => "label", Genre => "genre",
        Other => "other",
    }
}

string_enum! {
    /// A warning of the single-track parser: the result is a guess the caller may want to check.
    ///
    /// ```
    /// use trackparse::{parse, Warning};
    ///
    /// assert_eq!(parse("Just A Title").warnings, [Warning::NoSeparator]);
    /// assert_eq!(parse("Noisia - Stigma (Unbalanced").warnings, [Warning::UnbalancedBrackets]);
    /// assert_eq!(Warning::NoSeparator.as_str(), "noSeparator");
    /// ```
    #[non_exhaustive]
    Warning {
        NoSeparator => "noSeparator", AmbiguousSeparator => "ambiguousSeparator",
        UnspacedDashSplit => "unspacedDashSplit", AsymmetricDashSplit => "asymmetricDashSplit",
        BySplit => "bySplit", QuotedTitleSplit => "quotedTitleSplit",
        AmbiguousMixCredit => "ambiguousMixCredit", UnbalancedBrackets => "unbalancedBrackets",
    }
}

/// One credited artist.
///
/// ```
/// use trackparse::{parse, ArtistRole, ArtistSource};
///
/// let track = parse("Noisia & The Upbeats - Dead Limit");
/// let second = &track.artists[1];
/// assert_eq!(second.name, "The Upbeats");
/// assert_eq!(second.role, ArtistRole::Primary);
/// assert_eq!(second.joiner.as_deref(), Some("&"));
/// assert_eq!(second.source, ArtistSource::Artist);
/// assert_eq!(
///     second.to_json(),
///     r#"{"name":"The Upbeats","role":"primary","joiner":"&","source":"artist"}"#
/// );
/// ```
#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct Artist {
    /// The name as written, trimmed.
    pub name: String,
    /// What the artist did on the track.
    pub role: ArtistRole,
    /// Raw joiner / marker text that preceded the name in its credit list; `None` for the first.
    pub joiner: Option<String>,
    /// Where in the string the artist was found.
    pub source: ArtistSource,
}

/// One version of the track: a remix, an edit, a live take, a remaster (R5, R8).
///
/// ```
/// use trackparse::{parse, VersionDelimiter, VersionType};
///
/// let track = parse("Fox Stevenson - Bruises (Magnetude Extended Remix) [Live at Lowlands 2016]");
/// let remix = &track.versions[0];
/// assert_eq!(remix.version_type, VersionType::Remix);
/// assert_eq!(remix.raw, "Magnetude Extended Remix");
/// assert_eq!(remix.artists[0].name, "Magnetude");
/// assert_eq!(remix.modifiers, ["extended"]);
/// assert_eq!(remix.delimiter, VersionDelimiter::Paren);
/// let live = &track.versions[1];
/// assert_eq!(live.version_type, VersionType::Live);
/// assert_eq!(live.descriptor.as_deref(), Some("at Lowlands"));
/// assert_eq!(live.year, Some(2016));
/// assert_eq!(live.delimiter, VersionDelimiter::Square);
/// ```
#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "camelCase"))]
pub struct Version {
    /// The type; `type` in JSON.
    #[cfg_attr(feature = "serde", serde(rename = "type"))]
    pub version_type: VersionType,
    /// The version text as written, without its brackets.
    pub raw: String,
    /// Who made it (role `Remixer`); empty for versions without a credit.
    pub artists: Vec<Artist>,
    /// Other version words in the same group, as lowercase keys (`extended` in `Extended Remix`).
    pub modifiers: Vec<String>,
    /// Free text of the version that is neither a credit nor a keyword.
    pub descriptor: Option<String>,
    /// A year written inside the version.
    pub year: Option<u32>,
    /// The credit is a placeholder such as `ID` or `Unknown`.
    pub unknown_artist: bool,
    /// How the version was written.
    pub delimiter: VersionDelimiter,
}

/// A fragment that says nothing about the track: `Official Video`, `HD`, `Free Download`.
/// It is taken out of the title and kept here (the parser extracts, it does not delete).
///
/// ```
/// use trackparse::{parse, JunkKind};
///
/// let track = parse("Noisia - Stigma [Official Video]");
/// assert_eq!(track.junk[0].raw, "Official Video");
/// assert_eq!(track.junk[0].kind, JunkKind::Video);
/// ```
#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct Junk {
    /// The fragment as written, without its brackets.
    pub raw: String,
    /// What kind of noise it is.
    pub kind: JunkKind,
}

/// Yes/no facts about the track.
///
/// ```
/// let track = trackparse::parse("ID - ID (Explicit)");
/// assert!(track.flags.explicit);
/// assert!(track.flags.unknown_artist && track.flags.unknown_title);
/// assert!(!track.flags.clean);
/// ```
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "camelCase"))]
pub struct Flags {
    /// Marked `Explicit` / `Dirty`.
    pub explicit: bool,
    /// Marked `Clean`.
    pub clean: bool,
    /// The artist is a placeholder (`ID`, `Unknown Artist`).
    pub unknown_artist: bool,
    /// The title is a placeholder (`ID`, `Untitled`).
    pub unknown_title: bool,
}

/// A track number in front of the string (R4.2).
///
/// ```
/// let track = trackparse::parse("01. Camo & Krooked - Climax");
/// let position = track.position.unwrap();
/// assert_eq!(position.raw, "01");
/// assert_eq!(position.number, 1);
/// ```
#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct Position {
    /// As written, without the separator after it.
    pub raw: String,
    /// The number in it.
    pub number: u32,
}

/// A cue time in front of a single track string (R4.1), in whole seconds.
///
/// ```
/// let track = trackparse::parse("[01:23:45] Sub Focus - Out the Blue");
/// let timestamp = track.timestamp.unwrap();
/// assert_eq!(timestamp.raw, "01:23:45");
/// assert_eq!(timestamp.seconds, 5025);
/// ```
#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct Timestamp {
    /// As written, without brackets.
    pub raw: String,
    /// The time in seconds.
    pub seconds: u64,
}

/// The result of [`parse`](crate::parse): everything recognised in one track string.
///
/// Anything recognised is in a field; anything not recognised stays in `title` verbatim.
///
/// ```
/// let track = trackparse::parse("Noisia - Stigma (2015)");
/// assert_eq!(
///     track.to_json(),
///     concat!(
///         r#"{"input":"Noisia - Stigma (2015)","mode":"clean","position":null,"#,
///         r#""timestamp":null,"artists":[{"name":"Noisia","role":"primary","joiner":null,"#,
///         r#""source":"artist"}],"title":"Stigma","fullTitle":"Stigma","versions":[],"#,
///         r#""year":2015,"flags":{"explicit":false,"clean":false,"unknownArtist":false,"#,
///         r#""unknownTitle":false},"junk":[],"warnings":[]}"#
///     )
/// );
/// ```
#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "camelCase"))]
pub struct ParsedTrack {
    /// The string that was parsed, unchanged.
    pub input: String,
    /// The mode it was parsed in.
    pub mode: Mode,
    /// A leading track number.
    pub position: Option<Position>,
    /// A leading cue time.
    pub timestamp: Option<Timestamp>,
    /// Primary, featured and producer credits, in the order written. Remixers are in
    /// `versions`; [`all_artists`](crate::all_artists) gives both.
    pub artists: Vec<Artist>,
    /// The title without versions, featured credits and junk.
    pub title: String,
    /// The title with its versions, as written (`fullTitle` in JSON).
    pub full_title: String,
    /// Remixes, edits and other versions, in the order written.
    pub versions: Vec<Version>,
    /// A release year found outside the versions.
    pub year: Option<u32>,
    /// Yes/no facts.
    pub flags: Flags,
    /// Fragments that say nothing about the track.
    pub junk: Vec<Junk>,
    /// Guesses the caller may want to check.
    pub warnings: Vec<Warning>,
}

json_methods!(Artist, Version, Junk, Flags, Position, Timestamp, ParsedTrack);

/// Conversion to the spec's JSON form (camelCase keys, every key present, schema order).
///
/// Every output type also has `to_json()` as an inherent method; the trait is for generic code.
///
/// ```
/// use trackparse::json::Json;
/// use trackparse::ToJson;
///
/// let track = trackparse::parse("Noisia - Stigma");
/// let value: Json = track.to_json_value();
/// assert_eq!(value.get("title").and_then(Json::as_str), Some("Stigma"));
/// ```
pub trait ToJson {
    /// The value as a JSON tree.
    fn to_json_value(&self) -> Json;

    /// Compact JSON text.
    fn to_json(&self) -> String {
        self.to_json_value().write()
    }
}

pub(crate) fn obj(entries: Vec<(&str, Json)>) -> Json {
    Json::Obj(entries.into_iter().map(|(k, v)| (k.to_string(), v)).collect())
}

pub(crate) fn list<T: ToJson>(items: &[T]) -> Json {
    Json::Arr(items.iter().map(ToJson::to_json_value).collect())
}

pub(crate) fn opt<T: ToJson>(v: &Option<T>) -> Json {
    v.as_ref().map_or(Json::Null, ToJson::to_json_value)
}

fn num_opt(v: Option<u32>) -> Json {
    v.map_or(Json::Null, |n| Json::Num(f64::from(n)))
}

impl ToJson for Artist {
    fn to_json_value(&self) -> Json {
        obj(vec![
            ("name", Json::str(&self.name)),
            ("role", Json::str(self.role.as_str())),
            ("joiner", Json::opt_str(self.joiner.as_deref())),
            ("source", Json::str(self.source.as_str())),
        ])
    }
}

impl ToJson for Version {
    fn to_json_value(&self) -> Json {
        obj(vec![
            ("type", Json::str(self.version_type.as_str())),
            ("raw", Json::str(&self.raw)),
            ("artists", list(&self.artists)),
            ("modifiers", Json::Arr(self.modifiers.iter().map(|m| Json::str(m)).collect())),
            ("descriptor", Json::opt_str(self.descriptor.as_deref())),
            ("year", num_opt(self.year)),
            ("unknownArtist", Json::Bool(self.unknown_artist)),
            ("delimiter", Json::str(self.delimiter.as_str())),
        ])
    }
}

impl ToJson for Junk {
    fn to_json_value(&self) -> Json {
        obj(vec![("raw", Json::str(&self.raw)), ("kind", Json::str(self.kind.as_str()))])
    }
}

impl ToJson for Flags {
    fn to_json_value(&self) -> Json {
        obj(vec![
            ("explicit", Json::Bool(self.explicit)),
            ("clean", Json::Bool(self.clean)),
            ("unknownArtist", Json::Bool(self.unknown_artist)),
            ("unknownTitle", Json::Bool(self.unknown_title)),
        ])
    }
}

impl ToJson for Position {
    fn to_json_value(&self) -> Json {
        obj(vec![("raw", Json::str(&self.raw)), ("number", Json::Num(f64::from(self.number)))])
    }
}

impl ToJson for Timestamp {
    fn to_json_value(&self) -> Json {
        obj(vec![("raw", Json::str(&self.raw)), ("seconds", Json::Num(self.seconds as f64))])
    }
}

impl ToJson for ParsedTrack {
    fn to_json_value(&self) -> Json {
        obj(vec![
            ("input", Json::str(&self.input)),
            ("mode", Json::str(self.mode.as_str())),
            ("position", opt(&self.position)),
            ("timestamp", opt(&self.timestamp)),
            ("artists", list(&self.artists)),
            ("title", Json::str(&self.title)),
            ("fullTitle", Json::str(&self.full_title)),
            ("versions", list(&self.versions)),
            ("year", num_opt(self.year)),
            ("flags", self.flags.to_json_value()),
            ("junk", list(&self.junk)),
            ("warnings", Json::Arr(self.warnings.iter().map(|w| Json::str(w.as_str())).collect())),
        ])
    }
}

// ---------------------------------------------------------------------------
// The pipeline's own track. Version types and junk kinds are strings here: through loose JSON
// options (`keywords.versionHeads`, `keywords.junk`) the JS reference passes any string value on
// to the output, which the typed enums above cannot hold. `wire` writes this form; the typed API
// converts it (the typed options can only name real types and kinds).

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct RawVersion {
    pub vtype: String,
    pub raw: String,
    pub artists: Vec<Artist>,
    pub modifiers: Vec<String>,
    pub descriptor: Option<String>,
    pub year: Option<u32>,
    pub unknown_artist: bool,
    pub delimiter: VersionDelimiter,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct RawJunk {
    pub raw: String,
    pub kind: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct RawTrack {
    pub input: String,
    pub mode: Mode,
    pub position: Option<Position>,
    pub timestamp: Option<Timestamp>,
    pub artists: Vec<Artist>,
    pub title: String,
    pub full_title: String,
    pub versions: Vec<RawVersion>,
    pub year: Option<u32>,
    pub flags: Flags,
    pub junk: Vec<RawJunk>,
    pub warnings: Vec<Warning>,
}

impl RawTrack {
    pub(crate) fn empty(input: &str, mode: Mode) -> RawTrack {
        RawTrack {
            input: input.to_string(),
            mode,
            position: None,
            timestamp: None,
            artists: Vec::new(),
            title: String::new(),
            full_title: String::new(),
            versions: Vec::new(),
            year: None,
            flags: Flags::default(),
            junk: Vec::new(),
            warnings: Vec::new(),
        }
    }

    /// The typed track. A type or kind name the enums do not have (only reachable through loose
    /// JSON options) becomes `VersionType::Version` / `JunkKind::Other`.
    pub(crate) fn into_track(self) -> ParsedTrack {
        ParsedTrack {
            input: self.input,
            mode: self.mode,
            position: self.position,
            timestamp: self.timestamp,
            artists: self.artists,
            title: self.title,
            full_title: self.full_title,
            versions: self
                .versions
                .into_iter()
                .map(|v| Version {
                    version_type: VersionType::from_name(&v.vtype).unwrap_or(VersionType::Version),
                    raw: v.raw,
                    artists: v.artists,
                    modifiers: v.modifiers,
                    descriptor: v.descriptor,
                    year: v.year,
                    unknown_artist: v.unknown_artist,
                    delimiter: v.delimiter,
                })
                .collect(),
            year: self.year,
            flags: self.flags,
            junk: self
                .junk
                .into_iter()
                .map(|j| Junk {
                    raw: j.raw,
                    kind: JunkKind::from_name(&j.kind).unwrap_or(JunkKind::Other),
                })
                .collect(),
            warnings: self.warnings,
        }
    }

    /// The spec's JSON form, written exactly as the JS reference would.
    pub(crate) fn to_json_value(&self) -> Json {
        let versions = self
            .versions
            .iter()
            .map(|v| {
                obj(vec![
                    ("type", Json::str(&v.vtype)),
                    ("raw", Json::str(&v.raw)),
                    ("artists", list(&v.artists)),
                    ("modifiers", Json::Arr(v.modifiers.iter().map(|m| Json::str(m)).collect())),
                    ("descriptor", Json::opt_str(v.descriptor.as_deref())),
                    ("year", num_opt(v.year)),
                    ("unknownArtist", Json::Bool(v.unknown_artist)),
                    ("delimiter", Json::str(v.delimiter.as_str())),
                ])
            })
            .collect();
        let junk = self
            .junk
            .iter()
            .map(|j| obj(vec![("raw", Json::str(&j.raw)), ("kind", Json::str(&j.kind))]))
            .collect();
        obj(vec![
            ("input", Json::str(&self.input)),
            ("mode", Json::str(self.mode.as_str())),
            ("position", opt(&self.position)),
            ("timestamp", opt(&self.timestamp)),
            ("artists", list(&self.artists)),
            ("title", Json::str(&self.title)),
            ("fullTitle", Json::str(&self.full_title)),
            ("versions", Json::Arr(versions)),
            ("year", num_opt(self.year)),
            ("flags", self.flags.to_json_value()),
            ("junk", Json::Arr(junk)),
            ("warnings", Json::Arr(self.warnings.iter().map(|w| Json::str(w.as_str())).collect())),
        ])
    }
}
