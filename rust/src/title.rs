//! R8: the title side.

use std::collections::HashSet;

use crate::classify::{classify, Classification, Flag, Site, VersionDraft};
use crate::context::Ctx;
use crate::credits::{split_credits, Credit, ListOptions, PositionedYear};
use crate::mode::PositionedJunk;
use crate::scanner::{
    groups_of, remove_groups, render_skel, slice_skel, split_at_spaced_dashes, trim_skel, Skel, PH,
};
use crate::separator::{peel_suffixes, Peelable, Peeled};
use crate::tables::is_unknown_token;
use crate::types::{ArtistRole, ArtistSource};
use crate::words::{
    at, before, chars, collapse_chars, collapse_spaces, span_texts, string, sub, word_spans,
};

/// Everything the title side (and R6.2 peeling) can extract.
#[derive(Debug, Default)]
pub(crate) struct Extracted {
    pub credits: Vec<Credit>,
    pub versions: Vec<VersionDraft>,
    pub junk: Vec<PositionedJunk>,
    pub years: Vec<PositionedYear>,
    pub explicit: bool,
    pub clean: bool,
    pub unknown_artist: bool,
}

impl Extracted {
    fn set_flag(&mut self, flag: Flag) {
        match flag {
            Flag::Explicit => self.explicit = true,
            Flag::Clean => self.clean = true,
        }
    }
}

/// Record R6.2 / R8.2 peeled dash suffixes.
pub(crate) fn add_peeled(out: &mut Extracted, peeled: Vec<Peeled>) {
    for p in peeled {
        match p.cls {
            Peelable::Versions(versions) => out.versions.extend(versions),
            Peelable::Junk(kind) => out.junk.push(PositionedJunk { raw: p.raw, kind, pos: p.pos }),
            Peelable::Flag(flag) => out.set_flag(flag),
            Peelable::Year(year) => out.years.push(PositionedYear { year, pos: p.pos }),
        }
    }
}

pub(crate) struct TitleSideResult {
    pub title: String,
    pub unknown_title: bool,
}

/// `relaxed`: an artist side was split off (R8.2 peels without conditions).
pub(crate) fn parse_title_side(
    input: &Skel,
    relaxed: bool,
    youtube: bool,
    out: &mut Extracted,
    ctx: &mut Ctx<'_>,
) -> TitleSideResult {
    let removed = extract_groups(input, out, ctx); // R8.1

    // R8.2
    let segments = split_at_spaced_dashes(input, false);
    let (remaining, peeled) = peel_suffixes(segments, relaxed, ctx);
    add_peeled(out, peeled);
    let last_kept_end = remaining.last().map_or(0, |s| s.end);
    let mut skel = slice_skel(input, 0, last_kept_end);

    skel = extract_unbracketed_credits(&skel, out, ctx); // R8.3
    skel = remove_groups(&skel, &removed);
    if youtube {
        skel = strip_trailing_junk(&skel, out, ctx); // R8.4
    }

    let title = cleanup_title(&render_skel(&skel)); // R8.5
    let unknown_title = !title.is_empty() && is_unknown_token(&title, ctx.t); // R8.6
    TitleSideResult { title, unknown_title }
}

/// R8.1: classify every group; returns the positions of the groups to remove.
fn extract_groups(skel: &Skel, out: &mut Extracted, ctx: &mut Ctx<'_>) -> HashSet<i64> {
    let mut removed = HashSet::new();
    for (_, group) in groups_of(skel) {
        let site = Site { source: ArtistSource::Title, delimiter: group.open, pos: group.pos };
        match classify(&group.inner, site, ctx) {
            Classification::Versions(versions) => out.versions.extend(versions),
            Classification::Junk(kind) => {
                out.junk.push(PositionedJunk { raw: group.inner.clone(), kind, pos: group.pos });
            }
            Classification::Flag(flag) => out.set_flag(flag),
            Classification::Year(year) => out.years.push(PositionedYear { year, pos: group.pos }),
            Classification::Feat(c) | Classification::Producer(c) => {
                out.credits.extend(c.credits);
                out.years.extend(c.years);
                if c.unknown {
                    out.unknown_artist = true;
                }
            }
            Classification::Unknown => continue,
        }
        removed.insert(group.pos);
    }
    removed
}

struct MarkerHit {
    start: usize,
    end: usize,
    marker: String,
    role: ArtistRole,
}

/// R8.3 markers, left to right, each with a space on both sides: feat markers from
/// `#titleMarkers` (`feat.`, `ft.`, `featuring`; undotted `feat`/`ft` stay text) and
/// producer markers from `#titleProducer` (`prod.`, `prod. by`, `produced by`, `prod.by`).
fn title_markers(s: &Skel, ctx: &Ctx<'_>) -> Vec<MarkerHit> {
    let spans = word_spans(&s.text);
    let words = span_texts(&spans);
    let mut hits = Vec::new();
    let mut i = 0;
    while let Some(span) = spans.get(i) {
        let step = i + 1;
        if before(&s.text, span.start) != Some(' ') {
            i = step;
            continue;
        }
        let (role, last_index) = if let Some(feat) = ctx.t.title_feat_markers.match_at(&words, i) {
            (ArtistRole::Featured, i + feat.length - 1)
        } else if let Some(prod) = ctx.t.title_producer_markers.match_at(&words, i) {
            (ArtistRole::Producer, i + prod.length - 1)
        } else {
            i = step;
            continue;
        };
        let Some(last) = spans.get(last_index) else {
            i = step;
            continue;
        };
        if at(&s.text, last.end) != Some(' ') {
            i = step;
            continue;
        }
        hits.push(MarkerHit {
            start: span.start,
            end: last.end,
            marker: string(sub(&s.text, span.start, last.end)),
            role,
        });
        i = last_index + 1;
    }
    hits
}

