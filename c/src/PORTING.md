# Porting notes for c/src

The C port translates `go/*.go` file by file. Every internal type and prototype is in
`internal.h` (one section per Go file). The runtime is in `rt.h`, and the primitives are in
`words.h`, `unicode.h`, `digits.h`, `data_gen.h` and `unicode_gen.h`. This file maps the Go files
to C files and lists the translation decisions that every `.c` file must follow. The rules in
`c-brief.md` still apply. This file only adds to them.

## Go file -> C file -> internal.h section

The track column is a suggestion. The coordinator assigns the real owners.

| Go file | C file | internal.h section | Track |
|---|---|---|---|
| types.go | types.c | types.go | API |
| options.go | options.c | options.go | API |
| context.go | context.c | context.go | artist side |
| tables.go | tables.c | tables.go | artist side |
| normalize.go | normalize.c | normalize.go | title side |
| mode.go | mode.c | mode.go | title side |
| prefix.go | prefix.c | prefix.go | title side |
| scanner.go | scanner.c | scanner.go | title side |
| credits.go | credits.c | credits.go (placed before classify) | artist side |
| classify.go | classify.c | classify.go | artist side |
| separator.go | separator.c | separator.go | title side |
| artists.go | artists.c | artists.go | artist side |
| title.go | title.c | title.go | title side |
| assemble.go | assemble.c | assemble.go | title side |
| parser.go | parser.c | parser.go | title side |
| format.go | format.c | format.go | title side |
| tl_options.go | tl_options.c | tl_options.go | tracklist |
| tl_split.go | tl_split.c | tl_split.go | tracklist |
| tl_times.go | tl_times.c | tl_times.go | tracklist |
| tl_emphasis.go | tl_emphasis.c | tl_emphasis.go | tracklist |
| tl_kinds.go | tl_kinds.c | tl_kinds.go | tracklist |
| tl_positions.go | tl_positions.c | tl_positions.go | tracklist |
| tl_warnings.go | tl_warnings.c | tl_warnings.go | tracklist |
| tl_parse.go | tl_parse.c | tl_parse.go | tracklist |
| tl_edits.go | tl_edits.c | tl_edits.go | tracklist |
| json.go (writer half) | json.c | json.go, writer half | API |
| json.go (reader half) | ../wire/json_read.c | wire/json_read.h | wire |
| wire.go | ../wire/wire.c | wire.go (+ wire/wire.h) | wire |
| trackparse.go | trackparse.c | trackparse.go | API |
| words.go, unicode.go, digits.go | words.c, unicode.c, digits.c | words.h, unicode.h, digits.h | primitives |
| data_gen.go, unicode_gen.go | data_gen.c, unicode_gen.c | data_gen.h, unicode_gen.h | codegen |

## Integer widths

- Go `int` is `ptrdiff_t`, as in words.h. This covers indices, offsets, lengths, counts, years,
  position numbers, seconds and line numbers. Go `[]int` is `tp_idxvec` (`TP_VEC(ptrdiff_t)`,
  declared in internal.h). Go `[][]int` is `tp_idxvec_vec`.
- Go `int64` is `int64_t`, and Go `[]int64` is rt.h's `tp_intvec`.
- Exception: the edit fields that are JS numbers in the public API (`long long`) are `int64_t`,
  or `tp_intvec` for lists. These are `tp_edit.line`, `after`, `from` and `lines`,
  `tp_edit_warning.line` and `changed` in both edit results. `tp_saturating_int` returns
  `int64_t`, which is what Go's 64-bit `int` gives.
- Convert explicitly between `ptrdiff_t` and `size_t`, for example `(ptrdiff_t)v.n` and
  `(size_t)i`. Comparing a signed and an unsigned value warns under `-Wextra`. The code must also
  build warning-free on 32-bit, where `int64_t` and `ptrdiff_t` differ, so never mix
  `int64_t` and `ptrdiff_t` without a cast.
- Go `uint8` enums are C enums.

## Typed strings

`Mode`, `Role`, `Source`, `VersionType`, `Delimiter`, `JunkKind`, `Warning`, `EntryKind`, `Eol`,
`WarningCode`, `Severity`, `EditOp`, `EditWarningCode`, `SplitAnd`, `FeatPlacement`,
`JoinerStyle` and `VersionsOption` are all `tp_str`. Each has a reading typedef (`tp_role`, and
so on) and `TP_S()` constants (`TP_ROLE_PRIMARY`). Compare them with `tp_str_eq`. They stay
strings because keywords can introduce any version type or junk kind. `TP_S` is a compound
literal, so it is not a constant expression. In a `static const` table, write `{"remix", 5}`.

