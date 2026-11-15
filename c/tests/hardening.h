/* hardening.h: what the hardening tests and the fuzz targets share (AGENTS.md, C hazards).
 *
 * - The seeded input generator of go/properties_test.go (mulberry32 over the same fragments),
 *   so a failure is reproducible from its seed and case number.
 * - The SPEC invariant checks ("Invariants" and R13.11) of go/properties_test.go, over the
 *   internal typed API (tp_api_*: Go's exported functions), plus hz_check_public, which holds
 *   the public C API (include/trackparse.h) to the same JSON. test_properties.c runs them on
 *   generated inputs, the fuzz targets on what libFuzzer finds.
 * - A fixture loader (the JSONL case lines compare-ports sends), timers and scopes.
 *
 * Every hz_ function that returns or takes tp_str/tp_json allocates in the current arena, so it
 * runs inside hz_scoped (or another armed scope). A failed check prints what failed and calls
 * abort(): ctest reports it, libFuzzer saves the input. The JSON Schema checks of the Go suite
 * are not repeated here: the C port has no schema validator, and the fixture outputs are
 * validated by scripts/validate_outputs.mjs.
 */
#ifndef TP_HARDENING_H
#define TP_HARDENING_H

#include <stdint.h>

#include "../src/internal.h"

/* -------------------------------------------------------------------------------------------- */
/* Scopes, failures, timing                                                                     */

typedef void (*hz_fn)(void *arg);

/* Run fn(arg) in a fresh arena scope that is freed afterwards; abort() on out of memory. */
void hz_scoped(hz_fn fn, void *arg);

/* Print the message (printf format) and abort(). */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
TP_NORETURN void
hz_fail(const char *fmt, ...);

/* s as a quoted JSON string, for messages (arena). */
const char *hz_q(tp_str s);

/* Seconds since an arbitrary point (timespec_get, C11). */
double hz_now(void);
/* The fastest of three runs of fn(arg), each in its own scope, in seconds. */
double hz_best_of3(hz_fn fn, void *arg);
/* How much slower than an optimized plain build this one is: TRACKPARSE_SLOWDOWN from the
 * environment when set, else 10 when built with sanitizers (TRACKPARSE_SANITIZED), else 1. */
double hz_slowdown(void);

/* -------------------------------------------------------------------------------------------- */
/* Generator (go/properties_test.go)                                                            */

typedef struct hz_rng {
  uint32_t s;
} hz_rng;

hz_rng hz_rng_new(uint64_t seed);
uint32_t hz_next(hz_rng *r);
int hz_below(hz_rng *r, int n);
bool hz_chance(hz_rng *r, int percent);
uint64_t hz_fnv1a(const void *p, size_t n);

extern const tp_str hz_tricky_fragments[];
extern const size_t hz_tricky_fragments_n;
extern const tp_str hz_time_styles[];
extern const size_t hz_time_styles_n;
extern const tp_str hz_emphasis_rests[];
extern const size_t hz_emphasis_rests_n;

tp_str hz_any_string(hz_rng *r);
tp_str hz_track_string(hz_rng *r);
tp_str hz_quiet_track(hz_rng *r);
tp_str hz_decorated(hz_rng *r);
tp_str hz_noisy_text(hz_rng *r);
tp_str hz_list_line(hz_rng *r);
tp_str hz_timed_list(hz_rng *r);
tp_str hz_any_text(hz_rng *r);
tp_str hz_write_time(tp_str style, int seconds, int frac);
tp_json hz_loose_edits(hz_rng *r);
tp_format_options hz_format_options_from(hz_rng *r, tp_str s);

/* The option sets of go/fuzz_test.go. */
#define HZ_FUZZ_PARSE_OPTIONS 7
#define HZ_FUZZ_TRACKLIST_OPTIONS 7
tp_parse_options hz_fuzz_parse_options(int i);
tp_tracklist_options hz_fuzz_tracklist_options(int i);

/* -------------------------------------------------------------------------------------------- */
/* Checks (go/properties_test.go)                                                               */

tp_str hz_track_json(tp_track t); /* without its input */
void hz_check_artist_hygiene(tp_str s, const tp_artist_vec *artists);
void hz_check_track_totality(tp_str s);
/* The public API (trackparse_*) gives the same JSON as the internal one for s. */
void hz_check_public(tp_str s);
void hz_check_noise_insensitive(tp_str s, int cut);
void hz_check_round_trip(tp_str s);
void hz_check_conservation(tp_str s);
void hz_check_metamorphic(tp_str s);
void hz_check_shaped_track(hz_rng *r);
void hz_check_tracklist(tp_str text, const tp_tracklist_options *o);
void hz_check_loose_edits(tp_str text, tp_json edits);
void hz_check_identity(tp_str text);
void hz_check_locality(tp_str text, hz_rng *r);
void hz_check_insert_delete(tp_str text, hz_rng *r);
bool hz_check_shift_round_trip(tp_str text, hz_rng *r);
void hz_check_sort(tp_str text);
bool hz_check_one_line(tp_str text);
void hz_check_emphasis(tp_str t, tp_str rest);
/* The public tracklist and edit functions give the same JSON as the internal ones. */
void hz_check_public_tracklist(tp_str text);

/* -------------------------------------------------------------------------------------------- */
/* Fixture cases                                                                                */

typedef struct hz_case {
  char *id;
  char *fn;    /* parse, parseArtists, format, normalize, parseTracklist, applyEdits */
  char *input; /* NUL-terminated; may hold U+0000, so use input_n */
  size_t input_n;
  char *line; /* the case as one JSONL object {"fn","input","options","formatOptions","edits"},
                 defaults merged (what compare-ports sends to the dump tool) */
  size_t line_n;
  bool has_options; /* options or formatOptions not empty */
} hz_case;

typedef struct hz_cases {
  hz_case *items;
  size_t n;
} hz_cases;

/* Every case of the given fixture files, on the heap. Exits 2 when a file cannot be read. */
hz_cases hz_load_cases(int count, char **paths);
void hz_free_cases(hz_cases *c);

#endif /* TP_HARDENING_H */
