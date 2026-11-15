/* test_api.c: the public C API (include/trackparse.h): every public function at least once,
 * NULL options and NULL arguments, explicit lengths with embedded U+0000, invalid UTF-8 (the
 * Parse, Normalize and tracklist parts of go/utf8_test.go), the JSON form against the JSON door
 * (tp_wire_line) on the same input, format on a hand-built track, parser reuse after its options
 * are freed, edits, out of memory at every allocation, and a parser shared between threads. */
#include <stdio.h>
#include <stdlib.h>

#if !defined(_WIN32)
#include <pthread.h>
#define TP_TEST_THREADS 1
#endif

#include "../include/trackparse.h"
#include "../src/internal.h"
#include "../wire/wire.h"

static int failures;
static long checks;

#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond);   \
    }                                                                     \
  } while (0)

/* A trackparse_str equals a C string (no embedded NUL). */
static bool str_is(trackparse_str s, const char *want) {
  return s.ptr != NULL && s.len == strlen(want) && memcmp(s.ptr, want, s.len) == 0 &&
         s.ptr[s.len] == '\0';
}

#define STR_IS(s, lit) CHECK(str_is((s), (lit)))

/* Two results are equal as JSON text. */
static bool same_text(const trackparse_string *a, const trackparse_string *b) {
  return a != NULL && b != NULL && a->str.len == b->str.len &&
         memcmp(a->str.ptr, b->str.ptr, a->str.len) == 0;
}

static void show_diff(const char *what, const trackparse_string *a, const trackparse_string *b) {
  fprintf(stderr, "%s:\n  got  %.*s\n  want %.*s\n", what, a ? (int)a->str.len : 4,
          a ? a->str.ptr : "NULL", b ? (int)b->str.len : 4, b ? b->str.ptr : "NULL");
}

#define SAME_JSON(what, a, b)                 \
  do {                                        \
    checks++;                                 \
    if (!same_text((a), (b))) {               \
      failures++;                             \
      fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
      show_diff((what), (a), (b));            \
    }                                         \
  } while (0)

/* -------------------------------------------------------------------------------------------- */
/* A counting allocator that can fail the n-th malloc                                           */

static long live_blocks;
static long malloc_calls;
static long fail_at = -1; /* fail the malloc with this index, -1: never */

static void *count_malloc(size_t size, void *user) {
  void *p;
  (void)user;
  if (fail_at >= 0 && malloc_calls++ == fail_at) return NULL;
  p = malloc(size);
  if (p != NULL) live_blocks++;
  return p;
}

static void *count_realloc(void *ptr, size_t size, void *user) {
  (void)user;
  return realloc(ptr, size);
}

static void count_free(void *ptr, void *user) {
  (void)user;
  if (ptr != NULL) live_blocks--;
  free(ptr);
}

static void fail_from(long n) {
  malloc_calls = 0;
  fail_at = n;
}

/* -------------------------------------------------------------------------------------------- */
/* The JSON door, for the expected bytes                                                        */

/* tp_wire_line on one case object, copied into a trackparse_string-like malloc block. */
typedef struct wire_out {
  trackparse_string s;
  char *buf;
} wire_out;

static trackparse_string *wire(wire_out *out, const char *case_json) {
  tp_scope s;
  tp_str r;
  if (!tp_scope_open(&s)) return NULL;
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    return NULL;
  }
  tp_scope_arm(&s);
  r = tp_wire_line(tp_str_c(case_json));
  out->buf = (char *)malloc(r.n + 1);
  if (out->buf != NULL) {
    memcpy(out->buf, r.p, r.n);
    out->buf[r.n] = '\0';
  }
  out->s.str.ptr = out->buf;
  out->s.str.len = r.n;
  tp_scope_close(&s);
  tp_arena_free(s.arena);
  return out->buf != NULL ? &out->s : NULL;
}

/* Compare a result's JSON with the door's for the same case, then free both. */
static void check_wire(const char *what, trackparse_string *got, const char *case_json) {
  wire_out w;
  trackparse_string *want = wire(&w, case_json);
  SAME_JSON(what, got, want);
  trackparse_string_free(got);
  free(w.buf);
}

/* The JSON string of a result string (tp_string_json), to compare a normalize result with the
 * door's JSON. */
static trackparse_string *string_json(const trackparse_string *in) {
  tp_scope s;
  tp_str r, x;
  trackparse_string *out = NULL;
  if (!tp_scope_open(&s)) return NULL;
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    return NULL;
  }
  tp_scope_arm(&s);
  x.p = in->str.ptr;
  x.n = in->str.len;
  r = tp_string_json(x);
  tp_scope_close(&s);
  /* A plain malloc copy (free it with free, not trackparse_string_free). */
  out =(trackparse_string *)malloc(sizeof *out + r.n + 1);
  if (out != NULL) {
    char *d = (char *)(out + 1);
    memcpy(d, r.p, r.n);
    d[r.n] = '\0';
    out->str.ptr = d;
    out->str.len = r.n;
  }
  tp_arena_free(s.arena);
  return out;
}

/* -------------------------------------------------------------------------------------------- */

static void test_versions(void) {
  CHECK(strcmp(trackparse_library_version(), TRACKPARSE_VERSION) == 0);
  CHECK(strcmp(trackparse_spec_version(), TP_SPEC_VERSION) == 0);
  CHECK(strcmp(trackparse_library_version(), trackparse_spec_version()) == 0);
}

static void test_enums(void) {
  trackparse_mode m;
  trackparse_version_type vt;
  trackparse_junk_kind jk;
  CHECK(strcmp(trackparse_mode_name(TRACKPARSE_MODE_AUTO), "auto") == 0);
  CHECK(strcmp(trackparse_mode_name(TRACKPARSE_MODE_TRACKLIST), "tracklist") == 0);
  CHECK(strcmp(trackparse_mode_name((trackparse_mode)99), "") == 0);
  CHECK(strcmp(trackparse_mode_name((trackparse_mode)-1), "") == 0);
  CHECK(strcmp(trackparse_role_name(TRACKPARSE_ROLE_PRODUCER), "producer") == 0);
  CHECK(strcmp(trackparse_source_name(TRACKPARSE_SOURCE_VERSION), "version") == 0);
  CHECK(strcmp(trackparse_version_type_name(TRACKPARSE_VERSION_REMIX), "remix") == 0);
  CHECK(strcmp(trackparse_version_type_name(TRACKPARSE_VERSION_SPED_UP), "spedUp") == 0);
  CHECK(strcmp(trackparse_version_type_name(TRACKPARSE_VERSION_NIGHTCORE), "nightcore") == 0);
  CHECK(strcmp(trackparse_delimiter_name(TRACKPARSE_DELIMITER_BRACE), "{") == 0);
  CHECK(strcmp(trackparse_junk_kind_name(TRACKPARSE_JUNK_OTHER), "other") == 0);
  CHECK(strcmp(trackparse_warning_name(TRACKPARSE_WARNING_NO_SEPARATOR), "noSeparator") == 0);
  CHECK(strcmp(trackparse_warning_name(TRACKPARSE_WARNING_UNBALANCED_BRACKETS),
               "unbalancedBrackets") == 0);
  CHECK(strcmp(trackparse_entry_kind_name(TRACKPARSE_ENTRY_BLANK), "blank") == 0);
  CHECK(strcmp(trackparse_eol_name(TRACKPARSE_EOL_CRLF), "\r\n") == 0);
  CHECK(strcmp(trackparse_eol_name(TRACKPARSE_EOL_NONE), "") == 0);
  CHECK(strcmp(trackparse_tracklist_warning_code_name(TRACKPARSE_TL_TIME_OUT_OF_ORDER),
               "time-out-of-order") == 0);
  CHECK(strcmp(trackparse_tracklist_warning_code_name(TRACKPARSE_TL_LINE_UNPARSED),
               "line-unparsed") == 0);
  CHECK(strcmp(trackparse_severity_name(TRACKPARSE_SEVERITY_WARNING), "warning") == 0);
  CHECK(strcmp(trackparse_edit_op_name(TRACKPARSE_EDIT_SORT_BY_TIME), "sort-by-time") == 0);
  CHECK(strcmp(trackparse_edit_warning_code_name(TRACKPARSE_EDIT_WARNING_STYLE_UPGRADED),
               "style-upgraded") == 0);

  CHECK(trackparse_mode_from_name("youtube", TRACKPARSE_NUL_TERMINATED, &m) &&
        m == TRACKPARSE_MODE_YOUTUBE);
  CHECK(trackparse_mode_from_name("auto", 4, &m) && m == TRACKPARSE_MODE_AUTO);
  CHECK(trackparse_mode_from_name("cleanX", 5, &m) && m == TRACKPARSE_MODE_CLEAN);
  CHECK(!trackparse_mode_from_name("Clean", TRACKPARSE_NUL_TERMINATED, &m));
  CHECK(!trackparse_mode_from_name(NULL, 0, &m));
  CHECK(trackparse_version_type_from_name("vip", TRACKPARSE_NUL_TERMINATED, &vt) &&
        vt == TRACKPARSE_VERSION_VIP);
  CHECK(!trackparse_version_type_from_name("rerub", TRACKPARSE_NUL_TERMINATED, &vt));
  CHECK(trackparse_junk_kind_from_name("genre", TRACKPARSE_NUL_TERMINATED, &jk) &&
        jk == TRACKPARSE_JUNK_GENRE);
  CHECK(trackparse_junk_kind_from_name("label", 5, NULL));
}

