//! R3: top-level group scanner and skeleton.
//!
//! A `Skel` is a skeleton (code points) plus, for every code point, its absolute offset in the
//! normalized input. Offsets are in UTF-16 units, as in the JS reference, so that every
//! "order of appearance" comparison gives the same answer. Groups are looked up by the offset of
//! their placeholder, so any slice of a skeleton still knows which groups it contains.

use std::collections::{HashMap, HashSet};
use std::rc::Rc;

use crate::types::VersionDelimiter;
use crate::words::{at, before, collapse_chars, is_whitespace, string, sub};

/// R3.4 placeholder code point.
pub(crate) const PH: char = '\u{e000}';

#[derive(Debug, Clone)]
pub(crate) struct Group {
    pub open: VersionDelimiter,
    /// R3.5: inner text, trimmed and whitespace-collapsed.
    pub inner: String,
    /// The group exactly as written, brackets included.
    pub raw: String,
    /// Absolute offset of the opener.
    pub pos: i64,
}

pub(crate) type Groups = Rc<HashMap<i64, Group>>;

#[derive(Debug, Clone)]
pub(crate) struct Skel {
    pub text: Vec<char>,
    pub pos: Vec<i64>,
    pub groups: Groups,
}

fn closer_of(ch: char) -> Option<char> {
    match ch {
        '(' => Some(')'),
        '[' => Some(']'),
        '{' => Some('}'),
        _ => None,
    }
}

fn delimiter_of(ch: Option<char>) -> VersionDelimiter {
    match ch {
        Some('[') => VersionDelimiter::Square,
        Some('{') => VersionDelimiter::Curly,
        _ => VersionDelimiter::Paren,
    }
}

/// R3.1–R3.5: scan `s` (whose first unit sits at absolute offset `base`). The second value is
/// "unbalanced" (R3.3).
pub(crate) fn scan(s: &[char], base: i64) -> (Skel, bool) {
    // Absolute offset of every code point, plus one entry for the end of the string.
    let mut offsets: Vec<i64> = Vec::with_capacity(s.len() + 1);
    let mut off = base;
    for ch in s {
        offsets.push(off);
        off += ch.len_utf16() as i64;
    }
    offsets.push(off);
    let offset_at = |i: usize| offsets.get(i).copied().unwrap_or(off);

    let mut text: Vec<char> = Vec::with_capacity(s.len());
    let mut pos: Vec<i64> = Vec::with_capacity(s.len());
    let mut groups: HashMap<i64, Group> = HashMap::new();
    let mut stack: Vec<char> = Vec::new();
    let mut group_start = 0usize;
    for (i, &ch) in s.iter().enumerate() {
        if stack.is_empty() {
            if let Some(closer) = closer_of(ch) {
                stack.push(closer);
                group_start = i;
            } else {
                text.push(ch);
                pos.push(offset_at(i));
            }
            continue;
        }
        if let Some(closer) = closer_of(ch) {
            stack.push(closer);
        } else if stack.last() == Some(&ch) {
            stack.pop();
            if stack.is_empty() {
                add_group(s, group_start, i + 1, true, &offset_at, &mut groups);
                text.push(PH);
                pos.push(offset_at(group_start));
            }
        }
        // R3.2: a non-matching closer is literal text inside the group.
    }
    let unbalanced = !stack.is_empty();
    if unbalanced {
        // R3.3: the group runs to the end of the string.
        add_group(s, group_start, s.len(), false, &offset_at, &mut groups);
        text.push(PH);
        pos.push(offset_at(group_start));
    }
    (Skel { text, pos, groups: Rc::new(groups) }, unbalanced)
}

fn add_group(
    s: &[char],
    start: usize,
    end: usize,
    closed: bool,
    offset_at: &dyn Fn(usize) -> i64,
    groups: &mut HashMap<i64, Group>,
) {
    let inner = sub(s, start + 1, if closed { end.saturating_sub(1) } else { end });
    let pos = offset_at(start);
    groups.insert(
        pos,
        Group {
            open: delimiter_of(at(s, start)),
            inner: collapse_chars(inner),
            raw: string(sub(s, start, end)),
            pos,
        },
    );
}

// ---------------------------------------------------------------------------
// Skeleton operations

impl Skel {
    pub fn len(&self) -> usize {
        self.text.len()
    }

    pub fn is_empty(&self) -> bool {
        self.text.is_empty()
    }
}

pub(crate) fn slice_skel(s: &Skel, start: usize, end: usize) -> Skel {
    let a = start.min(s.text.len());
    let b = end.min(s.text.len()).max(a);
    Skel {
        text: sub(&s.text, a, b).to_vec(),
        pos: s.pos.get(a..b.min(s.pos.len())).unwrap_or(&[]).to_vec(),
        groups: Rc::clone(&s.groups),
    }
}

