//! Helpers for the `serde` feature, so `serde_json::to_string(&x)` is the text of `x.to_json()`.
//!
//! Numbers share the writer of `to_json()` ([`number_text`]): a whole value is handed to the
//! serializer as the integer with the digits JavaScript prints (`3167`, not `3167.0`;
//! `1152921504606847000` for 2^60), so `serde_json` writes the same text.
//!
//! What is true for `serde_json`:
//!
//! - Every finite number is written as `to_json()` writes it, except where `serde_json`'s own
//!   float notation differs from JavaScript's: `1e-6 <= |x| < 1e-5` (`1e-6` for `0.000001`) and,
//!   in `serde_json` releases that write floats with ryu, `|x| >= 1e21` (`1e21` for `1e+21`).
//!   It is the same number and reads back the same. No time the parser produces is in those
//!   ranges; only an `Edit` or `audio_seconds` built by hand is. Whole values from 2^63 on are
//!   handed over as `u64` / `i128`, which `serde_json` writes; a format without 128-bit integers
//!   reports an error for whole seconds of 2^64 and more.
//! - Non-finite seconds are written as `null`, as `to_json()` does, and `null` reads back as NaN
//!   (in an `Option`, as `None`). `apply_edits` treats every non-finite value as `edit-invalid`.
//! - Line numbers above 2^53 are written as JavaScript writes the nearest double, and read back
//!   as that double saturated to `usize`.
//! - Keyword maps are written as JSON objects without repeated keys: a repeated key keeps its
//!   first place and takes the last value, like a JS object. Reading does the same.
//!
//! Formats that are not human-readable get plain `f64` / `u64` values.

use serde::de::{Error as DeError, MapAccess, Visitor};
use serde::ser::SerializeMap;
use serde::{Deserialize, Deserializer, Serialize, Serializer};

use crate::json::number_text;

/// 2^53: up to here every whole double is its own decimal text.
const EXACT: f64 = 9_007_199_254_740_992.0;

/// A double as JS writes numbers, for a human-readable format.
fn js_number<S: Serializer>(value: f64, serializer: S) -> Result<S::Ok, S::Error> {
    if !value.is_finite() {
        return serializer.serialize_unit(); // `null`, like `JSON.stringify`
    }
    if value.fract() != 0.0 || value.abs() >= 1e21 {
        return serializer.serialize_f64(value);
    }
    if value.abs() <= EXACT {
        return serializer.serialize_i64(value as i64);
    }
    // A whole value JS prints without an exponent: the shortest digits that round-trip, then
    // zeros. Those digits are an integer the serializer can write as they are.
    let text = number_text(value);
    if let Ok(n) = text.parse::<i64>() {
        serializer.serialize_i64(n)
    } else if let Ok(n) = text.parse::<u64>() {
        serializer.serialize_u64(n)
    } else if let Ok(n) = text.parse::<i128>() {
        serializer.serialize_i128(n)
    } else {
        serializer.serialize_f64(value)
    }
}

/// Seconds: a whole value is an integer, a non-finite one is `null` (read back as NaN).
pub(crate) mod seconds {
    use super::*;

    pub(crate) fn serialize<S: Serializer>(value: &f64, serializer: S) -> Result<S::Ok, S::Error> {
        if serializer.is_human_readable() {
            js_number(*value, serializer)
        } else {
            serializer.serialize_f64(*value)
        }
    }

    pub(crate) fn deserialize<'de, D: Deserializer<'de>>(deserializer: D) -> Result<f64, D::Error> {
        if deserializer.is_human_readable() {
            Ok(Option::<f64>::deserialize(deserializer)?.unwrap_or(f64::NAN))
        } else {
            f64::deserialize(deserializer)
        }
    }
}

pub(crate) fn opt_seconds<S: Serializer>(
    value: &Option<f64>,
    serializer: S,
) -> Result<S::Ok, S::Error> {
    struct Seconds(f64);
    impl Serialize for Seconds {
        fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
            seconds::serialize(&self.0, serializer)
        }
    }
    value.map(Seconds).serialize(serializer)
}

/// A line number of an edit. `to_json()` writes it as a JS number, so above 2^53 it is the
/// nearest double; this writes the same digits and reads them back (saturating at `usize::MAX`).
struct Line(usize);

impl Serialize for Line {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        if serializer.is_human_readable() && self.0 as u64 > (1 << 53) {
            js_number(self.0 as f64, serializer)
        } else {
            serializer.serialize_u64(self.0 as u64)
        }
    }
}

impl<'de> Deserialize<'de> for Line {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Line, D::Error> {
        struct LineVisitor;
        impl Visitor<'_> for LineVisitor {
            type Value = Line;

            fn expecting(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                f.write_str("a line number (a non-negative whole number)")
            }

            fn visit_u64<E: DeError>(self, v: u64) -> Result<Line, E> {
                Ok(Line(usize::try_from(v).unwrap_or(usize::MAX)))
            }

