//! R9: assembly and dedup.

use std::collections::{BTreeSet, HashMap};

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

/// The remaining names of each credit list, as ascending entry indexes, so "the list's first
/// remaining name" and "the next one" are lookups and dedup stays near-linear on inputs with
/// thousands of duplicate credits.
struct Lists(HashMap<usize, BTreeSet<usize>>);

impl Lists {
    fn new(entries: &[Entry]) -> Lists {
        let mut lists: HashMap<usize, BTreeSet<usize>> = HashMap::new();
        for (i, e) in entries.iter().enumerate() {
            lists.entry(e.list).or_default().insert(i);
        }
        Lists(lists)
    }

    fn remove(&mut self, list: usize, e: usize) {
        if let Some(members) = self.0.get_mut(&list) {
            members.remove(&e);
        }
    }

    fn insert(&mut self, list: usize, e: usize) {
        self.0.entry(list).or_default().insert(e);
    }

    /// The entry that follows `e` in `list`, if `e` is the list's first remaining name.
    fn heir(&self, list: usize, e: usize) -> Option<usize> {
        let members = self.0.get(&list)?;
        if members.first() != Some(&e) {
            return None;
        }
        members.range(e + 1..).next().copied()
    }
}

/// R7.6: entry `e` leaves its list. If it was the list's first remaining name, the next
/// remaining name of that list takes `joiner`.
fn pass_joiner(entries: &mut [Entry], lists: &Lists, e: usize, joiner: Option<String>) {
    let Some(list) = entries.get(e).map(|x| x.list) else { return };
    if let Some(next) = lists.heir(list, e).and_then(|i| entries.get_mut(i)) {
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
    let mut lists = Lists::new(&entries);
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
        let Some((kept_role, kept_joiner, kept_list)) =
            entries.get(k).map(|x| (x.a.role, x.a.joiner.clone(), x.list))
        else {
            continue;
        };
        if precedence(e_role) > precedence(kept_role) {
            pass_joiner(&mut entries, &lists, k, kept_joiner);
            if let Some(kept) = entries.get_mut(k) {
                kept.a.role = e_role;
                kept.a.joiner = e_joiner;
                kept.a.source = e_source;
                kept.list = e_list;
            }
            lists.remove(kept_list, k);
            lists.insert(e_list, k);
        } else {
            pass_joiner(&mut entries, &lists, i, e_joiner);
        }
        if let Some(e) = entries.get_mut(i) {
            e.alive = false;
        }
        lists.remove(e_list, i);
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

#[cfg(test)]
#[allow(clippy::unwrap_used, clippy::indexing_slicing)]
mod tests {
    use super::*;
    use crate::types::ArtistSource;

    /// R9.2 / R7.6 written the plain way: every "first remaining name of the list" is a scan.
    /// `dedup_artists` must give exactly this.
    fn reference(credits: Vec<Credit>) -> Vec<Artist> {
        fn pass(entries: &mut [Entry], e: usize, joiner: Option<String>) {
            let list = entries[e].list;
            let mut members =
                entries.iter_mut().enumerate().filter(|(_, x)| x.alive && x.list == list);
            if members.next().map(|(i, _)| i) != Some(e) {
                return;
            }
            if let Some((_, next)) = members.next() {
                next.a.joiner = joiner;
            }
        }
        let mut entries: Vec<Entry> = credits
            .into_iter()
            .map(|c| {
                let list = c.list;
                Entry { a: to_artist(c), list, alive: true }
            })
            .collect();
        let mut index: HashMap<String, usize> = HashMap::new();
        for i in 0..entries.len() {
            let e = &entries[i];
            let class = if e.a.role == ArtistRole::Producer { "p" } else { "c" };
            let key = format!("{class}:{}", dedup_key(&e.a.name));
            let (role, joiner, source, list) = (e.a.role, e.a.joiner.clone(), e.a.source, e.list);
            let Some(&k) = index.get(&key) else {
                index.insert(key, i);
                continue;
            };
            if precedence(role) > precedence(entries[k].a.role) {
                let kept_joiner = entries[k].a.joiner.clone();
                pass(&mut entries, k, kept_joiner);
                let kept = &mut entries[k];
                kept.a.role = role;
                kept.a.joiner = joiner;
                kept.a.source = source;
                kept.list = list;
            } else {
                pass(&mut entries, i, joiner);
            }
            entries[i].alive = false;
        }
        entries.into_iter().filter(|e| e.alive).map(|e| e.a).collect()
    }

    #[test]
    fn dedup_matches_the_plain_algorithm() {
        let mut state = 0x2545_f491_u32;
        let mut below = |n: u32| {
            // xorshift32
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            (state % n) as usize
        };
        let roles =
            [ArtistRole::Primary, ArtistRole::Featured, ArtistRole::Producer, ArtistRole::Remixer];
        let sources = [ArtistSource::Artist, ArtistSource::Title, ArtistSource::Version];
        let names = ["a", "A", "b", "c", "é", "d"];
        for _ in 0..20_000 {
            let lists = 1 + below(4);
            let credits: Vec<Credit> = (0..below(14))
                .map(|i| Credit {
                    name: names[below(names.len() as u32)].to_string(),
                    role: roles[below(4)],
                    joiner: [None, Some("&".to_string()), Some(format!("j{i}"))][below(3)].clone(),
                    source: sources[below(3)],
                    pos: i as i64,
                    list: below(lists as u32),
                })
                .collect();
            assert_eq!(dedup_artists(credits.clone()), reference(credits.clone()), "{credits:?}");
        }
    }
}
