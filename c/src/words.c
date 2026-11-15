/* words.c: low-level text helpers shared by every rule (go/words.go). See words.h. */
#include "words.h"

/* ------------------------------------------------------------------------------------------ */
/* Rune slices                                                                                */

TP_PRIVATE tp_rune tp_at(tp_runes t, ptrdiff_t i) {
  if (i < 0 || (size_t)i >= t.n) return TP_NORUNE;
  return t.p[i];
}

TP_PRIVATE tp_rune tp_before(tp_runes t, ptrdiff_t i) { return tp_at(t, i - 1); }

TP_PRIVATE tp_runes tp_sub(tp_runes t, ptrdiff_t a, ptrdiff_t b) {
  ptrdiff_t n = (ptrdiff_t)t.n;
  if (b > n) b = n;
  if (b < 0) b = 0;
  if (a > b) a = b;
  if (a < 0) a = 0;
  tp_runes out = {t.p + a, (size_t)(b - a)};
  return out;
}

TP_PRIVATE tp_runes tp_tail(tp_runes t, ptrdiff_t a) { return tp_sub(t, a, (ptrdiff_t)t.n); }

/* ------------------------------------------------------------------------------------------ */
/* Whitespace (R0.2) and JS trim()                                                            */

TP_PRIVATE bool tp_is_whitespace(tp_rune r) {
  return (r >= 0x09 && r <= 0x0d) || r == 0x20 || r == 0x85 || r == 0xa0 || r == 0x1680 ||
         (r >= 0x2000 && r <= 0x200a) || r == 0x2028 || r == 0x2029 || r == 0x202f || r == 0x205f ||
         r == 0x3000;
}

TP_PRIVATE bool tp_is_js_space(tp_rune r) {
  return (r >= 0x09 && r <= 0x0d) || r == 0x20 || r == 0xa0 || r == 0x1680 ||
         (r >= 0x2000 && r <= 0x200a) || r == 0x2028 || r == 0x2029 || r == 0x202f || r == 0x205f ||
         r == 0x3000 || r == 0xfeff;
}

TP_PRIVATE tp_str tp_js_trim(tp_str s) { return tp_js_trim_end(tp_js_trim_start(s)); }

TP_PRIVATE tp_str tp_js_trim_start(tp_str s) {
  while (s.n > 0) {
    size_t size;
    tp_rune r = tp_decode_rune(s, &size);
    if (!tp_is_js_space(r)) break;
    s.p += size;
    s.n -= size;
  }
  return s;
}

TP_PRIVATE tp_str tp_js_trim_end(tp_str s) {
  while (s.n > 0) {
    size_t size;
    tp_rune r = tp_decode_last_rune(s, &size);
    if (!tp_is_js_space(r)) break;
    s.n -= size;
  }
  return s;
}

TP_PRIVATE tp_runes tp_js_trim_runes(tp_runes t) {
  size_t a = 0, b = t.n;
  while (a < b && tp_is_js_space(t.p[a])) a++;
  while (b > a && tp_is_js_space(t.p[b - 1])) b--;
  tp_runes out = {t.p + a, b - a};
  return out;
}

/* ------------------------------------------------------------------------------------------ */
/* Digits and letters (R0.6)                                                                  */

TP_PRIVATE bool tp_is_digit(tp_rune r) { return r >= '0' && r <= '9'; }

TP_PRIVATE bool tp_is_all_digits(tp_str s) {
  if (s.n == 0) return false;
  for (size_t i = 0; i < s.n; i++) {
    if (s.p[i] < '0' || s.p[i] > '9') return false;
  }
  return true;
}

TP_PRIVATE bool tp_is_letter_at(tp_runes t, ptrdiff_t i) {
  tp_rune r = tp_at(t, i);
  return r >= 0 && tp_is_letter(r);
}

TP_PRIVATE bool tp_is_letter_before(tp_runes t, ptrdiff_t i) { return tp_is_letter_at(t, i - 1); }

TP_PRIVATE bool tp_is_upper_at(tp_runes t, ptrdiff_t i) {
  tp_rune r = tp_at(t, i);
  return r >= 0 && tp_is_upper_letter(r);
}

/* ------------------------------------------------------------------------------------------ */
/* Word keys (R0.3)                                                                           */

