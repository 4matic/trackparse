//! R13.5 bare positions.

use crate::digits::{digit_run, digits_value};
use crate::words::{at, chars, is_ws, string, sub, tail};

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct BareCandidate {
    pub raw: String,
    pub number: u32,
    /// The track part with the digits and the following space removed.
    pub stripped: String,
}

/// R13.5: a track part starting with 1–3 ASCII digits and a space (U+0020 only), where R4.2 took
/// no position (`position_taken`), is a bare candidate. The remainder must be non-empty.
pub(crate) fn bare_candidate(rest: &str, position_taken: bool) -> Option<BareCandidate> {
    if position_taken {
        return None;
    }
    let rest = &chars(rest);
    let n = digit_run(rest, 0);
    if !(1..=3).contains(&n) || at(rest, n) != Some(' ') {
        return None;
    }
    let stripped = tail(rest, n + 1);
    if stripped.iter().all(|c| is_ws(*c)) {
        return None;
    }
    let raw = sub(rest, 0, n);
    Some(BareCandidate { raw: string(raw), number: digits_value(raw), stripped: string(stripped) })
}

/// R13.5: candidate `i` is a position when the nearest preceding or the nearest following
/// candidate line (R13.4.2) also has a bare candidate equal to `N - 1` or `N + 1`. `bare` holds
/// each line's bare number. Linear time.
pub(crate) fn confirm_positions(bare: &[Option<u32>], candidate_line: &[bool]) -> Vec<bool> {
    let n = bare.len();
    let is_candidate = |i: usize| candidate_line.get(i).copied().unwrap_or(false);
    let mut prev: Vec<Option<usize>> = vec![None; n];
    let mut next: Vec<Option<usize>> = vec![None; n];
    let mut last = None;
    for (i, slot) in prev.iter_mut().enumerate() {
        *slot = last;
        if is_candidate(i) {
            last = Some(i);
        }
    }
    last = None;
    for (i, slot) in next.iter_mut().enumerate().rev() {
        *slot = last;
        if is_candidate(i) {
            last = Some(i);
        }
    }
    let adjacent = |b: u32, j: Option<usize>| {
        j.and_then(|j| bare.get(j).copied().flatten()).is_some_and(|o| o + 1 == b || o == b + 1)
    };
    bare.iter()
        .zip(prev.iter().zip(&next))
        .map(|(b, (p, nx))| b.is_some_and(|b| adjacent(b, *p) || adjacent(b, *nx)))
        .collect()
}
