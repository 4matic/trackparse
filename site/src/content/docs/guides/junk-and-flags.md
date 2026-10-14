---
title: Junk, flags and prefixes
description: Video tags, quality markers, labels and platform suffixes; explicit and clean flags; years, track numbers and cue times.
---

## Junk

Junk is everything that describes the upload rather than the song. It leaves the title, and it goes
into `junk` with a kind so you can decide what to do with it.

```js
const t = parse("Daft Punk - One More Time (Official Music Video) [HD]");
t.title;  // 'One More Time'
t.junk;   // [ { raw: 'Official Music Video', kind: 'video' }, { raw: 'HD', kind: 'quality' } ]
```

| Kind | Examples |
|---|---|
| `video` | `Official Video`, `Official Music Video`, `Visualizer` |
| `audio` | `Official Audio`, `Audio` |
| `lyrics` | `Lyrics`, `Lyric Video` |
| `quality` | `HD`, `HQ`, `4K`, `1080p` |
| `promo` | `Free Download`, `Out Now`, `Premiere` |
| `platform` | `- YouTube`, `- SoundCloud` suffixes |
| `label` | `NCS Release`, `Monstercat Release` |
| `genre` | a bracketed genre such as `[Drum & Bass]` |
| `other` | a bare `(Official)` |

The phrases are in [`junk.json`](https://github.com/4matic/trackparse/blob/main/spec/data/junk.json)
(plus `genres.json` for the genre kind) and [`platform-suffixes.json`](https://github.com/4matic/trackparse/blob/main/spec/data/platform-suffixes.json).
Seeing junk is also one of the ways `mode: "auto"` decides a string came from YouTube.

```js
parse("Porter Robinson - Shelter - YouTube").junk;   // [ { raw: 'YouTube', kind: 'platform' } ]
parse("Elektronomia - Sky High [NCS Release]").junk; // [ { raw: 'NCS Release', kind: 'label' } ]
```

Catalogue numbers like `[HOSP123]` are not junk yet. They stay in the title. See the
[roadmap](/trackparse/project/roadmap/).

## Flags

```js
parse("Eminem - Without Me (Explicit)").flags;
// { explicit: true, clean: false, unknownArtist: false, unknownTitle: false }
```

`explicit` and `clean` come from `(Explicit)`, `[Clean]` and the like. `unknownArtist` and
`unknownTitle` are set when the artist or title is a placeholder such as `ID`.

## Year

A year on its own in brackets, or as a dash suffix, goes into `year`:

```js
const t = parse("Noisia - Stigma (2015)");
t.title;  // 'Stigma'
t.year;   // 2015
```

A year inside a version (`Remastered 2015`) belongs to that version instead, in `versions[i].year`.

## Track numbers

```js
parse("01. Aphex Twin - Xtal").position;   // { raw: '01', number: 1 }
parse("2 Unlimited - No Limit").position;  // null
```

`raw` keeps the digits as written, so you can tell `01` from `1`. These count as track numbers:

- a number followed by `.` or `)`: `01. Title`, `1) Intro`;
- a zero-padded number and a space: `01 Aphex Twin - Xtal`;
- a number and a dash, when another dash follows: `3 - Artist - Title`;
- in file names, also `07_Burial`, `01-Track` and `3 My Song.mp3`.

A plain number before a word is part of the name: `2 Unlimited`, `50 Cent`, `808 State`.

## Cue times

A time at the very start becomes `timestamp`, with seconds worked out for you:

```js
parse("[12:34] Noisia - Stigma").timestamp;   // { raw: '12:34', seconds: 754 }
parse("1:02:03 Noisia - Stigma").timestamp;   // { raw: '1:02:03', seconds: 3723 }
```

For whole tracklists with a time on every line, use [`parseTracklist`](/trackparse/guides/tracklists/)
instead. It understands many more time formats and checks the order.
