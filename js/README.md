# trackparse

[![npm](https://img.shields.io/npm/v/trackparse?logo=npm&logoColor=white)](https://www.npmjs.com/package/trackparse)
[![types](https://img.shields.io/npm/types/trackparse?logo=typescript&logoColor=white)](https://www.npmjs.com/package/trackparse)
[![tests](https://img.shields.io/github/actions/workflow/status/4matic/trackparse/js.yml?branch=main&label=tests&logo=github)](https://github.com/4matic/trackparse/actions/workflows/js.yml)
[![license](https://img.shields.io/badge/license-MIT-blue)](https://github.com/4matic/trackparse/blob/main/LICENSE)

**[Documentation](https://4matic.github.io/trackparse/)** · **[Getting started](https://4matic.github.io/trackparse/start/getting-started/)** · **[Playground](https://4matic.github.io/trackparse/playground/)**

**Turn messy track names into structured data: who made it, who's featured, who remixed it, and
what's just noise.**

```text
01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]
```

Most title parsers split that into `"Wilkinson ft. Becky Hill & Tom Cane"` and
`"Afterglow (Sub Focus VIP Remix)"` and stop. Others "clean" it and throw the feat and the remix
away. trackparse keeps all of it. Everything it recognizes goes into a field, and anything it
doesn't recognize stays in the title, word for word.

This is the JavaScript and TypeScript package of
[trackparse](https://github.com/4matic/trackparse), and the reference implementation. Its behaviour
is defined by a [shared spec](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md) and
pinned by 13,130 fixtures. It passes all of them, and the
[Python, Rust and Go packages](#other-languages) return the same output for every one.

- Node 18 or later, browsers and other runtimes (no Node-specific APIs)
- No dependencies
- ESM and CommonJS, TypeScript types included
- Never throws

## Install

```sh
npm install trackparse
# or: pnpm add trackparse / yarn add trackparse / bun add trackparse
```

## Quick start

**Docs:** [Getting started](https://4matic.github.io/trackparse/start/getting-started/)

```js
import { format, parse } from "trackparse";
// const { format, parse } = require("trackparse");

const track = parse(
  "01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]",
);

track.artists.map((a) => `${a.name} (${a.role})`);
// [ 'Wilkinson (primary)', 'Becky Hill (featured)', 'Tom Cane (featured)' ]
track.title;                       // 'Afterglow'
track.fullTitle;                   // 'Afterglow (Sub Focus VIP Remix)'
track.versions[0].type;            // 'remix'
track.versions[0].artists[0].name; // 'Sub Focus'
track.versions[0].modifiers;       // [ 'vip' ]
track.junk;                        // [ { raw: 'Official Video', kind: 'video' } ]
track.position;                    // { raw: '01', number: 1 }
track.mode;                        // 'youtube' (detected from the junk group)

format(track, { joiners: "canonical" });
// 'Wilkinson feat. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix)'
```

The result is a plain object, so `JSON.stringify(track)` gives the spec's JSON form. Every key is
always present:

```js
console.log(JSON.stringify(track, null, 2));
```

<details>
<summary>Output</summary>

```json
{
  "input": "01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]",
  "mode": "youtube",
  "position": { "raw": "01", "number": 1 },
  "timestamp": null,
  "artists": [
    { "name": "Wilkinson", "role": "primary", "joiner": null, "source": "artist" },
    { "name": "Becky Hill", "role": "featured", "joiner": "ft.", "source": "artist" },
    { "name": "Tom Cane", "role": "featured", "joiner": "&", "source": "artist" }
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
  "junk": [{ "raw": "Official Video", "kind": "video" }],
  "warnings": []
}
```

(Reformatted for width; the keys and values are exactly what `JSON.stringify` prints.)

</details>

## API

**Docs:** [parse and ParsedTrack](https://4matic.github.io/trackparse/reference/parse/) · [Other functions](https://4matic.github.io/trackparse/reference/functions/)

```ts
parse(input: string, options?: ParseOptions): ParsedTrack
parseArtists(input: string, options?: ParseOptions): Artist[]
format(track: ParsedTrack, options?: FormatOptions): string
normalize(input: string): string
allArtists(track: ParsedTrack): Artist[]
createParser(options?: ParseOptions): Parser   // parser.parse(...), parser.parseArtists(...)
SPEC_VERSION: string

// from "trackparse/tracklist"
parseTracklist(text: string, options?: TracklistOptions): Tracklist
applyEdits(text: string, edits: Edit[], options?: TracklistOptions): EditResult
```

### `parse(input, options?)`

**Docs:** [parse and ParsedTrack](https://4matic.github.io/trackparse/reference/parse/) · [Options](https://4matic.github.io/trackparse/reference/options/)

Parses one track string.

| Option | Default | |
|---|---|---|
| `mode` | `"auto"` | `"clean"`, `"youtube"`, `"filename"`, `"tracklist"`, or `"auto"` to detect from the string. See [Modes](#modes) |
| `uploader` | – | YouTube channel name, used as the artist when there's no separator (`- Topic` and `VEVO` are dropped) |
| `knownArtists` | `[]` | Names that are never split. There is no built-in list |
| `splitAnd` | `"auto"` | `"auto"` splits on `and` only after a comma (`A, B and C`), so `Simon and Garfunkel` stays whole. Also `"always"`, `"never"` |
| `keywords` | – | Extra vocabulary, added to the built-in lists (see below) |

```js
parse("Chase & Status - Baddadan").artists.map((a) => a.name);
// [ 'Chase', 'Status' ]
parse("Chase & Status - Baddadan", { knownArtists: ["Chase & Status"] }).artists.map((a) => a.name);
// [ 'Chase & Status' ]

parse("Simon and Garfunkel - Mrs. Robinson", { splitAnd: "always" }).artists.map((a) => a.name);
// [ 'Simon', 'Garfunkel' ]
```

`keywords` is an object with any of these keys:

| Key | Type | |
|---|---|---|
| `versionHeads` | `Record<string, VersionType>` | Word that sets a version type (`{ rerub: "rework" }`) |
| `descriptors` | `string[]` | Words kept as a version's `descriptor` |
| `genres` | `string[]` | Genre words |
| `junk` | `Record<string, JunkKind>` | Phrase to junk kind (`{ "some label rip": "label" }`) |
| `featMarkers` | `string[]` | Extra feat markers |

```js
parse("Artist - Song [Some Label Rip]").title;
// 'Song [Some Label Rip]'
parse("Artist - Song [Some Label Rip]", { keywords: { junk: { "some label rip": "label" } } }).junk;
// [ { raw: 'Some Label Rip', kind: 'label' } ]
```

### `ParsedTrack`

**Docs:** [ParsedTrack fields](https://4matic.github.io/trackparse/reference/parse/#parsedtrack)

```ts
interface ParsedTrack {
  input: string;                          // the original string, untouched
  mode: "clean" | "youtube" | "filename" | "tracklist"; // resolved mode
  position: { raw: string; number: number } | null;     // track number: "01." gives 1
  timestamp: { raw: string; seconds: number } | null;   // leading cue time: "[12:34]" gives 754
  artists: Artist[];                      // primary, featured and producer credits (not remixers)
  title: string;                          // clean title; brackets it doesn't recognize stay in
  fullTitle: string;                      // title with the versions re-attached
  versions: Version[];                    // remixes and other versions, left to right
  year: number | null;                    // from "(2015)" or "- 2015"
  flags: { explicit: boolean; clean: boolean; unknownArtist: boolean; unknownTitle: boolean };
  junk: { raw: string; kind: JunkKind }[]; // what was taken out, each with a kind
  warnings: Warning[];                    // heuristics that fired
}

interface Artist {
  name: string;
  role: "primary" | "featured" | "remixer" | "producer";
  joiner: string | null;                  // "&", ",", "x", "ft.", "prod. by" as written; null for the first name
  source: "artist" | "title" | "version"; // left of the dash, right of it, or inside a version
}

interface Version {
  type: VersionType;                      // remix, vip, bootleg, extended, radio, live, remaster...
  raw: string;                            // "Sub Focus VIP Remix"
  artists: Artist[];                      // remixers
  modifiers: string[];                    // type words that didn't become the type: ["vip"]
  descriptor: string | null;              // leftover words: "Taylor's", "at Wembley"
  year: number | null;
  unknownArtist: boolean;                 // "ID Remix"
  delimiter: "(" | "[" | "{" | "-";       // the bracket it was in, or "-" for a dash suffix
}
```

```js
parse("Pendulum - Watercolour (Extended VIP Mix)").versions[0];
// { type: 'vip', raw: 'Extended VIP Mix', artists: [], modifiers: [ 'extended' ],
//   descriptor: null, year: null, unknownArtist: false, delimiter: '(' }

parse("Queen - Bohemian Rhapsody (Live at Wembley 1986)").versions[0];
// { type: 'live', raw: 'Live at Wembley 1986', artists: [], modifiers: [],
//   descriptor: 'at Wembley', year: 1986, unknownArtist: false, delimiter: '(' }
```

- `VersionType`: `remix`, `bootleg`, `vip`, `edit`, `flip`, `refix`, `rework`, `mashup`, `blend`,
  `dub`, `mix`, `extended`, `radio`, `club`, `original`, `instrumental`, `acapella`, `live`,
  `acoustic`, `remaster`, `demo`, `reprise`, `cover`, `version`, `spedUp`, `slowed`, `nightcore`.
- `JunkKind`: `video`, `audio`, `lyrics`, `quality`, `promo`, `platform`, `label`, `genre`, `other`.
- `Warning`: see [Warnings](#warnings).

The same shape is published as a JSON Schema,
[`parsed-track.schema.json`](https://github.com/4matic/trackparse/blob/main/spec/schema/parsed-track.schema.json).

### `parseArtists(input, options?)`

**Docs:** [parseArtists](https://4matic.github.io/trackparse/reference/functions/#parseartistsinput-options) · [Artists and credits](https://4matic.github.io/trackparse/guides/artists/)

Splits a bare artist string (no title). Takes the same options as `parse`.

```js
import { parseArtists } from "trackparse";

parseArtists("Sub Focus, Wilkinson & Dimension feat. Kojo");
// [
//   { name: 'Sub Focus', role: 'primary', joiner: null, source: 'artist' },
//   { name: 'Wilkinson', role: 'primary', joiner: ',', source: 'artist' },
//   { name: 'Dimension', role: 'primary', joiner: '&', source: 'artist' },
//   { name: 'Kojo', role: 'featured', joiner: 'feat.', source: 'artist' }
// ]
```

### `format(track, options?)`

**Docs:** [format](https://4matic.github.io/trackparse/reference/functions/#formattrack-options) · [Formatting](https://4matic.github.io/trackparse/guides/formatting/)

Renders a `ParsedTrack` back to a string: `Artists - Title (versions) (prod. ...)`.

| Option | Default | |
|---|---|---|
| `feat` | `"source"` | Where featured artists go: `"source"` (where they were), `"artist"`, `"title"` (`(feat. X)` after the title) or `"omit"` |
| `featMarker` | – | Marker for featured artists, such as `"featuring"` |
| `joiners` | `"original"` | `"original"` keeps the joiners as written. `"canonical"` writes `and` as `&`, `vs` as `vs.` and every feat marker as `feat.` |
| `versions` | `"all"` | `"all"` or `"none"` |
| `producers` | `true` | Include `(prod. ...)` credits |
| `position` | `false` | Include the track number (`01. `) |

```js
import { format, parse } from "trackparse";

const t = parse("01. Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)");

format(t);                                          // 'Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)'
format(t, { feat: "title", joiners: "canonical" }); // 'Wilkinson - Afterglow (feat. Becky Hill) (Sub Focus Remix)'
format(t, { feat: "omit", versions: "none" });      // 'Wilkinson - Afterglow'
format(t, { featMarker: "featuring" });             // 'Wilkinson featuring Becky Hill - Afterglow (Sub Focus Remix)'
format(t, { position: true });                      // '01. Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)'
```

### `normalize(input)`

**Docs:** [normalize](https://4matic.github.io/trackparse/reference/functions/#normalizeinput)

The Unicode cleanup every function runs first: NFC, zero-width spaces, BOM and soft hyphens
removed, fullwidth brackets, smart quotes and dash variants mapped to ASCII, whitespace collapsed.
Running it twice changes nothing.

```js
import { normalize } from "trackparse";

normalize("  Artist\u200b — Title （Remix）  “Quoted”  ");
// 'Artist - Title (Remix) "Quoted"'
```

### `allArtists(track)`

**Docs:** [allArtists](https://4matic.github.io/trackparse/reference/functions/#allartiststrack)

Credits plus every remixer, deduplicated.

```js
import { allArtists, parse } from "trackparse";

allArtists(parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)"))
  .map((a) => `${a.name} (${a.role})`);
// [ 'Wilkinson (primary)', 'Becky Hill (featured)', 'Sub Focus (remixer)' ]
```

### `createParser(options?)`

**Docs:** [createParser](https://4matic.github.io/trackparse/reference/functions/#createparseroptions)

Compiles the options once, for many calls. Per-call options are merged over the parser's: a value
passed per call replaces the base one (`knownArtists` included), `undefined` leaves it alone, and
`keywords` objects are merged.

```js
import { createParser } from "trackparse";

const parser = createParser({
  knownArtists: ["Chase & Status", "Camo & Krooked"],
  keywords: { versionHeads: { rerub: "rework" }, junk: { "full album": "other" } },
});

const r = parser.parse("Chase & Status x Camo & Krooked - Track (Alix Perez Rerub) [Full Album]");
r.artists.map((a) => a.name); // [ 'Chase & Status', 'Camo & Krooked' ]
r.versions.map((v) => `${v.type} by ${v.artists.map((a) => a.name).join(", ")}`);
// [ 'rework by Alix Perez' ]
r.junk;                       // [ { raw: 'Full Album', kind: 'other' } ]
```

### `SPEC_VERSION`

The version of the spec this build implements. It equals the package version: the npm package,
the PyPI package, the Rust crate, the Go module and the spec are released together and always
share one version number.

## Modes

**Docs:** [Modes](https://4matic.github.io/trackparse/guides/modes/)

| Mode | For | Adds |
|---|---|---|
| `clean` | Store metadata, tags | Only a spaced dash separates artist and title |
| `youtube` | Video titles, scrobbles | Pipe segments, `"Title" by Artist`, `Artist: "Title"`, unspaced dashes, trailing junk, uploader fallback |
| `filename` | Files | Strips the extension, `_` becomes a space, `01-Track` numbering |
| `auto` | Anything | `filename` for known extensions, `youtube` when it sees junk, pipes or platform suffixes, `clean` otherwise |
| `tracklist` | One line of a tracklist | The first spaced dash always separates; `_-_` too. Never picked by `auto` |

```js
let t = parse('"Numb" by Linkin Park', { mode: "youtube" });
[t.artists.map((a) => a.name), t.title, t.warnings];
// [ [ 'Linkin Park' ], 'Numb', [ 'bySplit' ] ]

t = parse("07_Burial_-_Archangel.mp3");
[t.mode, t.artists.map((a) => a.name), t.title, t.position];
// [ 'filename', [ 'Burial' ], 'Archangel', { raw: '07', number: 7 } ]

parse("Shelter", { mode: "youtube", uploader: "Porter Robinson" }).artists;
// [ { name: 'Porter Robinson', role: 'primary', joiner: null, source: 'artist' } ]

parse("Jay-Z-Numb").title;                                           // 'Jay-Z-Numb' (clean: no split)
parse("Jay-Z-Numb", { mode: "youtube" }).artists.map((a) => a.name); // [ 'Jay-Z' ]
```

## Warnings

**Docs:** [Warnings](https://4matic.github.io/trackparse/reference/warnings/)

The parser never fails, but it says when it had to guess. `track.warnings` is an array of strings,
empty when every decision was unambiguous.

| Warning | Means |
|---|---|
| `noSeparator` | No artist/title separator was found. The whole string is the title |
| `ambiguousSeparator` | A two-part pipe title with no dash was read as artist and title |
| `unspacedDashSplit` | Split at a dash without spaces around it |
| `asymmetricDashSplit` | Split at a dash with a space on one side only |
| `bySplit` | Read `"Title" by Artist` |
| `quotedTitleSplit` | Took a quoted span as the title and the text before it as the artist |
| `ambiguousMixCredit` | `<Name> Mix` was read as a remix, but the name is long enough to be a style |
| `unbalancedBrackets` | A bracket was opened and never closed |

```js
const t = parse("Around the World");
[t.artists, t.title, t.warnings];
// [ [], 'Around the World', [ 'noSeparator' ] ]

parse("Noisia - Stigma (VIP").warnings;
// [ 'unbalancedBrackets' ]
```

Tracklists have their own warnings, with line numbers. They are listed under
[Tracklists](#tracklists).

## Tracklists

**Docs:** [Tracklists](https://4matic.github.io/trackparse/guides/tracklists/) · [Editing tracklists](https://4matic.github.io/trackparse/guides/editing-tracklists/) · [Tracklist API](https://4matic.github.io/trackparse/reference/tracklist/) · [Warnings](https://4matic.github.io/trackparse/reference/warnings/#tracklist-warnings)

`trackparse/tracklist` parses a whole tracklist, one entry per line, and checks it. Each entry
keeps its line exactly as written plus its own line ending, so
`list.entries.map((e) => e.raw + e.eol).join("")` is always the input, CRLF or not. It is a
separate entry point (about 8.4 KiB gzipped), so you only load it if you import it.

```js
import { applyEdits, parseTracklist } from "trackparse/tracklist";
// const { applyEdits, parseTracklist } = require("trackparse/tracklist");

const text = [
  "Hour 2",
  "48:20 Noisia - Stigma",
  "52:47 Mefjus & Kasra - Bandwagon (Phace Remix)",
  "56:05 Black Sun Empire - ID",
  "59:31 Alix Perez - Fade Away",
  "01:03:10 Interview with Hybris",
  "01:07:44 Imanu - Sacrifice",
  "00:11:02 Current Value feat. Teddy Killerz - Payload", // typo for 01:11:02
  "01:15:36 Hybris - Spinning Top",
].join("\r\n");

const list = parseTracklist(text);

list.entries.map((e) => e.kind);
// [ 'header', 'track', 'track', 'track', 'track', 'marker', 'track', 'track', 'track' ]

const e = list.entries[2];
e.time;                                   // { raw: '52:47', seconds: 3167 }
e.parts;
// { lead: '', time: '52:47', gap: ' ', rest: 'Mefjus & Kasra - Bandwagon (Phace Remix)' }
[e.track.artists.map((a) => a.name), e.track.title, e.track.versions[0].type];
// [ [ 'Mefjus', 'Kasra' ], 'Bandwagon', 'remix' ]
list.entries[3].track.flags.unknownTitle; // true (the ID)

list.warnings;
// [
//   { code: 'no-start-at-zero', line: 2, severity: 'info', suggestion: null },
//   { code: 'time-out-of-order', line: 8, severity: 'warning',
//     suggestion: { line: 8, raw: '01:11:02', seconds: 4262 } }
// ]

list.stats;
// { lines: 9, tracks: 7, markers: 1, timed: 8, firstTime: 2900, lastTime: 4536 }
```

The suggestion exists because exactly one time, one digit away from `00:11:02`, fits between its
neighbours. With several candidates or none, `suggestion` is `null`.

`applyEdits` takes an array of edit objects and only rewrites the lines they name:

```js
const fix = list.warnings[1].suggestion;
const result = applyEdits(text, [
  { op: "set-time", line: fix.line, seconds: fix.seconds },
  { op: "shift", seconds: 45, from: 5 },
]);

result.changed;               // [ 5, 6, 7, 8, 9 ]
result.warnings;              // [ { code: 'style-upgraded', edit: 1, line: 5 } ]
result.text.split("\r\n")[4]; // '01:00:16 Alix Perez - Fade Away'  (was 59:31)

applyEdits(text, [{ op: "shift", seconds: -3000, lines: [2, 3] }]).warnings;
// [ { code: 'time-clamped', edit: 0, line: 2 } ]
```

A rewritten time keeps its line's style: `52:47` stays `MM:SS`, and a time that crosses the hour
gets hours like the rest of the list, with a `style-upgraded` warning. A time pushed below zero
stops at zero and warns `time-clamped`. A bad edit changes nothing and warns `edit-invalid`.

Both functions take the options `mode` (default `"tracklist"`), `knownArtists`, `splitAnd` and
`keywords`, as `parse` does, plus `audioSeconds` (the length of the audio, which turns on the
`time-after-audio-end` check) and `markerWords` (extra words that make a line a marker):

```js
parseTracklist("00:00 Noisia - Stigma\n1:02:03 Mefjus - Suicide Bassline", { audioSeconds: 3600 })
  .warnings;
// [ { code: 'time-after-audio-end', line: 2, severity: 'warning', suggestion: null } ]
```

| Result | Fields |
|---|---|
| `Tracklist` | `specVersion`, `entries`, `warnings`, `stats` |
| `TracklistEntry` | `line` (1-based), `kind` (`track`, `marker`, `header`, `link`, `note`, `blank`), `eol`, `raw`, `parts`, `time`, `track` (`ParsedTrack` or `null`), `layered` (the track text after any time starts with `w/`) |
| `EntryParts` | `lead`, `time`, `gap`, `rest`, joined they give `raw` |
| `TracklistWarning` | `code`, `line`, `severity` (`info` or `warning`), `suggestion` |
| `TracklistTime` | `raw`, `seconds` (`entry.time`, or `null` on untimed lines) |
| `TimeSuggestion` | `line`, `raw`, `seconds` (`warning.suggestion`, or `null`) |
| `TracklistStats` | `lines`, `tracks`, `markers`, `timed`, `firstTime`, `lastTime` |
| `EditResult` | `text`, `changed`, `warnings` (`EditWarning`: `code`, `edit`, `line`) |

Lists pasted with list bullets or Markdown emphasis (`* 14:21 Artist - Title`,
`**[**14:21**] Artist - Title**`) read like plain lines; the decoration stays in `raw` and `parts`.
A line whose track text (after any time) starts with `w/` is `layered`: a track played over the
previous one
([Markdown](https://4matic.github.io/trackparse/guides/tracklists/#markdown),
[layered tracks](https://4matic.github.io/trackparse/guides/tracklists/#layered-tracks)).

Warning codes: `time-missing`, `time-out-of-order`, `time-duplicate`, `no-start-at-zero`,
`time-after-audio-end` (needs `audioSeconds`), `invalid-time`, `position-gap`,
`position-duplicate`, `line-unparsed`. Edits: `set-time {line, seconds}`,
`shift {seconds, lines? | from?}` (all timed lines when neither is given), `sort-by-time {}`,
`set-rest {line, rest}`, `insert {after, text}` (`after: 0` inserts at the top), `delete {line}`.
The [Tracklist API reference](https://4matic.github.io/trackparse/reference/tracklist/) explains
each one, and the rules are in
[SPEC R13](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md#r13-tracklists).
`JSON.stringify(list)` matches
[`tracklist.schema.json`](https://github.com/4matic/trackparse/blob/main/spec/schema/tracklist.schema.json).

Every line is parsed with `mode: "tracklist"`, where the first spaced dash always separates artist
and title. `parse()` accepts it too:

```js
parse("Electrosoul System - Liquid Metal").junk;
// [ { raw: 'Liquid Metal', kind: 'genre' } ]
parse("Electrosoul System - Liquid Metal", { mode: "tracklist" }).title;
// 'Liquid Metal'
```

## Errors

No function throws. `parse` returns a complete `ParsedTrack` for any string, `""` included, and a
value that isn't a string is read as `""`. An option of the wrong type or with an unknown value is
ignored and the default is used. `applyEdits` reports a bad edit as an `edit-invalid` warning and
leaves the text alone.

Recognition is done by hand-written scanners, so there is no regex backtracking. A
10,000-code-point input finishes in under 100 ms.

## TypeScript

**Docs:** [Types](https://4matic.github.io/trackparse/reference/parse/#types) · [Tracklist types](https://4matic.github.io/trackparse/reference/tracklist/#types)

The types ship with the package for both ESM and CommonJS. Nothing else needs installing.

```ts
import type { Artist, ParsedTrack, ParseOptions, VersionType } from "trackparse";
import type { Edit, Tracklist, TracklistWarning } from "trackparse/tracklist";
```

- `trackparse` exports `ParsedTrack`, `Artist`, `ArtistRole`, `ArtistSource`, `Version`,
  `VersionType`, `VersionDelimiter`, `Junk`, `JunkKind`, `Flags`, `Position`, `Timestamp`,
  `Warning`, `Mode`, `ModeOption`, `ParseOptions`, `KeywordOptions`, `FormatOptions` and `Parser`.
- `trackparse/tracklist` exports `Tracklist`, `TracklistEntry`, `EntryKind`, `EntryParts`, `Eol`,
  `TracklistTime`, `TracklistWarning`, `TracklistWarningCode`, `TimeSuggestion`, `TracklistStats`,
  `TracklistOptions`, `Edit`, `EditResult`, `EditWarning` and `EditWarningCode`.

Version types, junk kinds, warnings and edit operations are string literal unions, so the compiler
catches a misspelt name.

## How it is tested

**Docs:** [How it's tested](https://4matic.github.io/trackparse/project/testing/)

- 13,130 shared fixtures: 1,203 written by hand and 11,927 generated from the rules. Every output
  is also validated against the JSON Schema.
- Property tests with [fast-check](https://github.com/dubzzz/fast-check) for the spec's invariants:
  no throw on any input, `normalize` is idempotent, whitespace and dash noise doesn't change the
  result, and `parse(format(parse(s)))` round-trips.
- The published build is run against every fixture on Node 18 and 20, as ESM and as CommonJS.
- [`compare-ports.mjs`](https://github.com/4matic/trackparse/blob/main/scripts/compare-ports.mjs)
  runs every fixture input through this package, the Python package, the Rust crate and the Go
  module and diffs the complete JSON output. The difference count is zero.

## Versioning

**Changelog:** [js/CHANGELOG.md](https://github.com/4matic/trackparse/blob/main/js/CHANGELOG.md)

The package version is the spec version (`SPEC_VERSION`). While it is 0.x, a release that changes
the output for some input bumps the minor version, and a patch release only fixes bugs. If you
store parsed results, pin the minor version with a tilde range (`"trackparse": "~0.6.0"` stays on
0.6.x) and read the changelog before moving up.

## Other languages

**Docs:** [Ports](https://4matic.github.io/trackparse/project/ports/)

The same parser exists for Python, Rust and Go. All four implement one spec, pass the same
fixtures, are released together under one version number, and return identical JSON for every
fixture input. You can parse in one language and read the result in another.

| Language | Package | Install |
|---|---|---|
| JavaScript / TypeScript | [trackparse on npm](https://www.npmjs.com/package/trackparse) | `npm install trackparse` |
| Python | [trackparse on PyPI](https://pypi.org/project/trackparse/) | `pip install trackparse` |
| Rust | [trackparse on crates.io](https://crates.io/crates/trackparse) | `cargo add trackparse` |
| Go | [github.com/4matic/trackparse/go](https://pkg.go.dev/github.com/4matic/trackparse/go) | `go get github.com/4matic/trackparse/go` |

## Limitations

**Docs:** [Limitations and roadmap](https://4matic.github.io/trackparse/project/roadmap/)

- `parse()` takes one track string. Whole tracklists go through `trackparse/tracklist`, which reads
  start times only: no durations, trailing times or time ranges.
- Catalog numbers, key and BPM are not fields. They stay in the title.
- There is no artist database. Pass `knownArtists` for names that contain a joiner
  (`Earth, Wind & Fire`).

## Development

From the root of the [repository](https://github.com/4matic/trackparse), with Node 22 or later and
[pnpm](https://pnpm.io/):

```sh
pnpm install
pnpm --filter trackparse test         # every shared fixture, properties, API
pnpm --filter trackparse typecheck
pnpm lint                             # Biome
pnpm --filter trackparse conformance  # pass rate per fixture file and per rule
pnpm --filter trackparse build        # js/dist, ESM and CommonJS
```

The Python, Rust and Go ports mirror this package's modules one to one. Behaviour changes start in the
spec, not here: see [AGENTS.md](https://github.com/4matic/trackparse/blob/main/AGENTS.md).

## Links

- [Documentation](https://4matic.github.io/trackparse/), with every example in JavaScript, Python,
  Rust and Go, and a [playground](https://4matic.github.io/trackparse/playground/) that runs the
  parser in your browser
- [Spec](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md) and its
  [changelog](https://github.com/4matic/trackparse/blob/main/spec/CHANGELOG.md)
- [Changelog](https://github.com/4matic/trackparse/blob/main/js/CHANGELOG.md)
- [Project README](https://github.com/4matic/trackparse#readme)
- [GitHub repository](https://github.com/4matic/trackparse)
- [PyPI package](https://pypi.org/project/trackparse/) · [Rust crate](https://crates.io/crates/trackparse) · [Go module](https://pkg.go.dev/github.com/4matic/trackparse/go)

## License

[MIT](https://github.com/4matic/trackparse/blob/main/LICENSE)