/// R8.3: `Title feat. X`, `Title prod. by Y` → credits, removed from the title. A list runs to
/// the next placeholder, the next marker of the other kind, or the end; later feat markers
/// inside a featured list are joiners (R7.2).
fn extract_unbracketed_credits(input: &Skel, out: &mut Extracted, ctx: &mut Ctx<'_>) -> Skel {
    let markers = title_markers(input, ctx);
    if markers.is_empty() {
        return input.clone();
    }
    // `indexOf(PH, from)` for every `from`, computed once so many markers stay linear (R0.8).
    let len = input.text.len();
    let mut next_group = vec![len; len + 1];
    for k in (0..len).rev() {
        let next = if at(&input.text, k) == Some(PH) {
            k
        } else {
            next_group.get(k + 1).copied().unwrap_or(len)
        };
        if let Some(slot) = next_group.get_mut(k) {
            *slot = next;
        }
    }
    let mut cuts: Vec<(usize, usize)> = Vec::new();
    let mut i = 0;
    while let Some(hit) = markers.get(i) {
        let mut list_end = next_group.get(hit.end).copied().unwrap_or(len);
        let other = markers.iter().skip(i + 1).find(|m| m.role != hit.role && m.start >= hit.end);
        if let Some(other) = other {
            if other.start < list_end {
                list_end = other.start;
            }
        }
        let list = trim_skel(&slice_skel(input, hit.end, list_end));
        if !list.is_empty() {
            let r = split_credits(
                &list,
                &ListOptions {
                    role: hit.role,
                    source: ArtistSource::Title,
                    lead: Some(hit.marker.clone()),
                    split_with: false,
                    feat_markers_join: hit.role == ArtistRole::Featured,
                },
                ctx,
            );
            out.credits.extend(r.credits);
            out.years.extend(r.years);
            if r.unknown {
                out.unknown_artist = true;
            }
            cuts.push((hit.start, list_end));
        } else {
            list_end = hit.end;
        }
        while markers.get(i).is_some_and(|m| m.start < list_end) {
            i += 1;
        }
    }
    // The cuts are disjoint and ascending: remove them in one pass.
    let mut text: Vec<char> = Vec::with_capacity(len);
    let mut pos: Vec<i64> = Vec::with_capacity(len);
    let mut from = 0usize;
    for &(start, end) in &cuts {
        let start = start.min(len).max(from);
        text.extend_from_slice(sub(&input.text, from, start));
        pos.extend_from_slice(input.pos.get(from..start).unwrap_or(&[]));
        from = end.min(len).max(from);
    }
    text.extend_from_slice(sub(&input.text, from, len));
    pos.extend_from_slice(input.pos.get(from..).unwrap_or(&[]));
    Skel { text, pos, groups: std::rc::Rc::clone(&input.groups) }
}

/// R8.4: strip trailing unbracketed junk phrases (optionally after a lone `-` / `|`). Word spans
/// are computed once and walked backwards, so repeated junk stays linear (R0.8).
fn strip_trailing_junk(input: &Skel, out: &mut Extracted, ctx: &Ctx<'_>) -> Skel {
    let skel = trim_skel(input);
    let spans = word_spans(&skel.text);
    let words = span_texts(&spans);
    let mut end = spans.len(); // words still in the title
    while let Some(m) = ctx.t.junk_phrases.match_ending(&words, end) {
        let first = end - m.length.min(end);
        let mut keep = first;
        if keep > 0 && words.get(keep - 1).is_some_and(|w| is_lone_edge(w)) {
            keep -= 1;
        }
        if keep == 0 {
            break; // never strip the whole title
        }
        let from = spans.get(first).map_or(0, |s| s.start);
        let to = end.checked_sub(1).and_then(|k| spans.get(k)).map_or(skel.text.len(), |s| s.end);
        out.junk.push(PositionedJunk {
            raw: collapse_chars(sub(&skel.text, from, to)),
            kind: m.value.clone(),
            pos: skel.pos.get(from).copied().unwrap_or(0),
        });
        end = keep;
    }
    if end == spans.len() {
        return skel;
    }
    let cut = end.checked_sub(1).and_then(|k| spans.get(k)).map_or(0, |s| s.end);
    trim_skel(&slice_skel(&skel, 0, cut))
}

fn is_lone_edge(word: &str) -> bool {
    word == "-" || word == "|"
}

fn strip_lone_edges(s: &str) -> String {
    let words: Vec<&str> = s.split(' ').collect();
    let mut a = 0;
    let mut b = words.len();
    while a < b && words.get(a).is_some_and(|w| is_lone_edge(w)) {
        a += 1;
    }
    while b > a && words.get(b - 1).is_some_and(|w| is_lone_edge(w)) {
        b -= 1;
    }
    words.get(a..b).unwrap_or(&[]).join(" ")
}

/// R8.5
pub(crate) fn cleanup_title(raw: &str) -> String {
    let title = strip_lone_edges(&collapse_spaces(raw));
    let t = chars(&title);
    if let (Some(&q), Some(&last)) = (t.first(), t.last()) {
        if t.len() >= 2 && (q == '"' || q == '\'') && last == q {
            return strip_lone_edges(&collapse_chars(sub(&t, 1, t.len() - 1)));
        }
    }
    title
}
