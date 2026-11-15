/* tl_kinds.c: R13.3 line kinds, R13.4.2 candidate lines and the R13.13 layered prefix
 * (go/tl_kinds.go). */
#include "internal.h"

static void *tl_build_header_words(void) {
  tp_word_set *s = tp_alloc(sizeof *s);
  tp_word_set_add_all(s, tp_data_header_words_words, TP_DATA_HEADER_WORDS_WORDS_N);
  return s;
}

static void *tl_build_layered_words(void) {
  tp_word_set *s = tp_alloc(sizeof *s);
  tp_word_set_add_all(s, tp_data_layered_words_words, TP_DATA_LAYERED_WORDS_WORDS_N);
  return s;
}

static void *tl_build_default_markers(void) {
  tp_strvec none = {0};
  /* The table is built in this once's arena; the cast drops const for the void * interface. */
  return (void *)(uintptr_t)tp_build_marker_table(none);
}

static tp_once tl_header_once = TP_ONCE_INIT;
static tp_once tl_layered_once = TP_ONCE_INIT;
static tp_once tl_marker_once = TP_ONCE_INIT;

TP_PRIVATE const tp_word_set *tp_tl_header_words(void) {
  void *v = tp_once_get(&tl_header_once, tl_build_header_words);
  return v ? v : tl_build_header_words();
}

TP_PRIVATE const tp_word_set *tp_tl_layered_words(void) {
  void *v = tp_once_get(&tl_layered_once, tl_build_layered_words);
  return v ? v : tl_build_layered_words();
}

TP_PRIVATE const tp_phrase_table *tp_tl_marker_table(void) {
  void *v = tp_once_get(&tl_marker_once, tl_build_default_markers);
  return v ? v : tl_build_default_markers();
}

/* buildMarkerTable is data/marker-words.json plus extra. Entries are split into words the same
 * way as the track part (R13.3), so shout-out matches the words shout, out. */
TP_PRIVATE const tp_phrase_table *tp_build_marker_table(tp_strvec extra) {
  tp_phrase_table *t = tp_new_phrase_table();
  for (size_t k = 0; k < TP_DATA_MARKER_WORDS_WORDS_N; k++) {
    tp_phrase_table_add(t, tp_join(tp_marker_words(tp_data_marker_words_words[k]), TP_S(" ")),
                        NULL);
  }
  for (size_t k = 0; k < extra.n; k++) {
    tp_phrase_table_add(t, tp_join(tp_marker_words(extra.p[k]), TP_S(" ")), NULL);
  }
  return t;
}

/* markerTableFor is the marker table with options.markerWords (R13.10); the default table is
 * built once. */
TP_PRIVATE const tp_phrase_table *tp_marker_table_for(tp_strvec extra) {
  if (extra.n == 0) return tp_tl_marker_table();
  return tp_build_marker_table(extra);
}

/* isMarkerBreak splits marker words: '/', '-' and R0.2 whitespace. */
TP_PRIVATE bool tp_is_marker_break(tp_rune r) {
  return r == '/' || r == '-' || tp_is_whitespace(r);
}

/* isMarkerBracket is one of ()[]{}. */
TP_PRIVATE bool tp_is_marker_bracket(tp_rune r) {
  switch (r) {
  case '(':
  case ')':
  case '[':
  case ']':
  case '{':
  case '}':
    return true;
  default:
    return false;
  }
}

/* Go markerWords' closure flush: the piece current[a:b] without leading and trailing brackets,
 * appended to out when not empty. */
static void tl_flush_marker_word(tp_strvec *out, tp_runes current) {
  size_t a = 0, b = current.n;
  while (a < b && tp_is_marker_bracket(current.p[a])) a++;
  while (b > a && tp_is_marker_bracket(current.p[b - 1])) b--;
  if (b > a) tp_push(*out, tp_runes_str(tp_runes_slice(current, a, b)));
}

/* markerWords is R13.3: split on R0.2 whitespace, '/' and '-', then strip leading and trailing
 * ()[]{} from each piece; empty pieces are dropped. Word keys (R0.3) apply on matching. */
