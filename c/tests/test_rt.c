/* test_rt.c: src/rt.c (arenas, scopes and out of memory, once, strings, runes, vectors,
 * builders, the map, the stable sort, tp_json helpers). */
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "../src/rt.h"

static int failures;
static long checks;

#define CHECK(cond)                                                                                \
  do {                                                                                             \
    checks++;                                                                                      \
    if (!(cond)) {                                                                                 \
      failures++;                                                                                  \
      fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond);                            \
    }                                                                                              \
  } while (0)

#define STR_IS(s, lit) CHECK(tp_str_eq_c((s), (lit)))

/* -------------------------------------------------------------------------------------------- */
/* A counting allocator that fails the n-th malloc                                              */

static long live_blocks; /* mallocs minus frees */
static long malloc_calls;
static long fail_at = -1; /* fail the malloc with this index (0-based), -1: never */

static void *count_malloc(size_t size, void *user) {
  void *p;
  (void)user;
  if (malloc_calls++ == fail_at) return NULL;
  p = malloc(size);
  if (p != NULL) live_blocks++;
  return p;
}

static void *count_realloc(void *ptr, size_t size, void *user) {
  (void)user;
  return realloc(ptr, size);
}

static void count_free(void *ptr, void *user) {
  (void)user;
  if (ptr != NULL) live_blocks--;
  free(ptr);
}

static const tp_allocator counting = {count_malloc, count_realloc, count_free, NULL};

/* -------------------------------------------------------------------------------------------- */

typedef TP_VEC(int) intvec;

typedef struct pair {
  int key;
  int order;
} pair;
typedef TP_VEC(pair) pairvec;

static int cmp_pair(const void *a, const void *b, void *ctx) {
  const pair *x = (const pair *)a, *y = (const pair *)b;
  (void)ctx;
  return (x->key > y->key) - (x->key < y->key);
}

static uint64_t rng = 88172645463325252u;
static uint64_t next_rand(void) {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}

/* Work that touches every allocation path, for the out-of-memory sweep. Returns a checksum. */
static size_t workload(void) {
  tp_strvec parts = {0, 0, 0};
  tp_buf b = {0, 0, 0};
  tp_map m = {0, 0, 0};
  pairvec v = {0, 0, 0};
  size_t i, sum = 0;
  for (i = 0; i < 300; i++) {
    tp_str s = tp_itoa((int64_t)i * 7919);
    tp_push(parts, s);
    tp_map_put(&m, s, (int64_t)i);
    tp_buf_str(&b, s);
    {
      pair p;
      p.key = (int)(i % 17);
      p.order = (int)i;
      tp_push(v, p);
    }
  }
  (void)tp_alloc(200000);                         /* a chunk of its own */
  for (i = 0; i < 40000; i++) (void)tp_alloc(50); /* several regular chunks */
  tp_sort_stable(v.p, v.n, sizeof *v.p, cmp_pair, NULL);
  sum += tp_join(parts, TP_S(", ")).n + tp_buf_done(&b).n + tp_runes_of(TP_S("añb€😀")).n;
  sum += tp_number_str(0.1).n;
  {
    int64_t x = 0;
    if (tp_map_get(&m, TP_S("7919"), &x)) sum += (size_t)x;
  }
  return sum;
}

