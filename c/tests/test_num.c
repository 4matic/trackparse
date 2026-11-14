/* test_num.c: src/num.c against values from Node.
 *
 *   test_num [cases.jsonl]      (default: tests/data/numbers_sample.jsonl next to this file's
 *                                build, or the path in argv[1])
 *
 * The JSONL comes from scripts/dump_numbers.mjs. Lines are parsed ad hoc (the format is fixed and
 * holds no escapes). For {"s","v","b"}: tp_parse_number(s) has bits b and tp_number_str of it is
 * v. For {"d","s"}: tp_number_str(d) is s and, for finite d, tp_parse_number(s) gives d back bit
 * for bit (zero: either sign, String(-0) is "0"). */
#include <stdio.h>
#include <stdlib.h>

#include "../src/rt.h"

#ifndef TP_TEST_DATA
#define TP_TEST_DATA "tests/data/numbers_sample.jsonl"
#endif

static int failures;
static long checks;

#define CHECK(cond, ...)               \
  do {                                 \
    checks++;                          \
    if (!(cond)) {                     \
      failures++;                      \
      if (failures <= 30) {            \
        fprintf(stderr, __VA_ARGS__);  \
        fputc('\n', stderr);           \
      }                                \
    }                                  \
  } while (0)

static uint64_t bits_of(double v) {
  uint64_t u;
  memcpy(&u, &v, sizeof u);
  return u;
}

static double of_bits(uint64_t u) {
  double v;
  memcpy(&v, &u, sizeof v);
  return v;
}

static bool parse_hex(const char *p, size_t n, uint64_t *out) {
  uint64_t v = 0;
  size_t i;
  if (n == 0 || n > 16) return false;
  for (i = 0; i < n; i++) {
    char c = p[i];
    unsigned d;
    if (c >= '0' && c <= '9') {
      d = (unsigned)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      d = (unsigned)(c - 'a' + 10);
    } else {
      return false;
    }
    v = v << 4 | d;
  }
  *out = v;
  return true;
}

/* The string value of "key":"..." in line, or false. */
static bool field(const char *line, const char *key, tp_str *out) {
  char pat[16];
  const char *at, *end;
  size_t kn = strlen(key);
  pat[0] = '"';
  memcpy(pat + 1, key, kn);
  memcpy(pat + 1 + kn, "\":\"", 4);
  at = strstr(line, pat);
  if (at == NULL) return false;
  at += kn + 4;
  end = strchr(at, '"');
  if (end == NULL) return false;
  out->p = at;
  out->n = (size_t)(end - at);
  return true;
}

static tp_str num_str(double v) { return tp_number_str(v); }

static void expect_parse(const char *s, bool ok, uint64_t bits) {
  double v = 0;
  bool got = tp_parse_number(tp_str_c(s), &v);
  CHECK(got == ok, "parse %s: accepted %d, want %d", s, got, ok);
  if (got && ok) {
    CHECK(bits_of(v) == bits, "parse %s: bits %016llx, want %016llx", s,
          (unsigned long long)bits_of(v), (unsigned long long)bits);
  }
}

static void expect_fmt(double v, const char *want) {
  tp_str got = num_str(v);
  CHECK(tp_str_eq_c(got, want), "format %016llx: %.*s, want %s", (unsigned long long)bits_of(v),
        TP_FMT(got), want);
}

