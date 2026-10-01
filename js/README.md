# trackparse

Parse music track names into structured metadata: artists, featured artists, remixers, version
(remix, extended, live, remaster), producers and a clean title.

[![npm](https://img.shields.io/npm/v/trackparse.svg)](https://www.npmjs.com/package/trackparse)
[![js](https://img.shields.io/github/actions/workflow/status/4matic/trackparse/js.yml?branch=main&label=js)](https://github.com/4matic/trackparse/actions/workflows/js.yml)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/4matic/trackparse/blob/main/LICENSE)

This is the JavaScript/TypeScript reference implementation of the
[trackparse spec](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md). It passes all
15,000+ shared fixtures. Zero dependencies, ESM and CommonJS, TypeScript types included, never
throws.

The principle is **extract, don't delete**: everything recognised lands in a field (artists with
roles, versions with a type and their own credits, junk with a kind, flags, position), and
everything else stays in the title verbatim.

## Install

```sh
npm install trackparse
# or: pnpm add trackparse / yarn add trackparse / bun add trackparse
```

Node 18 or later. No Node-specific APIs are used, so it also runs in browsers and other runtimes.

## Quick start

```js
import { parse } from "trackparse";
// const { parse } = require("trackparse");

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
track.mode;                        // 'youtube' (auto-detected from the junk group)
```

## API

### `parse(input, options?) → ParsedTrack`

Parses one track string. Every key of the result is always present.

### `parseArtists(input, options?) → Artist[]`

Splits an artist string (no title) into credits.

```js
parseArtists("Sub Focus, Wilkinson & Dimension feat. Kojo");
// [
//   { name: 'Sub Focus', role: 'primary', joiner: null, source: 'artist' },
//   { name: 'Wilkinson', role: 'primary', joiner: ',', source: 'artist' },
//   { name: 'Dimension', role: 'primary', joiner: '&', source: 'artist' },
//   { name: 'Kojo', role: 'featured', joiner: 'feat.', source: 'artist' }
// ]
```

### `format(track, options?) → string`

Renders a `ParsedTrack` back to `Artists - Title (versions) (prod. ...)`.

```js
const t = parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)");

format(t);                                          // 'Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)'
format(t, { feat: "title", joiners: "canonical" }); // 'Wilkinson - Afterglow (feat. Becky Hill) (Sub Focus Remix)'
format(t, { feat: "omit", versions: "none" });      // 'Wilkinson - Afterglow'
```

| Option | Default | Effect |
|---|---|---|
| `feat` | `"source"` | `"source"` (where they were found), `"artist"`, `"title"` (`(feat. X)` after the title) or `"omit"`. |
| `featMarker` | – | Override the feat marker (`"featuring"`). |
| `joiners` | `"original"` | `"canonical"`: `and` → `&`, `vs` → `vs.`, feat markers → `feat.`. |
| `versions` | `"all"` | `"none"` drops versions. |
| `producers` | `true` | Append `(prod. by X)`. |
| `position` | `false` | Prefix the track number (`01. `). |

### `normalize(input) → string`

The Unicode cleanup applied before parsing: NFC, zero-width spaces, BOM and soft hyphens removed, fullwidth
brackets, smart quotes and dash variants mapped to ASCII, whitespace collapsed. Idempotent.

```js
normalize("  Artist​ — Title （Remix）  “Quoted”  ");
// 'Artist - Title (Remix) "Quoted"'
```

### `allArtists(track) → Artist[]`

Track credits plus every version's remixers, deduplicated.

```js
allArtists(parse("Camo & Krooked - Kallisto (Noisia Remix)", { knownArtists: ["Camo & Krooked"] }))
  .map((a) => `${a.name} (${a.role})`);
// [ 'Camo & Krooked (primary)', 'Noisia (remixer)' ]
```

### `createParser(options) → { parse, parseArtists }`

Compiles options, including extra vocabulary, once. Per-call options are merged over the base.

```js
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

The spec version this build implements, e.g. `"0.2.0"`. Always equal to the package version.

## Options

| Option | Default | Effect |
|---|---|---|
| `mode` | `"auto"` | `"clean"` (tags, store metadata), `"youtube"` (video titles), `"filename"`, or `"auto"` to detect. |
| `uploader` | – | YouTube channel, used as the artist when no separator is found. ` - Topic` and `VEVO` are stripped. |
| `knownArtists` | `[]` | Names never split (`Chase & Status`). There is no built-in list. |
| `splitAnd` | `"auto"` | `and` splits only after a comma in the same list (`A, B and C`), so `Simon and Garfunkel` stays one. `"always"` / `"never"`. |
| `keywords` | – | Extra vocabulary: `versionHeads`, `descriptors`, `genres`, `junk`, `featMarkers`. |

```js
parse("Chase & Status - Blind Faith (Loadstar Remix)").artists.map((a) => a.name);
// [ 'Chase', 'Status' ]
parse("Chase & Status - Blind Faith (Loadstar Remix)", { knownArtists: ["Chase & Status"] })
  .artists.map((a) => a.name);
// [ 'Chase & Status' ]

parse("Jay-Z-Numb").title;                                        // 'Jay-Z-Numb' (clean: no split)
parse("Jay-Z-Numb", { mode: "youtube" }).artists.map((a) => a.name); // [ 'Jay-Z' ]
```

## Output types

All types are exported (`import type { ParsedTrack } from "trackparse"`).

```ts
interface ParsedTrack {
  input: string;                          // original, untouched
  mode: "clean" | "youtube" | "filename"; // resolved mode
  position: { raw: string; number: number } | null;
  timestamp: { raw: string; seconds: number } | null;
  artists: Artist[];                      // primary, featured, producer (not remixers)
  title: string;                          // clean title
  fullTitle: string;                      // title + versions with their brackets
  versions: Version[];
  year: number | null;
  flags: { explicit: boolean; clean: boolean; unknownArtist: boolean; unknownTitle: boolean };
  junk: { raw: string; kind: JunkKind }[];
  warnings: Warning[];
}

interface Artist {
  name: string;
  role: "primary" | "featured" | "remixer" | "producer";
  joiner: string | null;                  // "&", ",", "x", "ft.", "prod. by"... as written
  source: "artist" | "title" | "version";
}

interface Version {
  type: VersionType;                      // remix, vip, bootleg, extended, radio, live, remaster...
  raw: string;                            // "Sub Focus VIP Remix"
  artists: Artist[];                      // remixers
  modifiers: string[];                    // ["vip"]
  descriptor: string | null;              // "Taylor's", "at Wembley", genres
  year: number | null;
  unknownArtist: boolean;                 // "ID Remix"
  delimiter: "(" | "[" | "{" | "-";
}
```

- `VersionType`: `remix`, `bootleg`, `vip`, `edit`, `flip`, `refix`, `rework`, `mashup`, `blend`,
  `dub`, `mix`, `extended`, `radio`, `club`, `original`, `instrumental`, `acapella`, `live`,
  `acoustic`, `remaster`, `demo`, `reprise`, `cover`, `version`, `spedUp`, `slowed`, `nightcore`.
- `JunkKind`: `video`, `audio`, `lyrics`, `quality`, `promo`, `platform`, `label`, `genre`, `other`.
- `Warning`: `noSeparator`, `ambiguousSeparator`, `unspacedDashSplit`, `asymmetricDashSplit`,
  `bySplit`, `quotedTitleSplit`, `ambiguousMixCredit`, `unbalancedBrackets`.

The full field reference, a gallery of examples and the comparison with other libraries are in the
[main README](https://github.com/4matic/trackparse#readme). Exact behaviour is defined by the
[spec](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md) and its
[fixtures](https://github.com/4matic/trackparse/tree/main/spec/fixtures).

## Limitations

One track string per call (no multi-line tracklists yet), no label/catalog, key or BPM fields, and
no built-in artist database: pass `knownArtists` for names that contain a joiner.

## License

MIT © Maksim Maksimov
