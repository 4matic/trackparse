/* test_threads.c: concurrent use, as go/concurrency_test.go. A shared trackparse_parser with
 * options, a parser without options, the free functions, parse_tracklist_with, apply_edits and
 * the JSON door, called from 16 threads at once, must give the same bytes as one thread calling
 * them in turn. Build with -DTRACKPARSE_SANITIZE_THREAD=ON to have ThreadSanitizer report any
 * race on shared state (the lazily built default tables, the allocator hook).
 *
 *   test_threads <fixture.json>...
 *
 * The inputs are the parse and parseArtists fixture inputs (without spec/, a few of its own),
 * and the JSON door runs every fixture case line. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "../include/trackparse.h"
#include "../wire/wire.h"
#include "hardening.h"

#define WORKERS 16
#define CALLS 9

typedef struct item {
  const char *s;
  size_t n;
} item;

static item *inputs;
static size_t inputs_n;
static hz_cases cases;
static trackparse_parser *parser, *zero;
static trackparse_tracklist_options *list_options;
static trackparse_format_options *format_options;
static trackparse_edits *edits;
static const char *list_text;

/* The JSON of every call for input i (NULL where a call does not run for i). */
typedef struct outputs {
  char *out[CALLS];
  size_t len[CALLS];
} outputs;

static outputs *want;

static void keep(outputs *o, int k, trackparse_string *s) {
  if (s == NULL) {
    fprintf(stderr, "out of memory\n");
    exit(2);
  }
  o->len[k] = s->str.len;
  o->out[k] = (char *)malloc(s->str.len + 1);
  if (!o->out[k]) exit(2);
  memcpy(o->out[k], s->str.ptr, s->str.len + 1);
  trackparse_string_free(s);
}

static trackparse_track *must_track(trackparse_track *t) {
  if (!t) {
    fprintf(stderr, "out of memory\n");
    exit(2);
  }
  return t;
}

typedef struct wire_arg {
  const hz_case *c;
  outputs *o;
} wire_arg;

static void wire_body(void *p) {
  wire_arg *a = (wire_arg *)p;
  tp_str line, out;
  line.p = a->c->line;
  line.n = a->c->line_n;
  out = tp_wire_line(line);
  a->o->len[8] = out.n;
  a->o->out[8] = (char *)malloc(out.n + 1);
  if (!a->o->out[8]) exit(2);
  memcpy(a->o->out[8], out.p, out.n);
  a->o->out[8][out.n] = '\0';
}

/* One run of every call for input i. */
static void run(size_t i, outputs *o) {
  const item *in = &inputs[i];
  trackparse_track *a, *b, *c;
  trackparse_artists *artists;
  memset(o, 0, sizeof *o);
  a = must_track(trackparse_parser_parse(parser, in->s, in->n));
  b = must_track(trackparse_parser_parse(zero, in->s, in->n));
  c = must_track(trackparse_parse(in->s, in->n));
  keep(o, 0, trackparse_track_to_json(a));
  keep(o, 1, trackparse_track_to_json(b));
  keep(o, 2, trackparse_track_to_json(c));
  artists = trackparse_parser_parse_artists(parser, in->s, in->n);
  if (!artists) exit(2);
  keep(o, 3, trackparse_artists_to_json(artists));
  trackparse_artists_free(artists);
  keep(o, 4, trackparse_format_with(a, format_options));
  trackparse_track_free(a);
  trackparse_track_free(b);
  trackparse_track_free(c);
  if (i % 10 == 0) {
    size_t tn = strlen(list_text);
    char *text = (char *)malloc(tn + 2 + in->n + 1);
    trackparse_tracklist *l;
    trackparse_edit_result *r;
    if (!text) exit(2);
    memcpy(text, list_text, tn);
    memcpy(text + tn, "\r\n", 2);
    memcpy(text + tn + 2, in->s, in->n);
    l = trackparse_parse_tracklist_with(text, tn + 2 + in->n, list_options);
    if (!l) exit(2);
    keep(o, 5, trackparse_tracklist_to_json(l));
    trackparse_tracklist_free(l);
    text[tn] = '\n';
    r = trackparse_apply_edits(text, tn + 1, edits);
    if (!r) exit(2);
    keep(o, 6, trackparse_edit_result_to_json(r));
    trackparse_edit_result_free(r);
    /* The same text without the input, through the parser-free list function. */
    l = trackparse_parse_tracklist(text, tn);
    if (!l) exit(2);
    keep(o, 7, trackparse_tracklist_to_json(l));
    trackparse_tracklist_free(l);
    free(text);
  }
  if (cases.n > 0) {
    wire_arg w;
    w.c = &cases.items[i % cases.n];
    w.o = o;
    hz_scoped(wire_body, &w);
  }
}

static void free_outputs(outputs *o) {
  for (int k = 0; k < CALLS; k++) free(o->out[k]);
}

typedef struct worker {
  int g;
  int failures;
} worker;

