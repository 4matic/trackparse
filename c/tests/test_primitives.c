/* test_primitives.c: the R0 primitives (words.c, digits.c) against values taken from the JS
 * reference (go/primitives_test.go, plus the sanitize cases of go/utf8_test.go).
 *
 * The expected values are ../rust/tests/data/primitives.json, written by
 * rust/scripts/dump_primitives.mjs from js/src/words.ts; it is read in place (not copied), and the
 * table tests skip when it is absent. TRACKPARSE_PRIMITIVES=<file> swaps in another dump. The
 * c/ directory is found from __FILE__, or from TRACKPARSE_C_DIR when set.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/digits.h"
#include "../src/unicode.h"
#include "../src/words.h"
#include "../wire/json_read.h"

static int failures = 0;

/* ------------------------------------------------------------------------------------------ */
/* Helpers                                                                                    */

static bool scoped(void (*fn)(void *), void *ctx) {
  tp_scope s;
  if (!tp_scope_open(&s)) return false;
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    return false;
  }
  tp_scope_arm(&s);
  fn(ctx);
  tp_scope_close(&s);
  tp_arena_free(s.arena);
  return true;
}

static void put_quoted(tp_str s) {
  putchar('"');
  for (size_t i = 0; i < s.n;) {
    size_t size;
    tp_str rest = {s.p + i, s.n - i};
    tp_rune r = tp_decode_rune(rest, &size);
    if (size == 1 && (unsigned char)s.p[i] >= 0x80) {
      printf("\\x%02x", (unsigned char)s.p[i]);
    } else if (r >= 0x20 && r < 0x7f && r != '"' && r != '\\') {
      putchar((int)r);
    } else {
      printf("\\u{%x}", (unsigned)r);
    }
    i += size;
  }
  putchar('"');
}

static void check_str(const char *what, tp_str in, tp_str got, tp_str want) {
  if (tp_str_eq(got, want)) return;
  if (++failures > 30) return;
  printf("FAIL %s(", what);
  put_quoted(in);
  printf(") = ");
  put_quoted(got);
  printf(", JS ");
  put_quoted(want);
  printf("\n");
}

static void check(bool ok, const char *what) {
  if (ok) return;
  if (++failures <= 30) printf("FAIL %s\n", what);
}

static tp_str S(const char *s) { return tp_str_c(s); }

static char *read_file(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  size_t cap = 1 << 16, len = 0;
  char *buf = malloc(cap);
  for (;;) {
    if (len == cap) {
      cap *= 2;
      char *nb = realloc(buf, cap);
      if (!nb) {
        free(buf);
        fclose(f);
        return NULL;
      }
      buf = nb;
    }
    size_t got = fread(buf + len, 1, cap - len, f);
    if (got == 0) break;
    len += got;
  }
  fclose(f);
  *n = len;
  return buf;
}

static void c_dir(char *out, size_t cap) {
  const char *env = getenv("TRACKPARSE_C_DIR");
  if (env && *env) {
    snprintf(out, cap, "%s", env);
    return;
  }
  const char *file = __FILE__;
  const char *cut = strstr(file, "tests/test_primitives.c");
  if (!cut) cut = strstr(file, "tests\\test_primitives.c");
  size_t n = cut ? (size_t)(cut - file) : 0;
  if (n == 0) {
    snprintf(out, cap, ".");
  } else {
    snprintf(out, cap, "%.*s", (int)(n - 1), file);
  }
}

/* ------------------------------------------------------------------------------------------ */
/* primitives.json                                                                            */

static char *data_text = NULL;
static size_t data_len = 0;
static bool data_override = false;

static bool primitives_data(tp_json *out) {
  if (!data_text) return false;
  tp_str text = {data_text, data_len};
  if (!tp_json_parse(text, out)) {
    check(false, "primitives.json does not parse");
    return false;
  }
  return true;
}

static tp_str get_str(tp_json v, const char *key) {
  tp_json x;
  if (!tp_json_get(v, tp_str_c(key), &x) || x.kind != TP_JSON_STRING) return TP_S("<missing>");
  return x.str;
}

