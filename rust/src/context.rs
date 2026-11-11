//! Per-call parse context: resolved options + vocabulary tables.

use std::collections::HashSet;

use crate::options::SplitAndMode;
use crate::tables::Tables;
use crate::types::Warning;
use crate::words::{at, dedup_key, is_whitespace, string, sub};

pub(crate) struct Ctx<'a> {
    pub t: &'a Tables,
    pub known_keys: &'a HashSet<String>,
    /// Longest knownArtists entry, in UTF-16 units (bounds the R7.3a search).
    pub known_max_len: usize,
    pub split_and: SplitAndMode,
    pub warnings: Vec<Warning>,
    /// Counter giving every split credit list an id (R7.6 joiner inheritance across R9.2).
    pub list_seq: usize,
}

/// The `knownArtists` part of a context, computed once per parser.
pub(crate) fn known_keys(known: &[String]) -> (HashSet<String>, usize) {
    let keys = known.iter().map(|k| dedup_key(k)).filter(|k| !k.is_empty()).collect();
    let max_len = known.iter().map(|k| k.encode_utf16().count()).max().unwrap_or(0);
    (keys, max_len)
}

pub(crate) fn warn(ctx: &mut Ctx<'_>, w: Warning) {
    if !ctx.warnings.contains(&w) {
        ctx.warnings.push(w);
    }
}

/// R7.3a: length of the longest knownArtists entry matching `text` at `start` (dedup-key
/// comparison), followed by end, whitespace, `,` or `;` (or `extra_boundary`: R4.2 also accepts
/// `-`, so `3 Doors Down-Kryptonite` is protected). 0 when none matches.
///
/// The search window is `2 * longest + 8` UTF-16 units, exactly as in the JS reference.
pub(crate) fn match_known_at(
    text: &[char],
    start: usize,
    ctx: &Ctx<'_>,
    extra_boundary: Option<char>,
) -> usize {
    if ctx.known_keys.is_empty() {
        return 0;
    }
    let budget = ctx.known_max_len.saturating_mul(2).saturating_add(8);
    let mut limit = start;
    let mut used = 0usize;
    while let Some(ch) = at(text, limit) {
        let w = ch.len_utf16();
        if used + w > budget {
            break;
        }
        used += w;
        limit += 1;
    }
    let mut end = limit;
    while end > start {
        let next = at(text, end);
        let boundary = end == text.len()
            || next == Some(',')
            || next == Some(';')
            || is_whitespace(next)
            || (extra_boundary.is_some() && next == extra_boundary);
        if boundary && ctx.known_keys.contains(&dedup_key(&string(sub(text, start, end)))) {
            return end - start;
        }
        end -= 1;
    }
    0
}

pub(crate) fn is_known_artist(text: &str, ctx: &Ctx<'_>) -> bool {
    !ctx.known_keys.is_empty() && ctx.known_keys.contains(&dedup_key(text))
}
