/* test_unicode.c: unicode.c against Node, the reference engine (go/unicode_test.go).
 *
 * tests/data/unicode/cases.jsonl is a committed sample (under 300 KB) written by
 * scripts/gen_unicode.mjs from Node, so the test needs no Node. `gen_unicode.mjs --check` keeps it
 * in step with the tables. The exhaustive check (every code point, every ordered pair of
 * non-starters, ...) is too big to commit: with TRACKPARSE_UNICODE_FULL=1 the test runs
 * `node scripts/gen_unicode.mjs --cases <dir>` and compares everything it writes.
 *
 * The c/ directory is found from __FILE__, or from TRACKPARSE_C_DIR when set.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/unicode.h"
#include "../src/unicode_gen.h"
#include "../src/words.h"
#include "../wire/json_read.h"

static int failures = 0;

/* ------------------------------------------------------------------------------------------ */
/* Helpers                                                                                    */

/* Runs fn inside an armed scope with a fresh arena, freed afterwards. */
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

/* A string with code points written as \u{...} outside printable ASCII. */
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
  if (++failures > 20) return;
  printf("FAIL %s(", what);
  put_quoted(in);
  printf(") = ");
  put_quoted(got);
  printf(", want ");
  put_quoted(want);
  printf("\n");
}

static void check(bool ok, const char *what) {
  if (ok) return;
  if (++failures <= 20) printf("FAIL %s\n", what);
}

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

/* The c/ directory: TRACKPARSE_C_DIR, else __FILE__ without "tests/<name>.c". */
static void c_dir(char *out, size_t cap) {
  const char *env = getenv("TRACKPARSE_C_DIR");
  if (env && *env) {
    snprintf(out, cap, "%s", env);
    return;
  }
  const char *file = __FILE__;
  const char *cut = strstr(file, "tests/test_unicode.c");
  if (!cut) cut = strstr(file, "tests\\test_unicode.c");
  size_t n = cut ? (size_t)(cut - file) : 0;
  if (n == 0) {
    snprintf(out, cap, ".");
  } else {
    snprintf(out, cap, "%.*s", (int)(n - 1), file);
  }
}

/* ------------------------------------------------------------------------------------------ */
/* The cases file                                                                             */

typedef struct case_ctx {
  tp_str line;
  int groups_n;
  char groups[16][16];
  long counts[16];
  long checks;
  bool bad;
} case_ctx;

static void run_case(void *p) {
  case_ctx *c = p;
  tp_json v, g, s, x;
  if (!tp_json_parse(c->line, &v) || !tp_json_get(v, TP_S("g"), &g) ||
      !tp_json_get(v, TP_S("s"), &s)) {
    c->bad = true;
    return;
  }
  int k;
  for (k = 0; k < c->groups_n; k++) {
    if (tp_str_eq_c(g.str, c->groups[k])) break;
  }
  if (k == c->groups_n && k < 16) {
    snprintf(c->groups[k], sizeof c->groups[k], "%.*s", TP_FMT(g.str));
    c->groups_n++;
  }
  if (k < 16) c->counts[k]++;
  tp_str in = s.str;
  check_str("nfc", in, tp_nfc(in), tp_json_get(v, TP_S("c"), &x) ? x.str : in);
  check_str("nfd", in, tp_nfd(in), tp_json_get(v, TP_S("d"), &x) ? x.str : in);
  check_str("js_to_lower", in, tp_js_to_lower(in), tp_json_get(v, TP_S("l"), &x) ? x.str : in);
  c->checks += 3;
  if (tp_json_get(v, TP_S("k"), &x)) {
    size_t size;
    tp_rune r = tp_decode_rune(in, &size);
    int flags = (int)x.num;
    if (tp_is_letter(r) != ((flags & 1) != 0) || tp_is_upper_letter(r) != ((flags & 2) != 0)) {
      if (++failures <= 20) {
        printf("FAIL categories of U+%04X: want flags %d\n", (unsigned)r, flags);
      }
    }
    c->checks += 2;
  }
}

