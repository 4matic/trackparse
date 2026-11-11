//! R11: render a parsed track back to a string.

use std::collections::HashSet;

use crate::assemble::render_version;
use crate::options::{FeatMode, ResolvedFormat};
use crate::tables::{default_tables, Tables};
use crate::types::{Artist, ArtistRole, ArtistSource, VersionDelimiter};
use crate::words::{dedup_key, word_matches};

/// The parts of a track R11 reads.
pub(crate) struct FormatInput<'a> {
    pub artists: &'a [Artist],
    pub title: &'a str,
    pub versions: Vec<(&'a str, VersionDelimiter)>,
    pub position_raw: Option<&'a str>,
}

fn is_feat_marker(raw: &str, t: &Tables) -> bool {
    t.feat_markers.match_at(&[raw], 0).is_some()
        || t.bracket_only_feat.match_at(&[raw], 0).is_some()
}

/// R11.5: map a raw joiner to its canonical form through data/joiners.json; feat markers become
/// `featCanonical`. A featured list's lead marker is always a feat marker (`with` → `feat.`),
/// while between names the joiner table wins (`Sigala with Ella Eyre` keeps `with`).
fn canonical_joiner(raw: &str, t: &Tables, feat_lead: bool) -> String {
    if feat_lead && is_feat_marker(raw, t) {
        return t.feat_canonical.to_string();
    }
    if let Some(tight) = t.tight_joiners.get(raw) {
        return (*tight).to_string();
    }
    for j in &t.spaced_joiners {
        if if j.case_sensitive { raw == j.raw } else { word_matches(raw, j.raw) } {
            return j.canonical.to_string();
        }
    }
    if is_feat_marker(raw, t) {
        t.feat_canonical.to_string()
    } else {
        raw.to_string()
    }
}

/// R11.1: `" " + joiner + " "`; tight joiners as the raw char + `" "`; a missing joiner as `&`.
fn join_sep(joiner: Option<&str>, o: &ResolvedFormat, t: &Tables) -> String {
    let j = match joiner {
        None => "&".to_string(),
        Some(j) if o.canonical => canonical_joiner(j, t, false),
        Some(j) => j.to_string(),
    };
    if t.tight_joiners.contains_key(j.as_str()) {
        format!("{j} ")
    } else {
        format!(" {j} ")
    }
}

/// Names of one credit list joined by their own joiners (the first joiner is not rendered).
fn join_names(list: &[&Artist], o: &ResolvedFormat, t: &Tables) -> String {
    let mut out = String::new();
    for (i, a) in list.iter().enumerate() {
        if i > 0 {
            out.push_str(&join_sep(a.joiner.as_deref(), o, t));
        }
        out.push_str(&a.name);
    }
    out
}

/// The marker in front of a featured list.
fn lead_marker(first: &Artist, o: &ResolvedFormat, t: &Tables, fallback: &str) -> String {
    if let Some(marker) = &o.feat_marker {
        return marker.clone();
    }
    let raw = first.joiner.as_deref().unwrap_or(fallback);
    if o.canonical {
        canonical_joiner(raw, t, true)
    } else {
        raw.to_string()
    }
}

pub(crate) fn format(track: &FormatInput<'_>, o: &ResolvedFormat) -> String {
    let tables = default_tables();
    let t: &Tables = &tables;
    let with_role = |role: ArtistRole| {
        track.artists.iter().filter(|a| a.role == role).collect::<Vec<&Artist>>()
    };
    let primaries = with_role(ArtistRole::Primary);
    let featured = with_role(ArtistRole::Featured);
    let producers = with_role(ArtistRole::Producer);

    let (artist_feat, title_feat): (Vec<&Artist>, Vec<&Artist>) = match o.feat {
        FeatMode::Source => {
            featured.iter().copied().partition(|a| a.source == ArtistSource::Artist)
        }
        FeatMode::Artist => (featured, Vec::new()),
        FeatMode::Title => (Vec::new(), featured),
        FeatMode::Omit => (Vec::new(), Vec::new()),
    };

    // R11.1 / R11.2
    let mut artist_part = join_names(&primaries, o, t);
    if let Some(first) = artist_feat.first() {
        let lead = lead_marker(first, o, t, t.feat_canonical);
        let names = join_names(&artist_feat, o, t);
        artist_part = if artist_part.is_empty() {
            format!("{lead} {names}")
        } else {
            format!("{artist_part} {lead} {names}")
        };
    }

    // R11.3
    let mut pieces: Vec<String> = Vec::new();
    if !track.title.is_empty() {
        pieces.push(track.title.to_string());
    }
    if let Some(first) = title_feat.first() {
        let lead = lead_marker(first, o, t, t.feat_canonical);
        pieces.push(format!("({lead} {})", join_names(&title_feat, o, t)));
    }
    if o.versions {
        pieces
            .extend(track.versions.iter().map(|(raw, delimiter)| render_version(raw, *delimiter)));
    }
    if let (true, Some(first)) = (o.producers, producers.first()) {
        let marker = first.joiner.as_deref().unwrap_or("prod.");
        pieces.push(format!("({marker} {})", join_names(&producers, o, t)));
    }
    let title_part = pieces.join(" ");

    // R11.4
    let mut result =
        if artist_part.is_empty() { title_part } else { format!("{artist_part} - {title_part}") };
    if let (true, Some(raw)) = (o.position, track.position_raw) {
        result = format!("{raw}. {result}");
    }
    result
}

/// All credited names: track artists plus every version's remixers, deduped by R0.4 key.
pub(crate) fn all_artists<'a>(all: impl Iterator<Item = &'a Artist>) -> Vec<Artist> {
    let mut seen: HashSet<String> = HashSet::new();
    all.filter(|a| seen.insert(dedup_key(&a.name))).cloned().collect()
}
