/* test_oom.c: allocation failure at every n (PORTING.md hazard 10).
 *
 * A counting allocator (trackparse_set_allocator) fails the n-th allocation. For a set of about
 * 50 fixture inputs and every public function (and the JSON door, which reports out of memory by
 * longjmp to its scope), each call is repeated with n = 1, 2, 3, ... until it succeeds. Each
 * time the call must return NULL (or false) or a result whose JSON equals the reference, and
 * every allocation it made must be freed again (live blocks before == after).
 *
 * The first calls of the process build the default tables once (tp_once_get) into arenas that
 * live forever. A first sweep runs with the tables not built yet ("cold"), so the out-of-memory
 * path of the lazy build is covered too; it checks the results but not the balance, since the
 * tables it builds are meant to stay.
 *
 *   test_oom <fixture.json>...
 */
#include <stdio.h>
#include <stdlib.h>

#include "../include/trackparse.h"
#include "../wire/wire.h"
#include "hardening.h"

/* -------------------------------------------------------------------------------------------- */
/* The counting allocator                                                                       */

static long long alloc_count;  /* allocations since the last reset */
static long long fail_at;      /* fail allocation number fail_at (1-based); 0: never */
static long long live;         /* blocks allocated and not freed */
static long long refused;      /* allocations refused since the last reset */
static long long total_allocs; /* over the whole run */

static void *count_malloc(size_t size, void *user) {
  void *p;
  (void)user;
  alloc_count++;
  total_allocs++;
  if (fail_at > 0 && alloc_count == fail_at) {
    refused++;
    return NULL;
  }
  p = malloc(size);
  if (p) live++;
  return p;
}

static void *count_realloc(void *ptr, size_t size, void *user) {
  void *p;
  (void)user;
  alloc_count++;
  total_allocs++;
  if (fail_at > 0 && alloc_count == fail_at) {
    refused++;
    return NULL;
  }
  p = realloc(ptr, size);
  if (p && !ptr) live++;
  return p;
}

static void count_free(void *ptr, void *user) {
  (void)user;
  if (ptr) {
    live--;
    free(ptr);
  }
}

static void arm(long long n) {
  alloc_count = 0;
  refused = 0;
  fail_at = n;
}

static void disarm(void) { fail_at = 0; }

static int failures;

#define FAILF(...)                                                                                 \
  do {                                                                                             \
    fprintf(stderr, "FAIL: " __VA_ARGS__);                                                         \
    fputc('\n', stderr);                                                                           \
    failures++;                                                                                    \
  } while (0)

/* -------------------------------------------------------------------------------------------- */
/* Operations: one call (or a few) that gives a JSON string or NULL on out of memory            */

typedef struct op {
  const char *name;
  const hz_case *c;
  const char *s; /* the input */
  size_t n;
  const trackparse_track *track; /* a parsed track, for format, all_artists and to_json */
  const trackparse_tracklist *list;
  const trackparse_edit_result *edited;
  char *(*run)(const struct op *o); /* heap JSON (plain malloc, not counted), or NULL */
} op;

/* A result string as a plain heap string (the library's string is freed). */
static char *take(trackparse_string *s) {
  char *p;
  if (!s) return NULL;
  p = (char *)malloc(s->str.len + 1);
  if (!p) exit(2);
  memcpy(p, s->str.ptr, s->str.len + 1);
  trackparse_string_free(s);
  return p;
}

static char *op_parse(const op *o) {
  trackparse_track *t = trackparse_parse(o->s, o->n);
  char *j;
  if (!t) return NULL;
  j = take(trackparse_track_to_json(t));
  trackparse_track_free(t);
  return j;
}

