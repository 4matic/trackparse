# Changelog

All notable changes to the spec and to each port. Ports share `MAJOR.MINOR` with the spec version
they implement; patch versions are independent per port. Any change to fixture expectations is a
behaviour change and must be listed here.

## Unreleased

### spec 0.1.0
- Initial specification (rules R0–R12), output schema, options schema and fixture format.
- Shared word lists in `spec/data`.
- Handwritten fixture corpus and combinatorial generator.
- Spec reconciliation from the fixture review: ZWJ/ZWNJ are kept (R1.2); filename `N - ` is always
  a position (R4.2); version type resolution, generic heads and version/cover descriptors (R5.7);
  years inside junk groups (R5.1); leading `- Title` (R6.1); prefix-form suffixes and R6.2 (ii);
  artist-side flags, quoted credit lists, joiner inheritance (R7.1, R7.4, R7.6); title-side
  markers limited to `feat.`/`ft.`/`featuring` and `prod.`/`prod. by`/`produced by`/`prod.by`
  (R8.3); role-upgrade joiners (R9.2); text-order year and junk (R9.6, R9.7); `; ` rendering
  (R11.1). `remastered`/`remaster`/`rework`/`re-work` removed from descriptors. Ambiguous
  fixtures resolved.
- Generated fixtures: compact encoding, about 6 MB.
- Producers are a separate credit class (R9.2): deduped only among producers, never removed or
  role-upgraded by a primary/featured credit of the same name; precedence is primary > featured.

### js 0.1.0
- Reference implementation: `parse`, `parseArtists`, `format`, `normalize`, `allArtists`, `createParser`.
