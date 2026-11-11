//! The typed tracklist API: it must agree with the JSON door (`wire`), which the shared fixtures
//! and the cross-port diff exercise.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic, clippy::indexing_slicing)]

use trackparse::json::Json;
use trackparse::tracklist::{
    apply_edits, apply_edits_with, parse_tracklist, parse_tracklist_with, Edit, EditResult,
    EditWarning, EditWarningCode, EntryKind, Eol, Parts, Severity, Stats, Suggestion, TimeValue,
    Tracklist, TracklistEntry, TracklistOptions, TracklistWarning, WarningCode,
};
use trackparse::{
    wire, JunkKind, KeywordOptions, Mode, ModeOption, SplitAnd, ToJson, VersionType, SPEC_VERSION,
};

const TEXTS: &[&str] = &[
    "",
    "\n",
    "00:00 Noisia - Stigma\n03:30 Phace - Psycho\nrecorded live\n07:15 Mefjus - Sinkhole",
    "Tracklist:\r\n[00:00] Intro\r\n[01:30] 1 Noisia - Stigma\r\n[05:00] 2 Phace - Psycho\r\n",
    "**00:00** *Noisia* - Stigma\r- 03:30 Camo & Krooked - Climax\rw/ Mefjus - Sinkhole\r",
    "0:00 A - B\n1:22:21 C - D\n1:03:00 E - F\n1:75 bad\nhttps://example.com\n",
    "01:02:03.250 🎵 - 𝐀𝐁 (👨‍👩‍👧 Remix)\u{2028}still the same line\n\u{3000}\n12:34,5 x",
    "1 Noisia - Stigma\n2 Phace - Psycho\n4 Mefjus - Sinkhole\n4 A - B",
];

fn json(text: &str) -> Json {
    Json::parse(text).unwrap()
}

fn none() -> Json {
    json("{}")
}

fn wire_parse(text: &str, options: &str) -> String {
    wire::call("parseTracklist", text, &json(options), &none(), &json("[]")).write()
}

fn wire_edits(text: &str, edits: &str) -> String {
    wire::call("applyEdits", text, &none(), &none(), &json(edits)).write()
}

#[test]
fn typed_parse_matches_the_wire() {
    for text in TEXTS {
        assert_eq!(parse_tracklist(text).to_json(), wire_parse(text, "{}"), "{text:?}");
        let default = parse_tracklist_with(text, &TracklistOptions::default());
        assert_eq!(default, parse_tracklist(text), "{text:?}");
    }
}

