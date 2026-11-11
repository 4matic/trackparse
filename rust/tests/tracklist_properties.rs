//! SPEC R13.11 tracklist invariants, checked on generated texts.
//!
//! The generator is a small deterministic PRNG over fragments that exercise the line splitter, the
//! time scanner, emphasis and the line kinds (the same fragments as the JS property tests), so a
//! failure is reproducible from its case number.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic, clippy::indexing_slicing)]

use std::time::{Duration, Instant};

use trackparse::json::Json;
use trackparse::tracklist::{
    apply_edits, parse_tracklist, Edit, EditResult, EditWarningCode, EntryKind, Eol, Tracklist,
};
use trackparse::{parse_with, wire, ModeOption, ParseOptions};

/// mulberry32.
struct Rng(u32);

impl Rng {
    fn next(&mut self) -> u32 {
        self.0 = self.0.wrapping_add(0x6d2b_79f5);
        let mut t = self.0;
        t = (t ^ (t >> 15)).wrapping_mul(t | 1);
        t ^= t.wrapping_add((t ^ (t >> 7)).wrapping_mul(t | 61));
        t ^ (t >> 14)
    }

    fn below(&mut self, n: usize) -> usize {
        self.next() as usize % n
    }

    fn pick<'a>(&mut self, items: &[&'a str]) -> &'a str {
        items[self.below(items.len())]
    }

    fn chance(&mut self, percent: usize) -> bool {
        self.below(100) < percent
    }
}

const FRAGMENTS: &[&str] = &[
    "\n",
    "\r\n",
    "\r",
    "\n\r",
    " ",
    "\u{2028}",
    "\u{85}",
    "\u{a0}",
    "  ",
    "\t",
    "\u{3000}",
    "00:00",
    "0:00 ",
    "04:37 ",
    "1:02:03 ",
    "01:02:03.5 ",
    "125:30 ",
    "12:34,250",
    "1:75 ",
    "0:61:00 ",
    "1:2",
    "[",
    "]",
    "(",
    ")",
    "[00:10] ",
    "(1:00)",
    " - ",
    " – ",
    "-",
    "|",
    ".",
    ":",
    "=> ",
    "==>",
    "1 ",
    "2 ",
    "01. ",
    "3) ",
    "Artist",
    "Title",
    "Noisia - Stigma",
    "Intro",
    "interview",
    "Tracklist:",
    "Part 2",
    "https://x.y",
    "www.",
    "_-_",
    "Ab-Cd",
    "ID",
    "(VIP)",
    "**",
    "*",
    "__",
    "_",
    "***",
    "* ",
    "- ",
    "• ",
    "**[",
    "]**",
    "w/ ",
    "W/",
    "\u{200b}",
    "𝄞",
    "🎵",
    "é",
    "Кино",
    "a",
    "7",
    "\u{0}",
    "\"",
    "\\",
];

fn noisy_text(rng: &mut Rng) -> String {
    let n = rng.below(41);
    (0..n).map(|_| rng.pick(FRAGMENTS)).collect()
}

const STYLES: &[&str] = &["M:SS", "MM:SS", "H:MM:SS", "HH:MM:SS", "MM:SS.d", "M:SS,ddd"];

fn write_time(style: &str, seconds: u32, frac: u32) -> String {
    let (h, m, s) = (seconds / 3600, (seconds % 3600) / 60, seconds % 60);
    match style {
        "M:SS" => format!("{}:{s:02}", seconds / 60),
        "MM:SS" => format!("{:02}:{s:02}", seconds / 60),
        "H:MM:SS" => format!("{h}:{m:02}:{s:02}"),
        "HH:MM:SS" => format!("{h:02}:{m:02}:{s:02}"),
        "MM:SS.d" => format!("{:02}:{s:02}.{}", seconds / 60, frac % 10),
        _ => format!("{}:{s:02},{frac:03}", seconds / 60),
    }
}

const RESTS: &[&str] = &[
    "Noisia - Stigma",
    "01. Phace - Plastic Acid",
    "2 Unlimited - No Limit",
    "Intro",
    "Paperclip interview",
    "Mefjus - Suicide Bassline (VIP)",
    "",
    "ID - ID",
];

fn list_line(rng: &mut Rng) -> String {
    if rng.below(7) == 0 {
        return rng.pick(&["", "Tracklist:", "a note", "https://example.com"]).to_string();
    }
    let time = write_time(rng.pick(STYLES), rng.below(3600) as u32, rng.below(1000) as u32);
    let gap = rng.pick(&[" ", "  ", " - ", " | ", " => "]);
    let rest = rng.pick(RESTS);
    match rng.pick(&["", " ", "["]) {
        "[" => format!("[{time}]{gap}{rest}"),
        lead => format!("{lead}{time}{gap}{rest}"),
    }
}