static void test_oom(void) {
  static size_t want;
  static long n, total;
  tp_set_allocator(&counting);
  /* The reference run. */
  {
    tp_scope s;
    CHECK(tp_scope_open(&s));
    if (setjmp(s.jmp) != 0) {
      tp_scope_abort(&s);
      CHECK(0 && "reference run ran out of memory");
      return;
    }
    tp_scope_arm(&s);
    want = workload();
    tp_scope_close(&s);
    tp_arena_free(s.arena);
  }
  CHECK(live_blocks == 0);
  total = malloc_calls;
  CHECK(total >= 6);
  printf("test_rt: out-of-memory sweep over %ld allocation points\n", total + 1);
  /* Fail each allocation in turn. */
  for (n = 0; n <= total; n++) {
    static tp_scope s; /* static: written before setjmp only, safe across the longjmp */
    static volatile int aborted;
    static size_t got;
    malloc_calls = 0;
    fail_at = n;
    aborted = 0;
    got = 0;
    if (!tp_scope_open(&s)) {
      CHECK(n == 0); /* only the arena header itself can fail here */
      CHECK(live_blocks == 0);
      continue;
    }
    if (setjmp(s.jmp) != 0) {
      tp_scope_abort(&s);
      aborted = 1;
    } else {
      tp_scope_arm(&s);
      got = workload();
      tp_scope_close(&s);
      tp_arena_free(s.arena);
    }
    CHECK(tp_arena_current() == NULL);
    CHECK(live_blocks == 0);
    if (n < total) {
      CHECK(aborted);
    } else {
      CHECK(!aborted && got == want);
    }
  }
  fail_at = -1;
  /* Nested scopes: an inner failure unwinds only the inner scope. */
  {
    static tp_scope outer, inner;
    static volatile int inner_failed;
    inner_failed = 0;
    CHECK(tp_scope_open(&outer));
    if (setjmp(outer.jmp) != 0) {
      tp_scope_abort(&outer);
      CHECK(0 && "outer scope aborted");
    } else {
      tp_str kept;
      tp_scope_arm(&outer);
      kept = tp_str_copy("outer", 5);
      CHECK(tp_scope_open(&inner));
      if (setjmp(inner.jmp) != 0) {
        tp_scope_abort(&inner);
        inner_failed = 1;
      } else {
        tp_scope_arm(&inner);
        fail_at = malloc_calls; /* the next malloc fails */
        (void)tp_alloc(1 << 20);
        CHECK(0 && "not reached");
      }
      fail_at = -1;
      CHECK(inner_failed);
      CHECK(tp_arena_current() == outer.arena);
      STR_IS(kept, "outer");
      (void)tp_alloc(100); /* the outer scope still works */
      tp_scope_close(&outer);
      tp_arena_free(outer.arena);
    }
    CHECK(live_blocks == 0);
  }
  /* Size overflow longjmps like an allocation failure. */
  {
    static tp_scope s;
    static volatile int hit;
    hit = 0;
    CHECK(tp_scope_open(&s));
    if (setjmp(s.jmp) != 0) {
      tp_scope_abort(&s);
      hit = 1;
    } else {
      tp_scope_arm(&s);
      (void)tp_alloc_array(SIZE_MAX / 2, 4);
      tp_scope_close(&s);
      tp_arena_free(s.arena);
    }
    CHECK(hit);
    CHECK(live_blocks == 0);
  }
  tp_set_allocator(NULL);
}

static void test_arena(void) {
  tp_arena *a;
  unsigned char *p, *q;
  size_t i;
  bool zero = true;
  a = tp_arena_new();
  CHECK(a != NULL);
  CHECK(tp_arena_use(a) == NULL);
  CHECK(tp_arena_current() == a);
  for (i = 1; i < 2000; i += 37) {
    p = (unsigned char *)tp_alloc(i);
    CHECK(((uintptr_t)p % TP_ALIGN) == 0);
    memset(p, 0xAB, i);
  }
  p = (unsigned char *)tp_alloc(1000000);
  CHECK(((uintptr_t)p % TP_ALIGN) == 0);
  for (i = 0; i < 1000000; i++) zero = zero && p[i] == 0;
  CHECK(zero);
  CHECK(tp_alloc(0) != NULL);
  CHECK(tp_arena_bytes(a) >= 1000000);
  /* Grow in place, and zero the new tail. */
  p = (unsigned char *)tp_alloc(16);
  memset(p, 1, 16);
  q = (unsigned char *)tp_realloc(p, 16, 64);
  CHECK(q == p);
  CHECK(q[15] == 1 && q[16] == 0 && q[63] == 0);
  (void)tp_alloc(8);
  p = (unsigned char *)tp_realloc(q, 64, 128); /* no longer last: copies */
  CHECK(p != q && p[0] == 1 && p[63] == 0 && p[127] == 0);
  CHECK(tp_size_add(1, 2) == 3 && tp_size_mul(3, 4) == 12);
  tp_arena_use(NULL);
  tp_arena_free(a);
}

