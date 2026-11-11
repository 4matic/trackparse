//! R6: artist/title separator, including R6.2 suffix peeling (shared with R8.2).

use crate::classify::{classify, Classification, Flag, Site, VersionDraft};
use crate::context::{is_known_artist, warn, Ctx};
use crate::scanner::{
    concat_skel, has_group, render_skel, slice_from, slice_skel, split_at_spaced_dashes, start_pos,
    trim_skel, Segment, Skel, PH,
};
use crate::types::{ArtistSource, Mode, VersionDelimiter, Warning};
use crate::words::{
    at, before, collapse_chars, in_vocab, is_letter_at, is_letter_before, is_upper_at,
    is_whitespace, split_words_chars, tail, year_of,
};

/// The classifications R6.2 / R8.2 can peel.
#[derive(Debug, Clone)]
pub(crate) enum Peelable {
    Junk(String),
    Flag(Flag),
    Year(u32),
    Versions(Vec<VersionDraft>),
}

#[derive(Debug, Clone)]
pub(crate) struct Peeled {
    pub cls: Peelable,
    pub raw: String,
    pub pos: i64,
}

/// R6.2 classification of a bare dash segment: placeholders make it Unknown.
fn classify_segment(seg: &Skel, ctx: &mut Ctx<'_>) -> Classification {
    if has_group(seg) {
        return Classification::Unknown;
    }
    let text = collapse_chars(&seg.text);
    let site = Site {
        source: ArtistSource::Title,
        delimiter: VersionDelimiter::Dash,
        pos: start_pos(seg, 0),
    };
    classify(&text, site, ctx)
}

fn peelable(c: Classification) -> Option<Peelable> {
    match c {
        Classification::Junk(kind) => Some(Peelable::Junk(kind)),
        Classification::Flag(flag) => Some(Peelable::Flag(flag)),
        Classification::Year(year) => Some(Peelable::Year(year)),
        Classification::Versions(versions) => Some(Peelable::Versions(versions)),
        Classification::Feat(_) | Classification::Producer(_) | Classification::Unknown => None,
    }
}

/// R6.2 / R8.2: peel classifiable suffixes off the right end. `relaxed` drops conditions
/// (i)–(iv) (R8.2 with an artist side). Returns the remaining segments and the peeled
/// suffixes, left to right.
pub(crate) fn peel_suffixes(
    segments: Vec<Segment>,
    relaxed: bool,
    ctx: &mut Ctx<'_>,
) -> (Vec<Segment>, Vec<Peeled>) {
    let mut remaining = segments;
    let mut peeled: Vec<Peeled> = Vec::new();
    while remaining.len() >= 2 {
        let Some(last) = remaining.last() else { break };
        let Some(cls) = peelable(classify_segment(&last.skel, ctx)) else { break };
        let words = split_words_chars(&last.skel.text);
        // (ii) does not apply to prefix-form versions (`Andy C b2b Hedex - Live at Printworks`).
        let prefix_form =
            matches!(&cls, Peelable::Versions(vs) if vs.iter().all(|v| v.prefix_form));
        let ok = relaxed
            || remaining.len() > 2
            || (words.len() >= 2 && !prefix_form)
            || words.iter().any(|w| year_of(w).is_some())
            || matches!(cls, Peelable::Junk(_));
        if !ok {
            break;
        }
        peeled.push(Peeled {
            cls,
            raw: collapse_chars(&last.skel.text),
            pos: start_pos(&last.skel, 0),
        });
        remaining.pop();
    }
    peeled.reverse();
    (remaining, peeled)
}

pub(crate) struct SeparatorResult {
    pub artist: Option<Skel>,
    pub title: Skel,
    /// A separator split the string (R6.3–R6.7): R8.2 peels without conditions.
    pub split: bool,
    pub peeled: Vec<Peeled>,
}

pub(crate) struct SeparatorInput<'a> {
    pub skel: &'a Skel,
    pub mode: Mode,
    pub uploader: Option<Skel>,
    /// R13.4.2 list context (internal, set only by the tracklist module): enables the R6.10
    /// tight `letter-Uppercase` dash in tracklist mode.
    pub list_context: bool,
}

