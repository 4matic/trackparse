/* test_match.c: the matcher's own cases (go/match_test.go TestMatcher), plus the JSON reader's
 * edge cases the matcher relies on. */
#include <stdio.h>

#include "../wire/json_read.h"
#include "match.h"

typedef struct match_case {
  const char *expected, *actual;
  bool exact, ok;
} match_case;

static const match_case cases[] = {
    /* Rule 1: extra keys are ignored in partial mode; an explicit null must be null. */
    {"{\"title\":\"A\"}", "{\"title\":\"A\",\"year\":null}", false, true},
    {"{\"year\":null}", "{\"year\":2015}", false, false},
    {"{\"year\":null}", "{}", false, false},
    /* Rule 2: arrays match in length and order. */
    {"{\"a\":[1,2]}", "{\"a\":[1,2,3]}", false, false},
    {"{\"a\":[2,1]}", "{\"a\":[1,2]}", false, false},
    /* Rule 3: scalars strictly; numbers by value. */
    {"7", "7.0", false, true},
    {"\"a\"", "\"A\"", false, false},
    {"true", "1", false, false},
    /* Rule 4: artist shorthand. */
    {"[\"Noisia\",{\"name\":\"Foreign Beggars\",\"role\":\"featured\"}]",
     "[{\"name\":\"Noisia\",\"role\":\"primary\"},{\"name\":\"Foreign Beggars\",\"role\":"
     "\"featured\"}]",
     false, true},
    {"[\"Noisia\"]", "[{\"name\":\"Phace\"}]", false, false},
    {"[\"Noisia\"]", "[\"Noisia\"]", false, true},
    /* Rule 5: exact. */
    {"{\"a\":1}", "{\"a\":1,\"b\":2}", true, false},
    {"{\"b\":2,\"a\":1}", "{\"a\":1,\"b\":2}", true, true},
    {"[\"Noisia\"]", "[{\"name\":\"Noisia\"}]", true, false},
    /* Rule 6: a top-level specVersion is never compared. */
    {"{\"specVersion\":\"9\",\"x\":1}", "{\"specVersion\":\"0.1\",\"x\":1}", true, true},
};

/* JSON text -> its compact form after reading (NULL: must be rejected). */
typedef struct read_case {
  const char *text, *shown;
} read_case;

static const read_case reads[] = {
    {" {\"a\" : [1, 2.5, -0, 1e2], \"b\": null } ", "{\"a\":[1,2.5,0,100],\"b\":null}"},
    /* A repeated key keeps its first place and takes the last value. */
    {"{\"a\":1,\"b\":2,\"a\":3}", "{\"a\":3,\"b\":2}"},
    /* Escapes; a lone surrogate becomes U+FFFD, a pair is one code point. */
    {"\"\\n\\t\\/\\u0041\\ud83d\\ude00\"", "\"\\n\\t/A\xf0\x9f\x98\x80\""},
    {"\"\\ud800x\"", "\"\xef\xbf\xbdx\""},
    {"\"\\ud800\\u0041\"", "\"\xef\xbf\xbd"
                           "A\""},
    {"\"\\udc00\"", "\"\xef\xbf\xbd\""},
    /* Infinity travels as 1e999 (shown as null, as JSON.stringify does). */
    {"[1e999,-1e999]", "[null,null]"},
    {"[]", "[]"},
    {"{}", "{}"},
    {"", NULL},
    {"[1,]", NULL},
    {"{\"a\" 1}", NULL},
    {"nul", NULL},
    {"1 2", NULL},
    {"\"abc", NULL},
    {"\"\\u12\"", NULL},
};

static int run(void) {
  int failed = 0;
  for (size_t i = 0; i < TP_COUNT(cases); i++) {
    const match_case *c = &cases[i];
    tp_json e, a;
    tp_str d;
    if (!tp_json_parse(tp_str_c(c->expected), &e) || !tp_json_parse(tp_str_c(c->actual), &a)) {
      printf("FAIL: bad JSON in case %u\n", (unsigned)i);
      failed++;
      continue;
    }
    d = tp_match(e, a, c->exact);
    if ((d.n == 0) != c->ok) {
      printf("FAIL: match(%s, %s, exact=%d) = \"%.*s\", want ok=%d\n", c->expected, c->actual,
             (int)c->exact, TP_FMT(d), (int)c->ok);
      failed++;
    }
  }
  for (size_t i = 0; i < TP_COUNT(reads); i++) {
    const read_case *c = &reads[i];
    tp_json v;
    bool ok = tp_json_parse(tp_str_c(c->text), &v);
    if (!c->shown) {
      if (ok) {
        printf("FAIL: read(%s) accepted, want rejected\n", c->text);
        failed++;
      }
      continue;
    }
    if (!ok) {
      printf("FAIL: read(%s) rejected\n", c->text);
      failed++;
    } else if (!tp_str_eq_c(tp_json_show(v), c->shown)) {
      tp_str got = tp_json_show(v);
      printf("FAIL: read(%s) = %.*s, want %s\n", c->text, TP_FMT(got), c->shown);
      failed++;
    }
  }
  {
    /* The depth limit: 200 nested arrays read, 300 do not. */
    tp_buf deep = {0}, deeper = {0};
    tp_json v;
    for (int i = 0; i < 200; i++) tp_buf_byte(&deep, '[');
    for (int i = 0; i < 200; i++) tp_buf_byte(&deep, ']');
    for (int i = 0; i < 300; i++) tp_buf_byte(&deeper, '[');
    for (int i = 0; i < 300; i++) tp_buf_byte(&deeper, ']');
    if (!tp_json_parse(tp_buf_done(&deep), &v)) {
      printf("FAIL: 200 nested arrays rejected\n");
      failed++;
    }
    if (tp_json_parse(tp_buf_done(&deeper), &v)) {
      printf("FAIL: 300 nested arrays accepted\n");
      failed++;
    }
  }
  return failed;
}

int main(void) {
  tp_scope s;
  volatile int failed = 0;
  if (!tp_scope_open(&s)) {
    printf("FAIL: out of memory\n");
    return 1;
  }
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    printf("FAIL: out of memory\n");
    return 1;
  }
  tp_scope_arm(&s);
  failed = run();
  tp_scope_close(&s);
  tp_arena_free(s.arena);
  if (failed) {
    printf("test_match: %d failures\n", failed);
    return 1;
  }
  printf("test_match: ok (%u matcher cases, %u reader cases)\n", (unsigned)TP_COUNT(cases),
         (unsigned)TP_COUNT(reads));
  return 0;
}