static void test_strings(void) {
  tp_str s = TP_S("hello, world");
  int64_t x;
  STR_IS(tp_str_c("abc"), "abc");
  CHECK(tp_str_c(NULL).n == 0);
  CHECK(tp_str_cstr(TP_S("ab"))[2] == '\0');
  CHECK(tp_str_cmp(TP_S("a"), TP_S("b")) < 0 && tp_str_cmp(TP_S("ab"), TP_S("a")) > 0);
  CHECK(tp_str_cmp(TP_S(""), TP_S("")) == 0 && tp_str_cmp(TP_S("\xff"), TP_S("a")) > 0);
  STR_IS(tp_str_slice(s, 7, 100), "world");
  STR_IS(tp_str_slice(s, 9, 3), "");
  CHECK(tp_has_prefix(s, TP_S("hell")) && !tp_has_prefix(s, TP_S("world")));
  CHECK(tp_has_suffix(s, TP_S("world")) && tp_has_suffix(s, TP_EMPTY));
  STR_IS(tp_trim_prefix(s, TP_S("hello, ")), "world");
  STR_IS(tp_trim_suffix(s, TP_S("x")), "hello, world");
  CHECK(tp_index(s, TP_S("o")) == 4 && tp_index(s, TP_S("wor")) == 7);
  CHECK(tp_index(s, TP_S("worlds")) == -1 && tp_index(s, TP_EMPTY) == 0);
  CHECK(tp_index(TP_S("aaab"), TP_S("aab")) == 1);
  CHECK(tp_index_byte(s, 'w') == 7 && tp_last_index_byte(s, 'o') == 8);
  CHECK(tp_index_byte(s, 'z') == -1 && tp_last_index_byte(TP_EMPTY, 'a') == -1);
  CHECK(tp_contains(s, TP_S(", ")) && !tp_contains(s, TP_S(";")));
  CHECK(tp_contains_rune(TP_S("a€b"), 0x20AC) && !tp_contains_rune(TP_S("ab"), 'c'));
  CHECK(tp_contains_any(TP_S("a€b"), TP_S("x€")) && !tp_contains_any(TP_S("ab"), TP_S("")));
  STR_IS(tp_concat(TP_S("ab"), TP_S("cd")), "abcd");
  STR_IS(tp_concat3(TP_S("a"), TP_EMPTY, TP_S("c")), "ac");
  STR_IS(tp_repeat(TP_S("ab"), 3), "ababab");
  STR_IS(tp_repeat(TP_S("ab"), 0), "");
  STR_IS(tp_replace_all(TP_S("a-b-c"), TP_S("-"), TP_S("--")), "a--b--c");
  STR_IS(tp_replace_all(TP_S("aaa"), TP_S("aa"), TP_S("b")), "ba");
  STR_IS(tp_replace_all(TP_S("a€"), TP_EMPTY, TP_S(".")), ".a.€.");
  STR_IS(tp_replace_all(TP_S("abc"), TP_S("x"), TP_S("y")), "abc");
  STR_IS(tp_itoa(0), "0");
  STR_IS(tp_itoa(-42), "-42");
  STR_IS(tp_itoa(INT64_MIN), "-9223372036854775808");
  STR_IS(tp_itoa(INT64_MAX), "9223372036854775807");
  CHECK(tp_atoi(TP_S("123"), &x) && x == 123);
  CHECK(tp_atoi(TP_S("-007"), &x) && x == -7);
  CHECK(tp_atoi(TP_S("+5"), &x) && x == 5);
  CHECK(tp_atoi(TP_S("-9223372036854775808"), &x) && x == INT64_MIN);
  CHECK(tp_atoi(TP_S("9223372036854775807"), &x) && x == INT64_MAX);
  CHECK(!tp_atoi(TP_S("9223372036854775808"), &x));
  CHECK(!tp_atoi(TP_S(""), &x) && !tp_atoi(TP_S("-"), &x) && !tp_atoi(TP_S("1a"), &x));
  CHECK(!tp_atoi(TP_S(" 1"), &x) && !tp_atoi(TP_S("1_0"), &x));
  CHECK(tp_utf8_valid(TP_S("a€😀\xef\xbf\xbd")));
  CHECK(!tp_utf8_valid(TP_S("\xff")) && !tp_utf8_valid(TP_S("\xc0\x80")));
  CHECK(!tp_utf8_valid(TP_S("\xed\xa0\x80")) && !tp_utf8_valid(TP_S("\xf4\x90\x80\x80")));
  CHECK(!tp_utf8_valid(TP_S("\xe2\x82")));
  {
    tp_strvec parts = tp_split(TP_S("a,b,,c"), TP_S(","));
    tp_strvec one = tp_split(TP_EMPTY, TP_S(","));
    CHECK(parts.n == 4);
    STR_IS(parts.p[2], "");
    STR_IS(parts.p[3], "c");
    CHECK(one.n == 1 && one.p[0].n == 0);
    STR_IS(tp_join(parts, TP_S("+")), "a+b++c");
    CHECK(tp_join(one, TP_S("+")).n == 0);
    CHECK(tp_strvec_eq(parts, tp_split(TP_S("a,b,,c"), TP_S(","))));
    CHECK(!tp_strvec_eq(parts, one));
    CHECK(tp_strvec_has(parts, TP_S("c")) && !tp_strvec_has(parts, TP_S("d")));
    CHECK(tp_split(TP_S("a€"), TP_EMPTY).n == 2);
  }
}

