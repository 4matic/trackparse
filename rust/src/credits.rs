//! R7.3–R7.5: splitting a credit list into names.

use crate::context::{match_known_at, Ctx};
use crate::options::SplitAndMode;
use crate::scanner::{render_skel, slice_skel, trim_skel, Skel};
use crate::tables::is_unknown_token;
use crate::types::{ArtistRole, ArtistSource};
use crate::words::{
    at, before, chars, collapse_chars, collapse_spaces, in_vocab, is_all_digits, is_digit,
    is_whitespace, js_trim_end, string, sub, tail, word_key, word_matches, year_of,
};

/// An artist credit plus its absolute offset, used for ordering (R9.1).
#[derive(Debug, Clone)]
pub(crate) struct Credit {
    pub name: String,
    pub role: ArtistRole,
    pub joiner: Option<String>,
    pub source: ArtistSource,
    pub pos: i64,
    /// Id of the credit list it came from (R7.6 joiner inheritance in R9.2).
    pub list: usize,
}

/// A year with its absolute offset (R9.6: the track year is the first in text order).
#[derive(Debug, Clone, Copy)]
pub(crate) struct PositionedYear {
    pub year: u32,
    pub pos: i64,
}

#[derive(Debug, Clone)]
pub(crate) struct ListOptions {
    pub role: ArtistRole,
    pub source: ArtistSource,
    /// Joiner of the first name: `None`, or the feat / producer marker as written.
    pub lead: Option<String>,
    /// R7.3: `with` splits only in the main list of the artist side.
    pub split_with: bool,
    /// R7.2: later feat markers act as joiners inside a featured list.
    pub feat_markers_join: bool,
}

#[derive(Debug, Clone, Default)]
pub(crate) struct ListResult {
    pub credits: Vec<Credit>,
    /// R7.5: an unknown token was dropped.
    pub unknown: bool,
    /// R7.5: dropped year-only names.
    pub years: Vec<PositionedYear>,
}

struct JoinerHit {
    start: usize,
    end: usize,
    raw: String,
}

const AND_LIKE: [&str; 3] = ["&", "and", "+"];

fn word_at(text: &[char], start: usize) -> usize {
    let mut end = start;
    while end < text.len() && !is_whitespace(at(text, end)) {
        end += 1;
    }
    end
}

/// The word after a joiner; a tight joiner (`,` `;`) ends it, since the name ends there.
fn next_word(text: &[char], from: usize) -> String {
    let mut a = from;
    while a < text.len() && is_whitespace(at(text, a)) {
        a += 1;
    }
    let mut b = a;
    while b < text.len() && !is_whitespace(at(text, b)) && !matches!(at(text, b), Some(',' | ';')) {
        b += 1;
    }
    string(sub(text, a, b))
}

/// R7.3b: the leftmost joiner occurrence at or after `from`, honouring the guards.
fn find_joiner(
    text: &[char],
    from: usize,
    split_at_comma: bool,
    opts: &ListOptions,
    ctx: &Ctx<'_>,
) -> Option<JoinerHit> {
    let mut p = from;
    while let Some(ch) = at(text, p) {
        if ch == ',' || ch == ';' {
            if ch == ',' && is_digit(before(text, p)) && is_digit(at(text, p + 1)) {
                p += 1;
                continue; // `1,000`
            }
            return Some(JoinerHit { start: p, end: p + 1, raw: ch.to_string() });
        }
        if p == 0 || before(text, p) != Some(' ') || is_whitespace(Some(ch)) {
            p += 1;
            continue;
        }
        let end = word_at(text, p);
        if end >= text.len() || at(text, end) != Some(' ') {
            p += 1;
            continue; // spaced joiners need a space after
        }
        let word = string(sub(text, p, end));
        if spaced_joiner(&word, text, end, split_at_comma, opts, ctx) {
            return Some(JoinerHit { start: p, end, raw: word });
        }
        p += 1;
    }
    None
}

fn spaced_joiner(
    word: &str,
    text: &[char],
    end: usize,
    split_at_comma: bool,
    opts: &ListOptions,
    ctx: &Ctx<'_>,
) -> bool {
    if opts.feat_markers_join && ctx.t.feat_markers.match_at(&[word], 0).is_some() {
        return true;
    }
    for j in &ctx.t.spaced_joiners {
        let matches = if j.case_sensitive { word == j.raw } else { word_matches(word, j.raw) };
        if !matches {
            continue;
        }
        if j.raw == "with" && !opts.split_with {
            return false;
        }
        if j.is_and {
            if ctx.split_and == SplitAndMode::Never {
                return false;
            }
            if ctx.split_and == SplitAndMode::Auto && !split_at_comma {
                return false;
            }
        }
        if AND_LIKE.contains(&j.raw) && in_vocab(&next_word(text, end), &ctx.t.no_split_before) {
            return false;
        }
        return true;
    }
    false
}

