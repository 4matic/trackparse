/* test_properties.c: SPEC.md "Invariants" for single tracks and R13.11 for tracklists on
 * generated inputs, and the time ceilings: go/properties_test.go and the ceilings of
 * go/bench_test.go. The generator and the checks are in hardening.c, shared with the fuzz
 * targets. A failure prints the input and aborts; the generator is seeded, so it reproduces.
 *
 *   test_properties [name...]     run the named tests only (substring match)
 *
 * TRACKPARSE_SHORT=1 runs a tenth of the cases (as go test -short), for Valgrind. Builds with
 * sanitizers (TRACKPARSE_SANITIZED) run a quarter of them, as Go under -race, and widen the
 * ceilings ten times; TRACKPARSE_SLOWDOWN=<n> sets that factor by hand.
 */
#include <stdio.h>
#include <stdlib.h>

#include "hardening.h"

/* A tp_str initializer. */
#define L(lit) {(lit), sizeof(lit) - 1}

static int argc_g;
static char **argv_g;
static int ceiling_failures;

static bool selected(const char *name) {
  if (argc_g <= 1) return true;
  for (int i = 1; i < argc_g; i++) {
    if (strstr(name, argv_g[i])) return true;
  }
  return false;
}

/* propertyRuns: a tenth under TRACKPARSE_SHORT, a quarter with sanitizers. */
static int runs(int n) {
  const char *s = getenv("TRACKPARSE_SHORT");
  if (s && s[0]) return n / 10;
#ifdef TRACKPARSE_SANITIZED
  return n / 4;
#else
  return n;
#endif
}

/* One property test: n cases from seed, each in its own scope. */
typedef void (*case_fn)(hz_rng *r, int *counter);

typedef struct prop_arg {
  hz_rng *r;
  int *counter;
  case_fn f;
} prop_arg;

static void prop_tramp(void *p) {
  prop_arg *a = (prop_arg *)p;
  a->f(a->r, a->counter);
}

static int run_prop(const char *name, uint64_t seed, int n, case_fn f) {
  hz_rng r = hz_rng_new(seed);
  int counter = 0;
  prop_arg a;
  double start = hz_now();
  if (!selected(name)) return -1;
  a.r = &r;
  a.counter = &counter;
  a.f = f;
  for (int i = 0; i < n; i++) hz_scoped(prop_tramp, &a);
  printf("%-34s %6d cases  %7.3f s\n", name, n, hz_now() - start);
  fflush(stdout);
  return counter;
}

/* -------------------------------------------------------------------------------------------- */
/* Single tracks                                                                                */

static void totality_case(hz_rng *r, int *c) {
  (void)c;
  tp_str s = hz_any_string(r);
  hz_check_track_totality(s);
  hz_check_public(s);
}

/* The fixed inputs and the tricky fragments, alone and repeated 50 times. */
static void totality_fixed(void *p) {
  static const tp_str fixed[] = {
      L(""),  L(" "),    L("\x00"), L("\xef\xbb\xbf"),     L("-"),        L(" - "), L("("),
      L(")"), L("(((("), L("]]]]"), L("\xf4\x8f\xbf\xbf"), L("\xff\xfe"),
  };
  (void)p;
  for (size_t i = 0; i < TP_COUNT(fixed); i++) {
    hz_check_track_totality(fixed[i]);
    hz_check_public(fixed[i]);
  }
  for (size_t i = 0; i < hz_tricky_fragments_n; i++) {
    tp_str f = hz_tricky_fragments[i];
    hz_check_track_totality(f);
    hz_check_track_totality(tp_repeat(f, 50));
    hz_check_public(tp_repeat(f, 50));
  }
}

static void normalize_case(hz_rng *r, int *c) {
  tp_str s = hz_any_string(r), once = tp_api_normalize(s), twice = tp_api_normalize(once);
  (void)c;
  if (!tp_str_eq(once, twice)) {
    hz_fail("normalize: %s: %s then %s", hz_q(s), hz_q(once), hz_q(twice));
  }
}

