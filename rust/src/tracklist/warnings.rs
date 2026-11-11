//! R13.7 tracklist warnings and the `time-out-of-order` suggestion search.

use super::times::seconds_of;
use super::types::{
    EntryKind, Severity, Suggestion, TimeValue, TracklistEntry, TracklistWarning, WarningCode,
};

pub(crate) struct WarningInput<'a> {
    pub entries: &'a [TracklistEntry],
    /// R4.2 / R13.5 position number of each entry's track, if it has one.
    pub positions: &'a [Option<u32>],
    /// R13.2.4 per entry.
    pub invalid: &'a [bool],
    /// R13.7 line-unparsed per entry: a `track` entry whose track part has no separator (R13.3).
    pub unparsed: &'a [bool],
    pub audio_seconds: Option<f64>,
}

#[derive(Debug, Clone, Copy)]
struct Slot {
    /// Index of the digit in `raw`.
    at: usize,
    field: usize,
    place: i64,
    digit: i64,
}

/// Outcome of one line's digit search: no candidate, exactly one, or several.
pub(crate) enum Search {
    None,
    One(TimeValue),
    Many,
}

/// One digit change: the slot and its new digit.
type Change = (Slot, i64);

struct Digits<'a> {
    raw: &'a [char],
    frac: String,
    values: Vec<i64>,
    lo: Option<f64>,
    hi: Option<f64>,
}

impl Digits<'_> {
    /// The value with `changes` applied, when it is a valid time inside the bounds.
    fn value_with(&self, changes: &[Change]) -> Option<f64> {
        let mut v = self.values.clone();
        for (slot, d) in changes {
            if let Some(field) = v.get_mut(slot.field) {
                *field += (d - slot.digit) * slot.place;
            }
        }
        let mult: &[i64] = if v.len() == 3 { &[3600, 60, 1] } else { &[60, 1] };
        let mut whole: i64 = 0;
        for (f, value) in v.iter().enumerate() {
            if f > 0 && *value > 59 {
                return None;
            }
            whole += value * mult.get(f).copied().unwrap_or(0);
        }
        let t = seconds_of(u32::try_from(whole).ok()?, &self.frac);
        (self.lo.map_or(true, |lo| lo < t) && self.hi.map_or(true, |hi| t < hi)).then_some(t)
    }

    fn render(&self, changes: &[Change]) -> String {
        let mut out = self.raw.to_vec();
        for (slot, d) in changes {
            if let (Some(ch), Some(digit)) =
                (out.get_mut(slot.at), u32::try_from(*d).ok().and_then(|d| char::from_digit(d, 10)))
            {
                *ch = digit;
            }
        }
        out.into_iter().collect()
    }
}

/// R13.7 suggestion search on one raw time: change 1, then 2 digits (fraction digits excluded);
/// keep candidates that are valid times (R13.2.2: fields after the first at most 59) with
/// `lo < t` (when `lo` exists) and `t < hi` (when `hi` exists). Only the smallest number of
/// changes with any candidate counts. Bounded and arithmetic: at most 7 digits, 9·7 + 81·21
/// candidates.
pub(crate) fn search_time(raw: &str, lo: Option<f64>, hi: Option<f64>) -> Search {
    let raw: Vec<char> = raw.chars().collect();
    let frac_start = raw.iter().position(|c| *c == '.' || *c == ',').unwrap_or(raw.len());
    let frac: String = raw.iter().skip(frac_start + 1).collect();
    let mut values: Vec<i64> = vec![0];
    let mut slots: Vec<Slot> = Vec::new();
    for (i, ch) in raw.iter().enumerate().take(frac_start) {
        if *ch == ':' {
            values.push(0);
        } else if let Some(d) = ch.to_digit(10).filter(|_| ch.is_ascii_digit()) {
            let f = values.len() - 1;
            if let Some(v) = values.last_mut() {
                *v = *v * 10 + i64::from(d);
            }
            slots.push(Slot { at: i, field: f, place: 0, digit: i64::from(d) });
        }
    }
    // Place value of each digit inside its field.
    let mut place = 1;
    let mut field = usize::MAX;
    for slot in slots.iter_mut().rev() {
        if slot.field != field {
            field = slot.field;
            place = 1;
        }
        slot.place = place;
        place *= 10;
    }
    let digits = Digits { raw: &raw, frac, values, lo, hi };

    let mut found: Option<Vec<Change>> = None;
    let mut count = 0;
    for slot in &slots {
        for d in (0..=9).filter(|d| *d != slot.digit) {
            let change = vec![(*slot, d)];
            if digits.value_with(&change).is_some() {
                count += 1;
                found = Some(change);
            }
        }
    }
    if count == 0 {
        'pairs: for (x, a) in slots.iter().enumerate() {
            for b in slots.iter().skip(x + 1) {
                for da in (0..=9).filter(|d| *d != a.digit) {
                    for db in (0..=9).filter(|d| *d != b.digit) {
                        let change = vec![(*a, da), (*b, db)];
                        if digits.value_with(&change).is_some() {
                            count += 1;
                            found = Some(change);
                            if count >= 2 {
                                break 'pairs;
                            }
                        }
                    }
                }
            }
        }
    }
    match found {
        Some(change) if count == 1 => match digits.value_with(&change) {
            Some(seconds) => Search::One(TimeValue { raw: digits.render(&change), seconds }),
            None => Search::None,
        },
        Some(_) if count > 1 => Search::Many,
        _ => Search::None,
    }
}

