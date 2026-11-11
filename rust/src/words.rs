//! Low-level text helpers shared by every rule: the R0.2 whitespace set, R0.3 word keys,
//! R0.4 dedup keys, R0.6 letters/digits and token-sequence (phrase) matching.
//!
//! Strings that are indexed are held as `&[char]`, so indices are code-point indices (R0.5).
//! Where the JS reference observes UTF-16 lengths (`utf16_len`), that is made explicit.

use std::collections::HashMap;

use unicode_normalization::UnicodeNormalization;

use crate::unicode_tables::{LETTER, UPPERCASE_LETTER};

pub(crate) fn chars(s: &str) -> Vec<char> {
    s.chars().collect()
}

pub(crate) fn string(t: &[char]) -> String {
    t.iter().collect()
}

/// `t[i]`, or `None` out of range (JS `t[i]` → `undefined`).
pub(crate) fn at(t: &[char], i: usize) -> Option<char> {
    t.get(i).copied()
}

/// `t[i - 1]`, or `None` when `i` is 0 or out of range.
pub(crate) fn before(t: &[char], i: usize) -> Option<char> {
    i.checked_sub(1).and_then(|j| t.get(j).copied())
}

/// `t[a..b]` with both ends clamped (JS `slice` with non-negative arguments).
pub(crate) fn sub(t: &[char], a: usize, b: usize) -> &[char] {
    let b = b.min(t.len());
    let a = a.min(b);
    t.get(a..b).unwrap_or(&[])
}

/// `t[a..]`, clamped.
pub(crate) fn tail(t: &[char], a: usize) -> &[char] {
    t.get(a.min(t.len())..).unwrap_or(&[])
}

/// R0.2: the exact whitespace code-point set. Never use `char::is_whitespace`.
pub(crate) fn is_whitespace_code(cp: u32) -> bool {
    (0x09..=0x0d).contains(&cp)
        || cp == 0x20
        || cp == 0x85
        || cp == 0xa0
        || cp == 0x1680
        || (0x2000..=0x200a).contains(&cp)
        || cp == 0x2028
        || cp == 0x2029
        || cp == 0x202f
        || cp == 0x205f
        || cp == 0x3000
}

/// R0.2 test on one code point.
pub(crate) fn is_ws(ch: char) -> bool {
    is_whitespace_code(ch as u32)
}

/// R0.2 test on an optional code point (`None` → false).
pub(crate) fn is_whitespace(ch: Option<char>) -> bool {
    ch.is_some_and(is_ws)
}

/// JS `String.prototype.trim()` set: WhiteSpace (with U+FEFF and Zs) + LineTerminator. Not the
/// R0.2 set: U+FEFF is in, U+0085 is out.
pub(crate) fn is_js_space(ch: char) -> bool {
    let cp = ch as u32;
    (0x09..=0x0d).contains(&cp)
        || cp == 0x20
        || cp == 0xa0
        || cp == 0x1680
        || (0x2000..=0x200a).contains(&cp)
        || cp == 0x2028
        || cp == 0x2029
        || cp == 0x202f
        || cp == 0x205f
        || cp == 0x3000
        || cp == 0xfeff
}

/// JS `trim()`.
pub(crate) fn js_trim(s: &str) -> &str {
    s.trim_matches(is_js_space)
}

/// JS `trimEnd()`.
pub(crate) fn js_trim_end(s: &str) -> &str {
    s.trim_end_matches(is_js_space)
}

/// JS `trim()` on a code-point slice.
pub(crate) fn js_trim_chars(t: &[char]) -> &[char] {
    let mut a = 0;
    let mut b = t.len();
    while a < b && at(t, a).is_some_and(is_js_space) {
        a += 1;
    }
    while b > a && before(t, b).is_some_and(is_js_space) {
        b -= 1;
    }
    sub(t, a, b)
}

/// R0.6: ASCII digits only.
pub(crate) fn is_digit(ch: Option<char>) -> bool {
    ch.is_some_and(|c| c.is_ascii_digit())
}

pub(crate) fn is_all_digits(s: &str) -> bool {
    !s.is_empty() && s.chars().all(|c| c.is_ascii_digit())
}

