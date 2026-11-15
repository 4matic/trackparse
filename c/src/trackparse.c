/* trackparse.c: the public C API (include/trackparse.h) and the internal functions of
 * go/trackparse.go (tp_api_*, the Parser).
 *
 * Every public function that returns a result:
 * 1. opens a tp_scope (a scratch arena plus the out-of-memory jump), reads its string input
 *    (TRACKPARSE_NUL_TERMINATED means strlen; NULL means ""), sanitizes it (tp_sanitize), and
 *    runs the internal pipeline in the scratch arena;
 * 2. builds the public result into ONE block from the system allocator, in two passes over the
 *    same fill code: the first pass measures (its writes land in the scratch arena), the second
 *    fills a block of exactly that size. The block starts with a hidden RB_HDR-byte header; the
 *    result struct follows it, then every array and string the result points to. _free frees
 *    the block;
 * 3. frees the scratch arena. On out of memory it frees the scratch arena and the block (if one
 *    was taken) and returns NULL.
 *
 * Option handles and edit lists hold the internal typed options in their own arena; setters
 * build what they add in that arena under a scope and commit it only when every allocation
 * succeeded, so a setter that runs out of memory leaves the handle unchanged. A parser holds its
 * engine (the resolved options and the compiled tables) in its own arena, with deep copies of
 * every option string, so it does not depend on the options handle it was made from. */
#include <limits.h>

#include "trackparse.h"
#include "internal.h"

/* ============================================================================================ */
/* go/trackparse.go                                                                             */

/* Parse parses one track string with the default options. */
TP_PRIVATE tp_track tp_api_parse(tp_str input) {
  return tp_engine_run(tp_shared_engine(), tp_sanitize(input), false);
}

/* ParseWith parses with options (R12); tables compiled per call. o NULL: defaults. */
TP_PRIVATE tp_track tp_api_parse_with(tp_str input, const tp_parse_options *o) {
  return tp_parser_parse(tp_new_parser(o), input);
}

/* ParseArtists splits an artist credit string into artists (R10). */
TP_PRIVATE tp_artist_vec tp_api_parse_artists(tp_str input) {
  return tp_engine_run_artists(tp_shared_engine(), tp_sanitize(input));
}

TP_PRIVATE tp_artist_vec tp_api_parse_artists_with(tp_str input, const tp_parse_options *o) {
  return tp_parser_parse_artists(tp_new_parser(o), input);
}

/* Format renders a parsed track back to a string (R11) with the default options. */
TP_PRIVATE tp_str tp_api_format(const tp_track *t) {
  return tp_format_track(t, tp_resolve_format(NULL));
}

TP_PRIVATE tp_str tp_api_format_with(const tp_track *t, const tp_format_options *o) {
  return tp_format_track(t, tp_resolve_format(o));
}

/* Normalize is R1 on sanitized input. */
TP_PRIVATE tp_str tp_api_normalize(tp_str input) { return tp_normalize(tp_sanitize(input)); }

/* AllArtists is every artist of the track, remixers included, without duplicates. */
TP_PRIVATE tp_artist_vec tp_api_all_artists(const tp_track *t) { return tp_all_artists(t); }

/* NewParser compiles options (R12) into a parser, in the current arena. */
TP_PRIVATE tp_parser *tp_new_parser(const tp_parse_options *o) {
  tp_parser *p = (tp_parser *)tp_alloc(sizeof *p);
  p->e = tp_engine_new(tp_resolve_parse(o));
  return p;
}

TP_PRIVATE const tp_engine *tp_parser_engine(const tp_parser *p) {
  if (p == NULL || p->e == NULL) return tp_shared_engine();
  return p->e;
}

TP_PRIVATE tp_track tp_parser_parse(const tp_parser *p, tp_str input) {
  return tp_engine_run(tp_parser_engine(p), tp_sanitize(input), false);
}

TP_PRIVATE tp_artist_vec tp_parser_parse_artists(const tp_parser *p, tp_str input) {
  return tp_engine_run_artists(tp_parser_engine(p), tp_sanitize(input));
}

/* ParseTracklist parses a multi-line tracklist (R13). */
TP_PRIVATE tp_tracklist tp_api_parse_tracklist(tp_str text) {
  return tp_parse_tracklist(tp_sanitize(text), tp_resolve_tracklist(NULL));
}

TP_PRIVATE tp_tracklist tp_api_parse_tracklist_with(tp_str text, const tp_tracklist_options *o) {
  return tp_parse_tracklist(tp_sanitize(text), tp_resolve_tracklist(o));
}

/* ApplyEdits applies edits in order, each to the previous result (R13.9), through each edit's
 * JSON form (which also sanitizes its strings: typed and loose edits share one code path). */
TP_PRIVATE tp_edit_result tp_api_apply_edits(tp_str text, const tp_edit *edits, size_t count) {
  tp_json *loose = (tp_json *)tp_alloc_array(count, sizeof *loose);
  tp_raw_edit_result r;
  size_t i;
  for (i = 0; i < count; i++) loose[i] = tp_edit_json(&edits[i]);
  r = tp_apply_edits(tp_sanitize(text), loose, count);
  return tp_raw_edit_result_result(&r);
}

/* ApplyEditsWith: the options are accepted for symmetry (R13.10); edits do not depend on them. */
TP_PRIVATE tp_edit_result tp_api_apply_edits_with(tp_str text, const tp_edit *edits, size_t count,
                                                  const tp_tracklist_options *o) {
  (void)o;
  return tp_api_apply_edits(text, edits, count);
}

/* ============================================================================================ */
/* Versions and the allocator                                                                   */

TRACKPARSE_API const char *trackparse_library_version(void) { return TRACKPARSE_VERSION; }

TRACKPARSE_API const char *trackparse_spec_version(void) { return TP_SPEC_VERSION; }

TRACKPARSE_API void trackparse_set_allocator(const trackparse_allocator *allocator) {
  tp_allocator a;
  if (allocator == NULL) {
    tp_set_allocator(NULL);
    return;
  }
  a.malloc_fn = allocator->malloc_fn;
  a.realloc_fn = allocator->realloc_fn;
  a.free_fn = allocator->free_fn;
  a.user = allocator->user;
  tp_set_allocator(&a);
}

/* ============================================================================================ */
/* Enumerations                                                                                 */

static const char *const mode_names[] = {"auto", "clean", "youtube", "filename", "tracklist"};
static const char *const role_names[] = {"primary", "featured", "remixer", "producer"};
static const char *const source_names[] = {"artist", "title", "version"};
static const char *const version_type_names[] = {
    "remix",    "bootleg",      "vip",      "edit",   "flip",     "refix",    "rework",
    "mashup",   "blend",        "dub",      "mix",    "extended", "radio",    "club",
    "original", "instrumental", "acapella", "live",   "acoustic", "remaster", "demo",
    "reprise",  "cover",        "version",  "spedUp", "slowed",   "nightcore"};
static const char *const delimiter_names[] = {"(", "[", "{", "-"};
static const char *const junk_kind_names[] = {"video",    "audio", "lyrics", "quality", "promo",
                                              "platform", "label", "genre",  "other"};
static const char *const warning_names[] = {
    "noSeparator", "ambiguousSeparator", "unspacedDashSplit",  "asymmetricDashSplit",
    "bySplit",     "quotedTitleSplit",   "ambiguousMixCredit", "unbalancedBrackets"};
static const char *const entry_kind_names[] = {"track", "marker", "header",
                                               "link",  "note",   "blank"};
static const char *const eol_names[] = {"\n", "\r\n", "\r", ""};
static const char *const tl_warning_names[] = {
    "time-missing",     "time-out-of-order",    "time-duplicate",
    "no-start-at-zero", "time-after-audio-end", "invalid-time",
    "position-gap",     "position-duplicate",   "line-unparsed"};
static const char *const severity_names[] = {"info", "warning"};
static const char *const edit_op_names[] = {"set-time", "shift",  "sort-by-time",
                                            "set-rest", "insert", "delete"};
static const char *const edit_warning_names[] = {"edit-invalid", "time-clamped", "style-upgraded"};

/* The name of value v in a table, "" out of range. */
static const char *name_at(const char *const *names, size_t n, size_t v) {
  return v < n ? names[v] : "";
}

#define NAME_OF(table, v) name_at((table), TP_COUNT(table), (size_t)(v))

/* The index of s in a table, or -1. */
static int index_of(const char *const *names, size_t n, tp_str s) {
  size_t i;
  for (i = 0; i < n; i++) {
    if (tp_str_eq_c(s, names[i])) return (int)i;
  }
  return -1;
}