TP_PRIVATE tp_strvec tp_marker_words(tp_str text) {
  tp_strvec out = {0};
  tp_runes t = tp_runes_of(text);
  size_t start = 0;
  for (size_t k = 0; k < t.n; k++) {
    if (tp_is_marker_break(t.p[k])) {
      tl_flush_marker_word(&out, tp_runes_slice(t, start, k));
      start = k + 1;
    }
  }
  tl_flush_marker_word(&out, tp_runes_slice(t, start, t.n));
  return out;
}

/* layeredPrefix is R13.13: the length (in code points) of a layered prefix at the start of the
 * line text: R0.2 whitespace, a word of data/layered-words.json (word key, R0.3), then R0.2
 * whitespace, with more text after it. 0 when there is none. */
TP_PRIVATE ptrdiff_t tp_layered_prefix(tp_runes text) {
  ptrdiff_t len = (ptrdiff_t)text.n;
  ptrdiff_t i = tp_skip_whitespace(text, 0);
  ptrdiff_t j = i;
  while (j < len && !tp_is_whitespace(text.p[j])) j++;
  if (j == i || j == len || !tp_in_vocab(tp_runes_str(tp_sub(text, i, j)), tp_tl_layered_words())) {
    return 0;
  }
  j = tp_skip_whitespace(text, j);
  return j < len ? j : 0;
}

/* isBlank is R13.3.1: the line is only R0.2 whitespace. */
TP_PRIVATE bool tp_is_blank(tp_str raw) {
  tp_str s = raw;
  while (s.n > 0) {
    size_t len;
    tp_rune r = tp_decode_rune(s, &len);
    if (!tp_is_whitespace(r)) return false;
    if (len == 0) break;
    s = tp_str_slice(s, len, s.n);
  }
  return true;
}

/* trimWS trims R0.2 whitespace from both ends. */
TP_PRIVATE tp_runes tp_trim_ws(tp_str s) {
  tp_runes t = tp_runes_of(s);
  size_t a = 0, b = t.n;
  while (a < b && tp_is_whitespace(t.p[a])) a++;
  while (b > a && tp_is_whitespace(t.p[b - 1])) b--;
  return tp_runes_slice(t, a, b);
}

/* isLink is R13.3.3: the text starts with http://, https:// or www. (ASCII case-insensitive). */
TP_PRIVATE bool tp_is_link(tp_str rest) {
  tp_str t = tp_ascii_lower(tp_runes_str(tp_sub(tp_trim_ws(rest), 0, 8)));
  return tp_has_prefix(t, TP_S("http://")) || tp_has_prefix(t, TP_S("https://")) ||
         tp_has_prefix(t, TP_S("www."));
}

/* isHeader is R13.3.3: the text ends with ':', or is a header word followed by at most one more
 * word. */
TP_PRIVATE bool tp_is_header(tp_str rest) {
  tp_runes t = tp_trim_ws(rest);
  tp_strvec words;
  if (t.n > 0 && t.p[t.n - 1] == ':') return true;
  words = tp_split_words_runes(t);
  return words.n > 0 && words.n <= 2 && tp_in_vocab(words.p[0], tp_tl_header_words());
}

/* hasMarkerWord is R13.3: any marker word (or multi-word entry) among the track part's marker
 * words. */
TP_PRIVATE bool tp_has_marker_word(tp_str track_part, const tp_phrase_table *markers) {
  tp_strvec words = tp_marker_words(track_part);
  for (size_t i = 0; i < words.n; i++) {
    tp_phrase_match m;
    if (tp_phrase_table_match_at(markers, words, (ptrdiff_t)i, &m)) return true;
  }
  return false;
}

/* hasSeparator is R13.3: the track part has a separator. It is not empty after R1 (empty), its
 * parse did not warn noSeparator, and the artist side is not empty (at least one primary
 * artist, or flags.unknown_artist). */
TP_PRIVATE bool tp_has_separator(const tp_track *t, bool empty) {
  if (empty) return false;
  for (size_t i = 0; i < t->warnings.n; i++) {
    if (tp_str_eq(t->warnings.p[i], TP_WARNING_NO_SEPARATOR)) return false;
  }
  if (t->flags.unknown_artist) return true;
  for (size_t i = 0; i < t->artists.n; i++) {
    if (tp_str_eq(t->artists.p[i].role, TP_ROLE_PRIMARY)) return true;
  }
  return false;
}
