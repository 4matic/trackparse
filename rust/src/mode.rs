//! R2: mode resolution, platform suffixes, filename handling and youtube pipes.

use crate::classify::junk_kind_of;
use crate::context::{warn, Ctx};
use crate::options::Requested;
use crate::scanner::{
    groups_of, has_spaced_dash, render_skel, scan, slice_skel, start_pos, Segment, Skel,
};
use crate::types::{Mode, Warning};
use crate::words::{
    ascii_lower, at, before, chars, collapse_spaces, js_trim_chars, span_texts, split_words,
    string, sub, tail, utf16_off, word_spans,
};

#[derive(Debug, Clone)]
pub(crate) struct PositionedJunk {
    pub raw: String,
    /// A junk kind name; `keywords.junk` values pass through as given.
    pub kind: String,
    pub pos: i64,
}

/// A contiguous piece of the normalized string and its absolute offset.
#[derive(Debug, Clone)]
pub(crate) struct Piece {
    pub text: Vec<char>,
    pub base: i64,
}

pub(crate) struct Prelude {
    pub mode: Mode,
    /// The working string (R2.4 step 2/4), or the artist piece in the `artist | title` case.
    pub working: Piece,
    /// R2.4 step 3: the title piece when two dash-less pipe segments remain.
    pub pipe_title: Option<Piece>,
    pub junk: Vec<PositionedJunk>,
}

fn ascii_lower_chars(t: &[char]) -> Vec<char> {
    t.iter().map(|c| c.to_ascii_lowercase()).collect()
}

/// R2.2: strip trailing platform suffixes (longest first, repeatedly).
fn strip_platform_suffixes(s: &[char], ctx: &Ctx<'_>, junk: &mut Vec<PositionedJunk>) -> Vec<char> {
    let mut text = s.to_vec();
    let mut stripped = true;
    while stripped {
        stripped = false;
        let lower = ascii_lower_chars(&text);
        for suffix in &ctx.t.platform_suffixes {
            let suffix_lower = chars(&ascii_lower(suffix));
            if !lower.ends_with(&suffix_lower) {
                continue;
            }
            let start = text.len() - suffix_lower.len().min(text.len());
            junk.push(PositionedJunk {
                raw: string(tail(&text, start + 3)),
                kind: "platform".to_string(),
                pos: utf16_off(sub(&text, 0, start)) + 3,
            });
            text.truncate(start);
            stripped = true;
            break;
        }
    }
    text
}

fn last_dot(s: &[char]) -> Option<usize> {
    s.iter().rposition(|&c| c == '.')
}

fn has_extension(s: &[char], ctx: &Ctx<'_>) -> bool {
    last_dot(s).is_some_and(|dot| {
        let ext = ascii_lower(&string(tail(s, dot + 1)));
        ctx.t.extensions.contains(ext.as_str())
    })
}

/// Top-level ` | ` / ` || ` separators on a skeleton → trimmed segment ranges.
pub(crate) fn split_pipes(skel: &Skel) -> Vec<Segment> {
    let t = &skel.text;
    let mut cuts: Vec<(usize, usize)> = Vec::new();
    let mut i = 1;
    while i < t.len() {
        if before(t, i) == Some(' ') && at(t, i) == Some('|') {
            let bars = if at(t, i + 1) == Some('|') { 2 } else { 1 };
            if at(t, i + bars) == Some(' ') {
                cuts.push((i - 1, i + bars + 1));
                i += bars;
            }
        }
        i += 1;
    }
    cuts.push((t.len(), t.len()));
    let mut segments = Vec::with_capacity(cuts.len());
    let mut start = 0usize;
    for (a, b) in cuts {
        let mut s = start;
        let mut e = a;
        while s < e && at(t, s) == Some(' ') {
            s += 1;
        }
        while e > s && before(t, e) == Some(' ') {
            e -= 1;
        }
        segments.push(Segment { skel: slice_skel(skel, s, e), start: s, end: e });
        start = b;
    }
    segments
}

/// R8.4 test: the skeleton ends with an unbracketed junk phrase (genres excluded). Returns the
/// code-point index where the phrase starts, and its kind.
pub(crate) fn trailing_junk_phrase(text: &[char], ctx: &Ctx<'_>) -> Option<(usize, String)> {
    let spans = word_spans(text);
    let words = span_texts(&spans);
    let m = ctx.t.junk_phrases.match_ending(&words, spans.len())?;
    let first = spans.get(spans.len().checked_sub(m.length)?)?;
    Some((first.start, m.value.clone()))
}

