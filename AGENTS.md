# AGENTS.md

Guide for AI agents and human contributors working on trackparse. `CLAUDE.md` is a symlink to this
file. Read it before changing behaviour.

trackparse is a spec-driven parser for music track strings. The spec and its fixtures define
behaviour; each language port implements them. The JS package is the reference port.

## Repo map

```
spec/
  SPEC.md                 the contract: rules R0–R13 with stable IDs, invariants, glossary
  VERSION                 spec version (MAJOR.MINOR.PATCH); ports share MAJOR.MINOR
  data/*.json             every word list (feat markers, joiners, version keywords, junk, genres...)
  schema/*.json           JSON Schemas: parsed-track, tracklist, options, fixture-file
  fixtures/README.md      fixture format and matching semantics
  fixtures/handwritten/   curated cases by topic (edit by hand)
  fixtures/generated/     combinatorial cases (NEVER edit by hand)
  generator/              @trackparse/fixture-gen: writes fixtures/generated
                          (src/tracklist.ts + scenarios/tracklist.ts, tracklist-emphasis.ts:
                          multi-line tracklist cases)
  proposals/tracklist.md  the platform proposal behind R13 (historical; SPEC.md wins)
  corpus/                 git-ignored: private real-world corpora (platform data, never commit)
js/                       npm package `trackparse`, reference implementation
  src/index.ts            public API exports
  src/tracklist/          the `trackparse/tracklist` subpath entry (R13):
    index.ts              exports parseTracklist, applyEdits and the tracklist types
    types.ts              public tracklist types (mirror tracklist.schema.json)
    options.ts            R13.10     split.ts     R13.1 lines + eol    times.ts  R13.2, R13.9.2
    emphasis.ts           R13.12 Markdown emphasis markers (pairing) and runs next to the time
    kinds.ts              R13.3, R13.4.2 candidate lines           positions.ts  R13.5
    warnings.ts           R13.7 + out-of-order suggestion search   parse.ts  R13.1–R13.8
    edits.ts              R13.9 applyEdits
  src/types.ts            public types (mirror the schemas)
  src/parser.ts           pipeline R1 → R2 → R4 → R3/R6 → R7 → R8 → R9, plus parseArtists (R10)
                          and the internal ListParser (probe + parse with list context) for R13
  src/normalize.ts        R1          src/mode.ts        R2
  src/scanner.ts          R3          src/prefix.ts      R4
  src/classify.ts         R5          src/separator.ts   R6
  src/artists.ts          R7          src/credits.ts     R7.3–R7.5 credit-list splitting
  src/title.ts            R8          src/assemble.ts    R9
  src/format.ts           R11, allArtists
  src/words.ts            R0 word keys, dedup keys, whitespace set
  src/digits.ts           ASCII digit runs shared by R4.1 and the R13.2 time scanner
  src/tables.ts           vocabulary tables built from data + options.keywords (R12)
  src/context.ts          per-call context: resolved options, tables, warnings
  src/data.generated.ts   GENERATED from spec/data + spec/VERSION (never edit)
  scripts/gen-data.ts     codegen for data.generated.ts
  scripts/conformance.ts  pass-rate report per file and per rule
  scripts/compat.mjs      every fixture against dist/ (ESM + CJS) with plain Node 18+
  scripts/size-check.mjs  size budget: the tracklist entry may add at most 9 KiB gzip
  test/runner.ts          loads and runs shared fixtures
  test/match.ts           partial/exact matcher (spec/fixtures/README.md)
  test/fixtures.test.ts   every fixture + schema validation + xfail/skip handling
  test/properties.test.ts SPEC invariants with fast-check
  test/api.test.ts        API surface
  test/match.test.ts      matcher unit tests
  test/tracklist.*.test.ts  tracklist API tests and R13.11 invariants (fast-check)
  test/skip.json          port-specific known failures (only ever shrinks)
python/                   PyPI package `trackparse` (Python 3.9+, zero deps, uv project)
  src/trackparse/         one module per js/src file, same rule IDs in comments:
    __init__.py           public API (`__all__`: parse, parse_artists, format_track, normalize,
                          all_artists, create_parser, Parser, SPEC_VERSION, __version__, types)
    types.py, options.py  frozen dataclasses + Literal aliases; option TypedDicts (snake_case)
    _parser.py            pipeline + Parser          _normalize.py  R1     _mode.py      R2
    _scanner.py           R3     _prefix.py    R4     _classify.py   R5     _separator.py R6
    _artists.py           R7     _credits.py   R7.3–R7.5             _title.py      R8
    _assemble.py          R9     _format.py    R11, all_artists      _words.py      R0
    _digits.py            digit runs (js/src/digits.ts)
    tracklist.py          public `trackparse.tracklist` (parse_tracklist, apply_edits, types)
    _tracklist_*.py       one per js/src/tracklist file: _types, _options, _split, _times,
                          _kinds, _positions, _warnings, _parse, _edits, _emphasis
    _tables.py            R12    _context.py   per-call context
    _data.py              GENERATED from spec/data + spec/VERSION (never edit)
  scripts/gen_data.py     codegen for _data.py (--check for CI)
  scripts/conformance.py  pass-rate report per file and per rule
  tests/                  runner.py, match.py, test_fixtures.py, test_properties.py (Hypothesis),
                          test_api.py, test_match.py, test_tracklist_api.py,
                          test_tracklist_properties.py, skip.json (only ever shrinks)
rust/                     crates.io package `trackparse` (Rust 1.74+, one dependency, no unsafe;
                          a release-please component, on crates.io since 0.4.0)
  src/lib.rs              public API: parse, parse_with, parse_artists(_with), format_track(_with),
                          normalize, all_artists, Parser, SPEC_VERSION; re-exports every type and
                          option at the crate root (`types` and `options` are private modules)
  src/types.rs            public structs and enums (mirror the schemas): to_json(), and
                          as_str() / from_name() / ALL / Display / FromStr on the enums
  src/options.rs          ParseOptions, FormatOptions, KeywordOptions (#[non_exhaustive], chained
                          setters from ::new()) + resolved forms
  src/*.rs                one module per js/src file, same rule IDs in comments: parser, normalize,
                          mode, scanner, prefix, classify, separator, artists, credits, title,
                          assemble, format, words, digits, tables, context
  src/tracklist/          public `trackparse::tracklist` (parse_tracklist(_with), apply_edits(_with)),
                          one module per js/src/tracklist file (mod.rs is index.ts)
  src/json.rs             hand-written JSON value, parser and writer with JS number formatting
  src/wire.rs             the JSON door (doc-hidden): every function with loose JSON arguments,
                          for the fixtures and the cross-port diff
  src/serde_support.rs    serde impls behind the `serde` feature
  src/data.rs             GENERATED from spec/data + spec/VERSION (never edit)
  src/unicode_tables.rs   GENERATED: categories L and Lu, the lowercase mapping (never edit)
  src/value.rs            the static value type data.rs is written in
  scripts/gen_data.mjs    codegen for data.rs and unicode_tables.rs (--check for CI)
  scripts/dump_primitives.mjs  expected R0 primitive values from JS, into tests/data/
  examples/dump.rs        Rust side of compare-ports (JSONL in, JSON out, through wire)
  tests/                  fixtures.rs (every shared fixture + the conformance report), api.rs,
                          tracklist_api.rs, tracklist_properties.rs (R13.11 invariants), serde.rs,
                          data/primitives.json (from dump_primitives.mjs), skip.json (only ever shrinks)
go/                       Go module `github.com/4matic/trackparse/go`, package `trackparse` (Go 1.22+,
                          no dependencies, no unsafe or cgo; a release-please component, tagged
                          go/vX.Y.Z, served by the Go module proxy)
  trackparse.go           public API: Parse, ParseWith, ParseArtists(With), Format(With), Normalize,
                          AllArtists, Parser/NewParser, ParseTracklist(With), ApplyEdits(With);
                          invalid UTF-8 is sanitized here (WHATWG maximal subparts to U+FFFD)
  doc.go                  package overview shown on pkg.go.dev
  types.go                public structs and typed-string enums (mirror the schemas); nullable
                          fields are pointers
  options.go              ParseOptions, FormatOptions, Keywords, TracklistOptions (zero value = the
                          defaults; FeatMarker and AudioSeconds are pointers), Ptr, the Edit
                          constructors, and the resolved forms
  json.go                 MarshalJSON / UnmarshalJSON with JSON.stringify's bytes, and the JSON reader
  *.go                    one file per js/src file, same rule IDs in comments: parser, normalize,
                          mode, scanner, prefix, classify, separator, artists, credits, title,
                          assemble, format, words, digits, tables, context
  tl_*.go                 one file per js/src/tracklist file: tl_split, tl_times, tl_emphasis,
                          tl_kinds, tl_positions, tl_warnings, tl_parse, tl_edits, tl_options
  unicode.go              L and Lu (R0.6), JS toLowerCase (R0.4), NFC/NFD (R1) over the generated tables
  wire.go                 the JSON door: every function with loose JSON arguments, reached from
                          internal/wire (fixtures, compare-ports, fuzzers) so it is not public API
  data_gen.go             GENERATED from spec/data + spec/VERSION (never edit); SpecVersion carries
                          the x-release-please-version marker
  unicode_gen.go          GENERATED from Node: L, Lu, the lowercase map, combining class ranks,
                          composition and decomposition tables (never edit)
  scripts/gen_data.mjs    codegen for data_gen.go (--check for CI)
  scripts/gen_unicode.mjs codegen for unicode_gen.go and testdata/unicode/ (--check for CI)
  scripts/dump_artist_cases.mjs  JS values for the artist-side unit tests, into testdata/
  internal/cmd/dump/      Go side of compare-ports (JSONL in, JSON out, through internal/wire)
  internal/wire/          the JSON door's package
  *_test.go               fixtures_test.go (every shared fixture + the conformance report; skipped
                          when ../spec is absent, as in the module zip), match_test.go, schema_test.go,
                          primitives_test.go (R0 values from rust/tests/data/primitives.json),
                          unicode_test.go, properties_test.go (SPEC invariants and R13.11),
                          example_test.go (the pkg.go.dev examples, checked by go test),
                          fuzz_test.go (fuzz targets seeded from the fixture inputs), unit tests per area
  testdata/               skip.json (only ever shrinks), unicode/ (cases dumped from Node),
                          artist_cases.json (from dump_artist_cases.mjs)
site/                     documentation site + playground (Astro + Starlight, GitHub Pages)
  astro.config.mjs        base /trackparse, sidebar
  src/content/docs/       the pages (Markdown/MDX, JS + Python + Rust + Go tabs on every example);
                          reference/spec.md is generated from spec/SPEC.md
  src/components/         HeroDemo, Playground (runs js/ in the browser), Feature, Stats
  src/lib/                trackview.ts (highlight + field rendering), icons.ts (Lucide SVG strings),
                          lang.ts (one language choice for docs tabs, install switch, playground)
  scripts/sync-spec.mjs   copies spec/SPEC.md into the docs at build time
scripts/compare-ports.mjs every fixture through js/dist, the Python port, the Rust crate and the Go
                          module, diffs the full outputs (parseTracklist and applyEdits included);
                          --only=python, --only=rust or --only=go for one port
scripts/fuzz-ports.mjs    random differential run: single tracks and tracklists with edits through
                          all four ports (--n, --lists, --seed, --only=go|rust|python, --out)
scripts/lib/ports.mjs     runners and the JSON diff shared by compare-ports and fuzz-ports
scripts/corpus-check.mjs  R13 against a private tracklist corpus (JSONL in spec/corpus/), via js/dist
scripts/dump_py.py        Python side of compare-ports (JSONL in, to_dict() JSON out)
.github/workflows/        js.yml (full suite on Node 22/24; compat: fixtures vs built dist on Node 18/20),
                          python.yml (lint, mypy, pytest on 3.9–3.14; build: fixtures vs installed wheel),
                          rust.yml (codegen drift, fmt, clippy, cargo test on stable and 1.74,
                          publish dry run; compare: Rust vs built JS on every fixture),
                          go.yml (codegen drift, gofmt, vet, staticcheck, the sort.Slice and
                          no-require greps, go test -race on stable and 1.22; compare: Go vs built JS),
                          spec.yml (fixture validation, generated/codegen drift, generator tests,
                          js/python/rust/go/spec version link), release.yml (release-please + npm,
                          PyPI and crates.io publish, Go tag checks through the module proxy),
                          pages.yml (build site/, deploy to GitHub Pages),
                          pr-title.yml (conventional PR titles)
.github/release-notes/    optional hand-written highlights prepended to a GitHub Release
release-please-config.json, .release-please-manifest.json   release automation (never hand-edit the manifest)
js/CHANGELOG.md, spec/CHANGELOG.md, python/CHANGELOG.md, rust/CHANGELOG.md, go/CHANGELOG.md
                          generated by release-please (go/CHANGELOG.md from the first Go release on)
```

