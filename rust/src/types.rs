//! Public types. They mirror `spec/schema/parsed-track.schema.json`; field names are snake_case
//! in Rust and the spec's camelCase in JSON (`to_json()`).

use crate::json::Json;

/// Declares a string-valued enum with `as_str()` / `from_name()` and its JSON form.
macro_rules! string_enum {
    ($(#[$meta:meta])* $name:ident { $($variant:ident => $text:literal),+ $(,)? }) => {
        $(#[$meta])*
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
        pub enum $name { $($variant),+ }

        impl $name {
            /// Every value, in the spec's order.
            pub const ALL: &'static [$name] = &[$($name::$variant),+];

            /// The name used in the spec and in JSON.
            pub fn as_str(self) -> &'static str {
                match self { $($name::$variant => $text),+ }
            }

            /// The value with that spec name.
            pub fn from_name(name: &str) -> Option<$name> {
                match name { $($text => Some($name::$variant),)+ _ => None }
            }
        }

        impl std::fmt::Display for $name {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                f.write_str(self.as_str())
            }
        }
    };
}
#[allow(unused_imports)]
pub(crate) use string_enum;

string_enum! {
    /// R2 / R13.6: `Tracklist` is only used when asked for (never resolved from `auto`).
    Mode { Clean => "clean", Youtube => "youtube", Filename => "filename", Tracklist => "tracklist" }
}

string_enum! {
    ArtistRole { Primary => "primary", Featured => "featured", Remixer => "remixer", Producer => "producer" }
}

string_enum! {
    ArtistSource { Artist => "artist", Title => "title", Version => "version" }
}

string_enum! {
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
    VersionDelimiter { Paren => "(", Square => "[", Curly => "{", Dash => "-" }
}

string_enum! {
    JunkKind {
        Video => "video", Audio => "audio", Lyrics => "lyrics", Quality => "quality",
        Promo => "promo", Platform => "platform", Label => "label", Genre => "genre",
        Other => "other",
    }
}

string_enum! {
    Warning {
        NoSeparator => "noSeparator", AmbiguousSeparator => "ambiguousSeparator",
        UnspacedDashSplit => "unspacedDashSplit", AsymmetricDashSplit => "asymmetricDashSplit",
        BySplit => "bySplit", QuotedTitleSplit => "quotedTitleSplit",
        AmbiguousMixCredit => "ambiguousMixCredit", UnbalancedBrackets => "unbalancedBrackets",
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Artist {
    pub name: String,
    pub role: ArtistRole,
    /// Raw joiner / marker text that preceded the name in its credit list; `None` for the first.
    pub joiner: Option<String>,
    pub source: ArtistSource,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Version {
    pub version_type: VersionType,
    pub raw: String,
    pub artists: Vec<Artist>,
    pub modifiers: Vec<String>,
    pub descriptor: Option<String>,
    pub year: Option<u32>,
    pub unknown_artist: bool,
    pub delimiter: VersionDelimiter,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Junk {
    pub raw: String,
    pub kind: JunkKind,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Flags {
    pub explicit: bool,
    pub clean: bool,
    pub unknown_artist: bool,
    pub unknown_title: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Position {
    pub raw: String,
    pub number: u32,
}

/// R4.1: a cue time in front of a single track string (whole seconds).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Timestamp {
    pub raw: String,
    pub seconds: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParsedTrack {
    pub input: String,
    pub mode: Mode,
    pub position: Option<Position>,
    pub timestamp: Option<Timestamp>,
    pub artists: Vec<Artist>,
    pub title: String,
    pub full_title: String,
    pub versions: Vec<Version>,
    pub year: Option<u32>,
    pub flags: Flags,
    pub junk: Vec<Junk>,
    pub warnings: Vec<Warning>,
}

/// Conversion to the spec's JSON form (camelCase keys, every key present, schema order).
pub trait ToJson {
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