static trackparse_parse_options *build_parse_options(const char *s, size_t n) {
  trackparse_parse_options *po = trackparse_parse_options_new();
  if (!po) return NULL;
  /* The input as uploader and known artist, cut to 60 bytes: the known-artist search window grows
   * with the longest name (2 * longest + 8, R7.3a), and parse_artists on a 10,000 code point
   * input with a 300-byte known artist takes 20 s (JS: 7 s), too slow for a sweep that repeats
   * the call. */
  if (n > 60) n = 60;
  if (!trackparse_parse_options_set_mode(po, TRACKPARSE_MODE_YOUTUBE) ||
      !trackparse_parse_options_set_uploader(po, s, n) ||
      !trackparse_parse_options_add_known_artist(po, "Camo & Krooked", TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_parse_options_add_known_artist(po, s, n) ||
      !trackparse_parse_options_set_split_and(po, TRACKPARSE_SPLIT_AND_ALWAYS) ||
      !trackparse_parse_options_add_version_head(po, "rerub", TRACKPARSE_NUL_TERMINATED, "rework",
                                                 TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_parse_options_add_descriptor(po, "Deluxe", TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_parse_options_add_genre(po, "neurofunk", TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_parse_options_add_junk(po, "out now", TRACKPARSE_NUL_TERMINATED, "promo",
                                         TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_parse_options_add_feat_marker(po, "w/", TRACKPARSE_NUL_TERMINATED)) {
    trackparse_parse_options_free(po);
    return NULL;
  }
  return po;
}

/* The options are built inside the sweep too: a setter that fails leaves them as they were. */
static char *op_parse_with(const op *o) {
  trackparse_parse_options *po = build_parse_options(o->s, o->n);
  trackparse_track *t;
  char *j;
  if (!po) return NULL;
  t = trackparse_parse_with(o->s, o->n, po);
  trackparse_parse_options_free(po);
  if (!t) return NULL;
  j = take(trackparse_track_to_json(t));
  trackparse_track_free(t);
  return j;
}

static char *op_parser(const op *o) {
  trackparse_parse_options *po = build_parse_options(o->s, o->n);
  trackparse_parser *p;
  trackparse_track *t;
  trackparse_artists *a;
  char *j, *k;
  if (!po) return NULL;
  p = trackparse_parser_new(po);
  trackparse_parse_options_free(po);
  if (!p) return NULL;
  t = trackparse_parser_parse(p, o->s, o->n);
  a = trackparse_parser_parse_artists(p, o->s, o->n);
  trackparse_parser_free(p);
  if (!t || !a) {
    trackparse_track_free(t);
    trackparse_artists_free(a);
    return NULL;
  }
  j = take(trackparse_track_to_json(t));
  k = take(trackparse_artists_to_json(a));
  trackparse_track_free(t);
  trackparse_artists_free(a);
  if (!j || !k) {
    free(j);
    free(k);
    return NULL;
  }
  {
    size_t jn = strlen(j), kn = strlen(k);
    char *both = (char *)malloc(jn + kn + 2);
    if (!both) exit(2);
    memcpy(both, j, jn);
    both[jn] = '\n';
    memcpy(both + jn + 1, k, kn + 1);
    free(j);
    free(k);
    return both;
  }
}

static char *op_parse_artists(const op *o) {
  trackparse_artists *a = trackparse_parse_artists(o->s, o->n);
  char *j;
  if (!a) return NULL;
  j = take(trackparse_artists_to_json(a));
  trackparse_artists_free(a);
  return j;
}

static char *op_normalize(const op *o) { return take(trackparse_normalize(o->s, o->n)); }

static char *op_format(const op *o) { return take(trackparse_format(o->track)); }

static char *op_format_with(const op *o) {
  trackparse_format_options *fo = trackparse_format_options_new();
  char *j;
  if (!fo) return NULL;
  if (!trackparse_format_options_set_feat(fo, TRACKPARSE_FEAT_TITLE) ||
      !trackparse_format_options_set_feat_marker(fo, "ft.", TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_format_options_set_joiners(fo, TRACKPARSE_JOINERS_CANONICAL) ||
      !trackparse_format_options_set_versions(fo, TRACKPARSE_VERSIONS_ALL) ||
      !trackparse_format_options_set_omit_producers(fo, true) ||
      !trackparse_format_options_set_position(fo, true) ||
      !trackparse_format_options_set_omit_label(fo, true)) {
    trackparse_format_options_free(fo);
    return NULL;
  }
  j = take(trackparse_format_with(o->track, fo));
  trackparse_format_options_free(fo);
  return j;
}

static char *op_all_artists(const op *o) {
  trackparse_artists *a = trackparse_all_artists(o->track);
  char *j;
  if (!a) return NULL;
  j = take(trackparse_artists_to_json(a));
  trackparse_artists_free(a);
  return j;
}

static char *op_track_json(const op *o) { return take(trackparse_track_to_json(o->track)); }

static char *op_tracklist(const op *o) {
  trackparse_tracklist *l = trackparse_parse_tracklist(o->s, o->n);
  char *j;
  if (!l) return NULL;
  j = take(trackparse_tracklist_to_json(l));
  trackparse_tracklist_free(l);
  return j;
}

static char *op_tracklist_with(const op *o) {
  trackparse_tracklist_options *lo = trackparse_tracklist_options_new();
  trackparse_tracklist *l;
  char *j;
  if (!lo) return NULL;
  if (!trackparse_tracklist_options_set_mode(lo, TRACKPARSE_MODE_AUTO) ||
      !trackparse_tracklist_options_add_known_artist(lo, "Camo & Krooked", 14) ||
      !trackparse_tracklist_options_set_split_and(lo, TRACKPARSE_SPLIT_AND_NEVER) ||
      !trackparse_tracklist_options_add_version_head(lo, "rerub", TRACKPARSE_NUL_TERMINATED,
                                                     "remix", TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_tracklist_options_add_descriptor(lo, "Deluxe", TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_tracklist_options_add_genre(lo, "neurofunk", TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_tracklist_options_add_junk(lo, "out now", TRACKPARSE_NUL_TERMINATED, "promo",
                                             TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_tracklist_options_add_feat_marker(lo, "w/", TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_tracklist_options_set_audio_seconds(lo, 600.5) ||
      !trackparse_tracklist_options_add_marker_word(lo, "Break", TRACKPARSE_NUL_TERMINATED)) {
    trackparse_tracklist_options_free(lo);
    return NULL;
  }
  l = trackparse_parse_tracklist_with(o->s, o->n, lo);
  trackparse_tracklist_options_free(lo);
  if (!l) return NULL;
  j = take(trackparse_tracklist_to_json(l));
  trackparse_tracklist_free(l);
  return j;
}

static char *op_tracklist_json(const op *o) { return take(trackparse_tracklist_to_json(o->list)); }

static char *op_apply_edits(const op *o) {
  static const long long lines[] = {1, 3};
  trackparse_edits *e = trackparse_edits_new();
  trackparse_edit_result *r;
  char *j;
  if (!e) return NULL;
  if (!trackparse_edits_set_time(e, 2, 62.5) || !trackparse_edits_shift(e, 1.25) ||
      !trackparse_edits_shift_from(e, -3, 2) || !trackparse_edits_shift_lines(e, 7, lines, 2) ||
      !trackparse_edits_sort_by_time(e) || !trackparse_edits_set_rest(e, 1, "A - B", 5) ||
      !trackparse_edits_insert(e, 0, "00:00 Intro", TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_edits_delete(e, 4) || !trackparse_edits_set_time(e, 99, 1)) {
    trackparse_edits_free(e);
    return NULL;
  }
  r = trackparse_apply_edits(o->s, o->n, e);
  trackparse_edits_free(e);
  if (!r) return NULL;
  j = take(trackparse_edit_result_to_json(r));
  trackparse_edit_result_free(r);
  return j;
}

static char *op_edit_json(const op *o) { return take(trackparse_edit_result_to_json(o->edited)); }

/* The JSON door: out of memory longjmps to this scope. The body is a function of its own so the
 * frame that calls setjmp holds nothing a longjmp could clobber. */
static char *wire_body(const hz_case *c) {
  tp_str line, out;
  char *p;
  line.p = c->line;
  line.n = c->line_n;
  out = tp_wire_line(line);
  p = (char *)malloc(out.n + 1); /* plain malloc: not counted */
  if (!p) exit(2);
  memcpy(p, out.p, out.n);
  p[out.n] = '\0';
  return p;
}

static char *op_wire(const op *o) {
  tp_scope s;
  char *volatile result = NULL;
  if (!tp_scope_open(&s)) return NULL;
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    free(result);
    return NULL;
  }
  tp_scope_arm(&s);
  result = wire_body(o->c);
  tp_scope_close(&s);
  tp_arena_free(s.arena);
  return result;
}

/* -------------------------------------------------------------------------------------------- */
/* The sweep                                                                                    */

static long long sweeps, steps, successes_after_refusal;
static bool warm_only; /* the warm-up pass: run each operation once, no sweep */

/* Repeat o with allocation n failing, n = 1, 2, ..., until it succeeds; check each outcome. */
static void sweep(const op *o, bool check_balance) {
  char *want;
  long long n;
  disarm();
  want = o->run(o);
  if (!want) {
    FAILF("%s(%s): no reference result", o->name, o->s ? o->s : "");
    return;
  }
  if (warm_only) {
    free(want);
    return;
  }
  sweeps++;
  for (n = 1; n < 1000000; n++) {
    long long before = live;
    char *got;
    arm(n);
    got = o->run(o);
    disarm();
    steps++;
    if (check_balance && live != before) {
      FAILF("%s(%.60s): allocation %lld failed: %lld blocks leaked", o->name, o->s ? o->s : "", n,
            live - before);
    }
    if (got == NULL) {
      if (refused == 0) FAILF("%s(%.60s): NULL although no allocation failed", o->name, o->s);
      continue;
    }
    if (strcmp(got, want) != 0) {
      FAILF("%s(%.60s): allocation %lld failed: wrong result\n  got  %.300s\n  want %.300s",
            o->name, o->s ? o->s : "", n, got, want);
    }
    if (refused > 0) successes_after_refusal++;
    free(got);
    if (refused == 0) break; /* the call made fewer than n allocations: done */
  }
  free(want);
  if (getenv("TRACKPARSE_OOM_VERBOSE")) {
    printf("  %-22s %6lld steps  %.60s\n", o->name, n, o->s ? o->s : "");
    fflush(stdout);
  }
}

/* A track, tracklist or edit result for the to_json and format operations, built without
 * failures. */
static trackparse_track *parsed(const char *s, size_t n) {
  trackparse_track *t;
  disarm();
  t = trackparse_parse(s, n);
  if (!t) exit(2);
  return t;
}

static void sweep_input(const char *fn, const hz_case *c, const char *s, size_t n) {
  op o;
  memset(&o, 0, sizeof o);
  o.c = c;
  o.s = s;
  o.n = n;
  if (c) {
    o.name = "wire";
    o.run = op_wire;
    sweep(&o, true);
  }
  if (strcmp(fn, "parse") == 0 || strcmp(fn, "format") == 0) {
    trackparse_track *t = parsed(s, n);
    o.track = t;
    o.name = "parse";
    o.run = op_parse;
    sweep(&o, true);
    o.name = "parse_with";
    o.run = op_parse_with;
    sweep(&o, true);
    o.name = "parser";
    o.run = op_parser;
    sweep(&o, true);
    o.name = "format";
    o.run = op_format;
    sweep(&o, true);
    o.name = "format_with";
    o.run = op_format_with;
    sweep(&o, true);
    o.name = "all_artists";
    o.run = op_all_artists;
    sweep(&o, true);
    o.name = "track_to_json";
    o.run = op_track_json;
    sweep(&o, true);
    trackparse_track_free(t);
  } else if (strcmp(fn, "parseArtists") == 0) {
    o.name = "parse_artists";
    o.run = op_parse_artists;
    sweep(&o, true);
  } else if (strcmp(fn, "normalize") == 0) {
    o.name = "normalize";
    o.run = op_normalize;
    sweep(&o, true);
  } else if (strcmp(fn, "parseTracklist") == 0 || strcmp(fn, "applyEdits") == 0) {
    trackparse_tracklist *l;
    trackparse_edit_result *r;
    trackparse_edits *e;
    disarm();
    l = trackparse_parse_tracklist(s, n);
    e = trackparse_edits_new();
    if (!l || !e || !trackparse_edits_shift(e, 5) || !trackparse_edits_sort_by_time(e)) exit(2);
    r = trackparse_apply_edits(s, n, e);
    if (!r) exit(2);
    o.list = l;
    o.edited = r;
    o.name = "parse_tracklist";
    o.run = op_tracklist;
    sweep(&o, true);
    o.name = "parse_tracklist_with";
    o.run = op_tracklist_with;
    sweep(&o, true);
    o.name = "tracklist_to_json";
    o.run = op_tracklist_json;
    sweep(&o, true);
    o.name = "apply_edits";
    o.run = op_apply_edits;
    sweep(&o, true);
    o.name = "edit_result_to_json";
    o.run = op_edit_json;
    sweep(&o, true);
    trackparse_tracklist_free(l);
    trackparse_edit_result_free(r);
    trackparse_edits_free(e);
  }
}

/* The cold sweep: the default tables are not built yet. */
static void cold(void) {
  static const char track[] = "Noisia feat. Foreign Beggars - Shellshock (Phace Remix) [HD]";
  static const char list[] = "00:00 Intro\n01:02.5 Noisia - Stigma\n**02:00** Break\n";
  op o;
  memset(&o, 0, sizeof o);
  o.s = track;
  o.n = sizeof track - 1;
  o.name = "cold parse";
  o.run = op_parse;
  /* No reference yet (computing it would build the tables): run the sweep first, then compare
   * the first success with a warm reference. */
  {
    char *first = NULL;
    for (long long n = 1; n < 100000 && !first; n++) {
      arm(n);
      first = op_parse(&o);
      disarm();
    }
    o.s = list;
    o.n = sizeof list - 1;
    {
      char *first_list = NULL;
      for (long long n = 1; n < 100000 && !first_list; n++) {
        arm(n);
        first_list = op_tracklist(&o);
        disarm();
      }
      if (!first || !first_list) {
        FAILF("cold sweep: no success");
      } else {
        char *a, *b;
        o.s = track;
        o.n = sizeof track - 1;
        a = op_parse(&o);
        o.s = list;
        o.n = sizeof list - 1;
        b = op_tracklist(&o);
        if (!a || strcmp(a, first) != 0) FAILF("cold parse: first success differs");
        if (!b || strcmp(b, first_list) != 0) FAILF("cold parse_tracklist: first success differs");
        free(a);
        free(b);
      }
      free(first_list);
    }
    free(first);
  }
}

/* Every k-th case of each function, about 50 in all. */
static const struct {
  const char *fn;
  int count;
} quota[] = {{"parse", 18},    {"format", 6},          {"parseArtists", 6},
             {"normalize", 4}, {"parseTracklist", 10}, {"applyEdits", 8}};

/* Every selected fixture case, then the inputs of this file. */
static void sweep_all(const hz_cases *cases_p, size_t *used_p) {
  const hz_cases cases = *cases_p;
  size_t used = 0;
  for (size_t q = 0; q < TP_COUNT(quota); q++) {
    size_t total = 0, step, taken = 0;
    for (size_t i = 0; i < cases.n; i++) total += strcmp(cases.items[i].fn, quota[q].fn) == 0;
    if (total == 0) continue;
    step = total / (size_t)quota[q].count;
    if (step == 0) step = 1;
    for (size_t i = 0, k = 0; i < cases.n && taken < (size_t)quota[q].count; i++) {
      const hz_case *c = &cases.items[i];
      if (strcmp(c->fn, quota[q].fn) != 0) continue;
      if (k++ % step != 0) continue;
      sweep_input(c->fn, c, c->input, c->input_n);
      taken++;
      used++;
    }
  }
  /* Inputs of their own (also when spec/ is absent), two of them 10,000 code points long, so the
   * arenas grow over many chunks. */
  {
    static const char *const own[] = {
        "",
        "\xff\xfe broken \xed\xa0\x80 UTF-8",
        ("01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) "
         "[Official Video]"),
        /* R8.7: a label and a note after spaced bullets. */
        "Noisia - concussion \xe2\x80\xa2 VISION \xe2\x80\xa2 VISION RADIO RELOAD",
    };
    char *big = (char *)malloc(10001), *list = (char *)malloc(400001);
    size_t ln = 0;
    if (!big || !list) exit(2);
    for (size_t i = 0; i < TP_COUNT(own); i++) {
      sweep_input("parse", NULL, own[i], strlen(own[i]));
      sweep_input("parseTracklist", NULL, own[i], strlen(own[i]));
      used += 2;
    }
    for (size_t i = 0; i < 10000; i++) big[i] = "a feat. B & C (x) - "[i % 20];
    big[10000] = '\0';
    sweep_input("parse", NULL, big, 10000);
    for (int i = 0; i < 2000; i++) {
      int s = i * 37 % 7200;
      ln += (size_t)sprintf(list + ln, "%02d:%02d:%02d Artist %d - Title %d (VIP)\n", s / 3600,
                            s % 3600 / 60, s % 60, i, i);
    }
    sweep_input("parseTracklist", NULL, list, ln);
    used += 2;
    free(big);
    free(list);
  }
  *used_p = used;
}

int main(int argc, char **argv) {
  trackparse_allocator a;
  hz_cases cases;
  double start = hz_now();
  long long baseline = 0;
  size_t used = 0;
  a.malloc_fn = count_malloc;
  a.realloc_fn = count_realloc;
  a.free_fn = count_free;
  a.user = NULL;
  /* Before any other call: the library's lazily built tables come from this allocator too. */
  trackparse_set_allocator(&a);
  cold();

  cases = hz_load_cases(argc - 1, argv + 1);
  /* Two passes over the same inputs: the first only runs each operation once, so every table
   * built lazily on some path exists before the baseline is taken; the second sweeps. */
  for (int pass = 0; pass < 2; pass++) {
    warm_only = pass == 0;
    if (pass == 1) baseline = live;
    used = 0;
    sweep_all(&cases, &used);
  }
  hz_free_cases(&cases);
  disarm();
  if (live > baseline) FAILF("%lld blocks still allocated at the end", live - baseline);
  printf("oom: %lu inputs, %lld sweeps, %lld failing calls, %lld allocations, %lld successes "
         "after a refused allocation, %.2f s\n",
         (unsigned long)used, sweeps, steps, total_allocs, successes_after_refusal,
         hz_now() - start);
  if (failures) {
    fprintf(stderr, "oom: %d failures\n", failures);
    return 1;
  }
  printf("oom: ok\n");
  return 0;
}