/// A realistic timed list for the edit properties.
fn timed_list(rng: &mut Rng) -> String {
    let n = 1 + rng.below(12);
    let eol = rng.pick(&["\n", "\r\n"]);
    let lines: Vec<String> = (0..n).map(|_| list_line(rng)).collect();
    lines.join(eol) + if rng.chance(50) { eol } else { "" }
}

fn any_text(rng: &mut Rng) -> String {
    if rng.chance(30) {
        timed_list(rng)
    } else {
        noisy_text(rng)
    }
}

fn raw_lines(list: &Tracklist) -> Vec<String> {
    list.entries.iter().map(|e| format!("{}{}", e.raw, e.eol)).collect()
}

fn raws(text: &str) -> Vec<String> {
    parse_tracklist(text).entries.into_iter().map(|e| e.raw).collect()
}

fn warned(result: &EditResult, codes: &[EditWarningCode]) -> bool {
    result.warnings.iter().any(|w| codes.contains(&w.code))
}

fn shift(seconds: f64, from: Option<usize>) -> Edit {
    Edit::Shift { seconds, lines: None, from }
}

#[test]
fn lossless_and_parts_join_to_raw() {
    let mut rng = Rng(1);
    for case in 0..3000 {
        let text = any_text(&mut rng);
        let list = parse_tracklist(&text);
        assert_eq!(raw_lines(&list).concat(), text, "case {case}: {text:?}");
        for e in &list.entries {
            let p = &e.parts;
            assert_eq!(format!("{}{}{}{}", p.lead, p.time, p.gap, p.rest), e.raw, "case {case}");
            // R13.1.3: without a time, `lead` holds at most a bullet; `time` and `gap` are empty.
            assert_eq!(e.time.is_none(), p.time.is_empty(), "case {case}: {text:?}");
            if e.time.is_none() {
                assert!(p.gap.is_empty(), "case {case}: {text:?}");
            }
            assert_eq!(e.track.is_some(), matches!(e.kind, EntryKind::Track | EntryKind::Marker));
            assert!(!e.raw.contains(['\n', '\r']));
        }
        assert_eq!(list.stats.lines, list.entries.len());
        let lines: Vec<usize> = list.entries.iter().map(|e| e.line).collect();
        assert_eq!(lines, (1..=list.entries.len()).collect::<Vec<_>>());
        // Only the last line may lack a terminator.
        let open = list.entries.iter().rev().skip(1).filter(|e| e.eol == Eol::None).count();
        assert_eq!(open, 0, "case {case}: {text:?}");
    }
}

#[test]
fn only_lf_crlf_and_cr_break_lines() {
    let count = |text: &str| parse_tracklist(text).entries.len();
    assert_eq!(count("a\u{2028}b\u{2029}c\u{85}d\u{b}e\u{c}f"), 1);
    assert_eq!(count("a\r\nb\rc\nd\n"), 4);
    assert_eq!(count("\n\r\r\n"), 3);
    assert_eq!(count(""), 0);
}