TP_PRIVATE tp_str tp_ascii_lower(tp_str s) {
  for (size_t i = 0; i < s.n; i++) {
    if (s.p[i] >= 'A' && s.p[i] <= 'Z') {
      char *b = tp_alloc(tp_size_add(s.n, 1));
      memcpy(b, s.p, s.n);
      for (size_t j = i; j < s.n; j++) {
        if (b[j] >= 'A' && b[j] <= 'Z') b[j] = (char)(b[j] + ('a' - 'A'));
      }
      tp_str out = {b, s.n};
      return out;
    }
  }
  return s;
}

TP_PRIVATE tp_str tp_strip_key_punct(tp_str lower) {
  if (lower.n > 0) {
    switch (lower.p[lower.n - 1]) {
    case '.':
    case ',':
    case '!':
    case '?':
    case ':':
    case ';':
      lower.n--;
      break;
    default:
      break;
    }
  }
  return lower;
}

TP_PRIVATE tp_str tp_word_key(tp_str word) { return tp_strip_key_punct(tp_ascii_lower(word)); }

TP_PRIVATE bool tp_word_matches(tp_str word, tp_str token) {
  tp_str lower = tp_ascii_lower(word);
  return tp_str_eq(lower, token) || tp_str_eq(tp_strip_key_punct(lower), token);
}

/* ------------------------------------------------------------------------------------------ */
/* Word sets                                                                                  */

TP_PRIVATE void tp_word_set_add(tp_word_set *s, tp_str word) { tp_map_put(&s->m, word, 0); }

TP_PRIVATE void tp_word_set_add_all(tp_word_set *s, const tp_str *words, size_t n) {
  for (size_t i = 0; i < n; i++) tp_word_set_add(s, words[i]);
}

TP_PRIVATE bool tp_word_set_has(const tp_word_set *s, tp_str key) { return tp_map_has(&s->m, key); }

TP_PRIVATE bool tp_in_vocab(tp_str word, const tp_word_set *vocab) {
  tp_str lower = tp_ascii_lower(word);
  return tp_word_set_has(vocab, lower) || tp_word_set_has(vocab, tp_strip_key_punct(lower));
}

/* ------------------------------------------------------------------------------------------ */
/* Collapsing and words (R0.7)                                                                */

TP_PRIVATE tp_str tp_collapse_spaces(tp_str s) {
  tp_buf out = {0};
  bool pending = false;
  for (size_t i = 0; i < s.n;) {
    size_t size;
    tp_str rest = {s.p + i, s.n - i};
    tp_rune r = tp_decode_rune(rest, &size);
    i += size;
    if (tp_is_whitespace(r)) {
      pending = out.n > 0;
      continue;
    }
    if (pending) tp_buf_byte(&out, ' ');
    pending = false;
    tp_buf_rune(&out, r);
  }
  return tp_buf_done(&out);
}

TP_PRIVATE tp_str tp_collapse_runes(tp_runes t) {
  /* Go: collapseSpaces(string(t)). Encoding a rune that is not a scalar value gives U+FFFD,
   * which tp_buf_rune does too, and U+FFFD is not whitespace, so the runes are read directly. */
  tp_buf out = {0};
  bool pending = false;
  for (size_t i = 0; i < t.n; i++) {
    tp_rune r = t.p[i];
    if (tp_is_whitespace(r)) {
      pending = out.n > 0;
      continue;
    }
    if (pending) tp_buf_byte(&out, ' ');
    pending = false;
    tp_buf_rune(&out, r);
  }
  return tp_buf_done(&out);
}

TP_PRIVATE tp_strvec tp_split_words(tp_str s) { return tp_split_words_runes(tp_runes_of(s)); }

TP_PRIVATE tp_strvec tp_split_words_runes(tp_runes t) { return tp_span_texts(tp_word_spans(t)); }

TP_PRIVATE tp_word_span_vec tp_word_spans(tp_runes t) {
  tp_word_span_vec spans = {0};
  ptrdiff_t start = -1;
  for (size_t i = 0; i <= t.n; i++) {
    if (i == t.n || tp_is_whitespace(t.p[i])) {
      if (start >= 0) {
        tp_word_span w;
        w.text = tp_runes_str(tp_sub(t, start, (ptrdiff_t)i));
        w.start = start;
        w.end = (ptrdiff_t)i;
        tp_push(spans, w);
      }
      start = -1;
    } else if (start < 0) {
      start = (ptrdiff_t)i;
    }
  }
  return spans;
}

TP_PRIVATE tp_strvec tp_span_texts(tp_word_span_vec spans) {
  tp_strvec out = {0};
  for (size_t i = 0; i < spans.n; i++) tp_push(out, spans.p[i].text);
  return out;
}