#[test]
fn typed_options_match_the_wire() {
    let keywords = KeywordOptions::new()
        .version_head("rub", VersionType::Remix)
        .junk("free dl", JunkKind::Promo);
    let cases: Vec<(TracklistOptions, &str)> = vec![
        (TracklistOptions::new().mode(ModeOption::Auto), r#"{"mode":"auto"}"#),
        (TracklistOptions::new().mode(ModeOption::Clean), r#"{"mode":"clean"}"#),
        (TracklistOptions::new().mode(ModeOption::Youtube), r#"{"mode":"youtube"}"#),
        (TracklistOptions::new().mode(ModeOption::Filename), r#"{"mode":"filename"}"#),
        (TracklistOptions::new().mode(ModeOption::Tracklist), r#"{"mode":"tracklist"}"#),
        (TracklistOptions::new().audio_seconds(200.0), r#"{"audioSeconds":200}"#),
        (TracklistOptions::new().audio_seconds(-1.0), r#"{"audioSeconds":-1}"#),
        (TracklistOptions::new().audio_seconds(f64::NAN), r#"{"audioSeconds":null}"#),
        (TracklistOptions::new().audio_seconds(f64::INFINITY), r#"{"audioSeconds":1e999}"#),
        (
            TracklistOptions::new().marker_words(["recorded", "x-y z"]),
            r#"{"markerWords":["recorded","x-y z"]}"#,
        ),
        (
            TracklistOptions::new().known_artists(["Camo & Krooked"]),
            r#"{"knownArtists":["Camo & Krooked"]}"#,
        ),
        (TracklistOptions::new().split_and(SplitAnd::Never), r#"{"splitAnd":"never"}"#),
        (TracklistOptions::new().split_and(SplitAnd::Always), r#"{"splitAnd":"always"}"#),
        (
            TracklistOptions::new().keywords(keywords),
            r#"{"keywords":{"versionHeads":{"rub":"remix"},"junk":{"free dl":"promo"}}}"#,
        ),
    ];
    let extra =
        "00:00 Camo & Krooked - Climax (Noisia Rub) free dl\n02:00 A and B - C\nrecorded live";
    for (typed, loose) in &cases {
        for text in TEXTS.iter().chain([&extra]) {
            assert_eq!(
                parse_tracklist_with(text, typed).to_json(),
                wire_parse(text, loose),
                "{loose} {text:?}"
            );
        }
    }
}

#[test]
fn bad_wire_options_fall_back() {
    let text = TEXTS[2];
    let default = wire_parse(text, "{}");
    for loose in [
        "null",
        "5",
        "[]",
        r#""x""#,
        r#"{"mode":"bogus"}"#,
        r#"{"mode":7,"splitAnd":"bogus","keywords":[],"knownArtists":"x"}"#,
        r#"{"audioSeconds":"60","markerWords":"recorded"}"#,
        r#"{"audioSeconds":true,"markerWords":[5,null]}"#,
        r#"{"uploader":"Noisia"}"#,
    ] {
        assert_eq!(wire_parse(text, loose), default, "{loose}");
    }
}

#[test]
fn entries_carry_every_field() {
    let list =
        parse_tracklist("Tracklist:\r\n[00:00] Intro\r01:30 - 1. Noisia - Stigma\n\nw/ A - B");
    assert_eq!(list.spec_version, SPEC_VERSION);
    let kinds: Vec<EntryKind> = list.entries.iter().map(|e| e.kind).collect();
    assert_eq!(
        kinds,
        [
            EntryKind::Header,
            EntryKind::Marker,
            EntryKind::Track,
            EntryKind::Blank,
            EntryKind::Track
        ]
    );
    let eols: Vec<Eol> = list.entries.iter().map(|e| e.eol).collect();
    assert_eq!(eols, [Eol::CrLf, Eol::Cr, Eol::Lf, Eol::Lf, Eol::None]);
    assert_eq!(Eol::CrLf.as_str(), "\r\n");

    let e = &list.entries[2];
    assert_eq!(e.line, 3);
    assert_eq!(
        e.parts,
        Parts {
            lead: String::new(),
            time: "01:30".into(),
            gap: " - ".into(),
            rest: "1. Noisia - Stigma".into()
        }
    );
    assert_eq!(e.time, Some(TimeValue { raw: "01:30".into(), seconds: 90.0 }));
    let track = e.track.as_ref().unwrap();
    assert_eq!(track.mode, Mode::Tracklist);
    assert_eq!(track.position.as_ref().unwrap().number, 1);
    assert_eq!((track.artists[0].name.as_str(), track.title.as_str()), ("Noisia", "Stigma"));
    assert!(!e.layered);
    assert!(list.entries[4].layered);
    assert!(list.entries[0].track.is_none());
    assert_eq!(
        list.stats,
        Stats {
            lines: 5,
            tracks: 2,
            markers: 1,
            timed: 2,
            first_time: Some(0.0),
            last_time: Some(90.0)
        }
    );
}

#[test]
fn warnings_and_suggestions() {
    // The JS reference's output for this text.
    let list = parse_tracklist(
        "0:10 A - B\n58:00 K - L\n1:22:21 C - D\n1:03:00 E - F\n1:04:00 G - H\nI - J\n1:75 x - y",
    );
    let warning = |code, line, suggestion| TracklistWarning {
        code,
        line,
        severity: if code == WarningCode::NoStartAtZero {
            Severity::Info
        } else {
            Severity::Warning
        },
        suggestion,
    };
    let fix = Suggestion { line: 3, raw: "1:02:21".into(), seconds: 3741.0 };
    assert_eq!(
        list.warnings,
        [
            warning(WarningCode::NoStartAtZero, 1, None),
            warning(WarningCode::TimeOutOfOrder, 4, Some(fix)),
            warning(WarningCode::TimeMissing, 6, None),
            warning(WarningCode::TimeMissing, 7, None),
            warning(WarningCode::InvalidTime, 7, None),
        ]
    );
    assert_eq!(WarningCode::ALL.len(), 9);
    assert_eq!(WarningCode::from_name("line-unparsed"), Some(WarningCode::LineUnparsed));
}

#[test]
fn json_has_the_schema_keys_in_order() {
    let list: Tracklist = parse_tracklist("00:00.5 Noisia - Stigma\n00:00 x");
    let value = list.to_json_value();
    let keys = |v: &Json| -> Vec<String> {
        v.as_object().unwrap().iter().map(|(k, _)| k.clone()).collect()
    };
    assert_eq!(keys(&value), ["specVersion", "entries", "warnings", "stats"]);
    let entry: &TracklistEntry = &list.entries[0];
    let entry_json = entry.to_json_value();
    assert_eq!(
        keys(&entry_json),
        ["line", "kind", "eol", "raw", "parts", "time", "track", "layered"]
    );
    assert_eq!(keys(entry_json.get("parts").unwrap()), ["lead", "time", "gap", "rest"]);
    assert_eq!(entry_json.get("time").unwrap().write(), r#"{"raw":"00:00.5","seconds":0.5}"#);
    assert_eq!(
        keys(value.get("stats").unwrap()),
        ["lines", "tracks", "markers", "timed", "firstTime", "lastTime"]
    );
    let warning = &value.get("warnings").unwrap().as_array().unwrap()[0];
    assert_eq!(keys(warning), ["code", "line", "severity", "suggestion"]);
    // Whole seconds are integers in JSON.
    assert!(parse_tracklist("52:47 A - B").to_json().contains(r#""seconds":3167}"#));
    assert_eq!(
        parse_tracklist("").to_json(),
        format!(
            r#"{{"specVersion":"{SPEC_VERSION}","entries":[],"warnings":[],"stats":{{"lines":0,"tracks":0,"markers":0,"timed":0,"firstTime":null,"lastTime":null}}}}"#
        )
    );
}

#[test]
fn typed_edits_match_the_wire() {
    let cases: Vec<(Vec<Edit>, &str)> = vec![
        (vec![], "[]"),
        (
            vec![Edit::SetTime { line: 2, seconds: 3725.0 }],
            r#"[{"op":"set-time","line":2,"seconds":3725}]"#,
        ),
        (
            vec![Edit::SetTime { line: 2, seconds: -4.5 }],
            r#"[{"op":"set-time","line":2,"seconds":-4.5}]"#,
        ),
        (
            vec![Edit::SetTime { line: 9, seconds: 1.0 }],
            r#"[{"op":"set-time","line":9,"seconds":1}]"#,
        ),
        (
            vec![Edit::SetTime { line: 1, seconds: f64::INFINITY }],
            r#"[{"op":"set-time","line":1,"seconds":1e999}]"#,
        ),
        (
            vec![Edit::SetTime { line: 1, seconds: f64::NAN }],
            r#"[{"op":"set-time","line":1,"seconds":null}]"#,
        ),
        (
            vec![Edit::SetTime { line: 1, seconds: 360_000.0 }],
            r#"[{"op":"set-time","line":1,"seconds":360000}]"#,
        ),
        (
            vec![Edit::Shift { seconds: 30.0, lines: None, from: None }],
            r#"[{"op":"shift","seconds":30}]"#,
        ),
        (
            vec![Edit::Shift { seconds: -3600.0, lines: None, from: Some(2) }],
            r#"[{"op":"shift","seconds":-3600,"from":2}]"#,
        ),
        (
            vec![Edit::Shift { seconds: 0.25, lines: Some(vec![4, 1, 4]), from: Some(3) }],
            r#"[{"op":"shift","seconds":0.25,"lines":[4,1,4],"from":3}]"#,
        ),
        (
            vec![Edit::Shift { seconds: 1.0, lines: Some(vec![0]), from: None }],
            r#"[{"op":"shift","seconds":1,"lines":[0]}]"#,
        ),
        (vec![Edit::SortByTime], r#"[{"op":"sort-by-time"}]"#),
        (
            vec![Edit::SetRest { line: 1, rest: "New - Rest".into() }],
            r#"[{"op":"set-rest","line":1,"rest":"New - Rest"}]"#,
        ),
        (
            vec![Edit::SetRest { line: 1, rest: "a\nb".into() }],
            r#"[{"op":"set-rest","line":1,"rest":"a\nb"}]"#,
        ),
        (
            vec![Edit::Insert { after: 0, text: "00:00 X - Y".into() }],
            r#"[{"op":"insert","after":0,"text":"00:00 X - Y"}]"#,
        ),
        (
            vec![Edit::Insert { after: 99, text: "x".into() }],
            r#"[{"op":"insert","after":99,"text":"x"}]"#,
        ),
        (vec![Edit::Delete { line: 1 }], r#"[{"op":"delete","line":1}]"#),
        (
            vec![
                Edit::Delete { line: 1 },
                Edit::Insert { after: 1, text: "9:59:59 Late - One".into() },
                Edit::SortByTime,
                Edit::Shift { seconds: 5000.0, lines: None, from: None },
            ],
            r#"[{"op":"delete","line":1},{"op":"insert","after":1,"text":"9:59:59 Late - One"},{"op":"sort-by-time"},{"op":"shift","seconds":5000}]"#,
        ),
    ];
    for (typed, loose) in &cases {
        let listed = Json::Arr(typed.iter().map(ToJson::to_json_value).collect()).write();
        // Non-finite numbers are `null` in written JSON, as in JS.
        assert_eq!(listed, json(loose).write(), "Edit::to_json_value");
        for text in TEXTS {
            let result = apply_edits(text, typed);
            assert_eq!(result.to_json(), wire_edits(text, loose), "{loose} {text:?}");
            assert_eq!(apply_edits_with(text, typed, &TracklistOptions::new()), result);
        }
    }
}

#[test]
fn edit_results() {
    let text = "00:00 Noisia - Stigma\n59:30 Phace - Psycho\n";
    let result = apply_edits(
        text,
        &[
            Edit::Shift { seconds: 45.0, lines: None, from: Some(2) },
            Edit::SetTime { line: 1, seconds: -3.0 },
            Edit::Delete { line: 7 },
        ],
    );
    assert_eq!(
        result,
        EditResult {
            text: "00:00 Noisia - Stigma\n01:00:15 Phace - Psycho\n".into(),
            changed: vec![1, 2],
            warnings: vec![
                EditWarning { code: EditWarningCode::StyleUpgraded, edit: 0, line: Some(2) },
                EditWarning { code: EditWarningCode::TimeClamped, edit: 1, line: Some(1) },
                EditWarning { code: EditWarningCode::EditInvalid, edit: 2, line: Some(7) },
            ],
        }
    );
    assert_eq!(
        result.to_json(),
        r#"{"text":"00:00 Noisia - Stigma\n01:00:15 Phace - Psycho\n","changed":[1,2],"warnings":[{"code":"style-upgraded","edit":0,"line":2},{"code":"time-clamped","edit":1,"line":1},{"code":"edit-invalid","edit":2,"line":7}]}"#
    );
    // Half-up rounding at the written precision (R13.9.2), not banker's rounding.
    let rounded = apply_edits("0:00.5 A - B", &[Edit::SetTime { line: 1, seconds: 2.25 }]);
    assert_eq!(rounded.text, "0:02.3 A - B");
    // 100 hours cannot be written (R13.9.2).
    let too_long = apply_edits("1:00:00 A - B", &[Edit::SetTime { line: 1, seconds: 360_000.0 }]);
    assert_eq!(too_long.text, "1:00:00 A - B");
    assert_eq!(too_long.warnings[0].code, EditWarningCode::EditInvalid);
}

#[test]
fn loose_edits_follow_js_number_rules() {
    let text = "00:00 A - B\n01:00 C - D";
    let invalid = |edits: &str| {
        let out = json(&wire_edits(text, edits));
        assert_eq!(out.get("text").unwrap().as_str(), Some(text), "{edits}");
        let warnings = out.get("warnings").unwrap().as_array().unwrap();
        assert_eq!(warnings.len(), 1, "{edits}");
        assert_eq!(warnings[0].get("code").unwrap().as_str(), Some("edit-invalid"), "{edits}");
        warnings[0].get("line").unwrap().write()
    };
    assert_eq!(invalid(r#"[{"op":"set-time","line":true,"seconds":5}]"#), "null");
    assert_eq!(invalid(r#"[{"op":"set-time","line":1,"seconds":true}]"#), "1");
    assert_eq!(invalid(r#"[{"op":"set-time","line":1,"seconds":null}]"#), "1");
    assert_eq!(invalid(r#"[{"op":"set-time","line":1,"seconds":"5"}]"#), "1");
    assert_eq!(invalid(r#"[{"op":"set-time","line":1,"seconds":1e999}]"#), "1");
    assert_eq!(invalid(r#"[{"op":"set-time","line":1.5,"seconds":5}]"#), "null");
    assert_eq!(invalid(r#"[{"op":"shift","seconds":-1e999}]"#), "null");
    assert_eq!(invalid(r#"[{"op":"shift","seconds":1,"lines":null}]"#), "null");
    assert_eq!(invalid(r#"[{"op":"shift","seconds":1,"lines":[1,3]}]"#), "null");
    assert_eq!(invalid(r#"[{"op":"shift","seconds":1,"from":0}]"#), "null");
    assert_eq!(invalid(r#"[{"op":"shift","seconds":9007199254740992}]"#), "null");
    assert_eq!(invalid(r#"[{"op":"rename","line":-2}]"#), "-2");
    assert_eq!(invalid(r#"[{"op":"rename","line":"x","after":1e300}]"#), "1e+300");
    assert_eq!(invalid(r#"[{"op":"insert","after":3,"text":"x"}]"#), "3");
    assert_eq!(invalid(r#"[{"op":"insert","after":1,"text":"a\rb"}]"#), "1");
    assert_eq!(invalid(r#"[{"op":"delete"}]"#), "null");
    assert_eq!(invalid("[null]"), "null");
    assert_eq!(invalid("[[]]"), "null");
    // `lines` wins over `from`; not an array of edits: nothing to do.
    assert_eq!(
        wire_edits(text, r#"[{"op":"shift","seconds":1,"lines":[2],"from":"bad"}]"#),
        r#"{"text":"00:00 A - B\n01:01 C - D","changed":[2],"warnings":[]}"#
    );
    assert_eq!(wire_edits(text, r#"{"op":"sort-by-time"}"#), wire_edits(text, "[]"));
}