static void unit_tests(void) {
  /* Grammar: accepted forms. */
  expect_parse("0", true, 0);
  expect_parse("-0", true, UINT64_C(0x8000000000000000));
  expect_parse("007", true, bits_of(7.0));
  expect_parse("1.", true, bits_of(1.0));
  expect_parse(".5", true, bits_of(0.5));
  expect_parse("+1e+007", true, bits_of(1e7));
  expect_parse("1E2", true, bits_of(100.0));
  expect_parse("-.0e5", true, UINT64_C(0x8000000000000000));
  expect_parse("3167.12345678901234567890", true, bits_of(3167.1234567890124));
  expect_parse("1e21", true, bits_of(1e21));
  expect_parse("1e309", true, UINT64_C(0x7FF0000000000000));
  expect_parse("-1e309", true, UINT64_C(0xFFF0000000000000));
  expect_parse("1e-400", true, 0);
  expect_parse("4.9406564584124654e-324", true, 1);
  expect_parse("2.4703282292062327e-324", true, 0);
  expect_parse("2.4703282292062328e-324", true, 1);
  expect_parse("9007199254740993", true, bits_of(9007199254740992.0));
  expect_parse("9007199254740995", true, bits_of(9007199254740996.0));
  expect_parse("1e99999999999999999999", true, UINT64_C(0x7FF0000000000000));
  expect_parse("0e99999999999999999999", true, 0);
  /* Grammar: rejected forms. */
  {
    static const char *bad[] = {"",   "-",   "+",   ".",    "e5",  "1e",   "1e+", "1.2.3",
                                " 1", "1 ",  "--1", "+-1",  "1e5.", "0x10", "1_0", "Infinity",
                                "NaN", "inf", "1e-", ".e1", "1d",   "١"};
    size_t i;
    for (i = 0; i < TP_COUNT(bad); i++) expect_parse(bad[i], false, 0);
  }
  /* Formatting. */
  expect_fmt(0.0, "0");
  expect_fmt(-0.0, "0");
  expect_fmt(tp_nan(), "NaN");
  expect_fmt(tp_inf(1), "Infinity");
  expect_fmt(tp_inf(-1), "-Infinity");
  expect_fmt(1e21, "1e+21");
  expect_fmt(1e20, "100000000000000000000");
  expect_fmt(123456789012345680000.0, "123456789012345680000");
  expect_fmt(1e-7, "1e-7");
  expect_fmt(0.000001, "0.000001");
  expect_fmt(1.5e-7, "1.5e-7");
  expect_fmt(of_bits(1), "5e-324");
  expect_fmt(1.7976931348623157e308, "1.7976931348623157e+308");
  expect_fmt(0.1, "0.1");
  expect_fmt(1.0 / 3.0, "0.3333333333333333");
  expect_fmt(3167.0, "3167");
  expect_fmt(-3167.5, "-3167.5");
  expect_fmt(9007199254740992.0, "9007199254740992");
  expect_fmt(2.2250738585072014e-308, "2.2250738585072014e-308");
  expect_fmt(5e-324 * 3, "1.5e-323");
  /* Floor and half-up rounding. */
  CHECK(tp_floor(-0.5) == -1.0, "floor(-0.5)");
  CHECK(tp_floor(0.5) == 0.0, "floor(0.5)");
  CHECK(bits_of(tp_floor(-0.0)) == UINT64_C(0x8000000000000000), "floor(-0)");
  CHECK(tp_floor(-3.0) == -3.0, "floor(-3)");
  CHECK(tp_floor(1e300) == 1e300, "floor(1e300)");
  CHECK(tp_floor(-1e-300) == -1.0, "floor(-1e-300)");
  CHECK(tp_floor(4503599627370495.5) == 4503599627370495.0, "floor(2^52-0.5)");
  CHECK(tp_is_nan(tp_floor(tp_nan())), "floor(nan)");
  CHECK(tp_round_half_up(1.005, 2) == 1.0, "1.005 (double below) -> 1"); /* 1.00499999... */
  CHECK(tp_round_half_up(2.5, 0) == 3.0, "2.5 -> 3 (half up, not even)");
  CHECK(tp_round_half_up(0.125, 2) == 0.13, "0.125 -> 0.13");
  CHECK(tp_half_up_units(83.4567, 1000) == 83457.0, "83.4567 units");
  /* Values where a fused multiply-add changes the result (JS gives the unfused one): Node,
   * Math.floor(v * 10 + 0.5). */
  CHECK(tp_half_up_units(of_bits(UINT64_C(0x42e99999999999a4)), 10) == 2251799813685251.0,
        "fma case 1");
  CHECK(tp_half_up_units(of_bits(UINT64_C(0x42e99999999999b4)), 10) == 2251799813685256.0,
        "fma case 2");
  CHECK(tp_is_inf(tp_inf(-1)) && !tp_is_inf(1e308) && !tp_is_inf(tp_nan()), "is_inf");
  CHECK(tp_is_nan(tp_nan()) && !tp_is_nan(tp_inf(1)), "is_nan");
}

