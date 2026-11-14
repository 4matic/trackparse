---
title: How it's tested
description: 13,130 shared fixtures, a generator that can't agree with the code by accident, property tests, and a byte-level diff between the ports.
---

The fixtures in [`spec/fixtures`](https://github.com/4matic/trackparse/tree/main/spec/fixtures) are
the source of truth. The [spec](/trackparse/reference/spec/) says what should happen, the fixtures
pin it down, and every port has to pass all of them. The JavaScript, Python, Rust and Go ports all do:
**13,130 of 13,130**, with empty skip lists, in CI on every push.

## Handwritten: 1,203 cases

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
| Unicode and normalization | 71 | NFC, zero-width characters, fullwidth brackets, Cyrillic, CJK, Turkish İ |
| `format`, `parseArtists`, unknown IDs | 94 | Round trips, canonical joiners, `ID - ID` |
| Tracklist mode | 67 | Real lines that single-line `parse()` gets wrong, real lines with no separator at all, Markdown around names, and names that keep their `*` and `_` |
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

Each port throws thousands of random strings at the parser to check the
[invariants](/trackparse/reference/spec/#invariants) from the spec. The JavaScript suite uses
[fast-check](https://github.com/dubzzz/fast-check) and the Python suite uses
[Hypothesis](https://hypothesis.works). The Rust and Go suites each have a small seeded generator
of their own, so neither needs an extra test dependency. The Go module also has fuzz targets for
`Parse`, `ParseArtists`, `Format`, `ParseTracklist`, `ApplyEdits`, `Track.UnmarshalJSON` and the
internal pipeline (`go test -fuzz`) that run the same checks on whatever the fuzzer finds, and a
test that calls the parser, the tracklist functions and the edits from 16 goroutines at once under
the race detector.

- it never throws or panics, on any input, in any mode;
- a 10,000-code-point input finishes inside a ceiling: 1 s in JavaScript and Python, 2 s in Rust
  and Go.
  It normally takes tens of milliseconds in JavaScript and Rust, a few milliseconds in Go and well
  under a second in Python;
  the ceiling is loose so that a busy CI machine doesn't
  fail the run but a blowup does;
- `normalize` is idempotent;
- whitespace, dash and zero-width noise don't change the result;
- every output validates against the JSON Schema (JavaScript, Python and Go; the Rust output is
  compared with JavaScript byte for byte instead, see below);
- no word of the input goes missing from the output (the Rust and Go suites);
- `parse(format(parse(s)))` round-trips.

For tracklists, on random multi-line text and random edit lists:

- `raw + eol` over all entries gives back the input, and `lead + time + gap + rest` gives `raw`;
- `applyEdits(t, [])` returns `t`, and an edit never touches a line it doesn't name;
- shifting forward and back restores the text, unless something was clamped or upgraded;
- 10,000 lines parse inside a ceiling each suite sets for itself: 3 s in JavaScript (it takes
  about 0.8 s), 15 s in Python, 3 s in an optimized Rust build, 3 s in Go (it takes about 0.35 s);
- Markdown emphasis around a time or a track line gives the same kind, time and track as the plain
  line.

## Port against port

A fixture only checks the fields it asserts. So
[`compare-ports.mjs`](https://github.com/4matic/trackparse/blob/main/scripts/compare-ports.mjs) runs
every fixture input through all four ports, JavaScript, Python, Rust and Go, and diffs the complete
JSON output, tracklists and edits included. The difference count is zero.

The Rust crate was also run against JavaScript on random input, which reaches places no fixture
does: about 500,000 random single-track cases and 290,000 random tracklists with edits, again with
zero differences.

## Real tracklists

The tracklist rules were written against a private corpus of real podcast tracklists, a little over
5,000 lines. It can't be published, so it stays out of the repository. Every surprising line in it
became a handwritten fixture instead, which is why the tracklist fixtures talk about "real lines".

## In CI

Every push runs one workflow per port, plus one for the spec:

| Workflow | What it runs |
|---|---|
| [`js.yml`](https://github.com/4matic/trackparse/blob/main/.github/workflows/js.yml) | The full suite on Node 22 and 24, and every fixture against the built package on Node 18 and 20 |
| [`python.yml`](https://github.com/4matic/trackparse/blob/main/.github/workflows/python.yml) | ruff, `mypy --strict` and pytest on Python 3.9 to 3.14, and every fixture against the installed wheel |
| [`rust.yml`](https://github.com/4matic/trackparse/blob/main/.github/workflows/rust.yml) | `cargo test --locked` on stable and on Rust 1.74. On stable only: rustfmt, clippy, the tests with `--all-features` and a publish dry run. Also the Rust output against the built JavaScript package on every fixture |
| [`go.yml`](https://github.com/4matic/trackparse/blob/main/.github/workflows/go.yml) | `go vet` and `go test -race` on Go 1.22 and on stable, with the artist-side test data written from the built JavaScript package. On stable only: gofmt, staticcheck and the full Unicode check against Node. Also checks that the generated data and Unicode tables are current, that `go.mod` has no dependencies and that no unstable `sort.Slice` is used, and compares the Go output with the built JavaScript package on every fixture. A separate job builds and tests `go/` on its own, as the module zip holds it. A weekly scheduled run fuzzes every fuzz target for three to five minutes |
| [`spec.yml`](https://github.com/4matic/trackparse/blob/main/.github/workflows/spec.yml) | Fixture validation, stale generated files, the generator's own tests, and that the version numbers of the spec and the four packages match |

## Run it yourself

```sh
# JavaScript (from the repository root; Node 22 or newer, pnpm)
pnpm install
pnpm test          # fixtures, properties, generator tests
pnpm conformance   # pass rate per fixture file and per spec rule

# Python (uv)
cd python
uv run pytest                          # the same fixtures and invariants
uv run python scripts/conformance.py   # pass rate per fixture file and per spec rule

# Rust (1.74 or newer)
cd ../rust
cargo test                                                      # the same fixtures and invariants
TRACKPARSE_REPORT=1 cargo test --test fixtures -- --nocapture   # pass rate per file and per rule

# Go (1.22 or newer)
cd ../go
go test ./...                                        # the same fixtures, invariants and unit tests
TRACKPARSE_REPORT=1 go test -run TestFixtures -v .   # pass rate per file and per rule

# All four against each other (from the root)
cd ..
pnpm build
node scripts/compare-ports.mjs   # must print "differences: 0"
```

## See also

- [Ports](/trackparse/project/ports/) for what each port had to get right.
- [Contributing](/trackparse/project/contributing/) for how a rule change goes through the spec,
  the fixtures and the four ports.
- [How it works](/trackparse/project/how-it-works/) for the pipeline the fixtures exercise.