/* The dumped code points and one flag string ("0110...") of property. */
static void check_property(const char *property, bool (*test)(tp_rune)) {
  tp_json data, table, runs, flags;
  if (!primitives_data(&data)) return;
  if (!tp_json_get(data, TP_S("codePoints"), &table) || !tp_json_get(table, TP_S("runs"), &runs) ||
      !tp_json_get(table, tp_str_c(property), &flags) || flags.kind != TP_JSON_STRING) {
    check(false, "codePoints table");
    return;
  }
  size_t idx = 0, bad = 0;
  for (size_t i = 0; i < runs.arr_n; i++) {
    const tp_json *run = &runs.arr[i];
    if (run->arr_n != 2) {
      check(false, "bad run");
      return;
    }
    tp_rune first = (tp_rune)run->arr[0].num;
    long count = (long)run->arr[1].num;
    for (long k = 0; k < count; k++, idx++) {
      if (idx >= flags.str.n) {
        check(false, "flags shorter than the code points");
        return;
      }
      tp_rune r = first + (tp_rune)k;
      bool want = flags.str.p[idx] == '1';
      if (test(r) != want && ++bad <= 10) {
        printf("FAIL %s of U+%04X: got %d\n", property, (unsigned)r, !want);
        failures++;
      }
    }
  }
  if (idx < 5000 || idx != flags.str.n) {
    printf("FAIL %s: %zu code points, %zu flags\n", property, idx, flags.str.n);
    failures++;
  }
}

/* ------------------------------------------------------------------------------------------ */
/* Tests                                                                                      */

static void whitespace_set(void *unused) {
  (void)unused;
  check_property("whitespace", tp_is_whitespace);
  /* R0.2, spelled out: U+0085 is in; U+FEFF, U+200B and U+180E are out. */
  tp_rune want[32];
  size_t nw = 0;
  static const tp_rune head[] = {0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x20, 0x85, 0xa0, 0x1680};
  for (size_t i = 0; i < TP_COUNT(head); i++) want[nw++] = head[i];
  for (tp_rune r = 0x2000; r <= 0x200a; r++) want[nw++] = r;
  want[nw++] = 0x2028;
  want[nw++] = 0x2029;
  want[nw++] = 0x202f;
  want[nw++] = 0x205f;
  want[nw++] = 0x3000;
  size_t ng = 0;
  bool same = true;
  for (tp_rune r = 0; r <= 0x10ffff; r++) {
    if (!tp_is_whitespace(r)) continue;
    if (ng >= nw || want[ng] != r) same = false;
    ng++;
  }
  check(same && ng == nw, "whitespace set");
  check(!tp_is_whitespace(TP_NORUNE), "TP_NORUNE is not whitespace");
}

static void js_trim_set(void *unused) {
  (void)unused;
  check_property("jsSpace", tp_is_js_space);
  check_str("js_trim", S("\xef\xbb\xbf x\xc2\xa0\xe2\x80\xa8"),
            tp_js_trim(S("\xef\xbb\xbf x\xc2\xa0\xe2\x80\xa8")), S("x"));
  /* R0.2 whitespace, not JS whitespace */
  check_str("js_trim", S("\xc2\x85x\xc2\x85"), tp_js_trim(S("\xc2\x85x\xc2\x85")),
            S("\xc2\x85x\xc2\x85"));
  check_str("js_trim_end", S(" x \xef\xbb\xbf"), tp_js_trim_end(S(" x \xef\xbb\xbf")), S(" x"));
  tp_str in = S("\xe3\x80\x80x y\t");
  check_str("js_trim_runes", in, tp_runes_str(tp_js_trim_runes(tp_runes_of(in))), S("x y"));
}

static void letter_and_upper(void *unused) {
  (void)unused;
  /* The same file's letter and uppercase flags (R0.6, R6.10). */
  check_property("letter", tp_is_letter);
  check_property("upper", tp_is_upper_letter);
}

