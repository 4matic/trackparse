//! SPEC.md "Invariants" for single tracks, checked on generated strings.
//!
//! The generator is a small deterministic PRNG over the fragments of the JS property tests
//! (`js/test/properties.test.ts`) plus random code points, so a failure is reproducible from its
//! case number. The tracklist invariants (R13.11) are in `tracklist_properties.rs`.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic, clippy::indexing_slicing)]

use std::collections::HashSet;
use std::time::{Duration, Instant};

use trackparse::{
    all_artists, format_track, format_track_with, normalize, parse, parse_artists,
    parse_artists_with, parse_with, ArtistRole, FeatPlacement, FormatOptions, JoinerStyle, Junk,
    JunkKind, Mode, ModeOption, ParseOptions, ParsedTrack, Parser, Position, SplitAnd,
    VersionDelimiter,
};

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

/// Characters that exercise every scanner branch (the `tricky` set of the JS tests; its lone
/// surrogate cannot be in a Rust string and is U+FFFD here).
const TRICKY: &[&str] = &[
    " ",
    "  ",
    "-",
    " - ",
    " – ",
    "—",
    "(",
    ")",
    "[",
    "]",
    "{",
    "}",
    "\"",
    "'",
    "“",
    "|",
    " | ",
    ",",
    ";",
    "&",
    " & ",
    " x ",
    " feat. ",
    " ft ",
    "prod. by ",
    " by ",
    ".",
    ":",
    "_",
    "0",
    "1",
    "01. ",
    "3:45 ",
    "Remix",
    " Mix",
    "VIP",
    "Official Video",
    "ID",
    "\u{200b}",
    "\u{a0}",
    "\u{3000}",
    "é",
    "e\u{301}",
    "𝄞",
    "\u{fffd}",
    "×",
    "／",
    ".mp3",
];

/// One random code point, biased to the ranges the rules look at.
fn any_char(rng: &mut Rng) -> char {
    let cp = match rng.below(10) {
        0..=3 => rng.below(0x80) as u32,
        4 => rng.below(0x100) as u32,
        5 => 0x300 + rng.below(0x70) as u32,  // combining marks
        6 => 0x2000 + rng.below(0x70) as u32, // spaces, dashes, quotes, invisibles
        7 => rng.below(0x1_0000) as u32,
        8 => 0xff00 + rng.below(0xf0) as u32, // fullwidth forms
        _ => rng.below(0x11_0000) as u32,
    };
    char::from_u32(cp).unwrap_or('\u{fffd}')
}

fn random_chars(rng: &mut Rng, max: usize) -> String {
    (0..rng.below(max + 1)).map(|_| any_char(rng)).collect()
}

fn noisy_string(rng: &mut Rng) -> String {
    let mut out = String::new();
    for _ in 0..rng.below(25) {
        if rng.chance(50) {
            out.push_str(rng.pick(TRICKY));
        } else {
            out.push_str(&random_chars(rng, 4));
        }
    }
    out
}

fn any_string(rng: &mut Rng) -> String {
    if rng.chance(50) {
        random_chars(rng, 60)
    } else {
        noisy_string(rng)
    }
}

const ARTISTS: &[&str] =
    &["Noisia", "Phace", "Fox Stevenson", "Sub Focus", "Mefjus", "Camo & Krooked", "Alix Perez"];
const TITLES: &[&str] =
    &["Tentacles", "Bruises", "Out the Blue", "Dead Limit", "Stigma", "Blind Faith"];
const REMIXERS: &[&str] = &["Skrillex", "Magnetude", "Wilkinson", "Break"];
const JOINERS: &[&str] = &[" & ", ", ", " x ", " vs. "];

/// A track-shaped input for the metamorphic and round-trip properties.
fn track_string(rng: &mut Rng) -> String {
    let mut artists: Vec<&str> = Vec::new();
    for _ in 0..1 + rng.below(3) {
        let a = rng.pick(ARTISTS);
        if !artists.contains(&a) {
            artists.push(a);
        }
    }
    let mut s = artists.join(rng.pick(JOINERS));
    if rng.chance(50) {
        let feat = rng.pick(ARTISTS);
        if !artists.contains(&feat) {
            s.push_str(&format!(" feat. {feat}"));
        }
    }
    s.push_str(&format!(" - {}", rng.pick(TITLES)));
    if rng.chance(50) {
        s.push_str(&format!(" ({} Remix)", rng.pick(REMIXERS)));
    }
    s
}