#define INDEX_OF(table, s) index_of((table), TP_COUNT(table), (s))

TRACKPARSE_API const char *trackparse_mode_name(trackparse_mode v) {
  return NAME_OF(mode_names, v);
}
TRACKPARSE_API const char *trackparse_role_name(trackparse_role v) {
  return NAME_OF(role_names, v);
}
TRACKPARSE_API const char *trackparse_source_name(trackparse_source v) {
  return NAME_OF(source_names, v);
}
TRACKPARSE_API const char *trackparse_version_type_name(trackparse_version_type v) {
  return NAME_OF(version_type_names, v);
}
TRACKPARSE_API const char *trackparse_delimiter_name(trackparse_delimiter v) {
  return NAME_OF(delimiter_names, v);
}
TRACKPARSE_API const char *trackparse_junk_kind_name(trackparse_junk_kind v) {
  return NAME_OF(junk_kind_names, v);
}
TRACKPARSE_API const char *trackparse_warning_name(trackparse_warning v) {
  return NAME_OF(warning_names, v);
}
TRACKPARSE_API const char *trackparse_entry_kind_name(trackparse_entry_kind v) {
  return NAME_OF(entry_kind_names, v);
}
TRACKPARSE_API const char *trackparse_eol_name(trackparse_eol v) { return NAME_OF(eol_names, v); }
TRACKPARSE_API const char *
trackparse_tracklist_warning_code_name(trackparse_tracklist_warning_code v) {
  return NAME_OF(tl_warning_names, v);
}
TRACKPARSE_API const char *trackparse_severity_name(trackparse_severity v) {
  return NAME_OF(severity_names, v);
}
TRACKPARSE_API const char *trackparse_edit_op_name(trackparse_edit_op v) {
  return NAME_OF(edit_op_names, v);
}
TRACKPARSE_API const char *trackparse_edit_warning_code_name(trackparse_edit_warning_code v) {
  return NAME_OF(edit_warning_names, v);
}

/* String input: NULL is "", TRACKPARSE_NUL_TERMINATED is strlen. Never a NULL pointer, so the
 * pipeline never hands NULL to memcpy. */
static tp_str in_str(const char *s, size_t len) {
  tp_str r;
  if (s == NULL) return TP_EMPTY;
  if (len == TRACKPARSE_NUL_TERMINATED) len = strlen(s);
  r.p = s;
  r.n = len;
  return r;
}

static int from_name(const char *const *names, size_t n, const char *name, size_t len) {
  if (name == NULL) return -1;
  return index_of(names, n, in_str(name, len));
}

TRACKPARSE_API bool trackparse_mode_from_name(const char *name, size_t len, trackparse_mode *out) {
  int i = from_name(mode_names, TP_COUNT(mode_names), name, len);
  if (i < 0) return false;
  if (out != NULL) *out = (trackparse_mode)i;
  return true;
}

TRACKPARSE_API bool trackparse_version_type_from_name(const char *name, size_t len,
                                                      trackparse_version_type *out) {
  int i = from_name(version_type_names, TP_COUNT(version_type_names), name, len);
  if (i < 0) return false;
  if (out != NULL) *out = (trackparse_version_type)i;
  return true;
}

TRACKPARSE_API bool trackparse_junk_kind_from_name(const char *name, size_t len,
                                                   trackparse_junk_kind *out) {
  int i = from_name(junk_kind_names, TP_COUNT(junk_kind_names), name, len);
  if (i < 0) return false;
  if (out != NULL) *out = (trackparse_junk_kind)i;
  return true;
}

/* ============================================================================================ */
/* Guarded calls and result blocks                                                              */

/* The hidden header in front of every result struct. */
#define RB_HDR 16
/* Alignment of every array and struct in a block (strings are byte-aligned). */
#define RB_ALIGN 8

/* A result block being built. base NULL: the measuring pass (allocations come from the scratch
 * arena and only used counts). */
typedef struct rb {
  char *base;
  size_t used;
  size_t cap;
} rb;

static void *rb_take(rb *b, size_t n, size_t align) {
  size_t at = tp_size_add(b->used, align - 1) & ~(align - 1);
  b->used = tp_size_add(at, n);
  if (b->base == NULL) return tp_alloc(n);
  if (b->used > b->cap) tp_oom(); /* cannot happen: both passes take the same sizes */
  return b->base + at;
}

static void *rb_alloc(rb *b, size_t n) { return rb_take(b, n, RB_ALIGN); }

/* An array of count elements, NULL for 0. */
static void *rb_array(rb *b, size_t count, size_t size) {
  if (count == 0) return NULL;
  return rb_alloc(b, tp_size_mul(count, size));
}

/* A NUL-terminated copy ("" is the static empty string). */
static trackparse_str rb_str(rb *b, tp_str s) {
  trackparse_str r;
  char *d;
  if (s.n == 0) {
    r.ptr = "";
    r.len = 0;
    return r;
  }
  d = (char *)rb_take(b, tp_size_add(s.n, 1), 1);
  memcpy(d, s.p, s.n);
  d[s.n] = '\0';
  r.ptr = d;
  r.len = s.n;
  return r;
}

/* The state of one guarded call. It lives in the frame of the public function, not in the frame
 * that calls setjmp, so its contents are well defined after the out-of-memory longjmp. */
typedef struct call {
  rb b;
  void *result;
} call;

typedef void (*work_fn)(call *c, void *arg);

/* Run work inside a scope: true when it finished, false when it ran out of memory (the scratch
 * arena and any result block are then freed). */
static bool guarded(call *c, work_fn work, void *arg) {
  tp_scope s;
  memset(c, 0, sizeof *c);
  if (!tp_scope_open(&s)) return false;
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    tp_sys_free(c->b.base);
    c->b.base = NULL;
    c->result = NULL;
    return false;
  }
  tp_scope_arm(&s);
  work(c, arg);
  tp_scope_close(&s);
  tp_arena_free(s.arena);
  return true;
}

static void *guarded_result(work_fn work, void *arg) {
  call c;
  if (!guarded(&c, work, arg)) return NULL;
  return c.result;
}

typedef void (*fill_fn)(rb *b, void *top, const void *src);

/* The result block for src: measure, take the block, fill it. Sets c->result. */
static void rb_build(call *c, size_t top_size, fill_fn fill, const void *src) {
  rb *b = &c->b;
  void *top;
  char *block;
  size_t total;
  b->base = NULL;
  b->cap = 0;
  b->used = RB_HDR;
  top = rb_alloc(b, top_size);
  fill(b, top, src);
  total = b->used;
  block = (char *)tp_sys_malloc(total);
  if (block == NULL) tp_oom();
  memset(block, 0, total);
  memcpy(block, "trackparse", 10); /* the hidden header: a tag for debuggers */
  b->base = block;
  b->cap = total;
  b->used = RB_HDR;
  top = rb_alloc(b, top_size);
  fill(b, top, src);
  b->base = NULL; /* owned by the result now */
  c->result = top;
}

/* The block of a result. */
static void free_result(const void *p) {
  if (p != NULL) tp_sys_free((char *)p - RB_HDR);
}

static int to_int(ptrdiff_t v) {
  if (v > INT_MAX) return INT_MAX;
  if (v < INT_MIN) return INT_MIN;
  return (int)v;
}

/* ============================================================================================ */
/* Internal results -> public results                                                           */

static void fill_artist(rb *b, trackparse_artist *out, const tp_artist *a) {
  int i;
  out->name = rb_str(b, a->name);
  i = INDEX_OF(role_names, a->role);
  out->role = (trackparse_role)(i < 0 ? 0 : i);
  out->joiner = NULL;
  if (a->joiner != NULL) {
    trackparse_str *j = (trackparse_str *)rb_alloc(b, sizeof *j);
    *j = rb_str(b, *a->joiner);
    out->joiner = j;
  }
  i = INDEX_OF(source_names, a->source);
  out->source = (trackparse_source)(i < 0 ? 0 : i);
}

static const trackparse_artist *fill_artist_list(rb *b, tp_artist_vec list) {
  trackparse_artist *items = (trackparse_artist *)rb_array(b, list.n, sizeof *items);
  size_t i;
  for (i = 0; i < list.n; i++) fill_artist(b, &items[i], &list.p[i]);
  return items;
}

static const int *rb_int(rb *b, const ptrdiff_t *v) {
  int *p;
  if (v == NULL) return NULL;
  p = (int *)rb_alloc(b, sizeof *p);
  *p = to_int(*v);
  return p;
}