static void strings(void *unused) {
  (void)unused;
  tp_json data, records;
  if (!primitives_data(&data)) return;
  if (!tp_json_get(data, TP_S("strings"), &records) || (records.arr_n < 250 && !data_override)) {
    printf("FAIL only %zu records\n", records.arr_n);
    failures++;
    return;
  }
  for (size_t i = 0; i < records.arr_n; i++) {
    tp_json r = records.arr[i], x;
    tp_str s = get_str(r, "s");
    tp_runes rs = tp_runes_of(s);
    if (!tp_json_get(r, TP_S("utf16Len"), &x) || tp_utf16_len(rs) != (ptrdiff_t)x.num ||
        tp_utf16_len_string(s) != tp_utf16_len(rs)) {
      check_str("utf16_len", s, tp_itoa(tp_utf16_len(rs)), tp_number_str(x.num));
    }
    check_str("ascii_lower", s, tp_ascii_lower(s), get_str(r, "asciiLower"));
    check_str("js_to_lower", s, tp_js_to_lower(s), get_str(r, "lower"));
    check_str("word_key", s, tp_word_key(s), get_str(r, "wordKey"));
    check_str("dedup_key", s, tp_dedup_key(s), get_str(r, "dedupKey"));
    check_str("collapse_spaces", s, tp_collapse_spaces(s), get_str(r, "collapse"));
    check_str("collapse_runes", s, tp_collapse_runes(rs), get_str(r, "collapse"));
    check_str("js_trim", s, tp_js_trim(s), get_str(r, "jsTrim"));
    check_str("js_trim_end", s, tp_js_trim_end(s), get_str(r, "jsTrimEnd"));
    tp_strvec want = {0};
    if (tp_json_get(r, TP_S("words"), &x)) {
      for (size_t k = 0; k < x.arr_n; k++) tp_push(want, x.arr[k].str);
    }
    tp_strvec got = tp_split_words(s);
    if (!tp_strvec_eq(got, want)) {
      check_str("split_words", s, tp_join(got, TP_S("|")), tp_join(want, TP_S("|")));
    }
    tp_json year;
    bool has_year = tp_json_get(r, TP_S("year"), &year) && year.kind != TP_JSON_NULL;
    int got_year;
    bool ok = tp_year_of(s, &got_year);
    if (has_year != ok || (ok && got_year != (int)year.num)) {
      check_str("year_of", s, tp_itoa(got_year), has_year ? tp_number_str(year.num) : S("null"));
    }
  }
}

/* The lowercase sweep of the same file: [code point, JS toLowerCase()] pairs. */
static void lower_sweep(void *unused) {
  (void)unused;
  tp_json data, sweep;
  if (!primitives_data(&data)) return;
  if (!tp_json_get(data, TP_S("lowerSweep"), &sweep) || sweep.arr_n < 1000) {
    check(data_override, "lowerSweep");
    return;
  }
  for (size_t i = 0; i < sweep.arr_n; i++) {
    const tp_json *e = &sweep.arr[i];
    if (e->arr_n != 2) {
      check(false, "bad lowerSweep entry");
      return;
    }
    tp_buf b = {0};
    tp_buf_rune(&b, (tp_rune)e->arr[0].num);
    tp_str in = tp_buf_done(&b);
    check_str("js_to_lower", in, tp_js_to_lower(in), e->arr[1].str);
  }
}

static void word_keys(void *unused) {
  (void)unused;
  check_str("word_key", S("FEAT."), tp_word_key(S("FEAT.")), S("feat"));
  check_str("word_key", S("Remix!!"), tp_word_key(S("Remix!!")), S("remix!"));
  /* ASCII lowercase only */
  check_str("word_key", S("\xc3\x89."), tp_word_key(S("\xc3\x89.")), S("\xc3\x89"));
  check(tp_word_matches(S("Feat."), S("feat.")) && tp_word_matches(S("Feat."), S("feat")) &&
            !tp_word_matches(S("feat"), S("feat.")),
        "word_matches");
  check(tp_str_eq(tp_strip_key_punct(S("a;")), S("a")) &&
            tp_str_eq(tp_strip_key_punct(S("\xc3\xa4")), S("\xc3\xa4")),
        "strip_key_punct");
  tp_word_set set = {{0}};
  static const tp_str words[] = {{"feat.", 5}, {"remix", 5}};
  tp_word_set_add_all(&set, words, TP_COUNT(words));
  check(tp_in_vocab(S("FEAT."), &set) && tp_in_vocab(S("Remix,"), &set) &&
            !tp_in_vocab(S("feat"), &set),
        "in_vocab");
}

static void dedup_keys(void *unused) {
  (void)unused;
  static const struct {
    const char *in, *want;
  } cases[] = {
      {"\xc4\xb0stanbul", "istanbul"}, /* i + U+0307, the mark then removed */
      {"STRA\xe1\xba\x9e"
       "E",
       "stra\xc3\x9f"
       "e"},
      {"\xce\x9f\xce\x94\xce\xa5\xce\xa3\xce\xa3\xce\x95\xce\x91\xce\xa3",
       "\xce\xbf\xce\xb4\xcf\x85\xcf\x83\xcf\x83\xce\xb5\xce\xb1\xcf\x82"}, /* final sigma */
      {"  Beyonce\xcc\x81   KNOWLES ", "beyonce knowles"},
      {"\xd0\x99", "\xd0\xb8"},                                 /* U+0306 is removed */
      {"\xe3\x81\x8b\xe3\x82\x99", "\xe3\x81\x8b\xe3\x82\x99"}, /* other marks stay */
      {"\xff"
       "A",
       "\xef\xbf\xbd"
       "a"}, /* invalid UTF-8 cannot reach here, but must not fail */
  };
  for (size_t i = 0; i < TP_COUNT(cases); i++) {
    check_str("dedup_key", S(cases[i].in), tp_dedup_key(S(cases[i].in)), S(cases[i].want));
  }
  (void)tp_dedup_key(S("\xff"
                       "A\xe2\x82"));
}

