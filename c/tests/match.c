/* match.c: the fixture matcher (go/match_test.go). Recursion depth is bounded by the JSON
 * reader's depth limit. */
#include "match.h"

static const char hexdigits[] = "0123456789abcdef";

void tp_json_quote(tp_buf *b, tp_str s) {
  tp_buf_byte(b, '"');
  for (size_t i = 0; i < s.n; i++) {
    unsigned char c = (unsigned char)s.p[i];
    switch (c) {
      case '"': tp_buf_cstr(b, "\\\""); break;
      case '\\': tp_buf_cstr(b, "\\\\"); break;
      case '\b': tp_buf_cstr(b, "\\b"); break;
      case '\f': tp_buf_cstr(b, "\\f"); break;
      case '\n': tp_buf_cstr(b, "\\n"); break;
      case '\r': tp_buf_cstr(b, "\\r"); break;
      case '\t': tp_buf_cstr(b, "\\t"); break;
      default:
        if (c < 0x20) {
          tp_buf_cstr(b, "\\u00");
          tp_buf_byte(b, hexdigits[c >> 4]);
          tp_buf_byte(b, hexdigits[c & 15]);
        } else {
          tp_buf_byte(b, (char)c);
        }
    }
  }
  tp_buf_byte(b, '"');
}

static void show_into(tp_buf *b, tp_json v) {
  switch (v.kind) {
    case TP_JSON_NULL: tp_buf_cstr(b, "null"); break;
    case TP_JSON_BOOL: tp_buf_cstr(b, v.b ? "true" : "false"); break;
    case TP_JSON_NUMBER:
      if (tp_is_nan(v.num) || tp_is_inf(v.num)) {
        tp_buf_cstr(b, "null");
      } else {
        tp_buf_number(b, v.num);
      }
      break;
    case TP_JSON_STRING: tp_json_quote(b, v.str); break;
    case TP_JSON_ARRAY:
      tp_buf_byte(b, '[');
      for (size_t i = 0; i < v.arr_n; i++) {
        if (i) tp_buf_byte(b, ',');
        show_into(b, v.arr[i]);
      }
      tp_buf_byte(b, ']');
      break;
    case TP_JSON_OBJECT:
      tp_buf_byte(b, '{');
      for (size_t i = 0; i < v.obj_n; i++) {
        if (i) tp_buf_byte(b, ',');
        tp_json_quote(b, v.obj[i].key);
        tp_buf_byte(b, ':');
        show_into(b, v.obj[i].val);
      }
      tp_buf_byte(b, '}');
      break;
  }
}

tp_str tp_json_show(tp_json v) {
  tp_buf b = {0};
  show_into(&b, v);
  return tp_buf_done(&b);
}

/* Scalars compare as JS does: numbers by value (7 equals 7.0), strings exactly. */
static bool same_scalar(tp_json e, tp_json a) {
  if (e.kind != a.kind) return false;
  switch (e.kind) {
    case TP_JSON_NULL: return true;
    case TP_JSON_BOOL: return e.b == a.b;
    case TP_JSON_NUMBER: return e.num == a.num;
    case TP_JSON_STRING: return tp_str_eq(e.str, a.str);
    default: return false;
  }
}

/* "<path>: expected <e>, got <a>" */
static tp_str expected_got(tp_str path, tp_json e, tp_json a) {
  tp_buf b = {0};
  tp_buf_str(&b, path);
  tp_buf_cstr(&b, ": expected ");
  tp_buf_str(&b, tp_json_show(e));
  tp_buf_cstr(&b, ", got ");
  tp_buf_str(&b, tp_json_show(a));
  return tp_buf_done(&b);
}

static tp_str index_path(tp_str path, size_t i) {
  tp_buf b = {0};
  tp_buf_str(&b, path);
  tp_buf_byte(&b, '[');
  tp_buf_int(&b, (int64_t)i);
  tp_buf_byte(&b, ']');
  return tp_buf_done(&b);
}

static tp_str key_path(tp_str path, tp_str key) {
  return tp_concat3(path, TP_S("."), key);
}

