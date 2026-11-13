---
title: Contributing
description: How to report a bug, propose a rule, and get a change merged.
---

## Report a bug

The most useful bug report is one line: the input string, and the output you expected. That's
usually enough to turn it into a fixture. If it came from a specific place (a YouTube title, a
Beatport export, a file name), say so, because that decides which [mode](/trackparse/guides/modes/)
it belongs to.

Open an issue on [GitHub](https://github.com/4matic/trackparse/issues). You can paste a
[playground](/trackparse/playground/) link: the URL contains the input. It doesn't matter which
language you use. The four packages share the rules, so a wrong result in one is a wrong result in
all of them. If JavaScript, Python, Rust and Go ever disagree with each other, that is a bug of its own,
and worth reporting with the two outputs.

## Change a rule

Behaviour starts in the spec, not in the code:

1. Add or amend a rule in [`spec/SPEC.md`](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md).
   Rule IDs are never renumbered or reused.
2. Add handwritten fixtures that cite the rule: the positive case, the near-miss that must not
   trigger, and one exact case if the rule touches many fields.
3. If the rule is combinatorial (a new joiner, a new version word), teach the generator the atom
   and regenerate.
4. Change every port, JavaScript, Python, Rust and Go, and cite the rule ID in a comment.
5. Check that every fixture passes in all four ports and that the cross-port diff reports zero
   differences.
6. Update the pages of this site that describe the rule, with an example in each language.

[How it works](/trackparse/project/how-it-works/) explains the pipeline, and
[How it's tested](/trackparse/project/testing/) explains the fixtures and the generator.

Word lists live only in `spec/data`. They're compiled into each port, so a new keyword is a JSON
change plus codegen, not a code change.

## The repository

```text
spec/        SPEC.md, word lists, JSON Schemas, fixtures, the fixture generator
js/          the npm package (reference implementation)
python/      the PyPI package
rust/        the crates.io package
go/          the Go module
site/        this website
scripts/     cross-port diff and other tools
```

## Working on the JavaScript package

From the repository root. Development needs Node 22 or newer and pnpm.

```sh
pnpm install
pnpm test                               # fixtures, properties, API tests, generator tests
pnpm conformance                        # pass rate per fixture file and per rule
pnpm lint                               # Biome lint and format check
pnpm --filter trackparse typecheck
pnpm gen:data                           # regenerate js/src/data.generated.ts from spec/data
pnpm gen:fixtures                       # regenerate spec/fixtures/generated
pnpm build                              # build js/dist
```

## Working on the Python package

Python 3.9 or newer, with [uv](https://docs.astral.sh/uv/).

```sh
cd python
uv sync                                  # install the dev environment
uv run pytest                            # fixtures, properties, API tests
uv run python scripts/conformance.py     # pass rate per fixture file and per rule
uv run ruff check . && uv run ruff format --check .
uv run mypy --strict src
uv run python scripts/gen_data.py        # regenerate src/trackparse/_data.py from spec/data
```

## Working on the Rust crate

Rust 1.74 or newer, and Node 24 for the codegen: the Unicode tables are generated from the Unicode
data built into Node, so another Node version can write different tables.

```sh
cd rust
cargo test                                                         # fixtures, invariants, API tests
TRACKPARSE_REPORT=1 cargo test --test fixtures -- --nocapture      # pass rate per file and per rule
cargo fmt --check
cargo clippy --all-targets --all-features -- -D warnings
node scripts/gen_data.mjs                                          # regenerate src/data.rs and src/unicode_tables.rs
```

## Working on the Go module

Go 1.22 or newer, and Node 24 for the codegen: as in Rust, the Unicode tables come from Node, not
from the Go toolchain.

```sh
cd go
go test ./...                                         # fixtures, invariants, unit tests
TRACKPARSE_REPORT=1 go test -run TestFixtures -v .    # pass rate per file and per rule
gofmt -l . && go vet ./...
node scripts/gen_data.mjs                             # regenerate data_gen.go from spec/data
node scripts/gen_unicode.mjs                          # regenerate unicode_gen.go
```

## Comparing the ports

After a change in any port, build the JavaScript package and run the cross-port diff from the
repository root. It runs every fixture through all four ports and compares the complete output.

```sh
pnpm build
node scripts/compare-ports.mjs                # must print "differences: 0"
node scripts/compare-ports.mjs --only=rust    # one port against JavaScript (or --only=python, --only=go)
```

## This website

The site lives in `site/`. Every code example has a JavaScript, a Python, a Rust and a Go tab, and every
output in it comes from running the code.

```sh
pnpm docs          # run the site locally
pnpm docs:build    # build it; fails on broken pages
```

[AGENTS.md](https://github.com/4matic/trackparse/blob/main/AGENTS.md) has the full repository map,
every command and the release process. Commits follow
[Conventional Commits](https://www.conventionalcommits.org/), because the changelog is generated
from them. The four packages and the spec are released together under one version number.

## License

[MIT](https://github.com/4matic/trackparse/blob/main/LICENSE) © Maksim Maksimov.