static void fill_version(rb *b, trackparse_version *out, const tp_version *v) {
  trackparse_str *mods;
  size_t i;
  int k = INDEX_OF(version_type_names, v->type);
  /* A type that keywords introduced is "version" in the enum; type_name keeps it. */
  out->type = k < 0 ? TRACKPARSE_VERSION_VERSION : (trackparse_version_type)k;
  out->type_name = rb_str(b, v->type);
  out->raw = rb_str(b, v->raw);
  out->artists = fill_artist_list(b, v->artists);
  out->artists_count = v->artists.n;
  mods = (trackparse_str *)rb_array(b, v->modifiers.n, sizeof *mods);
  for (i = 0; i < v->modifiers.n; i++) mods[i] = rb_str(b, v->modifiers.p[i]);
  out->modifiers = mods;
  out->modifiers_count = v->modifiers.n;
  out->descriptor = NULL;
  if (v->descriptor != NULL) {
    trackparse_str *d = (trackparse_str *)rb_alloc(b, sizeof *d);
    *d = rb_str(b, *v->descriptor);
    out->descriptor = d;
  }
  out->year = rb_int(b, v->year);
  out->unknown_artist = v->unknown_artist;
  k = INDEX_OF(delimiter_names, v->delimiter);
  out->delimiter = (trackparse_delimiter)(k < 0 ? 0 : k);
}

static void fill_track(rb *b, trackparse_track *out, const tp_track *t) {
  trackparse_version *versions;
  trackparse_junk *junk;
  trackparse_warning *warnings;
  size_t i;
  int k;
  out->input = rb_str(b, t->input);
  k = INDEX_OF(mode_names, t->mode);
  out->mode = (trackparse_mode)(k < 0 ? 0 : k);
  out->position = NULL;
  if (t->position != NULL) {
    trackparse_position *p = (trackparse_position *)rb_alloc(b, sizeof *p);
    p->raw = rb_str(b, t->position->raw);
    p->number = to_int(t->position->number);
    out->position = p;
  }
  out->timestamp = NULL;
  if (t->timestamp != NULL) {
    trackparse_timestamp *p = (trackparse_timestamp *)rb_alloc(b, sizeof *p);
    p->raw = rb_str(b, t->timestamp->raw);
    p->seconds = to_int(t->timestamp->seconds);
    out->timestamp = p;
  }
  out->artists = fill_artist_list(b, t->artists);
  out->artists_count = t->artists.n;
  out->title = rb_str(b, t->title);
  out->full_title = rb_str(b, t->full_title);
  versions = (trackparse_version *)rb_array(b, t->versions.n, sizeof *versions);
  for (i = 0; i < t->versions.n; i++) fill_version(b, &versions[i], &t->versions.p[i]);
  out->versions = versions;
  out->versions_count = t->versions.n;
  out->year = rb_int(b, t->year);
  out->flags.explicit_ = t->flags.explicit_;
  out->flags.clean = t->flags.clean;
  out->flags.unknown_artist = t->flags.unknown_artist;
  out->flags.unknown_title = t->flags.unknown_title;
  junk = (trackparse_junk *)rb_array(b, t->junk.n, sizeof *junk);
  for (i = 0; i < t->junk.n; i++) {
    const tp_junk *j = &t->junk.p[i];
    k = INDEX_OF(junk_kind_names, j->kind);
    junk[i].raw = rb_str(b, j->raw);
    junk[i].kind = k < 0 ? TRACKPARSE_JUNK_OTHER : (trackparse_junk_kind)k;
    junk[i].kind_name = rb_str(b, j->kind);
  }
  out->junk = junk;
  out->junk_count = t->junk.n;
  warnings = (trackparse_warning *)rb_array(b, t->warnings.n, sizeof *warnings);
  for (i = 0; i < t->warnings.n; i++) {
    k = INDEX_OF(warning_names, t->warnings.p[i]);
    warnings[i] = (trackparse_warning)(k < 0 ? 0 : k);
  }
  out->warnings = warnings;
  out->warnings_count = t->warnings.n;
}

static void fill_track_top(rb *b, void *top, const void *src) {
  fill_track(b, (trackparse_track *)top, (const tp_track *)src);
}

static void fill_artists_top(rb *b, void *top, const void *src) {
  trackparse_artists *out = (trackparse_artists *)top;
  const tp_artist_vec *list = (const tp_artist_vec *)src;
  out->items = fill_artist_list(b, *list);
  out->count = list->n;
}

static void fill_string_top(rb *b, void *top, const void *src) {
  ((trackparse_string *)top)->str = rb_str(b, *(const tp_str *)src);
}

static const double *rb_double(rb *b, const double *v) {
  double *p;
  if (v == NULL) return NULL;
  p = (double *)rb_alloc(b, sizeof *p);
  *p = *v;
  return p;
}

static void fill_tracklist_top(rb *b, void *top, const void *src) {
  trackparse_tracklist *out = (trackparse_tracklist *)top;
  const tp_tracklist *l = (const tp_tracklist *)src;
  trackparse_entry *entries;
  trackparse_tracklist_warning *warnings;
  size_t i;
  int k;
  out->spec_version = rb_str(b, l->spec_version);
  entries = (trackparse_entry *)rb_array(b, l->entries.n, sizeof *entries);
  for (i = 0; i < l->entries.n; i++) {
    const tp_entry *e = &l->entries.p[i];
    trackparse_entry *x = &entries[i];
    x->line = to_int(e->line);
    k = INDEX_OF(entry_kind_names, e->kind);
    x->kind = (trackparse_entry_kind)(k < 0 ? 0 : k);
    k = INDEX_OF(eol_names, e->eol);
    x->eol = (trackparse_eol)(k < 0 ? 0 : k);
    x->raw = rb_str(b, e->raw);
    x->parts.lead = rb_str(b, e->parts.lead);
    x->parts.time = rb_str(b, e->parts.time);
    x->parts.gap = rb_str(b, e->parts.gap);
    x->parts.rest = rb_str(b, e->parts.rest);
    x->time = NULL;
    if (e->time != NULL) {
      trackparse_tracklist_time *t = (trackparse_tracklist_time *)rb_alloc(b, sizeof *t);
      t->raw = rb_str(b, e->time->raw);
      t->seconds = e->time->seconds;
      x->time = t;
    }
    x->track = NULL;
    if (e->track != NULL) {
      trackparse_track *t = (trackparse_track *)rb_alloc(b, sizeof *t);
      fill_track(b, t, e->track);
      x->track = t;
    }
    x->layered = e->layered;
  }
  out->entries = entries;
  out->entries_count = l->entries.n;
  warnings = (trackparse_tracklist_warning *)rb_array(b, l->warnings.n, sizeof *warnings);
  for (i = 0; i < l->warnings.n; i++) {
    const tp_tracklist_warning *w = &l->warnings.p[i];
    trackparse_tracklist_warning *x = &warnings[i];
    k = INDEX_OF(tl_warning_names, w->code);
    x->code = (trackparse_tracklist_warning_code)(k < 0 ? 0 : k);
    x->line = to_int(w->line);
    k = INDEX_OF(severity_names, w->severity);
    x->severity = (trackparse_severity)(k < 0 ? 0 : k);
    x->suggestion = NULL;
    if (w->suggestion != NULL) {
      trackparse_suggestion *s = (trackparse_suggestion *)rb_alloc(b, sizeof *s);
      s->line = to_int(w->suggestion->line);
      s->raw = rb_str(b, w->suggestion->raw);
      s->seconds = w->suggestion->seconds;
      x->suggestion = s;
    }
  }
  out->warnings = warnings;
  out->warnings_count = l->warnings.n;
  out->stats.lines = to_int(l->stats.lines);
  out->stats.tracks = to_int(l->stats.tracks);
  out->stats.markers = to_int(l->stats.markers);
  out->stats.timed = to_int(l->stats.timed);
  out->stats.first_time = rb_double(b, l->stats.first_time);
  out->stats.last_time = rb_double(b, l->stats.last_time);
}

