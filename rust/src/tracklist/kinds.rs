//! R13.3 line kinds and R13.4.2 candidate lines.

use std::collections::HashSet;
use std::sync::{Arc, OnceLock};

use crate::data::{HEADER_WORDS_DATA, LAYERED_WORDS_DATA, MARKER_WORDS_DATA};
use crate::types::{ArtistRole, RawTrack, Warning};
use crate::words::{
    ascii_lower, at, chars, in_vocab, is_whitespace, is_ws, split_words_chars, string, sub,
    PhraseTable,
};

fn header_words() -> &'static HashSet<&'static str> {
    static WORDS: OnceLock<HashSet<&'static str>> = OnceLock::new();
    WORDS.get_or_init(|| HEADER_WORDS_DATA.get("words").strings().collect())
}

fn layered_words() -> &'static HashSet<&'static str> {
    static WORDS: OnceLock<HashSet<&'static str>> = OnceLock::new();
    WORDS.get_or_init(|| LAYERED_WORDS_DATA.get("words").strings().collect())
}

/// `data/marker-words.json` plus `options.markerWords` (R13.10). Entries are split into words the
/// same way as the track part (R13.3), so `shout-out` matches the words `shout`, `out`.
pub(crate) fn marker_table(extra: &[String]) -> Arc<PhraseTable<()>> {
    static DEFAULT: OnceLock<Arc<PhraseTable<()>>> = OnceLock::new();
    let entry = |w: &str| (marker_words(w).join(" "), ());
    let base = MARKER_WORDS_DATA.get("words").strings().map(entry);
    if extra.is_empty() {
        return DEFAULT.get_or_init(|| Arc::new(PhraseTable::new(base))).clone();
    }
    Arc::new(PhraseTable::new(base.chain(extra.iter().map(|w| entry(w)))))
}

fn is_marker_break(ch: char) -> bool {
    ch == '/' || ch == '-' || is_ws(ch)
}

fn is_bracket(ch: char) -> bool {
    matches!(ch, '(' | ')' | '[' | ']' | '{' | '}')
}

/// R13.3 marker words: split on R0.2 whitespace, `/` and `-`, then strip leading and trailing
/// `()[]{}` from each piece; empty pieces are dropped. Word keys (R0.3) are applied on matching.
pub(crate) fn marker_words(text: &str) -> Vec<String> {
    text.split(is_marker_break)
        .map(|piece| piece.trim_matches(is_bracket))
        .filter(|piece| !piece.is_empty())
        .map(str::to_string)
        .collect()
}

/// R13.13: the length of a layered prefix at the start of the line text: R0.2 whitespace, a word
/// of `data/layered-words.json` (word key, R0.3), then R0.2 whitespace, with more text after it.
/// 0 when there is none.
pub(crate) fn layered_prefix(text: &[char]) -> usize {
    let mut i = 0;
    while is_whitespace(at(text, i)) {
        i += 1;
    }
    let mut j = i;
    while j < text.len() && !is_whitespace(at(text, j)) {
        j += 1;
    }
    if j == i || j == text.len() || !in_vocab(&string(sub(text, i, j)), layered_words()) {
        return 0;
    }
    while is_whitespace(at(text, j)) {
        j += 1;
    }
    if j < text.len() {
        j
    } else {
        0
    }
}

/// R13.3.1
pub(crate) fn is_blank(raw: &str) -> bool {
    raw.chars().all(is_ws)
}

fn trim_ws(s: &str) -> &str {
    s.trim_matches(is_ws)
}

/// R13.3.3 link: `http://`, `https://` or `www.` (ASCII case-insensitive).
pub(crate) fn is_link(rest: &str) -> bool {
    let head: String = trim_ws(rest).chars().take(8).collect();
    let t = ascii_lower(&head);
    t.starts_with("http://") || t.starts_with("https://") || t.starts_with("www.")
}

/// R13.3.3 header: ends with `:`, or a header word followed by at most one more word.
pub(crate) fn is_header(rest: &str) -> bool {
    let t = trim_ws(rest);
    if t.ends_with(':') {
        return true;
    }
    let words = split_words_chars(&chars(t));
    words.len() <= 2 && words.first().is_some_and(|first| in_vocab(first, header_words()))
}

/// R13.3: any marker word (or multi-word entry) among the track part's marker words.
pub(crate) fn has_marker_word(track_part: &str, markers: &PhraseTable<()>) -> bool {
    let words = marker_words(track_part);
    (0..words.len()).any(|i| markers.match_at(&words, i).is_some())
}

/// R13.3: the track part has a separator: it is not empty after R1 (`empty`), its parse did not
/// warn `noSeparator`, and the artist side is not empty (at least one primary artist, or
/// `flags.unknownArtist`).
pub(crate) fn has_separator(track: &RawTrack, empty: bool) -> bool {
    if empty || track.warnings.contains(&Warning::NoSeparator) {
        return false;
    }
    track.flags.unknown_artist || track.artists.iter().any(|a| a.role == ArtistRole::Primary)
}
