---
title: parse and ParsedTrack
description: The parse function and every field of the ParsedTrack it returns.
---

```ts
parse(input: string, options?: ParseOptions): ParsedTrack
```

`parse` never throws. Any string gives a `ParsedTrack`, and unknown or malformed options fall back
to their defaults. The options are on the [Options](/trackparse/reference/options/) page.

## ParsedTrack

Every key is always present.

| Field | Type | |
|---|---|---|
| `input` | `string` | The original string, untouched (not normalised) |
| `mode` | `"clean" \| "youtube" \| "filename" \| "tracklist"` | The mode that actually ran |
| `position` | `{ raw: string, number: number } \| null` | Track number. `raw` is the digits as written (`"01"`) |
| `timestamp` | `{ raw: string, seconds: number } \| null` | Leading cue time, `"1:02:03"` → 3723 |
| `artists` | `Artist[]` | Primary, featured and producer credits. Remixers are in `versions` |
| `title` | `string` | The clean title. Brackets it doesn't recognise stay in |
| `fullTitle` | `string` | `title` with the versions put back in their original brackets |
| `versions` | `Version[]` | Remixes and versions, left to right |
| `year` | `number \| null` | From `(2015)` or `- 2015` |
| `flags` | `{ explicit, clean, unknownArtist, unknownTitle }` | All booleans |
| `junk` | `{ raw: string, kind: JunkKind }[]` | What was taken out of the title, and what kind of noise it was |
| `warnings` | `Warning[]` | Which heuristics fired. See [Warnings](/trackparse/reference/warnings/) |

## Artist

```ts
{ name: string; role: ArtistRole; joiner: string | null; source: ArtistSource }
```

| Field | Values |
|---|---|
| `role` | `primary`, `featured`, `remixer`, `producer` |
| `joiner` | the text before the name as written (`"&"`, `","`, `"ft."`, `"prod. by"`), or `null` for the first name in a list |
| `source` | `artist` (left of the dash), `title` (right of it), `version` (inside a version) |

`&`, `x` and `vs.` are joiners, not roles. Everyone they join is `primary`.

## Version

```ts
{
  type: VersionType;
  raw: string;
  artists: Artist[];
  modifiers: VersionType[];
  descriptor: string | null;
  year: number | null;
  unknownArtist: boolean;
  delimiter: "(" | "[" | "{" | "-";
}
```

| Field | |
|---|---|
| `type` | one of `remix` `bootleg` `vip` `edit` `flip` `refix` `rework` `mashup` `blend` `dub` `mix` `extended` `radio` `club` `original` `instrumental` `acapella` `live` `acoustic` `remaster` `demo` `reprise` `cover` `version` `spedUp` `slowed` `nightcore` |
| `raw` | the text inside the brackets, as written |
| `artists` | remixers (role `remixer`, source `version`) |
| `modifiers` | type words that didn't become the type: `Extended VIP Mix` → `vip`, `["extended"]` |
| `descriptor` | leftover words: `"Taylor's"`, `"at Wembley"`, `"D'n'B"` |
| `year` | a year inside the version: `Remastered 2015` → 2015 |
| `unknownArtist` | `true` for `ID Remix` and the like |
| `delimiter` | the bracket it was in, or `-` for a dash suffix |

[Versions and remixes](/trackparse/guides/versions/) explains how the type is chosen.

## Junk kinds

`video`, `audio`, `lyrics`, `quality`, `promo`, `platform`, `label`, `genre`, `other`. Examples are
in [Junk, flags and prefixes](/trackparse/guides/junk-and-flags/#junk). The vocabulary is in
[`spec/data`](https://github.com/4matic/trackparse/tree/main/spec/data).

## Types

All of these are exported from `trackparse` as TypeScript types:

`ParsedTrack`, `Artist`, `ArtistRole`, `ArtistSource`, `Version`, `VersionType`,
`VersionDelimiter`, `Junk`, `JunkKind`, `Flags`, `Position`, `Timestamp`, `Warning`, `Mode`,
`ModeOption`, `ParseOptions`, `KeywordOptions`, `FormatOptions`.

The JSON shape is also published as a JSON Schema,
[`parsed-track.schema.json`](https://github.com/4matic/trackparse/blob/main/spec/schema/parsed-track.schema.json).
