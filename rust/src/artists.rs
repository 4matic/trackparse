//! R7: the artist side.

use std::collections::HashSet;

use crate::classify::{classify, Classification, Flag, Site};
use crate::context::Ctx;
use crate::credits::{split_credits, Credit, ListOptions, PositionedYear};
use crate::mode::PositionedJunk;
use crate::scanner::{groups_of, remove_groups, slice_from, slice_skel, trim_skel, Skel, PH};
use crate::types::{ArtistRole, ArtistSource};
use crate::words::{at, before, span_texts, string, sub, word_spans};

#[derive(Debug, Default)]
pub(crate) struct ArtistSideResult {
    pub credits: Vec<Credit>,
    pub junk: Vec<PositionedJunk>,
    pub years: Vec<PositionedYear>,
    pub unknown_artist: bool,
    /// R7.1: artist-side Flag groups.
    pub explicit: bool,
    pub clean: bool,
}

pub(crate) struct FeatMarker {
    pub start: usize,
    pub end: usize,
    pub marker: String,
}

/// R7.2: leftmost unbracketed feat marker with a space (or placeholder) left and a space right.
pub(crate) fn find_feat_marker(s: &Skel, ctx: &Ctx<'_>, from: usize) -> Option<FeatMarker> {
    let spans = word_spans(&s.text);
    let words = span_texts(&spans);
    for (i, span) in spans.iter().enumerate() {
        if span.start < from {
            continue;
        }
        if !matches!(before(&s.text, span.start), Some(' ' | PH)) {
            continue;
        }
        let Some(m) = ctx.t.feat_markers.match_at(&words, i) else { continue };
        let Some(last) = spans.get(i + m.length - 1) else { continue };
        if at(&s.text, last.end) != Some(' ') {
            continue;
        }
        return Some(FeatMarker {
            start: span.start,
            end: last.end,
            marker: string(sub(&s.text, span.start, last.end)),
        });
    }
    None
}

/// R7.1–R7.5 over an artist-side skeleton.
pub(crate) fn parse_artist_side(side: &Skel, ctx: &mut Ctx<'_>) -> ArtistSideResult {
    let mut result = ArtistSideResult::default();
    let mut removed: HashSet<i64> = HashSet::new();

    // R7.1
    for (_, group) in groups_of(side) {
        let site = Site { source: ArtistSource::Artist, delimiter: group.open, pos: group.pos };
        match classify(&group.inner, site, ctx) {
            Classification::Feat(c) | Classification::Producer(c) => {
                result.credits.extend(c.credits);
                result.years.extend(c.years);
                if c.unknown {
                    result.unknown_artist = true;
                }
            }
            Classification::Flag(Flag::Explicit) => result.explicit = true,
            Classification::Flag(Flag::Clean) => result.clean = true,
            Classification::Year(year) => {
                result.years.push(PositionedYear { year, pos: group.pos })
            }
            Classification::Junk(kind) => {
                result.junk.push(PositionedJunk { raw: group.inner.clone(), kind, pos: group.pos });
            }
            Classification::Versions(_) | Classification::Unknown => continue,
        }
        removed.insert(group.pos);
    }
    let text = remove_groups(side, &removed);

    // R7.2
    let feat = find_feat_marker(&text, ctx, 0);
    let main = match &feat {
        Some(f) => trim_skel(&slice_skel(&text, 0, f.start)),
        None => trim_skel(&text),
    };
    let main_list = split_credits(
        &main,
        &ListOptions {
            role: ArtistRole::Primary,
            source: ArtistSource::Artist,
            lead: None,
            split_with: true,
            feat_markers_join: false,
        },
        ctx,
    );
    result.credits.extend(main_list.credits);
    result.years.extend(main_list.years);
    if main_list.unknown {
        result.unknown_artist = true;
    }
    if let Some(f) = feat {
        let featured = split_credits(
            &trim_skel(&slice_from(&text, f.end)),
            &ListOptions {
                role: ArtistRole::Featured,
                source: ArtistSource::Artist,
                lead: Some(f.marker),
                split_with: false,
                feat_markers_join: true,
            },
            ctx,
        );
        result.credits.extend(featured.credits);
        result.years.extend(featured.years);
        if featured.unknown {
            result.unknown_artist = true;
        }
    }
    result.credits.sort_by_key(|c| c.pos); // stable, like the JS sort
    result
}