static void check_cases(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    printf("FAIL %s: cannot open\n", path);
    failures++;
    return;
  }
  static char line[1 << 20];
  case_ctx c;
  memset(&c, 0, sizeof c);
  long total = 0;
  while (fgets(line, sizeof line, f)) {
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
    if (n == 0) continue;
    c.line.p = line;
    c.line.n = n;
    c.bad = false;
    if (!scoped(run_case, &c) || c.bad) {
      printf("FAIL %s: cannot read line %ld\n", path, total + 1);
      failures++;
      break;
    }
    total++;
  }
  fclose(f);
  printf("%s: %ld cases, %ld checks, %d failures:", path, total, c.checks, failures);
  for (int k = 0; k < c.groups_n; k++) printf(" %s=%ld", c.groups[k], c.counts[k]);
  printf("\n");
  if (total == 0) {
    printf("FAIL %s: no cases\n", path);
    failures++;
  }
}

static void test_sample(const char *dir) {
  char path[4096];
  snprintf(path, sizeof path, "%s/tests/data/unicode/cases.jsonl", dir);
  check_cases(path);
}

static void test_full(const char *dir) {
  const char *env = getenv("TRACKPARSE_UNICODE_FULL");
  if (!env || strcmp(env, "1") != 0) {
    printf("skip full sweep: set TRACKPARSE_UNICODE_FULL=1 to compare every code point and more "
           "against Node\n");
    return;
  }
  const char *out = "trackparse-unicode-full";
  char cmd[8192];
  snprintf(cmd, sizeof cmd, "node \"%s/scripts/gen_unicode.mjs\" --cases %s", dir, out);
  if (system(cmd) != 0) {
    printf("FAIL %s\n", cmd);
    failures++;
    return;
  }
  char path[4096];
  snprintf(path, sizeof path, "%s/unicode-version.txt", out);
  size_t n = 0;
  char *v = read_file(path, &n);
  while (v && n > 0 && (v[n - 1] == '\n' || v[n - 1] == '\r')) n--;
  if (!v || n != strlen(TP_UNICODE_VERSION) || memcmp(v, TP_UNICODE_VERSION, n) != 0) {
    printf("FAIL this Node has Unicode %.*s, the tables are Unicode %s: compare with the same "
           "version\n",
           v ? (int)n : 0, v ? v : "", TP_UNICODE_VERSION);
    failures++;
    free(v);
    return;
  }
  free(v);
  snprintf(path, sizeof path, "%s/cases.jsonl", out);
  check_cases(path);
  /* Clean up (remove() deletes an empty directory on POSIX; elsewhere it stays). */
  remove(path);
  snprintf(path, sizeof path, "%s/unicode-version.txt", out);
  remove(path);
  remove(out);
}

/* ------------------------------------------------------------------------------------------ */
/* Examples                                                                                   */

typedef struct pair {
  const char *in, *want;
} pair;

static tp_str S(const char *s) { return tp_str_c(s); }