            fn visit_i64<E: DeError>(self, v: i64) -> Result<Line, E> {
                u64::try_from(v)
                    .map_err(|_| E::custom(format_args!("a line number cannot be negative: {v}")))
                    .and_then(|v| self.visit_u64(v))
            }

            fn visit_f64<E: DeError>(self, v: f64) -> Result<Line, E> {
                if v.is_finite() && v.fract() == 0.0 && v >= 0.0 {
                    Ok(Line(v as usize)) // saturates
                } else {
                    Err(E::custom(format_args!("a line number must be a whole number: {v}")))
                }
            }
        }
        // A self-describing format may hold the number as a double (`18446744073709552000`), and
        // an `Edit` is read through serde's buffer for tagged enums, which only hands a double
        // to `deserialize_any`.
        if deserializer.is_human_readable() {
            deserializer.deserialize_any(LineVisitor)
        } else {
            deserializer.deserialize_u64(LineVisitor)
        }
    }
}

pub(crate) mod line {
    use super::*;

    pub(crate) fn serialize<S: Serializer>(v: &usize, serializer: S) -> Result<S::Ok, S::Error> {
        Line(*v).serialize(serializer)
    }

    pub(crate) fn deserialize<'de, D: Deserializer<'de>>(d: D) -> Result<usize, D::Error> {
        Line::deserialize(d).map(|l| l.0)
    }
}

pub(crate) mod opt_line {
    use super::*;

    pub(crate) fn serialize<S: Serializer>(
        v: &Option<usize>,
        serializer: S,
    ) -> Result<S::Ok, S::Error> {
        v.map(Line).serialize(serializer)
    }

    pub(crate) fn deserialize<'de, D: Deserializer<'de>>(d: D) -> Result<Option<usize>, D::Error> {
        Ok(Option::<Line>::deserialize(d)?.map(|l| l.0))
    }
}

pub(crate) mod opt_lines {
    use super::*;

    pub(crate) fn serialize<S: Serializer>(
        v: &Option<Vec<usize>>,
        serializer: S,
    ) -> Result<S::Ok, S::Error> {
        v.as_ref()
            .map(|lines| lines.iter().map(|l| Line(*l)).collect::<Vec<_>>())
            .serialize(serializer)
    }

    pub(crate) fn deserialize<'de, D: Deserializer<'de>>(
        d: D,
    ) -> Result<Option<Vec<usize>>, D::Error> {
        Ok(Option::<Vec<Line>>::deserialize(d)?
            .map(|lines| lines.into_iter().map(|l| l.0).collect()))
    }
}

/// Ordered `(key, value)` pairs as a map (`keywords.versionHeads`, `keywords.junk`). A repeated
/// key keeps its first place and takes the last value, as in a JS object, so the JSON never has
/// a key twice.
pub(crate) mod pairs {
    use super::*;
    use std::collections::HashMap;

    fn unique<'a, T>(pairs: impl Iterator<Item = (&'a String, T)>) -> Vec<(&'a String, T)> {
        let mut out: Vec<(&String, T)> = Vec::new();
        let mut index: HashMap<&str, usize> = HashMap::new();
        for (key, value) in pairs {
            match index.get(key.as_str()).and_then(|&i| out.get_mut(i)) {
                Some(slot) => slot.1 = value,
                None => {
                    index.insert(key.as_str(), out.len());
                    out.push((key, value));
                }
            }
        }
        out
    }

    pub(crate) fn serialize<S: Serializer, T: Serialize>(
        pairs: &[(String, T)],
        serializer: S,
    ) -> Result<S::Ok, S::Error> {
        let pairs = unique(pairs.iter().map(|(k, v)| (k, v)));
        let mut map = serializer.serialize_map(Some(pairs.len()))?;
        for (key, value) in pairs {
            map.serialize_entry(key, value)?;
        }
        map.end()
    }

    pub(crate) fn deserialize<'de, D: Deserializer<'de>, T: Deserialize<'de>>(
        deserializer: D,
    ) -> Result<Vec<(String, T)>, D::Error> {
        struct Pairs<T>(std::marker::PhantomData<T>);
        impl<'de, T: Deserialize<'de>> Visitor<'de> for Pairs<T> {
            type Value = Vec<(String, T)>;

            fn expecting(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                f.write_str("a map of keyword to name")
            }

            fn visit_map<A: MapAccess<'de>>(self, mut map: A) -> Result<Self::Value, A::Error> {
                let mut out: Vec<(String, T)> = Vec::new();
                let mut index: HashMap<String, usize> = HashMap::new();
                while let Some((key, value)) = map.next_entry::<String, T>()? {
                    match index.get(&key).and_then(|&i| out.get_mut(i)) {
                        Some(slot) => slot.1 = value,
                        None => {
                            index.insert(key.clone(), out.len());
                            out.push((key, value));
                        }
                    }
                }
                Ok(out)
            }
        }
        deserializer.deserialize_map(Pairs(std::marker::PhantomData))
    }
}
