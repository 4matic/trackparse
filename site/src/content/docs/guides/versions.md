---
title: Versions and remixes
description: The 27 version types, how generic words like "Mix" and "Edit" get their meaning, and what each Version field holds.
---

A version is anything that says which take of a song this is: a remix, a VIP, a radio edit, a live
recording, a remaster, a sped-up TikTok cut. trackparse reads them from brackets and from dash
suffixes, and puts each one in `versions`, left to right.

```ts
{
  type: VersionType;          // 'remix', 'vip', 'radio', …
  raw: string;                // the text as written, without brackets: 'Sub Focus VIP Remix'
  artists: Artist[];          // remixers, role 'remixer'
  modifiers: VersionType[];   // other type words that didn't win: [ 'extended' ]
  descriptor: string | null;  // leftover words: "Taylor's", 'at Wembley'
  year: number | null;
  unknownArtist: boolean;     // 'ID Remix'
  delimiter: "(" | "[" | "{" | "-";
}
```

`title` never includes versions. `fullTitle` is the title with them put back, in their original
brackets.

## The 27 types

| Group | Types |
|---|---|
| Rework by someone else | `remix` `bootleg` `vip` `edit` `flip` `refix` `rework` `mashup` `blend` `dub` `mix` |
| Length and format | `extended` `radio` `club` `original` `instrumental` `acapella` |
| Recording | `live` `acoustic` `remaster` `demo` `reprise` `cover` `version` |
| Speed | `spedUp` `slowed` `nightcore` |

The keywords behind them are in
[`version-keywords.json`](https://github.com/4matic/trackparse/blob/main/spec/data/version-keywords.json).

## Generic words borrow their meaning

"Mix", "Edit" and "Version" don't say much on their own. trackparse looks at the word in front:

| Input | `type` | Why |
|---|---|---|
| `(Radio Edit)` | `radio` | "Radio" decides; "Edit" is generic |
| `(Extended Mix)` | `extended` | same idea |
| `(Levela Mix)` | `remix`, by Levela | a name before "Mix" means a remix by that name |
| `(Extended VIP Mix)` | `vip`, modifiers `[extended]` | the strongest word wins, the rest become modifiers |
| `(Taylor's Version)` | `version`, descriptor `"Taylor's"` | nothing to borrow, so the words are kept as a descriptor |

```js
parse("Pendulum - Watercolour (Extended VIP Mix)").versions[0];
// { type: 'vip', raw: 'Extended VIP Mix', artists: [], modifiers: [ 'extended' ],
//   descriptor: null, year: null, unknownArtist: false, delimiter: '(' }
```

## Several versions

Two groups give two versions. So does a slash inside one group:

```js
parse("Avicii - Levels (Skrillex Remix) [Radio Edit]").versions.map((v) => v.type);
// [ 'remix', 'radio' ]

parse("Dimension - Offshore (Mefjus Remix / Noisia Remix)").versions.map((v) => v.artists[0].name);
// [ 'Mefjus', 'Noisia' ]
```

## Dash suffixes

Streaming services love writing versions after a second dash. Those are read too, and the
delimiter is `-`:

```js
const t = parse("The Beatles - Hey Jude - Remastered 2015");
t.title;        // 'Hey Jude'
t.versions[0];  // { type: 'remaster', raw: 'Remastered 2015', year: 2015, delimiter: '-', … }
```

A dash suffix that isn't a version stays in the title. In a [tracklist](/trackparse/guides/tracklists/#tracklist-mode)
the rule is stricter, because there the first dash is almost always the separator.

## Unknown remixers

`(ID Remix)` is a remix by someone not yet announced. You get the version with
`unknownArtist: true` and no artists, rather than an artist called "ID".

## Your own words

If your catalogue uses a word trackparse doesn't know, add it with `keywords.versionHeads`:

```js
import { createParser } from "trackparse";

const parser = createParser({ keywords: { versionHeads: { rerub: "rework" } } });
parser.parse("Chase & Status x Camo & Krooked - Track (Alix Perez Rerub)").versions[0].type;
// 'rework'
```

Keywords only add to the built-in vocabulary. They can't remove words. See [Options](/trackparse/reference/options/#keywords).