static void examples(void *unused) {
  (void)unused;
  static const pair lower[] = {
      {"", ""},
      {"abc", "abc"},
      {"ABC Def", "abc def"},
      {"\xce\x9f\xce\x94\xce\x9f\xce\xa3", "\xce\xbf\xce\xb4\xce\xbf\xcf\x82"}, /* final sigma */
      {"\xce\x9f\xce\x94\xce\x9f\xce\xa3"
       "A",
       "\xce\xbf\xce\xb4\xce\xbf\xcf\x83"
       "a"},
      {"\xce\xa3", "\xcf\x83"},     /* nothing cased before */
      {"A\xce\xa3.", "a\xcf\x82."}, /* "." is case-ignorable */
      {"\xc4\xb0", "i\xcc\x87"},    /* several code points */
      {"\xc3\x89"
       "COLE",
       "\xc3\xa9"
       "cole"},                                 /* Latin-1 */
      {"\xf0\x90\x90\x80", "\xf0\x90\x90\xa8"}, /* astral (Deseret) */
      {"a\xff"
       "B",
       "a\xef\xbf\xbd"
       "b"}, /* invalid UTF-8 */
  };
  for (size_t i = 0; i < TP_COUNT(lower); i++) {
    check_str("js_to_lower", S(lower[i].in), tp_js_to_lower(S(lower[i].in)), S(lower[i].want));
  }

  static const struct {
    const char *in, *c, *d;
  } norm[] = {
      {"", "", ""},
      {"abc", "abc", "abc"},
      {"e\xcc\x81", "\xc3\xa9", "e\xcc\x81"},
      {"\xc3\xa9", "\xc3\xa9", "e\xcc\x81"},
      /* reordering, then composition */
      {"\xe1\xb8\x8b\xcc\xa3", "\xe1\xb8\x8d\xcc\x87", "d\xcc\xa3\xcc\x87"},
      {"q\xcc\x87\xcc\xa3", "q\xcc\xa3\xcc\x87", "q\xcc\xa3\xcc\x87"}, /* no composite */
      {"\xe2\x84\xab", "\xc3\x85", "A\xcc\x8a"},                       /* singleton */
      {"\xcd\x84", "\xcc\x88\xcc\x81", "\xcc\x88\xcc\x81"},            /* non-starter decomp */
      {"\xea\xb0\x80", "\xea\xb0\x80", "\xe1\x84\x80\xe1\x85\xa1"},    /* Hangul LV */
      {"\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8", "\xea\xb0\x81",
       "\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8"}, /* Hangul L V T */
      {"\xea\xb0\x80\xe1\x86\xa7", "\xea\xb0\x80\xe1\x86\xa7",
       "\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa7"}, /* T base does not compose */
      {"\xe0\xad\x87\xe0\xac\xbe", "\xe0\xad\x8b", "\xe0\xad\x87\xe0\xac\xbe"}, /* two starters */
      {"a\xff"
       "e\xcc\x81",
       "a\xef\xbf\xbd\xc3\xa9",
       "a\xef\xbf\xbd"
       "e\xcc\x81"}, /* invalid */
      {"\xcc\x81"
       "e",
       "\xcc\x81"
       "e",
       "\xcc\x81"
       "e"},                                                          /* leading mark */
      {"A\xcc\x81\xcc\x81", "\xc3\x81\xcc\x81", "A\xcc\x81\xcc\x81"}, /* second mark blocked */
      {"\xf0\x9d\x85\x9e", "\xf0\x9d\x85\x97\xf0\x9d\x85\xa5",
       "\xf0\x9d\x85\x97\xf0\x9d\x85\xa5"}, /* excluded composite */
  };
  for (size_t i = 0; i < TP_COUNT(norm); i++) {
    check_str("nfc", S(norm[i].in), tp_nfc(S(norm[i].in)), S(norm[i].c));
    check_str("nfd", S(norm[i].in), tp_nfd(S(norm[i].in)), S(norm[i].d));
  }

  static const struct {
    tp_rune r;
    bool letter, upper;
  } cats[] =
      {
          {'a', true, false},     {'Z', true, true},        {'1', false, false},
          {'_', false, false},    {0xc9, true, true},       {0xe9, true, false},
          {0x1c5, true, false},   /* Lt is a letter, not Lu */
          {0x2160, false, false}, /* Nl: not L */
          {0x0301, false, false}, {0x4e00, true, false},    {0x10400, true, true},
          {-1, false, false},     {0x110000, false, false}, {0xfffd, false, false},
      };
  for (size_t i = 0; i < TP_COUNT(cats); i++) {
    if (tp_is_letter(cats[i].r) != cats[i].letter ||
        tp_is_upper_letter(cats[i].r) != cats[i].upper) {
      if (++failures <= 20) printf("FAIL categories of U+%04X\n", (unsigned)cats[i].r);
    }
  }
}

