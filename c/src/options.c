/* options.c: R12 options (go/options.go): the edit constructors, the resolved forms the pipeline
 * reads, and the two doors to them: the typed structs (tp_resolve_parse, ...) and loose JSON
 * (tp_resolve_parse_json, ...), which applies exactly the fallbacks of the JS reference (a bad
 * value means the default, R0.8). Resolution never fails. */
#include "internal.h"

/* isEmpty reports whether k adds no word, so the default tables can be shared. */
TP_PRIVATE bool tp_keywords_is_empty(const tp_keywords *k) {
  return k->version_heads.n == 0 && k->descriptors.n == 0 && k->genres.n == 0 &&
         k->junk.n == 0 && k->feat_markers.n == 0;
}

/* -------------------------------------------------------------------------------------------- */
/* Edit constructors                                                                            */

static tp_edit edit_of(tp_str op) {
  tp_edit e;
  memset(&e, 0, sizeof e);
  e.op = op;
  e.rest = TP_EMPTY;
  e.text = TP_EMPTY;
  return e;
}

/* SetTime rewrites the time of line, keeping its written style (R13.9.2). */
TP_PRIVATE tp_edit tp_edit_set_time(int64_t line, double seconds) {
  tp_edit e = edit_of(TP_EDIT_OP_SET_TIME);
  e.line = line;
  e.seconds = seconds;
  return e;
}

/* Shift adds seconds to every timed line. */
TP_PRIVATE tp_edit tp_edit_shift(double seconds) {
  tp_edit e = edit_of(TP_EDIT_OP_SHIFT);
  e.seconds = seconds;
  return e;
}

/* ShiftFrom adds seconds to every timed line from line from on. */
TP_PRIVATE tp_edit tp_edit_shift_from(double seconds, int64_t from) {
  tp_edit e = edit_of(TP_EDIT_OP_SHIFT);
  int64_t *f = (int64_t *)tp_alloc(sizeof *f);
  *f = from;
  e.seconds = seconds;
  e.from = f;
  return e;
}

/* ShiftLines adds seconds to the given lines only (Go append([]int{}, lines...): never nil). */
TP_PRIVATE tp_edit tp_edit_shift_lines(double seconds, const int64_t *lines, size_t count) {
  tp_edit e = edit_of(TP_EDIT_OP_SHIFT);
  size_t i;
  e.seconds = seconds;
  e.has_lines = true;
  for (i = 0; i < count; i++) tp_push(e.lines, lines[i]);
  return e;
}

/* SortByTime sorts the timed lines stably, each with the untimed lines after it (R13.9.4). */
TP_PRIVATE tp_edit tp_edit_sort_by_time(void) { return edit_of(TP_EDIT_OP_SORT_BY_TIME); }

/* SetRest replaces everything after the time and gap of line. */
TP_PRIVATE tp_edit tp_edit_set_rest(int64_t line, tp_str rest) {
  tp_edit e = edit_of(TP_EDIT_OP_SET_REST);
  e.line = line;
  e.rest = rest;
  return e;
}

/* Insert inserts text as a new line after line after (0: at the start). */
TP_PRIVATE tp_edit tp_edit_insert(int64_t after, tp_str text) {
  tp_edit e = edit_of(TP_EDIT_OP_INSERT);
  e.after = after;
  e.text = text;
  return e;
}

/* Delete removes line. */
TP_PRIVATE tp_edit tp_edit_delete(int64_t line) {
  tp_edit e = edit_of(TP_EDIT_OP_DELETE);
  e.line = line;
  return e;
}

/* -------------------------------------------------------------------------------------------- */
/* Resolved forms                                                                               */

/* modeByName is a mode for one of the four names of a parsed mode. */
TP_PRIVATE bool tp_mode_by_name(tp_str name, tp_mode *out) {
  if (tp_str_eq(name, TP_MODE_CLEAN) || tp_str_eq(name, TP_MODE_YOUTUBE) ||
      tp_str_eq(name, TP_MODE_FILENAME) || tp_str_eq(name, TP_MODE_TRACKLIST)) {
    *out = name;
    return true;
  }
  return false;
}

