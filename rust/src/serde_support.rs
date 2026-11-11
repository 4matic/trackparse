//! Helpers for the `serde` feature, so `serde_json::to_string(&x)` is the text of `x.to_json()`.

use serde::de::{MapAccess, Visitor};
use serde::ser::SerializeMap;
use serde::{Deserialize, Deserializer, Serialize, Serializer};

/// Seconds as JS writes numbers: a whole value is an integer (`3167`, not `3167.0`). Only for
/// human-readable formats; a binary format always gets an `f64`.
///
/// With `serde_json` the text is that of `to_json()` for every time the parser can produce.
/// Outside that, two ranges an `Edit` could carry are written differently (same number, other
/// notation): `1e-6 <= |x| < 1e-5` (`1e-6` for `0.000001`) and whole values from 2^64 to 1e21.
pub(crate) fn seconds<S: Serializer>(value: &f64, serializer: S) -> Result<S::Ok, S::Error> {
    let whole = value.is_finite() && value.fract() == 0.0 && serializer.is_human_readable();
    if whole && value.abs() < 9.0e18 {
        serializer.serialize_i64(*value as i64)
    } else if whole && *value > 0.0 && *value < 1.8e19 {
        serializer.serialize_u64(*value as u64)
    } else {
        serializer.serialize_f64(*value)
    }
}

pub(crate) fn opt_seconds<S: Serializer>(
    value: &Option<f64>,
    serializer: S,
) -> Result<S::Ok, S::Error> {
    struct Seconds(f64);
    impl Serialize for Seconds {
        fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
            seconds(&self.0, serializer)
        }
    }
    value.map(Seconds).serialize(serializer)
}

/// Ordered `(key, value)` pairs as a map (`keywords.versionHeads`, `keywords.junk`).
pub(crate) mod pairs {
    use super::*;

    pub(crate) fn serialize<S: Serializer, T: Serialize>(
        pairs: &[(String, T)],
        serializer: S,
    ) -> Result<S::Ok, S::Error> {
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
                let mut out = Vec::new();
                while let Some(entry) = map.next_entry::<String, T>()? {
                    out.push(entry);
                }
                Ok(out)
            }
        }
        deserializer.deserialize_map(Pairs(std::marker::PhantomData))
    }
}