static void fill_edit_result_top(rb *b, void *top, const void *src) {
  trackparse_edit_result *out = (trackparse_edit_result *)top;
  const tp_edit_result *r = (const tp_edit_result *)src;
  long long *changed;
  trackparse_edit_warning *warnings;
  size_t i;
  out->text = rb_str(b, r->text);
  changed = (long long *)rb_array(b, r->changed.n, sizeof *changed);
  for (i = 0; i < r->changed.n; i++) changed[i] = (long long)r->changed.p[i];
  out->changed = changed;
  out->changed_count = r->changed.n;
  warnings = (trackparse_edit_warning *)rb_array(b, r->warnings.n, sizeof *warnings);
  for (i = 0; i < r->warnings.n; i++) {
    const tp_edit_warning *w = &r->warnings.p[i];
    int k = INDEX_OF(edit_warning_names, w->code);
    warnings[i].code = (trackparse_edit_warning_code)(k < 0 ? 0 : k);
    warnings[i].edit = w->edit < 0 ? 0 : (size_t)w->edit;
    warnings[i].line = NULL;
    if (w->line != NULL) {
      long long *n = (long long *)rb_alloc(b, sizeof *n);
      *n = (long long)*w->line;
      warnings[i].line = n;
    }
  }
  out->warnings = warnings;
  out->warnings_count = r->warnings.n;
}

/* ============================================================================================ */
/* Public results -> internal values (format, all_artists, *_to_json). A C caller may build a   */
/* result by hand: NULL arrays count as empty, NULL strings as "", out-of-range enums as "".    */

static tp_str int_str(trackparse_str s) {
  tp_str r;
  if (s.ptr == NULL) return TP_EMPTY;
  r.p = s.ptr;
  r.n = s.len;
  return tp_sanitize(r);
}

static const tp_str *int_opt_str(const trackparse_str *s) {
  tp_str *p;
  if (s == NULL) return NULL;
  p = (tp_str *)tp_alloc(sizeof *p);
  *p = int_str(*s);
  return p;
}

static const ptrdiff_t *int_opt_int(const int *v) {
  ptrdiff_t *p;
  if (v == NULL) return NULL;
  p = (ptrdiff_t *)tp_alloc(sizeof *p);
  *p = (ptrdiff_t)*v;
  return p;
}

static const double *int_opt_double(const double *v) {
  double *p;
  if (v == NULL) return NULL;
  p = (double *)tp_alloc(sizeof *p);
  *p = *v;
  return p;
}

#define INT_NAME(table, v) tp_str_c(NAME_OF(table, v))
/* The count of an array, 0 when its pointer is NULL. */
#define COUNT(items, count) ((items) == NULL ? (size_t)0 : (count))

static tp_artist int_artist(const trackparse_artist *a) {
  tp_artist r;
  r.name = int_str(a->name);
  r.role = INT_NAME(role_names, a->role);
  r.joiner = int_opt_str(a->joiner);
  r.source = INT_NAME(source_names, a->source);
  return r;
}

static tp_artist_vec int_artist_list(const trackparse_artist *items, size_t count) {
  tp_artist_vec v = {0, 0, 0};
  size_t i, n = COUNT(items, count);
  for (i = 0; i < n; i++) tp_push(v, int_artist(&items[i]));
  return v;
}

static tp_version int_version(const trackparse_version *v) {
  tp_version r;
  size_t i, n;
  memset(&r, 0, sizeof r);
  /* type_name is what a library result carries; a hand-built version may leave it unset. */
  r.type = v->type_name.ptr != NULL ? int_str(v->type_name) : INT_NAME(version_type_names, v->type);
  r.raw = int_str(v->raw);
  r.artists = int_artist_list(v->artists, v->artists_count);
  n = COUNT(v->modifiers, v->modifiers_count);
  for (i = 0; i < n; i++) tp_push(r.modifiers, int_str(v->modifiers[i]));
  r.descriptor = int_opt_str(v->descriptor);
  r.year = int_opt_int(v->year);
  r.unknown_artist = v->unknown_artist;
  r.delimiter = INT_NAME(delimiter_names, v->delimiter);
  return r;
}

static tp_track int_track(const trackparse_track *t) {
  tp_track r;
  size_t i, n;
  memset(&r, 0, sizeof r);
  r.input = int_str(t->input);
  r.mode = INT_NAME(mode_names, t->mode);
  if (t->position != NULL) {
    tp_position *p = (tp_position *)tp_alloc(sizeof *p);
    p->raw = int_str(t->position->raw);
    p->number = (ptrdiff_t)t->position->number;
    r.position = p;
  }
  if (t->timestamp != NULL) {
    tp_timestamp *p = (tp_timestamp *)tp_alloc(sizeof *p);
    p->raw = int_str(t->timestamp->raw);
    p->seconds = (ptrdiff_t)t->timestamp->seconds;
    r.timestamp = p;
  }
  r.artists = int_artist_list(t->artists, t->artists_count);
  r.title = int_str(t->title);
  r.full_title = int_str(t->full_title);
  n = COUNT(t->versions, t->versions_count);
  for (i = 0; i < n; i++) tp_push(r.versions, int_version(&t->versions[i]));
  r.year = int_opt_int(t->year);
  r.flags.explicit_ = t->flags.explicit_;
  r.flags.clean = t->flags.clean;
  r.flags.unknown_artist = t->flags.unknown_artist;
  r.flags.unknown_title = t->flags.unknown_title;
  n = COUNT(t->junk, t->junk_count);
  for (i = 0; i < n; i++) {
    const trackparse_junk *j = &t->junk[i];
    tp_junk x;
    x.raw = int_str(j->raw);
    x.kind = j->kind_name.ptr != NULL ? int_str(j->kind_name) : INT_NAME(junk_kind_names, j->kind);
    tp_push(r.junk, x);
  }
  n = COUNT(t->warnings, t->warnings_count);
  for (i = 0; i < n; i++) tp_push(r.warnings, INT_NAME(warning_names, t->warnings[i]));
  return r;
}

/* The empty track a NULL track pointer stands for. */
static tp_track int_track_or_empty(const trackparse_track *t) {
  trackparse_track empty;
  if (t != NULL) return int_track(t);
  memset(&empty, 0, sizeof empty);
  empty.mode = TRACKPARSE_MODE_CLEAN;
  return int_track(&empty);
}

static tp_tracklist int_tracklist(const trackparse_tracklist *l) {
  tp_tracklist r;
  size_t i, n;
  memset(&r, 0, sizeof r);
  r.spec_version = int_str(l->spec_version);
  n = COUNT(l->entries, l->entries_count);
  for (i = 0; i < n; i++) {
    const trackparse_entry *e = &l->entries[i];
    tp_entry x;
    memset(&x, 0, sizeof x);
    x.line = (ptrdiff_t)e->line;
    x.kind = INT_NAME(entry_kind_names, e->kind);
    x.eol = INT_NAME(eol_names, e->eol);
    x.raw = int_str(e->raw);
    x.parts.lead = int_str(e->parts.lead);
    x.parts.time = int_str(e->parts.time);
    x.parts.gap = int_str(e->parts.gap);
    x.parts.rest = int_str(e->parts.rest);
    if (e->time != NULL) {
      tp_tracklist_time *t = (tp_tracklist_time *)tp_alloc(sizeof *t);
      t->raw = int_str(e->time->raw);
      t->seconds = e->time->seconds;
      x.time = t;
    }
    if (e->track != NULL) {
      tp_track *t = (tp_track *)tp_alloc(sizeof *t);
      *t = int_track(e->track);
      x.track = t;
    }
    x.layered = e->layered;
    tp_push(r.entries, x);
  }
  n = COUNT(l->warnings, l->warnings_count);
  for (i = 0; i < n; i++) {
    const trackparse_tracklist_warning *w = &l->warnings[i];
    tp_tracklist_warning x;
    x.code = INT_NAME(tl_warning_names, w->code);
    x.line = (ptrdiff_t)w->line;
    x.severity = INT_NAME(severity_names, w->severity);
    x.suggestion = NULL;
    if (w->suggestion != NULL) {
      tp_suggestion *s = (tp_suggestion *)tp_alloc(sizeof *s);
      s->line = (ptrdiff_t)w->suggestion->line;
      s->raw = int_str(w->suggestion->raw);
      s->seconds = w->suggestion->seconds;
      x.suggestion = s;
    }
    tp_push(r.warnings, x);
  }
  r.stats.lines = (ptrdiff_t)l->stats.lines;
  r.stats.tracks = (ptrdiff_t)l->stats.tracks;
  r.stats.markers = (ptrdiff_t)l->stats.markers;
  r.stats.timed = (ptrdiff_t)l->stats.timed;
  r.stats.first_time = int_opt_double(l->stats.first_time);
  r.stats.last_time = int_opt_double(l->stats.last_time);
  return r;
}