static void test_parse(void) {
  trackparse_track *t = trackparse_parse("Queen - Bohemian Rhapsody (Live at Wembley 1986)",
                                         TRACKPARSE_NUL_TERMINATED);
  CHECK(t != NULL);
  if (t == NULL) return;
  STR_IS(t->input, "Queen - Bohemian Rhapsody (Live at Wembley 1986)");
  CHECK(t->mode == TRACKPARSE_MODE_CLEAN);
  CHECK(t->position == NULL && t->timestamp == NULL);
  CHECK(t->artists_count == 1);
  if (t->artists_count == 1) {
    STR_IS(t->artists[0].name, "Queen");
    CHECK(t->artists[0].role == TRACKPARSE_ROLE_PRIMARY);
    CHECK(t->artists[0].joiner == NULL);
    CHECK(t->artists[0].source == TRACKPARSE_SOURCE_ARTIST);
  }
  STR_IS(t->title, "Bohemian Rhapsody");
  STR_IS(t->full_title, "Bohemian Rhapsody (Live at Wembley 1986)");
  CHECK(t->versions_count == 1);
  if (t->versions_count == 1) {
    const trackparse_version *v = &t->versions[0];
    CHECK(v->type == TRACKPARSE_VERSION_LIVE);
    STR_IS(v->type_name, "live");
    STR_IS(v->raw, "Live at Wembley 1986");
    CHECK(v->delimiter == TRACKPARSE_DELIMITER_PAREN);
    CHECK(v->descriptor != NULL && str_is(*v->descriptor, "at Wembley"));
    CHECK(v->year != NULL && *v->year == 1986);
    CHECK(v->artists_count == 0 && v->artists == NULL);
  }
  CHECK(t->year == NULL);
  CHECK(t->warnings_count == 0 && t->junk_count == 0);
  trackparse_track_free(t);

  /* Position, timestamp, year, junk, flags. */
  t = trackparse_parse("01. Noisia - Stigma (2015) [HD]", TRACKPARSE_NUL_TERMINATED);
  CHECK(t != NULL);
  if (t != NULL) {
    CHECK(t->position != NULL && t->position->number == 1 && str_is(t->position->raw, "01"));
    CHECK(t->year != NULL && *t->year == 2015);
    CHECK(t->junk_count == 1 && t->junk[0].kind == TRACKPARSE_JUNK_QUALITY &&
          str_is(t->junk[0].kind_name, "quality") && str_is(t->junk[0].raw, "HD"));
    check_wire("parse position", trackparse_track_to_json(t),
               "{\"fn\":\"parse\",\"input\":\"01. Noisia - Stigma (2015) [HD]\"}");
    trackparse_track_free(t);
  }

  /* The JSON form, byte for byte (go/example_test.go ExampleTrack_MarshalJSON). */
  t = trackparse_parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus VIP Remix) [HD]",
                       TRACKPARSE_NUL_TERMINATED);
  CHECK(t != NULL);
  if (t != NULL) {
    trackparse_string *j = trackparse_track_to_json(t);
    CHECK(j != NULL &&
          str_is(j->str,
                 "{\"input\":\"Wilkinson ft. Becky Hill - Afterglow (Sub Focus VIP Remix) [HD]\","
                 "\"mode\":\"youtube\",\"position\":null,\"timestamp\":null,\"artists\":[{\"name\":"
                 "\"Wilkinson\",\"role\":\"primary\",\"joiner\":null,\"source\":\"artist\"},{"
                 "\"name\":\"Becky Hill\",\"role\":\"featured\",\"joiner\":\"ft.\",\"source\":"
                 "\"artist\"}],\"title\":\"Afterglow\",\"fullTitle\":\"Afterglow (Sub Focus VIP "
                 "Remix)\",\"versions\":[{\"type\":\"remix\",\"raw\":\"Sub Focus VIP Remix\","
                 "\"artists\":[{\"name\":\"Sub Focus\",\"role\":\"remixer\",\"joiner\":null,"
                 "\"source\":\"version\"}],\"modifiers\":[\"vip\"],\"descriptor\":null,\"year\":"
                 "null,\"unknownArtist\":false,\"delimiter\":\"(\"}],\"year\":null,\"flags\":{"
                 "\"explicit\":false,\"clean\":false,\"unknownArtist\":false,\"unknownTitle\":"
                 "false},\"junk\":[{\"raw\":\"HD\",\"kind\":\"quality\"}],\"warnings\":[]}"));
    trackparse_string_free(j);
    trackparse_track_free(t);
  }

  /* NULL input is the empty string; the free functions accept NULL. */
  t = trackparse_parse(NULL, 5);
  CHECK(t != NULL && t->input.len == 0 && t->input.ptr != NULL && t->input.ptr[0] == '\0');
  if (t != NULL) {
    check_wire("parse empty", trackparse_track_to_json(t), "{\"fn\":\"parse\",\"input\":\"\"}");
  }
  trackparse_track_free(t);
  trackparse_track_free(NULL);
  trackparse_artists_free(NULL);
  trackparse_string_free(NULL);
  trackparse_tracklist_free(NULL);
  trackparse_edit_result_free(NULL);
  trackparse_parse_options_free(NULL);
  trackparse_format_options_free(NULL);
  trackparse_tracklist_options_free(NULL);
  trackparse_parser_free(NULL);
  trackparse_edits_free(NULL);

  /* An explicit length: a prefix of the buffer. */
  t = trackparse_parse("Noisia - Stigma (VIP) trailing", 21);
  CHECK(t != NULL && str_is(t->input, "Noisia - Stigma (VIP)") && t->versions_count == 1);
  trackparse_track_free(t);
}

