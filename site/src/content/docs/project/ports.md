---
title: Ports
description: The JavaScript, Python and Rust implementations, and what it takes to add another language.
---

| Language | Package | Status |
|---|---|---|
| JavaScript / TypeScript | [`trackparse`](https://www.npmjs.com/package/trackparse) on npm | Reference implementation. 13,130 / 13,130 fixtures. Tracklists in `trackparse/tracklist` |
| Python | [`trackparse`](https://pypi.org/project/trackparse/) on PyPI | 13,130 / 13,130 fixtures, output identical to JS. Tracklists in `trackparse.tracklist` |
| Rust | [`trackparse`](https://crates.io/crates/trackparse) on crates.io | 13,130 / 13,130 fixtures, output identical to JS. Tracklists in `trackparse::tracklist` |

All three share one version number. The spec, the JavaScript package, the Python package and the
Rust crate are released together, from one release PR, and CI fails if their versions drift apart.
Each release tags the crate too (`rust-vX.Y.Z`). The crate has been on crates.io since 0.4.0.

## Adding a language

A port is done when it passes every fixture and every invariant. There's no partial credit and no
port-specific behaviour. The Python and Rust ports map one to one onto the JavaScript source files,
which makes them good second references.

The checklist, in short:

1. Generate source from `spec/data` and `spec/VERSION`, with a check mode for CI.
2. Implement the R0 primitives: the whitespace set, word keys, dedup keys, code-point iteration.
3. Build the pipeline in the order of `js/src/parser.ts`, then tracklists as a separate module.
4. Write the fixture matcher (about 40 lines) and a runner over both fixture folders, with schema
   validation and a skip list that can only shrink.
5. Add property tests for the spec invariants.
6. Expose an API that's idiomatic for the language but returns the same JSON.
7. Extend the cross-port diff so every fixture's complete output is compared with JavaScript.

The full version, including the traps the Python port fell into (UTF-16 positions in a few
internal places, JavaScript's `trim()`, object key order, banker's rounding), is in
[AGENTS.md](https://github.com/4matic/trackparse/blob/main/AGENTS.md#porting-guide-rust-others).

### What the Rust port taught

Passing the fixtures was not enough here either. The differences below only showed up in the
cross-port diff and in random differential runs against JavaScript:

- **Letters and uppercase letters need their own tables.** The spec means general category L and
  Lu. Rust's `char::is_alphabetic` and `char::is_uppercase` test wider properties (they also accept
  letter numbers and some marks), so the crate generates L and Lu range tables instead.
- **Lowercasing follows the compiler.** `str::to_lowercase` uses the Unicode version of the Rust
  toolchain that built the crate, which need not be the one in Node or CPython. The crate generates
  the lowercase mapping from the same source as its letter tables.
- **Normalisation follows the crate.** `unicode-normalization` ships Unicode 18 while Node 24 has
  Unicode 17, so 32 new combining marks are ordered differently in NFC.
- **Keyword values are arbitrary strings.** JavaScript passes any string given as a version type or
  junk kind in `keywords` through to the output. A port with enums needs a string form inside the
  pipeline to produce the same JSON.
- **JavaScript's number formatting is part of the output.** Edit warnings echo the `line` of an
  edit, so a large or fractional number has to be written the way JavaScript's number-to-string does
  (`1e+21`, not twenty-two digits).
