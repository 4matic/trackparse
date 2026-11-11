//! The typed public API: it must agree with the JSON door (`wire`), which the shared fixtures
//! and the cross-port diff exercise.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic, clippy::indexing_slicing)]

use std::time::{Duration, Instant};

use trackparse::json::Json;
use trackparse::{
    all_artists, format_track, format_track_with, normalize, parse, parse_artists,
    parse_artists_with, parse_with, wire, ArtistRole, ArtistSource, FeatPlacement, FormatOptions,
    JoinerStyle, JunkKind, KeywordOptions, Mode, ModeOption, ParseOptions, Parser, SplitAnd,
    ToJson, VersionType, VersionsOption, Warning, SPEC_VERSION,
};

const INPUTS: &[&str] = &[
    "",
    "   ",
    "Noisia - Stigma",
    "Noisia feat. Foreign Beggars - Shellshock (Rockwell Remix) [Official Video]",
    "01. Camo & Krooked - Climax (2010)",
    "[01:23:45] Sub Focus ft. Alice Gold & Kele - Out the Blue (Wilkinson Remix)",
    "Linkin Park - Numb (Official Video) | Warner Records - YouTube",
    "03_some_track_name.mp3",
    "Jay-Z-Numb.flac",
    "Artist \"Quoted Title\" (Lyrics)",
    "Кино - Группа крови (Remastered 2015)",
    "A, B & C vs D x E - T (F Remix / G Bootleg) - Extended Mix",
    "ID - ID",
    "Title prod. by Someone (Explicit)",
    "Unbalanced (group - Title",
    "🎵 Emoji 🎵 - 𝐀𝐁 (👨‍👩‍👧 Remix)",
];

fn json(text: &str) -> Json {
    Json::parse(text).unwrap()
}

fn none() -> Json {
    json("{}")
}

#[test]
fn spec_version_is_set() {
    assert_eq!(SPEC_VERSION.split('.').count(), 3);
}

#[test]
fn typed_parse_matches_the_wire() {
    for input in INPUTS {
        let expected = wire::call("parse", input, &none(), &none(), &json("[]")).write();
        assert_eq!(parse(input).to_json(), expected, "{input:?}");
        assert_eq!(Parser::default().parse(input).to_json(), expected, "{input:?}");
        assert_eq!(parse_with(input, &ParseOptions::default()).to_json(), expected, "{input:?}");

        let artists = wire::call("parseArtists", input, &none(), &none(), &json("[]")).write();
        let typed =
            Json::Arr(parse_artists(input).iter().map(ToJson::to_json_value).collect()).write();
        assert_eq!(typed, artists, "{input:?}");

        let formatted = wire::call("format", input, &none(), &none(), &json("[]"));
        assert_eq!(Json::Str(format_track(&parse(input))), formatted, "{input:?}");

        assert_eq!(
            Json::Str(normalize(input)),
            wire::call("normalize", input, &none(), &none(), &json("[]"))
        );
    }
}

