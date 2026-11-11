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
                    same(edit, edit.to_json());
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