static tp_requested_mode request(tp_request_kind kind, tp_mode mode) {
  tp_requested_mode r;
  r.kind = kind;
  r.mode = mode;
  return r;
}

/* requestFor is the R2 request for a mode name given as a string. */
TP_PRIVATE tp_requested_mode tp_request_for(tp_str name) {
  tp_mode m;
  if (tp_str_eq(name, TP_S("auto"))) return request(TP_REQUEST_AUTO, TP_EMPTY);
  if (tp_mode_by_name(name, &m)) return request(TP_REQUEST_MODE, m);
  return request(TP_REQUEST_OTHER, TP_EMPTY);
}

/* parseSplitAnd is options.splitAnd of a parse: anything but auto and never (or missing)
 * behaves as always in JS. */
TP_PRIVATE tp_split_and_mode tp_parse_split_and(tp_str name) {
  if (tp_str_eq(name, TP_S("auto"))) return TP_SPLIT_AUTO;
  if (tp_str_eq(name, TP_S("never"))) return TP_SPLIT_NEVER;
  return TP_SPLIT_ALWAYS;
}

/* tracklistSplitAnd is options.splitAnd of a tracklist: only always and never count. */
TP_PRIVATE tp_split_and_mode tp_tracklist_split_and(tp_str name) {
  if (tp_str_eq(name, TP_S("always"))) return TP_SPLIT_ALWAYS;
  if (tp_str_eq(name, TP_S("never"))) return TP_SPLIT_NEVER;
  return TP_SPLIT_AUTO;
}

/* tracklistRequest is options.mode of a tracklist: a mode name or auto; anything else means
 * tracklist. */
TP_PRIVATE tp_requested_mode tp_tracklist_request(tp_str name) {
  tp_mode m;
  if (tp_str_eq(name, TP_S("auto"))) return request(TP_REQUEST_AUTO, TP_EMPTY);
  if (tp_mode_by_name(name, &m)) return request(TP_REQUEST_MODE, m);
  return request(TP_REQUEST_MODE, TP_MODE_TRACKLIST);
}

/* arrayIndex reports whether key is a canonical array index ("0", "17"; not "01"), which JS
 * lists first among an object's keys, in ascending order. */
TP_PRIVATE bool tp_array_index(tp_str key, uint64_t *out) {
  uint64_t n = 0;
  size_t i;
  if (key.n == 0 || key.n > 10) return false;
  if (key.n > 1 && key.p[0] == '0') return false;
  for (i = 0; i < key.n; i++) {
    unsigned char c = (unsigned char)key.p[i];
    if (c < '0' || c > '9') return false;
    n = n * 10 + (uint64_t)(c - '0');
  }
  if (n > 0xfffffffeu) return false;
  *out = n;
  return true;
}

typedef struct indexed_kv {
  uint64_t n;
  tp_kv p;
} indexed_kv;
typedef TP_VEC(indexed_kv) indexed_kv_vec;

static int by_index(const void *a, const void *b, void *ctx) {
  uint64_t x = ((const indexed_kv *)a)->n, y = ((const indexed_kv *)b)->n;
  (void)ctx;
  return x < y ? -1 : x > y ? 1 : 0;
}

/* jsEntries is the Object.entries order of an object built from pairs: a repeated key keeps its
 * first place and takes the last value; integer-like keys come first in ascending order, then
 * the others in insertion order. Go sorts the (unique) integer keys by insertion; a stable sort
 * gives the same order. */