fn non_empty(s: &Skel) -> Option<Skel> {
    let t = trim_skel(s);
    (!t.is_empty()).then_some(t)
}

fn split_at(s: &Skel, cut_start: usize, cut_end: usize) -> (Option<Skel>, Skel) {
    (non_empty(&slice_skel(s, 0, cut_start)), trim_skel(&slice_from(s, cut_end)))
}

fn result(parts: (Option<Skel>, Skel), peeled: Vec<Peeled>) -> SeparatorResult {
    let split = parts.0.is_some();
    SeparatorResult { artist: parts.0, title: parts.1, split, peeled }
}

/// R6.3: the first segment is the artist side, the rest (up to the last kept one) the title.
fn first_dash_split(skel: &Skel, segments: &[Segment]) -> Option<(Option<Skel>, Skel)> {
    let [first, second, ..] = segments else { return None };
    let last = segments.last()?;
    let artist = (!first.skel.is_empty()).then(|| first.skel.clone());
    Some((artist, slice_skel(skel, second.start, last.end)))
}

pub(crate) fn find_separator(input: SeparatorInput<'_>, ctx: &mut Ctx<'_>) -> SeparatorResult {
    let skel = input.skel;
    let mode = input.mode;
    let segments = split_at_spaced_dashes(skel, true);
    if mode == Mode::Tracklist {
        return tracklist_separator(skel, &segments, input.list_context, ctx);
    }
    let (remaining, peeled) = peel_suffixes(segments, false, ctx);

    // R6.3
    if let Some(parts) = first_dash_split(skel, &remaining) {
        return result(parts, peeled);
    }

    let rest = remaining.first().map_or_else(|| slice_skel(skel, 0, 0), |s| s.skel.clone());
    if let Some(other) = split_without_spaced_dash(&rest, mode, ctx) {
        return result(other, peeled);
    }

    // R6.8: the uploader fallback does not warn noSeparator.
    let artist = if mode == Mode::Youtube { input.uploader } else { None };
    if !rest.is_empty() && artist.is_none() {
        warn(ctx, Warning::NoSeparator);
    }
    SeparatorResult { artist, title: rest, split: false, peeled }
}

/// R6.10: tracklist mode. The first spaced dash always separates (no R6.2 peeling; R8.2 still
/// peels later suffixes on the title side). Without one: R6.4, then the first `_-_`, then (list
/// context only) the last tight `letter-Uppercase` dash; both of these warn `unspacedDashSplit`.
fn tracklist_separator(
    skel: &Skel,
    segments: &[Segment],
    list_context: bool,
    ctx: &mut Ctx<'_>,
) -> SeparatorResult {
    if let Some(parts) = first_dash_split(skel, segments) {
        return result(parts, Vec::new());
    }
    let rest = segments.first().map_or_else(|| slice_skel(skel, 0, 0), |s| s.skel.clone());
    if let Some(asym) = asymmetric_dash(&rest.text) {
        warn(ctx, Warning::AsymmetricDashSplit);
        return result(split_at(&rest, asym, asym + 1), Vec::new());
    }
    if let Some(underscore) = rest.text.windows(3).position(|w| w == ['_', '-', '_']) {
        warn(ctx, Warning::UnspacedDashSplit);
        return result(split_at(&rest, underscore, underscore + 3), Vec::new());
    }
    if list_context {
        if let Some(tight) = tight_upper_dash(&rest.text) {
            warn(ctx, Warning::UnspacedDashSplit);
            return result(split_at(&rest, tight, tight + 1), Vec::new());
        }
    }
    if !rest.is_empty() {
        warn(ctx, Warning::NoSeparator);
    }
    SeparatorResult { artist: None, title: rest, split: false, peeled: Vec::new() }
}

/// R6.10: the last `-` with a letter (R0.6) on its left and an uppercase letter (Lu) on its
/// right, as in R6.7 (`Jay-Z-Numb` → `Jay-Z` / `Numb`).
fn tight_upper_dash(t: &[char]) -> Option<usize> {
    (1..t.len().saturating_sub(1))
        .rev()
        .find(|&i| at(t, i) == Some('-') && is_letter_before(t, i) && is_upper_at(t, i + 1))
}