static void rune_helpers(void *unused) {
  (void)unused;
  tp_runes rs = tp_runes_of(S("a\xf0\x9d\x90\x80"
                              "b")); /* a, U+1D400, b */
  check(tp_at(rs, 1) == 0x1d400 && tp_at(rs, 3) == TP_NORUNE && tp_at(rs, -1) == TP_NORUNE &&
            tp_before(rs, 0) == TP_NORUNE,
        "at / before");
  check(tp_str_eq(tp_runes_str(tp_sub(rs, 1, 99)), S("\xf0\x9d\x90\x80"
                                                     "b")) &&
            tp_sub(rs, 5, 2).n == 0 && tp_str_eq(tp_runes_str(tp_tail(rs, 2)), S("b")),
        "sub / tail");
  check(tp_utf16_len(rs) == 4, "utf16_len");
  check(tp_is_letter_at(rs, 0) && !tp_is_letter_at(rs, 9) && !tp_is_upper_at(rs, 2) &&
            tp_is_upper_at(tp_runes_of(S("A")), 0) && tp_is_letter_before(rs, 1),
        "letter helpers");
  tp_word_span_vec spans = tp_word_spans(tp_runes_of(S(" ab\xe3\x80\x80"
                                                       "c ")));
  check(spans.n == 2 && tp_str_eq(spans.p[0].text, S("ab")) && spans.p[0].start == 1 &&
            spans.p[0].end == 3 && tp_str_eq(spans.p[1].text, S("c")) && spans.p[1].start == 4 &&
            spans.p[1].end == 5,
        "word_spans");
  tp_strvec texts = tp_span_texts(spans);
  check(texts.n == 2 && tp_str_eq(texts.p[0], S("ab")) && tp_str_eq(texts.p[1], S("c")),
        "span_texts");
  check(tp_is_all_digits(S("0123")) && !tp_is_all_digits(S("")) && !tp_is_all_digits(S("12a")),
        "is_all_digits");
}

static void digits(void *unused) {
  (void)unused;
  tp_runes s = tp_runes_of(S("x0123:59:60:7"));
  check(tp_digit_run(s, 1) == 4 && tp_digit_run(s, 0) == 0 && tp_digit_run(s, 99) == 0,
        "digit_run");
  check(tp_digits_value(tp_runes_of(S("0123"))) == 123 &&
            tp_digits_value(tp_runes_of(S("99999999999999999999"))) == ((int64_t)1 << 40),
        "digits_value");
  int n;
  check(tp_two_digits_up_to_59(s, 6, &n) && n == 59, "59");
  check(!tp_two_digits_up_to_59(s, 9, &n), "60");
  check(!tp_two_digits_up_to_59(s, 1, &n), "followed by a third digit");
  check(!tp_two_digits_up_to_59(s, 12, &n), "one digit");
}

#define R "\xef\xbf\xbd"

static void sanitize(void *unused) {
  (void)unused;
  static const struct {
    const char *in;
    size_t n;
    const char *want;
    size_t wn;
  } cases[] = {
#define C(in, want) {in, sizeof(in) - 1, want, sizeof(want) - 1}
      C("Noisia - Stigma", "Noisia - Stigma"),
      C("a\xff"
        "b",
        "a" R "b"),
      C("\xc0\xaf", R R),       /* C0 is never a lead */
      C("\xe0\x80\x80", R R R), /* E0 needs A0..BF next */
      C("\xe0\xa0", R),         /* truncated: one maximal subpart */
      C("\xe2\x82\xac", "\xe2\x82\xac"),
      C("\xe2\x82", R),
      C("\xe2\x82x", R "x"),
      C("\xed\xa0\x80", R R R), /* surrogates are ill-formed */
      C("\xf0\x90\x80", R),
      C("\xf0\x90\x80\x80", "\xf0\x90\x80\x80"),
      C("\xf4\x90\x80\x80", R R R R), /* above U+10FFFF */
      C("\xf5\x80", R R),
      C("\x80\x80", R R),
      C("\xf1\x80\x80\xe1\x80\xc2", R R R),
      C("a\0b", "a\0b"), /* U+0000 is valid */
      /* go/utf8_test.go lossyCases: as Rust's from_utf8_lossy and Node's TextDecoder */
      C("\xe2\x82"
        "A",
        R "A"),
      C("\xf0\x9f\x98", R),
      C("\xe0\x80\xaf", R R R),
      C("\xf0\x8f\xbf\xbf", R R R R),
      C("\xf1\x80\x80", R),
      C("\xe1\x80\xe2\x82\xac", R "\xe2\x82\xac"),
      C("Noisia \xff- Stigma \xf0\x9f\x98(VIP\xed\xbf\xbf)",
        "Noisia " R "- Stigma " R "(VIP" R R R ")"),
      C("00:00 A\xc3 - B\r\n01:00 \xe2\x80\x93 C - D\xf0\n02:00 \xed\xa0\x80 - \xff\n",
        "00:00 A" R " - B\r\n01:00 \xe2\x80\x93 C - D" R "\n02:00 " R R R " - " R "\n"),
#undef C
  };
  for (size_t i = 0; i < TP_COUNT(cases); i++) {
    tp_str in = {cases[i].in, cases[i].n};
    tp_str want = {cases[i].want, cases[i].wn};
    tp_str got = tp_sanitize(in);
    check_str("sanitize", in, got, want);
    check(tp_utf8_valid(got), "sanitize gives valid UTF-8");
  }
  tp_str valid = S("Bj\xc3\xb6rk");
  check(tp_sanitize(valid).p == valid.p, "valid input is returned unchanged");
}