fn in_ranges(table: &[(u32, u32)], cp: u32) -> bool {
    table
        .binary_search_by(|&(lo, hi)| {
            if hi < cp {
                std::cmp::Ordering::Less
            } else if lo > cp {
                std::cmp::Ordering::Greater
            } else {
                std::cmp::Ordering::Equal
            }
        })
        .is_ok()
}

/// R0.6: Unicode general category L (JS `/\p{L}/u`). Not `char::is_alphabetic`, which also
/// accepts Nl and combining marks.
pub(crate) fn is_letter(ch: char) -> bool {
    in_ranges(LETTER, ch as u32)
}

/// R0.6 letter test on the code point at `index`.
pub(crate) fn is_letter_at(t: &[char], index: usize) -> bool {
    at(t, index).is_some_and(is_letter)
}

/// R0.6 letter test on the code point that ends just before `index`.
pub(crate) fn is_letter_before(t: &[char], index: usize) -> bool {
    before(t, index).is_some_and(is_letter)
}

/// R6.10 uppercase letter: Unicode general category Lu (JS `/\p{Lu}/u`). Not
/// `char::is_uppercase`, which is the wider `Uppercase` property.
pub(crate) fn is_upper(ch: char) -> bool {
    in_ranges(UPPERCASE_LETTER, ch as u32)
}

/// R6.10 uppercase test on the code point at `index`.
pub(crate) fn is_upper_at(t: &[char], index: usize) -> bool {
    at(t, index).is_some_and(is_upper)
}

/// ASCII-only lowercase (A–Z → a–z).
pub(crate) fn ascii_lower(s: &str) -> String {
    s.chars().map(|c| c.to_ascii_lowercase()).collect()
}

const KEY_STRIPPABLE: &str = ".,!?:;";

/// R0.3: the lowercased word with one trailing `. , ! ? : ;` removed.
pub(crate) fn strip_key_punct(lower: &str) -> &str {
    match lower.chars().next_back() {
        Some(last) if KEY_STRIPPABLE.contains(last) => {
            lower.get(..lower.len() - last.len_utf8()).unwrap_or(lower)
        }
        _ => lower,
    }
}

/// R0.3 word key (stripped form).
pub(crate) fn word_key(word: &str) -> String {
    strip_key_punct(&ascii_lower(word)).to_string()
}

/// R0.3: does `word` match the vocabulary token `token`? The unstripped lowercase form is
/// compared first so entries ending in punctuation (`feat.`) match as written.
pub(crate) fn word_matches(word: &str, token: &str) -> bool {
    let lower = ascii_lower(word);
    lower == token || strip_key_punct(&lower) == token
}

/// A set of vocabulary words that can be asked with a `&str`.
pub(crate) trait Vocab {
    fn has(&self, key: &str) -> bool;
}

impl Vocab for std::collections::HashSet<&'static str> {
    fn has(&self, key: &str) -> bool {
        self.contains(key)
    }
}

impl Vocab for std::collections::HashSet<String> {
    fn has(&self, key: &str) -> bool {
        self.contains(key)
    }
}

pub(crate) fn in_vocab(word: &str, vocab: &impl Vocab) -> bool {
    let lower = ascii_lower(word);
    vocab.has(&lower) || vocab.has(strip_key_punct(&lower))
}

fn collapse_iter(it: impl Iterator<Item = char>) -> String {
    let mut out = String::new();
    let mut pending = false;
    for ch in it {
        if is_ws(ch) {
            pending = !out.is_empty();
        } else {
            if pending {
                out.push(' ');
            }
            pending = false;
            out.push(ch);
        }
    }
    out
}

/// Collapse runs of R0.2 whitespace to one U+0020 and trim.
pub(crate) fn collapse_spaces(s: &str) -> String {
    collapse_iter(s.chars())
}

/// `collapse_spaces` on a code-point slice.
pub(crate) fn collapse_chars(t: &[char]) -> String {
    collapse_iter(t.iter().copied())
}