#[test]
fn typed_options_match_the_wire() {
    let cases: Vec<(ParseOptions, &str)> = vec![
        (ParseOptions::new().mode(ModeOption::Auto), r#"{"mode":"auto"}"#),
        (ParseOptions::new().mode(ModeOption::Clean), r#"{"mode":"clean"}"#),
        (ParseOptions::new().mode(ModeOption::Youtube), r#"{"mode":"youtube"}"#),
        (ParseOptions::new().mode(ModeOption::Filename), r#"{"mode":"filename"}"#),
        (ParseOptions::new().mode(ModeOption::Tracklist), r#"{"mode":"tracklist"}"#),
        (
            ParseOptions::new().mode(ModeOption::Youtube).uploader("Linkin Park - Topic"),
            r#"{"mode":"youtube","uploader":"Linkin Park - Topic"}"#,
        ),
        (
            ParseOptions::new()
                .known_artists(["Camo & Krooked", "A, B"])
                .split_and(SplitAnd::Always),
            r#"{"knownArtists":["Camo & Krooked","A, B"],"splitAnd":"always"}"#,
        ),
        (ParseOptions::new().split_and(SplitAnd::Never), r#"{"splitAnd":"never"}"#),
        (
            ParseOptions::new().keywords(
                KeywordOptions::new()
                    .version_head("Rerub", VersionType::Remix)
                    .version_head("7", VersionType::Edit)
                    .descriptor("Chunky")
                    .genre("Neurofunk")
                    .junk("stigma", JunkKind::Promo)
                    .feat_marker("alongside"),
            ),
            r#"{"keywords":{"versionHeads":{"Rerub":"remix","7":"edit"},"descriptors":["Chunky"],
                "genres":["Neurofunk"],"junk":{"stigma":"promo"},"featMarkers":["alongside"]}}"#,
        ),
        (ParseOptions::new().keywords(KeywordOptions::new()), r#"{"keywords":{}}"#),
    ];
    let extra = [
        "Noisia alongside Phace - Stigma (Mefjus Rerub) [Neurofunk]",
        "Title (Chunky Mix) - Stigma",
        "Camo & Krooked and A, B - T (X 7)",
    ];
    for (typed, loose) in &cases {
        let loose = json(loose);
        let parser = Parser::new(typed);
        for input in INPUTS.iter().chain(extra.iter()) {
            let expected = wire::call("parse", input, &loose, &none(), &json("[]")).write();
            assert_eq!(parser.parse(input).to_json(), expected, "{input:?} with {typed:?}");
            assert_eq!(parse_with(input, typed).to_json(), expected, "{input:?} with {typed:?}");
            let artists = wire::call("parseArtists", input, &loose, &none(), &json("[]")).write();
            let got = Json::Arr(
                parse_artists_with(input, typed).iter().map(ToJson::to_json_value).collect(),
            );
            assert_eq!(got.write(), artists, "{input:?} with {typed:?}");
            assert_eq!(parser.parse_artists(input), parse_artists_with(input, typed));
        }
    }
}

#[test]
fn typed_format_options_match_the_wire() {
    let cases: Vec<(FormatOptions, &str)> = vec![
        (FormatOptions::new(), "{}"),
        (FormatOptions::new().feat(FeatPlacement::Source), r#"{"feat":"source"}"#),
        (FormatOptions::new().feat(FeatPlacement::Artist), r#"{"feat":"artist"}"#),
        (FormatOptions::new().feat(FeatPlacement::Title), r#"{"feat":"title"}"#),
        (FormatOptions::new().feat(FeatPlacement::Omit), r#"{"feat":"omit"}"#),
        (FormatOptions::new().feat_marker("w/"), r#"{"featMarker":"w/"}"#),
        (FormatOptions::new().joiners(JoinerStyle::Canonical), r#"{"joiners":"canonical"}"#),
        (FormatOptions::new().joiners(JoinerStyle::Original), r#"{"joiners":"original"}"#),
        (FormatOptions::new().versions(VersionsOption::None), r#"{"versions":"none"}"#),
        (FormatOptions::new().versions(VersionsOption::All), r#"{"versions":"all"}"#),
        (
            FormatOptions::new().producers(false).position(true),
            r#"{"producers":false,"position":true}"#,
        ),
        (
            FormatOptions::new()
                .feat(FeatPlacement::Title)
                .joiners(JoinerStyle::Canonical)
                .position(true),
            r#"{"feat":"title","joiners":"canonical","position":true}"#,
        ),
    ];
    for (typed, loose) in &cases {
        let loose = json(loose);
        for input in INPUTS {
            let expected = wire::call("format", input, &none(), &loose, &json("[]"));
            assert_eq!(
                Json::Str(format_track_with(&parse(input), typed)),
                expected,
                "{input:?} {typed:?}"
            );
        }
    }
}

#[test]
fn fields_are_typed() {
    let track =
        parse("01. Noisia feat. Foreign Beggars - Shellshock (Rockwell Remix) [Official Video]");
    assert_eq!(track.mode, Mode::Youtube);
    assert_eq!(track.position.as_ref().map(|p| p.number), Some(1));
    assert_eq!(track.artists.len(), 2);
    assert_eq!(track.artists[1].role, ArtistRole::Featured);
    assert_eq!(track.artists[1].joiner.as_deref(), Some("feat."));
    assert_eq!(track.artists[1].source, ArtistSource::Artist);
    assert_eq!(track.title, "Shellshock");
    assert_eq!(track.full_title, "Shellshock (Rockwell Remix)");
    assert_eq!(track.versions[0].version_type, VersionType::Remix);
    assert_eq!(track.versions[0].artists[0].role, ArtistRole::Remixer);
    assert_eq!(track.junk[0].kind, JunkKind::Video);
    assert!(track.warnings.is_empty());
    assert_eq!(parse("no separator here").warnings, [Warning::NoSeparator]);

    let names: Vec<String> = all_artists(&track).into_iter().map(|a| a.name).collect();
    assert_eq!(names, ["Noisia", "Foreign Beggars", "Rockwell"]);
}

#[test]
fn parser_is_send_and_sync() {
    fn check<T: Send + Sync>() {}
    check::<Parser>();
}

/// Loose JSON options: a bad value means the default (R0.8), exactly as in the JS reference.
#[test]
fn bad_wire_options_fall_back() {
    let call = |input: &str, options: &str| {
        wire::call("parse", input, &json(options), &none(), &json("[]"))
    };
    let default = call("Noisia - Stigma (Official Video)", "{}");
    for bad in [
        "null",
        "5",
        "\"x\"",
        "[]",
        r#"{"mode":null}"#,
        r#"{"keywords":5,"knownArtists":7,"uploader":3}"#,
    ] {
        assert_eq!(call("Noisia - Stigma (Official Video)", bad), default, "{bad}");
    }
    // An unknown mode skips the youtube detection but not the filename one.
    let odd = call("Noisia - Stigma (Official Video)", r#"{"mode":"bogus"}"#);
    assert_eq!(odd.get("mode"), Some(&Json::str("clean")));
    let odd = call("Noisia - Stigma.mp3", r#"{"mode":7}"#);
    assert_eq!(odd.get("mode"), Some(&Json::str("filename")));
    // keywords values pass through as given, even when they are not a known type or kind.
    let custom = call(
        "A - B (C Zonk) [Blorp]",
        r#"{"keywords":{"versionHeads":{"zonk":"banana"},"junk":{"blorp":"weird"}}}"#,
    );
    assert!(
        custom.write().contains(r#""type":"banana""#)
            && custom.write().contains(r#""kind":"weird""#)
    );
    // The same call must not panic for other functions and unknown ones.
    assert_eq!(wire::call("nope", "x", &none(), &none(), &json("[]")), Json::Null);
}

/// R0.8: total, and no super-linear blowup on 10,000 code points.
#[test]
fn long_inputs_finish_quickly() {
    let n = 10_000;
    let shapes: Vec<String> = vec![
        "a".repeat(n),
        "a ".repeat(n / 2),
        "a, ".repeat(n / 3),
        "a & ".repeat(n / 4),
        "- ".repeat(n / 2),
        " - ".repeat(n / 3),
        "(".repeat(n),
        ")".repeat(n),
        "()".repeat(n / 2),
        "(a) ".repeat(n / 4),
        " by ".repeat(n / 4),
        "x by (a) ".repeat(n / 9),
        "feat. a ".repeat(n / 8),
        "a - b (c Remix) ".repeat(n / 16),
        "Official Video ".repeat(n / 15),
        "\"".repeat(n),
        "| ".repeat(n / 2),
        " | a".repeat(n / 4),
        "1,".repeat(n / 2),
        "🎵".repeat(n),
        "a\u{301}".repeat(n / 2),
        "A-B".repeat(n / 3),
        "a / b; ".repeat(n / 7),
        "2015 ".repeat(n / 5),
    ];
    let modes = [
        ModeOption::Auto,
        ModeOption::Clean,
        ModeOption::Youtube,
        ModeOption::Filename,
        ModeOption::Tracklist,
    ];
    let known = ParseOptions::new().known_artists(["a & a", "a, a", "A-B"]);
    for shape in &shapes {
        let input: String = shape.chars().take(n).collect();
        let started = Instant::now();
        for mode in modes {
            let track = parse_with(&input, &ParseOptions::new().mode(mode).uploader("Uploader"));
            assert_eq!(track.input, input);
            let _ = format_track(&track);
        }
        let _ = parse_with(&input, &known);
        let _ = parse_artists(&input);
        let _ = normalize(&input);
        let elapsed = started.elapsed();
        // Generous: debug builds and busy CI machines. A quadratic path takes far longer.
        let label: String = input.chars().take(24).collect();
        if std::env::var("TRACKPARSE_TIMES").is_ok() {
            println!("{elapsed:>12?}  {label:?}");
        }
        assert!(elapsed < Duration::from_secs(10), "{label:?}… took {elapsed:?}");
    }
}

#[test]
fn numbers_are_written_like_js() {
    let cases: &[(f64, &str)] = &[
        (0.0, "0"),
        (-0.0, "0"),
        (3167.0, "3167"),
        (-5.0, "-5"),
        (7.5, "7.5"),
        (0.001, "0.001"),
        (62.125, "62.125"),
        (1e21, "1e+21"),
        (1e20, "100000000000000000000"),
        (123456789012345680000.0, "123456789012345680000"),
        (1.5e300, "1.5e+300"),
        (-1e300, "-1e+300"),
        (1e-6, "0.000001"),
        (1e-7, "1e-7"),
        (1.25e-9, "1.25e-9"),
        (9007199254740993.0, "9007199254740992"),
        (f64::INFINITY, "null"),
        (f64::NAN, "null"),
    ];
    for (n, text) in cases {
        assert_eq!(Json::Num(*n).write(), *text);
    }
}

#[test]
fn enums_have_names_both_ways() {
    for role in ArtistRole::ALL {
        assert_eq!(ArtistRole::from_name(role.as_str()), Some(*role));
        assert_eq!(role.to_string().parse::<ArtistRole>(), Ok(*role));
    }
    assert_eq!(VersionType::ALL.len(), VersionType::NAMES.len());
    assert_eq!(VersionType::NAMES[24], "spedUp");
    assert_eq!("spedUp".parse::<VersionType>(), Ok(VersionType::SpedUp));
    // Names are exact: no case folding, no trimming.
    let err = "Remix".parse::<VersionType>().unwrap_err();
    assert_eq!(err.name(), "Remix");
    assert_eq!(err.to_string(), "unknown VersionType name: \"Remix\"");
    let boxed: Box<dyn std::error::Error + Send + Sync> = Box::new(err);
    assert!(boxed.to_string().contains("VersionType"));
    assert_eq!(ModeOption::default(), ModeOption::Auto);
    assert_eq!(SplitAnd::default(), SplitAnd::Auto);
    assert_eq!(FeatPlacement::default(), FeatPlacement::Source);
    assert_eq!(JoinerStyle::default(), JoinerStyle::Original);
    assert_eq!(VersionsOption::default(), VersionsOption::All);
}

#[test]
fn to_json_needs_no_import() {
    mod inner {
        // No `ToJson` in scope here.
        pub fn json(input: &str) -> String {
            trackparse::parse(input).to_json()
        }
    }
    let track = parse("Noisia - Stigma");
    assert_eq!(inner::json("Noisia - Stigma"), ToJson::to_json(&track));
    assert_eq!(track.to_json_value().to_string(), track.to_json());
}