static void phrase_table(void *unused) {
  (void)unused;
  static const int v1 = 1, v2 = 2, v3 = 3, v4 = 4;
  tp_phrase_table *tb = tp_new_phrase_table();
  tp_phrase_table_add(tb, S("Official Video"), &v1);
  tp_phrase_table_add(tb, S("video"), &v2);
  tp_phrase_table_add(tb, S(" official  video "), &v3);
  tp_phrase_table_add(tb, S(""), &v4);
  tp_strvec words = {0};
  tp_push(words, S("Official"));
  tp_push(words, S("VIDEO,"));
  tp_push(words, S("x"));
  tp_phrase_match m;
#define MATCH(ok, want, len)                                                                       \
  check((ok) && m.value != NULL && *(const int *)m.value == (want) && m.length == (len),           \
        "phrase match " #want " " #len)
  bool ok = tp_phrase_table_match_at(tb, words, 0, &m);
  MATCH(ok, 1, 2);
  ok = tp_phrase_table_match_at(tb, words, 1, &m);
  MATCH(ok, 2, 1);
  ok = tp_phrase_table_match_ending(tb, words, 2, &m);
  MATCH(ok, 1, 2);
  check(!tp_phrase_table_match_ending(tb, words, 0, &m) && m.value == NULL, "match_ending at 0");
  check(!tp_phrase_table_match_at(tb, words, 3, &m), "match_at past the end");
  check(!tp_phrase_table_match_at(tb, words, -1, &m), "match_at before the start");
  /* The first entry for a key wins (keys are trimmed and lowercased, not collapsed). */
  tp_strvec w2 = {0};
  tp_push(w2, S("official"));
  tp_push(w2, S("video"));
  ok = tp_phrase_table_match_at(tb, w2, 0, &m);
  MATCH(ok, 1, 2);
  /* NULL values (Go struct{}) work too. */
  tp_phrase_table *set = tp_new_phrase_table();
  tp_phrase_table_add(set, S("a b"), NULL);
  ok = tp_phrase_table_match_at(set, w2, 0, &m);
  check(!ok, "no match");
  tp_strvec w3 = {0};
  tp_push(w3, S("A"));
  tp_push(w3, S("b."));
  ok = tp_phrase_table_match_at(set, w3, 0, &m);
  check(ok && m.value == NULL && m.length == 2, "NULL-valued match");
#undef MATCH
}

int main(void) {
  char dir[2048], path[4096];
  c_dir(dir, sizeof dir);
  const char *env = getenv("TRACKPARSE_PRIMITIVES");
  if (env && *env) {
    snprintf(path, sizeof path, "%s", env);
    data_override = true;
  } else {
    snprintf(path, sizeof path, "%s/../rust/tests/data/primitives.json", dir);
  }
  data_text = read_file(path, &data_len);
  if (!data_text) printf("skip the table tests: %s not found\n", path);

  void (*tests[])(void *) = {whitespace_set, js_trim_set, letter_and_upper, strings,
                             lower_sweep,    word_keys,   dedup_keys,       rune_helpers,
                             digits,         sanitize,    phrase_table};
  for (size_t i = 0; i < TP_COUNT(tests); i++) check(scoped(tests[i], NULL), "test ran");
  free(data_text);
  if (failures) {
    printf("test_primitives: %d failures\n", failures);
    return 1;
  }
  printf("test_primitives: ok\n");
  return 0;
}
