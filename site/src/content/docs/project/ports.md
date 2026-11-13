---
title: Ports
description: The JavaScript, Python, Rust and Go implementations, and what it takes to add another language.
---

| Language | Package | Status |
|---|---|---|
| JavaScript / TypeScript | [`trackparse`](https://www.npmjs.com/package/trackparse) on npm | Reference implementation. 13,130 / 13,130 fixtures. Tracklists in `trackparse/tracklist` |
| Python | [`trackparse`](https://pypi.org/project/trackparse/) on PyPI | 13,130 / 13,130 fixtures, output identical to JS. Tracklists in `trackparse.tracklist` |
| Rust | [`trackparse`](https://crates.io/crates/trackparse) on crates.io, API docs on [docs.rs](https://docs.rs/trackparse) | 13,130 / 13,130 fixtures, output identical to JS. Tracklists in `trackparse::tracklist` |
| Go | `github.com/4matic/trackparse/go`, from the next release (tag `go/vX.Y.Z`); API docs will be on [pkg.go.dev](https://pkg.go.dev/github.com/4matic/trackparse/go) | 13,130 / 13,130 fixtures, output identical to JS. Tracklists in the same package (`trackparse.ParseTracklist`) |

| | JavaScript | Python | Rust | Go |
|---|---|---|---|---|
| Install | `npm install trackparse` | `pip install trackparse` | `cargo add trackparse` | `go get github.com/4matic/trackparse/go@latest` |
| Needs | Node 18 or newer, or any ES2020 runtime | Python 3.9 or newer | Rust 1.74 or newer | Go 1.22 or newer |
| Dependencies | none | none | `unicode-normalization`; `serde` is optional | none |
| Types | TypeScript types included | `py.typed`, passes `mypy --strict` | structs and typed string constants, no `unsafe`, no cgo |
| Names | `parseArtists`, `fullTitle` | `parse_artists`, `full_title` | `parse_artists`, `full_title` | `ParseArtists`, `FullTitle` |
| Options | one object | keyword arguments | `_with` functions and option builders | `With` functions and option structs |
| On bad input | never throws on a string | `TypeError` if the input isn't a `str` | never panics, no `Result` | never panics, no `error` |
| JSON | `JSON.stringify(track)` | `track.to_dict()` | `track.to_json()`, or `serde` | `json.Marshal(track)` |

[Getting started](/trackparse/start/getting-started/) has the install steps, and
[Other functions](/trackparse/reference/functions/) has the full table of names.

All of them share one version number. The spec, the JavaScript package, the Python package and the
Rust crate are released together, from one release PR, and CI fails if their versions drift apart.
The crate has been on crates.io since 0.4.0. The Go module joins the same release PR and is
published with the next release, as the tag `go/vX.Y.Z` that the Go module proxy reads.

## Adding a language

A port is done when it passes every fixture and every invariant. There's no partial credit and no
port-specific behaviour. The Python, Rust and Go ports map one to one onto the JavaScript source files,
so they work as second references. [How it works](/trackparse/project/how-it-works/) describes the
pipeline a port has to build.

The checklist, in short:

1. Generate source from `spec/data` and `spec/VERSION`, with a check mode for CI.
2. Implement the R0 primitives: the whitespace set, word keys, dedup keys, code-point iteration.
3. Build the pipeline in the order of `js/src/parser.ts`, then tracklists as a separate module.
4. Write the fixture matcher (about 40 lines) and a runner over both fixture folders, with schema
   validation and a skip list that can only shrink.
5. Add property tests for the spec invariants.
6. Expose an API that's idiomatic for the language but returns the same JSON.
7. Extend the cross-port diff so every fixture's complete output is compared with JavaScript.

The full version is in
[AGENTS.md](https://github.com/4matic/trackparse/blob/main/AGENTS.md#porting-guide-rust-go-others).

### What the Python port taught

The fixtures passed before the output was identical. The
[cross-port diff](/trackparse/project/testing/#port-against-port) found the rest:

- **UTF-16 positions.** A few internal positions are counted in UTF-16 units in JavaScript. A port
  has to count astral characters as two there, or ties are ordered differently on emoji.
- **JavaScript's `trim()`.** Keyword options are trimmed with JavaScript's whitespace set, which is
  not the spec's set (U+FEFF and U+0085 differ).
- **Object key order.** In a JavaScript object, integer-like keys come first, in ascending order.
  A plain dict keeps insertion order, so keyword maps need the same reordering.
- **Rounding.** Rewritten times round half up. Python's `round()` rounds half to even.

### What the Rust port taught

Passing the fixtures was not enough here either. The differences below only showed up in the
cross-port diff and in random differential runs against JavaScript:

- **Letters and uppercase letters need their own tables.** The spec means general category L and
  Lu. Rust's `char::is_alphabetic` and `char::is_uppercase` test wider properties (they also accept
  letter numbers and some marks), so the crate generates L and Lu range tables instead.
- **Lowercasing follows the compiler.** `str::to_lowercase` uses the Unicode version of the Rust
  toolchain that built the crate, which need not be the one in Node or CPython. The crate generates
  the lowercase mapping from the same source as its letter tables.
- **Normalization follows the crate.** `unicode-normalization` ships Unicode 18 while Node 24 has
  Unicode 17, so 32 new combining marks are ordered differently in NFC.
- **Keyword values are arbitrary strings.** JavaScript passes any string given as a version type or
  junk kind in `keywords` through to the output. A port with enums needs a string form inside the
  pipeline to produce the same JSON.
- **JavaScript's number formatting is part of the output.** Edit warnings echo the `line` of an
  edit, so a large or fractional number has to be written the way JavaScript's number-to-string does
  (`1e+21`, not twenty-two digits).

### What the Go port taught

The Go port started from the lists above and from the Rust crate, which had already settled most
typing questions. What was new:

- **Unicode comes from Node, not from the Go toolchain.** Go's `unicode` package and
  `golang.org/x/text` follow the Unicode version of the toolchain that builds the program, and
  x/text even picks its normalization tables by Go version. Two users of the same trackparse
  version would get different output. The module generates its letter, uppercase, lowercase and
  NFC/NFD tables from Node instead, so its Unicode version is the reference engine's and it has no
  dependency. This is also why the Go output has no Unicode difference with JavaScript.
- **Typed strings instead of enums.** Roles, version types, junk kinds and warning codes are typed
  strings (`type Role string`, `trackparse.RolePrimary`). A keyword option can bring in any version
  type or junk kind, and a typed string carries it to the output unchanged, so the Go port needs no
  separate string form inside the pipeline.
- **Ordered slices for keyword maps.** Go maps have no insertion order to reproduce JavaScript's
  `Object.entries` order. `Keywords.VersionHeads` and `Keywords.Junk` are slices: a repeated key
  keeps its first place and takes the last value, and integer-like keys come first in ascending
  order, as in JavaScript.
- **Stable sorts.** JavaScript's `sort` is stable and the tracklist code relies on that for ties.
  Go's `sort.Slice` is not, so the module uses `slices.SortStableFunc`, and CI rejects
  `sort.Slice(`.
- **Invalid UTF-8.** A Go string can hold any bytes. The module replaces each maximal ill-formed
  subpart with one U+FFFD before parsing, the WHATWG rule that Rust's `from_utf8_lossy` and
  `TextDecoder` follow, so the Go and Rust ports read the same bytes as the same text. Go's own
  decoding would give one U+FFFD per bad byte.
- **Zero-value options.** The option structs mean the defaults when left empty, which covers
  almost every case. Three cannot be written that way: a tracklist parsed in auto mode (the zero
  `Mode` there means tracklist, so write `Mode("auto")`), an audio length of exactly 0 seconds (zero
  means unknown), and an option set to the empty string, which in Go is the zero value and means the
  default.
- **NUL is real input.** A scanner that reads past the end of a string must not see a character.
  The Go helpers return -1 out of range, not 0, because U+0000 can appear in the input.

## See also

- [How it's tested](/trackparse/project/testing/) for the fixtures and the cross-port diff.
- [The spec](/trackparse/reference/spec/#r0-portability): rule R0 is the portability contract.
- [Contributing](/trackparse/project/contributing/) for the commands of each port.
