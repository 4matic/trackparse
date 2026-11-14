/* json_read.c: the JSON reader of the wire door, a translation of the reader in go/json.go
 * (parseJSON, jsonReader). The recursion is bounded by TP_JSON_MAX_DEPTH. */
#include "json_read.h"

#include "../src/words.h" /* tp_sanitize */

typedef TP_VEC(tp_json) tp_jsonvec;
typedef TP_VEC(tp_json_member) tp_json_membervec;

typedef struct reader {
  tp_str s;
  size_t at;
  int depth;
} reader;

/* The byte at the cursor, or 0 at the end. */
static unsigned char peek(const reader *r) {
  return r->at < r->s.n ? (unsigned char)r->s.p[r->at] : 0;
}

static void ws(reader *r) {
  while (r->at < r->s.n) {
    char c = r->s.p[r->at];
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return;
    r->at++;
  }
}

static bool eat(reader *r, const char *word) {
  size_t n = strlen(word);
  if (r->s.n - r->at >= n && memcmp(r->s.p + r->at, word, n) == 0) {
    r->at += n;
    return true;
  }
  return false;
}

/* hex4 reads four hex digits. */
static bool hex4(reader *r, int32_t *out) {
  int32_t n = 0;
  if (r->s.n - r->at < 4) return false;
  for (size_t i = 0; i < 4; i++) {
    unsigned char c = (unsigned char)r->s.p[r->at + i];
    int32_t d;
    if (c >= '0' && c <= '9') {
      d = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      d = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      d = c - 'A' + 10;
    } else {
      return false;
    }
    n = n << 4 | d;
  }
  r->at += 4;
  *out = n;
  return true;
}

/* utf8.AppendRune: a surrogate (not a scalar value) is written as U+FFFD. */
static void append_rune(tp_buf *b, int32_t cp) {
  if (cp >= 0xd800 && cp < 0xe000) cp = 0xfffd;
  tp_buf_rune(b, cp);
}

static bool read_string(reader *r, tp_str *out) {
  tp_buf b = {0};
  if (peek(r) != '"') return false;
  r->at++;
  for (;;) {
    char c;
    if (r->at >= r->s.n) return false;
    c = r->s.p[r->at++];
    if (c == '"') {
      *out = tp_buf_done(&b);
      return true;
    }
    if (c != '\\') {
      tp_buf_byte(&b, c);
      continue;
    }
    if (r->at >= r->s.n) return false;
    c = r->s.p[r->at++];
    switch (c) {
      case 'n': tp_buf_byte(&b, '\n'); break;
      case 'r': tp_buf_byte(&b, '\r'); break;
      case 't': tp_buf_byte(&b, '\t'); break;
      case 'b': tp_buf_byte(&b, '\b'); break;
      case 'f': tp_buf_byte(&b, '\f'); break;
      case 'u': {
        int32_t hi, cp;
        if (!hex4(r, &hi)) return false;
        cp = hi;
        if (hi >= 0xd800 && hi < 0xdc00 && eat(r, "\\u")) {
          int32_t lo;
          if (!hex4(r, &lo)) return false;
          if (lo >= 0xdc00 && lo < 0xe000) {
            cp = 0x10000 + ((hi - 0xd800) << 10) + (lo - 0xdc00);
          } else {
            /* Two units that do not pair: both become U+FFFD (lo unless it is a scalar). */
            append_rune(&b, 0xfffd);
            cp = lo;
          }
        }
        append_rune(&b, cp);
        break;
      }
      default: {
        /* \" \\ \/ and any other escaped character stand for themselves. The escaped character
         * may be the first byte of a multi-byte code point: copy it whole. */
        size_t size = 0;
        tp_decode_rune(tp_str_slice(r->s, r->at - 1, r->s.n), &size);
        if (size == 0) size = 1;
        tp_buf_bytes(&b, r->s.p + r->at - 1, size);
        r->at += size - 1;
        break;
      }
    }
  }
}

static bool read_number(reader *r, tp_json *out) {
  size_t start = r->at;
  double f;
  while (r->at < r->s.n) {
    char c = r->s.p[r->at];
    if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
      r->at++;
      continue;
    }
    break;
  }
  if (r->at == start) return false;
  /* Out of range gives the infinity (or zero), as JS Number() and Go's ParseFloat do. */
  if (!tp_parse_number(tp_str_slice(r->s, start, r->at), &f)) return false;
  *out = tp_json_num(f);
  return true;
}

static bool read_value(reader *r, tp_json *out);

static bool read_array(reader *r, tp_json *out) {
  tp_jsonvec items = {0};
  r->at++;
  r->depth++;
  ws(r);
  if (peek(r) == ']') {
    r->at++;
  } else {
    for (;;) {
      tp_json v;
      ws(r);
      if (!read_value(r, &v)) return false;
      tp_push(items, v);
      ws(r);
      if (peek(r) == ',') {
        r->at++;
        continue;
      }
      if (peek(r) == ']') {
        r->at++;
        break;
      }
      return false;
    }
  }
  r->depth--;
  memset(out, 0, sizeof *out);
  out->kind = TP_JSON_ARRAY;
  out->arr = items.n ? items.p : (const tp_json *)tp_alloc(0);
  out->arr_n = items.n;
  return true;
}

static bool read_object(reader *r, tp_json *out) {
  tp_json_membervec members = {0};
  r->at++;
  r->depth++;
  ws(r);
  if (peek(r) == '}') {
    r->at++;
  } else {
    for (;;) {
      tp_str key;
      tp_json v;
      bool found = false;
      ws(r);
      if (!read_string(r, &key)) return false;
      ws(r);
      if (peek(r) != ':') return false;
      r->at++;
      ws(r);
      if (!read_value(r, &v)) return false;
      for (size_t i = 0; i < members.n; i++) {
        if (tp_str_eq(members.p[i].key, key)) {
          members.p[i].val = v;
          found = true;
          break;
        }
      }
      if (!found) {
        tp_json_member m;
        m.key = key;
        m.val = v;
        tp_push(members, m);
      }
      ws(r);
      if (peek(r) == ',') {
        r->at++;
        continue;
      }
      if (peek(r) == '}') {
        r->at++;
        break;
      }
      return false;
    }
  }
  r->depth--;
  memset(out, 0, sizeof *out);
  out->kind = TP_JSON_OBJECT;
  out->obj = members.n ? members.p : (const tp_json_member *)tp_alloc(0);
  out->obj_n = members.n;
  return true;
}

static bool read_value(reader *r, tp_json *out) {
  if (r->depth > TP_JSON_MAX_DEPTH || r->at >= r->s.n) return false;
  switch (peek(r)) {
    case 'n':
      *out = tp_json_null();
      return eat(r, "null");
    case 't':
      *out = tp_json_bool(true);
      return eat(r, "true");
    case 'f':
      *out = tp_json_bool(false);
      return eat(r, "false");
    case '"': {
      tp_str s;
      if (!read_string(r, &s)) return false;
      *out = tp_json_str(s);
      return true;
    }
    case '[':
      return read_array(r, out);
    case '{':
      return read_object(r, out);
    default:
      return read_number(r, out);
  }
}

TP_PRIVATE bool tp_json_parse(tp_str text, tp_json *out) {
  reader r;
  tp_json v;
  r.s = tp_sanitize(text);
  r.at = 0;
  r.depth = 0;
  *out = tp_json_null();
  ws(&r);
  if (!read_value(&r, &v)) return false;
  ws(&r);
  if (r.at != r.s.n) return false;
  *out = v;
  return true;
}
