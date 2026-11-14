# trackparse

[![Go Reference](https://pkg.go.dev/badge/github.com/4matic/trackparse/go.svg)](https://pkg.go.dev/github.com/4matic/trackparse/go)
[![Go version](https://img.shields.io/github/go-mod/go-version/4matic/trackparse?filename=go%2Fgo.mod&logo=go&logoColor=white)](https://github.com/4matic/trackparse/blob/main/go/go.mod)
[![tests](https://img.shields.io/github/actions/workflow/status/4matic/trackparse/go.yml?branch=main&label=tests&logo=github)](https://github.com/4matic/trackparse/actions/workflows/go.yml)
[![license](https://img.shields.io/badge/license-MIT-blue)](https://github.com/4matic/trackparse/blob/main/LICENSE)

**[Documentation](https://4matic.github.io/trackparse/)** · **[Getting started](https://4matic.github.io/trackparse/start/getting-started/)** · **[Playground](https://4matic.github.io/trackparse/playground/)** · **[API on pkg.go.dev](https://pkg.go.dev/github.com/4matic/trackparse/go)**

**Turn messy track names into structured data: who made it, who's featured, who remixed it, and
what's just noise.**

```text
01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]
```

Most title parsers split that into `"Wilkinson ft. Becky Hill & Tom Cane"` and
`"Afterglow (Sub Focus VIP Remix)"` and stop. Others "clean" it and throw the feat and the remix
away. trackparse keeps all of it. Everything it recognizes goes into a field, and anything it
doesn't recognize stays in the title, word for word.

This is the Go port of [trackparse](https://github.com/4matic/trackparse). Its behaviour is defined
by a [shared spec](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md) and pinned by
13,130 fixtures. It passes all of them, and for every fixture input its output is byte-identical
to the [JavaScript, Python and Rust packages](#other-languages).

- Go 1.22 or later (the `go` line of `go.mod`; CI runs the tests on 1.22 and on the latest stable)
- No dependencies: `go.mod` has no `require` line
- No `unsafe`, no cgo, no regular expressions
- Never panics and never returns an error: any string gives a result

## Install

```sh
go get github.com/4matic/trackparse/go@latest
```

```go
import trackparse "github.com/4matic/trackparse/go"
```

The import path ends in `/go` because the module lives in the `go/` folder of the repository,
next to the other ports. The package name is `trackparse`, so the alias in the import line is
optional; it only makes the name visible to readers and to tools that guess it from the path.

The module is released with the other ports, starting with the next release after 0.5.1. Until
that tag exists, `go get` and the pkg.go.dev links above find nothing.

## Quick start

**Docs:** [Getting started](https://4matic.github.io/trackparse/start/getting-started/)

```go
track := trackparse.Parse(
	"01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]",
)

for _, a := range track.Artists {
	fmt.Printf("%s (%s)\n", a.Name, a.Role)
}
// Wilkinson (primary)
// Becky Hill (featured)
// Tom Cane (featured)

fmt.Println(track.Title)                                     // Afterglow
fmt.Println(track.FullTitle)                                 // Afterglow (Sub Focus VIP Remix)
v := track.Versions[0]
fmt.Println(v.Type, v.Artists[0].Name, v.Modifiers)          // remix Sub Focus [vip]
fmt.Println(track.Junk[0].Raw, track.Junk[0].Kind)           // Official Video video
fmt.Println(track.Position.Number, track.Mode)               // 1 youtube

fmt.Println(trackparse.FormatWith(track, trackparse.FormatOptions{
	Joiners: trackparse.JoinersCanonical,
}))
// Wilkinson feat. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix)
```

`json.Marshal` gives the spec's JSON, with the same camelCase keys the JavaScript package uses:

```go
track := trackparse.Parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus VIP Remix) [HD]")
b, _ := json.Marshal(track)
fmt.Println(string(b))
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

(Reformatted for width; the keys and values are exactly what `json.Marshal` returns.)

</details>

The examples on [pkg.go.dev](https://pkg.go.dev/github.com/4matic/trackparse/go#pkg-examples) are
`Example` functions in
[`example_test.go`](https://github.com/4matic/trackparse/blob/main/go/example_test.go), so
`go test` checks their output.

## API

**Docs:** [Other functions](https://4matic.github.io/trackparse/reference/functions/)

```text
Parse(input string) Track
ParseWith(input string, options ParseOptions) Track
ParseArtists(input string) []Artist
ParseArtistsWith(input string, options ParseOptions) []Artist
Format(track Track) string
FormatWith(track Track, options FormatOptions) string
Normalize(input string) string
AllArtists(track Track) []Artist
NewParser(options ParseOptions) *Parser    // parser.Parse(..), parser.ParseArtists(..)
SpecVersion string

ParseTracklist(text string) Tracklist
ParseTracklistWith(text string, options TracklistOptions) Tracklist
ApplyEdits(text string, edits []Edit) EditResult
ApplyEditsWith(text string, edits []Edit, options TracklistOptions) EditResult
```

Every function has a plain form with the default options and a `With` form that takes them.
Tracklists are in the same package; there is no subpackage.

### `Parse` and `ParseOptions`

**Docs:** [parse and ParsedTrack](https://4matic.github.io/trackparse/reference/parse/) · [Options](https://4matic.github.io/trackparse/reference/options/)

| Field | Zero value means | |
|---|---|---|
| `Mode` | `ModeAuto` | `ModeClean`, `ModeYouTube`, `ModeFilename`, `ModeTracklist`, or `ModeAuto` to detect from the string. See [Modes](#modes) |
| `Uploader` | none | YouTube channel name, used as the artist when there's no separator (`- Topic` and `VEVO` are dropped) |
| `KnownArtists` | none | Names that are never split. There is no built-in list |
| `SplitAnd` | `SplitAndAuto` | `SplitAndAuto` splits on `and` only after a comma (`A, B and C`), so `Simon and Garfunkel` stays whole. Also `SplitAndAlways`, `SplitAndNever` |
| `Keywords` | none | Extra vocabulary, added to the built-in lists (see below) |

Options are plain structs, and the zero value of every struct is the default: `Parse(s)` is
`ParseWith(s, ParseOptions{})`. Set only the fields you need.

```go
names := func(t trackparse.Track) []string {
	var out []string
	for _, a := range t.Artists {
		out = append(out, a.Name)
	}
	return out
}

fmt.Println(names(trackparse.Parse("Chase & Status - Baddadan"))) // [Chase Status]
fmt.Println(names(trackparse.ParseWith("Chase & Status - Baddadan", trackparse.ParseOptions{
	KnownArtists: []string{"Chase & Status"},
}))) // [Chase & Status]
fmt.Println(names(trackparse.ParseWith("Simon and Garfunkel - Mrs. Robinson", trackparse.ParseOptions{
	SplitAnd: trackparse.SplitAndAlways,
}))) // [Simon Garfunkel]
```

`Keywords` adds words to the built-in lists:

| Field | Type | |
|---|---|---|
| `VersionHeads` | `[]VersionHead` | Word that sets a version type (`{Key: "rerub", Type: VersionTypeRework}`) |
| `Descriptors` | `[]string` | Words kept as a version's `Descriptor` |
| `Genres` | `[]string` | Genre words |
| `Junk` | `[]JunkPhrase` | Phrase to junk kind (`{Phrase: "some label rip", Kind: JunkKindLabel}`) |
| `FeatMarkers` | `[]string` | Extra feat markers |

The two maps of the JavaScript options are ordered slices here. A key given twice keeps its first
place and takes the last value, as in a JavaScript object.

```go
fmt.Println(trackparse.Parse("Artist - Song [Some Label Rip]").Title) // Song [Some Label Rip]

track := trackparse.ParseWith("Artist - Song [Some Label Rip]", trackparse.ParseOptions{
	Keywords: trackparse.Keywords{
		Junk: []trackparse.JunkPhrase{{Phrase: "some label rip", Kind: trackparse.JunkKindLabel}},
	},
})
fmt.Println(track.Title)                           // Song
fmt.Println(track.Junk[0].Raw, track.Junk[0].Kind) // Some Label Rip label
```

#### Pointer fields and `Ptr`

Every option value of the other ports has a Go spelling. Where the zero value of a field is also
a value someone may want to pass, the field is a pointer, and `nil` means "not set":

- `FormatOptions.FeatMarker` is a `*string`. `nil` keeps the input's marker; a pointer to `""`
  writes an empty marker, as `featMarker: ""` does in JavaScript.
- `TracklistOptions.AudioSeconds` is a `*float64`. `nil` means the length is unknown; a pointer to
  `0` is an audio of length 0, which flags every time after `0:00`.

`trackparse.Ptr(v)` returns a pointer to a copy of `v`, so a literal fits in one expression:
`FormatOptions{FeatMarker: trackparse.Ptr("featuring")}`,
`TracklistOptions{AudioSeconds: trackparse.Ptr(3600.0)}`.

`Mode` is a string field whose zero value `""` means the default of the function: `ModeAuto`
(`"auto"`) for `ParseWith`, `ModeTracklist` for `ParseTracklistWith`. Both constants can also be
passed explicitly, so `TracklistOptions{Mode: trackparse.ModeAuto}` parses each line in auto mode.

Option values are typed strings, so a value outside the constants compiles. It behaves as the
JavaScript package behaves with that value, as the doc comment of each type says: an unknown
`SplitAnd` is `SplitAndAlways` in `ParseOptions`, an unknown `Versions` is `VersionsNone`, and so
on. Nothing panics.

### `Track`

**Docs:** [ParsedTrack fields](https://4matic.github.io/trackparse/reference/parse/#parsedtrack)

| Field | Type | |
|---|---|---|
| `Input` | `string` | The original string, untouched |
| `Mode` | `Mode` | Resolved mode: `ModeClean`, `ModeYouTube`, `ModeFilename` or `ModeTracklist` |
| `Position` | `*Position` | Track number: `&Position{Raw: "01", Number: 1}` |
| `Timestamp` | `*Timestamp` | Leading cue time: `[12:34]` gives `&Timestamp{Raw: "12:34", Seconds: 754}` |
| `Artists` | `[]Artist` | Primary, featured and producer credits. Remixers live in `Versions` |
| `Title` | `string` | Clean title. Brackets it doesn't recognize stay in |
| `FullTitle` | `string` | `Title` with the versions re-attached |
| `Versions` | `[]Version` | Remixes and other versions, left to right |
| `Year` | `*int` | From `(2015)` or `- 2015` |
| `Flags` | `Flags` | `Explicit`, `Clean`, `UnknownArtist`, `UnknownTitle` |
| `Junk` | `[]Junk` | What was taken out, each with a `Kind` |
| `Warnings` | `[]Warning` | Heuristics that fired |

Fields that are `null` in JSON are pointers, `nil` when absent. Slices in a result from the parser
are never `nil`, so they marshal as `[]`.

`Artist{Name, Role, Joiner, Source}`:

- `Role` is `RolePrimary`, `RoleFeatured`, `RoleRemixer` or `RoleProducer`.
- `Joiner` is the text before the name as written (`"&"`, `","`, `"ft."`, `"prod. by"`), or `nil`
  for the first name in a list.
- `Source` is where the credit came from: `SourceArtist` (left of the dash), `SourceTitle` (right
  of it) or `SourceVersion`.

`Version{Type, Raw, Artists, Modifiers, Descriptor, Year, UnknownArtist, Delimiter}`:

```go
track := trackparse.Parse("Pendulum - Watercolour (Extended VIP Mix)")
v := track.Versions[0]
fmt.Println(v.Type, v.Raw, v.Modifiers, v.Delimiter) // vip Extended VIP Mix [extended] (

track = trackparse.Parse("Queen - Bohemian Rhapsody (Live at Wembley 1986)")
v = track.Versions[0]
fmt.Println(v.Type, *v.Descriptor, *v.Year) // live at Wembley 1986
```

Version types, junk kinds, warnings, roles, modes and the tracklist codes are typed strings
(`type VersionType string`) with one constant per spec name: `VersionTypeRemix`,
`JunkKindVideo`, `WarningNoSeparator`, `WarningCodeTimeOutOfOrder`. The value is the name used in
the spec and in JSON, so `string(v.Type)` is `"remix"` and a `switch` on the constants works. A
function per type returns every constant in the spec's order, as a new slice each time
(`AllVersionTypes()`, `AllJunkKinds()`, `AllWarnings()`, ...):

```go
fmt.Println(trackparse.VersionTypeSpedUp)        // spedUp
fmt.Println(len(trackparse.AllVersionTypes()))   // 27
fmt.Println(trackparse.AllRoles())               // [primary featured remixer producer]
```

A later spec version can add a version type or a warning, so a `switch` on them needs a
`default` case. A version head or junk phrase added through `Keywords` can carry any string as
its type or kind, and the result keeps that string as given.

### JSON

**Docs:** [Serializing to JSON](https://4matic.github.io/trackparse/reference/parse/#serializing-to-json)

Every result type has `MarshalJSON`, which writes compact JSON matching
[`parsed-track.schema.json`](https://github.com/4matic/trackparse/blob/main/spec/schema/parsed-track.schema.json)
and
[`tracklist.schema.json`](https://github.com/4matic/trackparse/blob/main/spec/schema/tracklist.schema.json):
camelCase keys in the schema's order, every key present, whole seconds as integers (`3167`, not
`3167.0`). Its bytes are those of `JSON.stringify(parse(s))` in JavaScript. `Track` also has
`UnmarshalJSON`, which reads that form back; `Edit` marshals as the spec writes an edit
(`{"op":"set-time","line":3,"seconds":90}`).

`encoding/json` changes some bytes of what `MarshalJSON` returns, not the values:
`json.Marshal` escapes `<`, `>`, `&`, U+2028 and U+2029 (`"&"` becomes `"\u0026"`). For the exact
`JSON.stringify` bytes, call `MarshalJSON` directly or use an `Encoder` with
`SetEscapeHTML(false)` (which adds a newline after each value):

```go
track := trackparse.Parse("Chase & Status - Baddadan")

b, _ := json.Marshal(track.Artists[1])
fmt.Println(string(b)) // {"name":"Status","role":"primary","joiner":"\u0026","source":"artist"}

enc := json.NewEncoder(os.Stdout)
enc.SetEscapeHTML(false)
enc.Encode(track.Artists[1]) // {"name":"Status","role":"primary","joiner":"&","source":"artist"}

b, _ = track.Artists[1].MarshalJSON()
fmt.Println(string(b)) // {"name":"Status","role":"primary","joiner":"&","source":"artist"}
```

`UnmarshalJSON` ignores unknown keys, leaves missing keys at their zero value, keeps enum values
as written, and returns an error for a key of the wrong type. The options structs have no JSON
methods.

### `ParseArtists`

**Docs:** [parseArtists](https://4matic.github.io/trackparse/reference/functions/#parseartistsinput-options) · [Artists and credits](https://4matic.github.io/trackparse/guides/artists/)

Splits a bare artist string (no title). `ParseArtistsWith` takes the same options as `ParseWith`.

```go
for _, a := range trackparse.ParseArtists("Sub Focus, Wilkinson & Dimension feat. Kojo") {
	fmt.Printf("%s:%s\n", a.Name, a.Role)
}
// Sub Focus:primary
// Wilkinson:primary
// Dimension:primary
// Kojo:featured
```

### `Format` and `FormatOptions`

**Docs:** [format](https://4matic.github.io/trackparse/reference/functions/#formattrack-options) · [Formatting](https://4matic.github.io/trackparse/guides/formatting/)

Renders a track back to a string.

| Field | Zero value means | |
|---|---|---|
| `Feat` | `FeatSource` | Where featured artists go: `FeatSource` (where they were), `FeatArtist`, `FeatTitle` (`(feat. X)` after the title) or `FeatOmit` |
| `FeatMarker` | `nil`: the input's marker | Marker for featured artists, such as `trackparse.Ptr("featuring")`. A pointer to `""` writes an empty marker |
| `Joiners` | `JoinersOriginal` | `JoinersOriginal` keeps the joiners as written. `JoinersCanonical` writes `and` as `&`, `vs` as `vs.` and every feat marker as `feat.` |
| `Versions` | `VersionsAll` | `VersionsAll` or `VersionsNone` |
| `OmitProducers` | `false` | Leave out `(prod. ...)` credits (`producers: false` in JavaScript) |
| `Position` | `false` | Include the track number (`01. `) |

```go
track := trackparse.Parse("01. Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)")

fmt.Println(trackparse.Format(track))
// Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)
fmt.Println(trackparse.FormatWith(track, trackparse.FormatOptions{
	Feat:    trackparse.FeatTitle,
	Joiners: trackparse.JoinersCanonical,
}))
// Wilkinson - Afterglow (feat. Becky Hill) (Sub Focus Remix)
fmt.Println(trackparse.FormatWith(track, trackparse.FormatOptions{
	Feat:     trackparse.FeatOmit,
	Versions: trackparse.VersionsNone,
}))
// Wilkinson - Afterglow
fmt.Println(trackparse.FormatWith(track, trackparse.FormatOptions{Position: true}))
// 01. Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)
fmt.Println(trackparse.FormatWith(track, trackparse.FormatOptions{FeatMarker: trackparse.Ptr("featuring")}))
// Wilkinson featuring Becky Hill - Afterglow (Sub Focus Remix)
```

### `Normalize`

**Docs:** [normalize](https://4matic.github.io/trackparse/reference/functions/#normalizeinput)

The Unicode cleanup every function runs first: NFC, zero-width spaces, BOM and soft hyphens
removed, fullwidth brackets, smart quotes and dash variants mapped to ASCII, whitespace collapsed.
Running it twice changes nothing.

```go
fmt.Println(trackparse.Normalize("Noisia\u200b – Stigma （VIP）")) // Noisia - Stigma (VIP)
```

### `AllArtists`

**Docs:** [allArtists](https://4matic.github.io/trackparse/reference/functions/#allartiststrack)

Credits plus every remixer, deduplicated.

```go
track := trackparse.Parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)")
for _, a := range trackparse.AllArtists(track) {
	fmt.Printf("%s:%s\n", a.Name, a.Role)
}
// Wilkinson:primary
// Becky Hill:featured
// Sub Focus:remixer
```

### `Parser`

**Docs:** [createParser](https://4matic.github.io/trackparse/reference/functions/#createparseroptions)

Resolves the options and builds the word tables once, for many calls. `ParseWith` builds them on
every call, so use a `Parser` when the options are the same each time.

```go
parser := trackparse.NewParser(trackparse.ParseOptions{
	KnownArtists: []string{"Chase & Status", "Camo & Krooked"},
	Keywords: trackparse.Keywords{
		VersionHeads: []trackparse.VersionHead{{Key: "rerub", Type: trackparse.VersionTypeRework}},
	},
})

track := parser.Parse("Chase & Status x Camo & Krooked - Track (Alix Perez Rerub)")
fmt.Println(len(track.Artists), track.Versions[0].Type) // 2 rework

var plain trackparse.Parser // the zero Parser uses the default options
fmt.Println(len(plain.Parse("Chase & Status - Baddadan").Artists)) // 2
```

A `Parser` is safe for concurrent use by many goroutines, and so are the package functions. The
default tables are built once, on first use.

### `SpecVersion`

The spec version this module implements. It equals the module version: the Go module, the npm
package, the PyPI package, the Rust crate and the spec are released together and always share one
version number.

## Modes

**Docs:** [Modes](https://4matic.github.io/trackparse/guides/modes/)

| Mode | For | Adds |
|---|---|---|
| `ModeClean` | Store metadata, tags | Only a spaced dash separates artist and title |
| `ModeYouTube` | Video titles, scrobbles | Pipe segments, `"Title" by Artist`, `Artist: "Title"`, unspaced dashes, trailing junk, uploader fallback |
| `ModeFilename` | Files | Strips the extension, `_` becomes a space, `01-Track` numbering |
| `ModeAuto` | Anything | `ModeFilename` for known extensions, `ModeYouTube` when it sees junk, pipes or platform suffixes, `ModeClean` otherwise |
| `ModeTracklist` | One line of a tracklist | The first spaced dash always separates; `_-_` too. Never picked by `ModeAuto` |

```go
youtube := trackparse.ParseOptions{Mode: trackparse.ModeYouTube}
track := trackparse.ParseWith(`"Numb" by Linkin Park`, youtube)
fmt.Println(track.Artists[0].Name, "/", track.Title, track.Warnings) // Linkin Park / Numb [bySplit]

track = trackparse.Parse("07_Burial_-_Archangel.mp3")
fmt.Println(track.Mode, track.Artists[0].Name, track.Title, track.Position.Number) // filename Burial Archangel 7

youtube.Uploader = "Porter Robinson"
fmt.Println(trackparse.ParseWith("Shelter", youtube).Artists[0].Name) // Porter Robinson
```

## Warnings

**Docs:** [Warnings](https://4matic.github.io/trackparse/reference/warnings/)

The parser never fails, but it says when it had to guess. `track.Warnings` is a `[]Warning`,
empty when every decision was unambiguous.

| Constant | JSON name | Means |
|---|---|---|
| `WarningNoSeparator` | `noSeparator` | No artist/title separator was found. The whole string is the title |
| `WarningAmbiguousSeparator` | `ambiguousSeparator` | A two-part pipe title with no dash was read as artist and title |
| `WarningUnspacedDashSplit` | `unspacedDashSplit` | Split at a dash without spaces around it |
| `WarningAsymmetricDashSplit` | `asymmetricDashSplit` | Split at a dash with a space on one side only |
| `WarningBySplit` | `bySplit` | Read `"Title" by Artist` |
| `WarningQuotedTitleSplit` | `quotedTitleSplit` | Took a quoted span as the title and the text before it as the artist |
| `WarningAmbiguousMixCredit` | `ambiguousMixCredit` | `<Name> Mix` was read as a remix, but the name is long enough to be a style |
| `WarningUnbalancedBrackets` | `unbalancedBrackets` | A bracket was opened and never closed |

```go
track := trackparse.Parse("Around the World")
fmt.Println(len(track.Artists), track.Title, track.Warnings) // 0 Around the World [noSeparator]
fmt.Println(track.Warnings[0] == trackparse.WarningNoSeparator) // true

fmt.Println(trackparse.Parse("Noisia - Stigma (VIP").Warnings) // [unbalancedBrackets]
```

Tracklists have their own warnings, with line numbers. They are listed under
[Tracklists](#tracklists).

## Tracklists

**Docs:** [Tracklists](https://4matic.github.io/trackparse/guides/tracklists/) · [Editing tracklists](https://4matic.github.io/trackparse/guides/editing-tracklists/) · [Tracklist API](https://4matic.github.io/trackparse/reference/tracklist/) · [Warnings](https://4matic.github.io/trackparse/reference/warnings/#tracklist-warnings)

`ParseTracklist` parses a whole tracklist, one entry per line, and checks it. Each entry keeps its
line exactly as written plus its own line ending, so joining every `Raw` and `Eol` is always the
input, CRLF or not.

```go
text := strings.Join([]string{
	"Hour 2",
	"48:20 Noisia - Stigma",
	"52:47 Mefjus & Kasra - Bandwagon (Phace Remix)",
	"56:05 Black Sun Empire - ID",
	"59:31 Alix Perez - Fade Away",
	"01:03:10 Interview with Hybris",
	"01:07:44 Imanu - Sacrifice",
	"00:11:02 Current Value feat. Teddy Killerz - Payload", // typo for 01:11:02
	"01:15:36 Hybris - Spinning Top",
}, "\r\n")

list := trackparse.ParseTracklist(text)

var joined strings.Builder
var kinds []trackparse.EntryKind
for _, e := range list.Entries {
	joined.WriteString(e.Raw + string(e.Eol))
	kinds = append(kinds, e.Kind)
}
fmt.Println(joined.String() == text) // true
fmt.Println(kinds) // [header track track track track marker track track track]

e := list.Entries[2]
fmt.Println(e.Time.Raw, e.Time.Seconds) // 52:47 3167
fmt.Printf("%q\n", e.Parts.Rest) // "Mefjus & Kasra - Bandwagon (Phace Remix)"
fmt.Println(len(e.Track.Artists), e.Track.Title, e.Track.Versions[0].Type) // 2 Bandwagon remix
fmt.Println(list.Entries[3].Track.Flags.UnknownTitle) // true (the ID)

for _, w := range list.Warnings {
	fmt.Println(w.Code, w.Line, w.Severity)
	if s := w.Suggestion; s != nil {
		fmt.Println("  suggestion:", s.Line, s.Raw, s.Seconds)
	}
}
// no-start-at-zero 2 info
// time-out-of-order 8 warning
//   suggestion: 8 01:11:02 4262

s := list.Stats
fmt.Println(s.Lines, s.Tracks, s.Markers, s.Timed, *s.FirstTime, *s.LastTime) // 9 7 1 8 2900 4536
```

The suggestion exists because exactly one time, one digit away from `00:11:02`, fits between its
neighbours. With several candidates or none, `Suggestion` is `nil`.

`ApplyEdits` takes a slice of `Edit` values and only rewrites the lines they name:

```go
text := "48:20 Noisia - Stigma\r\n59:31 Alix Perez - Fade Away\r\n00:03:10 Imanu - Sacrifice\r\n"

result := trackparse.ApplyEdits(text, []trackparse.Edit{
	trackparse.SetTime(3, 3790),
	trackparse.ShiftFrom(45, 2),
})
fmt.Printf("%q\n", result.Text)
// "48:20 Noisia - Stigma\r\n01:00:16 Alix Perez - Fade Away\r\n01:03:55 Imanu - Sacrifice\r\n"
fmt.Println(result.Changed) // [2 3]
w := result.Warnings[0]
fmt.Println(w.Code, w.Edit, *w.Line) // style-upgraded 1 2

result = trackparse.ApplyEdits(text, []trackparse.Edit{trackparse.ShiftLines(-3000, 1)})
fmt.Println(result.Warnings[0].Code) // time-clamped

result = trackparse.ApplyEdits(text, []trackparse.Edit{trackparse.Delete(9)})
fmt.Println(result.Warnings[0].Code, result.Text == text) // edit-invalid true
```

A rewritten time keeps its line's style: `48:20` stays `MM:SS`, and a time that crosses the hour
gets hours, with a `style-upgraded` warning. A time pushed below zero stops at zero and warns
`time-clamped`. A bad edit changes nothing and warns `edit-invalid`.

Edits are built with `SetTime(line, seconds)`, `Shift(seconds)` (all timed lines),
`ShiftFrom(seconds, from)`, `ShiftLines(seconds, lines...)`, `SortByTime()`,
`SetRest(line, rest)`, `Insert(after, text)` (`after` 0 inserts at the top) and `Delete(line)`.
`Edit` is a plain struct with an `Op` field, so you can also fill it yourself; each operation reads
only its own fields. Line numbers are 1-based.

`TracklistOptions` has `Mode` (zero value: `ModeTracklist`; `ModeAuto` picks a mode per line),
`KnownArtists`, `SplitAnd`, `Keywords`, `AudioSeconds` (a `*float64`; it enables
`time-after-audio-end`) and `MarkerWords`:

```go
list := trackparse.ParseTracklistWith("00:00 A - B\n1:02:03 C - D", trackparse.TracklistOptions{
	AudioSeconds: trackparse.Ptr(3600.0),
})
fmt.Println(list.Warnings[0].Code, list.Warnings[0].Line) // time-after-audio-end 2
```

| Result | Fields |
|---|---|
| `Tracklist` | `SpecVersion`, `Entries`, `Warnings`, `Stats` |
| `Entry` | `Line` (1-based), `Kind` (`EntryKindTrack`, `EntryKindMarker`, `EntryKindHeader`, `EntryKindLink`, `EntryKindNote`, `EntryKindBlank`), `Eol`, `Raw`, `Parts`, `Time` (`*TracklistTime`), `Track` (`*Track`), `Layered` (the track text after any time starts with `w/`) |
| `Parts` | `Lead`, `Time`, `Gap`, `Rest`, joined they give `Raw` |
| `TracklistWarning` | `Code` (`WarningCode`), `Line`, `Severity` (`SeverityInfo` or `SeverityWarning`), `Suggestion` (`*Suggestion`) |
| `TracklistTime` | `Raw`, `Seconds` (`float64`; `Entry.Time`, `nil` on untimed lines) |
| `Suggestion` | `Line`, `Raw`, `Seconds` |
| `Stats` | `Lines`, `Tracks`, `Markers`, `Timed`, `FirstTime`, `LastTime` (`*float64`) |
| `EditResult` | `Text`, `Changed`, `Warnings` (`EditWarning{Code, Edit, Line}`, `Line` is `*int`) |

`Eol` is the terminator itself (`EolLF` is `"\n"`, `EolCRLF` is `"\r\n"`, `EolCR` is `"\r"`, and
`EolNone` is `""` for a last line without one), so `e.Raw + string(e.Eol)` is the line as it was.

Lists pasted with list bullets or Markdown emphasis (`* 14:21 Artist - Title`,
`**[**14:21**] Artist - Title**`) read like plain lines; the decoration stays in `Raw` and `Parts`.
A line whose track text (after any time) starts with `w/` is `Layered`: a track played over the
previous one
([Markdown](https://4matic.github.io/trackparse/guides/tracklists/#markdown),
[layered tracks](https://4matic.github.io/trackparse/guides/tracklists/#layered-tracks)).

Warning codes (`WarningCode`): `time-missing`, `time-out-of-order`, `time-duplicate`,
`no-start-at-zero`, `time-after-audio-end` (needs `AudioSeconds`), `invalid-time`, `position-gap`,
`position-duplicate`, `line-unparsed`, as the constants `WarningCodeTimeMissing` to
`WarningCodeLineUnparsed`. Edit warnings (`EditWarningCode`): `EditWarningCodeEditInvalid`,
`EditWarningCodeTimeClamped`, `EditWarningCodeStyleUpgraded`. The
[Tracklist API reference](https://4matic.github.io/trackparse/reference/tracklist/) explains each
one, and the rules are in
[SPEC R13](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md#r13-tracklists).

Every line is parsed with `ModeTracklist`, where the first spaced dash always separates artist and
title. `ParseWith` accepts it too:

```go
track := trackparse.Parse("Electrosoul System - Liquid Metal")
fmt.Println(track.Title, "/", track.Junk[0].Raw, track.Junk[0].Kind) // Electrosoul System / Liquid Metal genre

track = trackparse.ParseWith("Electrosoul System - Liquid Metal", trackparse.ParseOptions{
	Mode: trackparse.ModeTracklist,
})
fmt.Println(track.Title) // Liquid Metal
```

## Errors

No function returns an `error` and none panics. `Parse` gives a complete `Track` for any string,
`""` included, and `ParseTracklist` and `ApplyEdits` return for any text. `ApplyEdits` reports an
edit it cannot apply (a line that does not exist, non-finite seconds) as an
`EditWarningCodeEditInvalid` warning and leaves the text alone. The only function with an `error`
result is `Track.UnmarshalJSON`, which reads JSON you give it.

A Go string can hold bytes that are not valid UTF-8. Every function reads such input with the
WHATWG rule, the one `TextDecoder` uses in JavaScript and `String::from_utf8_lossy` in Rust: each
maximal ill-formed subsequence becomes one U+FFFD. So the Go module and the Rust crate read the
same bytes as the same text, and every output string is valid UTF-8.

```go
fmt.Printf("%q\n", trackparse.Parse("Noisia - Sti\xffgma\xe2\x82").Title) // "Sti�gma�"
```

`\xff` is one replacement character, and the truncated three-byte sequence `\xe2\x82` is one more.
(Go's own `for range` over a string would give one per byte.)

## How it is tested

**Docs:** [How it's tested](https://4matic.github.io/trackparse/project/testing/) · [Ports](https://4matic.github.io/trackparse/project/ports/)

- `go test ./...` runs all 13,130 shared fixtures (1,203 written by hand, the rest generated from
  the rules) and checks every output against the JSON Schema. The skip list is empty. The fixture
  test skips itself when `../spec` is missing, as in the module zip that `go get` downloads.
- The R0 primitives (whitespace, word keys, dedup keys, letters, NFC and NFD) are tested against
  values dumped from the JavaScript package.
- Seeded property tests check the spec's invariants on generated input: no panic in any mode,
  `Normalize` idempotent, whitespace and dash noise not changing the result, no word of the input
  going missing, the `Format` round trip, and for tracklists the byte-for-byte round trip, edit
  locality and the shift round trip. Fuzz targets (`go test -fuzz`) check that nothing panics on
  any bytes and that every output string is valid UTF-8.
- The `Example` functions in `example_test.go` are checked by `go test`, so the examples on
  pkg.go.dev run as shown.
- [`scripts/compare-ports.mjs`](https://github.com/4matic/trackparse/blob/main/scripts/compare-ports.mjs)
  runs every fixture input through the JavaScript package and this module and compares the
  complete JSON output, every field of it, not only the ones a fixture asserts. The Python and
  Rust packages are checked the same way.
- CI runs `go vet`, staticcheck and `go test -race` on Go 1.22 and on the latest stable, and fails
  if `go.mod` gains a `require` line or the code calls `sort.Slice` (JavaScript sorts are stable,
  `sort.Slice` is not).

## Unicode version

The Unicode facts the rules need (letters, uppercase letters, the lowercase mapping, and the NFC
and NFD tables) are generated from Node 24, which has Unicode 17, and compiled into the module.
The `unicode` package of the Go standard library is not used for them, so the output does not
depend on the Go toolchain you build with, although Go 1.22 ships Unicode 15 and later releases
newer versions.

This differs from the Rust crate, which takes NFC and NFD from the `unicode-normalization` crate
and its Unicode 18 tables. Unicode 18 added 32 combining marks, so a string containing one of
them can come out with its marks in another order in Rust than in JavaScript and Go. Strings
without those marks are not affected, and every shared fixture gives the same bytes in all four
ports.

## Performance

There are no regular expressions, so there is no regex backtracking. Measured with `go test
-bench` on Go 1.24, on a 2013 desktop CPU (Intel i7-4770S) that was also running other jobs:

| Input | Time |
|---|---|
| A typical track (the string at the top of this page) | about 0.1 ms |
| 10,000 code points of repeated blocks (`a & `, `feat. X `, `(Remix) `, `[HD] `, `- `) | 6 to 18 ms |
| A 10,000-line tracklist | about 0.5 s |

The JavaScript package on Node 24 took about the same on that machine (0.12 ms for the typical
track, 12 ms for 10,000 code points of `(Remix) `).

## Versioning

The module version is the spec version (`SpecVersion`). Releases are tagged `go/vX.Y.Z` in the
repository, the tag form the Go toolchain expects for a module in a subfolder, and the Go
module proxy serves them from there. There is no upload step: the tag is the release.

While it is 0.x, a release that changes the output for some input bumps the minor version, and a
patch release only fixes bugs. `go get -u` moves to the newest minor, so if you store parsed
results, upgrade on purpose and read the changelog first. The changelog will be
`go/CHANGELOG.md` in the repository, written by the release that publishes the first version.

## Other languages

**Docs:** [Ports](https://4matic.github.io/trackparse/project/ports/)

The same parser exists for JavaScript, Python and Rust. All four implement one spec, pass the
same fixtures, are released together under one version number, and return identical JSON for
every fixture input. You can parse in one language and read the result in another. The one known
difference is described under [Unicode version](#unicode-version).

| Language | Package | Install |
|---|---|---|
| JavaScript / TypeScript | [trackparse on npm](https://www.npmjs.com/package/trackparse) | `npm install trackparse` |
| Python | [trackparse on PyPI](https://pypi.org/project/trackparse/) | `pip install trackparse` |
| Rust | [trackparse on crates.io](https://crates.io/crates/trackparse) | `cargo add trackparse` |
| Go | [github.com/4matic/trackparse/go](https://pkg.go.dev/github.com/4matic/trackparse/go) | `go get github.com/4matic/trackparse/go` |

## Limitations

**Docs:** [Limitations and roadmap](https://4matic.github.io/trackparse/project/roadmap/)

- `Parse` takes one track string. Whole tracklists go through `ParseTracklist`, which reads start
  times only: no durations, trailing times or time ranges.
- Catalog numbers, key and BPM are not fields. They stay in the title.
- There is no artist database. Pass `KnownArtists` for names that contain a joiner
  (`Earth, Wind & Fire`).

## Development

From `go/` in the [repository](https://github.com/4matic/trackparse) (Node 24 is needed for the
codegen scripts and the artist-side test data only):

```sh
node scripts/gen_data.mjs --check      # spec/data codegen is up to date
node scripts/gen_unicode.mjs --check   # Unicode tables are up to date
pnpm --filter trackparse build && node scripts/dump_artist_cases.mjs   # data for the artist-side tests
gofmt -l .                             # prints nothing
go vet ./...
go test -race ./...                    # every shared fixture, primitives, examples, API
TRACKPARSE_REPORT=1 go test -run TestFixtures -v   # pass rate per fixture file and per rule
```

Files mirror the JS reference one to one (`scanner.go` is `scanner.ts`, `tl_parse.go` is
`tracklist/parse.ts`, and so on) and cite the same spec rule IDs. Behaviour changes start in the
spec, not here: see [AGENTS.md](https://github.com/4matic/trackparse/blob/main/AGENTS.md).

## Links

- [Documentation](https://4matic.github.io/trackparse/), with every example in JavaScript, Python,
  Rust and Go, and a [playground](https://4matic.github.io/trackparse/playground/) that runs the
  parser in your browser
- [API documentation on pkg.go.dev](https://pkg.go.dev/github.com/4matic/trackparse/go)
- [Spec](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md)
- [Project README](https://github.com/4matic/trackparse#readme)
- [GitHub repository](https://github.com/4matic/trackparse)
- [npm package](https://www.npmjs.com/package/trackparse) · [PyPI package](https://pypi.org/project/trackparse/) · [Rust crate](https://crates.io/crates/trackparse)

## License

[MIT](https://github.com/4matic/trackparse/blob/main/LICENSE)
