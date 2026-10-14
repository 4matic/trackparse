---
title: Ports
description: The JavaScript, Python and upcoming Rust implementations, and what it takes to add another language.
---

| Language | Package | Status |
|---|---|---|
| JavaScript / TypeScript | [`trackparse`](https://www.npmjs.com/package/trackparse) on npm | Reference implementation. 12,965 / 12,965 fixtures. Tracklists in `trackparse/tracklist` |
| Python | [`trackparse`](https://pypi.org/project/trackparse/) on PyPI | 12,965 / 12,965 fixtures, output identical to JS. Tracklists in `trackparse.tracklist` |
| Rust | `trackparse` on crates.io | Planned, same fixtures |

All three share one version number. The spec, the JavaScript package and the Python package are
released together, and CI fails if their versions drift apart.

## Adding a language

A port is done when it passes every fixture and every invariant. There's no partial credit and no
port-specific behaviour. The Python port maps one to one onto the JavaScript source files, which
makes it a good second reference.

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