/// R7.4: clean one raw name.
pub(crate) fn clean_name(raw: &str) -> String {
    let mut name = chars(&collapse_spaces(raw));
    if let (Some(&first), Some(&last)) = (name.first(), name.last()) {
        if name.len() >= 2 && (first == '"' || first == '\'') && last == first {
            name = chars(&collapse_chars(sub(&name, 1, name.len() - 1)));
        }
    }
    let mut name: String = string(&name);
    while name.ends_with(',') || name.ends_with(';') {
        name.pop();
        name.truncate(js_trim_end(&name).len());
    }
    if let Some(space) = name.find(' ') {
        let head = name.get(..space).unwrap_or("");
        if space > 0 && word_key(head) == "by" {
            name = name.get(space + 1..).unwrap_or("").to_string();
        }
    }
    collapse_spaces(&name)
}

struct RawName {
    name: String,
    joiner: Option<String>,
    pos: i64,
}

/// R7.4: strip one pair of wrapping quotes from the whole list (no other such quote inside).
fn unwrap_list(list: &Skel) -> Skel {
    let t = trim_skel(list);
    let n = t.text.len();
    let Some(q) = at(&t.text, 0) else { return t };
    if n < 2 || (q != '"' && q != '\'') || at(&t.text, n - 1) != Some(q) {
        return t;
    }
    // `indexOf(q, 1)`: the next such quote must be the last character.
    let next = tail(&t.text, 1).iter().position(|&c| c == q).map(|i| i + 1);
    if next != Some(n - 1) {
        return t;
    }
    trim_skel(&slice_skel(&t, 1, n - 1))
}

/// R7.3: split `input` into credits.
pub(crate) fn split_credits(input: &Skel, opts: &ListOptions, ctx: &mut Ctx<'_>) -> ListResult {
    let list = unwrap_list(input); // R7.4: `"Camo & Krooked"`
    let text = &list.text;
    let mut raw: Vec<RawName> = Vec::new();
    let mut joiner: Option<String> = opts.lead.clone();
    let mut split_at_comma = false;
    let mut start = 0usize;
    while start <= text.len() {
        while start < text.len() && is_whitespace(at(text, start)) {
            start += 1;
        }
        let known = match_known_at(text, start, ctx, Some('-')); // R7.3a
        let hit = find_joiner(text, start + known, split_at_comma, opts, ctx);
        let end = hit.as_ref().map_or(text.len(), |h| h.start);
        raw.push(RawName {
            name: clean_name(&render_skel(&slice_skel(&list, start, end))),
            joiner: joiner.take(),
            pos: list.pos.get(start).or(list.pos.last()).copied().unwrap_or(0),
        });
        let Some(hit) = hit else { break };
        if hit.raw == "," || hit.raw == ";" {
            split_at_comma = true; // R7.3: `and` after a tight joiner
        }
        start = hit.end;
        joiner = Some(hit.raw);
    }
    finish_names(raw, opts, ctx)
}

/// R7.4 empty drops, R7.5 year/unknown drops. R7.6: the first survivor takes the lead joiner
/// (`None`, or the feat/producer marker).
fn finish_names(raw: Vec<RawName>, opts: &ListOptions, ctx: &mut Ctx<'_>) -> ListResult {
    let named: Vec<RawName> = raw.into_iter().filter(|r| !r.name.is_empty()).collect();
    let count = named.len();
    let mut unknown = false;
    let mut years: Vec<PositionedYear> = Vec::new();
    let mut kept: Vec<RawName> = Vec::new();
    for r in named {
        if is_unknown_token(&r.name, ctx.t) {
            unknown = true;
            continue;
        }
        let year = if is_all_digits(&r.name) { year_of(&r.name) } else { None };
        match year {
            Some(year) if count > 1 => years.push(PositionedYear { year, pos: r.pos }),
            _ => kept.push(r),
        }
    }
    let list = ctx.list_seq;
    ctx.list_seq += 1;
    let credits = kept
        .into_iter()
        .enumerate()
        .map(|(i, r)| Credit {
            name: r.name,
            role: opts.role,
            joiner: if i == 0 { opts.lead.clone() } else { r.joiner },
            source: opts.source,
            pos: r.pos,
            list,
        })
        .collect();
    ListResult { credits, unknown, years }
}