static tp_edit_result int_edit_result(const trackparse_edit_result *e) {
  tp_edit_result r;
  size_t i, n;
  memset(&r, 0, sizeof r);
  r.text = int_str(e->text);
  n = COUNT(e->changed, e->changed_count);
  for (i = 0; i < n; i++) tp_push(r.changed, (int64_t)e->changed[i]);
  n = COUNT(e->warnings, e->warnings_count);
  for (i = 0; i < n; i++) {
    const trackparse_edit_warning *w = &e->warnings[i];
    tp_edit_warning x;
    x.code = INT_NAME(edit_warning_names, w->code);
    x.edit = w->edit > (size_t)PTRDIFF_MAX ? PTRDIFF_MAX : (ptrdiff_t)w->edit;
    x.line = NULL;
    if (w->line != NULL) {
      int64_t *p = (int64_t *)tp_alloc(sizeof *p);
      *p = (int64_t)*w->line;
      x.line = p;
    }
    tp_push(r.warnings, x);
  }
  return r;
}

/* ============================================================================================ */
/* Options                                                                                      */

struct trackparse_parse_options {
  tp_arena *arena; /* NULL until a setter needs memory */
  tp_parse_options o;
};

struct trackparse_format_options {
  tp_arena *arena;
  tp_format_options o;
};

struct trackparse_tracklist_options {
  tp_arena *arena;
  tp_tracklist_options o;
};

/* A zeroed handle from the system allocator. */
static void *handle_new(size_t size) {
  void *p = tp_sys_malloc(size);
  if (p != NULL) memset(p, 0, size);
  return p;
}

static void init_keywords(tp_keywords *k) { memset(k, 0, sizeof *k); }

typedef void (*mutate_fn)(void *arg);

/* Run fn with *arena current, under a scope: false when it ran out of memory. fn must commit
 * its change only after its last allocation, so a failure leaves the handle as it was. */
static bool mutate(tp_arena **arena, mutate_fn fn, void *arg) {
  tp_scope s;
  if (*arena == NULL) {
    *arena = tp_arena_new();
    if (*arena == NULL) return false;
  }
  if (!tp_scope_open(&s)) return false;
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    return false;
  }
  tp_scope_arm(&s);
  tp_arena_use(*arena);
  fn(arg);
  tp_scope_close(&s);
  tp_arena_free(s.arena);
  return true;
}

/* Setter arguments. */
typedef struct set_arg {
  tp_str *str;         /* set a string */
  const tp_str **pstr; /* set a nullable string */
  tp_strvec *list;     /* append a string */
  tp_version_head_vec *heads;
  tp_junk_phrase_vec *junk;
  const double **num; /* set a nullable number */
  tp_str a, b;
  double value;
} set_arg;

static void do_set_str(void *p) {
  set_arg *x = (set_arg *)p;
  *x->str = tp_str_copy(x->a.p, x->a.n);
}

static void do_set_pstr(void *p) {
  set_arg *x = (set_arg *)p;
  tp_str *s = (tp_str *)tp_alloc(sizeof *s);
  *s = tp_str_copy(x->a.p, x->a.n);
  *x->pstr = s;
}

static void do_add_str(void *p) {
  set_arg *x = (set_arg *)p;
  tp_strvec v = *x->list;
  tp_push(v, tp_str_copy(x->a.p, x->a.n));
  *x->list = v;
}

static void do_add_head(void *p) {
  set_arg *x = (set_arg *)p;
  tp_version_head_vec v = *x->heads;
  tp_version_head h;
  h.key = tp_str_copy(x->a.p, x->a.n);
  h.type = tp_str_copy(x->b.p, x->b.n);
  tp_push(v, h);
  *x->heads = v;
}

static void do_add_junk(void *p) {
  set_arg *x = (set_arg *)p;
  tp_junk_phrase_vec v = *x->junk;
  tp_junk_phrase j;
  j.phrase = tp_str_copy(x->a.p, x->a.n);
  j.kind = tp_str_copy(x->b.p, x->b.n);
  tp_push(v, j);
  *x->junk = v;
}

static void do_set_num(void *p) {
  set_arg *x = (set_arg *)p;
  double *d = (double *)tp_alloc(sizeof *d);
  *d = x->value;
  *x->num = d;
}

static set_arg arg_of(tp_str a, tp_str b) {
  set_arg x;
  memset(&x, 0, sizeof x);
  x.a = a;
  x.b = b;
  return x;
}

static bool set_string(tp_arena **arena, tp_str *field, const char *s, size_t len) {
  set_arg x = arg_of(in_str(s, len), TP_EMPTY);
  x.str = field;
  return mutate(arena, do_set_str, &x);
}

static bool add_string(tp_arena **arena, tp_strvec *list, const char *s, size_t len) {
  set_arg x = arg_of(in_str(s, len), TP_EMPTY);
  x.list = list;
  return mutate(arena, do_add_str, &x);
}

static bool add_head(tp_arena **arena, tp_keywords *k, const char *key, size_t key_len,
                     const char *type, size_t type_len) {
  set_arg x = arg_of(in_str(key, key_len), in_str(type, type_len));
  x.heads = &k->version_heads;
  return mutate(arena, do_add_head, &x);
}

static bool add_junk(tp_arena **arena, tp_keywords *k, const char *phrase, size_t phrase_len,
                     const char *kind, size_t kind_len) {
  set_arg x = arg_of(in_str(phrase, phrase_len), in_str(kind, kind_len));
  x.junk = &k->junk;
  return mutate(arena, do_add_junk, &x);
}

/* The option string of an enum value; "" (the default) for a value out of range. */
static tp_str mode_option(trackparse_mode m) { return tp_str_c(NAME_OF(mode_names, m)); }

static tp_str split_and_option(trackparse_split_and v) {
  static const char *const names[] = {"auto", "always", "never"};
  return tp_str_c(NAME_OF(names, v));
}

/* Parse options */

TRACKPARSE_API trackparse_parse_options *trackparse_parse_options_new(void) {
  trackparse_parse_options *o = (trackparse_parse_options *)handle_new(sizeof *o);
  if (o == NULL) return NULL;
  o->o.mode = TP_EMPTY;
  o->o.uploader = TP_EMPTY;
  o->o.split_and = TP_EMPTY;
  init_keywords(&o->o.keywords);
  return o;
}

TRACKPARSE_API void trackparse_parse_options_free(trackparse_parse_options *o) {
  if (o == NULL) return;
  tp_arena_free(o->arena);
  tp_sys_free(o);
}

TRACKPARSE_API bool trackparse_parse_options_set_mode(trackparse_parse_options *o,
                                                      trackparse_mode mode) {
  if (o == NULL) return true;
  o->o.mode = mode_option(mode);
  return true;
}

TRACKPARSE_API bool trackparse_parse_options_set_uploader(trackparse_parse_options *o,
                                                          const char *s, size_t len) {
  if (o == NULL) return true;
  return set_string(&o->arena, &o->o.uploader, s, len);
}

TRACKPARSE_API bool trackparse_parse_options_add_known_artist(trackparse_parse_options *o,
                                                              const char *s, size_t len) {
  if (o == NULL) return true;
  return add_string(&o->arena, &o->o.known_artists, s, len);
}

TRACKPARSE_API bool trackparse_parse_options_set_split_and(trackparse_parse_options *o,
                                                           trackparse_split_and v) {
  if (o == NULL) return true;
  o->o.split_and = split_and_option(v);
  return true;
}

TRACKPARSE_API bool trackparse_parse_options_add_version_head(trackparse_parse_options *o,
                                                              const char *key, size_t key_len,
                                                              const char *type, size_t type_len) {
  if (o == NULL) return true;
  return add_head(&o->arena, &o->o.keywords, key, key_len, type, type_len);
}

TRACKPARSE_API bool trackparse_parse_options_add_descriptor(trackparse_parse_options *o,
                                                            const char *s, size_t len) {
  if (o == NULL) return true;
  return add_string(&o->arena, &o->o.keywords.descriptors, s, len);
}

TRACKPARSE_API bool trackparse_parse_options_add_genre(trackparse_parse_options *o, const char *s,
                                                       size_t len) {
  if (o == NULL) return true;
  return add_string(&o->arena, &o->o.keywords.genres, s, len);
}

TRACKPARSE_API bool trackparse_parse_options_add_junk(trackparse_parse_options *o,
                                                      const char *phrase, size_t phrase_len,
                                                      const char *kind, size_t kind_len) {
  if (o == NULL) return true;
  return add_junk(&o->arena, &o->o.keywords, phrase, phrase_len, kind, kind_len);
}

TRACKPARSE_API bool trackparse_parse_options_add_feat_marker(trackparse_parse_options *o,
                                                             const char *s, size_t len) {
  if (o == NULL) return true;
  return add_string(&o->arena, &o->o.keywords.feat_markers, s, len);
}

/* Format options */