/// R0.7: maximal runs of non-whitespace code points.
pub(crate) fn split_words(s: &str) -> Vec<String> {
    s.split(is_ws).filter(|w| !w.is_empty()).map(str::to_string).collect()
}

/// `split_words` on a code-point slice.
pub(crate) fn split_words_chars(t: &[char]) -> Vec<String> {
    word_spans(t).into_iter().map(|w| w.text).collect()
}

/// A word with its code-point span inside the slice it was taken from.
#[derive(Debug, Clone)]
pub(crate) struct WordSpan {
    pub text: String,
    pub start: usize,
    pub end: usize,
}

pub(crate) fn word_spans(t: &[char]) -> Vec<WordSpan> {
    let mut spans = Vec::new();
    let mut start: Option<usize> = None;
    for i in 0..=t.len() {
        let boundary = i == t.len() || is_whitespace(at(t, i));
        if boundary {
            if let Some(a) = start {
                spans.push(WordSpan { text: string(sub(t, a, i)), start: a, end: i });
            }
            start = None;
        } else if start.is_none() {
            start = Some(i);
        }
    }
    spans
}

pub(crate) fn span_texts(spans: &[WordSpan]) -> Vec<String> {
    spans.iter().map(|s| s.text.clone()).collect()
}

/// R0.4 dedup key: JS `toLowerCase()`, NFD, U+0300–U+036F removed, whitespace collapsed.
pub(crate) fn dedup_key(s: &str) -> String {
    let lowered = s.to_lowercase();
    collapse_iter(lowered.nfd().filter(|c| !('\u{300}'..='\u{36f}').contains(c)))
}

/// Year word: 4 ASCII digits in 1900–2099 (word key, so `2015,` counts).
pub(crate) fn year_of(word: &str) -> Option<u32> {
    let key = word_key(word);
    if key.len() != 4 || !is_all_digits(&key) {
        return None;
    }
    let n: u32 = key.parse().ok()?;
    (1900..=2099).contains(&n).then_some(n)
}

/// Length in UTF-16 code units (JS `string.length`). Offsets are kept in UTF-16 units so that
/// every order-of-appearance comparison (R9) matches the JS reference exactly.
pub(crate) fn utf16_len(t: &[char]) -> usize {
    t.iter().map(|c| c.len_utf16()).sum()
}

/// `utf16_len` as an offset.
pub(crate) fn utf16_off(t: &[char]) -> i64 {
    i64::try_from(utf16_len(t)).unwrap_or(i64::MAX)
}

// ---------------------------------------------------------------------------
// Phrase tables: multi-word vocabulary matched as token sequences, longest first.

#[derive(Debug)]
struct PhraseEntry<V> {
    tokens: Vec<String>,
    value: V,
}

pub(crate) struct PhraseMatch<'a, V> {
    pub value: &'a V,
    /// Number of words consumed.
    pub length: usize,
}

#[derive(Debug)]
pub(crate) struct PhraseTable<V> {
    entries: Vec<PhraseEntry<V>>,
    by_first: HashMap<String, Vec<usize>>,
    by_last: HashMap<String, Vec<usize>>,
    keys: std::collections::HashSet<String>,
}

impl<V> PhraseTable<V> {
    pub fn new<K: AsRef<str>>(entries: impl IntoIterator<Item = (K, V)>) -> Self {
        let mut t = PhraseTable {
            entries: Vec::new(),
            by_first: HashMap::new(),
            by_last: HashMap::new(),
            keys: std::collections::HashSet::new(),
        };
        for (k, v) in entries {
            t.add(k.as_ref(), v);
        }
        t
    }

    /// The first entry for a key wins. Keys are ASCII-lowercased and trimmed like JS `trim()`.
    pub fn add(&mut self, raw_key: &str, value: V) {
        let lowered = ascii_lower(raw_key);
        let key = js_trim(&lowered);
        let tokens = split_words(key);
        let (Some(first), Some(last)) = (tokens.first().cloned(), tokens.last().cloned()) else {
            return;
        };
        if self.keys.contains(key) {
            return;
        }
        let id = self.entries.len();
        self.keys.insert(key.to_string());
        self.entries.push(PhraseEntry { tokens, value });
        self.insert_sorted(true, first, id);
        self.insert_sorted(false, last, id);
    }