static void noise_case(hz_rng *r, int *c) {
  tp_str s = hz_track_string(r);
  (void)c;
  hz_check_noise_insensitive(s, hz_below(r, 1000));
}

static void hygiene_case(hz_rng *r, int *c) {
  tp_str s = hz_track_string(r);
  tp_track t;
  (void)c;
  if (hz_chance(r, 70)) s = hz_any_string(r);
  t = tp_api_parse(s);
  hz_check_artist_hygiene(s, &t.artists);
}

static void conservation_case(hz_rng *r, int *c) {
  (void)c;
  hz_check_conservation(hz_decorated(r));
}

static void metamorphic_case(hz_rng *r, int *c) {
  (void)c;
  hz_check_metamorphic(hz_quiet_track(r));
}

static void round_trip_case(hz_rng *r, int *c) {
  tp_str s = hz_quiet_track(r);
  tp_track t;
  tp_format_options fo;
  (void)c;
  hz_check_round_trip(s);
  t = tp_api_parse(s);
  fo = hz_format_options_from(r, s);
  (void)tp_api_format_with(&t, &fo);
}

/* -------------------------------------------------------------------------------------------- */
/* Tracklists                                                                                   */

static void lossless_case(hz_rng *r, int *c) {
  tp_tracklist_options o;
  tp_str text = hz_any_text(r);
  (void)c;
  memset(&o, 0, sizeof o);
  hz_check_tracklist(text, &o);
  hz_check_public_tracklist(text);
}

static void line_breaks(void *p) {
  static const struct {
    tp_str text;
    size_t want;
  } cases[] = {
      /* U+2028, U+2029, U+0085, VT and FF are whitespace, not line breaks. */
      {L("a\xe2\x80\xa8"
         "b\xe2\x80\xa9"
         "c\xc2\x85"
         "d\x0b"
         "e\x0c"
         "f"),
       1},
      {L("a\r\nb\rc\nd\n"), 4},
      {L("\n\r\r\n"), 3},
      {L(""), 0},
  };
  (void)p;
  for (size_t i = 0; i < TP_COUNT(cases); i++) {
    size_t got = tp_api_parse_tracklist(cases[i].text).entries.n;
    if (got != cases[i].want) {
      hz_fail("%s: %lu lines, want %lu", hz_q(cases[i].text), (unsigned long)got,
              (unsigned long)cases[i].want);
    }
  }
}

static void loose_edits_case(hz_rng *r, int *c) {
  tp_str text = hz_any_text(r);
  tp_json edits = hz_loose_edits(r);
  (void)c;
  hz_check_loose_edits(text, edits);
}

static void identity_case(hz_rng *r, int *c) {
  (void)c;
  hz_check_identity(hz_any_text(r));
}

static void locality_case(hz_rng *r, int *c) {
  tp_str text = hz_timed_list(r);
  (void)c;
  if (hz_chance(r, 20)) text = hz_noisy_text(r);
  hz_check_locality(text, r);
  hz_check_insert_delete(hz_timed_list(r), r);
}

static void shift_case(hz_rng *r, int *c) {
  tp_str text = hz_timed_list(r);
  if (hz_check_shift_round_trip(text, r)) (*c)++;
}

static void sort_case(hz_rng *r, int *c) {
  (void)c;
  hz_check_sort(hz_timed_list(r));
}

static void one_line_case(hz_rng *r, int *c) {
  tp_str text = TP_EMPTY;
  if (hz_chance(r, 50)) text = hz_list_line(r);
  if (hz_check_one_line(tp_concat(text, hz_noisy_text(r)))) (*c)++;
}

static void emphasis_case(hz_rng *r, int *c) {
  tp_str style = hz_time_styles[hz_below(r, (int)hz_time_styles_n)];
  int seconds = hz_below(r, 3600);
  int frac = hz_below(r, 1000);
  tp_str t = hz_write_time(style, seconds, frac);
  (void)c;
  for (size_t i = 0; i < hz_emphasis_rests_n; i++) hz_check_emphasis(t, hz_emphasis_rests[i]);
}