TRACKPARSE_API trackparse_format_options *trackparse_format_options_new(void) {
  trackparse_format_options *o = (trackparse_format_options *)handle_new(sizeof *o);
  if (o == NULL) return NULL;
  o->o.feat = TP_EMPTY;
  o->o.joiners = TP_EMPTY;
  o->o.versions = TP_EMPTY;
  return o;
}

TRACKPARSE_API void trackparse_format_options_free(trackparse_format_options *o) {
  if (o == NULL) return;
  tp_arena_free(o->arena);
  tp_sys_free(o);
}

TRACKPARSE_API bool trackparse_format_options_set_feat(trackparse_format_options *o,
                                                       trackparse_feat v) {
  static const char *const names[] = {"source", "artist", "title", "omit"};
  if (o == NULL) return true;
  o->o.feat = tp_str_c(NAME_OF(names, v));
  return true;
}

TRACKPARSE_API bool trackparse_format_options_set_feat_marker(trackparse_format_options *o,
                                                              const char *s, size_t len) {
  set_arg x;
  if (o == NULL) return true;
  x = arg_of(in_str(s, len), TP_EMPTY);
  x.pstr = &o->o.feat_marker;
  return mutate(&o->arena, do_set_pstr, &x);
}

TRACKPARSE_API bool trackparse_format_options_set_joiners(trackparse_format_options *o,
                                                          trackparse_joiners v) {
  static const char *const names[] = {"original", "canonical"};
  if (o == NULL) return true;
  o->o.joiners = tp_str_c(NAME_OF(names, v));
  return true;
}

TRACKPARSE_API bool trackparse_format_options_set_versions(trackparse_format_options *o,
                                                           trackparse_versions v) {
  static const char *const names[] = {"all", "none"};
  if (o == NULL) return true;
  o->o.versions = tp_str_c(NAME_OF(names, v));
  return true;
}

TRACKPARSE_API bool trackparse_format_options_set_omit_producers(trackparse_format_options *o,
                                                                 bool v) {
  if (o == NULL) return true;
  o->o.omit_producers = v;
  return true;
}

TRACKPARSE_API bool trackparse_format_options_set_position(trackparse_format_options *o, bool v) {
  if (o == NULL) return true;
  o->o.position = v;
  return true;
}

/* Tracklist options */

TRACKPARSE_API trackparse_tracklist_options *trackparse_tracklist_options_new(void) {
  trackparse_tracklist_options *o = (trackparse_tracklist_options *)handle_new(sizeof *o);
  if (o == NULL) return NULL;
  o->o.mode = TP_EMPTY;
  o->o.split_and = TP_EMPTY;
  init_keywords(&o->o.keywords);
  return o;
}

TRACKPARSE_API void trackparse_tracklist_options_free(trackparse_tracklist_options *o) {
  if (o == NULL) return;
  tp_arena_free(o->arena);
  tp_sys_free(o);
}

TRACKPARSE_API bool trackparse_tracklist_options_set_mode(trackparse_tracklist_options *o,
                                                          trackparse_mode mode) {
  if (o == NULL) return true;
  o->o.mode = mode_option(mode);
  return true;
}

TRACKPARSE_API bool trackparse_tracklist_options_add_known_artist(trackparse_tracklist_options *o,
                                                                  const char *s, size_t len) {
  if (o == NULL) return true;
  return add_string(&o->arena, &o->o.known_artists, s, len);
}

TRACKPARSE_API bool trackparse_tracklist_options_set_split_and(trackparse_tracklist_options *o,
                                                               trackparse_split_and v) {
  if (o == NULL) return true;
  o->o.split_and = split_and_option(v);
  return true;
}

TRACKPARSE_API bool trackparse_tracklist_options_add_version_head(trackparse_tracklist_options *o,
                                                                  const char *key, size_t key_len,
                                                                  const char *type,
                                                                  size_t type_len) {
  if (o == NULL) return true;
  return add_head(&o->arena, &o->o.keywords, key, key_len, type, type_len);
}

TRACKPARSE_API bool trackparse_tracklist_options_add_descriptor(trackparse_tracklist_options *o,
                                                                const char *s, size_t len) {
  if (o == NULL) return true;
  return add_string(&o->arena, &o->o.keywords.descriptors, s, len);
}

TRACKPARSE_API bool trackparse_tracklist_options_add_genre(trackparse_tracklist_options *o,
                                                           const char *s, size_t len) {
  if (o == NULL) return true;
  return add_string(&o->arena, &o->o.keywords.genres, s, len);
}

TRACKPARSE_API bool trackparse_tracklist_options_add_junk(trackparse_tracklist_options *o,
                                                          const char *phrase, size_t phrase_len,
                                                          const char *kind, size_t kind_len) {
  if (o == NULL) return true;
  return add_junk(&o->arena, &o->o.keywords, phrase, phrase_len, kind, kind_len);
}

TRACKPARSE_API bool trackparse_tracklist_options_add_feat_marker(trackparse_tracklist_options *o,
                                                                 const char *s, size_t len) {
  if (o == NULL) return true;
  return add_string(&o->arena, &o->o.keywords.feat_markers, s, len);
}

TRACKPARSE_API bool trackparse_tracklist_options_set_audio_seconds(trackparse_tracklist_options *o,
                                                                   double seconds) {
  set_arg x;
  if (o == NULL) return true;
  x = arg_of(TP_EMPTY, TP_EMPTY);
  x.num = &o->o.audio_seconds;
  x.value = seconds;
  return mutate(&o->arena, do_set_num, &x);
}

TRACKPARSE_API bool trackparse_tracklist_options_add_marker_word(trackparse_tracklist_options *o,
                                                                 const char *s, size_t len) {
  if (o == NULL) return true;
  return add_string(&o->arena, &o->o.marker_words, s, len);
}

/* ============================================================================================ */
/* Single tracks                                                                                */

typedef struct parse_arg {
  tp_str input;
  const tp_parse_options *o; /* with: compiled per call (NULL: defaults) */
  const tp_parser *p;        /* a parser's engine, or NULL */
  bool with;
} parse_arg;

static void work_parse(call *c, void *arg) {
  const parse_arg *a = (const parse_arg *)arg;
  tp_track t;
  if (a->p != NULL) {
    t = tp_parser_parse(a->p, a->input);
  } else if (a->with) {
    t = tp_api_parse_with(a->input, a->o);
  } else {
    t = tp_api_parse(a->input);
  }
  rb_build(c, sizeof(trackparse_track), fill_track_top, &t);
}

static void work_parse_artists(call *c, void *arg) {
  const parse_arg *a = (const parse_arg *)arg;
  tp_artist_vec list;
  if (a->p != NULL) {
    list = tp_parser_parse_artists(a->p, a->input);
  } else if (a->with) {
    list = tp_api_parse_artists_with(a->input, a->o);
  } else {
    list = tp_api_parse_artists(a->input);
  }
  rb_build(c, sizeof(trackparse_artists), fill_artists_top, &list);
}

static parse_arg parse_arg_of(const char *s, size_t len) {
  parse_arg a;
  memset(&a, 0, sizeof a);
  a.input = in_str(s, len);
  return a;
}

TRACKPARSE_API trackparse_track *trackparse_parse(const char *s, size_t len) {
  parse_arg a = parse_arg_of(s, len);
  return (trackparse_track *)guarded_result(work_parse, &a);
}

TRACKPARSE_API trackparse_track *trackparse_parse_with(const char *s, size_t len,
                                                       const trackparse_parse_options *o) {
  parse_arg a = parse_arg_of(s, len);
  a.with = o != NULL;
  a.o = o != NULL ? &o->o : NULL;
  return (trackparse_track *)guarded_result(work_parse, &a);
}

TRACKPARSE_API void trackparse_track_free(trackparse_track *t) { free_result(t); }

TRACKPARSE_API trackparse_artists *trackparse_parse_artists(const char *s, size_t len) {
  parse_arg a = parse_arg_of(s, len);
  return (trackparse_artists *)guarded_result(work_parse_artists, &a);
}

TRACKPARSE_API trackparse_artists *
trackparse_parse_artists_with(const char *s, size_t len, const trackparse_parse_options *o) {
  parse_arg a = parse_arg_of(s, len);
  a.with = o != NULL;
  a.o = o != NULL ? &o->o : NULL;
  return (trackparse_artists *)guarded_result(work_parse_artists, &a);
}

TRACKPARSE_API void trackparse_artists_free(trackparse_artists *a) { free_result(a); }

TRACKPARSE_API void trackparse_string_free(trackparse_string *s) { free_result(s); }

