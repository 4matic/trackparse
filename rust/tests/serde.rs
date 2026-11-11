//! The `serde` feature: `serde_json::to_string(&x)` is the text of `x.to_json()` for every
//! output type, over every shared fixture input, and the typed options and edits read from JSON
//! give what the JSON door (`wire`) gives.
#![cfg(feature = "serde")]
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic, clippy::indexing_slicing)]

use std::path::{Path, PathBuf};

use serde::de::DeserializeOwned;
use serde::Serialize;
use serde_json::{Map, Value};
use trackparse::json::Json;
use trackparse::tracklist::{
    apply_edits, parse_tracklist, parse_tracklist_with, Edit, EditResult, EditWarningCode,
    EntryKind, Eol, Severity, Tracklist, TracklistOptions, WarningCode,
};
use trackparse::{
    format_track, format_track_with, parse, parse_artists, parse_artists_with, parse_with, wire,
    Artist, ArtistRole, ArtistSource, FeatPlacement, FormatOptions, JoinerStyle, JunkKind,
    KeywordOptions, Mode, ModeOption, ParseOptions, ParsedTrack, SplitAnd, VersionDelimiter,
    VersionType, VersionsOption, Warning,
};

fn walk(dir: &Path, out: &mut Vec<PathBuf>) {
    let mut entries: Vec<PathBuf> =
        std::fs::read_dir(dir).unwrap().map(|e| e.unwrap().path()).collect();
    entries.sort();
    for p in entries {
        if p.is_dir() {
            walk(&p, out);
        } else if p.extension().is_some_and(|e| e == "json") {
            out.push(p);
        }
    }
}

fn merged(defaults: Option<&Value>, own: Option<&Value>) -> Value {
    let mut out = Map::new();
    for src in [defaults, own].into_iter().flatten() {
        if let Value::Object(o) = src {
            for (k, v) in o {
                out.insert(k.clone(), v.clone());
            }
        }
    }
    Value::Object(out)
}

/// The value as the crate's own JSON, for the `wire` door.
fn crate_json<T: Serialize>(value: &T) -> Json {
    Json::parse(&serde_json::to_string(value).unwrap()).unwrap()
}

fn none() -> Json {
    Json::Obj(Vec::new())
}

/// `serde_json` writes the text of `to_json()`, and reads it back to an equal value.
fn same<T: Serialize + DeserializeOwned + PartialEq + std::fmt::Debug>(value: &T, json: String) {
    assert_eq!(serde_json::to_string(value).unwrap(), json);
    let back: T = serde_json::from_str(&json).unwrap_or_else(|e| panic!("{e}: {json}"));
    assert_eq!(&back, value, "round trip of {json}");
}

fn same_value<T: PartialEq>(a: &T, b: &T) -> bool {
    a == b
}

fn same_artists(artists: &[Artist]) -> String {
    let json = format!("[{}]", artists.iter().map(Artist::to_json).collect::<Vec<_>>().join(","));
    same(&artists.to_vec(), json.clone());
    json
}

#[derive(Default, Debug)]
struct Counts {
    cases: usize,
    typed_options: usize,
    typed_edits: usize,
}

fn check_case(
    func: &str,
    input: &str,
    options: &Value,
    format_options: &Value,
    edits: &Value,
    n: &mut Counts,
) {
    n.cases += 1;
    match func {
        "parse" | "format" | "parseArtists" => {
            let track: ParsedTrack = parse(input);
            same(&track, track.to_json());
            same_artists(&parse_artists(input));
            // Options the typed API can express: the typed call is the JSON door's call.
            if let Ok(typed) = serde_json::from_value::<ParseOptions>(options.clone()) {
                n.typed_options += 1;
                let again = crate_json(&typed);
                same(&typed, again.write());
                let track = parse_with(input, &typed);
                same(&track, track.to_json());
                assert_eq!(
                    track.to_json(),
                    wire::call("parse", input, &again, &none(), &none()).write()
                );
                let artists = same_artists(&parse_artists_with(input, &typed));
                assert_eq!(
                    artists,
                    wire::call("parseArtists", input, &again, &none(), &none()).write()
                );
                if let Ok(format) = serde_json::from_value::<FormatOptions>(format_options.clone())
                {
                    let format_again = crate_json(&format);
                    same(&format, format_again.write());
                    let text = format_track_with(&track, &format);
                    assert_eq!(
                        Json::Str(text),
                        wire::call("format", input, &again, &format_again, &none())
                    );
                }
            }
        }
        "parseTracklist" | "applyEdits" => {
            let list: Tracklist = parse_tracklist(input);
            same(&list, list.to_json());
            if let Ok(typed) = serde_json::from_value::<TracklistOptions>(options.clone()) {
                n.typed_options += 1;
                let again = crate_json(&typed);
                same(&typed, again.write());
                let list = parse_tracklist_with(input, &typed);
                same(&list, list.to_json());
                assert_eq!(
                    list.to_json(),
                    wire::call("parseTracklist", input, &again, &none(), &none()).write()
                );
            }
            if let Ok(typed) = serde_json::from_value::<Vec<Edit>>(edits.clone()) {
                n.typed_edits += typed.len();
                for edit in &typed {
                    if same_value(edit, edit) {
                        same(edit, edit.to_json());
                    } else {
                        // Non-finite seconds (`null`) read back as NaN, which equals nothing.
                        assert_eq!(serde_json::to_string(edit).unwrap(), edit.to_json());
                    }
                }
                let result: EditResult = apply_edits(input, &typed);
                same(&result, result.to_json());
                assert_eq!(
                    result.to_json(),
                    wire::call("applyEdits", input, &none(), &none(), &crate_json(&typed)).write()
                );
            }
        }
        _ => {}
    }
}