#[test]
fn any_edit_list_returns() {
    // Loose edits through the JSON door: every field may be anything.
    const FIELDS: &[&str] = &[
        "-3",
        "0",
        "1",
        "2",
        "5",
        "15",
        "1.5",
        "-0.25",
        "1e300",
        "-1e300",
        "1e999",
        "-1e999",
        "null",
        "true",
        "false",
        "\"x\"",
        "\"\"",
        "[1,2]",
        "{}",
        "9007199254740993",
        "359999.5",
    ];
    const OPS: &[&str] =
        &["set-time", "shift", "sort-by-time", "set-rest", "insert", "delete", "x"];
    const TEXTS: &[&str] = &["\"a\\nb\"", "\"c\\rd\"", "\"00:10 New - Line\"", "\"w/ x\"", "\"\""];
    let mut rng = Rng(2);
    for case in 0..2000 {
        let text = any_text(&mut rng);
        let mut edits = Vec::new();
        for _ in 0..rng.below(7) {
            if rng.chance(8) {
                edits.push(rng.pick(&["null", "5", "\"shift\"", "[]", "{}"]).to_string());
                continue;
            }
            let mut fields = vec![format!("\"op\":\"{}\"", rng.pick(OPS))];
            for key in ["line", "seconds", "from", "after"] {
                if rng.chance(60) {
                    fields.push(format!("\"{key}\":{}", rng.pick(FIELDS)));
                }
            }
            if rng.chance(30) {
                let items: Vec<&str> = (0..rng.below(4)).map(|_| rng.pick(FIELDS)).collect();
                fields.push(format!("\"lines\":[{}]", items.join(",")));
            }
            for key in ["rest", "text"] {
                if rng.chance(60) {
                    let value = if rng.chance(80) { rng.pick(TEXTS) } else { rng.pick(FIELDS) };
                    fields.push(format!("\"{key}\":{value}"));
                }
            }
            edits.push(format!("{{{}}}", fields.join(",")));
        }
        let edits = Json::parse(&format!("[{}]", edits.join(","))).unwrap();
        let empty = Json::Obj(Vec::new());
        let out = wire::call("applyEdits", &text, &empty, &empty, &edits);
        let new_text = out.get("text").and_then(Json::as_str).unwrap();
        let n = parse_tracklist(new_text).entries.len();
        let changed = out.get("changed").and_then(Json::as_array).unwrap();
        let numbers: Vec<f64> = changed
            .iter()
            .map(|c| match c {
                Json::Num(n) => *n,
                other => panic!("case {case}: changed holds {other:?}"),
            })
            .collect();
        assert!(numbers.windows(2).all(|w| w[0] < w[1]), "case {case}: {numbers:?}");
        assert!(numbers.iter().all(|c| *c >= 1.0 && *c <= n as f64), "case {case}: {numbers:?}");
        // The work lines stay what a fresh split of the text gives (R13.9.5): applying nothing
        // more to the result is the identity, and it parses losslessly.
        assert_eq!(raw_lines(&parse_tracklist(new_text)).concat(), new_text, "case {case}");
    }
}

#[test]
fn no_edits_is_the_identity() {
    let mut rng = Rng(3);
    for _ in 0..1000 {
        let text = any_text(&mut rng);
        let result = apply_edits(&text, &[]);
        assert_eq!(result, EditResult { text, changed: vec![], warnings: vec![] });
    }
}

#[test]
fn an_edit_on_one_line_leaves_the_others() {
    let mut rng = Rng(4);
    for case in 0..1500 {
        let text = timed_list(&mut rng);
        let before = raw_lines(&parse_tracklist(&text));
        if before.is_empty() {
            continue;
        }
        let line = 1 + rng.below(before.len());
        let seconds = rng.below(8001) as f64 - 4000.0;
        let edit = match rng.below(3) {
            0 => Edit::SetTime { line, seconds },
            1 => Edit::SetRest { line, rest: "New - Rest".into() },
            _ => Edit::Shift { seconds, lines: Some(vec![line]), from: None },
        };
        let result = apply_edits(&text, std::slice::from_ref(&edit));
        let after = raw_lines(&parse_tracklist(&result.text));
        assert_eq!(after.len(), before.len(), "case {case}: {edit:?} on {text:?}");
        for (i, (a, b)) in after.iter().zip(&before).enumerate() {
            if i != line - 1 {
                assert_eq!(a, b, "case {case}: {edit:?} on {text:?}");
            }
        }
        assert!(result.changed.iter().all(|c| *c == line), "case {case}: {edit:?} on {text:?}");
        if warned(&result, &[EditWarningCode::EditInvalid]) {
            assert_eq!(result.text, text, "case {case}");
            assert!(result.changed.is_empty(), "case {case}");
        }
    }
}

#[test]
fn insert_and_delete_keep_the_other_lines() {
    let mut rng = Rng(5);
    for case in 0..1500 {
        let text = timed_list(&mut rng);
        let before = raws(&text);
        if before.is_empty() {
            continue;
        }
        let at = rng.below(before.len() + 1);
        let new_line = "00:00 New - Line";
        let ins = apply_edits(&text, &[Edit::Insert { after: at, text: new_line.into() }]);
        let mut expected = before.clone();
        expected.insert(at, new_line.to_string());
        assert_eq!(raws(&ins.text), expected, "case {case}: insert after {at} in {text:?}");
        assert!(ins.changed.contains(&(at + 1)), "case {case}");

        let line = 1 + rng.below(before.len());
        let del = apply_edits(&text, &[Edit::Delete { line }]);
        let mut kept = before.clone();
        kept.remove(line - 1);
        let got = raws(&del.text);
        // Deleting the last line moves its eol ("" here) up: an empty line that becomes last
        // without an eol is not a line (R13.9.5), so trailing empty lines may disappear.
        while kept.last().is_some_and(String::is_empty)
            && got.len() < kept.len()
            && line == before.len()
        {
            kept.pop();
        }
        assert_eq!(got, kept, "case {case}: delete {line} in {text:?}");
    }
}