static void build_string(call *c, tp_str s) {
  rb_build(c, sizeof(trackparse_string), fill_string_top, &s);
}

typedef struct format_arg {
  const trackparse_track *t;
  const tp_format_options *o;
} format_arg;

static void work_format(call *c, void *arg) {
  const format_arg *a = (const format_arg *)arg;
  tp_track t = int_track_or_empty(a->t);
  build_string(c, a->o != NULL ? tp_api_format_with(&t, a->o) : tp_api_format(&t));
}

TRACKPARSE_API trackparse_string *trackparse_format(const trackparse_track *t) {
  format_arg a;
  a.t = t;
  a.o = NULL;
  return (trackparse_string *)guarded_result(work_format, &a);
}

TRACKPARSE_API trackparse_string *trackparse_format_with(const trackparse_track *t,
                                                         const trackparse_format_options *o) {
  format_arg a;
  a.t = t;
  a.o = o != NULL ? &o->o : NULL;
  return (trackparse_string *)guarded_result(work_format, &a);
}

static void work_normalize(call *c, void *arg) {
  build_string(c, tp_api_normalize(*(const tp_str *)arg));
}

TRACKPARSE_API trackparse_string *trackparse_normalize(const char *s, size_t len) {
  tp_str in = in_str(s, len);
  return (trackparse_string *)guarded_result(work_normalize, &in);
}

static void work_all_artists(call *c, void *arg) {
  tp_track t = int_track_or_empty((const trackparse_track *)arg);
  tp_artist_vec list = tp_api_all_artists(&t);
  rb_build(c, sizeof(trackparse_artists), fill_artists_top, &list);
}

TRACKPARSE_API trackparse_artists *trackparse_all_artists(const trackparse_track *t) {
  return (trackparse_artists *)guarded_result(work_all_artists, (void *)(uintptr_t)t);
}

/* Parser */

struct trackparse_parser {
  tp_arena *arena; /* the engine, its tables and copies of the option strings */
  tp_parser p;
};

static tp_str copy_str(tp_str s) { return tp_str_copy(s.p, s.n); }

static tp_strvec copy_list(tp_strvec v) {
  tp_strvec out = {0, 0, 0};
  size_t i;
  for (i = 0; i < v.n; i++) tp_push(out, copy_str(v.p[i]));
  return out;
}

/* A deep copy of o in the current arena: the tables keep pointers to the keyword strings. */
static tp_parse_options copy_parse_options(const tp_parse_options *o) {
  tp_parse_options r;
  size_t i;
  memset(&r, 0, sizeof r);
  r.mode = copy_str(o->mode);
  r.uploader = copy_str(o->uploader);
  r.known_artists = copy_list(o->known_artists);
  r.split_and = copy_str(o->split_and);
  for (i = 0; i < o->keywords.version_heads.n; i++) {
    tp_version_head h;
    h.key = copy_str(o->keywords.version_heads.p[i].key);
    h.type = copy_str(o->keywords.version_heads.p[i].type);
    tp_push(r.keywords.version_heads, h);
  }
  r.keywords.descriptors = copy_list(o->keywords.descriptors);
  r.keywords.genres = copy_list(o->keywords.genres);
  for (i = 0; i < o->keywords.junk.n; i++) {
    tp_junk_phrase j;
    j.phrase = copy_str(o->keywords.junk.p[i].phrase);
    j.kind = copy_str(o->keywords.junk.p[i].kind);
    tp_push(r.keywords.junk, j);
  }
  r.keywords.feat_markers = copy_list(o->keywords.feat_markers);
  return r;
}

typedef struct parser_arg {
  trackparse_parser *p;
  const tp_parse_options *o;
} parser_arg;

static void do_parser_new(void *arg) {
  parser_arg *a = (parser_arg *)arg;
  if (a->o != NULL) {
    tp_parse_options copy = copy_parse_options(a->o);
    a->p->p = *tp_new_parser(&copy);
  } else {
    a->p->p = *tp_new_parser(NULL);
  }
}

TRACKPARSE_API trackparse_parser *trackparse_parser_new(const trackparse_parse_options *o) {
  trackparse_parser *p = (trackparse_parser *)handle_new(sizeof *p);
  parser_arg a;
  if (p == NULL) return NULL;
  a.p = p;
  a.o = o != NULL ? &o->o : NULL;
  if (!mutate(&p->arena, do_parser_new, &a)) {
    trackparse_parser_free(p);
    return NULL;
  }
  return p;
}

TRACKPARSE_API void trackparse_parser_free(trackparse_parser *p) {
  if (p == NULL) return;
  tp_arena_free(p->arena);
  tp_sys_free(p);
}

TRACKPARSE_API trackparse_track *trackparse_parser_parse(const trackparse_parser *p, const char *s,
                                                         size_t len) {
  parse_arg a = parse_arg_of(s, len);
  if (p != NULL) a.p = &p->p;
  return (trackparse_track *)guarded_result(work_parse, &a);
}

TRACKPARSE_API trackparse_artists *trackparse_parser_parse_artists(const trackparse_parser *p,
                                                                   const char *s, size_t len) {
  parse_arg a = parse_arg_of(s, len);
  if (p != NULL) a.p = &p->p;
  return (trackparse_artists *)guarded_result(work_parse_artists, &a);
}

/* ============================================================================================ */
/* Tracklists and edits                                                                         */

typedef struct tracklist_arg {
  tp_str text;
  const tp_tracklist_options *o;
} tracklist_arg;

static void work_tracklist(call *c, void *arg) {
  const tracklist_arg *a = (const tracklist_arg *)arg;
  tp_tracklist l =
      a->o != NULL ? tp_api_parse_tracklist_with(a->text, a->o) : tp_api_parse_tracklist(a->text);
  rb_build(c, sizeof(trackparse_tracklist), fill_tracklist_top, &l);
}

TRACKPARSE_API trackparse_tracklist *trackparse_parse_tracklist(const char *s, size_t len) {
  tracklist_arg a;
  a.text = in_str(s, len);
  a.o = NULL;
  return (trackparse_tracklist *)guarded_result(work_tracklist, &a);
}

TRACKPARSE_API trackparse_tracklist *
trackparse_parse_tracklist_with(const char *s, size_t len, const trackparse_tracklist_options *o) {
  tracklist_arg a;
  a.text = in_str(s, len);
  a.o = o != NULL ? &o->o : NULL;
  return (trackparse_tracklist *)guarded_result(work_tracklist, &a);
}

TRACKPARSE_API void trackparse_tracklist_free(trackparse_tracklist *t) { free_result(t); }

struct trackparse_edits {
  tp_arena *arena;
  tp_edit_vec v;
};

TRACKPARSE_API trackparse_edits *trackparse_edits_new(void) {
  return (trackparse_edits *)handle_new(sizeof(trackparse_edits));
}

TRACKPARSE_API void trackparse_edits_free(trackparse_edits *e) {
  if (e == NULL) return;
  tp_arena_free(e->arena);
  tp_sys_free(e);
}

/* What one edit constructor needs. */
typedef struct edit_arg {
  tp_edit_vec *v;
  int kind; /* index into edit_op_names; 1 with has_from / has_lines for the shift forms */
  int64_t line;
  double seconds;
  bool has_from;
  bool has_lines;
  const long long *lines;
  size_t count;
  tp_str text;
} edit_arg;

static void do_add_edit(void *arg) {
  edit_arg *a = (edit_arg *)arg;
  tp_edit_vec v = *a->v;
  tp_edit e;
  switch (a->kind) {
  case TRACKPARSE_EDIT_SET_TIME:
    e = tp_edit_set_time(a->line, a->seconds);
    break;
  case TRACKPARSE_EDIT_SHIFT:
    if (a->has_lines) {
      int64_t *lines = (int64_t *)tp_alloc_array(a->count, sizeof *lines);
      size_t i;
      for (i = 0; i < a->count; i++) lines[i] = (int64_t)a->lines[i];
      e = tp_edit_shift_lines(a->seconds, lines, a->count);
    } else if (a->has_from) {
      e = tp_edit_shift_from(a->seconds, a->line);
    } else {
      e = tp_edit_shift(a->seconds);
    }
    break;
  case TRACKPARSE_EDIT_SORT_BY_TIME:
    e = tp_edit_sort_by_time();
    break;
  case TRACKPARSE_EDIT_SET_REST:
    e = tp_edit_set_rest(a->line, copy_str(a->text));
    break;
  case TRACKPARSE_EDIT_INSERT:
    e = tp_edit_insert(a->line, copy_str(a->text));
    break;
  default:
    e = tp_edit_delete(a->line);
    break;
  }
  tp_push(v, e);
  *a->v = v;
}