fn junk_text_kind(text: &str, ctx: &Ctx<'_>) -> Option<String> {
    let words = split_words(text);
    if words.is_empty() {
        None
    } else {
        junk_kind_of(&words, ctx)
    }
}

/// R2.1 youtube signals (besides platform suffixes).
fn looks_like_youtube(skel: &Skel, ctx: &Ctx<'_>) -> bool {
    if split_pipes(skel).len() > 1 {
        return true;
    }
    for (_, group) in groups_of(skel) {
        if junk_text_kind(&group.inner, ctx).is_some_and(|kind| kind != "genre") {
            return true;
        }
    }
    trailing_junk_phrase(&skel.text, ctx).is_some()
}

pub(crate) fn prelude(normalized: &str, requested: Requested, ctx: &mut Ctx<'_>) -> Prelude {
    let mut junk: Vec<PositionedJunk> = Vec::new();
    let mut text = strip_platform_suffixes(&chars(normalized), ctx, &mut junk);
    let had_platform = !junk.is_empty();

    let mut mode = match requested {
        Requested::Mode(m) => m, // R13.6: tracklist is explicit only; R2.3/R2.4 do not run.
        _ if has_extension(&text, ctx) => Mode::Filename,
        _ => Mode::Clean,
    };

    if mode == Mode::Filename {
        // R2.3: strip the extension, trim, then `_` → space when there is no space.
        if has_extension(&text, ctx) {
            text.truncate(last_dot(&text).unwrap_or(text.len()));
        }
        text = js_trim_chars(&text).to_vec();
        if !text.contains(&' ') && text.contains(&'_') {
            let spaced: Vec<char> = text.iter().map(|&c| if c == '_' { ' ' } else { c }).collect();
            text = js_trim_chars(&spaced).to_vec();
        }
    }

    let (skel, unbalanced) = scan(&text, 0);
    if unbalanced {
        warn(ctx, Warning::UnbalancedBrackets);
    }
    if requested == Requested::Auto
        && mode == Mode::Clean
        && (had_platform || looks_like_youtube(&skel, ctx))
    {
        mode = Mode::Youtube;
    }

    if mode != Mode::Youtube {
        return Prelude { mode, working: Piece { text, base: 0 }, pipe_title: None, junk };
    }
    let (working, pipe_title) = split_youtube_pipes(&skel, ctx, &mut junk);
    Prelude { mode, working, pipe_title, junk }
}

fn piece_of(seg: &Segment) -> Piece {
    Piece { text: chars(&render_skel(&seg.skel)), base: start_pos(&seg.skel, 0) }
}

/// R2.4
fn split_youtube_pipes(
    skel: &Skel,
    ctx: &mut Ctx<'_>,
    junk: &mut Vec<PositionedJunk>,
) -> (Piece, Option<Piece>) {
    let segments = split_pipes(skel);
    if segments.len() <= 1 {
        return (Piece { text: chars(&render_skel(skel)), base: 0 }, None);
    }

    let mut remaining: Vec<&Segment> = Vec::new();
    for seg in &segments {
        let text = collapse_spaces(&render_skel(&seg.skel));
        // R2.4 step 1: a segment with a spaced dash is an artist/title candidate, never junk.
        let kind = if has_spaced_dash(&seg.skel.text) { None } else { junk_text_kind(&text, ctx) };
        match kind {
            Some(kind) if !kind.is_empty() => {
                junk.push(PositionedJunk { raw: text, kind, pos: start_pos(&seg.skel, 0) });
            }
            _ if !text.is_empty() => remaining.push(seg),
            _ => {}
        }
    }
    let push_other = |keep: usize, junk: &mut Vec<PositionedJunk>| {
        for (i, seg) in remaining.iter().enumerate() {
            if i == keep {
                continue;
            }
            junk.push(PositionedJunk {
                raw: collapse_spaces(&render_skel(&seg.skel)),
                kind: "other".to_string(),
                pos: start_pos(&seg.skel, 0),
            });
        }
    };

    if let Some(i) = remaining.iter().position(|seg| has_spaced_dash(&seg.skel.text)) {
        push_other(i, junk);
        return (remaining.get(i).map_or_else(empty_piece, |s| piece_of(s)), None);
    }
    if let [first, second] = remaining.as_slice() {
        warn(ctx, Warning::AmbiguousSeparator);
        return (piece_of(first), Some(piece_of(second)));
    }
    if let Some(first) = remaining.first() {
        push_other(0, junk);
        return (piece_of(first), None);
    }
    // Every segment was junk: nothing left to parse.
    (empty_piece(), None)
}

fn empty_piece() -> Piece {
    Piece { text: Vec::new(), base: 0 }
}