/// A track-shaped input that parses without warnings.
fn quiet_track(rng: &mut Rng) -> String {
    loop {
        let s = track_string(rng);
        if parse(&s).warnings.is_empty() {
            return s;
        }
    }
}

fn without_input(mut track: ParsedTrack) -> ParsedTrack {
    track.input = String::new();
    track
}

/// Every public single-track function, in every mode, with the string as its own options.
fn exercise(s: &str) {
    let default = parse(s);
    let _ = (format_track(&default), all_artists(&default), default.to_json());
    let _ = (parse_artists(s), normalize(s));
    for mode in ModeOption::ALL {
        let options = ParseOptions::new()
            .mode(*mode)
            .uploader(s)
            .known_artists([s, "a", ""])
            .split_and(SplitAnd::Always);
        let parser = Parser::new(&options);
        let track = parser.parse(s);
        assert_eq!(track.input, s);
        assert_eq!(track, parse_with(s, &options));
        assert_eq!(parser.parse_artists(s), parse_artists_with(s, &options));
        for feat in FeatPlacement::ALL {
            let format = FormatOptions::new()
                .feat(*feat)
                .feat_marker(s)
                .joiners(JoinerStyle::Canonical)
                .position(true);
            let _ = format_track_with(&track, &format);
        }
    }
}

#[test]
fn totality_never_panics() {
    let mut rng = Rng(1);
    for _ in 0..3000 {
        exercise(&any_string(&mut rng));
    }
    for s in ["", " ", "\0", "\u{feff}", "-", " - ", "(", ")", "((((", "]]]]", "\u{10ffff}"] {
        exercise(s);
    }
    for fragment in TRICKY {
        exercise(fragment);
        exercise(&fragment.repeat(50));
    }
}

/// The blocks of the JS suite, plus the credit shapes that once made dedup quadratic.
const BLOCKS: &[&str] = &[
    "(",
    "a - ",
    "A & B, ",
    "feat. X ",
    "[Remix] ",
    "-a",
    " by x",
    "\"q\" ",
    "xxxxxxx",
    "prod. A feat. B ",
    "a by me ",
    "(x) - ",
    "Official Video ",
    "x - Official Video ",
    "Remix - ",
    "a feat. ",
    "a & ",
    "(a remix) ",
];

fn repeated(block: &str, code_points: usize) -> String {
    block.chars().cycle().take(code_points).collect()
}

fn timed_parse(input: &str, options: &ParseOptions) -> Duration {
    let start = Instant::now();
    let track = parse_with(input, options);
    let elapsed = start.elapsed();
    assert_eq!(track.input, input);
    elapsed
}

#[test]
fn totality_ten_thousand_code_points() {
    // Invariant 1 asks for "well under 100 ms"; an optimized build needs a few milliseconds. The
    // ceiling is generous because tests run unoptimized and on shared CI machines: it catches
    // super-linear blowups, not noise.
    for block in BLOCKS {
        let input = repeated(block, 10_000);
        for mode in [ModeOption::Auto, ModeOption::Youtube, ModeOption::Filename] {
            let elapsed = timed_parse(&input, &ParseOptions::new().mode(mode));
            assert!(elapsed < Duration::from_secs(2), "{block:?} in {mode}: {elapsed:?}");
        }
    }
}

#[test]
fn repeated_fragments_scale_below_quadratic() {
    // Four times the input: linear work takes about 4 times as long, quadratic work 16 times.
    // Thousands of duplicate credits (`a feat. a feat. …`) were quadratic in R9.2 before the
    // per-list index in `assemble.rs` (a ratio of 15 here). The better of two runs keeps
    // scheduler noise out.
    fn best(run: impl Fn() -> usize) -> f64 {
        let once = || {
            let start = Instant::now();
            assert!(run() > 0);
            start.elapsed().as_secs_f64()
        };
        once().min(once())
    }
    let blocks = ["a feat. ", "A feat. a, ", "(feat. a) ", "a & "];
    for block in blocks {
        let (small, large) = (repeated(block, 40_000), repeated(block, 160_000));
        let credits = best(|| parse_artists(&large).len()) / best(|| parse_artists(&small).len());
        let track = best(|| parse(&large).input.len()) / best(|| parse(&small).input.len());
        println!("{block:?}: parse_artists x{credits:.1}, parse x{track:.1}");
        assert!(credits < 8.0, "{block:?}: parse_artists takes {credits:.1} times as long");
        assert!(track < 8.0, "{block:?}: parse takes {track:.1} times as long");
    }
}