static edit_arg edit_arg_of(trackparse_edits *e, int kind) {
  edit_arg a;
  memset(&a, 0, sizeof a);
  a.v = &e->v;
  a.kind = kind;
  a.text = TP_EMPTY;
  return a;
}

TRACKPARSE_API bool trackparse_edits_set_time(trackparse_edits *e, long long line, double seconds) {
  edit_arg a;
  if (e == NULL) return true;
  a = edit_arg_of(e, TRACKPARSE_EDIT_SET_TIME);
  a.line = (int64_t)line;
  a.seconds = seconds;
  return mutate(&e->arena, do_add_edit, &a);
}

TRACKPARSE_API bool trackparse_edits_shift(trackparse_edits *e, double seconds) {
  edit_arg a;
  if (e == NULL) return true;
  a = edit_arg_of(e, TRACKPARSE_EDIT_SHIFT);
  a.seconds = seconds;
  return mutate(&e->arena, do_add_edit, &a);
}

TRACKPARSE_API bool trackparse_edits_shift_from(trackparse_edits *e, double seconds,
                                                long long from) {
  edit_arg a;
  if (e == NULL) return true;
  a = edit_arg_of(e, TRACKPARSE_EDIT_SHIFT);
  a.seconds = seconds;
  a.has_from = true;
  a.line = (int64_t)from;
  return mutate(&e->arena, do_add_edit, &a);
}

TRACKPARSE_API bool trackparse_edits_shift_lines(trackparse_edits *e, double seconds,
                                                 const long long *lines, size_t count) {
  edit_arg a;
  if (e == NULL) return true;
  a = edit_arg_of(e, TRACKPARSE_EDIT_SHIFT);
  a.seconds = seconds;
  a.has_lines = true;
  a.lines = lines;
  a.count = lines != NULL ? count : 0;
  return mutate(&e->arena, do_add_edit, &a);
}

TRACKPARSE_API bool trackparse_edits_sort_by_time(trackparse_edits *e) {
  edit_arg a;
  if (e == NULL) return true;
  a = edit_arg_of(e, TRACKPARSE_EDIT_SORT_BY_TIME);
  return mutate(&e->arena, do_add_edit, &a);
}

TRACKPARSE_API bool trackparse_edits_set_rest(trackparse_edits *e, long long line, const char *rest,
                                              size_t len) {
  edit_arg a;
  if (e == NULL) return true;
  a = edit_arg_of(e, TRACKPARSE_EDIT_SET_REST);
  a.line = (int64_t)line;
  a.text = in_str(rest, len);
  return mutate(&e->arena, do_add_edit, &a);
}

TRACKPARSE_API bool trackparse_edits_insert(trackparse_edits *e, long long after, const char *text,
                                            size_t len) {
  edit_arg a;
  if (e == NULL) return true;
  a = edit_arg_of(e, TRACKPARSE_EDIT_INSERT);
  a.line = (int64_t)after;
  a.text = in_str(text, len);
  return mutate(&e->arena, do_add_edit, &a);
}

TRACKPARSE_API bool trackparse_edits_delete(trackparse_edits *e, long long line) {
  edit_arg a;
  if (e == NULL) return true;
  a = edit_arg_of(e, TRACKPARSE_EDIT_DELETE);
  a.line = (int64_t)line;
  return mutate(&e->arena, do_add_edit, &a);
}

typedef struct apply_arg {
  tp_str text;
  const trackparse_edits *e;
} apply_arg;

static void work_apply(call *c, void *arg) {
  const apply_arg *a = (const apply_arg *)arg;
  const tp_edit *edits = a->e != NULL ? a->e->v.p : NULL;
  size_t n = a->e != NULL ? a->e->v.n : 0;
  tp_edit_result r = tp_api_apply_edits_with(a->text, edits, n, NULL);
  rb_build(c, sizeof(trackparse_edit_result), fill_edit_result_top, &r);
}

TRACKPARSE_API trackparse_edit_result *trackparse_apply_edits(const char *s, size_t len,
                                                              const trackparse_edits *e) {
  apply_arg a;
  a.text = in_str(s, len);
  a.e = e;
  return (trackparse_edit_result *)guarded_result(work_apply, &a);
}

TRACKPARSE_API void trackparse_edit_result_free(trackparse_edit_result *r) { free_result(r); }

/* ============================================================================================ */
/* JSON                                                                                         */

static void work_track_json(call *c, void *arg) {
  const trackparse_track *t = (const trackparse_track *)arg;
  tp_track x;
  if (t == NULL) {
    build_string(c, TP_S("null"));
    return;
  }
  x = int_track(t);
  build_string(c, tp_track_json(&x));
}

static void work_artists_json(call *c, void *arg) {
  const trackparse_artists *a = (const trackparse_artists *)arg;
  if (a == NULL) {
    build_string(c, TP_S("null"));
    return;
  }
  build_string(c, tp_artists_json(int_artist_list(a->items, a->count)));
}

static void work_tracklist_json(call *c, void *arg) {
  const trackparse_tracklist *l = (const trackparse_tracklist *)arg;
  tp_tracklist x;
  if (l == NULL) {
    build_string(c, TP_S("null"));
    return;
  }
  x = int_tracklist(l);
  build_string(c, tp_tracklist_json(&x));
}

static void work_edit_result_json(call *c, void *arg) {
  const trackparse_edit_result *r = (const trackparse_edit_result *)arg;
  tp_edit_result x;
  if (r == NULL) {
    build_string(c, TP_S("null"));
    return;
  }
  x = int_edit_result(r);
  build_string(c, tp_edit_result_json(&x));
}

/* The arguments are read only; the casts drop const for the generic work signature. */
#define ARG(p) ((void *)(uintptr_t)(p))

TRACKPARSE_API trackparse_string *trackparse_track_to_json(const trackparse_track *t) {
  return (trackparse_string *)guarded_result(work_track_json, ARG(t));
}

TRACKPARSE_API trackparse_string *trackparse_artists_to_json(const trackparse_artists *a) {
  return (trackparse_string *)guarded_result(work_artists_json, ARG(a));
}

TRACKPARSE_API trackparse_string *trackparse_tracklist_to_json(const trackparse_tracklist *t) {
  return (trackparse_string *)guarded_result(work_tracklist_json, ARG(t));
}

TRACKPARSE_API trackparse_string *trackparse_edit_result_to_json(const trackparse_edit_result *r) {
  return (trackparse_string *)guarded_result(work_edit_result_json, ARG(r));
}

/* The _COUNT macros in trackparse.h must follow the enums. */
_Static_assert(TRACKPARSE_MODE_TRACKLIST + 1 == TRACKPARSE_MODE_COUNT, "mode count");
_Static_assert(TRACKPARSE_ROLE_PRODUCER + 1 == TRACKPARSE_ROLE_COUNT, "role count");
_Static_assert(TRACKPARSE_SOURCE_VERSION + 1 == TRACKPARSE_SOURCE_COUNT, "source count");
_Static_assert(TRACKPARSE_VERSION_NIGHTCORE + 1 == TRACKPARSE_VERSION_TYPE_COUNT, "version count");
_Static_assert(TRACKPARSE_DELIMITER_DASH + 1 == TRACKPARSE_DELIMITER_COUNT, "delimiter count");
_Static_assert(TRACKPARSE_JUNK_OTHER + 1 == TRACKPARSE_JUNK_KIND_COUNT, "junk count");
_Static_assert(TRACKPARSE_WARNING_UNBALANCED_BRACKETS + 1 == TRACKPARSE_WARNING_COUNT, "warnings");
_Static_assert(TRACKPARSE_ENTRY_BLANK + 1 == TRACKPARSE_ENTRY_KIND_COUNT, "entry count");
_Static_assert(TRACKPARSE_EOL_NONE + 1 == TRACKPARSE_EOL_COUNT, "eol count");
_Static_assert(TRACKPARSE_TL_LINE_UNPARSED + 1 == TRACKPARSE_TRACKLIST_WARNING_CODE_COUNT, "codes");
_Static_assert(TRACKPARSE_SEVERITY_WARNING + 1 == TRACKPARSE_SEVERITY_COUNT, "severity count");
_Static_assert(TRACKPARSE_EDIT_DELETE + 1 == TRACKPARSE_EDIT_OP_COUNT, "edit op count");
_Static_assert(TRACKPARSE_EDIT_WARNING_STYLE_UPGRADED + 1 == TRACKPARSE_EDIT_WARNING_CODE_COUNT,
               "edit warning count");
