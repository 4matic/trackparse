//! ASCII digit scanning shared by R4.1 (prefix.rs) and the tracklist time scanner (R13.2).

use crate::words::{at, is_digit, sub};

/// Length of the run of ASCII digits starting at `from`.
pub(crate) fn digit_run(s: &[char], from: usize) -> usize {
    let mut i = from;
    while is_digit(at(s, i)) {
        i += 1;
    }
    i - from
}

/// The value of a short run of ASCII digits (saturating; callers pass at most a few digits).
pub(crate) fn digits_value(s: &[char]) -> u32 {
    s.iter().fold(0u32, |n, c| n.saturating_mul(10).saturating_add(c.to_digit(10).unwrap_or(0)))
}

/// Exactly two digits (not followed by a third) with a value of at most 59, else `None`.
pub(crate) fn two_digits_up_to_59(s: &[char], at_: usize) -> Option<u32> {
    if !is_digit(at(s, at_)) || !is_digit(at(s, at_ + 1)) || is_digit(at(s, at_ + 2)) {
        return None;
    }
    let n = digits_value(sub(s, at_, at_ + 2));
    (n <= 59).then_some(n)
}