static void test_runes(void) {
  tp_runes r = tp_runes_of(TP_S("a\xc3\xb1\xe2\x82\xac\xf0\x9f\x98\x80"));
  size_t len;
  char enc[4];
  CHECK(r.n == 4 && r.p[0] == 'a' && r.p[1] == 0xF1 && r.p[2] == 0x20AC && r.p[3] == 0x1F600);
  STR_IS(tp_runes_str(r), "a\xc3\xb1\xe2\x82\xac\xf0\x9f\x98\x80");
  CHECK(tp_rune_count(TP_S("a\xc3\xb1\xff")) == 3);
  {
    tp_runes bad = tp_runes_of(TP_S("\xff\xe2\x82"));
    CHECK(bad.n == 3 && bad.p[0] == 0xFFFD && bad.p[1] == 0xFFFD && bad.p[2] == 0xFFFD);
  }
  CHECK(tp_runes_eq(tp_runes_slice(r, 1, 3), tp_runes_clone(tp_runes_slice(r, 1, 3))));
  CHECK(tp_runes_slice(r, 3, 1).n == 0 && tp_runes_slice(r, 2, 99).n == 2);
  CHECK(!tp_runes_eq(r, tp_runes_slice(r, 0, 3)));
  CHECK(tp_decode_rune(TP_S("\xe2\x82\xac!"), &len) == 0x20AC && len == 3);
  CHECK(tp_decode_rune(TP_EMPTY, &len) == TP_NORUNE && len == 0);
  CHECK(tp_decode_rune(TP_S("\xc0\x80"), &len) == 0xFFFD && len == 1);
  CHECK(tp_decode_last_rune(TP_S("!\xe2\x82\xac"), &len) == 0x20AC && len == 3);
  CHECK(tp_decode_last_rune(TP_S("ab"), &len) == 'b' && len == 1);
  CHECK(tp_decode_last_rune(TP_S("\xe2\x82"), &len) == 0xFFFD && len == 1);
  CHECK(tp_decode_last_rune(TP_S("\x80\x80\x80\x80\x80"), &len) == 0xFFFD && len == 1);
  CHECK(tp_decode_last_rune(TP_EMPTY, &len) == TP_NORUNE && len == 0);
  CHECK(tp_utf8_encode(enc, 0x10FFFF) == 4 && (unsigned char)enc[0] == 0xF4);
  CHECK(tp_utf8_encode(enc, 0xD800) == 3 && (unsigned char)enc[0] == 0xEF);
  CHECK(tp_utf8_encode(enc, 0) == 1 && enc[0] == 0);
  {
    tp_runevec v = {0, 0, 0};
    tp_push(v, 'x');
    tp_push(v, 0x20AC);
    STR_IS(tp_runes_str(tp_runevec_view(v)), "x\xe2\x82\xac");
  }
}

