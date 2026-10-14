---
title: Artists and credits
description: How trackparse splits artist lists, finds features and producers, and when to pass knownArtists.
---

Every credit in the output is an `Artist`:

```ts
{ name: string; role: "primary" | "featured" | "producer" | "remixer"; joiner: string | null; source: "artist" | "title" | "version" }
```

`role` says what the person did. `joiner` is the text that introduced the name, exactly as written
(`"&"`, `","`, `"ft."`, `"prod. by"`), and `null` for the first name of a list. `source` says where
the credit was found: left of the dash (`artist`), right of it (`title`), or inside a version
(`version`).

Remixers live inside `versions`, not in `artists`. Use [`allArtists(track)`](/trackparse/reference/functions/#allartiststrack)
when you want everyone in one list.

## Joiners

`&`, `x`, `vs.`, `,`, `;`, `+`, `/` and `and` separate names. They're joiners, not roles: every name
they produce is still primary.

```js
parse("Skrillex & Diplo x Justin Bieber - Where Are Ü Now").artists;
// Skrillex (joiner null), Diplo (joiner '&'), Justin Bieber (joiner 'x'), all primary

parse("Kanye West vs. Daft Punk - Stronger").artists.map((a) => a.name);
// [ 'Kanye West', 'Daft Punk' ]
```

Some things look like joiners and aren't:

- An unspaced `/`, `&` or `+` never splits. `AC/DC` and `Drum&Bass` stay whole.
- `x` only splits in lowercase, so `Malcolm X` keeps its X.
- `& Sons`, `& Co.`, `& His Orchestra` and a few others never start a new artist. The list is in
  [`no-split-before.json`](https://github.com/4matic/trackparse/blob/main/spec/data/no-split-before.json).
- A comma between two digits (`1,000`) is part of the name.

### `and` is special

`and` is common inside band names, so by default it only splits after a comma has already split the
same list:

```js
parse("Simon and Garfunkel - The Boxer").artists.map((a) => a.name);
// [ 'Simon and Garfunkel' ]

parseArtists("Crosby, Stills, Nash and Young").map((a) => a.name);
// [ 'Crosby', 'Stills', 'Nash', 'Young' ]
```

Set `splitAnd: "always"` or `"never"` to change that.

## Features

`feat.`, `ft.` and `featuring` work on either side of the dash and in brackets. `with` and `w/`
only count at the start of a bracket, since "with" turns up in plenty of titles. All of these give
the second name the role `featured`:

```js
parse("Noisia feat. Foreign Beggars - Shellshock");     // source 'artist'
parse("Noisia - Shellshock ft. Foreign Beggars");       // source 'title'
parse("Silk City - Electricity (with Dua Lipa)");       // source 'title'
```

If the same person is credited twice, they appear once:

```js
parse("Noisia feat. Foreign Beggars - Shellshock (feat. Foreign Beggars)").artists.map((a) => a.name);
// [ 'Noisia', 'Foreign Beggars' ]
```

Names are compared with full Unicode case folding and accents removed, so `Sigur Rós` and
`SIGUR ROS` count as the same artist.

## Producers

```js
const t = parse("Future - Mask Off (prod. by Metro Boomin)");
t.artists[1];  // { name: 'Metro Boomin', role: 'producer', joiner: 'prod. by', source: 'title' }
t.title;       // 'Mask Off'
```

## knownArtists

trackparse has no artist database, and that's deliberate. A built-in list would be wrong for half
its users and impossible to keep current. Instead you tell it the names you care about:

```js
parse("Chase & Status - Baddadan").artists.map((a) => a.name);
// [ 'Chase', 'Status' ]

parse("Chase & Status - Baddadan", { knownArtists: ["Chase & Status"] }).artists.map((a) => a.name);
// [ 'Chase & Status' ]
```

A known name is matched case- and accent-insensitively wherever a credit list starts. It also
protects names that begin with digits from being read as a track number (`3 Doors Down`). If you
parse a lot of strings with the same names, put them in [`createParser`](/trackparse/reference/functions/#createparseroptions).

## Unknown artists

`ID` (and `Unknown`, `Untitled`, `?`, `???`) means the track hasn't been announced yet. It doesn't become a name.
The track gets `flags.unknownArtist: true` instead, and the same goes for titles:

```js
const t = parse("ID - ID (ID Remix)");
t.artists;                    // []
t.flags.unknownArtist;        // true
t.flags.unknownTitle;         // true
t.versions[0].unknownArtist;  // true
```