/* match: "partial" (README rules 1 to 4); empty on a match. */
static tp_str match_partial(tp_json expected, tp_json actual, tp_str path) {
  if (expected.kind == TP_JSON_ARRAY) {
    if (actual.kind != TP_JSON_ARRAY) {
      return tp_concat(path, tp_concat(TP_S(": expected an array, got "), tp_json_show(actual)));
    }
    if (actual.arr_n != expected.arr_n) {
      tp_buf b = {0};
      tp_buf_str(&b, path);
      tp_buf_cstr(&b, ": expected ");
      tp_buf_int(&b, (int64_t)expected.arr_n);
      tp_buf_cstr(&b, " items, got ");
      tp_buf_int(&b, (int64_t)actual.arr_n);
      tp_buf_cstr(&b, ": ");
      tp_buf_str(&b, tp_json_show(actual));
      return tp_buf_done(&b);
    }
    for (size_t i = 0; i < expected.arr_n; i++) {
      tp_json ev = expected.arr[i], av = actual.arr[i], name;
      tp_str p = index_path(path, i), d;
      /* Rule 4: artist shorthand. */
      if (ev.kind == TP_JSON_STRING && av.kind == TP_JSON_OBJECT &&
          tp_json_get(av, TP_S("name"), &name)) {
        if (!same_scalar(ev, name)) return expected_got(key_path(p, TP_S("name")), ev, name);
        continue;
      }
      d = match_partial(ev, av, p);
      if (d.n) return d;
    }
    return TP_EMPTY;
  }
  if (expected.kind == TP_JSON_OBJECT) {
    if (actual.kind != TP_JSON_OBJECT) {
      return tp_concat(path, tp_concat(TP_S(": expected an object, got "), tp_json_show(actual)));
    }
    for (size_t i = 0; i < expected.obj_n; i++) {
      const tp_json_member *m = &expected.obj[i];
      tp_json av;
      tp_str d;
      if (!tp_json_get(actual, m->key, &av)) {
        return tp_concat(key_path(path, m->key), TP_S(": missing"));
      }
      d = match_partial(m->val, av, key_path(path, m->key));
      if (d.n) return d;
    }
    return TP_EMPTY;
  }
  if (!same_scalar(expected, actual)) return expected_got(path, expected, actual);
  return TP_EMPTY;
}

static int cmp_str(const void *a, const void *b, void *ctx) {
  (void)ctx;
  return tp_str_cmp(*(const tp_str *)a, *(const tp_str *)b);
}

static tp_strvec sorted_keys(tp_json v) {
  tp_strvec keys = {0};
  for (size_t i = 0; i < v.obj_n; i++) tp_push(keys, v.obj[i].key);
  tp_sort_stable(keys.p, keys.n, sizeof *keys.p, cmp_str, NULL);
  return keys;
}

static tp_json strings_value(tp_strvec list) {
  tp_json v = tp_json_null();
  tp_json *items = (tp_json *)tp_alloc_array(list.n, sizeof *items);
  for (size_t i = 0; i < list.n; i++) items[i] = tp_json_str(list.p[i]);
  v.kind = TP_JSON_ARRAY;
  v.arr = items;
  v.arr_n = list.n;
  return v;
}

/* Deep equality of complete values (README rule 5); empty on a match. */
static tp_str match_exact(tp_json expected, tp_json actual, tp_str path) {
  if (expected.kind == TP_JSON_ARRAY && actual.kind == TP_JSON_ARRAY &&
      expected.arr_n == actual.arr_n) {
    for (size_t i = 0; i < expected.arr_n; i++) {
      tp_str d = match_exact(expected.arr[i], actual.arr[i], index_path(path, i));
      if (d.n) return d;
    }
    return TP_EMPTY;
  }
  if (expected.kind == TP_JSON_OBJECT && actual.kind == TP_JSON_OBJECT) {
    tp_strvec ek = sorted_keys(expected), ak = sorted_keys(actual);
    if (!tp_strvec_eq(ek, ak)) {
      tp_buf b = {0};
      tp_buf_str(&b, path);
      tp_buf_cstr(&b, ": expected keys ");
      tp_buf_str(&b, tp_json_show(strings_value(ek)));
      tp_buf_cstr(&b, ", got ");
      tp_buf_str(&b, tp_json_show(strings_value(ak)));
      return tp_buf_done(&b);
    }
    for (size_t i = 0; i < ek.n; i++) {
      tp_json ev, av;
      tp_str d;
      tp_json_get(expected, ek.p[i], &ev);
      tp_json_get(actual, ek.p[i], &av);
      d = match_exact(ev, av, key_path(path, ek.p[i]));
      if (d.n) return d;
    }
    return TP_EMPTY;
  }
  if (expected.kind == TP_JSON_ARRAY || expected.kind == TP_JSON_OBJECT ||
      !same_scalar(expected, actual)) {
    return expected_got(path, expected, actual);
  }
  return TP_EMPTY;
}

/* Drops a top-level specVersion (README rule 6: never compared). */
static tp_json without_spec_version(tp_json v) {
  tp_json out = v;
  tp_json_member *members;
  size_t n = 0;
  if (v.kind != TP_JSON_OBJECT) return v;
  members = (tp_json_member *)tp_alloc_array(v.obj_n, sizeof *members);
  for (size_t i = 0; i < v.obj_n; i++) {
    if (!tp_str_eq(v.obj[i].key, TP_S("specVersion"))) members[n++] = v.obj[i];
  }
  out.obj = members;
  out.obj_n = n;
  return out;
}

tp_str tp_match(tp_json expected, tp_json actual, bool exact) {
  tp_json e = without_spec_version(expected), a = without_spec_version(actual);
  return exact ? match_exact(e, a, TP_S("$")) : match_partial(e, a, TP_S("$"));
}
