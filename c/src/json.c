/* json.c: the writer half of go/json.go. Bytes equal to JavaScript's JSON.stringify for every
 * output type, plus the edit's JSON value (Go jsonOf) built directly, since the library has no
 * JSON reader (wire/json_read.c is the reader half). */
#include "internal.h"

/* -------------------------------------------------------------------------------------------- */
/* Writer                                                                                       */

/* appendJSString appends s as JSON.stringify writes a string: only ", \ and the code points
 * below U+0020 are escaped, with the short forms \b \f \n \r \t. Bytes that are not valid UTF-8
 * are replaced as the API replaces them (tp_sanitize). After that every byte of a multi-byte
 * sequence is >= 0x80, so escaping byte by byte is escaping rune by rune. */
TP_PRIVATE void tp_append_js_string(tp_buf *b, tp_str s) {
  static const char hex[] = "0123456789abcdef";
  size_t i, run = 0;
  s = tp_sanitize(s);
  tp_buf_byte(b, '"');
  for (i = 0; i < s.n; i++) {
    unsigned char c = (unsigned char)s.p[i];
    const char *esc = NULL;
    if (c >= 0x20 && c != '"' && c != '\\') continue;
    if (i > run) tp_buf_bytes(b, s.p + run, i - run);
    run = i + 1;
    switch (c) {
    case '"':
      esc = "\\\"";
      break;
    case '\\':
      esc = "\\\\";
      break;
    case '\n':
      esc = "\\n";
      break;
    case '\r':
      esc = "\\r";
      break;
    case '\t':
      esc = "\\t";
      break;
    case '\b':
      esc = "\\b";
      break;
    case '\f':
      esc = "\\f";
      break;
    default:
      break;
    }
    if (esc != NULL) {
      tp_buf_cstr(b, esc);
    } else {
      char u[6];
      u[0] = '\\';
      u[1] = 'u';
      u[2] = '0';
      u[3] = '0';
      u[4] = hex[c >> 4];
      u[5] = hex[c & 0xf];
      tp_buf_bytes(b, u, sizeof u);
    }
  }
  if (s.n > run) tp_buf_bytes(b, s.p + run, s.n - run);
  tp_buf_byte(b, '"');
}

/* appendJSNumber appends n as Number#toString writes it (shortest round-trip digits, exponent
 * form from 1e21 and below 1e-6, -0 as 0); NaN and the infinities as null, as JSON.stringify. */
TP_PRIVATE void tp_append_js_number(tp_buf *b, double n) {
  if (tp_is_nan(n) || tp_is_inf(n)) {
    tp_buf_cstr(b, "null");
    return;
  }
  tp_buf_number(b, n);
}

TP_PRIVATE tp_jw tp_jw_new(void) {
  tp_jw w;
  memset(&w, 0, sizeof w);
  w.first = true;
  return w;
}

TP_PRIVATE tp_str tp_jw_done(tp_jw *w) { return tp_buf_done(&w->b); }

TP_PRIVATE void tp_jw_sep(tp_jw *w) {
  if (!w->first) tp_buf_byte(&w->b, ',');
  w->first = false;
}

TP_PRIVATE void tp_jw_open(tp_jw *w, char c) {
  tp_jw_sep(w);
  tp_buf_byte(&w->b, c);
  w->first = true;
}

TP_PRIVATE void tp_jw_close(tp_jw *w, char c) {
  tp_buf_byte(&w->b, c);
  w->first = false;
}

/* key writes an object key; the value that follows must not call sep again. */
TP_PRIVATE void tp_jw_key(tp_jw *w, tp_str k) {
  tp_jw_sep(w);
  tp_append_js_string(&w->b, k);
  tp_buf_byte(&w->b, ':');
  w->first = true;
}

TP_PRIVATE void tp_jw_str(tp_jw *w, tp_str s) {
  tp_jw_sep(w);
  tp_append_js_string(&w->b, s);
}

TP_PRIVATE void tp_jw_num(tp_jw *w, double n) {
  tp_jw_sep(w);
  tp_append_js_number(&w->b, n);
}

TP_PRIVATE void tp_jw_integer(tp_jw *w, int64_t n) {
  tp_jw_sep(w);
  tp_append_js_number(&w->b, (double)n);
}

TP_PRIVATE void tp_jw_null(tp_jw *w) {
  tp_jw_sep(w);
  tp_buf_cstr(&w->b, "null");
}

