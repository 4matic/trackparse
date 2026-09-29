# Fixtures

The shared test corpus. **Every port runs every case here**. The fixtures, not any one
implementation, define correct behaviour.

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
      "source": "neuropunk-web",              // where the case came from (optional)
      "note": "why this case exists"          // optional
    }
  ]
}
```

| Field | Meaning |
|---|---|
| `id` | Unique, stable, `[a-z0-9:._-]`. Handwritten: `<file-stem>-NNN`. Generated: `gen:<scenario>:<atoms>`. |
| `fn` | `parse` (default), `parseArtists`, `format`, `normalize`. Can be set in `defaults` or on the case. |
| `input` | Input string. For `fn: "format"` the input is **parsed first** (with `options`), then formatted with `formatOptions`. |
| `options` | Parse options. Merged over `defaults.options` (shallow). |
| `formatOptions` | Format options (fn `format` only). |
| `expected` | Partial expectation, see below. For `parse` an object, for `parseArtists` an array, for `format` / `normalize` a string. |
| `match` | `partial` (default) or `exact`. |
| `status` | `pass` (default), `xfail` (known wrong; the port must still fail it), `ambiguous` (several answers are acceptable; uses `expectedAny`). |
| `expectedAny` | For `ambiguous`: the case passes if **any** entry matches. |

## Matching semantics (`match: "partial"`)

Implement this matcher in every port (≈40 lines). It is the same everywhere:

1. **Objects:** every key in `expected` must exist in `actual` and match recursively. Keys not in
   `expected` are ignored. An explicit `null` must be `null`.
2. **Arrays:** same length, compared element by element **in order**.
3. **Scalars** (strings, numbers, booleans): strict equality. No normalization, no case folding.
4. **Artist shorthand:** when the expected element is a **string** and the actual element is an
   object with a `name` key, it matches iff `actual.name === expected`. This applies to arrays at
   `artists` and `versions[].artists`, and to `parseArtists` results.
5. `match: "exact"`: deep equality of the complete value (shorthand still allowed? **no**: exact cases
   spell every field out). Use a few exact cases per file to pin the whole schema.

Example: `"artists": ["Noisia", { "name": "Foreign Beggars", "role": "featured" }]` asserts exactly
two artists, the first named Noisia (any role), the second with name and role checked.

**Tip for authors:** assert what the case is *about* plus `artists` and `title`. Don't over-specify
unrelated fields. That keeps fixtures robust to changes elsewhere, and exact cases catch
the rest.

## Status workflow

- New behaviour → add `pass` cases first (spec-first), then implement.
- A case the reference port can't meet yet and that is out of scope for this release → `xfail`
  with a `note`. A port's runner **fails** if an `xfail` case unexpectedly passes, so the
  status gets promoted.
- Port-specific gaps go in that port's skip list (e.g. `js/test/skip.json`), not here.

## Changing expectations

Any change to an existing `expected` value is a behaviour change. Commit it as a `feat` (conventional
commits); release-please then bumps the spec minor version (0.x) and writes the changelog. See
[AGENTS.md](../../AGENTS.md#versioning-and-release).