#[test]
fn repeated_trailing_junk_is_linear() {
    let input = format!("A - T {}", "Official Video ".repeat(2_666));
    let start = Instant::now();
    let track = parse_with(&input, &ParseOptions::new().mode(ModeOption::Youtube));
    assert_eq!(track.title, "T");
    assert_eq!(track.junk.len(), 2_666);
    assert!(start.elapsed() < Duration::from_secs(2), "{:?}", start.elapsed());
}

#[test]
fn normalize_is_idempotent() {
    let mut rng = Rng(2);
    for case in 0..4000 {
        let s = any_string(&mut rng);
        let once = normalize(&s);
        assert_eq!(normalize(&once), once, "case {case}: {s:?}");
    }
}

#[test]
fn noise_insensitive() {
    let mut rng = Rng(3);
    for case in 0..300 {
        let s = track_string(&mut rng);
        let base = without_input(parse(&s));
        let cut = {
            let at = rng.below(s.chars().count() + 1);
            let byte = s.char_indices().nth(at).map_or(s.len(), |(i, _)| i);
            format!("{}\u{200b}{}", &s[..byte], &s[byte..])
        };
        let noisy = [s.replace(' ', "  "), s.replace(" - ", " – "), s.replace(" - ", " — "), cut];
        for n in noisy {
            assert_eq!(without_input(parse(&n)), base, "case {case}: {n:?}");
        }
    }
}

/// R0.2 whitespace.
fn is_ws(c: char) -> bool {
    matches!(c,
        '\u{9}'..='\u{d}' | ' ' | '\u{85}' | '\u{a0}' | '\u{1680}' | '\u{2000}'..='\u{200a}'
        | '\u{2028}' | '\u{2029}' | '\u{202f}' | '\u{205f}' | '\u{3000}')
}

#[test]
fn artist_hygiene() {
    let mut rng = Rng(5);
    for case in 0..3000 {
        let s = if rng.chance(70) { any_string(&mut rng) } else { track_string(&mut rng) };
        let track = parse(&s);
        let mut seen: HashSet<(bool, &str)> = HashSet::new();
        for artist in &track.artists {
            let name = artist.name.as_str();
            assert!(!name.is_empty(), "case {case}: empty name in {s:?}");
            assert_eq!(name.trim_matches(is_ws), name, "case {case}: untrimmed name in {s:?}");
            // R9.2: producers are deduped only among producers. (The R0.4 dedup key is not
            // public; names that are equal as written share it.)
            let class = artist.role == ArtistRole::Producer;
            assert!(seen.insert((class, name)), "case {case}: duplicate {name:?} in {s:?}");
        }
    }
}

/// Invariant 6: the words of `normalize(input)`, minus separators, joiners, feat markers and
/// bracket characters. Neither the JS nor the Python suite has this property; it is checked here
/// as the spec states it, on track-shaped inputs with an optional position and junk group.
fn content_words(normalized: &str) -> Vec<String> {
    normalized
        .split(is_ws)
        .filter(|w| !matches!(*w, "-" | "&" | "x" | "vs." | "feat."))
        .map(|w| w.trim_matches(|c| matches!(c, '(' | ')' | '[' | ']' | ',' | '.')).to_string())
        .filter(|w| !w.is_empty())
        .collect()
}

fn output_strings(track: &ParsedTrack) -> Vec<&str> {
    let mut out = vec![track.title.as_str(), track.full_title.as_str()];
    out.extend(track.artists.iter().map(|a| a.name.as_str()));
    for version in &track.versions {
        out.push(&version.raw);
        out.extend(version.artists.iter().map(|a| a.name.as_str()));
    }
    out.extend(track.junk.iter().map(|j| j.raw.as_str()));
    out.extend(track.position.iter().map(|p| p.raw.as_str()));
    out.extend(track.timestamp.iter().map(|t| t.raw.as_str()));
    out
}