static void test_embedded_nul(void) {
  static const char in[] = "A\0B - C\0D";
  trackparse_track *t = trackparse_parse(in, sizeof in - 1);
  trackparse_string *n;
  CHECK(t != NULL);
  if (t != NULL) {
    CHECK(t->input.len == sizeof in - 1 && memcmp(t->input.ptr, in, sizeof in - 1) == 0);
    CHECK(t->input.ptr[t->input.len] == '\0');
    check_wire("parse U+0000", trackparse_track_to_json(t),
               "{\"fn\":\"parse\",\"input\":\"A\\u0000B - C\\u0000D\"}");
    trackparse_track_free(t);
  }
  n = trackparse_normalize(in, sizeof in - 1);
  CHECK(n != NULL);
  if (n != NULL) {
    wire_out w;
    trackparse_string *want =
        wire(&w, "{\"fn\":\"normalize\",\"input\":\"A\\u0000B - C\\u0000D\"}");
    trackparse_string *got = string_json(n);
    SAME_JSON("normalize U+0000", got, want);
    free(w.buf);
    free(got);
    trackparse_string_free(n);
  }
  {
    trackparse_artists *a = trackparse_parse_artists(in, sizeof in - 1);
    CHECK(a != NULL);
    if (a != NULL) {
      check_wire("parseArtists U+0000", trackparse_artists_to_json(a),
                 "{\"fn\":\"parseArtists\",\"input\":\"A\\u0000B - C\\u0000D\"}");
      trackparse_artists_free(a);
    }
  }
}

/* go/utf8_test.go */
typedef struct lossy_case {
  const char *bad;
  const char *want;
} lossy_case;

static const lossy_case lossy_cases[] = {
    {"a\xff" "b", "a\xef\xbf\xbd" "b"},
    {"\xe2\x82", "\xef\xbf\xbd"},
    {"\xe2\x82" "A", "\xef\xbf\xbd" "A"},
    {"\xed\xa0\x80", "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"},
    {"\xf0\x9f\x98", "\xef\xbf\xbd"},
    {"\xf4\x90\x80\x80", "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"},
    {"\xc0\xaf", "\xef\xbf\xbd\xef\xbf\xbd"},
    {"\xe0\x80\xaf", "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"},
    {"\xf0\x8f\xbf\xbf", "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"},
    {"\x80\x80", "\xef\xbf\xbd\xef\xbf\xbd"},
    {"\xf1\x80\x80", "\xef\xbf\xbd"},
    {"\xe1\x80\xe2\x82\xac", "\xef\xbf\xbd\xe2\x82\xac"},
    {"Noisia \xff- Stigma \xf0\x9f\x98(VIP\xed\xbf\xbf)",
     "Noisia \xef\xbf\xbd- Stigma \xef\xbf\xbd(VIP\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd)"},
};

static void test_invalid_utf8(void) {
  size_t i;
  for (i = 0; i < TP_COUNT(lossy_cases); i++) {
    const lossy_case *c = &lossy_cases[i];
    trackparse_string *a, *b, *ja, *jb;
    trackparse_track *ta, *tb;
    trackparse_artists *aa, *ab;
    trackparse_parse_options *ob, *ow;

    a = trackparse_normalize(c->bad, TRACKPARSE_NUL_TERMINATED);
    b = trackparse_normalize(c->want, TRACKPARSE_NUL_TERMINATED);
    SAME_JSON("normalize lossy", a, b);
    trackparse_string_free(a);
    trackparse_string_free(b);

    ta = trackparse_parse(c->bad, TRACKPARSE_NUL_TERMINATED);
    tb = trackparse_parse(c->want, TRACKPARSE_NUL_TERMINATED);
    CHECK(ta != NULL && tb != NULL);
    if (ta != NULL) CHECK(str_is(ta->input, c->want)); /* the input is the lossy string */
    ja = trackparse_track_to_json(ta);
    jb = trackparse_track_to_json(tb);
    SAME_JSON("parse lossy", ja, jb);
    trackparse_string_free(ja);
    trackparse_string_free(jb);
    trackparse_track_free(ta);
    trackparse_track_free(tb);

    aa = trackparse_parse_artists(c->bad, TRACKPARSE_NUL_TERMINATED);
    ab = trackparse_parse_artists(c->want, TRACKPARSE_NUL_TERMINATED);
    ja = trackparse_artists_to_json(aa);
    jb = trackparse_artists_to_json(ab);
    SAME_JSON("parseArtists lossy", ja, jb);
    trackparse_string_free(ja);
    trackparse_string_free(jb);
    trackparse_artists_free(aa);
    trackparse_artists_free(ab);

    /* Bad bytes in the options too. */
    ob = trackparse_parse_options_new();
    ow = trackparse_parse_options_new();
    CHECK(ob != NULL && ow != NULL);
    CHECK(trackparse_parse_options_set_mode(ob, TRACKPARSE_MODE_YOUTUBE));
    CHECK(trackparse_parse_options_set_uploader(ob, c->bad, TRACKPARSE_NUL_TERMINATED));
    CHECK(trackparse_parse_options_add_known_artist(ob, c->bad, TRACKPARSE_NUL_TERMINATED));
    CHECK(trackparse_parse_options_set_mode(ow, TRACKPARSE_MODE_YOUTUBE));
    CHECK(trackparse_parse_options_set_uploader(ow, c->want, TRACKPARSE_NUL_TERMINATED));
    CHECK(trackparse_parse_options_add_known_artist(ow, c->want, TRACKPARSE_NUL_TERMINATED));
    ta = trackparse_parse_with(c->bad, TRACKPARSE_NUL_TERMINATED, ob);
    tb = trackparse_parse_with(c->want, TRACKPARSE_NUL_TERMINATED, ow);
    ja = trackparse_track_to_json(ta);
    jb = trackparse_track_to_json(tb);
    SAME_JSON("parseWith lossy", ja, jb);
    trackparse_string_free(ja);
    trackparse_string_free(jb);
    trackparse_track_free(ta);
    trackparse_track_free(tb);
    trackparse_parse_options_free(ob);
    trackparse_parse_options_free(ow);
  }

  {
    static const char bad[] =
        "00:00 A\xc3 - B\r\n01:00 \xe2\x80\x93 C - D\xf0\n02:00 \xed\xa0\x80 - \xff\n";
    static const char want[] = "00:00 A\xef\xbf\xbd - B\r\n01:00 \xe2\x80\x93 C - "
                               "D\xef\xbf\xbd\n02:00 \xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd - "
                               "\xef\xbf\xbd\n";
    trackparse_tracklist *gl = trackparse_parse_tracklist(bad, TRACKPARSE_NUL_TERMINATED);
    trackparse_tracklist *wl = trackparse_parse_tracklist(want, TRACKPARSE_NUL_TERMINATED);
    trackparse_string *ja = trackparse_tracklist_to_json(gl);
    trackparse_string *jb = trackparse_tracklist_to_json(wl);
    trackparse_edits *eb = trackparse_edits_new(), *ew = trackparse_edits_new();
    trackparse_edit_result *rb, *rw;
    SAME_JSON("parseTracklist lossy", ja, jb);
    CHECK(gl != NULL && gl->entries_count == 3);
    trackparse_string_free(ja);
    trackparse_string_free(jb);
    trackparse_tracklist_free(gl);
    trackparse_tracklist_free(wl);

    CHECK(trackparse_edits_set_rest(eb, 1, "X\xff - Y", TRACKPARSE_NUL_TERMINATED));
    CHECK(trackparse_edits_insert(eb, 3, "03:00 \xe2\x82 - Z", TRACKPARSE_NUL_TERMINATED));
    CHECK(trackparse_edits_shift(eb, 5));
    CHECK(trackparse_edits_set_rest(ew, 1, "X\xef\xbf\xbd - Y", TRACKPARSE_NUL_TERMINATED));
    CHECK(trackparse_edits_insert(ew, 3, "03:00 \xef\xbf\xbd - Z", TRACKPARSE_NUL_TERMINATED));
    CHECK(trackparse_edits_shift(ew, 5));
    rb = trackparse_apply_edits(bad, TRACKPARSE_NUL_TERMINATED, eb);
    rw = trackparse_apply_edits(want, TRACKPARSE_NUL_TERMINATED, ew);
    ja = trackparse_edit_result_to_json(rb);
    jb = trackparse_edit_result_to_json(rw);
    SAME_JSON("applyEdits lossy", ja, jb);
    CHECK(rb != NULL && rw != NULL && rb->text.len == rw->text.len &&
          memcmp(rb->text.ptr, rw->text.ptr, rb->text.len) == 0);
    trackparse_string_free(ja);
    trackparse_string_free(jb);
    trackparse_edit_result_free(rb);
    trackparse_edit_result_free(rw);
    trackparse_edits_free(eb);
    trackparse_edits_free(ew);
  }
}

