/* test_bench.c: benchmarks, printed like go test -bench (go/bench_test.go). Not a CTest test:
 *
 *   test_bench [-benchtime <seconds>] [--fixtures <fixture.json>...]
 *   TRACKPARSE_BENCH=1 cmake ...  registers it as the CTest test "bench"
 *
 * Each benchmark runs its call N times, doubling N until the run takes -benchtime seconds
 * (default 1), and prints ns/op and allocs/op (allocations from the system allocator: arena
 * chunks and result blocks). Every call goes through the public API, so the numbers include
 * building the result and freeing it. With --fixtures it also reports the mean over every
 * fixture input per function, and the scaling ratios of go's
 * TestRepeatedFragmentsScaleBelowQuadratic (4 times the input should take about 4 times as
 * long, not 16). */
#include <stdio.h>
#include <stdlib.h>

#include "../include/trackparse.h"
#include "hardening.h"

static double benchtime = 1;
static long long allocs;

static void *bench_malloc(size_t size, void *user) {
  (void)user;
  allocs++;
  return malloc(size);
}

static void *bench_realloc(void *p, size_t size, void *user) {
  (void)user;
  allocs++;
  return realloc(p, size);
}

static void bench_free(void *p, void *user) {
  (void)user;
  free(p);
}

typedef void (*bench_fn)(const void *arg);

/* Run f until the run takes benchtime; print the line. */
static void bench(const char *name, bench_fn f, const void *arg) {
  long long n = 1;
  double d = 0;
  long long a = 0;
  for (;;) {
    double start = hz_now();
    allocs = 0;
    for (long long i = 0; i < n; i++) f(arg);
    d = hz_now() - start;
    a = allocs;
    if (d >= benchtime || n >= (1LL << 40)) break;
    if (d < benchtime / 100) {
      n *= 100;
    } else {
      long long next = (long long)((double)n * benchtime * 1.2 / d);
      n = next > n * 2 ? next : n * 2;
    }
  }
  printf("Benchmark%-36s %10lld %14.0f ns/op %8.1f allocs/op\n", name, n, d * 1e9 / (double)n,
         (double)a / (double)n);
  fflush(stdout);
}

typedef struct text {
  const char *s;
  size_t n;
  const trackparse_parser *p;
  const trackparse_edits *e;
} text;

static void b_parse(const void *arg) {
  const text *t = (const text *)arg;
  trackparse_track_free(trackparse_parse(t->s, t->n));
}

static void b_parser(const void *arg) {
  const text *t = (const text *)arg;
  trackparse_track_free(trackparse_parser_parse(t->p, t->s, t->n));
}

static void b_parse_artists(const void *arg) {
  const text *t = (const text *)arg;
  trackparse_artists_free(trackparse_parse_artists(t->s, t->n));
}

static void b_list(const void *arg) {
  const text *t = (const text *)arg;
  trackparse_tracklist_free(trackparse_parse_tracklist(t->s, t->n));
}

static void b_edits(const void *arg) {
  const text *t = (const text *)arg;
  trackparse_edit_result_free(trackparse_apply_edits(t->s, t->n, t->e));
}

static void b_json(const void *arg) {
  const text *t = (const text *)arg;
  trackparse_track *tr = trackparse_parse(t->s, t->n);
  trackparse_string_free(trackparse_track_to_json(tr));
  trackparse_track_free(tr);
}

/* block repeated to n code points, on the heap. */
static char *repeated(const char *block, size_t n, size_t *len) {
  size_t bl = strlen(block), cps = 0, out_n = 0, i = 0;
  char *out = (char *)malloc(n * 4 + 1);
  if (!out) exit(2);
  while (cps < n) {
    unsigned char c = (unsigned char)block[i];
    size_t k = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
    memcpy(out + out_n, block + i, k);
    out_n += k;
    cps++;
    i = (i + k) % bl;
  }
  out[out_n] = '\0';
  *len = out_n;
  return out;
}