#[test]
fn conservation_no_word_goes_missing() {
    let mut rng = Rng(6);
    for case in 0..1000 {
        let mut s = track_string(&mut rng);
        if rng.chance(30) {
            s = format!("{}{s}", rng.pick(&["01. ", "7. ", "12. ", "[03:45] ", "1:02:03 "]));
        }
        if rng.chance(30) {
            s.push_str(rng.pick(&[" (Official Video)", " [HD]", " [Free Download]", " (VIP)"]));
        }
        let track = parse(&s);
        let fields = output_strings(&track);
        for word in content_words(&normalize(&s)) {
            assert!(
                fields.iter().any(|f| f.contains(word.as_str())),
                "case {case}: {word:?} of {s:?} is in no output field: {}",
                track.to_json()
            );
        }
    }
}

#[test]
fn metamorphic_position_prefix_only_sets_position() {
    let mut rng = Rng(7);
    for case in 0..200 {
        let s = quiet_track(&mut rng);
        let plain = without_input(parse(&s));
        let mut numbered = without_input(parse(&format!("01. {s}")));
        assert_eq!(
            numbered.position.take(),
            Some(Position { raw: "01".into(), number: 1 }),
            "case {case}: {s:?}"
        );
        assert_eq!(numbered, plain, "case {case}: {s:?}");
    }
}

#[test]
fn metamorphic_official_video_only_adds_junk() {
    let mut rng = Rng(8);
    for case in 0..200 {
        let s = quiet_track(&mut rng);
        let plain = without_input(parse(&s));
        let mut noisy = without_input(parse(&format!("{s} (Official Video)")));
        let mut junk = plain.junk.clone();
        junk.push(Junk { raw: "Official Video".into(), kind: JunkKind::Video });
        assert_eq!(noisy.junk, junk, "case {case}: {s:?}");
        assert!(noisy.mode == plain.mode || noisy.mode == Mode::Youtube, "case {case}: {s:?}");
        noisy.junk = plain.junk.clone();
        noisy.mode = plain.mode;
        assert_eq!(noisy, plain, "case {case}: {s:?}");
    }
}

/// A warning-free track that ends with ` (X Remix)`, cut before that group.
fn quiet_remix(rng: &mut Rng) -> (String, String) {
    loop {
        let s = quiet_track(rng);
        if s.ends_with(" Remix)") {
            let open = s.rfind('(').unwrap();
            return (s[..open].to_string(), s[open + 1..s.len() - 1].to_string());
        }
    }
}

#[test]
fn metamorphic_square_brackets_change_only_the_delimiter() {
    let mut rng = Rng(9);
    for case in 0..200 {
        let (head, group) = quiet_remix(&mut rng);
        let round = without_input(parse(&format!("{head}({group})")));
        let mut square = without_input(parse(&format!("{head}[{group}]")));
        assert_eq!(square.versions.last().unwrap().delimiter, VersionDelimiter::Square);
        for version in &mut square.versions {
            version.delimiter = VersionDelimiter::Paren;
        }
        square.full_title = round.full_title.clone();
        assert_eq!(square, round, "case {case}: {head:?} {group:?}");
    }
}

#[test]
fn metamorphic_keyword_case_changes_only_raw() {
    let mut rng = Rng(10);
    for case in 0..200 {
        let (head, group) = quiet_remix(&mut rng);
        let lower = without_input(parse(&format!("{head}({group})")));
        let shouted = group.replace(" Remix", " REMIX");
        let mut upper = without_input(parse(&format!("{head}({shouted})")));
        assert_eq!(upper.versions.last().unwrap().raw, shouted, "case {case}");
        for (version, same) in upper.versions.iter_mut().zip(&lower.versions) {
            version.raw = same.raw.clone();
        }
        upper.full_title = lower.full_title.clone();
        assert_eq!(upper, lower, "case {case}: {head:?} {group:?}");
    }
}

/// What R11.6 promises to keep: names with roles, the title, and each version's type and names.
type Shape = (Vec<(String, ArtistRole)>, String, Vec<(String, Vec<String>)>);

fn shape(track: &ParsedTrack) -> Shape {
    (
        track.artists.iter().map(|a| (a.name.clone(), a.role)).collect(),
        track.title.clone(),
        track
            .versions
            .iter()
            .map(|v| {
                (v.version_type.to_string(), v.artists.iter().map(|a| a.name.clone()).collect())
            })
            .collect(),
    )
}

#[test]
fn round_trip_through_format_track() {
    let mut rng = Rng(11);
    for case in 0..500 {
        let s = quiet_track(&mut rng);
        let first = parse(&s);
        let formatted = format_track(&first);
        let second = parse(&formatted);
        assert_eq!(shape(&second), shape(&first), "case {case}: {s:?} -> {formatted:?}");
    }
}
