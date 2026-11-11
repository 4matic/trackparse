//! R1: normalization.

use unicode_normalization::UnicodeNormalization;

use crate::words::{at, before, is_ws};

/// R1.2 invisible characters removed outright. ZWNJ/ZWJ (U+200C/U+200D) are kept (ZWJ emoji).
/// U+E000 is reserved as the R3.4 group placeholder, so it can never occur in input.
fn is_removed(cp: u32) -> bool {
    matches!(cp, 0x200b | 0x2060 | 0xfeff | 0x00ad | 0xe000)
}

/// R1.3 brackets, R1.4 quotes, R1.5 dashes → ASCII. `None` for a removed character.
fn map_char(ch: char) -> Option<char> {
    let cp = ch as u32;
    if is_removed(cp) {
        return None;
    }
    if (0x2010..=0x2015).contains(&cp) {
        return Some('-'); // R1.5
    }
    Some(match cp {
        0xff08 => '(',
        0xff09 => ')',
        0xff3b | 0x3010 | 0x3014 => '[',
        0xff3d | 0x3011 | 0x3015 => ']',
        0xff5b => '{',
        0xff5d => '}',
        0x201c | 0x201d | 0x201e | 0x00ab | 0x00bb | 0x300c | 0x300d | 0x300e | 0x300f | 0xff02 => {
            '"'
        }
        0x2018 | 0x2019 | 0x201a | 0x0060 | 0x00b4 => '\'',
        0x2212 | 0xfe58 | 0xfe63 | 0xff0d => '-',
        _ => ch,
    })
}

/// R1.5 (second half): a run of 2+ `-` with whitespace on both sides becomes one `-`.
fn collapse_spaced_dash_runs(chars: &[char]) -> Vec<char> {
    let mut out = Vec::with_capacity(chars.len());
    let mut i = 0;
    while let Some(ch) = at(chars, i) {
        if ch != '-' {
            out.push(ch);
            i += 1;
            continue;
        }
        let mut j = i;
        while at(chars, j) == Some('-') {
            j += 1;
        }
        let spaced =
            j - i >= 2 && before(chars, i).is_some_and(is_ws) && at(chars, j).is_some_and(is_ws);
        if spaced {
            out.push('-');
        } else {
            out.extend(std::iter::repeat('-').take(j - i));
        }
        i = j;
    }
    out
}

/// R1.6: every whitespace code point → one space, runs collapsed, trimmed.
fn collapse_whitespace(chars: &[char]) -> String {
    crate::words::collapse_chars(chars)
}

/// R1: normalize a track string. Idempotent.
pub(crate) fn normalize(input: &str) -> String {
    let chars: Vec<char> = input.nfc().filter_map(map_char).collect();
    // R1.8: removing invisibles can leave a decomposed sequence behind; re-compose so that
    // normalize() stays idempotent.
    collapse_whitespace(&collapse_spaced_dash_runs(&chars)).nfc().collect()
}
