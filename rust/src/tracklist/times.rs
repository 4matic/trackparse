//! R13.2: the tracklist time scanner (separate from R4.1) and R13.9.2 time formatting.

use crate::digits::{digit_run, digits_value};
use crate::words::{at, chars, is_digit, is_whitespace, string, sub, tail};

use super::emphasis::{emphasis_run, without_markers};
use super::types::{Parts, TimeValue};

/// JS `Number.MAX_SAFE_INTEGER`.
const MAX_SAFE_INTEGER: f64 = 9_007_199_254_740_991.0;

/// The written shape of a time, kept by edits (R13.9.2).
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct TimeStyle {
    pub hours: bool,
    /// Digits written for the hours (minimum width), 0 without hours.
    pub hour_width: usize,
    /// Digits written for the minutes (minimum width).
    pub minute_width: usize,
    /// R13.9.2: two-part time already at 60 minutes or more; it stays two-part when it grows.
    pub long_minutes: bool,
    /// `.` or `,` when the time has a fraction.
    pub frac_sep: Option<char>,
    pub frac_digits: usize,
}

#[derive(Debug, Clone, PartialEq)]
pub(crate) struct TimeScan {
    pub parts: Parts,
    pub time: Option<TimeValue>,
    pub style: Option<TimeStyle>,
    /// R13.2.4: a `digits:digits` shape after lead that fails R13.2.2.
    pub invalid: bool,
    /// R13.12: `rest` without its paired emphasis markers (the line text, before R13.13 / R13.5).
    pub text: String,
}

/// R13.2.3 separators after the time.
fn is_gap_separator(ch: Option<char>) -> bool {
    matches!(ch, Some('-' | '–' | '—' | '|' | '.' | ':'))
}

fn skip_whitespace(s: &[char], from: usize) -> usize {
    let mut i = from;
    while is_whitespace(at(s, i)) {
        i += 1;
    }
    i
}

/// R13.2.1 bullets (before a time, or alone on a line without one).
fn is_bullet(ch: Option<char>) -> bool {
    matches!(ch, Some('-' | '–' | '—' | '*' | '•'))
}

/// R13.1.3 / R13.2.1: a line without a time. `lead` holds an optional bullet: R0.2 whitespace,
/// one of `-` `–` `—` `*` `•`, then at least one R0.2 whitespace (all of it); `rest` is the
/// remainder.
fn untimed(raw: &[char], invalid: bool) -> TimeScan {
    let start = skip_whitespace(raw, 0);
    let mut lead = 0;
    if is_bullet(at(raw, start)) && is_whitespace(at(raw, start + 1)) {
        lead = skip_whitespace(raw, start + 1);
    }
    TimeScan {
        parts: Parts {
            lead: string(sub(raw, 0, lead)),
            time: String::new(),
            gap: String::new(),
            rest: string(tail(raw, lead)),
        },
        time: None,
        style: None,
        invalid,
        text: without_markers(raw, lead, None),
    }
}

/// R13.2.3: past a closing run for an opening run of `open` (same character, 1 or 2 long).
fn closing_run(raw: &[char], from: usize, open: Option<char>) -> usize {
    if open.is_some() && at(raw, from) == open {
        from + emphasis_run(raw, from)
    } else {
        from
    }
}

/// `int.frac` parsed as one decimal literal, so every port gets the same double.
pub(crate) fn seconds_of(whole: u32, frac: &str) -> f64 {
    if frac.is_empty() {
        f64::from(whole)
    } else {
        format!("{whole}.{frac}").parse().unwrap_or(f64::from(whole))
    }
}

struct Shape<'a> {
    /// The digit runs between the colons (2 or more).
    parts: Vec<&'a [char]>,
    end: usize,
}

/// R13.2.4: the maximal `digits(:digits)+` run at `from`, or `None`.
fn shape_at(s: &[char], from: usize) -> Option<Shape<'_>> {
    let na = digit_run(s, from);
    if na == 0 || at(s, from + na) != Some(':') || !is_digit(at(s, from + na + 1)) {
        return None;
    }
    let mut parts = vec![sub(s, from, from + na)];
    let mut end = from + na;
    while at(s, end) == Some(':') && is_digit(at(s, end + 1)) {
        let n = digit_run(s, end + 1);
        parts.push(sub(s, end + 1, end + 1 + n));
        end += 1 + n;
    }
    Some(Shape { parts, end })
}