static void hms(char *b, int s) {
  b[0] = (char)('0' + s / 36000 % 10);
  b[1] = (char)('0' + s / 3600 % 10);
  b[2] = ':';
  b[3] = (char)('0' + s % 3600 / 600);
  b[4] = (char)('0' + s % 3600 / 60 % 10);
  b[5] = ':';
  b[6] = (char)('0' + s % 60 / 10);
  b[7] = (char)('0' + s % 10);
}

static char *ten_thousand_lines(size_t *len) {
  char *out = (char *)malloc(10000 * 80);
  size_t n = 0;
  if (!out) exit(2);
  for (int i = 0; i < 10000; i++) {
    if (i) {
      out[n++] = '\r';
      out[n++] = '\n';
    }
    hms(out + n, (i * 37) % 7200);
    n += 8;
    n += (size_t)sprintf(out + n, " %d Artist %d & Other - Title %d (VIP Mix)", i % 100, i, i);
  }
  out[n] = '\0';
  *len = n;
  return out;
}

static char *corpus_sized_list(size_t *len) {
  static const char *const artists[] = {"Noisia", "Phace",          "Fox Stevenson", "Sub Focus",
                                        "Mefjus", "Camo & Krooked", "Alix Perez"};
  static const char *const remixers[] = {"Skrillex", "Magnetude", "Wilkinson", "Break"};
  char *out = (char *)malloc(8192);
  size_t n = 0;
  if (!out) exit(2);
  n += (size_t)sprintf(out, "Tracklist:\n00:00 Intro\n");
  for (int i = 1; i <= 40; i++) {
    hms(out + n, i * 150);
    n += 8;
    n += (size_t)sprintf(out + n, " %s feat. %s - Title %d (%s Remix)\n", artists[i % 7],
                         artists[(i + 3) % 7], i, remixers[i % 4]);
  }
  *len = n;
  return out;
}

static double time_once(bench_fn f, const void *arg) {
  double best = 1e300;
  for (int i = 0; i < 3; i++) {
    double start = hz_now(), d;
    f(arg);
    d = hz_now() - start;
    if (d < best) best = d;
  }
  return best;
}

/* Mean ns per call over every fixture input of fn (parse inputs through parse, and so on). */
static void fixture_means(const hz_cases *cases) {
  static const char *const fns[] = {"parse", "parseArtists", "parseTracklist", "applyEdits"};
  trackparse_edits *e = trackparse_edits_new();
  if (!e || !trackparse_edits_shift(e, 5) || !trackparse_edits_sort_by_time(e)) exit(2);
  for (size_t f = 0; f < TP_COUNT(fns); f++) {
    long long calls = 0;
    double start = hz_now(), d;
    int rounds = 0;
    do {
      for (size_t i = 0; i < cases->n; i++) {
        const hz_case *c = &cases->items[i];
        text t;
        if (strcmp(c->fn, fns[f]) != 0) continue;
        t.s = c->input;
        t.n = c->input_n;
        t.p = NULL;
        t.e = e;
        if (f == 0) b_parse(&t);
        if (f == 1) b_parse_artists(&t);
        if (f == 2) b_list(&t);
        if (f == 3) b_edits(&t);
        calls++;
      }
      rounds++;
      d = hz_now() - start;
    } while (d < benchtime && calls > 0);
    if (calls) {
      printf("BenchmarkFixtures/%-27s %10lld %14.0f ns/op   (%d rounds over the %s inputs)\n",
             fns[f], calls, d * 1e9 / (double)calls, rounds, fns[f]);
    }
  }
  trackparse_edits_free(e);
}