TP_PRIVATE tp_kv_vec tp_js_entries(tp_kv_vec pairs) {
  tp_kv_vec unique = {0, 0, 0}, rest = {0, 0, 0}, out = {0, 0, 0};
  indexed_kv_vec ints = {0, 0, 0};
  tp_map index;
  size_t i;
  memset(&index, 0, sizeof index);
  for (i = 0; i < pairs.n; i++) {
    int64_t at;
    if (tp_map_get(&index, pairs.p[i].key, &at)) {
      unique.p[at].value = pairs.p[i].value;
      continue;
    }
    tp_map_put(&index, pairs.p[i].key, (int64_t)unique.n);
    tp_push(unique, pairs.p[i]);
  }
  for (i = 0; i < unique.n; i++) {
    uint64_t n;
    if (tp_array_index(unique.p[i].key, &n)) {
      indexed_kv x;
      x.n = n;
      x.p = unique.p[i];
      tp_push(ints, x);
    } else {
      tp_push(rest, unique.p[i]);
    }
  }
  if (ints.n > 1) tp_sort_stable(ints.p, ints.n, sizeof *ints.p, by_index, NULL);
  for (i = 0; i < ints.n; i++) tp_push(out, ints.p[i].p);
  tp_append(out, rest);
  return out;
}

/* sanitizeAll is tp_sanitize over a list (a fresh vector, never the caller's). */
TP_PRIVATE tp_strvec tp_sanitize_all(tp_strvec list) {
  tp_strvec out = {0, 0, 0};
  size_t i;
  if (list.n == 0) return out;
  out.p = (tp_str *)tp_alloc_array(list.n, sizeof *out.p);
  out.n = out.cap = list.n;
  for (i = 0; i < list.n; i++) out.p[i] = tp_sanitize(list.p[i]);
  return out;
}

static tp_kv kv_of(tp_str key, tp_str value) {
  tp_kv p;
  p.key = key;
  p.value = value;
  return p;
}

/* resolveKeywords is the typed keywords; NULL when they add nothing. */
TP_PRIVATE const tp_keyword_lists *tp_resolve_keywords(const tp_keywords *k) {
  tp_kv_vec heads = {0, 0, 0}, junk = {0, 0, 0};
  tp_keyword_lists *out;
  size_t i;
  if (tp_keywords_is_empty(k)) return NULL;
  for (i = 0; i < k->version_heads.n; i++) {
    const tp_version_head *h = &k->version_heads.p[i];
    tp_push(heads, kv_of(tp_sanitize(h->key), tp_sanitize(h->type)));
  }
  for (i = 0; i < k->junk.n; i++) {
    const tp_junk_phrase *j = &k->junk.p[i];
    tp_push(junk, kv_of(tp_sanitize(j->phrase), tp_sanitize(j->kind)));
  }
  out = (tp_keyword_lists *)tp_alloc(sizeof *out);
  out->version_heads = tp_js_entries(heads);
  out->descriptors = tp_sanitize_all(k->descriptors);
  out->genres = tp_sanitize_all(k->genres);
  out->junk = tp_js_entries(junk);
  out->feat_markers = tp_sanitize_all(k->feat_markers);
  return out;
}

/* resolveParse is the typed door to resolvedOptions. o NULL is the zero value. */
TP_PRIVATE tp_resolved_options tp_resolve_parse(const tp_parse_options *o) {
  tp_resolved_options r;
  memset(&r, 0, sizeof r);
  r.uploader = TP_EMPTY;
  if (o == NULL) return r;
  r.uploader = tp_sanitize(o->uploader);
  r.known_artists = tp_sanitize_all(o->known_artists);
  r.keywords = tp_resolve_keywords(&o->keywords);
  if (o->mode.n != 0) r.mode = tp_request_for(o->mode);
  if (o->split_and.n != 0) r.split_and = tp_parse_split_and(o->split_and);
  return r;
}