TP_PRIVATE void tp_jw_boolean(tp_jw *w, bool v) {
  tp_jw_sep(w);
  tp_buf_cstr(&w->b, v ? "true" : "false");
}

TP_PRIVATE void tp_jw_opt_str(tp_jw *w, const tp_str *s) {
  if (s == NULL) {
    tp_jw_null(w);
  } else {
    tp_jw_str(w, *s);
  }
}

TP_PRIVATE void tp_jw_opt_int(tp_jw *w, const ptrdiff_t *n) {
  if (n == NULL) {
    tp_jw_null(w);
  } else {
    tp_jw_integer(w, (int64_t)*n);
  }
}

TP_PRIVATE void tp_jw_opt_int64(tp_jw *w, const int64_t *n) {
  if (n == NULL) {
    tp_jw_null(w);
  } else {
    tp_jw_integer(w, *n);
  }
}

TP_PRIVATE void tp_jw_opt_num(tp_jw *w, const double *n) {
  if (n == NULL) {
    tp_jw_null(w);
  } else {
    tp_jw_num(w, *n);
  }
}

TP_PRIVATE void tp_jw_strings(tp_jw *w, tp_strvec list) {
  size_t i;
  tp_jw_open(w, '[');
  for (i = 0; i < list.n; i++) tp_jw_str(w, list.p[i]);
  tp_jw_close(w, ']');
}

TP_PRIVATE void tp_jw_ints(tp_jw *w, tp_intvec list) {
  size_t i;
  tp_jw_open(w, '[');
  for (i = 0; i < list.n; i++) tp_jw_integer(w, list.p[i]);
  tp_jw_close(w, ']');
}

/* -------------------------------------------------------------------------------------------- */
/* The result types. Keys in the schema's order, every key present, empty vectors as [].        */

#define KEY(w, k) tp_jw_key((w), TP_S(k))

TP_PRIVATE void tp_artist_write(const tp_artist *a, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "name");
  tp_jw_str(w, a->name);
  KEY(w, "role");
  tp_jw_str(w, a->role);
  KEY(w, "joiner");
  tp_jw_opt_str(w, a->joiner);
  KEY(w, "source");
  tp_jw_str(w, a->source);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_write_artists(tp_jw *w, tp_artist_vec list) {
  size_t i;
  tp_jw_open(w, '[');
  for (i = 0; i < list.n; i++) tp_artist_write(&list.p[i], w);
  tp_jw_close(w, ']');
}