int main(int argc, char **argv) {
  static const char typical[] =
      "Noisia feat. Foreign Beggars - Shellshock (Phace Remix) [Official Video]";
  static const struct {
    const char *name, *block;
  } tenk[] = {{"text", "Noisia feat. Foreign Beggars - Shellshock (Phace Remix) [Official Video] "},
              {"feat", "a feat. "},
              {"and", "a & "},
              {"featX", "feat. X "},
              {"remix", "(Remix) "},
              {"hd", "[HD] "},
              {"dash", "- "}};
  trackparse_allocator a;
  trackparse_parse_options *o;
  trackparse_parser *p;
  trackparse_edits *e;
  hz_cases cases = {NULL, 0};
  text t;
  char name[64];
  int files = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-benchtime") == 0 && i + 1 < argc) {
      int v = atoi(argv[++i]);
      if (v > 0) benchtime = v;
    } else if (strcmp(argv[i], "--fixtures") == 0) {
      files = argc - i - 1;
      cases = hz_load_cases(files, argv + i + 1);
      break;
    }
  }
  a.malloc_fn = bench_malloc;
  a.realloc_fn = bench_realloc;
  a.free_fn = bench_free;
  a.user = NULL;
  trackparse_set_allocator(&a);
#ifdef TRACKPARSE_SANITIZED
  printf("warning: built with sanitizers; the numbers are not representative\n");
#endif

  memset(&t, 0, sizeof t);
  t.s = typical;
  t.n = sizeof typical - 1;
  bench("Parse", b_parse, &t);
  bench("ParseToJSON", b_json, &t);
  o = trackparse_parse_options_new();
  if (!o || !trackparse_parse_options_set_mode(o, TRACKPARSE_MODE_YOUTUBE) ||
      !trackparse_parse_options_add_known_artist(o, "Camo & Krooked", TRACKPARSE_NUL_TERMINATED)) {
    return 2;
  }
  p = trackparse_parser_new(o);
  trackparse_parse_options_free(o);
  t.p = p;
  bench("Parser", b_parser, &t);
  bench("ParseArtists", b_parse_artists, &t);

  for (size_t i = 0; i < TP_COUNT(tenk); i++) {
    size_t n;
    char *s = repeated(tenk[i].block, 10000, &n);
    t.s = s;
    t.n = n;
    snprintf(name, sizeof name, "Parse10k/%s", tenk[i].name);
    bench(name, b_parse, &t);
    free(s);
  }

  e = trackparse_edits_new();
  if (!e || !trackparse_edits_shift(e, 5) || !trackparse_edits_sort_by_time(e)) return 2;
  t.e = e;
  {
    size_t n;
    char *s = corpus_sized_list(&n);
    t.s = s;
    t.n = n;
    bench("ParseTracklist/corpus-sized", b_list, &t);
    bench("ApplyEdits/corpus-sized", b_edits, &t);
    free(s);
    s = ten_thousand_lines(&n);
    t.s = s;
    t.n = n;
    bench("ParseTracklist/10k-lines", b_list, &t);
    bench("ApplyEdits/10k-lines", b_edits, &t);
    free(s);
  }

  /* Scaling: 4 times the input should take about 4 times as long. */
  {
    static const char *const blocks[] = {"a feat. ", "A feat. a, ", "(feat. a) ", "a & "};
    for (size_t i = 0; i < TP_COUNT(blocks); i++) {
      size_t sn, ln;
      char *small = repeated(blocks[i], 40000, &sn), *large = repeated(blocks[i], 160000, &ln);
      text ts, tl;
      double credits, track;
      memset(&ts, 0, sizeof ts);
      memset(&tl, 0, sizeof tl);
      ts.s = small;
      ts.n = sn;
      tl.s = large;
      tl.n = ln;
      credits = time_once(b_parse_artists, &tl) / time_once(b_parse_artists, &ts);
      track = time_once(b_parse, &tl) / time_once(b_parse, &ts);
      printf("Scaling %-14s 40k -> 160k code points: parseArtists x%.1f, parse x%.1f\n", blocks[i],
             credits, track);
      free(small);
      free(large);
    }
  }

  if (cases.n) fixture_means(&cases);
  hz_free_cases(&cases);
  trackparse_parser_free(p);
  trackparse_edits_free(e);
  return 0;
}