/* -------------------------------------------------------------------------------------------- */
/* Ceilings (go/bench_test.go)                                                                  */

/* block repeated to exactly n code points. */
static tp_str repeated(tp_str block, size_t n) {
  tp_runes rs = tp_runes_of(block);
  tp_rune *out = (tp_rune *)tp_alloc_array(n, sizeof *out);
  tp_runes r;
  for (size_t i = 0; i < n; i++) out[i] = rs.p[i % rs.n];
  r.p = out;
  r.n = n;
  return tp_runes_str(r);
}

static void check_ceiling(const char *what, double d, double ceiling) {
  if (d > ceiling) {
    fprintf(stderr, "FAIL: %s: %.3f s (ceiling %.1f s)\n", what, d, ceiling);
    ceiling_failures++;
  }
}

typedef struct timed_parse {
  const tp_parser *p;
  tp_parse_options *o;
  tp_str input;
  int which; /* 0 parser, 1 parse, 2 parse_artists, 3 parse_with */
} timed_parse;

static void do_timed_parse(void *p) {
  timed_parse *t = (timed_parse *)p;
  switch (t->which) {
  case 0:
    (void)tp_parser_parse(t->p, t->input);
    break;
  case 1:
    (void)tp_api_parse(t->input);
    break;
  case 2:
    (void)tp_api_parse_artists(t->input);
    break;
  default:
    (void)tp_api_parse_with(t->input, t->o);
  }
}

static const tp_str ceiling_blocks[] = {
    L("("),
    L("a - "),
    L("A & B, "),
    L("feat. X "),
    L("[Remix] "),
    L("-a"),
    L(" by x"),
    L("\"q\" "),
    L("xxxxxxx"),
    L("prod. A feat. B "),
    L("a by me "),
    L("(x) - "),
    L("Official Video "),
    L("x - Official Video "),
    L("Remix - "),
    L("a feat. "),
    L("a & "),
    L("(a remix) "),
    L("Noisia feat. Foreign Beggars - Shellshock (Phace Remix) [Official Video] "),
};

static void ceiling_10k_code_points(void *p) {
  static const tp_str modes[] = {L("auto"), L("youtube"), L("filename")};
  double ceiling = 2 * hz_slowdown(), worst = 0;
  (void)p;
  for (size_t b = 0; b < TP_COUNT(ceiling_blocks); b++) {
    tp_str input = repeated(ceiling_blocks[b], 10000);
    for (size_t m = 0; m < TP_COUNT(modes); m++) {
      tp_parse_options o;
      timed_parse t;
      double d;
      memset(&o, 0, sizeof o);
      o.mode = modes[m];
      t.p = tp_new_parser(&o);
      t.o = NULL;
      t.input = input;
      t.which = 0;
      d = hz_best_of3(do_timed_parse, &t);
      if (d > worst) worst = d;
      if (d > ceiling) {
        fprintf(stderr, "FAIL: %s in %s: %.3f s (ceiling %.1f s)\n", hz_q(ceiling_blocks[b]),
                modes[m].p, d, ceiling);
        ceiling_failures++;
      }
    }
  }
  printf("  10,000 code points: slowest %.4f s over %lu blocks x 3 modes\n", worst,
         (unsigned long)TP_COUNT(ceiling_blocks));
}

static void ceiling_feat_credits(void *p) {
  timed_parse t;
  double ceiling = 2 * hz_slowdown(), a, b;
  (void)p;
  /* 1,250 `a feat. ` credits: 10,000 code points that all dedup to one name (R9.2). */
  memset(&t, 0, sizeof t);
  t.input = tp_repeat(TP_S("a feat. "), 1250);
  t.which = 1;
  a = hz_best_of3(do_timed_parse, &t);
  t.which = 2;
  b = hz_best_of3(do_timed_parse, &t);
  printf("  1,250 feat credits: parse %.4f s, parseArtists %.4f s\n", a, b);
  check_ceiling("repeated feat credits: parse", a, ceiling);
  check_ceiling("repeated feat credits: parseArtists", b, ceiling);
}