/* ------------------------------------------------------------------------------------------ */
/* Dedup keys (R0.4), years, UTF-16 lengths                                                   */

TP_PRIVATE tp_str tp_dedup_key(tp_str s) {
  tp_runes decomposed = tp_runes_of(tp_nfd(tp_js_to_lower(s)));
  tp_rune *kept = tp_alloc_array(decomposed.n, sizeof(tp_rune));
  size_t k = 0;
  for (size_t i = 0; i < decomposed.n; i++) {
    tp_rune r = decomposed.p[i];
    if (r >= 0x300 && r <= 0x36f) continue;
    kept[k++] = r;
  }
  tp_runes view = {kept, k};
  return tp_collapse_runes(view);
}

TP_PRIVATE bool tp_year_of(tp_str word, int *out) {
  tp_str key = tp_word_key(word);
  *out = 0;
  if (key.n != 4 || !tp_is_all_digits(key)) return false;
  int n =
      (key.p[0] - '0') * 1000 + (key.p[1] - '0') * 100 + (key.p[2] - '0') * 10 + (key.p[3] - '0');
  *out = n;
  return n >= 1900 && n <= 2099;
}

TP_PRIVATE ptrdiff_t tp_utf16_len(tp_runes t) {
  ptrdiff_t n = 0;
  for (size_t i = 0; i < t.n; i++) n += t.p[i] >= 0x10000 ? 2 : 1;
  return n;
}

TP_PRIVATE ptrdiff_t tp_utf16_len_string(tp_str s) {
  ptrdiff_t n = 0;
  for (size_t i = 0; i < s.n;) {
    size_t size;
    tp_str rest = {s.p + i, s.n - i};
    tp_rune r = tp_decode_rune(rest, &size);
    i += size;
    n += r >= 0x10000 ? 2 : 1;
  }
  return n;
}

/* ------------------------------------------------------------------------------------------ */
/* Sanitizing invalid UTF-8 (WHATWG maximal subparts)                                         */

TP_PRIVATE tp_str tp_sanitize(tp_str s) {
  static const char replacement[3] = {'\xef', '\xbf', '\xbd'}; /* U+FFFD */
  if (tp_utf8_valid(s)) return s;
  const unsigned char *b = (const unsigned char *)s.p;
  tp_buf out = {0};
  size_t i = 0;
  while (i < s.n) {
    unsigned char c = b[i];
    if (c < 0x80) {
      tp_buf_byte(&out, (char)c);
      i++;
      continue;
    }
    /* The bounds of the second byte, and how many continuation bytes follow the lead. */
    unsigned char lo = 0x80, hi = 0xbf;
    size_t need = 0;
    if (c >= 0xc2 && c <= 0xdf) {
      need = 1;
    } else if (c == 0xe0) {
      lo = 0xa0;
      need = 2;
    } else if ((c >= 0xe1 && c <= 0xec) || c == 0xee || c == 0xef) {
      need = 2;
    } else if (c == 0xed) {
      hi = 0x9f;
      need = 2;
    } else if (c == 0xf0) {
      lo = 0x90;
      need = 3;
    } else if (c >= 0xf1 && c <= 0xf3) {
      need = 3;
    } else if (c == 0xf4) {
      hi = 0x8f;
      need = 3;
    }
    if (need == 0) {
      tp_buf_bytes(&out, replacement, 3);
      i++;
      continue;
    }
    size_t j = i + 1;
    bool ok = true;
    for (size_t k = 0; k < need; k++) {
      if (j >= s.n) {
        ok = false;
        break;
      }
      if (k > 0) {
        lo = 0x80;
        hi = 0xbf;
      }
      if (b[j] < lo || b[j] > hi) {
        ok = false;
        break;
      }
      j++;
    }
    if (ok) {
      tp_buf_bytes(&out, s.p + i, j - i);
    } else {
      /* The lead and the valid continuation bytes so far are one maximal subpart. */
      tp_buf_bytes(&out, replacement, 3);
    }
    i = j;
  }
  return tp_buf_done(&out);
}

/* ------------------------------------------------------------------------------------------ */
/* Phrase tables                                                                              */

TP_PRIVATE tp_phrase_table *tp_new_phrase_table(void) { return tp_alloc(sizeof(tp_phrase_table)); }

/* Keeps each list longest first, insertion order among equals (a stable insert, like the JS
 * sort). */
