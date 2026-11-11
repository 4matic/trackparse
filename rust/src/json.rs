//! A small JSON value with a reader and a writer. The crate has no JSON dependency: this is what
//! `to_json()` writes with, and what the fixtures and the cross-port diff read.

/// An owned JSON value. Objects keep insertion order (like JS objects with string keys).
///
/// ```
/// use trackparse::json::Json;
///
/// let value = Json::parse(r#"{"title":"Stigma","year":2015,"tags":["dnb",null]}"#).unwrap();
/// assert_eq!(value.get("title").and_then(Json::as_str), Some("Stigma"));
/// assert_eq!(value.get("year"), Some(&Json::Num(2015.0)));
/// assert_eq!(value.get("tags").and_then(Json::as_array).map(|t| t.len()), Some(2));
/// assert_eq!(value.write(), r#"{"title":"Stigma","year":2015,"tags":["dnb",null]}"#);
/// assert_eq!(Json::parse("{oops"), None);
/// ```
///
/// The enum is `#[non_exhaustive]`: a `match` on it outside this crate needs a `_` arm.
#[derive(Debug, Clone, PartialEq)]
#[non_exhaustive]
pub enum Json {
    /// `null`.
    Null,
    /// `true` or `false`.
    Bool(bool),
    /// A number. Written as JS writes it: `3167`, `7.5`; a non-finite value is `null`.
    Num(f64),
    /// A string.
    Str(String),
    /// An array.
    Arr(Vec<Json>),
    /// An object: its members in order.
    Obj(Vec<(String, Json)>),
}

impl std::fmt::Display for Json {
    /// The compact JSON text of [`Json::write`].
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.write())
    }
}

impl Json {
    /// A string value.
    pub fn str(s: &str) -> Json {
        Json::Str(s.to_string())
    }

    /// A string value, or `null` for `None`.
    pub fn opt_str(s: Option<&str>) -> Json {
        s.map_or(Json::Null, Json::str)
    }

    /// The member `key` of an object.
    pub fn get(&self, key: &str) -> Option<&Json> {
        match self {
            Json::Obj(entries) => entries.iter().find(|(k, _)| k == key).map(|(_, v)| v),
            _ => None,
        }
    }

    /// The string, if the value is one.
    pub fn as_str(&self) -> Option<&str> {
        match self {
            Json::Str(s) => Some(s),
            _ => None,
        }
    }

    /// The items, if the value is an array.
    pub fn as_array(&self) -> Option<&[Json]> {
        match self {
            Json::Arr(items) => Some(items),
            _ => None,
        }
    }

    /// The members in order, if the value is an object.
    pub fn as_object(&self) -> Option<&[(String, Json)]> {
        match self {
            Json::Obj(entries) => Some(entries),
            _ => None,
        }
    }

    /// Compact JSON text, written the way `JSON.stringify` writes it.
    pub fn write(&self) -> String {
        let mut out = String::new();
        self.write_into(&mut out);
        out
    }

    fn write_into(&self, out: &mut String) {
        match self {
            Json::Null => out.push_str("null"),
            Json::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
            Json::Num(n) => write_number(*n, out),
            Json::Str(s) => write_string(s, out),
            Json::Arr(items) => {
                out.push('[');
                for (i, item) in items.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    item.write_into(out);
                }
                out.push(']');
            }
            Json::Obj(entries) => {
                out.push('{');
                for (i, (k, v)) in entries.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    write_string(k, out);
                    out.push(':');
                    v.write_into(out);
                }
                out.push('}');
            }
        }
    }

    /// Parse JSON text. `None` on malformed input (never panics). `1e999` reads as infinity, as
    /// in JS, so fixtures can carry non-finite numbers.
    pub fn parse(text: &str) -> Option<Json> {
        let chars: Vec<char> = text.chars().collect();
        let mut p = Reader { chars: &chars, at: 0, depth: 0 };
        p.ws();
        let v = p.value()?;
        p.ws();
        if p.at == chars.len() {
            Some(v)
        } else {
            None
        }
    }
}

/// JS `Number::toString` (what `JSON.stringify` writes): the shortest digits that round-trip,
/// laid out without an exponent from 1e-6 up to 1e21 and as `1e+21` / `1.5e-7` outside it.
/// Non-finite values are `null`; `-0` is `0`.
#[cfg(feature = "serde")]
pub(crate) fn number_text(n: f64) -> String {
    let mut out = String::new();
    write_number(n, &mut out);
    out
}

fn write_number(n: f64, out: &mut String) {
    if !n.is_finite() {
        out.push_str("null");
        return;
    }
    if n == 0.0 {
        out.push('0');
        return;
    }
    if n < 0.0 {
        out.push('-');
    }
    // `{:e}` gives the shortest round-trip digits as `d[.ddd]e<exp>`.
    let sci = format!("{:e}", n.abs());
    let (mantissa, exp) = sci.split_once('e').unwrap_or((&sci, "0"));
    let digits: String = mantissa.chars().filter(char::is_ascii_digit).collect();
    let k = digits.len() as i32;
    // The value is 0.digits × 10^point.
    let point = exp.parse::<i32>().unwrap_or(0) + 1;
    if k <= point && point <= 21 {
        out.push_str(&digits);
        out.extend(std::iter::repeat('0').take((point - k) as usize));
    } else if 0 < point && point <= 21 {
        let (int, frac) = digits.split_at(point as usize);
        out.push_str(int);
        out.push('.');
        out.push_str(frac);
    } else if -6 < point && point <= 0 {
        out.push_str("0.");
        out.extend(std::iter::repeat('0').take((-point) as usize));
        out.push_str(&digits);
    } else {
        let (first, rest) = digits.split_at(1.min(digits.len()));
        out.push_str(first);
        if !rest.is_empty() {
            out.push('.');
            out.push_str(rest);
        }
        out.push('e');
        out.push(if point > 0 { '+' } else { '-' });
        out.push_str(&(point - 1).abs().to_string());
    }
}

