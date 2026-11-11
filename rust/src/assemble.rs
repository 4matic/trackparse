//! R9: assembly and dedup.

use std::collections::HashMap;

use crate::classify::VersionDraft;
use crate::credits::Credit;
use crate::types::{Artist, ArtistRole, RawVersion, VersionDelimiter};
use crate::words::dedup_key;

fn precedence(role: ArtistRole) -> u8 {
    match role {
        ArtistRole::Primary => 2,
        ArtistRole::Featured => 1,
        ArtistRole::Producer | ArtistRole::Remixer => 0,
    }
}

pub(crate) fn to_artist(c: Credit) -> Artist {
    Artist { name: c.name, role: c.role, joiner: c.joiner, source: c.source }
}

struct Entry {
    a: Artist,
    list: usize,
    alive: bool,
}

/// R7.6: entry `e` leaves its list. If it was the list's first remaining name, the next
/// remaining name of that list takes `joiner`.
fn pass_joiner(entries: &mut [Entry], e: usize, joiner: Option<String>) {
    let Some(list) = entries.get(e).map(|x| x.list) else { return };
    let mut members = entries.iter_mut().enumerate().filter(|(_, x)| x.alive && x.list == list);
    if members.next().map(|(i, _)| i) != Some(e) {
        return;
    }
    if let Some((_, next)) = members.next() {
        next.a.joiner = joiner;
    }
}

/// R9.2: keep the first occurrence. A later lower/equal-precedence duplicate is removed (R7.6
/// joiner inheritance); a later higher-precedence duplicate (role upgrade) gives the kept
/// entry its role, joiner and source, and the kept entry moves into that occurrence's list.
pub(crate) fn dedup_artists(credits: Vec<Credit>) -> Vec<Artist> {
    let mut entries: Vec<Entry> = credits
        .into_iter()
        .map(|c| {
            let list = c.list;
            Entry { a: to_artist(c), list, alive: true }
        })
        .collect();
    let mut index: HashMap<String, usize> = HashMap::new();
    for i in 0..entries.len() {
        let Some(e) = entries.get(i) else { continue };
        // R9.2: producers are a separate credit class, deduped only among themselves.
        let class = if e.a.role == ArtistRole::Producer { "p" } else { "c" };
        let key = format!("{class}:{}", dedup_key(&e.a.name));
        let (e_role, e_joiner, e_source, e_list) =
            (e.a.role, e.a.joiner.clone(), e.a.source, e.list);
        let Some(&k) = index.get(&key) else {
            index.insert(key, i);
            continue;
        };
        let Some((kept_role, kept_joiner)) = entries.get(k).map(|x| (x.a.role, x.a.joiner.clone()))
        else {
            continue;
        };
        if precedence(e_role) > precedence(kept_role) {
            pass_joiner(&mut entries, k, kept_joiner);
            if let Some(kept) = entries.get_mut(k) {
                kept.a.role = e_role;
                kept.a.joiner = e_joiner;
                kept.a.source = e_source;
                kept.list = e_list;
            }
        } else {
            pass_joiner(&mut entries, i, e_joiner);
        }
        if let Some(e) = entries.get_mut(i) {
            e.alive = false;
        }
    }
    entries.into_iter().filter(|e| e.alive).map(|e| e.a).collect()
}

pub(crate) fn to_version(v: VersionDraft) -> RawVersion {
    RawVersion {
        vtype: v.vtype,
        raw: v.raw,
        artists: v.artists.into_iter().map(to_artist).collect(),
        modifiers: v.modifiers,
        descriptor: v.descriptor,
        year: v.year,
        unknown_artist: v.unknown_artist,
        delimiter: v.delimiter,
    }
}

/// R9.4: a version rendered with its delimiter.
pub(crate) fn render_version(raw: &str, delimiter: VersionDelimiter) -> String {
    match delimiter {
        VersionDelimiter::Paren => format!("({raw})"),
        VersionDelimiter::Square => format!("[{raw}]"),
        VersionDelimiter::Curly => format!("{{{raw}}}"),
        VersionDelimiter::Dash => format!("- {raw}"),
    }
}

pub(crate) fn full_title_of(title: &str, versions: &[RawVersion]) -> String {
    std::iter::once(title.to_string())
        .chain(versions.iter().map(|v| render_version(&v.raw, v.delimiter)))
        .filter(|s| !s.is_empty())
        .collect::<Vec<_>>()
        .join(" ")
}