    fn insert_sorted(&mut self, first: bool, k: String, id: usize) {
        let entries = &self.entries;
        let list = if first { &mut self.by_first } else { &mut self.by_last }.entry(k).or_default();
        list.push(id);
        // Stable, like the JS sort: longest first, insertion order among equals.
        list.sort_by_key(|&i| std::cmp::Reverse(entries.get(i).map_or(0, |e| e.tokens.len())));
    }

    /// Longest entry matching words[start …].
    pub fn match_at<S: AsRef<str>>(&self, words: &[S], start: usize) -> Option<PhraseMatch<'_, V>> {
        let word = words.get(start)?.as_ref();
        let mut best: Option<PhraseMatch<'_, V>> = None;
        for entry in self.candidates(true, word) {
            let n = entry.tokens.len();
            if best.as_ref().is_some_and(|b| n <= b.length) {
                continue;
            }
            if start + n > words.len() {
                continue;
            }
            if tokens_match(words, start, &entry.tokens) {
                best = Some(PhraseMatch { value: &entry.value, length: n });
            }
        }
        best
    }

    /// Longest entry matching words[… end-1] (end exclusive).
    pub fn match_ending<S: AsRef<str>>(
        &self,
        words: &[S],
        end: usize,
    ) -> Option<PhraseMatch<'_, V>> {
        let word = words.get(end.checked_sub(1)?)?.as_ref();
        let mut best: Option<PhraseMatch<'_, V>> = None;
        for entry in self.candidates(false, word) {
            let n = entry.tokens.len();
            if best.as_ref().is_some_and(|b| n <= b.length) {
                continue;
            }
            let Some(from) = end.checked_sub(n) else { continue };
            if tokens_match(words, from, &entry.tokens) {
                best = Some(PhraseMatch { value: &entry.value, length: n });
            }
        }
        best
    }

    /// Entries indexed under the word's lowercase form, then under its stripped form.
    fn candidates(&self, first: bool, word: &str) -> impl Iterator<Item = &PhraseEntry<V>> {
        let map = if first { &self.by_first } else { &self.by_last };
        let lower = ascii_lower(word);
        let stripped = strip_key_punct(&lower);
        let a = map.get(lower.as_str());
        let b = if stripped == lower { None } else { map.get(stripped) };
        a.into_iter().chain(b).flatten().filter_map(|&i| self.entries.get(i))
    }
}

fn tokens_match<S: AsRef<str>>(words: &[S], start: usize, tokens: &[String]) -> bool {
    tokens
        .iter()
        .enumerate()
        .all(|(k, t)| words.get(start + k).is_some_and(|w| word_matches(w.as_ref(), t)))
}

#[cfg(test)]
mod tests {
    //! R0 primitives against values taken from the JS reference (`scripts/dump_primitives.mjs`
    //! writes `tests/data/primitives.json`). `TRACKPARSE_PRIMITIVES=<file>` swaps in another
    //! dump, e.g. the `--all` sweep over every Unicode scalar value.

    use super::*;
    use crate::json::Json;

    fn data() -> Json {
        let text = match std::env::var("TRACKPARSE_PRIMITIVES") {
            Ok(path) => std::fs::read_to_string(path).unwrap_or_default(),
            Err(_) => include_str!("../tests/data/primitives.json").to_string(),
        };
        Json::parse(&text).unwrap_or(Json::Null)
    }

    fn num(v: &Json) -> u32 {
        match v {
            Json::Num(n) => *n as u32,
            _ => u32::MAX,
        }
    }