TP_PRIVATE void tp_version_write(const tp_version *v, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "type");
  tp_jw_str(w, v->type);
  KEY(w, "raw");
  tp_jw_str(w, v->raw);
  KEY(w, "artists");
  tp_write_artists(w, v->artists);
  KEY(w, "modifiers");
  tp_jw_strings(w, v->modifiers);
  KEY(w, "descriptor");
  tp_jw_opt_str(w, v->descriptor);
  KEY(w, "year");
  tp_jw_opt_int(w, v->year);
  KEY(w, "unknownArtist");
  tp_jw_boolean(w, v->unknown_artist);
  KEY(w, "delimiter");
  tp_jw_str(w, v->delimiter);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_junk_write(const tp_junk *j, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "raw");
  tp_jw_str(w, j->raw);
  KEY(w, "kind");
  tp_jw_str(w, j->kind);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_flags_write(const tp_flags *f, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "explicit");
  tp_jw_boolean(w, f->explicit_);
  KEY(w, "clean");
  tp_jw_boolean(w, f->clean);
  KEY(w, "unknownArtist");
  tp_jw_boolean(w, f->unknown_artist);
  KEY(w, "unknownTitle");
  tp_jw_boolean(w, f->unknown_title);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_position_write(const tp_position *p, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "raw");
  tp_jw_str(w, p->raw);
  KEY(w, "number");
  tp_jw_integer(w, (int64_t)p->number);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_timestamp_write(const tp_timestamp *t, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "raw");
  tp_jw_str(w, t->raw);
  KEY(w, "seconds");
  tp_jw_integer(w, (int64_t)t->seconds);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_track_write(const tp_track *t, tp_jw *w) {
  size_t i;
  tp_jw_open(w, '{');
  KEY(w, "input");
  tp_jw_str(w, t->input);
  KEY(w, "mode");
  tp_jw_str(w, t->mode);
  KEY(w, "position");
  if (t->position == NULL) {
    tp_jw_null(w);
  } else {
    tp_position_write(t->position, w);
  }
  KEY(w, "timestamp");
  if (t->timestamp == NULL) {
    tp_jw_null(w);
  } else {
    tp_timestamp_write(t->timestamp, w);
  }
  KEY(w, "artists");
  tp_write_artists(w, t->artists);
  KEY(w, "title");
  tp_jw_str(w, t->title);
  KEY(w, "fullTitle");
  tp_jw_str(w, t->full_title);
  KEY(w, "versions");
  tp_jw_open(w, '[');
  for (i = 0; i < t->versions.n; i++) tp_version_write(&t->versions.p[i], w);
  tp_jw_close(w, ']');
  KEY(w, "year");
  tp_jw_opt_int(w, t->year);
  KEY(w, "label");
  tp_jw_opt_str(w, t->label);
  KEY(w, "flags");
  tp_flags_write(&t->flags, w);
  KEY(w, "junk");
  tp_jw_open(w, '[');
  for (i = 0; i < t->junk.n; i++) tp_junk_write(&t->junk.p[i], w);
  tp_jw_close(w, ']');
  KEY(w, "warnings");
  tp_jw_strings(w, t->warnings);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_parts_write(const tp_parts *p, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "lead");
  tp_jw_str(w, p->lead);
  KEY(w, "time");
  tp_jw_str(w, p->time);
  KEY(w, "gap");
  tp_jw_str(w, p->gap);
  KEY(w, "rest");
  tp_jw_str(w, p->rest);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_tracklist_time_write(const tp_tracklist_time *t, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "raw");
  tp_jw_str(w, t->raw);
  KEY(w, "seconds");
  tp_jw_num(w, t->seconds);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_entry_write(const tp_entry *e, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "line");
  tp_jw_integer(w, (int64_t)e->line);
  KEY(w, "kind");
  tp_jw_str(w, e->kind);
  KEY(w, "eol");
  tp_jw_str(w, e->eol);
  KEY(w, "raw");
  tp_jw_str(w, e->raw);
  KEY(w, "parts");
  tp_parts_write(&e->parts, w);
  KEY(w, "time");
  if (e->time == NULL) {
    tp_jw_null(w);
  } else {
    tp_tracklist_time_write(e->time, w);
  }
  KEY(w, "track");
  if (e->track == NULL) {
    tp_jw_null(w);
  } else {
    tp_track_write(e->track, w);
  }
  KEY(w, "layered");
  tp_jw_boolean(w, e->layered);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_suggestion_write(const tp_suggestion *s, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "line");
  tp_jw_integer(w, (int64_t)s->line);
  KEY(w, "raw");
  tp_jw_str(w, s->raw);
  KEY(w, "seconds");
  tp_jw_num(w, s->seconds);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_tracklist_warning_write(const tp_tracklist_warning *x, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "code");
  tp_jw_str(w, x->code);
  KEY(w, "line");
  tp_jw_integer(w, (int64_t)x->line);
  KEY(w, "severity");
  tp_jw_str(w, x->severity);
  KEY(w, "suggestion");
  if (x->suggestion == NULL) {
    tp_jw_null(w);
  } else {
    tp_suggestion_write(x->suggestion, w);
  }
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_stats_write(const tp_stats *s, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "lines");
  tp_jw_integer(w, (int64_t)s->lines);
  KEY(w, "tracks");
  tp_jw_integer(w, (int64_t)s->tracks);
  KEY(w, "markers");
  tp_jw_integer(w, (int64_t)s->markers);
  KEY(w, "timed");
  tp_jw_integer(w, (int64_t)s->timed);
  KEY(w, "firstTime");
  tp_jw_opt_num(w, s->first_time);
  KEY(w, "lastTime");
  tp_jw_opt_num(w, s->last_time);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_tracklist_write(const tp_tracklist *t, tp_jw *w) {
  size_t i;
  tp_jw_open(w, '{');
  KEY(w, "specVersion");
  tp_jw_str(w, t->spec_version);
  KEY(w, "entries");
  tp_jw_open(w, '[');
  for (i = 0; i < t->entries.n; i++) tp_entry_write(&t->entries.p[i], w);
  tp_jw_close(w, ']');
  KEY(w, "warnings");
  tp_jw_open(w, '[');
  for (i = 0; i < t->warnings.n; i++) tp_tracklist_warning_write(&t->warnings.p[i], w);
  tp_jw_close(w, ']');
  KEY(w, "stats");
  tp_stats_write(&t->stats, w);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_edit_write(const tp_edit *e, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "op");
  tp_jw_str(w, e->op);
  if (tp_str_eq(e->op, TP_EDIT_OP_SET_TIME)) {
    KEY(w, "line");
    tp_jw_integer(w, e->line);
    KEY(w, "seconds");
    tp_jw_num(w, e->seconds);
  } else if (tp_str_eq(e->op, TP_EDIT_OP_SHIFT)) {
    KEY(w, "seconds");
    tp_jw_num(w, e->seconds);
    if (e->has_lines) {
      KEY(w, "lines");
      tp_jw_ints(w, e->lines);
    }
    if (e->from != NULL) {
      KEY(w, "from");
      tp_jw_integer(w, *e->from);
    }
  } else if (tp_str_eq(e->op, TP_EDIT_OP_SET_REST)) {
    KEY(w, "line");
    tp_jw_integer(w, e->line);
    KEY(w, "rest");
    tp_jw_str(w, e->rest);
  } else if (tp_str_eq(e->op, TP_EDIT_OP_INSERT)) {
    KEY(w, "after");
    tp_jw_integer(w, e->after);
    KEY(w, "text");
    tp_jw_str(w, e->text);
  } else if (tp_str_eq(e->op, TP_EDIT_OP_DELETE)) {
    KEY(w, "line");
    tp_jw_integer(w, e->line);
  }
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_edit_warning_write(const tp_edit_warning *x, tp_jw *w) {
  tp_jw_open(w, '{');
  KEY(w, "code");
  tp_jw_str(w, x->code);
  KEY(w, "edit");
  tp_jw_integer(w, (int64_t)x->edit);
  KEY(w, "line");
  tp_jw_opt_int64(w, x->line);
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_edit_result_write(const tp_edit_result *r, tp_jw *w) {
  size_t i;
  tp_jw_open(w, '{');
  KEY(w, "text");
  tp_jw_str(w, r->text);
  KEY(w, "changed");
  tp_jw_ints(w, r->changed);
  KEY(w, "warnings");
  tp_jw_open(w, '[');
  for (i = 0; i < r->warnings.n; i++) tp_edit_warning_write(&r->warnings.p[i], w);
  tp_jw_close(w, ']');
  tp_jw_close(w, '}');
}

TP_PRIVATE void tp_raw_edit_result_write(const tp_raw_edit_result *r, tp_jw *w) {
  size_t i;
  tp_jw_open(w, '{');
  KEY(w, "text");
  tp_jw_str(w, r->text);
  KEY(w, "changed");
  tp_jw_ints(w, r->changed);
  KEY(w, "warnings");
  tp_jw_open(w, '[');
  for (i = 0; i < r->warnings.n; i++) {
    const tp_raw_edit_warning *x = &r->warnings.p[i];
    tp_jw_open(w, '{');
    KEY(w, "code");
    tp_jw_str(w, x->code);
    KEY(w, "edit");
    tp_jw_integer(w, (int64_t)x->edit);
    KEY(w, "line");
    tp_jw_opt_num(w, x->line);
    tp_jw_close(w, '}');
  }
  tp_jw_close(w, ']');
  tp_jw_close(w, '}');
}

/* jsonValue.write. Recursive: the depth is bounded by the reader (TP_JSON_MAX_DEPTH) or by the
 * builder (tp_edit_json: 2). */
TP_PRIVATE void tp_json_write(const tp_json *v, tp_jw *w) {
  size_t i;
  switch (v->kind) {
  case TP_JSON_NULL:
    tp_jw_null(w);
    break;
  case TP_JSON_BOOL:
    tp_jw_boolean(w, v->b);
    break;
  case TP_JSON_NUMBER:
    tp_jw_num(w, v->num);
    break;
  case TP_JSON_STRING:
    tp_jw_str(w, v->str);
    break;
  case TP_JSON_ARRAY:
    tp_jw_open(w, '[');
    for (i = 0; i < v->arr_n; i++) tp_json_write(&v->arr[i], w);
    tp_jw_close(w, ']');
    break;
  case TP_JSON_OBJECT:
    tp_jw_open(w, '{');
    for (i = 0; i < v->obj_n; i++) {
      tp_jw_key(w, v->obj[i].key);
      tp_json_write(&v->obj[i].val, w);
    }
    tp_jw_close(w, '}');
    break;
  }
}

/* -------------------------------------------------------------------------------------------- */
/* marshal, one per type                                                                        */

TP_PRIVATE tp_str tp_track_json(const tp_track *t) {
  tp_jw w = tp_jw_new();
  tp_track_write(t, &w);
  return tp_jw_done(&w);
}

TP_PRIVATE tp_str tp_artists_json(tp_artist_vec list) {
  tp_jw w = tp_jw_new();
  tp_write_artists(&w, list);
  return tp_jw_done(&w);
}

TP_PRIVATE tp_str tp_tracklist_json(const tp_tracklist *t) {
  tp_jw w = tp_jw_new();
  tp_tracklist_write(t, &w);
  return tp_jw_done(&w);
}

TP_PRIVATE tp_str tp_edit_result_json(const tp_edit_result *r) {
  tp_jw w = tp_jw_new();
  tp_edit_result_write(r, &w);
  return tp_jw_done(&w);
}

TP_PRIVATE tp_str tp_raw_edit_result_json(const tp_raw_edit_result *r) {
  tp_jw w = tp_jw_new();
  tp_raw_edit_result_write(r, &w);
  return tp_jw_done(&w);
}

TP_PRIVATE tp_str tp_string_json(tp_str s) {
  tp_jw w = tp_jw_new();
  tp_jw_str(&w, s);
  return tp_jw_done(&w);
}

/* -------------------------------------------------------------------------------------------- */
/* JSON values                                                                                  */

/* getString is the string at key, or false. */
TP_PRIVATE bool tp_json_get_string(tp_json v, tp_str key, tp_str *out) {
  tp_json x;
  if (!tp_json_get(v, key, &x) || x.kind != TP_JSON_STRING) return false;
  *out = x.str;
  return true;
}

typedef TP_VEC(tp_json_member) member_vec;

static void member(member_vec *m, const char *key, tp_json v) {
  tp_json_member x;
  x.key = tp_str_c(key);
  x.val = v;
  tp_push(*m, x);
}

/* A number as reading its JSON text back gives it: NaN and the infinities are written as null,
 * and -0 is written as 0. Finite values round-trip exactly (shortest round-trip digits). */
static tp_json number_value(double n) {
  if (tp_is_nan(n) || tp_is_inf(n)) return tp_json_null();
  if (n == 0) return tp_json_num(0.0);
  return tp_json_num(n);
}

/* An integer field: written as the double (double)n, read back as that double. */
static tp_json integer_value(int64_t n) { return number_value((double)n); }

/* jsonOf(&edit): the edit's JSON value, exactly what reading tp_edit_write's text back gives
 * (Go goes through the text), built directly: strings tp_sanitize'd, the integer fields as
 * doubles, a NaN or infinite seconds as null, -0 as 0, keys in tp_edit_write's order. */
TP_PRIVATE tp_json tp_edit_json(const tp_edit *e) {
  member_vec m = {0, 0, 0};
  tp_json out = tp_json_null();
  member(&m, "op", tp_json_str(tp_sanitize(e->op)));
  if (tp_str_eq(e->op, TP_EDIT_OP_SET_TIME)) {
    member(&m, "line", integer_value(e->line));
    member(&m, "seconds", number_value(e->seconds));
  } else if (tp_str_eq(e->op, TP_EDIT_OP_SHIFT)) {
    member(&m, "seconds", number_value(e->seconds));
    if (e->has_lines) {
      tp_json arr = tp_json_null();
      tp_json *items = (tp_json *)tp_alloc_array(e->lines.n, sizeof *items);
      size_t i;
      for (i = 0; i < e->lines.n; i++) items[i] = integer_value(e->lines.p[i]);
      arr.kind = TP_JSON_ARRAY;
      arr.arr = items;
      arr.arr_n = e->lines.n;
      member(&m, "lines", arr);
    }
    if (e->from != NULL) member(&m, "from", integer_value(*e->from));
  } else if (tp_str_eq(e->op, TP_EDIT_OP_SET_REST)) {
    member(&m, "line", integer_value(e->line));
    member(&m, "rest", tp_json_str(tp_sanitize(e->rest)));
  } else if (tp_str_eq(e->op, TP_EDIT_OP_INSERT)) {
    member(&m, "after", integer_value(e->after));
    member(&m, "text", tp_json_str(tp_sanitize(e->text)));
  } else if (tp_str_eq(e->op, TP_EDIT_OP_DELETE)) {
    member(&m, "line", integer_value(e->line));
  }
  out.kind = TP_JSON_OBJECT;
  out.obj = m.p != NULL ? m.p : (const tp_json_member *)tp_alloc(0);
  out.obj_n = m.n;
  return out;
}
