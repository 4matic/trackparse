//! trackparse: a spec-driven parser for music track strings.
//!
//! Behaviour is defined by `spec/SPEC.md`; rule IDs (`R6.2`…) in comments refer to it. The output
//! is byte-identical to the JavaScript and Python packages for every input.

pub mod json;
pub mod types;
pub mod value;
#[doc(hidden)]
pub mod wire;

#[rustfmt::skip]
mod data;
#[rustfmt::skip]
mod unicode_tables;

pub use data::SPEC_VERSION;
pub use types::{
    Artist, ArtistRole, ArtistSource, Flags, Junk, JunkKind, Mode, ParsedTrack, Position,
    Timestamp, ToJson, Version, VersionDelimiter, VersionType, Warning,
};
