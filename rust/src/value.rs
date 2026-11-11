//! A static JSON-like value: the shape `scripts/gen_data.mjs` compiles `spec/data` into.

/// A compile-time data value. Objects keep the key order of the JSON source.
#[derive(Debug, Clone, Copy, PartialEq)]
#[allow(dead_code)] // the codegen can write every variant; today's data has no numbers
pub enum V {
    Null,
    Bool(bool),
    Num(f64),
    Str(&'static str),
    Arr(&'static [V]),
    Obj(&'static [(&'static str, V)]),
}

impl V {
    /// The member `key` of an object, or `V::Null`.
    pub fn get(&self, key: &str) -> V {
        if let V::Obj(entries) = self {
            for (k, v) in entries.iter() {
                if *k == key {
                    return *v;
                }
            }
        }
        V::Null
    }

    /// The items of an array (empty for anything else).
    pub fn items(&self) -> &'static [V] {
        match self {
            V::Arr(items) => items,
            _ => &[],
        }
    }

    /// The entries of an object, in source order (empty for anything else).
    pub fn entries(&self) -> &'static [(&'static str, V)] {
        match self {
            V::Obj(entries) => entries,
            _ => &[],
        }
    }

    pub fn as_str(&self) -> Option<&'static str> {
        match self {
            V::Str(s) => Some(s),
            _ => None,
        }
    }

    /// The string items of an array, skipping anything else.
    pub fn strings(&self) -> impl Iterator<Item = &'static str> {
        self.items().iter().filter_map(|v| v.as_str())
    }
}