/* resolveFormat is the typed door to resolvedFormat. o NULL is the zero value. */
TP_PRIVATE tp_resolved_format tp_resolve_format(const tp_format_options *o) {
  tp_resolved_format r;
  tp_str feat;
  memset(&r, 0, sizeof r);
  if (o == NULL) {
    r.feat = TP_FEAT_MODE_SOURCE;
    r.versions = true;
    r.producers = true;
    return r;
  }
  r.canonical = tp_str_eq(o->joiners, TP_JOINERS_CANONICAL);
  r.versions = o->versions.n == 0 || tp_str_eq(o->versions, TP_VERSIONS_ALL);
  r.producers = !o->omit_producers;
  r.position = o->position;
  feat = o->feat;
  if (feat.n == 0 || tp_str_eq(feat, TP_FEAT_SOURCE)) {
    r.feat = TP_FEAT_MODE_SOURCE;
  } else if (tp_str_eq(feat, TP_FEAT_ARTIST)) {
    r.feat = TP_FEAT_MODE_ARTIST;
  } else if (tp_str_eq(feat, TP_FEAT_TITLE)) {
    r.feat = TP_FEAT_MODE_TITLE;
  } else {
    r.feat = TP_FEAT_MODE_OMIT;
  }
  if (o->feat_marker != NULL) {
    tp_str *m = (tp_str *)tp_alloc(sizeof *m);
    *m = tp_sanitize(*o->feat_marker);
    r.feat_marker = m;
  }
  return r;
}

/* audio is the R13.10 audio length: NULL unless given, finite and not negative. */
TP_PRIVATE const double *tp_audio(const double *seconds) {
  double v, *out;
  if (seconds == NULL) return NULL;
  v = *seconds;
  if (tp_is_nan(v) || tp_is_inf(v) || v < 0) return NULL;
  out = (double *)tp_alloc(sizeof *out);
  *out = v;
  return out;
}

/* resolveTracklist is the typed door to resolvedTracklist. o NULL is the zero value. */
TP_PRIVATE tp_resolved_tracklist tp_resolve_tracklist(const tp_tracklist_options *o) {
  tp_resolved_tracklist r;
  memset(&r, 0, sizeof r);
  r.parse.uploader = TP_EMPTY;
  r.parse.mode = request(TP_REQUEST_MODE, TP_MODE_TRACKLIST);
  if (o == NULL) {
    r.parse.split_and = TP_SPLIT_AUTO;
    return r;
  }
  r.parse.known_artists = tp_sanitize_all(o->known_artists);
  r.parse.keywords = tp_resolve_keywords(&o->keywords);
  r.audio_seconds = tp_audio(o->audio_seconds);
  r.marker_words = tp_sanitize_all(o->marker_words);
  if (o->mode.n != 0) r.parse.mode = tp_tracklist_request(o->mode);
  r.parse.split_and = tp_tracklist_split_and(o->split_and);
  return r;
}

/* -------------------------------------------------------------------------------------------- */
/* The JSON door (wire/): loose values with the JS fallbacks.                                   */

/* stringList is an option list as JS reads it at run time (R0.8): only an array's strings
 * count. */
TP_PRIVATE tp_strvec tp_string_list(tp_json v, bool ok) {
  tp_strvec out = {0, 0, 0};
  size_t i;
  if (!ok || v.kind != TP_JSON_ARRAY) return out;
  for (i = 0; i < v.arr_n; i++) {
    if (v.arr[i].kind == TP_JSON_STRING) tp_push(out, v.arr[i].str);
  }
  return out;
}

/* stringEntries is an option map as JS reads it at run time (R0.8): only a plain object's
 * string values count, in Object.entries order. */
TP_PRIVATE tp_kv_vec tp_string_entries(tp_json v, bool ok) {
  tp_kv_vec pairs = {0, 0, 0};
  size_t i;
  if (!ok || v.kind != TP_JSON_OBJECT) return pairs;
  for (i = 0; i < v.obj_n; i++) {
    const tp_json_member *m = &v.obj[i];
    if (m->val.kind == TP_JSON_STRING) tp_push(pairs, kv_of(m->key, m->val.str));
  }
  /* The reader already merged repeated keys; jsEntries only reorders integer-like keys. */
  return tp_js_entries(pairs);
}

/* Go v.get(key) as the (value, ok) pair the list readers take. */
static tp_json get(tp_json v, const char *key, bool *ok) {
  tp_json out = tp_json_null();
  *ok = tp_json_get(v, tp_str_c(key), &out);
  return out;
}