/// `slice_skel(s, start)` to the end.
pub(crate) fn slice_from(s: &Skel, start: usize) -> Skel {
    slice_skel(s, start, s.text.len())
}

pub(crate) fn trim_skel(s: &Skel) -> Skel {
    let mut a = 0;
    let mut b = s.text.len();
    while a < b && is_whitespace(at(&s.text, a)) {
        a += 1;
    }
    while b > a && is_whitespace(before(&s.text, b)) {
        b -= 1;
    }
    slice_skel(s, a, b)
}

pub(crate) fn concat_skel(a: &Skel, b: &Skel) -> Skel {
    let mut text = a.text.clone();
    text.extend_from_slice(&b.text);
    let mut pos = a.pos.clone();
    pos.extend_from_slice(&b.pos);
    Skel { text, pos, groups: Rc::clone(&a.groups) }
}

/// The group behind the placeholder at `index`, if it is one.
pub(crate) fn group_at(s: &Skel, index: usize) -> Option<&Group> {
    if at(&s.text, index) != Some(PH) {
        return None;
    }
    s.groups.get(s.pos.get(index)?)
}

/// Groups in the skeleton, left to right, with their unit index.
pub(crate) fn groups_of(s: &Skel) -> Vec<(usize, &Group)> {
    (0..s.text.len()).filter_map(|i| group_at(s, i).map(|g| (i, g))).collect()
}

pub(crate) fn has_group(s: &Skel) -> bool {
    s.text.contains(&PH)
}

/// Substitute groups back (R3.4 reassembly). Whitespace is not touched.
pub(crate) fn render_skel(s: &Skel) -> String {
    let mut out = String::new();
    for (i, &ch) in s.text.iter().enumerate() {
        match group_at(s, i) {
            Some(g) => out.push_str(&g.raw),
            None => out.push(ch),
        }
    }
    out
}

/// Absolute offset of the first unit (or `fallback` for an empty skeleton).
pub(crate) fn start_pos(s: &Skel, fallback: i64) -> i64 {
    s.pos.first().copied().unwrap_or(fallback)
}

/// Remove the placeholders of the given groups.
pub(crate) fn remove_groups(s: &Skel, remove: &HashSet<i64>) -> Skel {
    if remove.is_empty() {
        return s.clone();
    }
    let mut text = Vec::with_capacity(s.text.len());
    let mut pos = Vec::with_capacity(s.pos.len());
    for (&ch, &p) in s.text.iter().zip(s.pos.iter()) {
        if ch == PH && remove.contains(&p) {
            continue;
        }
        text.push(ch);
        pos.push(p);
    }
    Skel { text, pos, groups: Rc::clone(&s.groups) }
}

/// R6.1: `-` with a space immediately on both sides.
pub(crate) fn is_spaced_dash_at(text: &[char], i: usize) -> bool {
    at(text, i) == Some('-') && before(text, i) == Some(' ') && at(text, i + 1) == Some(' ')
}

pub(crate) fn has_spaced_dash(text: &[char]) -> bool {
    (1..text.len().saturating_sub(1)).any(|i| is_spaced_dash_at(text, i))
}

#[derive(Debug, Clone)]
pub(crate) struct Segment {
    pub skel: Skel,
    /// Trimmed range inside the parent skeleton.
    pub start: usize,
    pub end: usize,
}

fn trimmed_segment(s: &Skel, start: usize, end: usize) -> Segment {
    let mut a = start;
    let mut b = start.max(end);
    while a < b && is_whitespace(at(&s.text, a)) {
        a += 1;
    }
    while b > a && is_whitespace(before(&s.text, b)) {
        b -= 1;
    }
    Segment { skel: slice_skel(s, a, b), start: a, end: b }
}

/// R6.1 / R8.2: split a skeleton at every spaced dash into trimmed segments. With
/// `allow_leading`, a string starting with `- ` yields an empty first segment (R6.3).
pub(crate) fn split_at_spaced_dashes(s: &Skel, allow_leading: bool) -> Vec<Segment> {
    let mut segments = Vec::new();
    let t = &s.text;
    let mut start = 0usize;
    if allow_leading && at(t, 0) == Some('-') && at(t, 1) == Some(' ') {
        segments.push(Segment { skel: slice_skel(s, 0, 0), start: 0, end: 0 });
        start = 2;
    }
    let mut i = start.max(1);
    while i + 1 < t.len() {
        if is_spaced_dash_at(t, i) {
            segments.push(trimmed_segment(s, start, i - 1));
            start = i + 2;
        }
        i += 1;
    }
    segments.push(trimmed_segment(s, start, t.len()));
    segments
}