    /// The dumped code points with one flag string (`"0110…"`) of the named property.
    fn code_points(data: &Json, property: &str) -> Vec<(char, bool)> {
        let table = data.get("codePoints");
        let runs = table.and_then(|t| t.get("runs")).and_then(Json::as_array).unwrap_or(&[]);
        let cps = runs.iter().flat_map(|run| {
            let pair = run.as_array().unwrap_or(&[]);
            let first = pair.first().map_or(0, num);
            let count = pair.get(1).map_or(0, num);
            (first..first + count).filter_map(char::from_u32)
        });
        let flags = table.and_then(|t| t.get(property)).and_then(Json::as_str).unwrap_or("");
        let out: Vec<(char, bool)> = cps.zip(flags.chars().map(|f| f == '1')).collect();
        assert!(out.len() > 5000, "no code points for {property}");
        assert_eq!(out.len(), flags.len(), "{property}: flags and code points differ in length");
        out
    }

    fn check_property(property: &str, test: impl Fn(char) -> bool) {
        for (ch, expected) in code_points(&data(), property) {
            assert_eq!(test(ch), expected, "{property} of U+{:04X}", ch as u32);
        }
    }

    #[test]
    fn primitives_whitespace_set() {
        check_property("whitespace", is_ws);
        // R0.2, spelled out: U+0085 is in, U+FEFF, U+200B and U+180E are out.
        let set: Vec<u32> = (0..=0x10ffff).filter(|&cp| is_whitespace_code(cp)).collect();
        let expected: Vec<u32> = (0x09..=0x0d)
            .chain([0x20, 0x85, 0xa0, 0x1680])
            .chain(0x2000..=0x200a)
            .chain([0x2028, 0x2029, 0x202f, 0x205f, 0x3000])
            .collect();
        assert_eq!(set, expected);
    }

    #[test]
    fn primitives_letter_is_category_l() {
        check_property("letter", is_letter);
        assert!(is_letter('a') && is_letter('Я') && is_letter('中') && is_letter('ǅ'));
        // Nl and marks are alphabetic for Rust's std, but not letters.
        assert!('Ⅷ'.is_alphabetic() && !is_letter('Ⅷ'));
        assert!('\u{345}'.is_alphabetic() && !is_letter('\u{345}'));
        assert!(!is_letter('1') && !is_letter('_') && !is_letter('\u{301}'));
    }

    #[test]
    fn primitives_upper_is_category_lu() {
        check_property("upper", is_upper);
        assert!(is_upper('A') && is_upper('Я') && is_upper('𝐀'));
        // Other_Uppercase (Ⓐ, Ⅷ) is uppercase for Rust's std, but not Lu; ǅ is Lt.
        assert!('Ⓐ'.is_uppercase() && !is_upper('Ⓐ'));
        assert!('Ⅷ'.is_uppercase() && !is_upper('Ⅷ'));
        assert!(!is_upper('ǅ') && !is_upper('a') && !is_upper('中'));
    }

    #[test]
    fn primitives_js_trim_set() {
        check_property("jsSpace", is_js_space);
        assert_eq!(js_trim("\u{feff} x\u{a0}\u{2028}"), "x");
        assert_eq!(js_trim("\u{85}x\u{85}"), "\u{85}x\u{85}"); // R0.2 whitespace, not JS whitespace
        assert_eq!(js_trim_end(" x \u{feff}"), " x");
        assert_eq!(string(js_trim_chars(&chars("\u{3000}x y\t"))), "x y");
    }

    #[test]
    fn primitives_strings() {
        let data = data();
        let records = data.get("strings").and_then(Json::as_array).unwrap_or(&[]);
        assert!(records.len() > 250 || std::env::var("TRACKPARSE_PRIMITIVES").is_ok());
        for r in records {
            let field = |key: &str| r.get(key).and_then(Json::as_str).unwrap_or("<missing>");
            let s = field("s");
            assert_eq!(
                utf16_len(&chars(s)),
                r.get("utf16Len").map_or(0, num) as usize,
                "utf16Len {s:?}"
            );
            assert_eq!(ascii_lower(s), field("asciiLower"), "asciiLower {s:?}");
            assert_eq!(word_key(s), field("wordKey"), "wordKey {s:?}");
            assert_eq!(dedup_key(s), field("dedupKey"), "dedupKey {s:?}");
            assert_eq!(collapse_spaces(s), field("collapse"), "collapse {s:?}");
            assert_eq!(collapse_chars(&chars(s)), field("collapse"), "collapse_chars {s:?}");
            assert_eq!(js_trim(s), field("jsTrim"), "jsTrim {s:?}");
            assert_eq!(js_trim_end(s), field("jsTrimEnd"), "jsTrimEnd {s:?}");
            let words: Vec<&str> = r
                .get("words")
                .and_then(Json::as_array)
                .unwrap_or(&[])
                .iter()
                .filter_map(Json::as_str)
                .collect();
            assert_eq!(split_words(s), words, "splitWords {s:?}");
            assert_eq!(split_words_chars(&chars(s)), words, "split_words_chars {s:?}");
            let year = r.get("year").filter(|y| !matches!(y, Json::Null)).map(num);
            assert_eq!(year_of(s), year, "yearOf {s:?}");
        }
    }