static void test_vectors(void) {
  intvec v = {0, 0, 0}, w = {0, 0, 0}, c;
  int *p;
  int i;
  bool ok = true;
  for (i = 0; i < 1000; i++) tp_push(v, i);
  CHECK(v.n == 1000 && tp_last(v) == 999);
  for (i = 0; i < 1000; i++) ok = ok && v.p[i] == i;
  CHECK(ok);
  tp_remove(v, 10, 990); /* 0..9, 990..999 */
  CHECK(v.n == 20 && v.p[10] == 990);
  p = tp_push_n(v, 5); /* zeroed although the slots held old elements */
  CHECK(v.n == 25 && p == &v.p[20] && p[0] == 0 && p[4] == 0);
  tp_insert(v, 0, -1);
  tp_insert(v, v.n, 77);
  CHECK(v.p[0] == -1 && v.p[1] == 0 && tp_last(v) == 77 && v.n == 27);
  tp_push(w, 5);
  tp_append(w, v);
  CHECK(w.n == 28 && w.p[0] == 5 && w.p[1] == -1);
  tp_clone(c, w);
  CHECK(c.p != w.p && c.n == w.n && memcmp(c.p, w.p, c.n * sizeof(int)) == 0);
  {
    intvec e = {0, 0, 0};
    tp_append(w, e);
    CHECK(w.n == 28);
    p = tp_push_n(e, 0);
    CHECK(e.n == 0);
    (void)p;
  }
}

static void test_buf(void) {
  tp_buf b = {0, 0, 0};
  tp_str first, second;
  size_t i;
  CHECK(tp_buf_done(&b).n == 0);
  tp_buf_cstr(&b, "abc");
  tp_buf_byte(&b, '-');
  tp_buf_rune(&b, 0x20AC);
  tp_buf_int(&b, -12);
  {
    tp_rune rs[2] = {'x', 0x1F600};
    tp_runes r;
    r.p = rs;
    r.n = 2;
    tp_buf_runes(&b, r);
  }
  first = tp_buf_done(&b);
  STR_IS(first, "abc-\xe2\x82\xac-12x\xf0\x9f\x98\x80");
  CHECK(first.p[first.n] == '\0');
  /* Appending after done leaves the returned string, and its NUL, alone. */
  for (i = 0; i < 100; i++) tp_buf_byte(&b, 'z');
  CHECK(first.p[first.n] == '\0');
  STR_IS(first, "abc-\xe2\x82\xac-12x\xf0\x9f\x98\x80");
  second = tp_buf_done(&b);
  CHECK(second.n == first.n + 100 && second.p[second.n] == '\0');
  tp_buf_bytes(&b, "", 0);
  CHECK(tp_buf_done(&b).n == second.n);
}

static void test_map(void) {
  tp_map m = {0, 0, 0};
  tp_strvec keys = {0, 0, 0};
  int64_t v = 0;
  size_t i;
  bool ok = true;
  CHECK(!tp_map_get(&m, TP_S("x"), &v) && !tp_map_has(&m, TP_EMPTY));
  for (i = 0; i < 5000; i++) tp_push(keys, tp_itoa((int64_t)i));
  for (i = 0; i < 5000; i++) tp_map_put(&m, keys.p[i], (int64_t)i * 3);
  tp_map_put(&m, TP_S("17"), -1); /* overwrite */
  tp_map_put(&m, TP_EMPTY, 42);
  CHECK(m.n == 5001);
  for (i = 0; i < 5000; i++) {
    ok = ok && tp_map_get(&m, keys.p[i], &v) && v == (i == 17 ? -1 : (int64_t)i * 3);
  }
  CHECK(ok);
  CHECK(tp_map_get(&m, TP_EMPTY, &v) && v == 42);
  CHECK(!tp_map_has(&m, TP_S("5000")) && tp_map_has(&m, TP_S("4999")));
}

static void test_sort(void) {
  size_t sizes[] = {0, 1, 2, 7, 8, 9, 16, 17, 100, 1000, 4097};
  size_t si;
  for (si = 0; si < TP_COUNT(sizes); si++) {
    size_t n = sizes[si], i;
    pair *a = (pair *)tp_alloc_array(n, sizeof(pair));
    bool ok = true;
    int keys = (int)(si % 3 == 0 ? 3 : 1000);
    for (i = 0; i < n; i++) {
      a[i].key = (int)(next_rand() % (uint64_t)keys);
      a[i].order = (int)i;
    }
    tp_sort_stable(a, n, sizeof(pair), cmp_pair, NULL);
    for (i = 1; i < n; i++) {
      ok = ok &&
           (a[i - 1].key < a[i].key || (a[i - 1].key == a[i].key && a[i - 1].order < a[i].order));
    }
    CHECK(ok);
  }
}