/// A timed entry: its line number and time.
type Timed<'a> = (usize, &'a TimeValue);

/// R13.7 suggestion for the out-of-order timed entry `k` (index into `times`): search its own
/// time between its neighbours first; only when that finds no candidate at all, search the
/// previous timed entry's time between the one before it and entry `k`.
pub(crate) fn suggest_fix(times: &[Timed<'_>], k: usize) -> Option<Suggestion> {
    let (cur_line, cur) = *times.get(k)?;
    let (prev_line, prev) = *times.get(k.checked_sub(1)?)?;
    let next = times.get(k + 1).map(|(_, t)| t.seconds);
    match search_time(&cur.raw, Some(prev.seconds), next) {
        Search::One(t) => {
            return Some(Suggestion { line: cur_line, raw: t.raw, seconds: t.seconds })
        }
        Search::Many => return None,
        Search::None => {}
    }
    let before = k.checked_sub(2).and_then(|i| times.get(i)).map(|(_, t)| t.seconds);
    match search_time(&prev.raw, before, Some(cur.seconds)) {
        Search::One(t) => Some(Suggestion { line: prev_line, raw: t.raw, seconds: t.seconds }),
        _ => None,
    }
}

/// Table order of R13.7; warnings of one line are emitted in this order.
fn rank(code: WarningCode) -> usize {
    WarningCode::ALL.iter().position(|c| *c == code).unwrap_or(usize::MAX)
}

pub(crate) fn tracklist_warnings(input: &WarningInput<'_>) -> Vec<TracklistWarning> {
    let entries = input.entries;
    let mut per_line: Vec<Vec<TracklistWarning>> = entries.iter().map(|_| Vec::new()).collect();
    let mut add = |i: usize, code: WarningCode, suggestion: Option<Suggestion>| {
        let (Some(e), Some(list)) = (entries.get(i), per_line.get_mut(i)) else { return };
        let severity =
            if code == WarningCode::NoStartAtZero { Severity::Info } else { Severity::Warning };
        list.push(TracklistWarning { code, line: e.line, severity, suggestion });
    };

    let timed: Vec<(usize, Timed<'_>)> = entries
        .iter()
        .enumerate()
        .filter_map(|(i, e)| e.time.as_ref().map(|t| (i, (e.line, t))))
        .collect();
    let flag = |list: &[bool], i: usize| list.get(i).copied().unwrap_or(false);

    for (i, e) in entries.iter().enumerate() {
        // R13.13.3: an untimed layered line plays over the previous track, at its time.
        if e.kind == EntryKind::Track && e.time.is_none() && !e.layered && !timed.is_empty() {
            add(i, WarningCode::TimeMissing, None);
        }
        if flag(input.invalid, i) {
            add(i, WarningCode::InvalidTime, None);
        }
        if flag(input.unparsed, i) {
            add(i, WarningCode::LineUnparsed, None);
        }
    }

    let times: Vec<Timed<'_>> = timed.iter().map(|(_, t)| *t).collect();
    let mut prev: Option<f64> = None;
    for (k, (i, (_, t))) in timed.iter().enumerate() {
        match prev {
            None => {
                if t.seconds != 0.0 {
                    add(*i, WarningCode::NoStartAtZero, None);
                }
            }
            Some(p) => {
                if t.seconds < p {
                    add(*i, WarningCode::TimeOutOfOrder, suggest_fix(&times, k));
                } else if t.seconds == p {
                    add(*i, WarningCode::TimeDuplicate, None);
                }
            }
        }
        if input.audio_seconds.is_some_and(|audio| t.seconds > audio) {
            add(*i, WarningCode::TimeAfterAudioEnd, None);
        }
        prev = Some(t.seconds);
    }

    let mut prev_pos: Option<u32> = None;
    for (i, e) in entries.iter().enumerate() {
        let pos = if e.kind == EntryKind::Track {
            input.positions.get(i).copied().flatten()
        } else {
            None
        };
        let Some(pos) = pos else { continue };
        if let Some(p) = prev_pos {
            if pos == p {
                add(i, WarningCode::PositionDuplicate, None);
            } else if p.checked_add(1) != Some(pos) {
                add(i, WarningCode::PositionGap, None);
            }
        }
        prev_pos = Some(pos);
    }

    per_line
        .into_iter()
        .flat_map(|mut ws| {
            ws.sort_by_key(|w| rank(w.code)); // stable
            ws
        })
        .collect()
}