fn write_string(s: &str, out: &mut String) {
    out.push('"');
    for ch in s.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            '\u{8}' => out.push_str("\\b"),
            '\u{c}' => out.push_str("\\f"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
}

struct Reader<'a> {
    chars: &'a [char],
    at: usize,
    depth: usize,
}

impl Reader<'_> {
    fn peek(&self) -> Option<char> {
        self.chars.get(self.at).copied()
    }

    fn ws(&mut self) {
        while matches!(self.peek(), Some(' ' | '\t' | '\n' | '\r')) {
            self.at += 1;
        }
    }

    fn eat(&mut self, word: &str) -> bool {
        let n = word.chars().count();
        let ok = self
            .chars
            .get(self.at..self.at + n)
            .is_some_and(|s| s.iter().copied().eq(word.chars()));
        if ok {
            self.at += n;
        }
        ok
    }

    fn value(&mut self) -> Option<Json> {
        if self.depth > 200 {
            return None;
        }
        match self.peek()? {
            'n' => self.eat("null").then_some(Json::Null),
            't' => self.eat("true").then_some(Json::Bool(true)),
            'f' => self.eat("false").then_some(Json::Bool(false)),
            '"' => self.string().map(Json::Str),
            '[' => {
                self.at += 1;
                self.depth += 1;
                let mut items = Vec::new();
                self.ws();
                if self.peek() == Some(']') {
                    self.at += 1;
                } else {
                    loop {
                        self.ws();
                        items.push(self.value()?);
                        self.ws();
                        match self.peek()? {
                            ',' => self.at += 1,
                            ']' => {
                                self.at += 1;
                                break;
                            }
                            _ => return None,
                        }
                    }
                }
                self.depth -= 1;
                Some(Json::Arr(items))
            }
            '{' => {
                self.at += 1;
                self.depth += 1;
                let mut entries: Vec<(String, Json)> = Vec::new();
                self.ws();
                if self.peek() == Some('}') {
                    self.at += 1;
                } else {
                    loop {
                        self.ws();
                        let key = self.string()?;
                        self.ws();
                        if self.peek()? != ':' {
                            return None;
                        }
                        self.at += 1;
                        self.ws();
                        let v = self.value()?;
                        // A repeated key keeps its first position and takes the last value (JS).
                        match entries.iter_mut().find(|(k, _)| *k == key) {
                            Some(slot) => slot.1 = v,
                            None => entries.push((key, v)),
                        }
                        self.ws();
                        match self.peek()? {
                            ',' => self.at += 1,
                            '}' => {
                                self.at += 1;
                                break;
                            }
                            _ => return None,
                        }
                    }
                }
                self.depth -= 1;
                Some(Json::Obj(entries))
            }
            _ => self.number(),
        }
    }

    fn number(&mut self) -> Option<Json> {
        let start = self.at;
        while matches!(self.peek(), Some('0'..='9' | '-' | '+' | '.' | 'e' | 'E')) {
            self.at += 1;
        }
        let text: String = self.chars.get(start..self.at)?.iter().collect();
        text.parse::<f64>().ok().map(Json::Num)
    }

    fn hex4(&mut self) -> Option<u32> {
        let s: String = self.chars.get(self.at..self.at + 4)?.iter().collect();
        self.at += 4;
        u32::from_str_radix(&s, 16).ok()
    }

    fn string(&mut self) -> Option<String> {
        if self.peek()? != '"' {
            return None;
        }
        self.at += 1;
        let mut out = String::new();
        loop {
            let ch = self.peek()?;
            self.at += 1;
            match ch {
                '"' => return Some(out),
                '\\' => {
                    let e = self.peek()?;
                    self.at += 1;
                    match e {
                        'n' => out.push('\n'),
                        'r' => out.push('\r'),
                        't' => out.push('\t'),
                        'b' => out.push('\u{8}'),
                        'f' => out.push('\u{c}'),
                        'u' => {
                            let hi = self.hex4()?;
                            let cp = if (0xd800..0xdc00).contains(&hi) && self.eat("\\u") {
                                let lo = self.hex4()?;
                                if (0xdc00..0xe000).contains(&lo) {
                                    0x10000 + ((hi - 0xd800) << 10) + (lo - 0xdc00)
                                } else {
                                    // Two units that do not pair: both become U+FFFD.
                                    out.push('\u{fffd}');
                                    lo
                                }
                            } else {
                                hi
                            };
                            // A lone surrogate cannot live in a Rust string.
                            out.push(char::from_u32(cp).unwrap_or('\u{fffd}'));
                        }
                        other => out.push(other),
                    }
                }
                c => out.push(c),
            }
        }
    }
}