#[test]
fn shift_round_trip_restores_the_text() {
    let lossy = [
        EditWarningCode::TimeClamped,
        EditWarningCode::StyleUpgraded,
        EditWarningCode::EditInvalid,
    ];
    let widths = |text: &str| -> Vec<Option<usize>> {
        parse_tracklist(text).entries.iter().map(|e| e.time.as_ref().map(|t| t.raw.len())).collect()
    };
    let mut rng = Rng(6);
    let mut checked = 0;
    for case in 0..2000 {
        let text = timed_list(&mut rng);
        let s = (1 + rng.below(5000)) as f64;
        let n = parse_tracklist(&text).entries.len();
        let from = Some(1 + rng.below(12)).filter(|f| rng.chance(50) && *f <= n);
        let up = apply_edits(&text, &[shift(s, from)]);
        let down = apply_edits(&up.text, &[shift(-s, from)]);
        // Widths are minimum widths (R13.9.2): `0:00` + 600 s is `10:00`, whose style then has two
        // minute digits, so the way back writes `00:00`. A time whose digit count grew is outside
        // the round trip (R13.9.7), like clamps and upgrades.
        if warned(&up, &lossy) || warned(&down, &lossy) || widths(&up.text) != widths(&text) {
            continue;
        }
        checked += 1;
        assert_eq!(down.text, text, "case {case}: ±{s} from {from:?}");
    }
    assert!(checked > 100, "only {checked} round trips were checked");
}

#[test]
fn sort_keeps_the_eol_slots_and_is_idempotent() {
    let eols =
        |text: &str| -> Vec<Eol> { parse_tracklist(text).entries.iter().map(|e| e.eol).collect() };
    let mut rng = Rng(7);
    for case in 0..1500 {
        let text = timed_list(&mut rng);
        let once = apply_edits(&text, &[Edit::SortByTime]);
        // An empty line sorted into the last slot (eol "") is not a line any more (R13.1.1).
        if eols(&once.text).len() == eols(&text).len() {
            assert_eq!(eols(&once.text), eols(&text), "case {case}: {text:?}");
        }
        let times: Vec<f64> = parse_tracklist(&once.text)
            .entries
            .iter()
            .filter_map(|e| e.time.as_ref().map(|t| t.seconds))
            .collect();
        assert!(times.windows(2).all(|w| w[0] <= w[1]), "case {case}: {times:?}");
        assert_eq!(apply_edits(&once.text, &[Edit::SortByTime]).text, once.text, "case {case}");
    }
}

#[test]
fn a_one_line_track_is_the_parse_of_its_line_text() {
    // R13.4.1: the track part of a one-line text (no neighbours, so no bare position) is `rest`
    // without its emphasis markers (R13.12) and a layered prefix (R13.13); the track's `input`
    // is that text.
    let options = ParseOptions::new().mode(ModeOption::Tracklist);
    let mut rng = Rng(8);
    let mut tracks = 0;
    for case in 0..3000 {
        let mut text = if rng.chance(50) { String::new() } else { list_line(&mut rng) };
        text.push_str(&noisy_text(&mut rng));
        let text = text.replace(['\r', '\n'], " ");
        let list = parse_tracklist(&text);
        let Some(entry) = list.entries.first() else { continue };
        assert_eq!(list.entries.len(), 1);
        let Some(track) = &entry.track else { continue };
        tracks += 1;
        assert_eq!(track, &parse_with(&track.input, &options), "case {case}: {text:?}");
        assert!(
            track.position.is_none() || !track.input.starts_with(char::is_numeric) || {
                // A position here is R4.2's own (`01. `, `3) `), never a bare one.
                let raw = &track.position.as_ref().unwrap().raw;
                track.input.trim_start().len() > raw.len()
            }
        );
        let plain = !entry.parts.rest.contains(['*', '_']) && !entry.layered;
        if plain {
            assert_eq!(track.input, entry.parts.rest, "case {case}: {text:?}");
        }
    }
    assert!(tracks > 500, "only {tracks} one-line tracks were checked");
}

