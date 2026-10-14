---
title: Options
description: Every option of parse, parseArtists and createParser, including custom keywords.
---

```ts
interface ParseOptions {
  mode?: "auto" | "clean" | "youtube" | "filename" | "tracklist";
  knownArtists?: string[];
  uploader?: string;
  splitAnd?: "auto" | "always" | "never";
  keywords?: KeywordOptions;
}
```

| Option | Default | |
|---|---|---|
| `mode` | `"auto"` | Which [mode](/trackparse/guides/modes/) to use. `auto` picks `filename`, `youtube` or `clean` from the string |
| `knownArtists` | `[]` | Names that are never split, like `Chase & Status` or `Earth, Wind & Fire`. Matched case- and accent-insensitively |
| `uploader` | – | The YouTube channel. In youtube mode it becomes the artist when there's no separator. `- Topic` and `VEVO` are dropped |
| `splitAnd` | `"auto"` | `auto` splits on `and` only after a comma (`A, B and C`), so `Simon and Garfunkel` stays whole. `always` and `never` do what they say |
| `keywords` | – | Extra vocabulary, see below |

Python takes the same options as keyword arguments: `mode`, `known_artists`, `uploader`,
`split_and`, `keywords`.

Options never cause an error. An unknown mode, a non-array `knownArtists` or a keyword of the wrong
type is ignored and the default is used.

## keywords

`keywords` adds words to the built-in vocabulary. It can't take any away.

```ts
interface KeywordOptions {
  versionHeads?: Record<string, VersionType>;   // { rerub: "rework" }
  descriptors?: string[];                        // extra style words, like the built-in "D'n'B"
  genres?: string[];                             // extra genre names (junk kind 'genre')
  junk?: Record<string, JunkKind>;               // { "live stream": "other" }
  featMarkers?: string[];                        // extra feat markers
}
```

Keys are compared the same way as the built-in words: ASCII letters are lowercased and one trailing
`. , ! ? : ;` is ignored.

```js
import { createParser } from "trackparse";

const parser = createParser({
  knownArtists: ["Chase & Status", "Camo & Krooked"],
  keywords: { versionHeads: { rerub: "rework" } },
});

const t = parser.parse("Chase & Status x Camo & Krooked - Track (Alix Perez Rerub)");
t.artists.map((a) => a.name);   // [ 'Chase & Status', 'Camo & Krooked' ]
t.versions[0].type;             // 'rework'
```

## Tracklist options

`parseTracklist` and `applyEdits` take `mode` (default `"tracklist"`), `knownArtists`, `splitAnd`
and `keywords`, plus two of their own:

| Option | Default | |
|---|---|---|
| `audioSeconds` | – | Length of the audio. Turns on the `time-after-audio-end` check |
| `markerWords` | `[]` | Extra marker words. With `["drop"]`, `03:00 Drop` is a marker, not a track |

See the [Tracklist API](/trackparse/reference/tracklist/).
