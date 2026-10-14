---
title: Other functions
description: parseArtists, format, normalize, allArtists, createParser and SPEC_VERSION.
---

All of these are exported from `trackparse`. Python names are in brackets.

## parseArtists(input, options?)

*(Python: `parse_artists`)*

Splits a bare artist string, with no title, into credits.

```js
parseArtists("Sub Focus, Wilkinson & Dimension feat. Kojo").map((a) => `${a.name}:${a.role}`);
// [ 'Sub Focus:primary', 'Wilkinson:primary', 'Dimension:primary', 'Kojo:featured' ]
```

Takes `knownArtists`, `splitAnd` and `keywords`. Useful for the artist field of ID3 tags or a
streaming API, where the title is already separate.

## format(track, options?)

*(Python: `format_track`)*

Renders a `ParsedTrack` back to a string. The options are `feat`, `featMarker`, `joiners`,
`versions`, `producers` and `position`. [Formatting](/trackparse/guides/formatting/) has examples
of each.

## normalize(input)

The Unicode cleanup every function runs first. It applies NFC, removes invisible characters
(zero-width spaces, BOMs, soft hyphens), turns dash, bracket and quote variants into their plain
ASCII forms, and collapses whitespace.

```js
normalize("Noisia​ — “Stigma”");   // 'Noisia - "Stigma"'
```

You don't need to call it before `parse`. It's exported because it's handy for building lookup
keys.

## allArtists(track)

*(Python: `all_artists`)*

Every credit of a track in one list: the artists, then every remixer, with duplicates removed.

```js
allArtists(parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)")).map((a) => `${a.name}:${a.role}`);
// [ 'Wilkinson:primary', 'Becky Hill:featured', 'Sub Focus:remixer' ]
```

## createParser(options)

*(Python: `create_parser`)*

Compiles options and keywords once and returns an object with `parse` and `parseArtists`. Per-call
options are merged over the compiled ones. Use it when you parse many strings with the same
`knownArtists` or `keywords`.

```js
import { createParser } from "trackparse";

const parser = createParser({
  knownArtists: ["Chase & Status", "Camo & Krooked"],
  keywords: { versionHeads: { rerub: "rework" } },
});

parser.parse("Chase & Status x Camo & Krooked - Track (Alix Perez Rerub)").versions[0].type;
// 'rework'
```

## SPEC_VERSION

The version of the [spec](/trackparse/reference/spec/) the package implements, as a string. The
packages and the spec are released together, so it's the same as the package version.