#[test]
fn emphasis_changes_nothing() {
    const RESTS: &[&str] = &[
        "Noisia - Stigma",
        "01. Phace - Plastic Acid",
        "Intro",
        "Paperclip interview",
        "Mefjus - Suicide Bassline (VIP)",
        "ID - ID",
        "Noisia feat. Foreign Beggars - Shellshock",
    ];
    const TIME_WRAPS: &[(&str, &str)] = &[
        ("", ""),
        ("**", "**"),
        ("__", "__"),
        ("*", "*"),
        ("_", "_"),
        ("**[", "]**"),
        ("[**", "**]"),
        ("**[**", "**]"),
        ("_[", "]_"),
    ];
    let mut rng = Rng(9);
    for _ in 0..40 {
        let t = write_time(rng.pick(STYLES), rng.below(3600) as u32, rng.below(1000) as u32);
        for rest in RESTS {
            let bare = parse_tracklist(&format!("{t} {rest}")).entries.remove(0);
            for (open, close) in TIME_WRAPS {
                for wrap in ["", "**", "__", "*", "_"] {
                    let decorated = format!("{open}{t}{close} {wrap}{rest}{wrap}");
                    let e = parse_tracklist(&decorated).entries.remove(0);
                    assert_eq!(
                        e.time.as_ref().map(|t| t.seconds),
                        bare.time.as_ref().map(|t| t.seconds),
                        "{decorated:?}"
                    );
                    assert_eq!(e.kind, bare.kind, "{decorated:?}");
                    assert_eq!(e.track, bare.track, "{decorated:?}");
                }
            }
        }
    }
}

/// Best of three, so a busy CI worker does not fail the run; a super-linear blowup still would.
fn best(mut f: impl FnMut()) -> Duration {
    (0..3)
        .map(|_| {
            let start = Instant::now();
            f();
            start.elapsed()
        })
        .min()
        .unwrap()
}

fn hms(s: usize) -> String {
    format!("{:02}:{:02}:{:02}", s / 3600, (s % 3600) / 60, s % 60)
}

// R13.11.2: generous ceilings that catch super-linear blowups, not CI load. `cargo test` runs
// these in the dev profile, where the parser is several times slower than in a release build
// (10,000 lines: about 0.7 s optimised, 4 s unoptimised on the machine this was written on).
const SLOW_BUILD: u64 = if cfg!(debug_assertions) { 6 } else { 1 };
const PARSE_CEILING: Duration = Duration::from_secs(3 * SLOW_BUILD);
const EDIT_CEILING: Duration = Duration::from_secs(SLOW_BUILD);

#[test]
fn ten_thousand_lines_finish_under_the_ceiling() {
    let lines: Vec<String> = (0..10_000)
        .map(|i| {
            format!("{} {} Artist {i} & Other - Title {i} (VIP Mix)", hms((i * 37) % 7200), i % 100)
        })
        .collect();
    let text = lines.join("\r\n");
    assert_eq!(parse_tracklist(&text).entries.len(), 10_000);
    let parse = best(|| {
        parse_tracklist(&text);
    });
    let edits = [shift(5.0, None), Edit::SortByTime];
    let edit = best(|| {
        apply_edits(&text, &edits);
    });
    println!("10,000 lines: parse_tracklist {parse:?}, shift + sort {edit:?}");
    assert!(parse < PARSE_CEILING, "parse_tracklist took {parse:?}");
    assert!(edit < EDIT_CEILING, "apply_edits took {edit:?}");
}

#[test]
fn two_thousand_edits_on_five_thousand_lines_stay_linear() {
    // R13.11.2: apply_edits costs lines plus edits, not their product. Each line's time is
    // scanned once per write and the one-digit-hours test is kept up to date.
    let lines: Vec<String> = (0..5000).map(|i| format!("{} A {i} - T {i}", hms(i * 30))).collect();
    let text = lines.join("\n");
    let set_times: Vec<Edit> = (0..2000)
        .map(|k| Edit::SetTime { line: (k * 7) % 5000 + 1, seconds: (k * 10) as f64 })
        .collect();
    let inserts: Vec<Edit> = (0..2000)
        .map(|k| Edit::Insert { after: (k * 7) % 5000, text: "00:00 X - Y".into() })
        .collect();
    assert_eq!(apply_edits(&text, &set_times).changed.len(), 2000);
    assert_eq!(apply_edits(&text, &inserts).changed.len(), 2000);
    let set = best(|| {
        apply_edits(&text, &set_times);
    });
    let ins = best(|| {
        apply_edits(&text, &inserts);
    });
    println!("2,000 edits on 5,000 lines: set-time {set:?}, insert {ins:?}");
    assert!(set < EDIT_CEILING, "2,000 set-time edits took {set:?}");
    assert!(ins < EDIT_CEILING, "2,000 insert edits took {ins:?}");
}
