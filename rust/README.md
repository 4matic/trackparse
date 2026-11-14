# trackparse

[![crates.io](https://img.shields.io/crates/v/trackparse?logo=rust&logoColor=white)](https://crates.io/crates/trackparse)
[![docs.rs](https://img.shields.io/docsrs/trackparse?logo=docsdotrs&logoColor=white)](https://docs.rs/trackparse)
[![MSRV](https://img.shields.io/crates/msrv/trackparse?logo=rust&logoColor=white)](https://crates.io/crates/trackparse)
[![tests](https://img.shields.io/github/actions/workflow/status/4matic/trackparse/rust.yml?branch=main&label=tests&logo=github)](https://github.com/4matic/trackparse/actions/workflows/rust.yml)
[![license](https://img.shields.io/badge/license-MIT-blue)](https://github.com/4matic/trackparse/blob/main/LICENSE)

**[Documentation](https://4matic.github.io/trackparse/)** · **[Getting started](https://4matic.github.io/trackparse/start/getting-started/)** · **[Playground](https://4matic.github.io/trackparse/playground/)** · **[API on docs.rs](https://docs.rs/trackparse)** · **[Changelog](https://github.com/4matic/trackparse/blob/main/rust/CHANGELOG.md)**

**Turn messy track names into structured data: who made it, who's featured, who remixed it, and
what's just noise.**

```text
01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]
```

Most title parsers split that into `"Wilkinson ft. Becky Hill & Tom Cane"` and
`"Afterglow (Sub Focus VIP Remix)"` and stop. Others "clean" it and throw the feat and the remix
away. trackparse keeps all of it. Everything it recognizes goes into a field, and anything it
doesn't recognize stays in the title, word for word
([try it in the playground](https://4matic.github.io/trackparse/playground/#track=01.%20Wilkinson%20ft.%20Becky%20Hill%20%26%20Tom%20Cane%20-%20Afterglow%20%28Sub%20Focus%20VIP%20Remix%29%20%5BOfficial%20Video%5D)).

This is the Rust port of [trackparse](https://github.com/4matic/trackparse). Its behaviour is
defined by a [shared spec](https://4matic.github.io/trackparse/reference/spec/) and pinned
by [13,130 fixtures](https://4matic.github.io/trackparse/project/testing/). It passes all of them, and for every fixture input its output is byte-identical
to the [JavaScript, Python and Go packages](#other-languages).

- Rust 1.74 or later (the minimum supported version; CI runs the test suite on 1.74 and on stable)
- One dependency (`unicode-normalization`); `serde` is optional
- No `unsafe`, no regular expressions
- Never panics and never returns an error: any `&str` gives a result ([Errors](#errors))

## Install

```sh
cargo add trackparse
# with Serialize / Deserialize on the types
cargo add trackparse --features serde
```

## Quick start

**Docs:** [Getting started](https://4matic.github.io/trackparse/start/getting-started/)

```rust
use trackparse::{format_track_with, parse, FormatOptions, JoinerStyle, JunkKind, Mode, VersionType};

let track = parse(
    "01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]",
);

let credits: Vec<String> = track.artists.iter().map(|a| format!("{} ({})", a.name, a.role)).collect();
assert_eq!(credits, ["Wilkinson (primary)", "Becky Hill (featured)", "Tom Cane (featured)"]);
assert_eq!(track.title, "Afterglow");
assert_eq!(track.full_title, "Afterglow (Sub Focus VIP Remix)");
assert_eq!(track.versions[0].version_type, VersionType::Remix);
assert_eq!(track.versions[0].artists[0].name, "Sub Focus");
assert_eq!(track.versions[0].modifiers, ["vip"]);
assert_eq!(track.junk[0].raw, "Official Video");
assert_eq!(track.junk[0].kind, JunkKind::Video);
assert_eq!(track.position.as_ref().map(|p| p.number), Some(1));
assert_eq!(track.mode, Mode::Youtube);

let options = FormatOptions::new().joiners(JoinerStyle::Canonical);
assert_eq!(
    format_track_with(&track, &options),
    "Wilkinson feat. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix)"
);
```

[`to_json()`](https://docs.rs/trackparse/latest/trackparse/struct.ParsedTrack.html#method.to_json) returns the spec's JSON form as compact
text, with the same camelCase keys the JavaScript package uses ([JSON and serde](#json-and-serde)):

```rust
let track = trackparse::parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus VIP Remix) [HD]");
println!("{}", track.to_json());
```

<details>
<summary>Output</summary>

```json
{
  "input": "Wilkinson ft. Becky Hill - Afterglow (Sub Focus VIP Remix) [HD]",
  "mode": "youtube",
  "position": null,
  "timestamp": null,
  "artists": [
    { "name": "Wilkinson", "role": "primary", "joiner": null, "source": "artist" },
    { "name": "Becky Hill", "role": "featured", "joiner": "ft.", "source": "artist" }
  ],
  "title": "Afterglow",
  "fullTitle": "Afterglow (Sub Focus VIP Remix)",
  "versions": [
    {
      "type": "remix",
      "raw": "Sub Focus VIP Remix",
      "artists": [{ "name": "Sub Focus", "role": "remixer", "joiner": null, "source": "version" }],
      "modifiers": ["vip"],
      "descriptor": null,
      "year": null,
      "unknownArtist": false,
      "delimiter": "("
    }
  ],
  "year": null,
  "flags": { "explicit": false, "clean": false, "unknownArtist": false, "unknownTitle": false },
  "junk": [{ "raw": "HD", "kind": "quality" }],
  "warnings": []
}
```

(Reformatted for width; the keys and values are exactly what `to_json()` returns.)

</details>

## API

**Docs:** [Other functions](https://4matic.github.io/trackparse/reference/functions/)

```text
parse(input: &str) -> ParsedTrack
parse_with(input: &str, options: &ParseOptions) -> ParsedTrack
parse_artists(input: &str) -> Vec<Artist>
parse_artists_with(input: &str, options: &ParseOptions) -> Vec<Artist>
format_track(track: &ParsedTrack) -> String
format_track_with(track: &ParsedTrack, options: &FormatOptions) -> String
normalize(input: &str) -> String
all_artists(track: &ParsedTrack) -> Vec<Artist>
Parser::new(options: &ParseOptions) -> Parser    // parser.parse(..), parser.parse_artists(..)
SPEC_VERSION: &str

tracklist::parse_tracklist(text: &str) -> Tracklist
tracklist::parse_tracklist_with(text: &str, options: &TracklistOptions) -> Tracklist
tracklist::apply_edits(text: &str, edits: &[Edit]) -> EditResult
tracklist::apply_edits_with(text: &str, edits: &[Edit], options: &TracklistOptions) -> EditResult
```

Every function has a plain form with the default options and a `_with` form that takes them. Each
has a section below, a page on [docs.rs](https://docs.rs/trackparse) and one in the
[API reference](https://4matic.github.io/trackparse/reference/functions/), where every example has a Rust tab; the tracklist
functions are under [Tracklists](#tracklists).

### `parse` and `ParseOptions`

**Docs:** [parse and ParsedTrack](https://4matic.github.io/trackparse/reference/parse/) · [Options](https://4matic.github.io/trackparse/reference/options/) · [`parse`](https://docs.rs/trackparse/latest/trackparse/fn.parse.html), [`parse_with`](https://docs.rs/trackparse/latest/trackparse/fn.parse_with.html) and [`ParseOptions`](https://docs.rs/trackparse/latest/trackparse/struct.ParseOptions.html) on docs.rs

Parses one track string, running [rules R1 to R9](https://4matic.github.io/trackparse/reference/spec/#r1-normalization) of the
spec in order.

| Option | Default | |
|---|---|---|
| [`mode`](https://4matic.github.io/trackparse/guides/modes/) | `ModeOption::Auto` | `Clean`, `Youtube`, `Filename`, `Tracklist`, or `Auto` to detect from the string. See [Modes](#modes) |
| [`uploader`](https://4matic.github.io/trackparse/guides/modes/#the-uploader-fallback) | `None` | YouTube channel name, used as the artist when there's no separator (`- Topic` and `VEVO` are dropped) |
| [`known_artists`](https://4matic.github.io/trackparse/guides/artists/#knownartists) | empty | Names that are never split. There is no built-in list |
| [`split_and`](https://4matic.github.io/trackparse/guides/artists/#and-is-special) | `SplitAnd::Auto` | `Auto` splits on `and` only after a comma (`A, B and C`), so `Simon and Garfunkel` stays whole. Also `Always`, `Never` |
| [`keywords`](https://4matic.github.io/trackparse/reference/options/#keywords) | `None` | Extra vocabulary, added to the built-in lists (see below) |

Options are built with chained setters. The structs are `#[non_exhaustive]`, so start from
`ParseOptions::new()` (or `default()`); the fields are public and can also be assigned.

```rust
use trackparse::{parse, parse_with, ParseOptions, SplitAnd};

let names = |track: &trackparse::ParsedTrack| -> Vec<String> {
    track.artists.iter().map(|a| a.name.clone()).collect()
};

assert_eq!(names(&parse("Chase & Status - Baddadan")), ["Chase", "Status"]);
let options = ParseOptions::new().known_artists(["Chase & Status"]);
assert_eq!(names(&parse_with("Chase & Status - Baddadan", &options)), ["Chase & Status"]);

let options = ParseOptions::new().split_and(SplitAnd::Always);
assert_eq!(
    names(&parse_with("Simon and Garfunkel - Mrs. Robinson", &options)),
    ["Simon", "Garfunkel"]
);
```

[`KeywordOptions`](https://docs.rs/trackparse/latest/trackparse/struct.KeywordOptions.html) adds words to the built-in lists
([your own words](https://4matic.github.io/trackparse/guides/versions/#your-own-words)):

| Setter | Field | |
|---|---|---|
| `version_head(word, VersionType)` | `version_heads` | Word that sets a version type (`"rerub"` as `VersionType::Rework`) |
| `descriptor(word)` | `descriptors` | Words kept as a version's `descriptor` |
| `genre(word)` | `genres` | Genre words |
| `junk(phrase, JunkKind)` | `junk` | Phrase to junk kind (`"some label rip"` as `JunkKind::Label`) |
| `feat_marker(marker)` | `feat_markers` | Extra feat markers |

```rust
use trackparse::{parse, parse_with, JunkKind, KeywordOptions, ParseOptions};

assert_eq!(parse("Artist - Song [Some Label Rip]").title, "Song [Some Label Rip]");

let keywords = KeywordOptions::new().junk("some label rip", JunkKind::Label);
let track = parse_with("Artist - Song [Some Label Rip]", &ParseOptions::new().keywords(keywords));
assert_eq!(track.title, "Song");
assert_eq!(track.junk[0].raw, "Some Label Rip");
assert_eq!(track.junk[0].kind, JunkKind::Label);
```

### `ParsedTrack`

**Docs:** [ParsedTrack fields](https://4matic.github.io/trackparse/reference/parse/#parsedtrack) · [`ParsedTrack`](https://docs.rs/trackparse/latest/trackparse/struct.ParsedTrack.html) on docs.rs

| Field | Type | |
|---|---|---|
| `input` | `String` | The original string, untouched |
| `mode` | `Mode` | Resolved mode: `Clean`, `Youtube`, `Filename` or `Tracklist` |
| `position` | `Option<Position>` | Track number: `Position { raw: "01", number: 1 }` |
| `timestamp` | `Option<Timestamp>` | Leading cue time: `[12:34]` gives `Timestamp { raw: "12:34", seconds: 754 }` |
| `artists` | `Vec<Artist>` | Primary, featured and producer credits. Remixers live in `versions` |
| `title` | `String` | Clean title. Brackets it doesn't recognize stay in |
| `full_title` | `String` | `title` with the versions re-attached |
| `versions` | `Vec<Version>` | Remixes and other versions, left to right |
| `year` | `Option<u32>` | From `(2015)` or `- 2015` |
| `flags` | `Flags` | `explicit`, `clean`, `unknown_artist`, `unknown_title` |
| `junk` | `Vec<Junk>` | What was taken out, each with a `kind` |
| `warnings` | `Vec<Warning>` | Heuristics that fired |

`Artist { name, role, joiner, source }`:

- `role` is `ArtistRole::Primary`, `Featured`, `Remixer` or `Producer`.
- `joiner` is the text before the name as written (`"&"`, `","`, `"ft."`, `"prod. by"`), or `None`
  for the first name in a list.
- `source` is where the credit came from: `ArtistSource::Artist` (left of the dash), `Title`
  (right of it) or `Version`.

`Version { version_type, raw, artists, modifiers, descriptor, year, unknown_artist, delimiter }`
(`version_type` is `type` in JSON):

```rust
use trackparse::{parse, VersionDelimiter, VersionType};

let track = parse("Pendulum - Watercolour (Extended VIP Mix)");
let version = &track.versions[0];
assert_eq!(version.version_type, VersionType::Vip);
assert_eq!(version.raw, "Extended VIP Mix");
assert_eq!(version.modifiers, ["extended"]);
assert_eq!(version.delimiter, VersionDelimiter::Paren);

let track = parse("Queen - Bohemian Rhapsody (Live at Wembley 1986)");
let version = &track.versions[0];
assert_eq!(version.version_type, VersionType::Live);
assert_eq!(version.descriptor.as_deref(), Some("at Wembley"));
assert_eq!(version.year, Some(1986));
```

Version types, junk kinds, warnings, roles and modes are enums
([`VersionType`](https://docs.rs/trackparse/latest/trackparse/enum.VersionType.html), [`JunkKind`](https://docs.rs/trackparse/latest/trackparse/enum.JunkKind.html),
[`Warning`](https://docs.rs/trackparse/latest/trackparse/enum.Warning.html), [`ArtistRole`](https://docs.rs/trackparse/latest/trackparse/enum.ArtistRole.html),
[`ArtistSource`](https://docs.rs/trackparse/latest/trackparse/enum.ArtistSource.html), [`Mode`](https://docs.rs/trackparse/latest/trackparse/enum.Mode.html),
[`ModeOption`](https://docs.rs/trackparse/latest/trackparse/enum.ModeOption.html), [`VersionDelimiter`](https://docs.rs/trackparse/latest/trackparse/enum.VersionDelimiter.html)),
listed in the [API reference](https://4matic.github.io/trackparse/reference/parse/) ([the 27 version types](https://4matic.github.io/trackparse/guides/versions/#the-27-types),
[junk kinds](https://4matic.github.io/trackparse/reference/parse/#junk-kinds)). Each has `as_str()` (the name
used in the spec and in JSON), `from_name()`, `ALL`, `Display` and `FromStr`:

```rust
use trackparse::VersionType;

assert_eq!(VersionType::SpedUp.as_str(), "spedUp");
assert_eq!(VersionType::from_name("remix"), Some(VersionType::Remix));
assert_eq!("bootleg".parse::<VersionType>(), Ok(VersionType::Bootleg));
assert_eq!(VersionType::ALL.len(), 27);
```

Most of these enums are `#[non_exhaustive]`: a later spec version can add a version type or a
warning, so a `match` on them needs a `_` arm.

### JSON and serde

**Docs:** [Serializing to JSON](https://4matic.github.io/trackparse/reference/parse/#serializing-to-json)

Every result type has [`to_json()`](https://docs.rs/trackparse/latest/trackparse/struct.ParsedTrack.html#method.to_json), which writes compact JSON matching
[`parsed-track.schema.json`](https://github.com/4matic/trackparse/blob/main/spec/schema/parsed-track.schema.json)
and
[`tracklist.schema.json`](https://github.com/4matic/trackparse/blob/main/spec/schema/tracklist.schema.json)
(`fullTitle`, `unknownArtist`, ...). The bytes are the same as `JSON.stringify(parse(s))` in
JavaScript. It needs no feature and no dependency.

With the `serde` feature, the result types, the options and `Edit` implement `Serialize` and
`Deserialize` with the same keys, so `serde_json` writes the same text and reads it back:

```rust
use trackparse::tracklist::Edit;
use trackparse::{parse, ModeOption, ParseOptions, ParsedTrack};

let track = parse("Noisia - Stigma (2015)");
let json = serde_json::to_string(&track).unwrap();
assert_eq!(json, track.to_json());
assert_eq!(serde_json::from_str::<ParsedTrack>(&json).unwrap(), track);

let options: ParseOptions =
    serde_json::from_str(r#"{"mode":"youtube","knownArtists":["Chase & Status"]}"#).unwrap();
assert_eq!(options, ParseOptions::new().mode(ModeOption::Youtube).known_artists(["Chase & Status"]));

let edit: Edit = serde_json::from_str(r#"{"op":"set-time","line":3,"seconds":90}"#).unwrap();
assert_eq!(edit, Edit::set_time(3, 90.0));
assert_eq!(serde_json::to_string(&edit).unwrap(), r#"{"op":"set-time","line":3,"seconds":90}"#);
```

Numbers are written as `to_json()` writes them: whole seconds are integers (`90`, not `90.0`), and
a value of 2^53 and more has the digits JavaScript prints. For every result the parser can produce
the two texts are equal. The edge cases, all in an `Edit` or an option you build yourself:

- Seconds from 0.000001 up to 0.00001 are written in `serde_json`'s float notation (`1e-6` where
  `to_json()` writes `0.000001`), and so are seconds of 1e21 and more by older `serde_json`
  releases (`1e21` for `1e+21`). The number is the same and reads back the same.
- Non-finite seconds (NaN, infinity) are written as `null`, as `to_json()` and `JSON.stringify`
  do, and `null` reads back as NaN. `apply_edits` reports such an edit as `EditInvalid` either way.
- Line numbers above 2^53 are written as the nearest double, because that is what the edit runs
  on (`Edit::delete(usize::MAX)` is `{"op":"delete","line":18446744073709552000}`).
- `KeywordOptions` maps are written as JSON objects without repeated keys: a key added twice
  keeps its first place and takes the last value, as in a JavaScript object.

Reading options is strict: a missing key is the default, an unknown name such as
`"mode":"radio"` is an error.

### `parse_artists`

**Docs:** [parseArtists](https://4matic.github.io/trackparse/reference/functions/#parseartistsinput-options) · [Artists and credits](https://4matic.github.io/trackparse/guides/artists/)

Splits a bare artist string (no title) ([R10](https://4matic.github.io/trackparse/reference/spec/#r10-parseartists)).
[`parse_artists_with`](https://docs.rs/trackparse/latest/trackparse/fn.parse_artists_with.html) takes the same options as `parse_with`.

```rust
use trackparse::parse_artists;

let credits: Vec<String> = parse_artists("Sub Focus, Wilkinson & Dimension feat. Kojo")
    .iter()
    .map(|a| format!("{}:{}", a.name, a.role))
    .collect();
assert_eq!(credits, ["Sub Focus:primary", "Wilkinson:primary", "Dimension:primary", "Kojo:featured"]);
```

### `format_track` and `FormatOptions`

**Docs:** [format](https://4matic.github.io/trackparse/reference/functions/#formattrack-options) · [Formatting](https://4matic.github.io/trackparse/guides/formatting/)

Renders a track back to a string ([format options](https://4matic.github.io/trackparse/reference/options/#format-options),
[R11](https://4matic.github.io/trackparse/reference/spec/#r11-format)): [`format_track`](https://docs.rs/trackparse/latest/trackparse/fn.format_track.html),
[`format_track_with`](https://docs.rs/trackparse/latest/trackparse/fn.format_track_with.html), [`FormatOptions`](https://docs.rs/trackparse/latest/trackparse/struct.FormatOptions.html).

| Option | Default | |
|---|---|---|
| `feat` | `FeatPlacement::Source` | Where featured artists go: `Source` (where they were), `Artist`, `Title` (`(feat. X)` after the title) or `Omit` |
| `feat_marker` | `None` | Marker for featured artists, such as `"featuring"` |
| `joiners` | `JoinerStyle::Original` | `Original` keeps the joiners as written. `Canonical` writes `and` as `&`, `vs` as `vs.` and every feat marker as `feat.` |
| `versions` | `VersionsOption::All` | `All` or `None` |
| `producers` | `true` | Include `(prod. ...)` credits |
| `position` | `false` | Include the track number (`01. `) |

```rust
use trackparse::{
    format_track, format_track_with, parse, FeatPlacement, FormatOptions, JoinerStyle, VersionsOption,
};

let track = parse("01. Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)");
assert_eq!(format_track(&track), "Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)");

let options = FormatOptions::new().feat(FeatPlacement::Title).joiners(JoinerStyle::Canonical);
assert_eq!(
    format_track_with(&track, &options),
    "Wilkinson - Afterglow (feat. Becky Hill) (Sub Focus Remix)"
);

let options = FormatOptions::new().feat(FeatPlacement::Omit).versions(VersionsOption::None);
assert_eq!(format_track_with(&track, &options), "Wilkinson - Afterglow");

let options = FormatOptions::new().position(true);
assert_eq!(
    format_track_with(&track, &options),
    "01. Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)"
);

let options = FormatOptions::new().feat_marker("featuring");
assert_eq!(
    format_track_with(&track, &options),
    "Wilkinson featuring Becky Hill - Afterglow (Sub Focus Remix)"
);
```

### `normalize`

**Docs:** [normalize](https://4matic.github.io/trackparse/reference/functions/#normalizeinput)

The Unicode cleanup every function runs first: NFC, zero-width spaces, BOM and soft hyphens
removed, fullwidth brackets, smart quotes and dash variants mapped to ASCII, whitespace collapsed
([R1](https://4matic.github.io/trackparse/reference/spec/#r1-normalization)). Running it twice changes nothing
([`normalize`](https://docs.rs/trackparse/latest/trackparse/fn.normalize.html)).

```rust
use trackparse::normalize;

assert_eq!(normalize("Noisia\u{200b} – Stigma （VIP）"), "Noisia - Stigma (VIP)");
```

### `all_artists`

**Docs:** [allArtists](https://4matic.github.io/trackparse/reference/functions/#allartiststrack)

Credits plus every remixer, deduplicated ([R9](https://4matic.github.io/trackparse/reference/spec/#r9-assembly-and-dedup),
[`all_artists`](https://docs.rs/trackparse/latest/trackparse/fn.all_artists.html)).

```rust
use trackparse::{all_artists, parse};

let track = parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)");
let credits: Vec<String> =
    all_artists(&track).iter().map(|a| format!("{}:{}", a.name, a.role)).collect();
assert_eq!(credits, ["Wilkinson:primary", "Becky Hill:featured", "Sub Focus:remixer"]);
```

### `Parser`

**Docs:** [createParser](https://4matic.github.io/trackparse/reference/functions/#createparseroptions)

[`Parser`](https://docs.rs/trackparse/latest/trackparse/struct.Parser.html) resolves the options and builds the word tables once, for many
calls. It is `Send + Sync`.

```rust
use trackparse::{KeywordOptions, ParseOptions, Parser, VersionType};

let options = ParseOptions::new()
    .known_artists(["Chase & Status", "Camo & Krooked"])
    .keywords(KeywordOptions::new().version_head("rerub", VersionType::Rework));
let parser = Parser::new(&options);

let track = parser.parse("Chase & Status x Camo & Krooked - Track (Alix Perez Rerub)");
assert_eq!(track.artists.len(), 2);
assert_eq!(track.versions[0].version_type, VersionType::Rework);
```

### `SPEC_VERSION`

**Docs:** [SPEC_VERSION](https://4matic.github.io/trackparse/reference/functions/#spec_version) · [`SPEC_VERSION`](https://docs.rs/trackparse/latest/trackparse/constant.SPEC_VERSION.html) on docs.rs

The version of the [spec](https://4matic.github.io/trackparse/reference/spec/) this build implements. It equals the crate version: the crate, the npm
package, the PyPI package, the Go module and the spec are released together and always share one
version number.

## Modes

**Docs:** [Modes](https://4matic.github.io/trackparse/guides/modes/) · [R2 in the spec](https://4matic.github.io/trackparse/reference/spec/#r2-mode-prelude) · [`ModeOption`](https://docs.rs/trackparse/latest/trackparse/enum.ModeOption.html) on docs.rs

| Mode | For | Adds |
|---|---|---|
| `Clean` | Store metadata, tags | Only a spaced dash separates artist and title |
| [`Youtube`](https://4matic.github.io/trackparse/guides/modes/#youtube-mode) | Video titles, scrobbles | Pipe segments, `"Title" by Artist`, `Artist: "Title"`, unspaced dashes, trailing junk, uploader fallback |
| [`Filename`](https://4matic.github.io/trackparse/guides/modes/#filename-mode) | Files | Strips the extension, `_` becomes a space, `01-Track` numbering |
| [`Auto`](https://4matic.github.io/trackparse/guides/modes/#how-auto-decides) | Anything | `Filename` for known extensions, `Youtube` when it sees junk, pipes or platform suffixes, `Clean` otherwise |
| [`Tracklist`](https://4matic.github.io/trackparse/guides/modes/#tracklist-mode) | One line of a tracklist | The first spaced dash always separates; `_-_` too. Never picked by `Auto` |

```rust
use trackparse::{parse, parse_with, Mode, ModeOption, ParseOptions, Warning};

let youtube = ParseOptions::new().mode(ModeOption::Youtube);
let track = parse_with("\"Numb\" by Linkin Park", &youtube);
assert_eq!(track.artists[0].name, "Linkin Park");
assert_eq!(track.title, "Numb");
assert_eq!(track.warnings, [Warning::BySplit]);

let track = parse("07_Burial_-_Archangel.mp3");
assert_eq!(track.mode, Mode::Filename);
assert_eq!(track.artists[0].name, "Burial");
assert_eq!(track.title, "Archangel");
assert_eq!(track.position.as_ref().map(|p| p.number), Some(7));

let track = parse_with("Shelter", &youtube.uploader("Porter Robinson"));
assert_eq!(track.artists[0].name, "Porter Robinson");
```

Try [`"Numb" by Linkin Park` in youtube mode](https://4matic.github.io/trackparse/playground/#track=%22Numb%22%20by%20Linkin%20Park&mode=youtube) or
[`07_Burial_-_Archangel.mp3`](https://4matic.github.io/trackparse/playground/#track=07_Burial_-_Archangel.mp3) in the playground.

## Warnings

**Docs:** [Warnings](https://4matic.github.io/trackparse/reference/warnings/) · [`Warning`](https://docs.rs/trackparse/latest/trackparse/enum.Warning.html) on docs.rs

The parser never fails, but it says when it had to guess. `track.warnings` is a `Vec<Warning>`,
empty when every decision was unambiguous. `as_str()` gives the name used in the spec and in JSON.

| Warning | JSON name | Means |
|---|---|---|
| `NoSeparator` | [`noSeparator`](https://4matic.github.io/trackparse/reference/warnings/#track-warnings:~:text=noSeparator) | No artist/title separator was found. The whole string is the title |
| `AmbiguousSeparator` | [`ambiguousSeparator`](https://4matic.github.io/trackparse/reference/warnings/#track-warnings:~:text=ambiguousSeparator) | A two-part pipe title with no dash was read as artist and title |
| `UnspacedDashSplit` | [`unspacedDashSplit`](https://4matic.github.io/trackparse/reference/warnings/#track-warnings:~:text=unspacedDashSplit) | Split at a dash without spaces around it |
| `AsymmetricDashSplit` | [`asymmetricDashSplit`](https://4matic.github.io/trackparse/reference/warnings/#track-warnings:~:text=asymmetricDashSplit) | Split at a dash with a space on one side only |
| `BySplit` | [`bySplit`](https://4matic.github.io/trackparse/reference/warnings/#track-warnings:~:text=bySplit) | Read `"Title" by Artist` |
| `QuotedTitleSplit` | [`quotedTitleSplit`](https://4matic.github.io/trackparse/reference/warnings/#track-warnings:~:text=quotedTitleSplit) | Took a quoted span as the title and the text before it as the artist |
| `AmbiguousMixCredit` | [`ambiguousMixCredit`](https://4matic.github.io/trackparse/reference/warnings/#track-warnings:~:text=ambiguousMixCredit) | `<Name> Mix` was read as a remix, but the name is long enough to be a style |
| `UnbalancedBrackets` | [`unbalancedBrackets`](https://4matic.github.io/trackparse/reference/warnings/#track-warnings:~:text=unbalancedBrackets) | A bracket was opened and never closed |

```rust
use trackparse::{parse, Warning};

let track = parse("Around the World");
assert!(track.artists.is_empty());
assert_eq!(track.title, "Around the World");
assert_eq!(track.warnings, [Warning::NoSeparator]);
assert_eq!(track.warnings[0].as_str(), "noSeparator");

assert_eq!(parse("Noisia - Stigma (VIP").warnings, [Warning::UnbalancedBrackets]);
```

Tracklists have their own [warnings](https://4matic.github.io/trackparse/reference/warnings/#tracklist-warnings), with line numbers. They are listed
under [Tracklists](#tracklists).

## Tracklists

**Docs:** [Tracklists](https://4matic.github.io/trackparse/guides/tracklists/) · [Editing tracklists](https://4matic.github.io/trackparse/guides/editing-tracklists/) · [Tracklist API](https://4matic.github.io/trackparse/reference/tracklist/) · [Warnings](https://4matic.github.io/trackparse/reference/warnings/#tracklist-warnings)

[`trackparse::tracklist`](https://docs.rs/trackparse/latest/trackparse/tracklist/index.html) parses a whole tracklist, one entry per line, and checks it. Each entry
keeps its line exactly as written plus its own line ending, so joining every `raw` and `eol` is
always the input, CRLF or not.

```rust
use trackparse::tracklist::{parse_tracklist, EntryKind, Severity, WarningCode};
use trackparse::VersionType;

let text = [
    "Hour 2",
    "48:20 Noisia - Stigma",
    "52:47 Mefjus & Kasra - Bandwagon (Phace Remix)",
    "56:05 Black Sun Empire - ID",
    "59:31 Alix Perez - Fade Away",
    "01:03:10 Interview with Hybris",
    "01:07:44 Imanu - Sacrifice",
    "00:11:02 Current Value feat. Teddy Killerz - Payload", // typo for 01:11:02
    "01:15:36 Hybris - Spinning Top",
]
.join("\r\n");

let list = parse_tracklist(&text);

let joined: String = list.entries.iter().map(|e| format!("{}{}", e.raw, e.eol)).collect();
assert_eq!(joined, text);

use EntryKind::{Header, Marker, Track};
let kinds: Vec<EntryKind> = list.entries.iter().map(|e| e.kind).collect();
assert_eq!(kinds, [Header, Track, Track, Track, Track, Marker, Track, Track, Track]);

let entry = &list.entries[2];
let time = entry.time.as_ref().unwrap();
assert_eq!((time.raw.as_str(), time.seconds), ("52:47", 3167.0));
assert_eq!(entry.parts.rest, "Mefjus & Kasra - Bandwagon (Phace Remix)");
let track = entry.track.as_ref().unwrap();
assert_eq!(track.artists.len(), 2);
assert_eq!(track.title, "Bandwagon");
assert_eq!(track.versions[0].version_type, VersionType::Remix);
assert!(list.entries[3].track.as_ref().unwrap().flags.unknown_title); // the ID

assert_eq!(list.warnings.len(), 2);
assert_eq!(list.warnings[0].code, WarningCode::NoStartAtZero);
assert_eq!(list.warnings[0].severity, Severity::Info);
let warning = &list.warnings[1];
assert_eq!((warning.code, warning.line), (WarningCode::TimeOutOfOrder, 8));
let fix = warning.suggestion.as_ref().unwrap();
assert_eq!((fix.line, fix.raw.as_str(), fix.seconds), (8, "01:11:02", 4262.0));

let stats = &list.stats;
assert_eq!((stats.lines, stats.tracks, stats.markers, stats.timed), (9, 7, 1, 8));
assert_eq!((stats.first_time, stats.last_time), (Some(2900.0), Some(4536.0)));
```

The [suggestion](https://4matic.github.io/trackparse/reference/warnings/#suggestions) exists because exactly one time, one digit away from `00:11:02`,
fits between its neighbours. With several candidates or none, `suggestion` is `None`.
[Open this list in the playground](https://4matic.github.io/trackparse/playground/#list=Hour%202%0A48%3A20%20Noisia%20-%20Stigma%0A52%3A47%20Mefjus%20%26%20Kasra%20-%20Bandwagon%20%28Phace%20Remix%29%0A56%3A05%20Black%20Sun%20Empire%20-%20ID%0A59%3A31%20Alix%20Perez%20-%20Fade%20Away%0A01%3A03%3A10%20Interview%20with%20Hybris%0A01%3A07%3A44%20Imanu%20-%20Sacrifice%0A00%3A11%3A02%20Current%20Value%20feat.%20Teddy%20Killerz%20-%20Payload%0A01%3A15%3A36%20Hybris%20-%20Spinning%20Top).

[`apply_edits`](https://docs.rs/trackparse/latest/trackparse/tracklist/fn.apply_edits.html) takes a slice of [`Edit`](https://docs.rs/trackparse/latest/trackparse/tracklist/enum.Edit.html)
values and only rewrites the lines they name:

```rust
use trackparse::tracklist::{apply_edits, Edit, EditWarningCode};

let text = "48:20 Noisia - Stigma\r\n59:31 Alix Perez - Fade Away\r\n00:03:10 Imanu - Sacrifice\r\n";

let edits = [Edit::set_time(3, 3790.0), Edit::shift_from(45.0, 2)];
let result = apply_edits(text, &edits);

assert_eq!(
    result.text,
    "48:20 Noisia - Stigma\r\n01:00:16 Alix Perez - Fade Away\r\n01:03:55 Imanu - Sacrifice\r\n"
);
assert_eq!(result.changed, [2, 3]);
assert_eq!(result.warnings[0].code, EditWarningCode::StyleUpgraded);
assert_eq!((result.warnings[0].edit, result.warnings[0].line), (1, Some(2)));

let result = apply_edits(text, &[Edit::shift_lines(-3000.0, [1])]);
assert_eq!(result.warnings[0].code, EditWarningCode::TimeClamped);
```

A rewritten time keeps its line's style: `48:20` stays `MM:SS`, and a time that crosses the hour
gets hours, with a `style-upgraded` warning. A time pushed below zero stops at zero and warns
`time-clamped`. A bad edit changes nothing and warns `edit-invalid`
([edit warnings](https://4matic.github.io/trackparse/reference/warnings/#edit-warnings), [editing tracklists](https://4matic.github.io/trackparse/guides/editing-tracklists/#times-keep-their-style)).

[`TracklistOptions`](https://docs.rs/trackparse/latest/trackparse/tracklist/struct.TracklistOptions.html) ([options](https://4matic.github.io/trackparse/reference/options/#tracklist-options))
has `mode` (default `ModeOption::Tracklist`), `known_artists`, `split_and`,
`keywords`, `audio_seconds` (enables `time-after-audio-end`) and `marker_words`:

```rust
use trackparse::tracklist::{parse_tracklist_with, TracklistOptions, WarningCode};

let options = TracklistOptions::new().audio_seconds(3600.0);
let list = parse_tracklist_with("00:00 A - B\n1:02:03 C - D", &options);
assert_eq!(list.warnings[0].code, WarningCode::TimeAfterAudioEnd);
assert_eq!(list.warnings[0].line, 2);
```

| Result | Fields |
|---|---|
| `Tracklist` | `spec_version`, `entries`, `warnings`, `stats` |
| `TracklistEntry` | `line` (1-based), `kind` (`EntryKind`: `Track`, `Marker`, `Header`, `Link`, `Note`, `Blank`), `eol`, `raw`, `parts`, `time`, `track` (`Option<ParsedTrack>`), `layered` (the track text after any time starts with `w/`) |
| `Parts` | `lead`, `time`, `gap`, `rest`, joined they give `raw` |
| `TracklistWarning` | `code` (`WarningCode`), `line`, `severity` (`Info` or `Warning`), `suggestion` |
| `TimeValue` | `raw`, `seconds` (`entry.time`, or `None` on untimed lines) |
| `Suggestion` | `line`, `raw`, `seconds` (`warning.suggestion`, or `None`) |
| `Stats` | `lines`, `tracks`, `markers`, `timed`, `first_time`, `last_time` |
| `EditResult` | `text`, `changed`, `warnings` (`EditWarning { code, edit, line }`) |

Lists pasted with list bullets or Markdown emphasis (`* 14:21 Artist - Title`,
`**[**14:21**] Artist - Title**`) read like plain lines; the decoration stays in `raw` and `parts`.
A line whose track text (after any time) starts with `w/` is `layered`: a track played over the
previous one
([Markdown](https://4matic.github.io/trackparse/guides/tracklists/#markdown),
[layered tracks](https://4matic.github.io/trackparse/guides/tracklists/#layered-tracks)).

[Warning codes](https://4matic.github.io/trackparse/reference/warnings/#tracklist-warnings) ([`WarningCode`](https://docs.rs/trackparse/latest/trackparse/tracklist/enum.WarningCode.html)): `time-missing`, `time-out-of-order`, `time-duplicate`,
`no-start-at-zero`, `time-after-audio-end` (needs `audio_seconds`), `invalid-time`, `position-gap`,
`position-duplicate`, `line-unparsed`. Edits: `Edit::set_time(line, seconds)`,
`Edit::shift(seconds)` (all timed lines), `Edit::shift_from(seconds, from)`,
`Edit::shift_lines(seconds, lines)`, `Edit::sort_by_time()`, `Edit::set_rest(line, rest)`,
`Edit::insert(after, text)` (`after: 0` inserts at the top), `Edit::delete(line)`. The
[Tracklist API reference](https://4matic.github.io/trackparse/reference/tracklist/) explains each
one, and the rules are in
[R13 of the spec](https://4matic.github.io/trackparse/reference/spec/#r13-tracklists).

Every line is parsed with [`ModeOption::Tracklist`](https://4matic.github.io/trackparse/guides/tracklists/#tracklist-mode), where the
first spaced dash always separates artist and title. `parse_with` accepts it too:

```rust
use trackparse::{parse, parse_with, JunkKind, ModeOption, ParseOptions};

let track = parse("Electrosoul System - Liquid Metal");
assert_eq!(track.junk[0].raw, "Liquid Metal");
assert_eq!(track.junk[0].kind, JunkKind::Genre);

let options = ParseOptions::new().mode(ModeOption::Tracklist);
assert_eq!(parse_with("Electrosoul System - Liquid Metal", &options).title, "Liquid Metal");
```

## Errors

No function returns a `Result` and none panics ([R0.8](https://4matic.github.io/trackparse/reference/spec/#r0-portability)). `parse` gives a complete `ParsedTrack` for any
`&str`, `""` included, and `parse_tracklist` and `apply_edits` return for any text. Options are
typed, so a wrong value does not compile. `apply_edits` reports an edit it cannot apply (a line
that does not exist, non-finite seconds) as an `EditWarningCode::EditInvalid` warning and leaves
the text alone.

The only fallible calls are conversions from text: `FromStr` on the enums returns
[`UnknownName`](https://docs.rs/trackparse/latest/trackparse/struct.UnknownName.html)
for a name the spec does not have, and with the `serde` feature, deserializing options or edits
from JSON can fail as any `serde` type can.

## How it is tested

**Docs:** [How it's tested](https://4matic.github.io/trackparse/project/testing/) · [Ports](https://4matic.github.io/trackparse/project/ports/)

- `cargo test` runs all 13,130 shared fixtures (1,203 written by hand, the rest generated from
  the rules), the spec's invariants for single tracks and for tracklists on generated input (no
  panic in any mode, `normalize` idempotent, the format round trip, no word lost, timing), and the
  API tests. The skip list is empty.
- [`scripts/compare-ports.mjs`](https://github.com/4matic/trackparse/blob/main/scripts/compare-ports.mjs) in the repository runs every fixture input through the JavaScript
  package and this crate and compares the complete JSON output, every field of it, not only the
  ones a fixture asserts. The Python package and the Go module are checked the same way.
- The Unicode facts the rules need (letters, uppercase letters, lowercase mapping) are tables
  generated from the reference implementation and compiled into the crate. The output does not
  depend on the compiler's Unicode version: Rust 1.74 and current stable give the same bytes.
  NFC and NFD are the exception, see [Unicode version](#unicode-version).
- With the `serde` feature, a test asserts that `serde_json::to_string` equals `to_json()` for
  every fixture input.
- Clippy denies `unwrap`, `expect`, `panic!` and indexing in the library code, and `unsafe` is
  forbidden.

## Unicode version

Normalization (NFC in `normalize`, NFD in the key that finds duplicate artists) comes from the
[`unicode-normalization`](https://crates.io/crates/unicode-normalization) crate and follows its Unicode version. This crate requires 0.1.26 or
later, which has the Unicode 18 tables; older releases have older tables and give other results
for some strings, so Cargo will not select them.

The JavaScript package normalizes with its runtime's tables, which are Unicode 17 on Node 24.
Unicode 18 added 32 combining marks, so a string that contains one of them can come out with its
marks in another order here than in JavaScript on that runtime, and than in the Go module, whose
NFC and NFD tables are generated from Node 24. Strings without those marks are not affected, and
every shared fixture gives the same bytes. More in
[what the Rust port taught](https://4matic.github.io/trackparse/project/ports/#what-the-rust-port-taught).

## Performance

There are no regular expressions, so there is no regex backtracking. A 10,000-code-point input
parses in under 20 ms in a release build. The test suite runs hostile repetitions of that
length (thousands of duplicate credits, bracket groups or junk phrases) under a time ceiling.

## Versioning

**Changelog:** [rust/CHANGELOG.md](https://github.com/4matic/trackparse/blob/main/rust/CHANGELOG.md)

The crate version is the spec version ([`SPEC_VERSION`](#spec_version)); every release is on
[crates.io](https://crates.io/crates/trackparse/versions). While it is 0.x, a release that changes
the output for some input bumps the minor version, and a patch release only fixes bugs. Cargo's
default requirement already stays within one minor version (`trackparse = "0.6"` means 0.6.x), so
`cargo update` only brings fixes. Read the changelog before moving to the next minor.

## Other languages

**Docs:** [Ports](https://4matic.github.io/trackparse/project/ports/)

The same parser exists for JavaScript, Python and Go. All four implement one spec, pass the same
fixtures, are released together under one version number, and return identical JSON for every
fixture input. You can parse in one language and read the result in another. The one known
difference is described under [Unicode version](#unicode-version).

| Language | Package | Install | Docs |
|---|---|---|---|
| JavaScript / TypeScript | [trackparse on npm](https://www.npmjs.com/package/trackparse) | `npm install trackparse` | [README](https://github.com/4matic/trackparse/blob/main/js/README.md) · [changelog](https://github.com/4matic/trackparse/blob/main/js/CHANGELOG.md) · [API](https://4matic.github.io/trackparse/reference/parse/) |
| Python | [trackparse on PyPI](https://pypi.org/project/trackparse/) | `pip install trackparse` | [README](https://github.com/4matic/trackparse/blob/main/python/README.md) · [changelog](https://github.com/4matic/trackparse/blob/main/python/CHANGELOG.md) · [API](https://4matic.github.io/trackparse/reference/parse/) |
| Rust | [trackparse on crates.io](https://crates.io/crates/trackparse) | `cargo add trackparse` | [README](https://github.com/4matic/trackparse/blob/main/rust/README.md) · [changelog](https://github.com/4matic/trackparse/blob/main/rust/CHANGELOG.md) · [API on docs.rs](https://docs.rs/trackparse) |
| Go | [github.com/4matic/trackparse/go](https://pkg.go.dev/github.com/4matic/trackparse/go) | `go get github.com/4matic/trackparse/go` | [README](https://github.com/4matic/trackparse/blob/main/go/README.md) · [changelog](https://github.com/4matic/trackparse/blob/main/go/CHANGELOG.md) · [API on pkg.go.dev](https://pkg.go.dev/github.com/4matic/trackparse/go) |

## Limitations

**Docs:** [Limitations and roadmap](https://4matic.github.io/trackparse/project/roadmap/)

- `parse` takes one track string. Whole tracklists go through `trackparse::tracklist`, which reads
  start times only: no durations, trailing times or time ranges.
- Catalog numbers, key and BPM are not fields. They stay in the title.
- There is no artist database. Pass `known_artists` for names that contain a joiner
  (`Earth, Wind & Fire`).

## Development

From `rust/` in the [repository](https://github.com/4matic/trackparse) (Node 24 is needed for the
codegen script only):

```sh
node scripts/gen_data.mjs --check                           # spec/data and Unicode codegen is up to date
cargo fmt --check
cargo clippy --all-targets --all-features -- -D warnings
cargo test                                                  # every shared fixture, properties, API
cargo test --all-features                                   # the same with serde, and this README's examples
TRACKPARSE_REPORT=1 cargo test --test fixtures -- --nocapture   # pass rate per fixture file and per rule
```

Modules mirror the JS reference one to one (`scanner.rs` is `scanner.ts`, and so on) and cite the
same spec rule IDs. Behaviour changes start in the spec, not here: see
[AGENTS.md](https://github.com/4matic/trackparse/blob/main/AGENTS.md).

## Links

- [Documentation](https://4matic.github.io/trackparse/), with every example in JavaScript, Python,
  Rust and Go, and a [playground](https://4matic.github.io/trackparse/playground/) that runs the
  parser in your browser
- [API documentation on docs.rs](https://docs.rs/trackparse)
- [Spec](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md) and its
  [changelog](https://github.com/4matic/trackparse/blob/main/spec/CHANGELOG.md)
- [Changelog](https://github.com/4matic/trackparse/blob/main/rust/CHANGELOG.md)
- [Project README](https://github.com/4matic/trackparse#readme)
- [GitHub repository](https://github.com/4matic/trackparse)
- [npm package](https://www.npmjs.com/package/trackparse) · [PyPI package](https://pypi.org/project/trackparse/) · [Go module](https://pkg.go.dev/github.com/4matic/trackparse/go)

## License

[MIT](https://github.com/4matic/trackparse/blob/main/LICENSE)