#[test]
fn serde_json_writes_to_json_for_every_fixture_input() {
    let mut files = Vec::new();
    walk(&Path::new(env!("CARGO_MANIFEST_DIR")).join("../spec/fixtures"), &mut files);
    let mut n = Counts::default();
    for path in &files {
        // `1e999` (infinity) is not JSON serde_json reads; such edits have no typed form anyway.
        let text = std::fs::read_to_string(path)
            .unwrap()
            .replace("-1e999", "null")
            .replace("1e999", "null");
        let data: Value = serde_json::from_str(&text).unwrap();
        let Some(cases) = data.get("cases").and_then(Value::as_array) else { continue };
        let defaults = data.get("defaults");
        for case in cases {
            let func = case
                .get("fn")
                .or_else(|| defaults.and_then(|d| d.get("fn")))
                .and_then(Value::as_str)
                .unwrap_or("parse");
            let options = merged(defaults.and_then(|d| d.get("options")), case.get("options"));
            let format_options =
                merged(defaults.and_then(|d| d.get("formatOptions")), case.get("formatOptions"));
            let edits = case.get("edits").cloned().unwrap_or(Value::Array(vec![]));
            let input = case.get("input").and_then(Value::as_str).unwrap_or("");
            check_case(func, input, &options, &format_options, &edits, &mut n);
        }
    }
    println!("{n:?}");
    assert!(n.cases > 13_000, "{n:?}");
    assert!(n.typed_options > 12_000, "{n:?}");
    assert!(n.typed_edits > 50, "{n:?}");
}

#[test]
fn enums_are_their_spec_names() {
    fn names<T: Serialize + DeserializeOwned + PartialEq + std::fmt::Debug + Copy>(
        all: &[T],
        as_str: impl Fn(T) -> &'static str,
    ) {
        for value in all {
            let json = serde_json::to_string(value).unwrap();
            assert_eq!(json, serde_json::to_string(as_str(*value)).unwrap());
            assert_eq!(&serde_json::from_str::<T>(&json).unwrap(), value);
        }
        assert!(serde_json::from_str::<T>("\"no such name\"").is_err());
        assert!(serde_json::from_str::<T>("1").is_err());
    }
    names(Mode::ALL, Mode::as_str);
    names(ArtistRole::ALL, ArtistRole::as_str);
    names(ArtistSource::ALL, ArtistSource::as_str);
    names(VersionType::ALL, VersionType::as_str);
    names(VersionDelimiter::ALL, VersionDelimiter::as_str);
    names(JunkKind::ALL, JunkKind::as_str);
    names(Warning::ALL, Warning::as_str);
    names(ModeOption::ALL, ModeOption::as_str);
    names(SplitAnd::ALL, SplitAnd::as_str);
    names(FeatPlacement::ALL, FeatPlacement::as_str);
    names(JoinerStyle::ALL, JoinerStyle::as_str);
    names(VersionsOption::ALL, VersionsOption::as_str);
    names(EntryKind::ALL, EntryKind::as_str);
    names(Eol::ALL, Eol::as_str);
    names(WarningCode::ALL, WarningCode::as_str);
    names(Severity::ALL, Severity::as_str);
    names(EditWarningCode::ALL, EditWarningCode::as_str);
}