static void file_tests(const char *path, long *cases) {
  tp_arena *scratch = tp_arena_current(); /* the scope's own arena, freed by main */
  FILE *f = fopen(path, "rb");
  char *line;
  size_t cap = 1 << 16;
  if (f == NULL) {
    fprintf(stderr, "cannot open %s\n", path);
    failures++;
    return;
  }
  line = (char *)malloc(cap);
  if (line == NULL) abort();
  for (;;) {
    size_t n = 0;
    int c;
    tp_str s, v, b, d;
    while ((c = fgetc(f)) != EOF && c != '\n') {
      if (n + 2 > cap) {
        char *grown = (char *)realloc(line, cap * 2);
        if (grown == NULL) abort();
        line = grown;
        cap *= 2;
      }
      line[n++] = (char)c;
    }
    if (n == 0 && c == EOF) break;
    line[n] = '\0';
    if (n == 0) continue;
    (*cases)++;
    if (*cases % 10000 == 0) { /* recycle the scratch arena */
      tp_arena *fresh = tp_arena_new(), *old;
      if (fresh == NULL) abort();
      old = tp_arena_use(fresh);
      if (old != scratch) tp_arena_free(old);
    }
    if (field(line, "d", &d)) {
      uint64_t bits;
      double x, back = 0;
      tp_str got;
      CHECK(field(line, "s", &s) && parse_hex(d.p, d.n, &bits), "bad line: %s", line);
      if (!parse_hex(d.p, d.n, &bits)) continue;
      x = of_bits(bits);
      got = num_str(x);
      CHECK(tp_str_eq(got, s), "format %016llx: %.*s, want %.*s", (unsigned long long)bits,
            TP_FMT(got), TP_FMT(s));
      if (!tp_is_nan(x) && !tp_is_inf(x)) {
        bool ok = tp_parse_number(s, &back);
        CHECK(ok && (bits_of(back) == bits || (x == 0 && back == 0)),
              "round trip %016llx via %.*s: %016llx", (unsigned long long)bits, TP_FMT(s),
              (unsigned long long)bits_of(back));
      }
    } else {
      uint64_t bits;
      double x = 0;
      bool ok;
      if (!field(line, "s", &s) || !field(line, "v", &v) || !field(line, "b", &b) ||
          !parse_hex(b.p, b.n, &bits)) {
        CHECK(false, "bad line: %s", line);
        continue;
      }
      ok = tp_parse_number(s, &x);
      CHECK(ok, "rejected: %.*s", TP_FMT(s));
      if (!ok) continue;
      CHECK(bits_of(x) == bits, "parse %.*s: %016llx, want %016llx", TP_FMT(s),
            (unsigned long long)bits_of(x), (unsigned long long)bits);
      {
        tp_str got = num_str(x);
        CHECK(tp_str_eq(got, v), "format of %.*s: %.*s, want %.*s", TP_FMT(s), TP_FMT(got),
              TP_FMT(v));
      }
    }
  }
  free(line);
  fclose(f);
  if (tp_arena_current() != scratch) tp_arena_free(tp_arena_use(scratch));
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : TP_TEST_DATA;
  static long cases;
  tp_scope s;
  if (!tp_scope_open(&s)) return 2;
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    fprintf(stderr, "out of memory\n");
    return 2;
  }
  tp_scope_arm(&s);
  unit_tests();
  tp_scope_close(&s);
  tp_arena_free(s.arena);
  /* One scope per batch of lines, so the arena stays small on millions of cases. */
  if (!tp_scope_open(&s)) return 2;
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    fprintf(stderr, "out of memory\n");
    return 2;
  }
  tp_scope_arm(&s);
  file_tests(path, &cases);
  tp_scope_close(&s);
  tp_arena_free(s.arena);
  printf("test_num: %ld file cases, %ld checks, %d failures\n", cases, checks, failures);
  return failures == 0 && cases > 0 ? 0 : 1;
}