static void work(worker *w) {
  for (size_t k = 0; k < inputs_n && w->failures == 0; k++) {
    /* Each thread starts at its own offset, so different inputs run side by side. */
    size_t i = (k + (size_t)w->g * inputs_n / WORKERS) % inputs_n;
    outputs got;
    run(i, &got);
    for (int c = 0; c < CALLS; c++) {
      if ((got.out[c] == NULL) != (want[i].out[c] == NULL) ||
          (got.out[c] &&
           (got.len[c] != want[i].len[c] || memcmp(got.out[c], want[i].out[c], got.len[c]) != 0))) {
        fprintf(stderr, "thread %d, input %s, call %d: got\n%s\nwant\n%s\n", w->g, inputs[i].s, c,
                got.out[c] ? got.out[c] : "(none)", want[i].out[c] ? want[i].out[c] : "(none)");
        w->failures++;
        break;
      }
    }
    free_outputs(&got);
  }
}

/* The cold start: the first calls of the process, from every thread at once, race to build the
 * default tables (tp_once_get). Each thread keeps its JSON; all must agree. */
static const char cold_list[] = "00:00 Intro\n01:02.5 Noisia - Stigma (VIP)\n**02:00** Break";
static outputs cold[WORKERS];

static void cold_work(worker *w) {
  outputs *o = &cold[w->g];
  trackparse_track *t = must_track(
      trackparse_parse("Noisia feat. Foreign Beggars - Shellshock", TRACKPARSE_NUL_TERMINATED));
  trackparse_tracklist *l = trackparse_parse_tracklist(cold_list, TRACKPARSE_NUL_TERMINATED);
  trackparse_artists *a = trackparse_parse_artists("A & B feat. C", TRACKPARSE_NUL_TERMINATED);
  if (!l || !a) exit(2);
  memset(o, 0, sizeof *o);
  keep(o, 0, trackparse_track_to_json(t));
  keep(o, 1, trackparse_tracklist_to_json(l));
  keep(o, 2, trackparse_artists_to_json(a));
  keep(o, 3, trackparse_normalize("A \xe2\x80\x94 B", TRACKPARSE_NUL_TERMINATED));
  trackparse_track_free(t);
  trackparse_tracklist_free(l);
  trackparse_artists_free(a);
}

#if defined(_WIN32)
static DWORD WINAPI thread_main(LPVOID p) {
  work((worker *)p);
  return 0;
}
static DWORD WINAPI cold_main(LPVOID p) {
  cold_work((worker *)p);
  return 0;
}
typedef HANDLE thread_t;
static bool start_thread(thread_t *t, LPTHREAD_START_ROUTINE f, worker *w) {
  *t = CreateThread(NULL, 0, f, w, 0, NULL);
  return *t != NULL;
}
static void join_thread(thread_t t) {
  WaitForSingleObject(t, INFINITE);
  CloseHandle(t);
}
#else
static void *thread_main(void *p) {
  work((worker *)p);
  return NULL;
}
static void *cold_main(void *p) {
  cold_work((worker *)p);
  return NULL;
}
typedef pthread_t thread_t;
static bool start_thread(thread_t *t, void *(*f)(void *), worker *w) {
  return pthread_create(t, NULL, f, w) == 0;
}
static void join_thread(thread_t t) { pthread_join(t, NULL); }
#endif

static int cold_start(void) {
  thread_t th[WORKERS];
  worker w[WORKERS];
  int failures = 0;
  for (int g = 0; g < WORKERS; g++) {
    w[g].g = g;
    w[g].failures = 0;
    if (!start_thread(&th[g], cold_main, &w[g])) exit(2);
  }
  for (int g = 0; g < WORKERS; g++) join_thread(th[g]);
  for (int g = 1; g < WORKERS; g++) {
    for (int k = 0; k < 4; k++) {
      if (strcmp(cold[g].out[k], cold[0].out[k]) != 0) {
        fprintf(stderr, "cold start: thread %d, call %d differs:\n%s\n%s\n", g, k, cold[g].out[k],
                cold[0].out[k]);
        failures++;
      }
    }
  }
  for (int g = 0; g < WORKERS; g++) free_outputs(&cold[g]);
  return failures;
}

static const char *const builtin[] = {
    "Chase & Status x Camo & Krooked - Track (Alix Perez Rerub)",
    "01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]",
    "Noisia - Stigma (VIP",
    "\"Numb\" by Linkin Park",
    "07_Burial_-_Archangel.mp3",
    "Sub Focus, Wilkinson & Dimension feat. Kojo - Out Now (Extended Mix) [HD]",
};

#define NT TRACKPARSE_NUL_TERMINATED