## nil and empty

- The JSON writer writes a vector as `[]` whether its `p` is NULL or not. So Go's
  `if x == nil { x = []T{} }` (parser.go `runArtists` and `finish`, and types.go `result`) needs
  no C code.
- Exactly one slice nil-check reaches the output: `Edit.Lines`. A nil `Lines` means "not
  given"; an empty `Lines` means "shift no line". C has `bool has_lines` for this. Never test
  `lines.p`.
- Nullable Go pointers are `const T *` into the arena, and NULL is nil. To set one, allocate it:
  `int64_t *y = tp_alloc(sizeof *y); *y = v;`.
- Public results (API track): it is the API's decision whether a zero-length array has
  `items == NULL`. The pipeline never depends on it.

## Slices and aliasing

Copying a `TP_VEC` header shares its storage, as copying a Go slice header does. Where Go
builds a new slice (`slices.Concat`, `slices.Clone`, `append([]T(nil), ...)`), build a new
vector with `tp_clone` / `tp_append`. Never append to a vector that you received unless the Go
code appends to that same slice. Go's `append` on a slice with spare capacity aliases, and C
must alias in the same places, which is only where Go does.

`tp_credit_lists.entries` deliberately shares storage with the caller's entries (assemble.go).

## Maps

Never iterate a map on a path that reaches the output.

- `groupMap` (`map[int]*group`) is `tp_group_map`, a vector sorted by `pos`. `tp_scan` adds
  groups in strictly ascending opener offset. Look groups up with `tp_group_map_get` (binary
  search).
- The removal sets (`map[int]struct{}` from `extractGroups` and `parseArtistSide`) are an
  ascending `tp_idxvec`. Both are filled in `tp_groups_of` order, which is ascending.
  `tp_remove_groups` checks membership with `tp_sorted_has`.
- `creditLists.heaps` (`map[int][]int`) is `tp_idxvec_vec`, indexed by list id. Ids run from 0
  to `ctx.list_seq - 1`. Grow the vector on demand.
- `tables.tightJoiners` (`map[string]string`) is `tp_kv_vec`. `tp_tables_tight_joiner`
  searches from the end, so a repeated raw key resolves to its last value, as a Go map
  assignment does.
- Local lookup maps (`seen` and `index` in classify, assemble, format and options, and the
  `runIndex` in `buildTables`) are a local `tp_map` whose value is an index into a vector.

## Closures and generics

Go closures become `static` helpers in their `.c` file. Where a closure captures state, pass that
state in a small context struct:

- options.go: `resolveFormatJSON`'s `get`.
- tables.go: `buildTables`' `note(key, patch)`. Make it a helper that returns the `tp_run_info *`
  for a key (creating it in the context struct's keys, index and infos), then set the field at
  the call site.
- mode.go: `splitYoutubePipes`' `pushOther`.
- scanner.go: `scan`'s `offsetAt`. This one is public: `tp_offsets` + `tp_offset_at`, because
  `tp_add_group` takes it.
- title.go: `extractUnbracketedCredits`' `nextGroupAt`.
- format.go: `allArtists`' `add`.
- tl_times.go: `scanTime`'s `followsTime`.
- tl_kinds.go: `markerWords`' `flush`.
- tl_positions.go: `confirmPositions`' `isCandidate` and `adjacent`.
- tl_warnings.go: `tracklistWarnings`' `add`.
- tl_edits.go: `commonEol`'s `isTop`.

Go generics:

- `clamped[T]` becomes `tp_clamped_runes` and `tp_clamped_ints`.
- `byPos[T]` becomes `tp_by_pos_versions`, `_credits`, `_years` and `_junk`. They sort in place
  with `tp_sort_stable`, comparing `pos` as Go's `cmp.Compare` does.
- `phraseTable[V]` values are `const void *`, as in words.h: NULL for `struct{}`,
  `const tp_str *` for string tables and `const tp_run_info *` for `run_words`.
- `Ptr[T]` has no C counterpart.

Package-level `var` slices (`explicitFlags`, `cleanFlags`, `andLike`) are `static const` arrays
in their `.c` file.

## sync.OnceValue

`sharedEngine`, `defaultTables`, `tlHeaderWords`, `tlLayeredWords` and `tlMarkerTable` are
`tp_shared_engine()`, `tp_default_tables()`, `tp_tl_header_words()`, `tp_tl_layered_words()`
and `tp_tl_marker_table()`. Each one uses `tp_once_get`. When that returns NULL (out of memory
while building), build the value into the current arena and return that. Everything the build
function allocates must come from `tp_alloc` inside the build, so it lands in the once's private
arena.

## Edits: typed to loose

Go `ApplyEdits` turns each typed `Edit` into a `jsonValue` through its JSON text
(`jsonOf` = `parseJSON(marshal(e))`). The library has no JSON reader, so `tp_edit_json` builds
the same value directly:

- Keys and order are as in `tp_edit_write`, with only the fields of the op. `lines` is present
  only when `has_lines` is set, and `from` only when it is non-NULL.
- Strings are passed through `tp_sanitize`.
- Integers become `(double)` values.
- A NaN or infinite `seconds` becomes a JSON null (`JSON.stringify` writes null, and the edit
  then becomes `edit-invalid`). A `-0` becomes `+0`, because it is written as `0`.

`tp_apply_edits` (tl_edits.c) takes `const tp_json *edits, size_t count`. The wire door passes
the array's elements, and the typed API passes `tp_edit_json` of each edit. The result is a
`tp_raw_edit_result`. `tp_raw_edit_result_result` types it.

## Calling conventions

- Go passes a struct by value or by pointer, and the C prototype does the same. Results are
  returned by value. The exceptions are heap-like objects (`tp_engine`, `tp_ctx`,
  `tp_credit_lists`, `tp_work_line`, `tp_parser`, `tp_tables`), which come back as `tp_alloc`
  pointers.
- A Go `(T, bool)` return becomes `bool f(..., T *out)`, and `out` is written only on true.
  Functions whose comment says "always written" are the `(A, B)` form: `tp_scan`'s
  `*unbalanced`, `tp_peel_suffixes`' `*peeled` and `tp_split_youtube_pipes`' `*pipe_title`.
- `ctx` is always `tp_ctx *` (mutable: warnings and the list id). Tables are
  `const tp_tables *`. The engine is `const tp_engine *`.
- Offsets in `pos` fields and `skel.pos` are absolute UTF-16 units, as computed by `tp_scan`.
  Indices into `tp_runes` are code points.

## Names that deviate from the plain convention

- `tp_api_*` is used for the trackparse.go functions (`Parse`, `ParseWith`, `ParseArtists`,
  `ParseArtistsWith`, `Format`, `FormatWith`, `Normalize`, `AllArtists`, `ParseTracklist`,
  `ParseTracklistWith`, `ApplyEdits`, `ApplyEditsWith`), because `tp_normalize`,
  `tp_parse_tracklist`, `tp_apply_edits` and `tp_all_artists` are already the internal
  functions. The `Parser` methods keep their names (`tp_new_parser`, `tp_parser_parse`...).
- `tp_edit_set_time`, `tp_edit_shift`, ... `tp_edit_delete` are Go's `SetTime` ... `Delete`.
  rt.h's `tp_insert` macro would swallow a function named `tp_insert`.
- The private `featMode` enum values are `TP_FEAT_MODE_SOURCE` and so on, because
  `TP_FEAT_SOURCE` is the `FeatPlacement` string.
- Fields named `explicit` are `explicit_`, matching the public header (C++ keyword).
- Engine and ctx methods: `tp_engine_new`, `tp_engine_new_ctx`, `tp_engine_stage`,
  `tp_engine_run`, `tp_engine_run_artists`, `tp_engine_probe`, `tp_ctx_warn`, and
  `tp_line_probe_finish` (for `(p *lineProbe) finish`). Package `finish` is `tp_finish`.
- `marshal(v)` is one function per type: `tp_track_json`, `tp_artists_json`,
  `tp_tracklist_json`, `tp_edit_result_json`, `tp_raw_edit_result_json` and `tp_string_json`.
- The credits.go section comes before classify.go, because `tp_classification` holds a
  `tp_list_result` by value.
- C additions with no Go function: `tp_name_index`, `tp_tables_tight_joiner`, `tp_data_strs`,
  `tp_group_map_put` / `_get`, `tp_offsets` / `tp_offset_at`, `tp_sorted_has`, `tp_jw_new`,
  `tp_jw_done`, `tp_jw_opt_int64`, `tp_json_get_string` (Go `getString`) and `tp_edit_json`
  (Go `jsonOf`).

## Recursion

The only recursive function is `tp_json_write`. Its depth is bounded by the reader's
`TP_JSON_MAX_DEPTH` (200) and, for `tp_edit_json` values, by 2.