/// R13.2.2 validity: whole seconds of `A:B` / `A:B:C` (no other part count), or `None`.
pub(crate) fn shape_seconds(parts: &[&[char]]) -> Option<u32> {
    match parts {
        [a, b, c] => {
            if a.is_empty() || a.len() > 2 || b.len() != 2 || c.len() != 2 {
                return None;
            }
            let (m, sec) = (digits_value(b), digits_value(c));
            if m > 59 || sec > 59 {
                return None;
            }
            Some(digits_value(a) * 3600 + m * 60 + sec)
        }
        [a, b] => {
            if a.is_empty() || a.len() > 3 || b.len() != 2 {
                return None;
            }
            let sec = digits_value(b);
            if sec > 59 {
                return None;
            }
            Some(digits_value(a) * 60 + sec)
        }
        _ => None,
    }
}

fn starts_with(s: &[char], from: usize, word: &str) -> bool {
    let n = word.chars().count();
    sub(s, from, from + n).iter().copied().eq(word.chars())
}

/// R13.2: lead, time and gap at the start of `raw`; the rest is everything after.
pub(crate) fn scan_time(raw: &str) -> TimeScan {
    let raw = &chars(raw);
    // R13.2.1 lead: whitespace, an optional bullet, an optional emphasis run, an optional `[` /
    // `(`, then (after a bracket) another optional emphasis run. A run counts only when the next
    // character is a bracket (first run) or a digit.
    let mut i = skip_whitespace(raw, 0);
    if is_bullet(at(raw, i)) && is_whitespace(at(raw, i + 1)) {
        i = skip_whitespace(raw, i + 1);
    }
    let mut outer_open: Option<char> = None;
    let mut n = emphasis_run(raw, i);
    if n > 0 && (matches!(at(raw, i + n), Some('[' | '(')) || is_digit(at(raw, i + n))) {
        outer_open = at(raw, i);
        i += n;
    }
    let closer = match at(raw, i) {
        Some('[') => Some(']'),
        Some('(') => Some(')'),
        _ => None,
    };
    let mut inner_open: Option<char> = None;
    if closer.is_some() {
        i += 1;
        n = emphasis_run(raw, i);
        if n > 0 && is_digit(at(raw, i + n)) {
            inner_open = at(raw, i);
            i += n;
        }
    }
    let lead_end = i;

    // R13.2.2 time
    let Some(shape) = shape_at(raw, i) else { return untimed(raw, false) };
    let Some(whole) = shape_seconds(&shape.parts) else { return untimed(raw, true) }; // R13.2.4
    let mut j = shape.end;
    let mut frac_sep: Option<char> = None;
    let mut frac = String::new();
    if let Some(sep @ ('.' | ',')) = at(raw, j) {
        let n = digit_run(raw, j + 1);
        if (1..=3).contains(&n) {
            frac_sep = Some(sep);
            frac = string(sub(raw, j + 1, j + 1 + n));
            j += 1 + n;
        }
    }
    let time_end = j;

    // R13.2.3 gap: closing runs for the lead's runs (same character), around the closing bracket.
    let follows_time =
        |k: usize| k >= raw.len() || is_whitespace(at(raw, k)) || is_gap_separator(at(raw, k));
    // R13.12.4: a lead run the gap leaves open may pair with a run at the end of the line.
    let mut open: Option<char> = None;
    if let Some(closer) = closer {
        let mut k = closing_run(raw, j, inner_open);
        if k == j {
            open = inner_open;
        }
        j = k;
        if at(raw, j) != Some(closer) {
            return untimed(raw, false);
        }
        j += 1;
        k = closing_run(raw, j, outer_open);
        if k > j && follows_time(k) {
            j = k;
        } else {
            open = outer_open.or(open);
        }
    } else {
        let k = closing_run(raw, j, outer_open);
        if k == j {
            open = outer_open;
        }
        j = k;
        if !follows_time(j) {
            return untimed(raw, false);
        }
    }
    j = skip_whitespace(raw, j);
    if is_gap_separator(at(raw, j)) {
        j = skip_whitespace(raw, j + 1);
    }
    if starts_with(raw, j, "==>") {
        j = skip_whitespace(raw, j + 3);
    } else if starts_with(raw, j, "=>") {
        j = skip_whitespace(raw, j + 2);
    }

    let time_raw = string(sub(raw, lead_end, time_end));
    let hours = shape.parts.len() == 3;
    let first = shape.parts.first().copied().unwrap_or(&[]);
    let second = shape.parts.get(1).copied().unwrap_or(&[]);
    // R13.9.2 long-minute style: two parts with 60 minutes or more.
    let long_minutes = !hours && digits_value(first) >= 60;
    TimeScan {
        parts: Parts {
            lead: string(sub(raw, 0, lead_end)),
            time: time_raw.clone(),
            gap: string(sub(raw, time_end, j)),
            rest: string(tail(raw, j)),
        },
        time: Some(TimeValue { raw: time_raw, seconds: seconds_of(whole, &frac) }),
        style: Some(TimeStyle {
            hours,
            hour_width: if hours { first.len() } else { 0 },
            // R13.9.2: a long-minute time's minimum minute width is min(width, 2), so it never
            // keeps 3 minute digits once it is short again (`125:30` − 7000 s → `08:50`).
            minute_width: if hours {
                second.len()
            } else if long_minutes {
                first.len().min(2)
            } else {
                first.len()
            },
            long_minutes,
            frac_sep,
            frac_digits: frac.chars().count(),
        }),
        invalid: false,
        text: without_markers(raw, j, open),
    }
}