/* Normalized input comes back as the same string, without allocating (Go's AllocsPerRun test). */
static void no_copy_when_normalized(void *unused) {
  (void)unused;
  static const char *nfc_inputs[] = {
      "Noisia feat. Foreign Beggars - Shellshock (Original Mix)",
      "Bj\xc3\xb6rk - J\xc3\xb3ga",
      "\xd0\xa1\xd0\xbf\xd0\xbb\xd0\xb8\xd0\xbd - \xd0\x93\xd1\x80\xd0\xb0\xd0\xbd\xd0\xb0\xd1\x82",
      "\xec\x95\x84\xec\x9d\xb4\xec\x9c\xa0 - \xec\xa2\x8b\xec\x9d\x80 \xeb\x82\xa0",
  };
  for (size_t i = 0; i < TP_COUNT(nfc_inputs); i++) {
    tp_str s = S(nfc_inputs[i]);
    check(tp_nfc(s).p == s.p, "nfc of NFC text is the input itself");
  }
  tp_str ascii = S("Noisia feat. Foreign Beggars - Shellshock");
  check(tp_nfd(ascii).p == ascii.p, "nfd of ASCII is the input itself");
  tp_str lower = S("noisia");
  check(tp_js_to_lower(lower).p == lower.p, "js_to_lower of lowercase ASCII is the input itself");
  size_t before = tp_arena_bytes(tp_arena_current());
  for (int k = 0; k < 1000; k++) {
    (void)tp_nfc(S(nfc_inputs[1]));
    (void)tp_nfd(ascii);
  }
  check(tp_arena_bytes(tp_arena_current()) == before, "no allocation for normalized input");
}

/* xorshift64*, a small seeded PRNG. */
static uint64_t rng_state = 1;
static uint64_t rng_next(void) {
  rng_state ^= rng_state >> 12;
  rng_state ^= rng_state << 25;
  rng_state ^= rng_state >> 27;
  return rng_state * 0x2545F4914F6CDD1DULL;
}
static size_t rng_int(size_t n) { return (size_t)(rng_next() % n); }

/* Normalization and lowercasing are total, and NFC/NFD have the algebraic properties of UAX #15
 * on random input, invalid UTF-8 included. */
static void total_and_stable(void *unused) {
  (void)unused;
  static const tp_rune pool[] = {'a',    'A',     'e',    0x3a3,  0x301,  0x323,  0x345,  0x334,
                                 0xe9,   0x1e0b,  0x1100, 0x1161, 0x11a8, 0xac00, 0x212b, 0x344,
                                 0xf900, 0x1d15e, 0xb47,  0xb3e,  0x130,  0x2e,   0x1f600};
  rng_state = 0x9e3779b97f4a7c15ULL;
  for (int i = 0; i < 20000; i++) {
    tp_buf b = {0};
    for (size_t k = rng_int(16); k > 0; k--) {
      if (rng_int(10) == 0) {
        tp_buf_byte(&b, (char)(0x80 + rng_int(0x80))); /* a stray byte */
        continue;
      }
      tp_buf_rune(&b, pool[rng_int(TP_COUNT(pool))]);
    }
    tp_str s = tp_buf_done(&b);
    tp_str c = tp_nfc(s), d = tp_nfd(s);
    (void)tp_js_to_lower(s);
    if (!tp_utf8_valid(c) || !tp_utf8_valid(d)) {
      check(false, "invalid UTF-8 out of nfc/nfd");
      return;
    }
    if (!tp_str_eq(tp_nfc(c), c) || !tp_str_eq(tp_nfd(d), d) || !tp_str_eq(tp_nfc(d), c) ||
        !tp_str_eq(tp_nfd(c), d)) {
      printf("FAIL not stable: ");
      put_quoted(s);
      printf("\n");
      failures++;
      return;
    }
  }
  /* A long run of marks in reverse class order takes the stable sort path. */
  tp_str long_run = tp_concat(TP_S("a"), tp_repeat(TP_S("\xcc\x81\xcc\xa3\xcd\x85\xcc\xb4"), 2500));
  tp_str got = tp_nfd(long_run);
  check(tp_has_prefix(got, TP_S("a\xcc\xb4\xcc\xb4")) && tp_str_eq(tp_nfd(got), got),
        "long run of marks ordered");
}

int main(void) {
  char dir[2048];
  c_dir(dir, sizeof dir);
  check(scoped(examples, NULL), "examples ran");
  check(scoped(no_copy_when_normalized, NULL), "no-copy test ran");
  check(scoped(total_and_stable, NULL), "random test ran");
  test_sample(dir);
  test_full(dir);
  if (failures) {
    printf("test_unicode: %d failures\n", failures);
    return 1;
  }
  printf("test_unicode: ok\n");
  return 0;
}
