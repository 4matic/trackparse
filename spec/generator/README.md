# @trackparse/fixture-gen

Combinatorial fixture generator for the trackparse spec. It writes
`spec/fixtures/generated/*.json` (about 11k cases, about 6 MB). Every port runs these files next to
the handwritten fixtures.

## Oracle principle

The generator is **not a parser**. Each case is a *composition* of hand-written **atoms** (an
artist, a joiner, a feat marker and placement, a title, a version, a junk phrase, a prefix, a mode,
a Unicode transform…). Every atom carries two things: the text it renders, and the output it
contributes to the parse result (for example `{R} Remix` → type `remix`, remixers = the credit).
`model.ts` renders the input string and computes the expected result **from the atoms alone**.
It never runs a parser.

That only works when a composition is unambiguous. `problem()` in `model.ts` rejects every
composition whose expected output would depend on rules the atoms do not encode, citing the
SPEC rule each time (a digits-only artist before a dash suffix, a trailing junk phrase that
overlaps the title, an undotted `ft` in the title…). `text.ts` holds *conservative* safety
predicates for atoms ("could this title word be recognised as a marker/head/junk?"). They err on
the side of rejecting.

When the SPEC changes, update the atom expectations and `problem()` in the same change. Then run
`gen`, and the JS port must pass the result.

## Scenarios

| Scenario | Focus (SPEC rules) |
|---|---|
| `artists-joiners` | main credit lists × every joiner × `splitAnd` (R7.3) |
| `feat-placement` | every feat marker × placement × 1–2 names, optional producer (R5.2, R5.3, R7.1, R7.2, R8.1, R8.3) |
| `versions` | every version atom × delimiter × remixer shapes × year/flag/unknown groups (R5.4–R5.8, R6.2, R9.4) |
| `prefixes` | timestamps and positions × numeric artists (R4) |
| `junk-youtube` | junk in every form, pipes, platform suffixes, auto mode (R2, R5.1, R6.2, R8.4) |
| `filename` | extensions, underscores, filename-only positions, unspaced dashes (R2.3, R4.2, R6.7) |
| `unicode` | R1 normalization transforms over Unicode names/titles |
| `dash-suffix` | dash-suffix peeling with and without an artist side (R6.2, R6.8, R8.2) |
| `dedup` | repeated credits in every placement (R0.4, R7.6, R9.2), exhaustive |
| `kitchen-sink` | all dimensions at once |

Scenarios whose product is at most `exhaustiveLimit` are exhaustive. The others use a greedy
pairwise covering array (every pair of atom values from two dimensions appears in at least one
valid case) plus a seeded random sample (`sample` per scenario, `kitchenSinkSample` in
`config.json`). Output is deterministic for a given `config.json` seed.

## Output encoding (kept compact, still strict)

- One case per line, sorted by id. Ids are `gen:<scenario>:<atom ids joined by .>`, with `none`
  for unused dimensions.
- `defaults.options` of each file is the most common per-case `options`. A case omits `options`
  when it would equal the defaults (the runner merges them shallowly).
- `expected.mode` is asserted only for `mode: "auto"` cases. An explicit mode is echoed verbatim.
- `expected.artists`: the first primary credit (role `primary`, joiner `null`, source `artist`)
  uses the string shorthand (name only). Every other credit is a full
  `{name, role, joiner, source}` object.
- `expected.fullTitle` is present only when there are versions (otherwise it equals `title`).
- The `versions` and `dash-suffix` scenarios spell every version field out. The others assert
  `type`, `raw`, `artists`, `delimiter` and the non-default fields.
- No per-case `tags`: the id already names the atoms. `summary.json` has per-dimension counts.

## Commands

```sh
pnpm --filter @trackparse/fixture-gen gen        # write spec/fixtures/generated
pnpm --filter @trackparse/fixture-gen check      # regenerate in memory, fail on drift
pnpm --filter @trackparse/fixture-gen validate   # schema + repo-wide unique ids (all fixtures)
pnpm --filter @trackparse/fixture-gen test       # unit tests (atoms, model, generated output)
pnpm --filter @trackparse/fixture-gen typecheck
```

## Adding an atom

1. Add it to the right pool in `src/atoms.ts` with a lowercase `[a-z0-9-]` id that is unique in
   its pool. Fill in its expected contribution (for a version: `type`, `modifiers`,
   `descriptor`, `year`, `unknownArtist`, plus `creditAsDescriptor` / `prefixForm` when those
   rules apply).
2. If the atom can interact with a rule in a way the model does not encode, add a rejection to
   `problem()` (or a predicate in `text.ts`), citing the SPEC rule. Add a unit test in
   `test/model.test.ts` or `test/atoms.test.ts`.
3. Use it in a scenario (`src/scenarios/*.ts`). New atoms in an existing pool are picked up
   automatically.
4. Run `gen`, then the JS suite (`pnpm --filter trackparse test`). A failure means either the
   atom's expectation is wrong or the reference implementation disagrees with the spec. Fix the
   one that is wrong. Never edit generated files by hand.
