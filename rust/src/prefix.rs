//! R4: leading timestamp and track-position prefixes.

use crate::context::{match_known_at, Ctx};
use crate::digits::{digit_run, digits_value, two_digits_up_to_59};
use crate::scanner::{has_spaced_dash, scan};
use crate::types::{Mode, Position, Timestamp};
use crate::words::{at, string, sub, tail, utf16_off};

pub(crate) struct PrefixResult {
    pub rest: Vec<char>,
    /// Absolute offset of `rest`.
    pub base: i64,
    pub timestamp: Option<Timestamp>,
    pub position: Option<Position>,
}

struct TimeMatch {
    raw: String,
    seconds: u64,
    end: usize,
}

/// `H:MM:SS`, `HH:MM:SS`, `M:SS`, `MM:SS` starting at `at_`.
fn match_time(s: &[char], at_: usize) -> Option<TimeMatch> {
    let lead = digit_run(s, at_);
    if !(1..=2).contains(&lead) || at(s, at_ + lead) != Some(':') {
        return None;
    }
    let first = u64::from(digits_value(sub(s, at_, at_ + lead)));
    let mid = u64::from(two_digits_up_to_59(s, at_ + lead + 1)?);
    let after_mid = at_ + lead + 3;
    if at(s, after_mid) == Some(':') {
        if let Some(sec) = two_digits_up_to_59(s, after_mid + 1) {
            let end = after_mid + 3;
            return Some(TimeMatch {
                raw: string(sub(s, at_, end)),
                seconds: first * 3600 + mid * 60 + u64::from(sec),
                end,
            });
        }
    }
    if lead == 2 && first > 59 {
        return None; // MM is 00–59
    }
    Some(TimeMatch {
        raw: string(sub(s, at_, after_mid)),
        seconds: first * 60 + mid,
        end: after_mid,
    })
}

/// After a prefix: skip one space, then an optional `- ` (spaced dash).
fn skip_space_and_dash(s: &[char], at_: usize) -> usize {
    let mut i = at_;
    if at(s, i) == Some(' ') {
        i += 1;
    }
    if at(s, i) == Some('-') && at(s, i + 1) == Some(' ') {
        i += 2;
    }
    i
}

/// R4.1
fn match_timestamp(s: &[char]) -> Option<(Timestamp, usize)> {
    let bracket = match at(s, 0) {
        Some('[') => Some(']'),
        Some('(') => Some(')'),
        _ => None,
    };
    let t = match_time(s, usize::from(bracket.is_some()))?;
    let mut end = t.end;
    if bracket.is_some() {
        if at(s, end) != bracket {
            return None;
        }
        end += 1;
    }
    if end < s.len() && at(s, end) != Some(' ') {
        return None;
    }
    Some((Timestamp { raw: t.raw, seconds: t.seconds }, skip_space_and_dash(s, end)))
}

fn contains_spaced_dash(s: &[char]) -> bool {
    has_spaced_dash(&scan(s, 0).0.text)
}

/// R4.2: end offset of the position prefix, or `None`. Forms are tried (a), (b), (c), (d)/(e).
fn match_position(s: &[char], mode: Mode) -> Option<usize> {
    let n = digit_run(s, 0);
    if !(1..=3).contains(&n) {
        return None;
    }
    let c = at(s, n);
    let zero_padded = at(s, 0) == Some('0') && n >= 2;
    // (a) `01. `, `1) `
    if (c == Some('.') || c == Some(')')) && (at(s, n + 1) == Some(' ') || n + 1 == s.len()) {
        return Some(skip_space_and_dash(s, n + 1));
    }
    // (b) `01 `: a position in every mode.
    if zero_padded && c == Some(' ') {
        return Some(skip_space_and_dash(s, n));
    }
    // (c) `3 - Artist - Title`; in filename mode `N - ` is always a position (`311 - Amber.mp3`).
    if c == Some(' ') && at(s, n + 1) == Some('-') && at(s, n + 2) == Some(' ') {
        if mode == Mode::Filename || contains_spaced_dash(tail(s, n + 3)) {
            return Some(n + 3);
        }
        return None;
    }
    if mode == Mode::Filename {
        // (e) `01-Track`, `03_name`, `01.Track`
        if matches!(c, Some('-' | '_' | '.')) && at(s, n + 1).is_some_and(|ch| ch != ' ') {
            return Some(n + 1);
        }
        // (e) `3 My Song.mp3`: only when the remainder has no spaced dash.
        if c == Some(' ') && !contains_spaced_dash(tail(s, n + 1)) {
            return Some(skip_space_and_dash(s, n));
        }
    }
    // (d) `2 Unlimited`, `50 Cent`: never a position outside filename mode.
    None
}

pub(crate) fn strip_prefixes(text: &[char], base: i64, mode: Mode, ctx: &Ctx<'_>) -> PrefixResult {
    let mut rest: &[char] = text;
    let mut offset = base;
    let mut timestamp = None;
    let mut position = None;

    if let Some((ts, end)) = match_timestamp(rest) {
        timestamp = Some(ts);
        offset += utf16_off(sub(rest, 0, end));
        rest = tail(rest, end);
    }
    if match_known_at(rest, 0, ctx, Some('-')) == 0 {
        if let Some(end) = match_position(rest, mode).filter(|&end| end < rest.len()) {
            let digits = sub(rest, 0, digit_run(rest, 0));
            position = Some(Position { raw: string(digits), number: digits_value(digits) });
            offset += utf16_off(sub(rest, 0, end));
            rest = tail(rest, end);
        }
    }
    PrefixResult { rest: rest.to_vec(), base: offset, timestamp, position }
}