## Golden rule

**Spec first, then fixtures, then ports.**

- Never change a port's behaviour without a fixture that pins the new behaviour. If a fix needs no
  fixture change, it is a bug fix; if an existing `expected` changes, it is a behaviour change.
- Never hand-edit `spec/fixtures/generated/**`, `js/src/data.generated.ts`,
  `python/src/trackparse/_data.py`, `rust/src/data.rs`, `rust/src/unicode_tables.rs`,
  `go/data_gen.go` or `go/unicode_gen.go`. Regenerate them.
- Word lists live **only** in `spec/data/`. Ports compile them into source with codegen; never copy
  a list into port code by hand, never hard-code a keyword in a scanner.
- The fixtures, not any one implementation, are the source of truth. When SPEC.md and a fixture
  disagree, fix both in the same change.
- Principle: **extract, don't delete.** Anything recognised goes into a field; anything not
  recognised stays in the title verbatim. A rule that silently drops input is wrong.
- **The website and the READMEs ship with the change.** A PR that changes behaviour, API, options,
  output fields, warnings, commands or published numbers updates the matching pages in
  `site/src/content/docs` and the README sections in the same PR. See
  [Documentation site and READMEs](#documentation-site-and-readmes).

## Workflow: add or change a rule

1. **Spec.** Edit `spec/SPEC.md`. Add or amend a rule with an ID (`R8.3`); never renumber or reuse
   existing IDs. If the rule needs words, add them to the right `spec/data/*.json` file.
2. **Handwritten fixtures.** Add cases to the topical file in `spec/fixtures/handwritten/` that
   cite the rule in `rules`. Cover the positive case, the near-miss that must *not* trigger, and
   one `match: "exact"` case if the rule touches many fields.
3. **Generator** (if the rule is combinatorial: a new joiner, version word, junk phrase, prefix
   form...). Add atoms to `spec/generator/src/atoms.ts` or the pools in
   `spec/generator/src/scenarios/pools.ts`, and teach `model.ts` the expected contribution. The
   expected output must come from the atoms, never from running a parser. If a combination becomes
   ambiguous, reject it in `model.ts` (`problem()`) instead of guessing. Then
   `pnpm gen:fixtures` and commit the output.
4. **Codegen.** If `spec/data` or `spec/VERSION` changed: `pnpm gen:data`, in `python/`
   `uv run python scripts/gen_data.py`, in `rust/` `node scripts/gen_data.mjs`, and in `go/`
   `node scripts/gen_data.mjs`.
5. **Implementation.** Change every port (JS, Python, Rust and Go). Cite rule IDs in comments.
6. **Conformance.** `pnpm test` and `pnpm conformance` must be green: 100% of fixtures, schema-valid
   output, property tests passing, `pnpm gen:check` clean. Same for Python (`uv run pytest`,
   `uv run python scripts/conformance.py`), Rust (`cargo test`, and the report with
   `TRACKPARSE_REPORT=1`) and Go (`go test ./...`, and the report with
   `TRACKPARSE_REPORT=1 go test -run TestFixtures -v`), then `node scripts/compare-ports.mjs` must
   report 0 differences.
7. **Docs.** Update the site pages and README sections that describe the rule (see
   [Documentation site and READMEs](#documentation-site-and-readmes)), with JavaScript, Python,
   Rust and Go examples whose outputs you ran, and `pnpm docs:build` green.
8. **Commit message.** Conventional commits; the message *is* the changelog entry (see
   [Versioning and release](#versioning-and-release)). Any change to an existing `expected` value is
   a behaviour change and must be a `feat` (or carry `!`), never a `fix`/`chore`. Don't touch
   versions or changelogs by hand.

## R0 portability constraints

Every port must produce byte-identical output. The full text is SPEC.md R0; in short:

- **No regex lookaround, backreferences or `\p{…}` classes.** Recognition is done by hand-written
  scanners over code points. Small anchored regexes only for fixed shapes (digits, timestamps).
  (The first exception, R0.6 "letter", is general category L: JS `/\p{L}/u`, Python
  `str.isalpha()`. Not `\p{Alphabetic}` and not Rust `char::is_alphabetic()`, which also accept Nl
  and combining marks. The second, R0.6 "uppercase letter" used only by R6.10, is category Lu: JS
  `/\p{Lu}/u`, Python `unicodedata.category(c) == "Lu"`. Go's `unicode.IsLetter` and `IsUpper`
  are category L and Lu, but of Go's Unicode version, so the Go port uses tables generated from
  Node instead.)
- **Explicit whitespace set** (R0.2): U+0009–U+000D, U+0020, U+0085, U+00A0, U+1680,
  U+2000–U+200A, U+2028, U+2029, U+202F, U+205F, U+3000. Never use a language's `\s` or `trim()`
  semantics without checking they match.
- **ASCII word keys** (R0.3): keyword comparison lowercases only A–Z and strips one trailing
  `. , ! ? : ;` (unless the vocabulary entry ends with it). Dedup keys (R0.4) are the only place
  with full Unicode lowercasing + NFD + mark stripping.
- **Iterate by code point** (R0.5), not UTF-16 unit or byte. No offsets in the output.
- *Digit* means ASCII `0`–`9` only.
- **Total** (R0.8): any string returns a ParsedTrack. Never throw or panic, including on bad
  options. No backtracking blowups: a 10,000-code-point input takes tens of milliseconds in JS and Rust
  and well under a second in Python (the suites enforce 1 s, Rust 2 s).

## Commands

Run from the repo root (pnpm workspace; Node ≥ 22 for development: pnpm 11 and vitest need it).

| Command | What it does |
|---|---|
| `pnpm install` | Install everything |
| `pnpm test` | `pnpm -r test`: JS fixtures + properties + API tests, generator tests |
| `pnpm build` | Build the npm package (`js/dist`) |
| `pnpm lint` | Biome lint + format check |
| `pnpm conformance` | JS pass rate per fixture file and per rule ID |
| `pnpm docs` · `pnpm docs:build` | Run the documentation site locally / build it into `site/dist` |
| `pnpm --filter trackparse compat` | After `build`: every fixture against `dist/` (ESM + CJS) with plain Node; runs on Node 18+ |
| `pnpm --filter trackparse size` | After `build`: fail if `trackparse/tracklist` adds more than 9 KiB gzip (ESM and CJS) |
| `pnpm gen:data` | Regenerate `js/src/data.generated.ts` from `spec/data` + `spec/VERSION` |
| `pnpm gen:fixtures` | Regenerate `spec/fixtures/generated/*.json` |
| `pnpm gen:check` | Fail if generated fixtures or JS codegen are stale |

Per package:

| Package | Command |
|---|---|
| `js/` (`trackparse`) | `pnpm --filter trackparse build` · `test` · `typecheck` · `lint` · `conformance` · `gen:data` · `gen:data:check` · `check:package` (publint + attw) · `size` |
| `spec/generator/` (`@trackparse/fixture-gen`) | `pnpm --filter @trackparse/fixture-gen gen` · `check` · `validate` (schema + unique ids for all fixtures) · `test` · `typecheck` |

Python (run inside `python/`; uv picks up `.python-version` and `uv.lock`):

| Command | What it does |
|---|---|
| `uv sync` | Install the dev environment (CI uses `uv sync --locked`) |
| `uv run python scripts/gen_data.py` | Regenerate `src/trackparse/_data.py`; `--check` fails if stale |
| `uv run ruff check .` · `uv run ruff format --check .` | Lint and format check |
| `uv run mypy --strict src` | Type check |
| `uv run pytest` | Every fixture, schema checks, Hypothesis properties, API tests |
| `uv run python scripts/conformance.py` | Pass rate per fixture file and per rule ID |
| `uv build` | Build the sdist and wheel into `dist/` |

Rust (run inside `rust/`; Rust 1.74 or newer, Node for the codegen):

| Command | What it does |
|---|---|
| `cargo test` | Every fixture, the R13.11 invariants, API tests, doc tests (CI also runs `--all-features`) |
| `TRACKPARSE_REPORT=1 cargo test --test fixtures -- --nocapture` | Pass rate per fixture file and per rule ID |
| `node scripts/gen_data.mjs` | Regenerate `src/data.rs` and `src/unicode_tables.rs`; `--check` fails if stale |
| `cargo fmt --check` · `cargo clippy --all-targets --all-features -- -D warnings` | Format check and lint (the panic lints are in `Cargo.toml`) |
| `cargo publish --dry-run` | The crate packages cleanly |

Go (run inside `go/`; Go 1.22 or newer, Node for the codegen):

| Command | What it does |
|---|---|
| `go test ./...` | Every fixture, the primitives, the invariants, the examples, API and unit tests (CI adds `-race`) |
| `TRACKPARSE_REPORT=1 go test -run TestFixtures -v` | Pass rate per fixture file and per rule ID |
| `node scripts/gen_data.mjs` · `node scripts/gen_unicode.mjs` | Regenerate `data_gen.go` / `unicode_gen.go` (and `testdata/unicode/`); `--check` fails if stale |
| `gofmt -l .` · `go vet ./...` · `go run honnef.co/go/tools/cmd/staticcheck@2026.2.2 ./...` | Format check (must print nothing) and lint |
| `go test -run '^$' -fuzz '^FuzzParse$' -fuzztime 60s` | Fuzz one target (`go test -list Fuzz` lists them: Parse, ParseArtists, Format, ParseTracklist, ApplyEdits, Run); a failing input lands in `testdata/fuzz/` |
| `TRACKPARSE_UNICODE_FULL=1 go test -run TestUnicodeFull` | The full NFC/NFD and lowercase sweep against Node (slow) |

Cross-port check, from the repo root (needs `uv`, `cargo` and `go`):

```sh
pnpm --filter trackparse build && node scripts/compare-ports.mjs   # four ports; must print "differences: 0"
node scripts/compare-ports.mjs --only=go                           # one port against JS (or python, rust)
```

Corpus check for R13 changes, when you have the private corpus (JSONL, one object per tracklist
with `title` and `track_list`, kept in the git-ignored `spec/corpus/`):

```sh
pnpm --filter trackparse build && node scripts/corpus-check.mjs spec/corpus/neuropunk-podcast.jsonl
```

It asserts losslessness (`raw` + `eol` and `lead + time + gap + rest`), the platform regex's seconds on every line, the 81 bare positions, the 10
out-of-order warnings and their 2 suggestions, and lists the lines still `line-unparsed` (2 today).
Never commit the corpus itself. Fixture cases taken from it carry a `source`
(`neuropunk podcast corpus: <list title>`).

Run a single fixture case (tests are named by case id):

```sh
pnpm --filter trackparse exec vitest run test/fixtures.test.ts -t "brackets-version-001"
cd python && uv run pytest tests/test_fixtures.py -k "brackets-version-001"
cd go && TRACKPARSE_ONLY=brackets-version-001 go test -run TestFixtures -v
```

Before pushing, mirror CI: `pnpm lint && pnpm --filter trackparse typecheck && pnpm test &&
pnpm build && pnpm --filter trackparse size && pnpm --filter trackparse check:package &&
pnpm --filter trackparse compat && pnpm gen:check && pnpm --filter @trackparse/fixture-gen validate`, and in `python/`
`uv run python scripts/gen_data.py --check && uv run ruff check . && uv run ruff format --check . &&
uv run mypy --strict src && uv run pytest && uv build`, and in `rust/`
`node scripts/gen_data.mjs --check && cargo fmt --check &&
cargo clippy --all-targets --all-features -- -D warnings && cargo test && cargo test --all-features`, and in `go/`
`node scripts/gen_data.mjs --check && node scripts/gen_unicode.mjs --check && test -z "$(gofmt -l .)" &&
go vet ./... && go run honnef.co/go/tools/cmd/staticcheck@2026.2.2 ./... && go test -race ./...`.

## Fixture authoring

Format and matcher: [`spec/fixtures/README.md`](spec/fixtures/README.md). Schema:
[`spec/schema/fixture-file.schema.json`](spec/schema/fixture-file.schema.json).

- **Partial matching is the default.** Assert what the case is about plus `artists` and `title`.
  Unlisted keys are ignored; arrays must match in length and order; strings compare exactly.
  An artist can be written as a bare string (name only) or an object (name, role, joiner, source).
- **Exact cases** (`"match": "exact"`) spell out the whole ParsedTrack. Keep a few per file to pin
  the schema; don't make every case exact.
- **Status:** `pass` (default); `xfail` for a known-wrong result out of scope for this release (add
  a `note`; the runner fails when an xfail case starts passing, so it gets promoted); `ambiguous`
  with `expectedAny` when several answers are acceptable.
- **IDs** are unique across the repo and never reused. Handwritten: `<file-stem>-NNN` (next free
  number, don't renumber). Generated: `gen:<scenario>:<atoms>`.
- Always fill `rules` with the SPEC rule IDs exercised. Add `source` when a case comes from
  somewhere (a bug report, another project's tests) and respect that project's license
  (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
- Defaults (`fn`, `options`) go in the file's `defaults`; options merge shallowly per case.
- `js/test/skip.json` (and `python/tests/skip.json`, `rust/tests/skip.json`,
  `go/testdata/skip.json`) is for port-specific gaps only, keyed by case id with a reason. It can only
  shrink: a skip-listed case that passes fails the run. Shared "known wrong" belongs in `xfail`.

## Documentation site and READMEs

The site at <https://4matic.github.io/trackparse/> (`site/`) is the user documentation; the READMEs
are short entry points that link into it. Both must describe the code as it is on `main`.

**Keep them in sync.** Whenever a change touches one of these, update the listed places in the same
PR:

| Change | Update |
|---|---|
| a rule's behaviour (R1–R12) | the guide for that area (`guides/what-it-understands`, `artists`, `versions`, `junk-and-flags`, `modes`, `formatting`), the root README tables |
| tracklists (R13) | `guides/tracklists`, `guides/editing-tracklists`, `reference/tracklist`, `reference/warnings` |
| a function, option, field or type | `reference/parse`, `reference/options`, `reference/functions` or `reference/tracklist`; the API sections of `js/README.md`, `python/README.md`, `rust/README.md` and `go/README.md`, and the Go doc comments and `go/example_test.go` |
| a warning code | `reference/warnings` |
| fixture counts, file counts, the size budget | `components/Stats.astro`, `index.mdx`, `start/introduction`, `project/testing`, `project/ports`, the root README "At a glance" and "How it's tested" |
| a port, a command, CI or the release flow | `project/ports`, `project/contributing`, `project/testing` ("Run it yourself") |
| `spec/SPEC.md` | nothing by hand: `reference/spec` is generated from it at build time |

**Every code example is in four languages.** There are no per-language pages. Each example on the
site is four synced tabs, in the order JavaScript, Python, Rust, Go:

````mdx
<Tabs syncKey="lang">
<TabItem label="JavaScript">

```js
parse("Noisia - Stigma (2015)").year;   // 2015
```

</TabItem>
<TabItem label="Python">

```python
parse("Noisia - Stigma (2015)").year   # 2015
```

</TabItem>
<TabItem label="Rust">

```rust
parse("Noisia - Stigma (2015)").year;   // Some(2015)
```

</TabItem>
<TabItem label="Go">

```go
fmt.Println(*trackparse.Parse("Noisia - Stigma (2015)").Year)   // 2015
```

</TabItem>
</Tabs>
````

- Keep the `syncKey` and the labels exactly as above; they make one choice apply site-wide.
  Pages with tabs are `.mdx` and import `Tabs, TabItem` from `@astrojs/starlight/components`.
- Write the Python version in Python terms: snake_case functions, options and attributes
  (`format_track`, `known_artists=`, `full_title`), tuples, `None`/`True`, and results as the
  dataclass `repr` (`Junk(raw='HD', kind='quality')`).
- Write the Rust version in Rust terms: `use trackparse::…;`, the `_with` functions with option
  builders (`parse_with(s, &ParseOptions::new().known_artists([…]))`), snake_case fields
  (`version_type`, `full_title`), `Option` and `Vec`, and results as their `Debug` output
  (`Junk { raw: "HD", kind: Quality }`, `Some(2015)`).
- Write the Go version in Go terms: the package name `trackparse` (import path
  `github.com/4matic/trackparse/go`), exported CamelCase functions and fields (`ParseWith`,
  `FormatWith`, `VersionType`, `FullTitle`), options as struct literals
  (`trackparse.ParseWith(s, trackparse.ParseOptions{KnownArtists: []string{…}})`), and nullable
  fields as pointers (`*t.Year`, after a `nil` check in real code). Go has no expression results,
  so every output is printed: the example is a `fmt` call and the output its comment. Use
  `fmt.Println` for numbers and plain values, `%q` for strings and string slices (`"Stigma"`,
  `["Chase" "Status"]`), `%+v` for structs and slices of structs
  (`[{Raw:HD Kind:quality}]`), and `json.Marshal` when the page is about the JSON form. Don't
  print a pointer field without `*`: `%+v` writes its address.
- Run every example and paste the real output; never write an output by hand. For Rust, a scratch
  cargo project that depends on the crate by path (`trackparse = { path = "…/rust" }`) is enough. For Go,
  a scratch module with `go mod edit -replace=github.com/4matic/trackparse/go=…/go` and
  `GOFLAGS=-mod=mod`. Language-specific
  notes (typing, `to_dict()`, `TypeError`) go on the page they concern, not on a separate page.
- The playground and the landing demo import `js/` directly, so they follow the code automatically;
  check them in a browser when the output shape changes. The playground's code panels generate
  JS, Python, Rust and Go (`code()` and `listCode()` in `Playground.astro`): when an option or an
  edit changes, update all four generators and compile a generated Rust and Go snippet.

**READMEs link to the site.** Each README section opens with a `**Docs:**` line that links the
matching site pages (`README.md`, `js/README.md`, `python/README.md`, `rust/README.md`,
`go/README.md`, `spec/fixtures/README.md`, `spec/generator/README.md`). Add one for a new section, and fix the links
when a page or heading moves. Use full `https://4matic.github.io/trackparse/...` URLs: npm, PyPI,
crates.io and pkg.go.dev render the package READMEs where relative links break.

**Moving or removing a page.** npm (`homepage`), PyPI (`[project.urls]`) and published READMEs point
at site URLs that can't be changed after a release. When a page moves or disappears, add a
`redirects` entry in `site/astro.config.mjs` so the old URL keeps working, then update the
links in the repo. The sidebar is in the same file.

**Design.** The visual rules (neutral greys, one orange accent, Geist type, hairlines, no decorative
icons or gradients, data colours only for parsed output) are written at the top of
`site/src/styles/custom.css`. Keep new pages and components to them.

**Check before pushing:** `pnpm docs:build` passes (it fails on broken MDX), and you looked at the
changed pages with `pnpm docs`. Site-only commits (`docs(site)`, `fix(site)`, `feat(site)`) don't
trigger a release; changes under `js/`, `python/` or `spec/` do, even `docs:` ones.

### What the docs may and may not claim

Each of these was written wrong at least once and caught in review. Check them when you touch docs.

| Don't write | Write instead, and why |
|---|---|
| "linear time" | Hand-written scanners, no regex backtracking. The R9.2 dedup in JS and Python is quadratic on thousands of repeated `feat.` credits (Rust and Go are near-linear). |
| "under 100 ms" for every port | True for JS (10 to 70 ms on 10,000 code points) and Rust (under 20 ms, release build). Python takes 40 to 170 ms. The suites enforce 1 s (JS, Python) and 2 s (Rust). Go: 5 to 8 ms (benchmarks in `go/bench_test.go`), ceiling 2 s. So "tens of milliseconds" holds for JS, Rust and Go, not Python. |
| "identical output for any input" | Identical on every fixture input. The Rust crate takes NFC from `unicode-normalization` (Unicode 18), Node 24 has Unicode 17, so 32 new combining marks can be ordered differently. Link `project/ports/#what-the-rust-port-taught`. |
| "zero dependencies" for the project | Zero in JS, Python and Go (the Go tables are generated from Node, not taken from `golang.org/x/text`). Rust has one, `unicode-normalization`. |
| a size in mixed units | Core: about 18 kB gzip. Tracklist entry: about 8.4 KiB (8,607 B), budget 9 KiB. Re-measure with `pnpm --filter trackparse build && pnpm --filter trackparse size`. |
| every suite checks "no word goes missing" | Only `rust/tests/properties.rs` and `go/properties_test.go` have that test today. |
| "Neuropunk" on the site | "a private corpus of real podcast tracklists". Fixture file names and `source` values keep the name; the site and READMEs don't. |

Numbers that appear in many files (grep all of them when one changes): fixture counts (13,130 =
1,203 handwritten + 11,927 generated), test counts (vitest 1,352, pytest 1,342), CI matrices
(Node 22/24 + 18/20, Python 3.9 to 3.14, Rust stable + 1.74, Go stable + 1.22), the 27 version types, the sizes
above. `spec/generator/README.md` links the site anchor `#generated-11927-cases`, which changes
with the count.

### Checking examples and links

- **Run every block, in every language.** A harness that extracts the fenced `js` / `python` /
  `rust` / `go` blocks per file, runs them and compares each `expr // output` line takes minutes to
  write and has caught every wrong output so far. JS runs against `js/dist` after a build, Python
  with `uv run python`, Rust in a scratch cargo project with a path dependency
  (`features = ["serde"]`, plus `serde_json`), Go in a scratch module with a `replace` to `go/`.
- **Rust blocks must compile as written, in page order.** A bare `value;` moves a non-`Copy`
  value, so a later line that uses it fails (E0382, E0507). Borrow instead: `&track.artists;`,
  `&list.warnings;`, `&parse(…).versions[0];`. Type listings (`pub struct …`) go in their own block,
  apart from runnable code.
- **`rust/README.md` is compiled.** `lib.rs` includes it as crate docs, so every Rust block in it is
  a doc test: run `cargo test --doc --all-features` after editing it.
- **Go blocks are statements, not a program.** Wrap them in `func main()` with the imports to
  run them, in page order; a variable declared with `:=` in one block is reassigned with `=` in a
  later one on the same page. The pkg.go.dev examples are `Example` functions in
  `go/example_test.go` with an `// Output:` block, so `go test` checks them.
- **Python examples need their imports** (`from trackparse import ParsedTrack`). Functions that
  take a string raise `TypeError` on other input; `format_track` and `all_artists` take a
  `ParsedTrack` unchecked.
- **Links.** Package READMEs use full `https://` URLs. Check site URLs against the built HTML
  (`site/dist`), ids included. `curl` gets 403 from npmjs.com and 404 from crates.io pages without
  `Accept: text/html`: use `registry.npmjs.org/trackparse` and `crates.io/api/v1/crates/trackparse`.
  For Go, `proxy.golang.org/github.com/4matic/trackparse/go/@v/list` lists the published versions.
  Shields badges can lag a release by some hours.
- **Renaming a heading breaks published links.** Keep the old id with an `<a id="…">` alias, as
  `reference/parse.mdx` does for `serialising-to-json` and `json-and-python-objects`.
- **Spelling** in docs is American (`normalize`, `serialize`, `recognize`, `catalog`), except
  "behaviour", which the spec uses throughout.

### Site internals worth knowing

- **One language choice.** `site/src/lib/lang.ts` reads and writes Starlight's own
  `starlight-synced-tabs__lang` localStorage entry (the value is the tab label). The docs tabs, the
  install switch (`Install.astro`) and the playground code panels all follow it. It must keep
  working when storage is blocked.
- **Playground state is in the URL hash** (input, mode, options, `known`, edits), so a docs page can
  link to a prefilled example. Look at an existing link before writing one.
- **Warning names link to `reference/warnings`** with a text fragment
  (`#tracklist-warnings:~:text=<code>`); dashes in the code are encoded as `%2D`. The section ids
  are `track-warnings`, `tracklist-warnings` and `edit-warnings`.
- **Astro 7's Markdown processor rejects rehype plugins.** Small DOM fixes are head scripts in
  `astro.config.mjs` (the `nowrap` class on space-free code in tables).
- **Building somewhere else.** `astro build --outDir` into `/tmp` fails with `EXDEV` (rename across
  filesystems). Build into a folder under `site/node_modules/.cache/` when `site/dist` is in use.
- `pnpm lint` covers `js/` only, and `astro check` stops at an install prompt. The build and a
  browser are the checks for the site. Look at 1280 and 375 px, light and dark; a headless Chrome
  over the DevTools protocol is enough.

## Working with several agents

- Give each agent its own files and say so. The split that worked for a docs pass: root README;
  the package READMEs and `spec/**/README.md`; `site/src/content/docs/**`; components, `lib/`,
  styles, `index.mdx` and `playground.mdx`. Then one reviewer that edits nothing, then one fixer.
- Agents never commit, push or switch branches. The session that started them commits.
- Two agents building the site at once must not share `site/dist`.
- Timings measured while other agents run are too high, and
  `test_ten_thousand_lines_are_fast` in the Python suite can fail on a busy machine. Re-run it alone
  before believing it.
- A reviewer should run the examples, not read them. Ask for file, line and the concrete fix.

## Known gaps (not yet fixed)

- R9.2 dedup is quadratic in JS and Python on thousands of repeated credits (Rust and Go are
  near-linear).
- `format(track, { feat: "nope" })` drops the featured artists in JS and Python (and in Go, which
  matches JS: `FormatOptions{Feat: "nope"}`) instead of falling
  back to the default, unlike other bad options. Needs a spec decision and a fixture first.
- `format(track, { featMarker: null })` writes the literal marker `null` in JS, Rust and Go;
  Python falls back to the input's marker (found by `scripts/fuzz-ports.mjs`). Needs a spec
  decision and a fixture.
- R13.9.5: `insert` next to a bare CR can merge the new line with an empty LF line on re-read
  (`"\r\r\n\n\r"`, insert after 2 gives 4 lines). All ports agree; no fixture pins it yet.
- Python tracklist types have `to_dict()` but no `from_dict()`; `ParsedTrack` has both.
- JS and Python do not test invariant 6 ("no word goes missing"); only Rust and Go do.
- The npm and PyPI publish jobs use actions by tag; the crates.io job pins its auth action by SHA.
- The playground's Rust snippet is a whole program, the JS and Python ones are bare expressions.

## Porting guide (Rust, Go, others)

A port is done when it passes every fixture and every invariant. The Python, Rust and Go ports are
done and are good second references next to JS: their modules map one to one onto `js/src`.
Checklist:

1. **Codegen from `spec/data` and `spec/VERSION`** into a source file (like
   `js/scripts/gen-data.ts`), with a `--check` mode wired into CI. Expose `SPEC_VERSION`.
2. **Word/whitespace primitives** per R0: whitespace set, word key, dedup key, code-point
   iteration, letter/digit tests.
3. **Pipeline** in the order of `js/src/parser.ts`: normalize (R1), mode prelude (R2), prefixes
   (R4), group scanner and skeleton (R3), separator (R6), artist side (R7), title side (R8),
   assembly and dedup (R9), then `parse_artists` (R10), `format` (R11), options (R12).
   Then tracklists (R13) as a separate module/entry, in the order of `js/src/tracklist/parse.ts`
   (see the R13 notes below).
4. **Matcher** (~40 lines) implementing partial/exact matching and the artist shorthand exactly as
   in `spec/fixtures/README.md`.
5. **Fixture runner** over `spec/fixtures/handwritten` and `spec/fixtures/generated`: validate each
   file against the fixture schema, each output against `parsed-track.schema.json` (or
   `tracklist.schema.json` for `fn: "parseTracklist"`; `applyEdits` cases also check `text`), honour
   `status` (pass / xfail / ambiguous), and keep a port-local skip list that can only shrink.
6. **Property tests** for the SPEC invariants (Hypothesis for Python; the Rust crate and the Go
   module use a small seeded generator of their own, to stay free of dev dependencies; Go adds
   `go test -fuzz` targets).
7. **API** idiomatic for the language but equivalent: Python `parse`, `parse_artists`,
   `format_track`, `normalize` (snake_case fields are fine only if the JSON output keeps the spec's
   keys); Rust with `serde` behind a feature flag and `unicode-normalization` as the only required
   dependency; Go with exported fields, option structs whose zero value is the default (a pointer
   where the zero value is also a real setting), and `MarshalJSON` writing the spec's JSON.
8. **Conformance report** script and a CI workflow mirroring `js.yml`.
9. **Cross-port diff.** Extend `scripts/compare-ports.mjs` (or write the same thing) so every
   fixture's full output is compared with JS, not only the fields the fixture asserts.
10. Update the Ports table in `README.md`, and add the port as a component in
    `release-please-config.json` (+ `.release-please-manifest.json`, and the `linked-versions`
    group) with its own publish job in `release.yml`.

Hazards the Python port ran into. Fixtures alone did not catch all of them; compare-ports did:

- **UTF-16 positions.** Some internal positions are in UTF-16 units in JS (junk offsets used to
  order ties, the `knownArtists` search window `2 * longest + 8`). Count astral characters as two
  there, or ordering differs on emoji and other non-BMP input (`utf16_len` in `_words.py`).
- **JS `trim()` on raw keyword keys.** `options.keywords` entries are trimmed with JS `trim()`
  semantics, not the R0.2 set (`js_trim` in `_words.py`). The two sets differ (U+FEFF, U+0085).
- **`Object.entries` order.** For `keywords.versionHeads` / `junk` maps, integer-like keys come
  first in ascending order, then insertion order. A plain dict iteration does not do that.
- **Python 3.9 at runtime.** `X | None` in annotations evaluated at runtime (dataclasses,
  `get_type_hints`) breaks on 3.9; use `Optional[X]`. Ruff's UP007/UP045 are disabled for this.

Hazards the Rust port ran into. Again the fixtures passed first; compare-ports and random
differential runs against JS (about 500,000 single-track inputs, 290,000 tracklists with edits)
found these:

- **Letter and uppercase need generated tables.** R0.6 is general category L and Lu.
  `char::is_alphabetic()` and `char::is_uppercase()` test the wider `Alphabetic` and `Uppercase`
  properties (Nl, some marks, Other_Uppercase). The crate generates range tables
  (`unicode_tables.rs`).
- **`str::to_lowercase` follows the compiler's Unicode version**, not Node's. The dedup key (R0.4)
  uses a generated lowercase mapping instead, with JS's final-sigma rule.
- **The normalisation crate has its own Unicode version.** `unicode-normalization` ships Unicode 18
  while Node 24 has 17, so 32 new combining marks are ordered differently in NFC. Any port that
  takes NFC from a library inherits that library's Unicode version.
- **Keyword option values are arbitrary strings.** JS passes any string given as a version type or
  junk kind in `options.keywords` through to the output. Typed enums cannot hold them, so the
  pipeline keeps types and kinds as strings (`RawTrack`), `wire` writes that form, and the typed
  API maps unknown names to `VersionType::Version` / `JunkKind::Other`.
- **JS number-to-string rules reach the output.** Edit warnings echo the `line` an edit named, so
  the JSON writer must format numbers as JS does (`1e+21`, no `.0` on whole values, `-0` as `0`).
- **Lone surrogates.** A Rust `str` cannot hold them; the crate's JSON reader (`json.rs`) turns an
  unpaired `\uD800` escape into U+FFFD.

Hazards the Go port ran into. Most were found by probing Go and the x/text sources before the
port started, the rest by compare-ports and the differential run against JS:

- **The user's Go version picks the Unicode tables.** `unicode.IsLetter` / `IsUpper` and
  `strings.ToLower` use the toolchain's Unicode (15.0 in Go 1.24), and `golang.org/x/text`'s
  `norm` package ships Unicode 15 tables for Go before 1.27 and 17 from 1.27 (build tags). Two
  users of the same module version would get different output. The module generates L, Lu, the
  lowercase map and the NFC/NFD tables from Node instead (`scripts/gen_unicode.mjs`,
  `unicode_gen.go`), so its Unicode version is the reference engine's, and it has no dependency.
- **Combining classes are ranks.** Node has no API for canonical combining classes, so
  `gen_unicode.mjs` derives them from `normalize()` as ranks: normalization only compares classes
  for order and equality, and ranks keep both. The real Unicode values are not in the tables.
- **`strings.ToLower` has no final-sigma rule** (`ΟΔΟΣ` gives `οδοσ`, JS `οδος`), and
  **`strings.TrimSpace` is not JS `trim()`**: it strips U+0085 and keeps U+FEFF, the opposite of
  JS on both. Use `jsToLower` and `jsTrim`.
- **Invalid UTF-8.** JS strings are UTF-16 and cannot hold it; Go strings can. `for range` gives
  one U+FFFD per bad byte, while `TextDecoder` and Rust's `from_utf8_lossy` give one per maximal
  ill-formed subpart (WHATWG). The public functions sanitize input with the WHATWG rule first
  (`sanitize` in `words.go`), so Go and Rust read the same bytes as the same text.
- **`at()` returns -1, not 0.** Out-of-range lookups return `noRune` (-1), which is not a code
  point and fails every class test. Returning 0 would be wrong: U+0000 is real input and must be
  treated as a character.
- **Typed strings remove Rust's `RawTrack` split.** Enums are `type VersionType string` and so on,
  so a version type or junk kind introduced by `Keywords` travels to the output unchanged; the
  typed API needs no "unknown name" mapping.
- **Zero-value options must not hide a setting.** An option struct's zero value means the
  default, so a field whose zero value is also a real JS setting is a pointer: `FeatMarker
  *string` (a pointer to `""` is JS `featMarker: ""`) and `AudioSeconds *float64` (a pointer to 0
  is JS `audioSeconds: 0`), set with `trackparse.Ptr`. `ModeAuto` is `"auto"` and `""` means the
  function's default (auto for a parse, tracklist for a tracklist), so both can be asked for. JS
  reads an empty string as an unknown name; in Go any other unknown name says that.
  `TestTypedOptionsExpressEveryFixture` (`options_test.go`) maps every fixture's JSON options to
  the typed structs and requires the same output bytes as the JSON door.
- **Ordered slices for the two keyword maps.** `Keywords.VersionHeads` and `Keywords.Junk` are
  slices, read in JS `Object.entries` order (integer-like keys first, ascending, then insertion
  order; a repeated key keeps its first place and the last value). A Go map has no insertion order.
- **Stable sorts and no map iteration on output paths.** JS `sort` is stable, `sort.Slice` is not:
  use `slices.SortStableFunc` (go.yml greps for `sort.Slice(`). Never `range` over a map where the
  order can reach the output.
- **JSON bytes.** `encoding/json` escapes `<`, `>`, `&` and U+2028/U+2029 even inside a
  `MarshalJSON` result, so `json.Marshal(track)` is not byte-identical to `JSON.stringify` (the
  values are equal). `json.go` writes JS's bytes; `MarshalJSON` called directly, or an encoder with
  `SetEscapeHTML(false)`, keeps them.
- **Rounding.** Half up is `math.Floor(v*10^d + 0.5)`. `math.Round` differs on values such as
  `0.49999999999999994` and on negatives.

R13 (tracklists) notes for a new port:

- **Two passes per text.** First a cheap *probe* per line: the R13.2 time scan, then R1, R2 and R4
  only (`probe` in `js/src/parser.ts`), to learn whether R4.2 took a position and whether the
  skeleton R6 would search has a spaced dash. Those facts decide the list context (R13.4.2: at
  least half of the candidate lines have a spaced dash) and the bare positions (R13.5). Only then
  is each line parsed once, with the list-context flag passed into R6.10. Testing ` - ` on `raw`
  instead of the skeleton gets `00:00 - Intro` and `01 - Title` wrong.
- **Line split is LF / CRLF / CR only.** U+2028, U+2029 and U+0085 are R0.2 whitespace but not
  line breaks. `raw` is never normalized; R1 runs inside the per-line `parse` only.
- **Suggestion search** (R13.7) is two steps: the out-of-order line's own time between its timed
  neighbours, then, only if that found no candidate at all, the previous timed line's time. Change
  1 digit, then 2 (never fraction digits); only the smallest digit count with any candidate counts,
  and exactly one candidate gives a suggestion. On the real corpus this second step is what finds
  `01:22:21` → `01:02:21`.
- **Numbers.** Fractional seconds are the correctly rounded double of the decimal string (JS
  `Number()`, Python `float()`); whole values must serialize as integers (`3167`, not `3167.0`).
  Rewriting rounds half up with `floor(v · 10^d + 0.5)`, never a language `round()` (Python's is
  banker's rounding). Edit fields follow JS number semantics: a bool is not a number, `null` and
  non-finite values make the edit `edit-invalid` (`js_number` in `_tracklist_options.py`).
- **Uppercase** for R6.10's tight dash is general category Lu (`unicodedata.category(c) == "Lu"`).
- **No UTF-16 positions** in R13: everything is strings and line numbers, so the `utf16_len`
  hazard above does not apply here.
- **Time ceilings.** R13.11 asks for linear time; each port's suite picks its own 10,000-line
  ceilings: `parseTracklist` JS 3 s, Python 15 s; `applyEdits` (shift + sort, 2,000 edits) JS 1 s,
  Python 2 s / 5 s. The Rust ceilings are in `rust/tests/tracklist_properties.rs`.

## Versioning and release

Releases are automated with [release-please](https://github.com/googleapis/release-please):

1. Feature PRs are **rebase-merged**, so every commit lands on `main` and becomes its own
   changelog line: each commit message must be a conventional commit, and so must the PR title
   (`pr-title.yml` enforces the title). Release PRs (`chore: release main`) are squash-merged. Types: `feat`, `fix`, `perf`, `revert`, `docs` (shown in the changelog) and `refactor`,
   `test`, `build`, `ci`, `chore` (hidden). Scopes: `js`, `python`, `rust`, `go`, `spec`, `generator`, `fixtures`, `site`, `ci`, `deps`…
2. On every push to `main`, `release.yml` updates a **release PR** (`chore: release main`) that
   bumps `js/package.json`, `spec/VERSION`, `SPEC_VERSION` in `js/src/data.generated.ts`, the
   Python, Rust and Go version files (below), and writes `js/CHANGELOG.md` / `spec/CHANGELOG.md` /
   `python/CHANGELOG.md` / `rust/CHANGELOG.md` / `go/CHANGELOG.md`.
3. **Merging the release PR** creates tags `js-vX.Y.Z`, `spec-vX.Y.Z`, `python-vX.Y.Z`,
   `rust-vX.Y.Z` and `go/vX.Y.Z`, GitHub Releases with the changelog as notes, then publishes
   `trackparse` to npm and to PyPI via trusted publishing (OIDC, no stored token), attaches the
   tarball / wheel + sdist and appends install instructions to each release. The crates.io job
   (`publish-crates`) runs only when the repository variable `CRATES_PUBLISH` is `true` (see
   below). The Go module needs no upload: the tag is the release (see below). For a bigger
   release, add `.github/release-notes/X.Y.Z.md` beforehand: it is prepended to the notes.

Version rules:

- `js` (the reference implementation) and `spec` are **linked**: always the same version.
  `spec.yml` fails if `js/package.json` and `spec/VERSION` differ.
- While 0.x: `feat` and breaking changes bump the **minor**, `fix`/`perf` bump the patch. Changing an
  existing fixture expectation is a behaviour change → `feat`. Adding fixtures for behaviour that
  already passes is not.
- `python` is **linked** too: same version as `js` and `spec`. The release PR also bumps
  `python/pyproject.toml`, `__version__` in `python/src/trackparse/__init__.py` and `SPEC_VERSION`
  in `python/src/trackparse/_data.py`, and writes `python/CHANGELOG.md`. Merging it tags
  `python-vX.Y.Z` and publishes to PyPI via trusted publishing (environment `pypi`), with the wheel
  and sdist attached to the release. `spec.yml` checks that all five versions match (js, spec,
  python, rust, go).
- `rust` is **linked** as well: a release-please component (`release-type: rust`) in the same
  `linked-versions` group. The release PR bumps `rust/Cargo.toml` (the line marked
  `x-release-please-version`) and `SPEC_VERSION` in `rust/src/data.rs`, and writes
  `rust/CHANGELOG.md`. Merging it tags `rust-vX.Y.Z` and creates the GitHub Release. The release PR
  also bumps the crate's own entry in the committed `rust/Cargo.lock`.
- **crates.io publishing** is the `publish-crates` job in `release.yml` (environment `crates`:
  version check, `cargo test --all-features`, `cargo package`, `cargo publish` with a token from
  crates.io trusted publishing, the `.crate` file attached to the release). It runs only when the
  repository variable `CRATES_PUBLISH` is `true`, which it has been since 0.5.0. The trusted
  publisher on crates.io is `4matic/trackparse`, workflow `release.yml`, environment `crates`.
  0.4.0 was published by hand, because crates.io can only configure trusted publishing for a
  crate that already exists. With the variable off, a release still tags `rust-vX.Y.Z` but skips
  the job.
- `go` is **linked** as well: a release-please component (`release-type: go`, which bumps no
  version file of its own) in the same `linked-versions` group. The release PR bumps `SpecVersion`
  in `go/data_gen.go` (the line marked `x-release-please-version`, which
  `go/scripts/gen_data.mjs` writes, so `--check` stays clean after the bump) and writes
  `go/CHANGELOG.md`. The component sets `"tag-separator": "/"`, so its tag is `go/vX.Y.Z` and its
  GitHub Release is named `go: vX.Y.Z`. The go command only finds a module in the `go/`
  subdirectory under tags with exactly that prefix: `go-v0.6.0` would be ignored. The release
  workflow outputs keep the path as their key (`go--release_created`, `go--tag_name`,
  `go--version`).
- **Go publishing is the tag.** There is no registry upload and no token: the module proxy
  (`proxy.golang.org`) fetches `go/vX.Y.Z` on the first request, and the checksum database
  (`sum.golang.org`) records its hash. The `publish-go` job in `release.yml` (environment `go`)
  checks out the tag, checks that `SpecVersion`, `spec/VERSION` and the tag agree and that
  `gen_data.mjs --check` passes, runs `go vet` and `go test`, makes that first request with
  retries (`GOPROXY=https://proxy.golang.org go list -m github.com/4matic/trackparse/go@vX.Y.Z`),
  runs `go get` and a small program from a clean module, requests the version's `.info` from the
  proxy (pkg.go.dev adds versions it sees there; the page can take a few minutes), and appends
  install notes to the GitHub Release. **A published Go version is permanent**: the proxy and the
  checksum database keep the tag's content forever, even if the tag is deleted or moved. Never
  move or re-create a `go/v…` tag. A bad version can only be marked with a `retract` directive in
  `go/go.mod`, released in a new version.
- **A publish job failed after the tags were created?** Fix the cause on `main` with a hidden commit
  type (`ci`/`chore`, so no new release), then re-run publishing for the existing tag:
  `gh workflow run release.yml -f python_tag=python-vX.Y.Z` (PyPI) or
  `-f rust_tag=rust-vX.Y.Z` (crates.io) or `-f go_tag=go/vX.Y.Z` (the Go checks, proxy request
  and release notes; the module itself was published by the tag). The npm job always republishes from a fresh release; a
  failed npm publish needs the same treatment by hand.
- **Things the 0.5.0 release taught:**
  - The release PR is opened with `GITHUB_TOKEN`, so no CI runs on it and GitHub shows it as
    `UNSTABLE`. That is expected; `main` was green when it was generated.
  - A `docs:` commit under `js/`, `python/`, `rust/` or `go/` (a package README) opens a release PR. A
    commit that touches only the root README, `AGENTS.md`, `site/` or `.github/` does not.
  - A component's first changelog lists every commit that ever touched its folder, because there
    is no earlier tag to start from.
  - crates.io refuses to publish until the account has a verified email address.
  - After a release, check every registry by API, not by badge: `registry.npmjs.org`,
    `pypi.org/pypi/trackparse/json`, `crates.io/api/v1/crates/trackparse`, and the Go proxy:
    `proxy.golang.org/github.com/4matic/trackparse/go/@v/list`.
  - The Go component's first changelog links `go/v0.5.1...go/vX.Y.Z`, a tag that never existed
    (the manifest starts Go at the linked 0.5.1), as rust's first entry linked `rust-v0.4.0`.
  - Don't force-push a PR branch to pick up `main`: GitHub's rebase-merge rebases it when there
    are no conflicts.
- **Agents never merge release PRs, publish, push tags or bump versions on their own initiative.**
  Merging the release PR is the maintainer's publish decision.

## Code style

- Small, single-purpose functions; scanners over code points instead of clever regexes.
- Cite rule IDs in comments (`// R6.2: peel version suffixes`) so code, spec and fixtures can be
  cross-referenced.
- JS: TypeScript strict, Biome formatting (2 spaces, double quotes, 100 columns), no runtime
  dependencies, ES2020 target, ESM + CJS via tsup.
- Python: ruff (100 columns), `mypy --strict`, no runtime dependencies, Python 3.9 syntax.
- Rust: rustfmt (100 columns), clippy with `-D warnings`, MSRV 1.74, `unicode-normalization` as
  the only required dependency, no `unsafe`. `unwrap`, `expect`, `panic!` and indexing are denied
  in `src/` (R0.8): use `get`, iterators and `Option`.
- Go: `gofmt`, `go vet` and staticcheck clean, `go 1.22` in `go.mod`, no `require` lines (no
  dependencies), no `unsafe`, no cgo. Sorts are stable (`slices.SortStableFunc`, never
  `sort.Slice`); no `range` over a map on a path that reaches the output. No `recover()` in the
  library: a panic is a bug to fix, found by fuzzing and the differential run, not hidden. Go has
  no lint against indexing, so look up code points with `at()` (it returns -1 out of range).
  Every exported identifier has a doc comment; examples are `Example` functions with `// Output:`.
- Public types in `js/src/types.ts`, `python/src/trackparse/types.py`, `rust/src/types.rs` and
  `go/types.go` mirror the schemas; change them together.
- No `throw` (or panic) on any input path. Bad options fall back to defaults.

## Definition of done

- [ ] SPEC.md updated (rule text and IDs) when behaviour changed
- [ ] Handwritten fixtures cite the rules; generator updated and regenerated if relevant
- [ ] `pnpm test`, `pnpm lint`, `pnpm --filter trackparse typecheck` pass
- [ ] `pnpm conformance` is 100%; `js/test/skip.json` did not grow
- [ ] `pnpm gen:check` and `pnpm --filter @trackparse/fixture-gen validate` pass
- [ ] Python: `gen_data.py --check`, ruff, `mypy --strict src` and `pytest` pass; conformance is
      100%; `python/tests/skip.json` did not grow
- [ ] Rust: `gen_data.mjs --check`, `cargo fmt --check`, clippy and `cargo test` pass; the
      `TRACKPARSE_REPORT=1` report is 100%; `rust/tests/skip.json` did not grow
- [ ] Go: `gen_data.mjs --check`, `gen_unicode.mjs --check`, `gofmt -l` empty, `go vet`, staticcheck
      and `go test -race ./...` pass; the `TRACKPARSE_REPORT=1` report is 100%;
      `go/testdata/skip.json` did not grow
- [ ] `node scripts/compare-ports.mjs` reports 0 differences (all four ports)
- [ ] `pnpm --filter trackparse size` and `check:package` pass
- [ ] R13 changes: `node scripts/corpus-check.mjs` passes when you have the corpus
- [ ] Conventional commits and PR title; `feat` if any fixture expectation changed
- [ ] Site pages (`site/src/content/docs`) and READMEs updated in the same PR, examples in
      JavaScript, Python, Rust and Go tabs with real output, `**Docs:**` links still pointing at existing
      pages, `pnpm docs:build` passes
