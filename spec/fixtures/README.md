# Fixtures

The shared test corpus: 13,130 cases, 1,203 written by hand and 11,927 generated. Every port
(JavaScript, Python, Rust and Go) runs every case here. The fixtures, not any one implementation,
define correct behaviour.

**Docs:** [How it's tested](https://4matic.github.io/trackparse/project/testing/) · [Contributing](https://4matic.github.io/trackparse/project/contributing/)

```
fixtures/
  handwritten/*.json   curated cases, grouped by topic; edit by hand
  generated/*.json     combinatorial cases from ../generator; NEVER edit by hand
```

Generated files use the same format in a compact form (per-file `defaults.options`, the string
shorthand for the first primary artist). See [`../generator/README.md`](../generator/README.md).

Each file validates against [`../schema/fixture-file.schema.json`](../schema/fixture-file.schema.json).

## File format

```jsonc
{
  "$schema": "../../schema/fixture-file.schema.json",
  "description": "Remix/version groups in brackets",
  "defaults": { "fn": "parse", "options": { "mode": "clean" } },
  "cases": [
    {
      "id": "brackets-version-001",          // unique across the whole repo, never reused
      "input": "Fox Stevenson - Bruises (Magnetude Remix)",
      "expected": {
        "artists": ["Fox Stevenson"],
        "title": "Bruises",
        "versions": [{ "type": "remix", "artists": ["Magnetude"] }]
      },
      "rules": ["R5.7"],                      // SPEC.md rule IDs this case exercises
      "tags": ["remix"],
      "source": "get-artist-title (MIT, © 2016 René Kooi)", // where the case came from (optional)
      "note": "why this case exists"          // optional
    }
  ]
}
```

| Field | Meaning |
|---|---|
| `id` | Unique, stable, `[a-z0-9:._-]`. Handwritten: `<file-stem>-NNN`. Generated: `gen:<scenario>:<atoms>`. |
| `fn` | `parse` (default), `parseArtists`, `format`, `normalize`, `parseTracklist`, `applyEdits` (SPEC R13). Can be set in `defaults` or on the case. |
| `input` | Input string. For `fn: "format"` the input is parsed first (with `options`), then formatted with `formatOptions`. For `parseTracklist` / `applyEdits` it is the whole (multi-line) text. |
| `edits` | `fn: "applyEdits"` only: the edit list (SPEC R13.9). |
| `options` | Parse options (tracklist options, SPEC R13.10, for `parseTracklist` / `applyEdits`). Merged over `defaults.options` (shallow). |
| `formatOptions` | Format options (fn `format` only). |
| `expected` | Partial expectation, see below. For `parse` an object, for `parseArtists` an array, for `format` / `normalize` a string, for `parseTracklist` a Tracklist, for `applyEdits` `{ text, changed, warnings }`. |
| `match` | `partial` (default) or `exact`. |
| `status` | `pass` (default), `xfail` (known wrong; the port must still fail it), `ambiguous` (several answers are acceptable; uses `expectedAny`). |
| `expectedAny` | For `ambiguous`: the case passes if any entry matches. |

## Matching semantics (`match: "partial"`)

Every port implements this matcher (about 40 lines). It is the same everywhere:
[`js/test/match.ts`](../../js/test/match.ts), [`python/tests/match.py`](../../python/tests/match.py),
`matches` in [`rust/tests/fixtures.rs`](../../rust/tests/fixtures.rs) and `matches` in
[`go/match_test.go`](../../go/match_test.go).

1. **Objects:** every key in `expected` must exist in `actual` and match recursively. Keys not in
   `expected` are ignored. An explicit `null` must be `null`.
2. **Arrays:** same length, compared element by element, in order.
3. **Scalars** (strings, numbers, booleans): strict equality. No normalization, no case folding.
4. **Artist shorthand:** when the expected element is a string and the actual element is an
   object with a `name` key, it matches iff `actual.name === expected`. This applies to arrays at
   `artists` and `versions[].artists`, and to `parseArtists` results.
5. `match: "exact"`: deep equality of the complete value. The artist shorthand is not allowed:
   exact cases spell every field out. Use a few exact cases per file to pin the whole schema.
6. A top-level `specVersion` (on a Tracklist) is never compared, in either mode: it changes with
   every release. Fixtures omit it; schema validation still requires it in the output.

Example: `"artists": ["Noisia", { "name": "Foreign Beggars", "role": "featured" }]` asserts exactly
two artists, the first named Noisia (any role), the second with name and role checked.

When you write a case, assert what it is about plus `artists` and `title`. Don't spell out
unrelated fields: the case then survives changes elsewhere, and the exact cases catch the rest.

## Status workflow

- New behaviour: add `pass` cases first (spec first), then implement.
- A case the reference port can't meet yet and that is out of scope for this release is `xfail`
  with a `note`. A port's runner fails if an `xfail` case unexpectedly passes, so the status gets
  promoted.
- Port-specific gaps go in that port's skip list (`js/test/skip.json`, `python/tests/skip.json`,
  `rust/tests/skip.json`, `go/testdata/skip.json`), not here. A skip list can only shrink, and all
  four are empty today.

## Running the fixtures

From the repository root:

```sh
pnpm test                          # JavaScript: js/test/fixtures.test.ts
(cd python && uv run pytest)       # Python: python/tests/test_fixtures.py
(cd rust && cargo test)            # Rust: rust/tests/fixtures.rs
(cd go && go test ./...)           # Go: go/fixtures_test.go

# one case, by id
pnpm --filter trackparse exec vitest run test/fixtures.test.ts -t "brackets-version-001"
(cd python && uv run pytest tests/test_fixtures.py -k "brackets-version-001")
(cd go && TRACKPARSE_ONLY=brackets-version-001 go test -run TestFixtures -v)

# pass rate per fixture file and per spec rule
pnpm conformance
(cd python && uv run python scripts/conformance.py)
(cd rust && TRACKPARSE_REPORT=1 cargo test --test fixtures -- --nocapture)
(cd go && TRACKPARSE_REPORT=1 go test -run TestFixtures -v)

# after pnpm build: the complete output of all four ports, compared
node scripts/compare-ports.mjs

# schema and repo-wide unique ids, for every fixture file
pnpm --filter @trackparse/fixture-gen validate
```

## Changing expectations

Any change to an existing `expected` value is a behaviour change. Commit it as a `feat` (conventional
commits); release-please then bumps the spec minor version (0.x) and writes the changelog. See
[AGENTS.md](../../AGENTS.md#versioning-and-release).