fn pad(n: u64, width: usize) -> String {
    format!("{n:0width$}")
}

pub(crate) struct FormattedTime {
    pub raw: String,
    pub upgraded: bool,
}

/// R13.9.2: write `seconds` (finite, ≥ 0) in `style`. Values are rounded half up to the fraction
/// precision (`floor(v·10^d + 0.5)`). A value of an hour or more without hours is upgraded to
/// `HH:MM:SS` (`H:MM:SS` when `one_digit_hours`), except in long-minute style. Returns `None`
/// when the value cannot be written as a valid R13.2.2 time (100 hours or more, more than 999
/// long-style minutes, R13.9.2).
pub(crate) fn format_time(
    seconds: f64,
    style: &TimeStyle,
    one_digit_hours: bool,
) -> Option<FormattedTime> {
    let scale: f64 = match style.frac_digits {
        0 => 1.0,
        1 => 10.0,
        2 => 100.0,
        _ => 1000.0,
    };
    // The same double arithmetic as the JS reference; integers only after the range check.
    let units = (seconds * scale + 0.5).floor();
    if !units.is_finite() || units.abs() > MAX_SAFE_INTEGER || units < 0.0 {
        return None;
    }
    let whole_f = (units / scale).floor();
    let frac_units = (units - whole_f * scale) as u64;
    // The written time must stay valid under R13.2.2 (hours are 1–2 digits).
    if whole_f >= 100.0 * 3600.0 {
        return None;
    }
    let whole = whole_f as u64;
    let frac = match style.frac_sep {
        Some(sep) if style.frac_digits > 0 => {
            format!("{sep}{}", pad(frac_units, style.frac_digits))
        }
        _ => String::new(),
    };
    let sec = whole % 60;
    if style.hours {
        let h = whole / 3600;
        let m = (whole % 3600) / 60;
        return Some(FormattedTime {
            raw: format!(
                "{}:{}:{}{frac}",
                pad(h, style.hour_width),
                pad(m, style.minute_width),
                pad(sec, 2)
            ),
            upgraded: false,
        });
    }
    if style.long_minutes {
        let m = whole / 60;
        if m > 999 {
            return None; // R13.2.2: two-part minutes are 1–3 digits
        }
        return Some(FormattedTime {
            raw: format!("{}:{}{frac}", pad(m, style.minute_width), pad(sec, 2)),
            upgraded: false,
        });
    }
    if whole >= 3600 {
        let h = whole / 3600;
        let m = (whole % 3600) / 60;
        return Some(FormattedTime {
            raw: format!(
                "{}:{}:{}{frac}",
                pad(h, if one_digit_hours { 1 } else { 2 }),
                pad(m, 2),
                pad(sec, 2)
            ),
            upgraded: true,
        });
    }
    let m = whole / 60;
    Some(FormattedTime {
        raw: format!("{}:{}{frac}", pad(m, style.minute_width), pad(sec, 2)),
        upgraded: false,
    })
}