#[test]
fn options_read_and_write_the_spec_keys() {
    let options: ParseOptions = serde_json::from_str(
        r#"{"mode":"youtube","uploader":"Noisia","knownArtists":["Chase & Status"],
            "splitAnd":"never","keywords":{"versionHeads":{"rerub":"remix","7":"edit"},
            "junk":{"out now":"promo"},"featMarkers":["starring"]}}"#,
    )
    .unwrap();
    let keywords = KeywordOptions::new()
        .version_head("rerub", VersionType::Remix)
        .version_head("7", VersionType::Edit)
        .junk("out now", JunkKind::Promo)
        .feat_marker("starring");
    let expected = ParseOptions::new()
        .mode(ModeOption::Youtube)
        .uploader("Noisia")
        .known_artists(["Chase & Status"])
        .split_and(SplitAnd::Never)
        .keywords(keywords);
    assert_eq!(options, expected);
    assert_eq!(
        serde_json::to_string(&options).unwrap(),
        concat!(
            r#"{"mode":"youtube","uploader":"Noisia","knownArtists":["Chase & Status"],"#,
            r#""splitAnd":"never","keywords":{"versionHeads":{"rerub":"remix","7":"edit"},"#,
            r#""descriptors":[],"genres":[],"junk":{"out now":"promo"},"featMarkers":["starring"]}}"#
        )
    );
    // Missing keys are the defaults; a bad value is an error (the typed API has no fallbacks).
    assert_eq!(serde_json::from_str::<ParseOptions>("{}").unwrap(), ParseOptions::default());
    assert_eq!(serde_json::from_str::<FormatOptions>("{}").unwrap(), FormatOptions::default());
    assert_eq!(
        serde_json::from_str::<TracklistOptions>("{}").unwrap(),
        TracklistOptions::default()
    );
    assert!(serde_json::from_str::<ParseOptions>(r#"{"mode":"radio"}"#).is_err());
    assert_eq!(
        serde_json::to_string(&FormatOptions::new().feat(FeatPlacement::Omit)).unwrap(),
        r#"{"feat":"omit","joiners":"original","versions":"all","producers":true,"position":false}"#
    );
    assert_eq!(
        serde_json::to_string(&TracklistOptions::new().audio_seconds(3600.5)).unwrap(),
        r#"{"mode":"tracklist","knownArtists":[],"splitAnd":"auto","audioSeconds":3600.5,"markerWords":[]}"#
    );
}

#[test]
fn edits_read_and_write_the_spec_form() {
    let edits: Vec<Edit> = serde_json::from_str(
        r#"[{"op":"set-time","line":3,"seconds":90},{"op":"shift","seconds":-1.5,"from":2},
            {"op":"shift","seconds":10,"lines":[1,2]},{"op":"sort-by-time"},
            {"op":"set-rest","line":1,"rest":"A - B"},{"op":"insert","after":0,"text":"x"},
            {"op":"delete","line":4}]"#,
    )
    .unwrap();
    assert_eq!(
        edits,
        [
            Edit::set_time(3, 90.0),
            Edit::shift_from(-1.5, 2),
            Edit::shift_lines(10.0, [1, 2]),
            Edit::sort_by_time(),
            Edit::set_rest(1, "A - B"),
            Edit::insert(0, "x"),
            Edit::delete(4),
        ]
    );
    assert_eq!(
        serde_json::to_string(&edits).unwrap(),
        concat!(
            r#"[{"op":"set-time","line":3,"seconds":90},{"op":"shift","seconds":-1.5,"from":2},"#,
            r#"{"op":"shift","seconds":10,"lines":[1,2]},{"op":"sort-by-time"},"#,
            r#"{"op":"set-rest","line":1,"rest":"A - B"},{"op":"insert","after":0,"text":"x"},"#,
            r#"{"op":"delete","line":4}]"#
        )
    );
    assert!(serde_json::from_str::<Edit>(r#"{"op":"rename","line":1}"#).is_err());
    assert_eq!(format_track(&parse("A - B")), "A - B");
}

/// `serde_json` writes `json`, which is also `to_json()`.
fn text<T: Serialize>(value: &T, to_json: String, json: &str) {
    assert_eq!(to_json, json);
    assert_eq!(serde_json::to_string(value).unwrap(), json);
}

#[test]
fn numbers_are_written_as_to_json_writes_them() {
    // Whole seconds of 2^53 and more: the digits JavaScript prints, not the exact integer.
    let big = [
        (9007199254740992.0, "9007199254740992"),
        (9007199254740994.0, "9007199254740994"),
        (2f64.powi(60), "1152921504606847000"),
        (-(2f64.powi(60)), "-1152921504606847000"),
        (2f64.powi(63), "9223372036854776000"),
        (2f64.powi(64), "18446744073709552000"),
        (-(2f64.powi(64)), "-18446744073709552000"),
        (1e20, "100000000000000000000"),
        (123456789012345680000.0, "123456789012345680000"),
        (-0.0, "0"),
        (0.1, "0.1"),
        (3600.5, "3600.5"),
        (0.00001, "0.00001"),
        (1.5e-7, "1.5e-7"),
    ];
    for (seconds, digits) in big {
        let edit = Edit::set_time(1, seconds);
        text(&edit, edit.to_json(), &format!(r#"{{"op":"set-time","line":1,"seconds":{digits}}}"#));
        // Read back: the same double. (Without its `float_roundtrip` feature `serde_json` may
        // read a long digit string one unit in the last place off; allow exactly that.)
        let Edit::SetTime { seconds: back, .. } = serde_json::from_str(&edit.to_json()).unwrap()
        else {
            panic!("{digits}")
        };
        assert!((back - seconds).abs() <= seconds.abs() * f64::EPSILON, "{digits}: {back}");
        let edit = Edit::shift(seconds);
        text(&edit, edit.to_json(), &format!(r#"{{"op":"shift","seconds":{digits}}}"#));
        let options = TracklistOptions::new().audio_seconds(seconds);
        let json = serde_json::to_string(&options).unwrap();
        assert!(json.contains(&format!(r#""audioSeconds":{digits},"#)), "{json}");
        let back = serde_json::from_str::<TracklistOptions>(&json).unwrap().audio_seconds.unwrap();
        assert!((back - seconds).abs() <= seconds.abs() * f64::EPSILON, "{digits}: {back}");
    }

    // Where `serde_json` has its own float notation the characters can differ from `to_json()`
    // (which writes what JavaScript writes): `1e-6` for `0.000001`, and in serde_json releases
    // that write floats with ryu, `1e21` for `1e+21`. It is the same number and reads back equal from either text.
    for (seconds, js) in [(0.000001, "0.000001"), (0.0000025, "0.0000025"), (1e21, "1e+21")] {
        let edit = Edit::shift(seconds);
        assert_eq!(edit.to_json(), format!(r#"{{"op":"shift","seconds":{js}}}"#));
        let json = serde_json::to_string(&edit).unwrap();
        assert_eq!(serde_json::from_str::<Edit>(&json).unwrap(), edit);
        assert_eq!(serde_json::from_str::<Edit>(&edit.to_json()).unwrap(), edit);
    }
    assert_eq!(
        serde_json::to_string(&Edit::shift(0.000001)).unwrap(),
        r#"{"op":"shift","seconds":1e-6}"#
    );
}

#[test]
fn non_finite_seconds_are_null_and_read_back_as_nan() {
    for seconds in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        let edit = Edit::set_time(2, seconds);
        text(&edit, edit.to_json(), r#"{"op":"set-time","line":2,"seconds":null}"#);
        let back: Edit = serde_json::from_str(&edit.to_json()).unwrap();
        assert!(matches!(back, Edit::SetTime { line: 2, seconds } if seconds.is_nan()), "{back:?}");
        // Either way the edit is invalid (R13.9.6).
        let text = "00:00 A - B\n01:00 C - D";
        assert_eq!(apply_edits(text, &[back]), apply_edits(text, &[edit]));
        assert_eq!(apply_edits(text, &[Edit::shift(seconds)]).warnings[0].line, None);

        let options = TracklistOptions::new().audio_seconds(seconds);
        let json = serde_json::to_string(&options).unwrap();
        assert!(json.contains(r#""audioSeconds":null"#), "{json}");
        // A non-finite length is ignored (R13.10), so reading it back as "not set" is the same.
        assert_eq!(serde_json::from_str::<TracklistOptions>(&json).unwrap().audio_seconds, None);
    }
    assert!(serde_json::from_str::<Edit>(r#"{"op":"set-time","line":2,"seconds":"5"}"#).is_err());
}

#[test]
fn line_numbers_above_2_53_are_js_numbers() {
    let edit = Edit::delete(usize::MAX);
    text(&edit, edit.to_json(), r#"{"op":"delete","line":18446744073709552000}"#);
    assert_eq!(serde_json::from_str::<Edit>(&edit.to_json()).unwrap(), edit);

    let odd = (1usize << 53) + 1;
    for edit in [Edit::set_time(odd, 1.0), Edit::set_rest(odd, "x"), Edit::insert(odd, "x")] {
        let json = edit.to_json();
        assert!(json.contains("9007199254740992"), "{json}");
        assert_eq!(serde_json::to_string(&edit).unwrap(), json);
    }
    let edit = Edit::shift_lines(1.0, [1, odd, usize::MAX]);
    text(
        &edit,
        edit.to_json(),
        r#"{"op":"shift","seconds":1,"lines":[1,9007199254740992,18446744073709552000]}"#,
    );
    assert_eq!(
        serde_json::from_str::<Edit>(&edit.to_json()).unwrap(),
        Edit::shift_lines(1.0, [1, 1 << 53, usize::MAX])
    );
    let edit = Edit::shift_from(1.0, usize::MAX);
    text(&edit, edit.to_json(), r#"{"op":"shift","seconds":1,"from":18446744073709552000}"#);
    assert_eq!(serde_json::from_str::<Edit>(&edit.to_json()).unwrap(), edit);

    // The warning carries the edit's line as a `usize`, and its JSON is the wire's.
    let result = apply_edits("00:00 a - b", &[Edit::delete(usize::MAX), Edit::delete(5)]);
    assert_eq!(result.warnings[0].line, Some(usize::MAX));
    same(&result, result.to_json());
    assert_eq!(
        result.to_json(),
        wire::call(
            "applyEdits",
            "00:00 a - b",
            &none(),
            &none(),
            &crate_json(&[Edit::delete(usize::MAX), Edit::delete(5)])
        )
        .write()
    );

    // A line is a non-negative whole number; `3.0` is one, as in JavaScript.
    assert_eq!(
        serde_json::from_str::<Edit>(r#"{"op":"delete","line":3.0}"#).unwrap(),
        Edit::delete(3)
    );
    for bad in ["-1", "1.5", "\"1\"", "null", "true"] {
        let json = format!(r#"{{"op":"delete","line":{bad}}}"#);
        assert!(serde_json::from_str::<Edit>(&json).is_err(), "{json}");
    }
    assert_eq!(
        serde_json::from_str::<Edit>(r#"{"op":"shift","seconds":1}"#).unwrap(),
        Edit::shift(1.0)
    );
}

#[test]
fn keyword_maps_have_no_repeated_keys() {
    // A repeated key keeps its first place and takes the last value, like a JS object.
    let keywords = KeywordOptions::new()
        .version_head("rerub", VersionType::Remix)
        .version_head("touch", VersionType::Edit)
        .version_head("rerub", VersionType::Vip)
        .junk("out now", JunkKind::Promo)
        .junk("out now", JunkKind::Label);
    let unique = KeywordOptions::new()
        .version_head("rerub", VersionType::Vip)
        .version_head("touch", VersionType::Edit)
        .junk("out now", JunkKind::Label);
    let json = serde_json::to_string(&keywords).unwrap();
    assert_eq!(
        json,
        concat!(
            r#"{"versionHeads":{"rerub":"vip","touch":"edit"},"descriptors":[],"genres":[],"#,
            r#""junk":{"out now":"label"},"featMarkers":[]}"#
        )
    );
    assert_eq!(serde_json::from_str::<KeywordOptions>(&json).unwrap(), unique);
    let repeated = r#"{"versionHeads":{"rerub":"remix","touch":"edit","rerub":"vip"},
        "junk":{"out now":"promo","out now":"label"}}"#;
    assert_eq!(serde_json::from_str::<KeywordOptions>(repeated).unwrap(), unique);
    // All three mean the same options.
    let input = "Noisia - Stigma (Phace Rerub) [Out Now]";
    let with = |k: &KeywordOptions| parse_with(input, &ParseOptions::new().keywords(k.clone()));
    assert_eq!(with(&keywords), with(&unique));
    assert_eq!(with(&unique).versions[0].version_type, VersionType::Vip);
    assert_eq!(
        with(&keywords).to_json(),
        wire::call(
            "parse",
            input,
            &Json::parse(&format!(r#"{{"keywords":{repeated}}}"#)).unwrap(),
            &none(),
            &none()
        )
        .write()
    );
}
