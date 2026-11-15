---
title: Ports
description: The JavaScript, Python, Rust, Go and C implementations, and what it takes to add another language.
---

| Language | Package | Status |
|---|---|---|
| JavaScript / TypeScript | [`trackparse`](https://www.npmjs.com/package/trackparse) on npm | Reference implementation. 13,130 / 13,130 fixtures. Tracklists in `trackparse/tracklist` |
| Python | [`trackparse`](https://pypi.org/project/trackparse/) on PyPI | 13,130 / 13,130 fixtures, output identical to JS. Tracklists in `trackparse.tracklist` |
| Rust | [`trackparse`](https://crates.io/crates/trackparse) on crates.io, API docs on [docs.rs](https://docs.rs/trackparse) | 13,130 / 13,130 fixtures, output identical to JS. Tracklists in `trackparse::tracklist` |
| Go | [`github.com/4matic/trackparse/go`](https://pkg.go.dev/github.com/4matic/trackparse/go), API docs on [pkg.go.dev](https://pkg.go.dev/github.com/4matic/trackparse/go) | 13,130 / 13,130 fixtures, output identical to JS. Tracklists in the same package (`trackparse.ParseTracklist`) |
| C | Source tarball and amalgamation on each [GitHub Release](https://github.com/4matic/trackparse/releases) from 0.7.0; the API is [`trackparse.h`](https://github.com/4matic/trackparse/blob/main/c/include/trackparse.h) | 13,130 / 13,130 fixtures, output identical to JS. Tracklists in the same library (`trackparse_parse_tracklist`) |

| | JavaScript | Python | Rust | Go | C |
|---|---|---|---|---|---|
| Install | `npm install trackparse` | `pip install trackparse` | `cargo add trackparse` | `go get github.com/4matic/trackparse/go@latest` | CMake `FetchContent` from the release tarball, or copy the amalgamation |
| Needs | Node 18 or newer, or any ES2020 runtime | Python 3.9 or newer | Rust 1.74 or newer | Go 1.22 or newer | a C11 compiler (GCC, Clang or MSVC); CMake 3.16 or newer for the CMake build |
| Dependencies | none | none | `unicode-normalization`; `serde` is optional | none | none beyond the C standard library and the system's threads |
| Types | TypeScript types included | `py.typed`, passes `mypy --strict` | structs and enums, no `unsafe` | structs and typed string constants, no `unsafe`, no cgo | read-only structs, C enums with `_name()` functions, opaque option handles |
| Names | [`parseArtists`](/trackparse/reference/functions/#parseartistsinput-options), [`fullTitle`](/trackparse/reference/parse/#parsedtrack) | `parse_artists`, `full_title` | `parse_artists`, `full_title` | `ParseArtists`, `FullTitle` | `trackparse_parse_artists`, `full_title` |
| Options | one object | keyword arguments | `_with` functions and option builders | `With` functions and option structs | `_with` functions and option handles with setters |
| On bad input | never throws on a string | `TypeError` if the input isn't a `str` | never panics, no `Result` | never panics, no `error` | never crashes; `NULL` only when memory runs out |
| JSON | `JSON.stringify(track)` | `track.to_dict()` | `track.to_json()`, or `serde` | `json.Marshal(track)` | `trackparse_track_to_json(track)` |

[Getting started](/trackparse/start/getting-started/) has the install steps, and
[Other functions](/trackparse/reference/functions/) has the full table of names.

All of them share one version number. The spec, the JavaScript package, the Python package, the
Rust crate, the Go module and the C library are released together, from one release PR, and CI
fails if their versions drift apart. The crate has been on crates.io since 0.4.0. A Go release is
the tag `go/vX.Y.Z`, which the Go module proxy reads
([published versions](https://proxy.golang.org/github.com/4matic/trackparse/go/@v/list)). C has no
registry either: a C release is the tag `c-vX.Y.Z`, with the source tarball
(`trackparse-c-X.Y.Z.tar.gz`), the amalgamation (`trackparse-c-X.Y.Z-amalgamation.zip`) and
`SHA256SUMS` attached to its GitHub Release. 0.7.0 is the first. The
[changelogs](/trackparse/project/roadmap/#next) list what each release brought.

## Adding a language

A port is done when it passes every fixture and every invariant. There's no partial credit and no
port-specific behaviour. The Python, Rust and Go ports map one to one onto the JavaScript source files,
and the C port onto the Go files, so they work as second references. [How it works](/trackparse/project/how-it-works/) describes the
pipeline a port has to build.

The checklist, in short:

1. Generate source from `spec/data` and `spec/VERSION`, with a check mode for CI.
2. Implement the [R0](/trackparse/reference/spec/#r0-portability) primitives: the whitespace set, word keys, dedup keys, code-point iteration.
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
  junk kind in [`keywords`](/trackparse/reference/options/#keywords) through to the output. A port with enums needs a string form inside the
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
  dependency. This is also why the Go output has no Unicode difference with JavaScript on
  Node 24, whose Unicode 17 the tables come from. A Node release with a newer Unicode version can
  differ until the tables are regenerated with it.
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
- **Zero-value options.** The option structs mean the defaults when left empty. A field whose
  zero value is also a real setting must be a pointer, or that setting cannot be written: an
  audio length of 0 seconds (`AudioSeconds *float64`) and an empty feat marker
  (`FeatMarker *string`), both set with `trackparse.Ptr`. `ModeAuto` is `"auto"`, not the empty
  string, so `TracklistOptions{Mode: ModeAuto}` means auto while an empty `Mode` keeps the
  tracklist default. A test maps the options of every fixture to the typed structs and checks that
  the output equals the JSON door's.
- **NUL is real input.** A scanner that reads past the end of a string must not see a character.
  The Go helpers return -1 out of range, not 0, because U+0000 can appear in the input.

### What the C port taught

The C library is a file-by-file translation of the Go port, because Go has the closest shape:
explicit types, a code point lookup that returns -1 out of range, and no generics in the hot
paths. Every `c/src` file is a Go file with the same rule IDs in its comments, and one internal
header declared every type and prototype, in Go file order, before any pipeline code was written.
The JSON reader lives in a separate test library that is never installed, so the library writes
JSON but does not read it. The Go lessons above carried over (Unicode tables, final sigma,
JavaScript's `trim()`, WHATWG UTF-8, key order, stable sorts). What C added:

- **Memory without checks in the pipeline.** Each public call opens an arena with a `jmp_buf`.
  Inside the pipeline, allocation never returns `NULL`: when `malloc` fails, the allocator jumps
  back to the public function with `longjmp`, which frees the arena and returns `NULL`. So the
  pipeline has no `NULL` checks and no `free` calls, and each result is one block that its
  `_free` function releases. Running out of memory is the only way a call can fail.
- **Numbers without the C library.** `strtod` and `printf("%g")` follow the process locale, and no
  `printf` format writes JavaScript's shortest round-trip digits. `num.c` parses decimal strings to
  the correctly rounded double and writes numbers as JavaScript's `Number#toString` does, with
  exact big-integer arithmetic. `src/` has no `strtod`, no float `printf` and no `<ctype.h>`, so a
  German locale can't write `3167,5` and a Turkish one can't lowercase `I` to `ı`.
- **Fused multiply-add.** GCC in GNU mode contracts `floor(v * 10^d + 0.5)` into one fused
  instruction on ARM64, or on x86 with FMA enabled, and that rounds differently. CMake passes
  `-ffp-contract=off` (`/fp:precise` on MSVC), `num.c` sets the standard pragma where the compiler
  honours it, and the product goes through a `volatile`, so the amalgamation is safe under other
  flags too. On 32-bit x86, x87 registers compute with extra precision, so `num.c` refuses to
  compile unless `FLT_EVAL_METHOD` is 0 (`-msse2 -mfpmath=sse`).
- **Unicode tables from Node, as in Go.** The C library can't take letters, lowercasing or
  normalization from the C library or the platform. Its generator shares the derivation with the
  Go generator, so both ports carry the same tables: categories L and Lu, JavaScript's
  `toLowerCase` with the final-sigma rule, combining class ranks, compositions and
  decompositions.
- **One translation unit.** The installed library is built from the amalgamation, where every
  internal function and table is `static`, so only the `trackparse_*` API leaves the object file,
  even in a static library. Since all files share one scope there, the amalgamation script fails
  when two files define the same `static` name.
- **Unset is not zero.** Options are opaque handles with setters, so an option that was never set
  and one set to its zero value stay different (an audio length of 0 seconds, an empty feat
  marker) without the pointers Go needed, and a new option doesn't change the ABI.
- **Enums and names.** Roles, version types and junk kinds are C enums, but a keyword option can
  bring in a version type or junk kind that the enum doesn't have. So each version carries
  `type_name` and each junk item `kind_name`, the string that goes into the JSON, and the enum
  falls back to `TRACKPARSE_VERSION_VERSION` or `TRACKPARSE_JUNK_OTHER`, as the Rust crate does.
- **Small portability traps.** `qsort` is not stable, so there is a merge sort. `char` is signed
  on x86 and unsigned on ARM, so every byte the pipeline inspects is an `unsigned char`. Generated
  tables and non-ASCII literals are `\xNN` escapes, because MSVC reads source files in the system
  code page.

## See also

- [How it's tested](/trackparse/project/testing/) for the fixtures and the cross-port diff.
- [The spec](/trackparse/reference/spec/#r0-portability): rule [R0](/trackparse/reference/spec/#r0-portability) is the portability contract.
- [Contributing](/trackparse/project/contributing/) for the commands of each port.