    /// Only with the `--all` dump: every scalar value's dedup key (lowercase + NFD + marks).
    #[test]
    fn primitives_dedup_sweep() {
        let data = data();
        let Some(sweep) = data.get("dedupSweep").and_then(Json::as_array) else { return };
        let mut expected: HashMap<u32, &str> = HashMap::new();
        for pair in sweep {
            let pair = pair.as_array().unwrap_or(&[]);
            expected.insert(
                pair.first().map_or(0, num),
                pair.get(1).and_then(Json::as_str).unwrap_or(""),
            );
        }
        let mut wrong = Vec::new();
        for ch in (0..=0x10ffffu32).filter_map(char::from_u32) {
            let s = ch.to_string();
            let want = expected.get(&(ch as u32)).copied().unwrap_or(s.as_str());
            if dedup_key(&s) != want {
                wrong.push(format!("U+{:04X}: {:?}, JS {:?}", ch as u32, dedup_key(&s), want));
            }
        }
        assert!(
            wrong.is_empty(),
            "{} differ: {:?}",
            wrong.len(),
            wrong.iter().take(20).collect::<Vec<_>>()
        );
    }

    #[test]
    fn primitives_word_keys() {
        assert_eq!(word_key("FEAT."), "feat");
        assert_eq!(word_key("Remix!!"), "remix!");
        assert_eq!(word_key("É."), "É"); // ASCII lowercase only
        assert!(word_matches("Feat.", "feat.") && word_matches("Feat.", "feat"));
        assert!(!word_matches("feat", "feat."));
        assert_eq!(strip_key_punct("a;"), "a");
        assert_eq!(strip_key_punct("ä"), "ä");
    }

    #[test]
    fn primitives_dedup_keys() {
        // JS: "İstanbul".toLowerCase() is "i̇stanbul" (i + U+0307), and the mark is then removed.
        assert_eq!(dedup_key("İstanbul"), "istanbul");
        assert_eq!(dedup_key("STRAẞE"), "straße");
        assert_eq!(dedup_key("ΟΔΥΣΣΕΑΣ"), "οδυσσεας"); // final sigma
        assert_eq!(dedup_key("  Beyonce\u{301}   KNOWLES "), "beyonce knowles");
        assert_eq!(dedup_key("Й"), "и"); // U+0306 is in the removed range
        assert_eq!(dedup_key("か\u{3099}"), "か\u{3099}"); // other marks stay
    }

    #[test]
    fn primitives_phrase_table() {
        let t = PhraseTable::new([
            ("Official Video", 1),
            ("video", 2),
            (" official  video ", 3),
            ("", 4),
        ]);
        let words = ["Official", "VIDEO,", "x"];
        assert_eq!(t.match_at(&words, 0).map(|m| (*m.value, m.length)), Some((1, 2)));
        assert_eq!(t.match_at(&words, 1).map(|m| (*m.value, m.length)), Some((2, 1)));
        assert_eq!(t.match_ending(&words, 2).map(|m| (*m.value, m.length)), Some((1, 2)));
        assert!(t.match_ending(&words, 0).is_none() && t.match_at(&words, 3).is_none());
        // The first entry for a key wins (keys are trimmed and lowercased, not collapsed).
        assert_eq!(t.match_at(&["official", "video"], 0).map(|m| *m.value), Some(1));
    }
}