fn split_without_spaced_dash(
    s: &Skel,
    mode: Mode,
    ctx: &mut Ctx<'_>,
) -> Option<(Option<Skel>, Skel)> {
    if let Some(asym) = asymmetric_dash(&s.text) {
        warn(ctx, Warning::AsymmetricDashSplit);
        return Some(split_at(s, asym, asym + 1));
    }
    if mode == Mode::Youtube {
        if let Some(quoted) = quoted_title(s) {
            warn(ctx, Warning::QuotedTitleSplit);
            return Some(quoted);
        }
        if let Some(by) = by_split(s, ctx) {
            warn(ctx, Warning::BySplit);
            let (left, right) = split_at(s, by, by + 4);
            let artist = (!right.is_empty()).then_some(right);
            return Some((artist, left.unwrap_or_else(|| s.clone())));
        }
    }
    if mode == Mode::Youtube || mode == Mode::Filename {
        if let Some(dash) = unspaced_dash(s, ctx) {
            warn(ctx, Warning::UnspacedDashSplit);
            return Some(split_at(s, dash, dash + 1));
        }
    }
    None
}

/// R6.4: the first `-` with a space on exactly one side.
fn asymmetric_dash(t: &[char]) -> Option<usize> {
    (1..t.len().saturating_sub(1)).find(|&i| {
        at(t, i) == Some('-') && is_whitespace(before(t, i)) != is_whitespace(at(t, i + 1))
    })
}

/// R6.5: `Artist "Title" (extra)`.
fn quoted_title(s: &Skel) -> Option<(Option<Skel>, Skel)> {
    let t = &s.text;
    let open = t.iter().position(|&c| c == '"').filter(|&i| i > 0)?;
    let close = tail(t, open + 1).iter().position(|&c| c == '"')? + open + 1;
    let mut artist = trim_skel(&slice_skel(s, 0, open));
    if matches!(artist.text.last(), Some(':' | '-')) {
        artist = trim_skel(&slice_skel(&artist, 0, artist.len() - 1));
    }
    let title = trim_skel(&concat_skel(&slice_skel(s, open + 1, close), &slice_from(s, close + 1)));
    Some(((!artist.is_empty()).then_some(artist), title))
}

/// R6.6: index of the space before the last usable ` by `. The right side must be non-empty and
/// not a single stopword (`Stand by Me`).
fn by_split(s: &Skel, ctx: &Ctx<'_>) -> Option<usize> {
    let t = &s.text;
    // Candidates right to left; index 0 is never one (`i > 0` in the JS loop).
    let mut i = t.len().checked_sub(4)?;
    while i > 0 {
        if t.get(i..i + 4) == Some(&[' ', 'b', 'y', ' ']) {
            // Placeholder-only words (groups such as `(Official Video)`) don't count here.
            let right: Vec<String> = split_words_chars(tail(t, i + 4))
                .into_iter()
                .filter(|w| !(w.chars().count() == 1 && w.starts_with(PH)))
                .collect();
            let single_stopword =
                right.len() == 1 && right.first().is_some_and(|w| in_vocab(w, &ctx.t.stopwords));
            if !right.is_empty() && !single_stopword {
                return Some(i);
            }
        }
        i -= 1;
    }
    None
}

/// R6.7: `Jay-Z-Numb` → the chosen unspaced dash index.
fn unspaced_dash(s: &Skel, ctx: &Ctx<'_>) -> Option<usize> {
    let t = &s.text;
    let mut last_candidate = None;
    for i in 1..t.len().saturating_sub(1) {
        if at(t, i) != Some('-') || is_whitespace(before(t, i)) || is_whitespace(at(t, i + 1)) {
            continue;
        }
        if !(at(t, i + 1) == Some('"') || is_letter_at(t, i + 1)) {
            continue;
        }
        if !ctx.known_keys.is_empty() && is_known_artist(&render_skel(&slice_skel(s, 0, i)), ctx) {
            return Some(i);
        }
        last_candidate = Some(i);
    }
    last_candidate
}