static void ceiling_trailing_junk(void *p) {
  tp_parse_options o;
  timed_parse t;
  tp_track track;
  double d;
  (void)p;
  memset(&o, 0, sizeof o);
  o.mode = TP_MODE_YOUTUBE;
  memset(&t, 0, sizeof t);
  t.input = tp_concat(TP_S("A - T "), tp_repeat(TP_S("Official Video "), 2666));
  t.o = &o;
  t.which = 3;
  track = tp_api_parse_with(t.input, &o);
  if (!tp_str_eq(track.title, TP_S("T")) || track.junk.n != 2666) {
    hz_fail("trailing junk: title %s, %lu junk", hz_q(track.title), (unsigned long)track.junk.n);
  }
  d = hz_best_of3(do_timed_parse, &t);
  printf("  2,666 trailing junk phrases: %.4f s\n", d);
  check_ceiling("repeated trailing junk", d, 2 * hz_slowdown());
}

static void put2(tp_buf *b, int n) {
  tp_buf_byte(b, (char)('0' + n / 10 % 10));
  tp_buf_byte(b, (char)('0' + n % 10));
}

static void hms(tp_buf *b, int s) {
  put2(b, s / 3600);
  tp_buf_byte(b, ':');
  put2(b, (s % 3600) / 60);
  tp_buf_byte(b, ':');
  put2(b, s % 60);
}

/* The 10,000-line list of the other suites. */
static tp_str ten_thousand_lines(void) {
  tp_buf b = {0};
  for (int i = 0; i < 10000; i++) {
    if (i) tp_buf_cstr(&b, "\r\n");
    hms(&b, (i * 37) % 7200);
    tp_buf_byte(&b, ' ');
    tp_buf_int(&b, i % 100);
    tp_buf_cstr(&b, " Artist ");
    tp_buf_int(&b, i);
    tp_buf_cstr(&b, " & Other - Title ");
    tp_buf_int(&b, i);
    tp_buf_cstr(&b, " (VIP Mix)");
  }
  return tp_buf_done(&b);
}

typedef struct timed_list {
  tp_str text;
  const tp_edit *edits;
  size_t n;
} timed_list;

static void do_parse_list(void *p) { (void)tp_api_parse_tracklist(((timed_list *)p)->text); }

static void do_apply(void *p) {
  timed_list *t = (timed_list *)p;
  (void)tp_api_apply_edits(t->text, t->edits, t->n);
}

static void ceiling_10k_lines(void *p) {
  timed_list t;
  tp_edit edits[2];
  double parse, edit;
  size_t n;
  (void)p;
  t.text = ten_thousand_lines();
  n = tp_api_parse_tracklist(t.text).entries.n;
  if (n != 10000) hz_fail("10,000 lines: %lu entries", (unsigned long)n);
  parse = hz_best_of3(do_parse_list, &t);
  edits[0] = tp_edit_shift(5);
  edits[1] = tp_edit_sort_by_time();
  t.edits = edits;
  t.n = 2;
  edit = hz_best_of3(do_apply, &t);
  printf("  10,000 lines: parseTracklist %.4f s, shift + sort %.4f s\n", parse, edit);
  check_ceiling("parseTracklist on 10,000 lines", parse, 3 * hz_slowdown());
  check_ceiling("applyEdits on 10,000 lines", edit, 1 * hz_slowdown());
}