static void insert_sorted(tp_phrase_table *t, tp_map *m, tp_str k, int64_t id) {
  int64_t idx;
  if (!tp_map_get(m, k, &idx)) {
    tp_intvec empty = {0};
    idx = (int64_t)t->lists.n;
    tp_push(t->lists, empty);
    tp_map_put(m, k, idx);
  }
  tp_intvec *list = &t->lists.p[idx];
  tp_push(*list, id);
  size_t n = t->entries.p[id].tokens.n;
  for (size_t i = list->n - 1; i > 0 && t->entries.p[list->p[i - 1]].tokens.n < n; i--) {
    int64_t x = list->p[i];
    list->p[i] = list->p[i - 1];
    list->p[i - 1] = x;
  }
}

TP_PRIVATE void tp_phrase_table_add(tp_phrase_table *t, tp_str raw_key, const void *value) {
  /* The key is copied: the table may outlive the caller's string. */
  tp_str key = tp_js_trim(tp_ascii_lower(raw_key));
  tp_strvec tokens = tp_split_words(key);
  if (tokens.n == 0) return;
  if (tp_map_has(&t->keys, key)) return;
  int64_t id = (int64_t)t->entries.n;
  tp_map_put(&t->keys, tp_str_copy(key.p, key.n), 0);
  tp_phrase_entry e;
  e.tokens = tokens;
  e.value = value;
  tp_push(t->entries, e);
  insert_sorted(t, &t->by_first, tokens.p[0], id);
  insert_sorted(t, &t->by_last, tokens.p[tokens.n - 1], id);
}

/* The entries indexed under the word's lowercase form, then under its stripped form (Go
 * candidates, without building the concatenation). */
static void candidates(const tp_phrase_table *t, const tp_map *m, tp_str word, tp_intvec *a,
                       tp_intvec *b) {
  tp_intvec none = {0};
  int64_t idx;
  tp_str lower = tp_ascii_lower(word);
  tp_str stripped = tp_strip_key_punct(lower);
  *a = tp_map_get(m, lower, &idx) ? t->lists.p[idx] : none;
  *b = none;
  if (stripped.n != lower.n && tp_map_get(m, stripped, &idx)) *b = t->lists.p[idx];
}

static bool tokens_match(tp_strvec words, ptrdiff_t start, tp_strvec tokens) {
  for (size_t k = 0; k < tokens.n; k++) {
    ptrdiff_t i = start + (ptrdiff_t)k;
    if (i < 0 || (size_t)i >= words.n || !tp_word_matches(words.p[i], tokens.p[k])) return false;
  }
  return true;
}

TP_PRIVATE bool tp_phrase_table_match_at(const tp_phrase_table *t, tp_strvec words, ptrdiff_t start,
                                         tp_phrase_match *out) {
  tp_phrase_match best = {NULL, 0};
  bool found = false;
  *out = best;
  if (start < 0 || (size_t)start >= words.n) return false;
  tp_intvec lists[2];
  candidates(t, &t->by_first, words.p[start], &lists[0], &lists[1]);
  for (int l = 0; l < 2; l++) {
    for (size_t k = 0; k < lists[l].n; k++) {
      const tp_phrase_entry *e = &t->entries.p[lists[l].p[k]];
      ptrdiff_t n = (ptrdiff_t)e->tokens.n;
      if (found && n <= best.length) continue;
      if (start + n > (ptrdiff_t)words.n) continue;
      if (tokens_match(words, start, e->tokens)) {
        best.value = e->value;
        best.length = n;
        found = true;
      }
    }
  }
  *out = best;
  return found;
}

TP_PRIVATE bool tp_phrase_table_match_ending(const tp_phrase_table *t, tp_strvec words,
                                             ptrdiff_t end, tp_phrase_match *out) {
  tp_phrase_match best = {NULL, 0};
  bool found = false;
  *out = best;
  if (end < 1 || (size_t)end > words.n) return false;
  tp_intvec lists[2];
  candidates(t, &t->by_last, words.p[end - 1], &lists[0], &lists[1]);
  for (int l = 0; l < 2; l++) {
    for (size_t k = 0; k < lists[l].n; k++) {
      const tp_phrase_entry *e = &t->entries.p[lists[l].p[k]];
      ptrdiff_t n = (ptrdiff_t)e->tokens.n;
      if (found && n <= best.length) continue;
      ptrdiff_t from = end - n;
      if (from < 0) continue;
      if (tokens_match(words, from, e->tokens)) {
        best.value = e->value;
        best.length = n;
        found = true;
      }
    }
  }
  *out = best;
  return found;
}