/* keywordsJSON is options.keywords from loose JSON (a plain object only). Never NULL, even when
 * it adds nothing (Go returns a non-nil pointer: fresh tables). */
TP_PRIVATE const tp_keyword_lists *tp_keywords_json(tp_json v) {
  tp_keyword_lists *k = (tp_keyword_lists *)tp_alloc(sizeof *k);
  tp_json x;
  bool ok;
  x = get(v, "versionHeads", &ok);
  k->version_heads = tp_string_entries(x, ok);
  x = get(v, "descriptors", &ok);
  k->descriptors = tp_string_list(x, ok);
  x = get(v, "genres", &ok);
  k->genres = tp_string_list(x, ok);
  x = get(v, "junk", &ok);
  k->junk = tp_string_entries(x, ok);
  x = get(v, "featMarkers", &ok);
  k->feat_markers = tp_string_list(x, ok);
  return k;
}

/* resolveParseJSON is the JSON door to resolvedOptions, with the JS fallbacks. */
TP_PRIVATE tp_resolved_options tp_resolve_parse_json(tp_json o) {
  tp_resolved_options r;
  tp_json m;
  bool ok;
  memset(&r, 0, sizeof r);
  r.uploader = TP_EMPTY;
  if (o.kind != TP_JSON_OBJECT) return r; /* safeOptions: not an object */
  /* `options.mode ?? "auto"` */
  if (tp_json_get(o, TP_S("mode"), &m) && m.kind != TP_JSON_NULL) {
    if (m.kind == TP_JSON_STRING) {
      r.mode = tp_request_for(m.str);
    } else {
      r.mode = request(TP_REQUEST_OTHER, TP_EMPTY);
    }
  }
  /* `options.splitAnd ?? "auto"` */
  if (tp_json_get(o, TP_S("splitAnd"), &m) && m.kind != TP_JSON_NULL) {
    if (m.kind == TP_JSON_STRING) {
      r.split_and = tp_parse_split_and(m.str);
    } else {
      r.split_and = TP_SPLIT_ALWAYS;
    }
  }
  /* `options.keywords ? ... : baseTables`, then isRecord: only a plain object adds words. */
  if (tp_json_get(o, TP_S("keywords"), &m) && m.kind == TP_JSON_OBJECT) {
    r.keywords = tp_keywords_json(m);
  }
  if (tp_json_get(o, TP_S("uploader"), &m) && m.kind == TP_JSON_STRING) r.uploader = m.str;
  m = get(o, "knownArtists", &ok);
  r.known_artists = tp_string_list(m, ok);
  return r;
}

/* truthy is JS truthiness of a JSON value. */
TP_PRIVATE bool tp_truthy(tp_json v) {
  switch (v.kind) {
  case TP_JSON_NULL:
    return false;
  case TP_JSON_BOOL:
    return v.b;
  case TP_JSON_NUMBER:
    return v.num != 0 && !tp_is_nan(v.num);
  case TP_JSON_STRING:
    return v.str.n != 0;
  default:
    return true;
  }
}

/* jsString into a builder. Recursive over nested arrays only; the depth is bounded by the
 * reader's TP_JSON_MAX_DEPTH. */
static void js_string_to(tp_buf *b, tp_json v) {
  size_t i;
  switch (v.kind) {
  case TP_JSON_NULL:
    tp_buf_cstr(b, "null");
    return;
  case TP_JSON_BOOL:
    tp_buf_cstr(b, v.b ? "true" : "false");
    return;
  case TP_JSON_NUMBER:
    /* Number#toString: NaN, Infinity and -Infinity included. */
    tp_buf_number(b, v.num);
    return;
  case TP_JSON_STRING:
    tp_buf_str(b, v.str);
    return;
  case TP_JSON_ARRAY:
    /* Array.prototype.join: null elements are empty. */
    for (i = 0; i < v.arr_n; i++) {
      if (i > 0) tp_buf_byte(b, ',');
      if (v.arr[i].kind != TP_JSON_NULL) js_string_to(b, v.arr[i]);
    }
    return;
  default:
    tp_buf_cstr(b, "[object Object]");
    return;
  }
}