static void setup(void) {
  trackparse_parse_options *o = trackparse_parse_options_new();
  bool ok = o != NULL;
  ok = ok && trackparse_parse_options_add_known_artist(o, "Chase & Status", NT);
  ok = ok && trackparse_parse_options_add_known_artist(o, "Camo & Krooked", NT);
  ok = ok && trackparse_parse_options_add_known_artist(o, "Above & Beyond", NT);
  ok = ok && trackparse_parse_options_set_split_and(o, TRACKPARSE_SPLIT_AND_ALWAYS);
  ok = ok && trackparse_parse_options_add_version_head(o, "rerub", TRACKPARSE_NUL_TERMINATED,
                                                       "rework", TRACKPARSE_NUL_TERMINATED);
  ok = ok && trackparse_parse_options_add_junk(o, "out now", TRACKPARSE_NUL_TERMINATED, "promo",
                                               TRACKPARSE_NUL_TERMINATED);
  ok = ok && trackparse_parse_options_add_feat_marker(o, "w/", TRACKPARSE_NUL_TERMINATED);
  parser = ok ? trackparse_parser_new(o) : NULL;
  zero = trackparse_parser_new(NULL);
  trackparse_parse_options_free(o);

  list_options = trackparse_tracklist_options_new();
  ok = parser && zero && list_options;
  ok = ok && trackparse_tracklist_options_add_known_artist(list_options, "Chase & Status",
                                                           TRACKPARSE_NUL_TERMINATED);
  ok = ok && trackparse_tracklist_options_set_audio_seconds(list_options, 3600);
  ok = ok && trackparse_tracklist_options_add_marker_word(list_options, "drop", NT);

  format_options = trackparse_format_options_new();
  ok = ok && format_options;
  ok = ok && trackparse_format_options_set_feat(format_options, TRACKPARSE_FEAT_TITLE);
  ok = ok && trackparse_format_options_set_feat_marker(format_options, "ft.", NT);

  edits = trackparse_edits_new();
  ok = ok && edits;
  ok = ok && trackparse_edits_set_time(edits, 7, 4262);
  ok = ok && trackparse_edits_shift_from(edits, 45, 2);
  ok = ok && trackparse_edits_sort_by_time(edits);
  ok = ok && trackparse_edits_insert(edits, 0, "00:00 Intro", TRACKPARSE_NUL_TERMINATED);
  ok = ok && trackparse_edits_delete(edits, 4);
  if (!ok) {
    fprintf(stderr, "out of memory in setup\n");
    exit(2);
  }
  list_text =
      "Hour 2\r\n48:20 Noisia - Stigma\r\n52:47 Mefjus & Kasra - Bandwagon (Phace Remix)\r\n"
      "03:00 Drop\r\n59:31 Alix Perez - Fade Away\r\n01:07:44 Chase & Status - Baddadan\r\n"
      "00:11:02 Current Value feat. Teddy Killerz - Payload\r\n1:02:03 Hybris - Spinning Top";
}

int main(int argc, char **argv) {
#ifdef TRACKPARSE_SANITIZED
  size_t limit = 300;
#else
  size_t limit = 2000;
#endif
  worker workers[WORKERS];
  double start;
  int failures = cold_start();
  setup();
  cases = hz_load_cases(argc - 1, argv + 1);
  inputs = (item *)malloc((limit + TP_COUNT(builtin)) * sizeof *inputs);
  if (!inputs) return 2;
  for (size_t i = 0; i < TP_COUNT(builtin); i++) {
    inputs[inputs_n].s = builtin[i];
    inputs[inputs_n++].n = strlen(builtin[i]);
  }
  for (size_t i = 0; i < cases.n && inputs_n < limit; i++) {
    if (strcmp(cases.items[i].fn, "parse") == 0 || strcmp(cases.items[i].fn, "parseArtists") == 0) {
      inputs[inputs_n].s = cases.items[i].input;
      inputs[inputs_n++].n = cases.items[i].input_n;
    }
  }

  start = hz_now();
  want = (outputs *)calloc(inputs_n, sizeof *want);
  if (!want) return 2;
  for (size_t i = 0; i < inputs_n; i++) run(i, &want[i]);
  printf("sequential: %lu inputs, %.3f s\n", (unsigned long)inputs_n, hz_now() - start);

  start = hz_now();
  {
    thread_t th[WORKERS];
    for (int g = 0; g < WORKERS; g++) {
      workers[g].g = g;
      workers[g].failures = 0;
      if (!start_thread(&th[g], thread_main, &workers[g])) return 2;
    }
    for (int g = 0; g < WORKERS; g++) join_thread(th[g]);
  }
  for (int g = 0; g < WORKERS; g++) failures += workers[g].failures;
  printf("%d threads: %lu inputs each, %.3f s\n", WORKERS, (unsigned long)inputs_n,
         hz_now() - start);

  for (size_t i = 0; i < inputs_n; i++) free_outputs(&want[i]);
  free(want);
  free(inputs);
  hz_free_cases(&cases);
  trackparse_parser_free(parser);
  trackparse_parser_free(zero);
  trackparse_tracklist_options_free(list_options);
  trackparse_format_options_free(format_options);
  trackparse_edits_free(edits);
  if (failures) {
    fprintf(stderr, "threads: %d mismatches\n", failures);
    return 1;
  }
  printf("threads: ok\n");
  return 0;
}