static void test_json(void) {
  tp_json members_vals[2];
  tp_json_member members[2];
  tp_json obj = tp_json_null(), got;
  CHECK(obj.kind == TP_JSON_NULL);
  CHECK(tp_json_bool(true).kind == TP_JSON_BOOL && tp_json_bool(true).b);
  CHECK(tp_json_num(2.5).kind == TP_JSON_NUMBER && tp_json_num(2.5).num == 2.5);
  STR_IS(tp_json_str(TP_S("x")).str, "x");
  members_vals[0] = tp_json_num(1);
  members_vals[1] = tp_json_str(TP_S("v"));
  members[0].key = TP_S("a");
  members[0].val = members_vals[0];
  members[1].key = TP_S("b");
  members[1].val = members_vals[1];
  obj.kind = TP_JSON_OBJECT;
  obj.obj = members;
  obj.obj_n = 2;
  CHECK(tp_json_get(obj, TP_S("b"), &got) && got.kind == TP_JSON_STRING);
  CHECK(!tp_json_get(obj, TP_S("c"), &got));
  CHECK(!tp_json_get(tp_json_num(1), TP_S("a"), &got));
}

/* -------------------------------------------------------------------------------------------- */
/* Once                                                                                         */

static tp_once once_a = TP_ONCE_INIT, once_b = TP_ONCE_INIT, once_fail = TP_ONCE_INIT;
static long build_b_calls;

static void *build_b(void) {
  int *v = (int *)tp_alloc(sizeof(int));
  /* Two threads may build at once (tp_once_get publishes one result), so count atomically. */
#if defined(_MSC_VER)
  InterlockedIncrement(&build_b_calls);
#else
  __atomic_fetch_add(&build_b_calls, 1, __ATOMIC_RELAXED);
#endif
  *v = 7;
  return v;
}

static void *build_a(void) { /* nests another once */
  int *b = (int *)tp_once_get(&once_b, build_b);
  int *v = (int *)tp_alloc(sizeof(int));
  *v = b == NULL ? -1 : *b * 6;
  return v;
}

static void *build_fail(void) {
  (void)tp_alloc_array(SIZE_MAX / 4, 8);
  return NULL;
}

#if defined(_WIN32)
static DWORD WINAPI once_thread(LPVOID arg) {
  *(int **)arg = (int *)tp_once_get(&once_a, build_a);
  return 0;
}
#else
static void *once_thread(void *arg) {
  *(int **)arg = (int *)tp_once_get(&once_a, build_a);
  return NULL;
}
#endif

static void test_once(void) {
  int *results[8];
  int i;
#if defined(_WIN32)
  HANDLE th[8];
  for (i = 0; i < 8; i++) th[i] = CreateThread(NULL, 0, once_thread, &results[i], 0, NULL);
  for (i = 0; i < 8; i++) {
    WaitForSingleObject(th[i], INFINITE);
    CloseHandle(th[i]);
  }
#else
  pthread_t th[8];
  for (i = 0; i < 8; i++) CHECK(pthread_create(&th[i], NULL, once_thread, &results[i]) == 0);
  for (i = 0; i < 8; i++) pthread_join(th[i], NULL);
#endif
  for (i = 0; i < 8; i++) {
    CHECK(results[i] != NULL && results[i] == results[0] && *results[i] == 42);
  }
  CHECK(tp_once_get(&once_a, build_a) == results[0]);
  CHECK(build_b_calls >= 1);
  CHECK(tp_once_get(&once_fail, build_fail) == NULL);
  CHECK(tp_once_get(&once_fail, build_fail) == NULL); /* retried, still fails */
}

int main(void) {
  static tp_scope s;
  test_oom();
  test_arena();
  test_once();
  if (!tp_scope_open(&s)) return 2;
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    fprintf(stderr, "out of memory\n");
    return 2;
  }
  tp_scope_arm(&s);
  test_strings();
  test_runes();
  test_vectors();
  test_buf();
  test_map();
  test_sort();
  test_json();
  tp_scope_close(&s);
  tp_arena_free(s.arena);
  printf("test_rt: %ld checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