/* jsString is JS String(value), what a template literal does with a non-string featMarker. */
TP_PRIVATE tp_str tp_js_string(tp_json v) {
  tp_buf b;
  if (v.kind == TP_JSON_STRING) return v.str;
  memset(&b, 0, sizeof b);
  js_string_to(&b, v);
  return tp_buf_done(&b);
}

/* The Go closure get of resolveFormatJSON: the value at key, missing and null alike false
 * (`??`). */
static bool get_set(tp_json o, const char *key, tp_json *out) {
  tp_json v;
  if (!tp_json_get(o, tp_str_c(key), &v) || v.kind == TP_JSON_NULL) return false;
  *out = v;
  return true;
}

static bool is_string(tp_json v, const char *s) {
  return v.kind == TP_JSON_STRING && tp_str_eq_c(v.str, s);
}

/* resolveFormatJSON is the JSON door to resolvedFormat (`??` for missing and null). */
TP_PRIVATE tp_resolved_format tp_resolve_format_json(tp_json o) {
  tp_resolved_format r;
  tp_json v;
  memset(&r, 0, sizeof r);
  r.feat = TP_FEAT_MODE_SOURCE;
  r.versions = true;
  r.producers = true;
  if (get_set(o, "feat", &v)) {
    if (is_string(v, "source")) {
      r.feat = TP_FEAT_MODE_SOURCE;
    } else if (is_string(v, "artist")) {
      r.feat = TP_FEAT_MODE_ARTIST;
    } else if (is_string(v, "title")) {
      r.feat = TP_FEAT_MODE_TITLE;
    } else {
      r.feat = TP_FEAT_MODE_OMIT;
    }
  }
  /* `o.featMarker !== undefined`: an explicit null is rendered as "null". */
  if (tp_json_get(o, TP_S("featMarker"), &v)) {
    tp_str *m = (tp_str *)tp_alloc(sizeof *m);
    *m = tp_js_string(v);
    r.feat_marker = m;
  }
  if (get_set(o, "joiners", &v)) r.canonical = is_string(v, "canonical");
  if (get_set(o, "versions", &v)) r.versions = is_string(v, "all");
  if (get_set(o, "producers", &v)) r.producers = tp_truthy(v);
  if (get_set(o, "position", &v)) r.position = tp_truthy(v);
  return r;
}

/* jsNumber is a JS number view of a JSON value: true only for a number (a bool is not one). The
 * value may be NaN or infinite; callers apply their own checks. */
TP_PRIVATE bool tp_js_number(tp_json v, bool present, double *out) {
  if (!present || v.kind != TP_JSON_NUMBER) return false;
  *out = v.num;
  return true;
}

/* resolveTracklistJSON is the JSON door to resolvedTracklist. mode, splitAnd, knownArtists and
 * keywords are passed on only when well-formed; uploader is not a tracklist option. */
TP_PRIVATE tp_resolved_tracklist tp_resolve_tracklist_json(tp_json o) {
  tp_resolved_tracklist r;
  tp_json m;
  bool ok;
  double n;
  memset(&r, 0, sizeof r);
  r.parse = tp_resolve_parse_json(o);
  r.parse.uploader = TP_EMPTY;
  r.parse.mode = request(TP_REQUEST_MODE, TP_MODE_TRACKLIST);
  if (tp_json_get(o, TP_S("mode"), &m) && m.kind == TP_JSON_STRING) {
    r.parse.mode = tp_tracklist_request(m.str);
  }
  r.parse.split_and = TP_SPLIT_AUTO;
  if (tp_json_get(o, TP_S("splitAnd"), &m) && m.kind == TP_JSON_STRING) {
    r.parse.split_and = tp_tracklist_split_and(m.str);
  }
  m = get(o, "audioSeconds", &ok);
  if (tp_js_number(m, ok, &n)) r.audio_seconds = tp_audio(&n);
  m = get(o, "markerWords", &ok);
  r.marker_words = tp_string_list(m, ok);
  return r;
}
