---
title: How it's tested
description: 13,124 shared fixtures, a generator that can't agree with the code by accident, property tests, and a byte-level diff between the ports.
---

The fixtures in [`spec/fixtures`](https://github.com/4matic/trackparse/tree/main/spec/fixtures) are
the source of truth. The spec says what should happen, the fixtures pin it down, and every port has
to pass all of them. All three ports do: **13,124 of 13,124**, with empty skip lists, in CI on every
push.

## Handwritten: 1,197 cases

Each case cites the spec rules it exercises, and 62 of them spell out the complete output, field by
field, so nothing drifts unnoticed.

| Group | Cases | What's in there |
|---|---:|---|
| Regressions | 273 | Every test from two real-world parsers this library replaces, and the known failures of get-artist-title and friends |
| Versions and brackets | 90 | Every version keyword, generic heads (`Radio Edit` → radio), multi-version groups, feat inside brackets |
| Artists | 76 | Joiners, guards (`AC/DC`, `Mumford & Sons`, `Malcolm X`), `knownArtists`, dedup |
| Title credits and producers | 57 | `Title ft. X`, `(prod. by X)`, self-produced artists |
| Separators and suffixes | 63 | Dash variants, `- Remastered 2015`, unspaced and asymmetric dashes |
| Prefixes, junk, flags | 111 | Track numbers vs `2 Unlimited`, cue times, every junk kind, years, explicit/clean |
| Modes | 65 | YouTube pipes, `"Title" by Artist`, uploader fallback, file names |
| Unicode and normalisation | 71 | NFC, zero-width characters, fullwidth brackets, Cyrillic, CJK, Turkish İ |
| `format`, `parseArtists`, unknown IDs | 94 | Round trips, canonical joiners, `ID - ID` |
| Tracklist mode | 61 | Real lines that single-line `parse()` gets wrong, real lines with no separator at all, Markdown around names, and names that keep their `*` and `_` |
| Tracklist lines and times | 104 | LF/CRLF/CR, trailing spaces, line kinds, every time form, brackets, gap separators, invalid times, list bullets, Markdown around times, `w/` lines |
| Tracklist positions | 15 | A real 81-line list numbered without dots, and the `2 Unlimited` guard |
| Tracklist warnings | 38 | Each code with its near-miss, and real out-of-order lists with their suggestions |
| Tracklist edits | 79 | Style keeping, shift, clamp, sort, insert/delete line endings, invalid edits, locality |

## Generated: 11,927 cases

A deterministic generator builds inputs from small "atoms": an artist, a joiner, a feat placement,
a version group, a junk suffix. Each atom carries its own expected contribution, and the expected
output is assembled from those atoms. It never comes from running a parser, so the generator can't
quietly agree with the code it's testing. Combinations whose answer would depend on a rule the atoms
don't model are rejected instead of guessed.

| Scenario | Cases | Scenario | Cases |
|---|---:|---|---:|
| artists × joiners | 1,811 | kitchen sink (pairwise + random) | 1,589 |
| versions | 1,650 | junk × YouTube | 806 |
| dash suffixes | 1,174 | file names | 780 |
| dedup | 1,008 | unicode | 674 |
| feat placement | 922 | prefixes | 577 |
| tracklists | 826 | tracklists with Markdown | 110 |

## Properties

[fast-check](https://github.com/dubzzz/fast-check) (JS) and [Hypothesis](https://hypothesis.works)
(Python) throw thousands of random strings at the parser to check the invariants from the spec.
The Rust suite checks the tracklist invariants with a small seeded generator of its own:

- it never throws, on any input, and a 10,000-character input stays linear;
- `normalize` is idempotent;
- whitespace, dash and zero-width noise don't change the result;
- every output validates against the JSON Schema;
- no word of the input goes missing from the output;
- `parse(format(parse(s)))` round-trips.

For tracklists, on random multi-line text and random edit lists:

- `raw + eol` over all entries gives back the input, and `lead + time + gap + rest` gives `raw`;
- `applyEdits(t, [])` returns `t`, and an edit never touches a line it doesn't name;
- shifting forward and back restores the text, unless something was clamped or upgraded;
- 10,000 lines parse in linear time (the JS suite allows 3 s; it takes about 0.6 s);
- Markdown emphasis around a time or a track line gives the same kind, time and track as the plain
  line.

## Port against port

A fixture only checks the fields it asserts. So
[`compare-ports.mjs`](https://github.com/4matic/trackparse/blob/main/scripts/compare-ports.mjs) runs
every fixture input through all three ports, JavaScript, Python and Rust, and diffs the complete
JSON output, tracklists and edits included. The difference count is zero.

The Rust crate was also run against JavaScript on random input, which reaches places no fixture
does: about 500,000 random single-track cases and 290,000 random tracklists with edits, again with
zero differences.

## Real tracklists

The tracklist rules were written against a private corpus of real podcast tracklists, a little over
5,000 lines. It can't be published, so it stays out of the repository. Every surprising line in it
became a handwritten fixture instead, which is why the tracklist fixtures talk about "real lines".

## Run it yourself

```sh
pnpm install
pnpm test          # fixtures, properties, generator tests
pnpm conformance   # pass rate per fixture file and per spec rule

cd python && uv run pytest   # the same fixtures and invariants in Python
cd rust && cargo test         # and in Rust

node scripts/compare-ports.mjs   # after pnpm build: all three ports, full outputs
```