static void ceiling_2000_edits(void *p) {
  tp_buf b = {0};
  tp_edit *set_times, *inserts;
  timed_list t;
  double set, ins;
  size_t n;
  (void)p;
  /* R13.11.2: ApplyEdits costs lines plus edits, not their product. */
  for (int i = 0; i < 5000; i++) {
    if (i) tp_buf_byte(&b, '\n');
    hms(&b, i * 30);
    tp_buf_cstr(&b, " A ");
    tp_buf_int(&b, i);
    tp_buf_cstr(&b, " - T ");
    tp_buf_int(&b, i);
  }
  t.text = tp_buf_done(&b);
  set_times = (tp_edit *)tp_alloc_array(2000, sizeof *set_times);
  inserts = (tp_edit *)tp_alloc_array(2000, sizeof *inserts);
  for (int k = 0; k < 2000; k++) {
    set_times[k] = tp_edit_set_time((k * 7) % 5000 + 1, (double)(k * 10));
    inserts[k] = tp_edit_insert((k * 7) % 5000, TP_S("00:00 X - Y"));
  }
  n = tp_api_apply_edits(t.text, set_times, 2000).changed.n;
  if (n != 2000) hz_fail("set-time changed %lu lines", (unsigned long)n);
  n = tp_api_apply_edits(t.text, inserts, 2000).changed.n;
  if (n != 2000) hz_fail("insert changed %lu lines", (unsigned long)n);
  t.n = 2000;
  t.edits = set_times;
  set = hz_best_of3(do_apply, &t);
  t.edits = inserts;
  ins = hz_best_of3(do_apply, &t);
  printf("  2,000 edits on 5,000 lines: set-time %.4f s, insert %.4f s\n", set, ins);
  check_ceiling("2,000 set-time edits", set, 1 * hz_slowdown());
  check_ceiling("2,000 insert edits", ins, 1 * hz_slowdown());
}

static void run_fixed(const char *name, hz_fn fn) {
  double start = hz_now();
  if (!selected(name)) return;
  hz_scoped(fn, NULL);
  printf("%-34s %7.3f s\n", name, hz_now() - start);
  fflush(stdout);
}

int main(int argc, char **argv) {
  int n;
  argc_g = argc;
  argv_g = argv;
#ifdef TRACKPARSE_SANITIZED
  printf("sanitizer build: a quarter of the cases, ceilings x%.0f\n", hz_slowdown());
#endif

  run_prop("PropertyTotality", 1, runs(2000), totality_case);
  run_fixed("PropertyTotalityFixed", totality_fixed);
  run_prop("PropertyNormalizeIdempotent", 2, runs(4000), normalize_case);
  run_prop("PropertyNoiseInsensitive", 3, runs(500), noise_case);
  run_prop("PropertyArtistHygiene", 5, runs(3000), hygiene_case);
  run_prop("PropertyConservation", 6, runs(1000), conservation_case);
  run_prop("PropertyMetamorphic", 7, runs(500), metamorphic_case);
  run_prop("PropertyRoundTrip", 11, runs(500), round_trip_case);

  run_prop("PropertyTracklistLossless", 21, runs(3000), lossless_case);
  run_fixed("PropertyOnlyLFCRLFAndCRBreakLines", line_breaks);
  run_prop("PropertyAnyEditListReturns", 22, runs(2000), loose_edits_case);
  run_prop("PropertyNoEditsIsTheIdentity", 23, runs(1000), identity_case);
  run_prop("PropertyEditLocality", 24, runs(1500), locality_case);
  n = run_prop("PropertyShiftRoundTrip", 26, runs(2000), shift_case);
  if (n >= 0 && n < runs(2000) / 20) hz_fail("only %d shift round trips were checked", n);
  run_prop("PropertySort", 27, runs(1500), sort_case);
  n = run_prop("PropertyOneLineTrack", 28, runs(3000), one_line_case);
  if (n >= 0 && n < runs(3000) / 6) hz_fail("only %d one-line tracks were checked", n);
  run_prop("PropertyEmphasis", 29, runs(40), emphasis_case);

  run_fixed("CeilingTenThousandCodePoints", ceiling_10k_code_points);
  run_fixed("CeilingRepeatedFeatCredits", ceiling_feat_credits);
  run_fixed("CeilingRepeatedTrailingJunk", ceiling_trailing_junk);
  run_fixed("CeilingTenThousandLines", ceiling_10k_lines);
  run_fixed("CeilingTwoThousandEdits", ceiling_2000_edits);

  if (ceiling_failures) {
    fprintf(stderr, "%d ceilings exceeded\n", ceiling_failures);
    return 1;
  }
  printf("properties: ok\n");
  return 0;
}