static void test_parse_artists(void) {
  trackparse_artists *a = trackparse_parse_artists("Sub Focus, Wilkinson & Dimension feat. Kojo",
                                                   TRACKPARSE_NUL_TERMINATED);
  CHECK(a != NULL && a->count == 4);
  if (a != NULL && a->count == 4) {
    STR_IS(a->items[0].name, "Sub Focus");
    CHECK(a->items[0].joiner == NULL);
    STR_IS(a->items[1].name, "Wilkinson");
    CHECK(a->items[1].joiner != NULL && str_is(*a->items[1].joiner, ","));
    CHECK(a->items[2].joiner != NULL && str_is(*a->items[2].joiner, "&"));
    STR_IS(a->items[3].name, "Kojo");
    CHECK(a->items[3].role == TRACKPARSE_ROLE_FEATURED);
    CHECK(a->items[3].joiner != NULL && str_is(*a->items[3].joiner, "feat."));
    check_wire("parseArtists", trackparse_artists_to_json(a),
               "{\"fn\":\"parseArtists\",\"input\":\"Sub Focus, Wilkinson & Dimension feat. "
               "Kojo\"}");
  }
  trackparse_artists_free(a);
}

static void test_parse_with(void) {
  trackparse_parse_options *o = trackparse_parse_options_new();
  trackparse_track *t;
  trackparse_artists *a;
  CHECK(o != NULL);

  /* NULL options are the defaults. */
  t = trackparse_parse_with("Chase & Status - Baddadan", TRACKPARSE_NUL_TERMINATED, NULL);
  CHECK(t != NULL && t->artists_count == 2);
  trackparse_track_free(t);
  a = trackparse_parse_artists_with("Chase & Status", TRACKPARSE_NUL_TERMINATED, NULL);
  CHECK(a != NULL && a->count == 2);
  trackparse_artists_free(a);

  CHECK(trackparse_parse_options_add_known_artist(o, "Chase & Status", TRACKPARSE_NUL_TERMINATED));
  t = trackparse_parse_with("Chase & Status - Baddadan", TRACKPARSE_NUL_TERMINATED, o);
  CHECK(t != NULL && t->artists_count == 1 && str_is(t->artists[0].name, "Chase & Status"));
  if (t != NULL) {
    check_wire("parseWith known", trackparse_track_to_json(t),
               "{\"fn\":\"parse\",\"input\":\"Chase & Status - Baddadan\",\"options\":{"
               "\"knownArtists\":[\"Chase & Status\"]}}");
  }
  trackparse_track_free(t);
  a = trackparse_parse_artists_with("Chase & Status", TRACKPARSE_NUL_TERMINATED, o);
  CHECK(a != NULL && a->count == 1);
  trackparse_artists_free(a);
  trackparse_parse_options_free(o);

  /* Mode, uploader. */
  o = trackparse_parse_options_new();
  CHECK(trackparse_parse_options_set_mode(o, TRACKPARSE_MODE_YOUTUBE));
  t = trackparse_parse_with("\"Numb\" by Linkin Park", TRACKPARSE_NUL_TERMINATED, o);
  CHECK(t != NULL && t->artists_count == 1 && str_is(t->artists[0].name, "Linkin Park") &&
        str_is(t->title, "Numb") && t->warnings_count == 1 &&
        t->warnings[0] == TRACKPARSE_WARNING_BY_SPLIT);
  trackparse_track_free(t);
  CHECK(trackparse_parse_options_set_uploader(o, "Porter Robinson", TRACKPARSE_NUL_TERMINATED));
  t = trackparse_parse_with("Shelter", TRACKPARSE_NUL_TERMINATED, o);
  CHECK(t != NULL && t->artists_count == 1 && str_is(t->artists[0].name, "Porter Robinson") &&
        str_is(t->title, "Shelter"));
  trackparse_track_free(t);
  /* A bad mode value falls back to the default (auto). */
  CHECK(trackparse_parse_options_set_mode(o, (trackparse_mode)42));
  CHECK(trackparse_parse_options_set_split_and(o, (trackparse_split_and)42));
  t = trackparse_parse_with("Shelter", TRACKPARSE_NUL_TERMINATED, o);
  CHECK(t != NULL && t->mode == TRACKPARSE_MODE_CLEAN);
  trackparse_track_free(t);
  trackparse_parse_options_free(o);

  /* Keywords and splitAnd, against the JSON door. */
  o = trackparse_parse_options_new();
  CHECK(trackparse_parse_options_add_junk(o, "some label rip", TRACKPARSE_NUL_TERMINATED,
                                          "label", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_parse_options_add_version_head(o, "rerub", TRACKPARSE_NUL_TERMINATED,
                                                  "rerubtype", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_parse_options_add_descriptor(o, "deluxe", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_parse_options_add_genre(o, "neurofunk", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_parse_options_add_feat_marker(o, "avec", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_parse_options_set_split_and(o, TRACKPARSE_SPLIT_AND_NEVER));
  t = trackparse_parse_with("A and B avec C - Song (D Rerub) [Some Label Rip]",
                            TRACKPARSE_NUL_TERMINATED, o);
  CHECK(t != NULL);
  if (t != NULL) {
    CHECK(t->junk_count == 1 && t->junk[0].kind == TRACKPARSE_JUNK_LABEL);
    /* A type that keywords introduced: VERSION in the enum, the name kept. */
    CHECK(t->versions_count == 1 && t->versions[0].type == TRACKPARSE_VERSION_VERSION &&
          str_is(t->versions[0].type_name, "rerubtype"));
    check_wire("parseWith keywords", trackparse_track_to_json(t),
               "{\"fn\":\"parse\",\"input\":\"A and B avec C - Song (D Rerub) [Some Label Rip]\","
               "\"options\":{\"splitAnd\":\"never\",\"keywords\":{\"junk\":{\"some label rip\":"
               "\"label\"},\"versionHeads\":{\"rerub\":\"rerubtype\"},\"descriptors\":["
               "\"deluxe\"],\"genres\":[\"neurofunk\"],\"featMarkers\":[\"avec\"]}}}");
  }
  trackparse_track_free(t);
  trackparse_parse_options_free(o);
}

static void test_format(void) {
  trackparse_track *t = trackparse_parse(
      "01. Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)", TRACKPARSE_NUL_TERMINATED);
  trackparse_format_options *o = trackparse_format_options_new();
  trackparse_string *s;
  CHECK(t != NULL && o != NULL);
  if (t == NULL || o == NULL) return;
  s = trackparse_format(t);
  CHECK(s != NULL && str_is(s->str, "Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)"));
  trackparse_string_free(s);
  s = trackparse_format_with(t, NULL);
  CHECK(s != NULL && str_is(s->str, "Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)"));
  trackparse_string_free(s);
  CHECK(trackparse_format_options_set_feat(o, TRACKPARSE_FEAT_TITLE));
  CHECK(trackparse_format_options_set_joiners(o, TRACKPARSE_JOINERS_CANONICAL));
  s = trackparse_format_with(t, o);
  CHECK(s != NULL && str_is(s->str, "Wilkinson - Afterglow (feat. Becky Hill) (Sub Focus Remix)"));
  trackparse_string_free(s);
  trackparse_format_options_free(o);

  o = trackparse_format_options_new();
  CHECK(trackparse_format_options_set_feat(o, TRACKPARSE_FEAT_OMIT));
  CHECK(trackparse_format_options_set_versions(o, TRACKPARSE_VERSIONS_NONE));
  s = trackparse_format_with(t, o);
  CHECK(s != NULL && str_is(s->str, "Wilkinson - Afterglow"));
  trackparse_string_free(s);
  trackparse_format_options_free(o);

  o = trackparse_format_options_new();
  CHECK(trackparse_format_options_set_position(o, true));
  CHECK(trackparse_format_options_set_omit_producers(o, true));
  s = trackparse_format_with(t, o);
  CHECK(s != NULL &&
        str_is(s->str, "01. Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)"));
  trackparse_string_free(s);
  CHECK(trackparse_format_options_set_feat_marker(o, "with", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_format_options_set_position(o, false));
  s = trackparse_format_with(t, o);
  CHECK(s != NULL && str_is(s->str, "Wilkinson with Becky Hill - Afterglow (Sub Focus Remix)"));
  trackparse_string_free(s);
  /* "" is a valid marker; compare with the JSON door. */
  CHECK(trackparse_format_options_set_feat_marker(o, "", 0));
  s = trackparse_format_with(t, o);
  CHECK(s != NULL);
  if (s != NULL) {
    wire_out w;
    trackparse_string *want = wire(&w, "{\"fn\":\"format\",\"input\":\"01. Wilkinson ft. Becky "
                                       "Hill - Afterglow (Sub Focus Remix)\",\"formatOptions\":{"
                                       "\"featMarker\":\"\",\"producers\":false}}");
    trackparse_string *got = string_json(s);
    SAME_JSON("format featMarker \"\"", got, want);
    free(w.buf);
    free(got);
    trackparse_string_free(s);
  }
  trackparse_format_options_free(o);
  trackparse_track_free(t);
}

static void test_hand_built(void) {
  trackparse_artist artist;
  trackparse_version version;
  trackparse_track t;
  trackparse_string *s, *j;
  trackparse_artists *all;
  memset(&artist, 0, sizeof artist);
  memset(&version, 0, sizeof version);
  memset(&t, 0, sizeof t);
  artist.name.ptr = "Noisia";
  artist.name.len = 6;
  artist.role = TRACKPARSE_ROLE_PRIMARY;
  artist.source = TRACKPARSE_SOURCE_ARTIST;
  version.type = TRACKPARSE_VERSION_VIP; /* type_name left unset: the enum's name is used */
  version.raw.ptr = "VIP";
  version.raw.len = 3;
  version.delimiter = TRACKPARSE_DELIMITER_PAREN;
  version.modifiers_count = 3; /* with a NULL pointer: no modifiers */
  t.input.ptr = "Noisia - Stigma (VIP)";
  t.input.len = 21;
  t.mode = TRACKPARSE_MODE_CLEAN;
  t.artists = &artist;
  t.artists_count = 1;
  t.title.ptr = "Stigma";
  t.title.len = 6;
  t.full_title.ptr = "Stigma (VIP)";
  t.full_title.len = 12;
  t.versions = &version;
  t.versions_count = 1;
  t.junk_count = 7; /* NULL junk */
  t.warnings_count = 2; /* NULL warnings */

  s = trackparse_format(&t);
  CHECK(s != NULL && str_is(s->str, "Noisia - Stigma (VIP)"));
  trackparse_string_free(s);

  j = trackparse_track_to_json(&t);
  check_wire("hand-built to_json", j, "{\"fn\":\"parse\",\"input\":\"Noisia - Stigma (VIP)\"}");

  all = trackparse_all_artists(&t);
  CHECK(all != NULL && all->count == 1 && str_is(all->items[0].name, "Noisia"));
  trackparse_artists_free(all);

  /* An all-zero track and a NULL track format as "" (Go Format(Track{})). */
  memset(&t, 0, sizeof t);
  s = trackparse_format(&t);
  CHECK(s != NULL && s->str.len == 0 && s->str.ptr != NULL);
  trackparse_string_free(s);
  s = trackparse_format(NULL);
  CHECK(s != NULL && s->str.len == 0);
  trackparse_string_free(s);
  all = trackparse_all_artists(NULL);
  CHECK(all != NULL && all->count == 0);
  trackparse_artists_free(all);
  j = trackparse_track_to_json(&t);
  CHECK(j != NULL && strstr(j->str.ptr, "\"mode\":\"auto\"") != NULL &&
        strstr(j->str.ptr, "\"artists\":[]") != NULL);
  trackparse_string_free(j);
  t.mode = (trackparse_mode)77;
  j = trackparse_track_to_json(&t);
  CHECK(j != NULL && strstr(j->str.ptr, "\"mode\":\"\"") != NULL);
  trackparse_string_free(j);
  j = trackparse_track_to_json(NULL);
  CHECK(j != NULL && str_is(j->str, "null"));
  trackparse_string_free(j);
  j = trackparse_artists_to_json(NULL);
  CHECK(j != NULL && str_is(j->str, "null"));
  trackparse_string_free(j);
  j = trackparse_tracklist_to_json(NULL);
  CHECK(j != NULL && str_is(j->str, "null"));
  trackparse_string_free(j);
  j = trackparse_edit_result_to_json(NULL);
  CHECK(j != NULL && str_is(j->str, "null"));
  trackparse_string_free(j);
}

static void test_normalize_and_all_artists(void) {
  trackparse_string *s = trackparse_normalize("Noisia\xe2\x80\x8b \xe2\x80\x93 Stigma \xef\xbc\x88VIP\xef\xbc\x89",
                                              TRACKPARSE_NUL_TERMINATED);
  trackparse_track *t;
  trackparse_artists *a;
  CHECK(s != NULL && str_is(s->str, "Noisia - Stigma (VIP)"));
  trackparse_string_free(s);
  s = trackparse_normalize(NULL, 0);
  CHECK(s != NULL && s->str.len == 0);
  trackparse_string_free(s);

  t = trackparse_parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)",
                       TRACKPARSE_NUL_TERMINATED);
  a = trackparse_all_artists(t);
  CHECK(a != NULL && a->count == 3);
  if (a != NULL && a->count == 3) {
    CHECK(str_is(a->items[0].name, "Wilkinson") && a->items[0].role == TRACKPARSE_ROLE_PRIMARY);
    CHECK(str_is(a->items[1].name, "Becky Hill") && a->items[1].role == TRACKPARSE_ROLE_FEATURED);
    CHECK(str_is(a->items[2].name, "Sub Focus") && a->items[2].role == TRACKPARSE_ROLE_REMIXER);
  }
  trackparse_artists_free(a);
  trackparse_track_free(t);
}

static const char *const parser_inputs[] = {
    "Chase & Status x Camo & Krooked - Track (Alix Perez Rerub)",
    "Chase & Status - Baddadan",
    "Camo & Krooked feat. Chase & Status - Something (Rerub)",
};

static void test_parser(void) {
  trackparse_parse_options *o = trackparse_parse_options_new();
  trackparse_parser *p, *plain;
  trackparse_track *t;
  trackparse_artists *a;
  trackparse_string *want[TP_COUNT(parser_inputs)];
  size_t i;
  int round;
  CHECK(trackparse_parse_options_add_known_artist(o, "Chase & Status", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_parse_options_add_known_artist(o, "Camo & Krooked", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_parse_options_add_version_head(o, "rerub", TRACKPARSE_NUL_TERMINATED, "rework",
                                                  TRACKPARSE_NUL_TERMINATED));
  for (i = 0; i < TP_COUNT(parser_inputs); i++) {
    t = trackparse_parse_with(parser_inputs[i], TRACKPARSE_NUL_TERMINATED, o);
    want[i] = trackparse_track_to_json(t);
    trackparse_track_free(t);
  }
  p = trackparse_parser_new(o);
  CHECK(p != NULL);
  /* The parser keeps its own copies: freeing (and scribbling over) the options is harmless. */
  trackparse_parse_options_free(o);
  for (round = 0; round < 3; round++) {
    for (i = 0; i < TP_COUNT(parser_inputs); i++) {
      trackparse_string *got;
      t = trackparse_parser_parse(p, parser_inputs[i], TRACKPARSE_NUL_TERMINATED);
      got = trackparse_track_to_json(t);
      SAME_JSON("parser reuse", got, want[i]);
      trackparse_string_free(got);
      trackparse_track_free(t);
    }
  }
  t = trackparse_parser_parse(p, parser_inputs[0], TRACKPARSE_NUL_TERMINATED);
  CHECK(t != NULL && t->artists_count == 2 && str_is(t->artists[0].name, "Chase & Status") &&
        str_is(t->artists[1].name, "Camo & Krooked") && t->versions_count == 1 &&
        t->versions[0].type == TRACKPARSE_VERSION_REWORK);
  trackparse_track_free(t);
  a = trackparse_parser_parse_artists(p, "Chase & Status, Camo & Krooked", TRACKPARSE_NUL_TERMINATED);
  CHECK(a != NULL && a->count == 2);
  trackparse_artists_free(a);
  for (i = 0; i < TP_COUNT(parser_inputs); i++) trackparse_string_free(want[i]);
  trackparse_parser_free(p);

  /* A parser without options, and a NULL parser, use the defaults. */
  plain = trackparse_parser_new(NULL);
  CHECK(plain != NULL);
  t = trackparse_parser_parse(plain, "Chase & Status - Baddadan", TRACKPARSE_NUL_TERMINATED);
  CHECK(t != NULL && t->artists_count == 2);
  trackparse_track_free(t);
  trackparse_parser_free(plain);
  t = trackparse_parser_parse(NULL, "Chase & Status - Baddadan", TRACKPARSE_NUL_TERMINATED);
  CHECK(t != NULL && t->artists_count == 2);
  trackparse_track_free(t);
  a = trackparse_parser_parse_artists(NULL, "A & B", TRACKPARSE_NUL_TERMINATED);
  CHECK(a != NULL && a->count == 2);
  trackparse_artists_free(a);
}

static const char tracklist_text[] =
    "Hour 2\r\n"
    "48:20 Noisia - Stigma\r\n"
    "52:47 Mefjus & Kasra - Bandwagon (Phace Remix)\r\n"
    "56:05 Black Sun Empire - ID\r\n"
    "59:31 Alix Perez - Fade Away\r\n"
    "01:03:10 Interview with Hybris\r\n"
    "01:07:44 Imanu - Sacrifice\r\n"
    "00:11:02 Current Value feat. Teddy Killerz - Payload\r\n"
    "01:15:36 Hybris - Spinning Top";

static void test_tracklist(void) {
  trackparse_tracklist *l = trackparse_parse_tracklist(tracklist_text, TRACKPARSE_NUL_TERMINATED);
  trackparse_tracklist_options *o;
  size_t i, at = 0;
  bool joined = true;
  CHECK(l != NULL);
  if (l == NULL) return;
  STR_IS(l->spec_version, TP_SPEC_VERSION);
  CHECK(l->entries_count == 9);
  for (i = 0; i < l->entries_count; i++) {
    const trackparse_entry *e = &l->entries[i];
    const char *eol = trackparse_eol_name(e->eol);
    size_t n = strlen(eol);
    joined = joined && memcmp(tracklist_text + at, e->raw.ptr, e->raw.len) == 0;
    at += e->raw.len;
    joined = joined && memcmp(tracklist_text + at, eol, n) == 0;
    at += n;
    CHECK(e->line == (int)i + 1);
  }
  CHECK(joined && at == sizeof tracklist_text - 1);
  if (l->entries_count == 9) {
    const trackparse_entry *e = &l->entries[2];
    CHECK(l->entries[0].kind == TRACKPARSE_ENTRY_HEADER);
    CHECK(l->entries[5].kind == TRACKPARSE_ENTRY_MARKER);
    CHECK(l->entries[8].eol == TRACKPARSE_EOL_NONE && l->entries[0].eol == TRACKPARSE_EOL_CRLF);
    CHECK(e->kind == TRACKPARSE_ENTRY_TRACK && e->time != NULL && str_is(e->time->raw, "52:47") &&
          e->time->seconds == 3167);
    STR_IS(e->parts.rest, "Mefjus & Kasra - Bandwagon (Phace Remix)");
    CHECK(e->track != NULL && e->track->artists_count == 2 &&
          str_is(e->track->title, "Bandwagon") && e->track->versions_count == 1 &&
          e->track->versions[0].type == TRACKPARSE_VERSION_REMIX);
    CHECK(l->entries[3].track != NULL && l->entries[3].track->flags.unknown_title);
    CHECK(l->entries[0].track == NULL && l->entries[0].time == NULL);
  }
  CHECK(l->warnings_count == 2);
  if (l->warnings_count == 2) {
    CHECK(l->warnings[0].code == TRACKPARSE_TL_NO_START_AT_ZERO && l->warnings[0].line == 2 &&
          l->warnings[0].severity == TRACKPARSE_SEVERITY_INFO && l->warnings[0].suggestion == NULL);
    CHECK(l->warnings[1].code == TRACKPARSE_TL_TIME_OUT_OF_ORDER && l->warnings[1].line == 8 &&
          l->warnings[1].severity == TRACKPARSE_SEVERITY_WARNING);
    CHECK(l->warnings[1].suggestion != NULL && l->warnings[1].suggestion->line == 8 &&
          str_is(l->warnings[1].suggestion->raw, "01:11:02") &&
          l->warnings[1].suggestion->seconds == 4262);
  }
  CHECK(l->stats.lines == 9 && l->stats.tracks == 7 && l->stats.markers == 1 &&
        l->stats.timed == 8);
  CHECK(l->stats.first_time != NULL && *l->stats.first_time == 2900);
  CHECK(l->stats.last_time != NULL && *l->stats.last_time == 4536);
  check_wire("parseTracklist", trackparse_tracklist_to_json(l),
             "{\"fn\":\"parseTracklist\",\"input\":\"Hour 2\\r\\n48:20 Noisia - Stigma\\r\\n52:47 "
             "Mefjus & Kasra - Bandwagon (Phace Remix)\\r\\n56:05 Black Sun Empire - ID\\r\\n59:31 "
             "Alix Perez - Fade Away\\r\\n01:03:10 Interview with Hybris\\r\\n01:07:44 Imanu - "
             "Sacrifice\\r\\n00:11:02 Current Value feat. Teddy Killerz - Payload\\r\\n01:15:36 "
             "Hybris - Spinning Top\"}");
  trackparse_tracklist_free(l);

  /* Options: audio length (0 is a length), mode, and the rest against the JSON door. */
  o = trackparse_tracklist_options_new();
  CHECK(o != NULL);
  CHECK(trackparse_tracklist_options_set_audio_seconds(o, 0));
  l = trackparse_parse_tracklist_with("00:00 A - B\n00:30 C - D", TRACKPARSE_NUL_TERMINATED, o);
  CHECK(l != NULL && l->warnings_count == 1 &&
        l->warnings[0].code == TRACKPARSE_TL_TIME_AFTER_AUDIO_END && l->warnings[0].line == 2);
  trackparse_tracklist_free(l);
  CHECK(trackparse_tracklist_options_set_audio_seconds(o, 3600));
  CHECK(trackparse_tracklist_options_set_mode(o, TRACKPARSE_MODE_AUTO));
  CHECK(trackparse_tracklist_options_set_split_and(o, TRACKPARSE_SPLIT_AND_ALWAYS));
  CHECK(trackparse_tracklist_options_add_known_artist(o, "Camo & Krooked", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_tracklist_options_add_version_head(o, "rerub", TRACKPARSE_NUL_TERMINATED,
                                                      "rework", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_tracklist_options_add_descriptor(o, "deluxe", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_tracklist_options_add_genre(o, "neurofunk", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_tracklist_options_add_junk(o, "out now", TRACKPARSE_NUL_TERMINATED, "promo",
                                              TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_tracklist_options_add_feat_marker(o, "avec", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_tracklist_options_add_marker_word(o, "Intermission", TRACKPARSE_NUL_TERMINATED));
  l = trackparse_parse_tracklist_with(
      "00:00 Camo & Krooked and X - Y (Z Rerub) Out Now\n1:02:03 C avec D - E\n"
      "1:05:00 Intermission\n",
      TRACKPARSE_NUL_TERMINATED, o);
  CHECK(l != NULL);
  if (l != NULL) {
    check_wire("parseTracklistWith", trackparse_tracklist_to_json(l),
               "{\"fn\":\"parseTracklist\",\"input\":\"00:00 Camo & Krooked and X - Y (Z Rerub) "
               "Out Now\\n1:02:03 C avec D - E\\n1:05:00 Intermission\\n\",\"options\":{"
               "\"audioSeconds\":3600,\"mode\":\"auto\",\"splitAnd\":\"always\","
               "\"knownArtists\":[\"Camo & Krooked\"],\"keywords\":{\"versionHeads\":{\"rerub\":"
               "\"rework\"},\"descriptors\":[\"deluxe\"],\"genres\":[\"neurofunk\"],\"junk\":{"
               "\"out now\":\"promo\"},\"featMarkers\":[\"avec\"]},\"markerWords\":["
               "\"Intermission\"]}}");
    trackparse_tracklist_free(l);
  }
  trackparse_tracklist_options_free(o);

  l = trackparse_parse_tracklist_with("", 0, NULL);
  CHECK(l != NULL && l->entries_count == 0 && l->entries == NULL && l->stats.first_time == NULL);
  trackparse_tracklist_free(l);
}

static void test_edits(void) {
  static const char text[] =
      "48:20 Noisia - Stigma\r\n59:31 Alix Perez - Fade Away\r\n00:03:10 Imanu - Sacrifice\r\n";
  trackparse_edits *e = trackparse_edits_new();
  trackparse_edit_result *r;
  static const long long one[] = {1};
  CHECK(e != NULL);
  CHECK(trackparse_edits_set_time(e, 3, 3790));
  CHECK(trackparse_edits_shift_from(e, 45, 2));
  r = trackparse_apply_edits(text, TRACKPARSE_NUL_TERMINATED, e);
  CHECK(r != NULL);
  if (r != NULL) {
    STR_IS(r->text,
           "48:20 Noisia - Stigma\r\n01:00:16 Alix Perez - Fade Away\r\n01:03:55 Imanu - "
           "Sacrifice\r\n");
    CHECK(r->changed_count == 2 && r->changed[0] == 2 && r->changed[1] == 3);
    CHECK(r->warnings_count == 1 && r->warnings[0].code == TRACKPARSE_EDIT_WARNING_STYLE_UPGRADED &&
          r->warnings[0].edit == 1 && r->warnings[0].line != NULL && *r->warnings[0].line == 2);
    check_wire("applyEdits", trackparse_edit_result_to_json(r),
               "{\"fn\":\"applyEdits\",\"input\":\"48:20 Noisia - Stigma\\r\\n59:31 Alix Perez - "
               "Fade Away\\r\\n00:03:10 Imanu - Sacrifice\\r\\n\",\"edits\":[{\"op\":\"set-time\","
               "\"line\":3,\"seconds\":3790},{\"op\":\"shift\",\"seconds\":45,\"from\":2}]}");
  }
  trackparse_edit_result_free(r);
  trackparse_edits_free(e);

  e = trackparse_edits_new();
  CHECK(trackparse_edits_shift_lines(e, -3000, one, 1));
  r = trackparse_apply_edits(text, TRACKPARSE_NUL_TERMINATED, e);
  CHECK(r != NULL && r->warnings_count == 1 &&
        r->warnings[0].code == TRACKPARSE_EDIT_WARNING_TIME_CLAMPED);
  trackparse_edit_result_free(r);
  trackparse_edits_free(e);

  e = trackparse_edits_new();
  CHECK(trackparse_edits_delete(e, 9));
  r = trackparse_apply_edits(text, TRACKPARSE_NUL_TERMINATED, e);
  CHECK(r != NULL && r->warnings_count == 1 &&
        r->warnings[0].code == TRACKPARSE_EDIT_WARNING_EDIT_INVALID &&
        r->text.len == sizeof text - 1 && memcmp(r->text.ptr, text, r->text.len) == 0 &&
        r->changed_count == 0);
  trackparse_edit_result_free(r);
  trackparse_edits_free(e);

  /* Every constructor, against the JSON door. shift_lines with no line shifts nothing; a NaN
   * shift is edit-invalid (JSON null). */
  e = trackparse_edits_new();
  CHECK(trackparse_edits_shift(e, 1.5));
  CHECK(trackparse_edits_shift_lines(e, 7, NULL, 0));
  CHECK(trackparse_edits_shift_lines(e, 7, one, 1));
  CHECK(trackparse_edits_sort_by_time(e));
  CHECK(trackparse_edits_set_rest(e, 2, "New - Rest", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_edits_insert(e, 0, "00:00 Intro - Start", TRACKPARSE_NUL_TERMINATED));
  CHECK(trackparse_edits_delete(e, 4));
  CHECK(trackparse_edits_shift(e, tp_nan()));
  CHECK(trackparse_edits_set_time(e, 1, -0.0));
  r = trackparse_apply_edits(text, TRACKPARSE_NUL_TERMINATED, e);
  CHECK(r != NULL);
  if (r != NULL) {
    check_wire("applyEdits all", trackparse_edit_result_to_json(r),
               "{\"fn\":\"applyEdits\",\"input\":\"48:20 Noisia - Stigma\\r\\n59:31 Alix Perez - "
               "Fade Away\\r\\n00:03:10 Imanu - Sacrifice\\r\\n\",\"edits\":[{\"op\":\"shift\","
               "\"seconds\":1.5},{\"op\":\"shift\",\"seconds\":7,\"lines\":[]},{\"op\":\"shift\","
               "\"seconds\":7,\"lines\":[1]},{\"op\":\"sort-by-time\"},{\"op\":\"set-rest\","
               "\"line\":2,\"rest\":\"New - Rest\"},{\"op\":\"insert\",\"after\":0,\"text\":"
               "\"00:00 Intro - Start\"},{\"op\":\"delete\",\"line\":4},{\"op\":\"shift\","
               "\"seconds\":null},{\"op\":\"set-time\",\"line\":1,\"seconds\":0}]}");
  }
  trackparse_edit_result_free(r);
  trackparse_edits_free(e);

  /* No edits, and NULL edits. */
  r = trackparse_apply_edits(text, TRACKPARSE_NUL_TERMINATED, NULL);
  CHECK(r != NULL && r->text.len == sizeof text - 1 && r->warnings_count == 0 &&
        r->changed == NULL);
  trackparse_edit_result_free(r);
}

/* -------------------------------------------------------------------------------------------- */
/* Out of memory at every allocation: NULL (or false), nothing leaked, then success.            */

typedef void *(*oom_call)(void);

static void *oom_parse(void) {
  return trackparse_parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus VIP Remix) [HD]",
                          TRACKPARSE_NUL_TERMINATED);
}
static void oom_free_track(void *p) { trackparse_track_free((trackparse_track *)p); }

static void *oom_tracklist(void) {
  return trackparse_parse_tracklist(tracklist_text, TRACKPARSE_NUL_TERMINATED);
}
static void oom_free_tracklist(void *p) { trackparse_tracklist_free((trackparse_tracklist *)p); }

static void *oom_parser(void) {
  trackparse_parse_options *o = trackparse_parse_options_new();
  trackparse_parser *p = NULL;
  if (o == NULL) return NULL;
  if (trackparse_parse_options_add_known_artist(o, "Chase & Status", TRACKPARSE_NUL_TERMINATED) &&
      trackparse_parse_options_add_junk(o, "out now", TRACKPARSE_NUL_TERMINATED, "promo",
                                        TRACKPARSE_NUL_TERMINATED)) {
    p = trackparse_parser_new(o);
  }
  trackparse_parse_options_free(o);
  return p;
}
static void oom_free_parser(void *p) { trackparse_parser_free((trackparse_parser *)p); }

static void *oom_edits(void) {
  trackparse_edits *e = trackparse_edits_new();
  trackparse_edit_result *r = NULL;
  if (e == NULL) return NULL;
  if (trackparse_edits_set_time(e, 3, 3790) && trackparse_edits_insert(e, 1, "x", 1)) {
    r = trackparse_apply_edits(tracklist_text, TRACKPARSE_NUL_TERMINATED, e);
  }
  trackparse_edits_free(e);
  return r;
}
static void oom_free_edits(void *p) { trackparse_edit_result_free((trackparse_edit_result *)p); }

static void *oom_json(void) {
  trackparse_track *t = trackparse_parse("Noisia - Stigma (VIP)", TRACKPARSE_NUL_TERMINATED);
  trackparse_string *s = NULL;
  if (t == NULL) return NULL;
  s = trackparse_track_to_json(t);
  trackparse_track_free(t);
  return s;
}
static void oom_free_string(void *p) { trackparse_string_free((trackparse_string *)p); }

static void check_oom(const char *what, oom_call call, void (*release)(void *)) {
  long n, base = live_blocks;
  void *r = NULL;
  for (n = 0; n < 100000; n++) {
    fail_from(n);
    r = call();
    fail_from(-1);
    if (r != NULL) break;
    if (live_blocks != base) {
      fprintf(stderr, "%s: %ld blocks leaked after failing malloc %ld\n", what,
              live_blocks - base, n);
      failures++;
      return;
    }
  }
  checks++;
  if (r == NULL) {
    fprintf(stderr, "%s: never succeeded\n", what);
    failures++;
    return;
  }
  release(r);
  CHECK(live_blocks == base);
  CHECK(n > 0); /* at least one failure was exercised */
}

static void test_oom(void) {
  trackparse_parse_options *o;
  bool ok;
  long base;
  /* Warm up the process-wide tables (built once, kept forever). */
  oom_free_tracklist(oom_tracklist());
  oom_free_track(oom_parse());

  check_oom("parse", oom_parse, oom_free_track);
  check_oom("parse_tracklist", oom_tracklist, oom_free_tracklist);
  check_oom("parser_new", oom_parser, oom_free_parser);
  check_oom("apply_edits", oom_edits, oom_free_edits);
  check_oom("to_json", oom_json, oom_free_string);

  /* A setter that runs out of memory leaves the option unchanged. */
  o = trackparse_parse_options_new();
  CHECK(o != NULL);
  CHECK(trackparse_parse_options_add_known_artist(o, "Chase & Status", TRACKPARSE_NUL_TERMINATED));
  base = live_blocks;
  fail_from(0);
  ok = trackparse_parse_options_add_known_artist(o, "Camo & Krooked", TRACKPARSE_NUL_TERMINATED);
  fail_from(-1);
  CHECK(!ok);
  CHECK(live_blocks == base);
  {
    trackparse_track *t =
        trackparse_parse_with("Camo & Krooked - X", TRACKPARSE_NUL_TERMINATED, o);
    CHECK(t != NULL && t->artists_count == 2);
    trackparse_track_free(t);
    t = trackparse_parse_with("Chase & Status - X", TRACKPARSE_NUL_TERMINATED, o);
    CHECK(t != NULL && t->artists_count == 1);
    trackparse_track_free(t);
  }
  trackparse_parse_options_free(o);

  /* Running out on the handle itself. */
  base = live_blocks;
  fail_from(0);
  CHECK(trackparse_parse_options_new() == NULL);
  fail_from(0);
  CHECK(trackparse_edits_new() == NULL);
  fail_from(0);
  CHECK(trackparse_parser_new(NULL) == NULL);
  fail_from(-1);
  CHECK(live_blocks == base);
}

/* -------------------------------------------------------------------------------------------- */
/* A parser shared between threads                                                              */

#ifdef TP_TEST_THREADS
typedef struct thread_arg {
  const trackparse_parser *p;
  const trackparse_string *want;
  int bad;
} thread_arg;

static void *thread_main(void *arg) {
  thread_arg *a = (thread_arg *)arg;
  int i;
  for (i = 0; i < 200; i++) {
    trackparse_track *t = trackparse_parser_parse(a->p, parser_inputs[0], TRACKPARSE_NUL_TERMINATED);
    trackparse_string *s = trackparse_track_to_json(t);
    if (!same_text(s, a->want)) a->bad++;
    trackparse_string_free(s);
    trackparse_track_free(t);
  }
  return NULL;
}

static void test_threads(void) {
  trackparse_parse_options *o = trackparse_parse_options_new();
  trackparse_parser *p;
  trackparse_track *t;
  trackparse_string *want;
  pthread_t th[4];
  thread_arg args[4];
  int i;
  CHECK(trackparse_parse_options_add_known_artist(o, "Chase & Status", TRACKPARSE_NUL_TERMINATED));
  p = trackparse_parser_new(o);
  trackparse_parse_options_free(o);
  t = trackparse_parser_parse(p, parser_inputs[0], TRACKPARSE_NUL_TERMINATED);
  want = trackparse_track_to_json(t);
  trackparse_track_free(t);
  for (i = 0; i < 4; i++) {
    args[i].p = p;
    args[i].want = want;
    args[i].bad = 0;
    CHECK(pthread_create(&th[i], NULL, thread_main, &args[i]) == 0);
  }
  for (i = 0; i < 4; i++) {
    pthread_join(th[i], NULL);
    CHECK(args[i].bad == 0);
  }
  trackparse_string_free(want);
  trackparse_parser_free(p);
}
#endif

int main(void) {
  static const trackparse_allocator counting = {count_malloc, count_realloc, count_free, NULL};
  trackparse_set_allocator(&counting);
  test_versions();
  test_enums();
  test_parse();
  test_embedded_nul();
  test_invalid_utf8();
  test_parse_artists();
  test_parse_with();
  test_format();
  test_hand_built();
  test_normalize_and_all_artists();
  test_parser();
  test_tracklist();
  test_edits();
  test_oom();
#ifdef TP_TEST_THREADS
  test_threads();
#endif
  trackparse_set_allocator(NULL);
  printf("test_api: %ld checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
