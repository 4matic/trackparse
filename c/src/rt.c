/* rt.c: the runtime declared in rt.h (allocator hook, arenas, scopes, once, strings, runes,
 * vectors, byte builders, the string map, the stable sort and the tp_json helpers).
 * Numbers live in num.c. */
#include "rt.h"

#include <stdlib.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

/* -------------------------------------------------------------------------------------------- */
/* Allocator hook                                                                               */

static tp_allocator rt_allocator; /* zero: malloc/free */

TP_PRIVATE void tp_set_allocator(const tp_allocator *a) {
  if (a == NULL || a->malloc_fn == NULL || a->free_fn == NULL) {
    memset(&rt_allocator, 0, sizeof rt_allocator);
  } else {
    rt_allocator = *a;
  }
}

TP_PRIVATE void *tp_sys_malloc(size_t size) {
  if (size == 0) size = 1;
  if (rt_allocator.malloc_fn != NULL) return rt_allocator.malloc_fn(size, rt_allocator.user);
  return malloc(size);
}

TP_PRIVATE void tp_sys_free(void *ptr) {
  if (ptr == NULL) return;
  if (rt_allocator.free_fn != NULL) {
    rt_allocator.free_fn(ptr, rt_allocator.user);
  } else {
    free(ptr);
  }
}

/* -------------------------------------------------------------------------------------------- */
/* Arenas                                                                                       */

/* A chunk: this header, then the data (TP_ALIGN-aligned). */
typedef struct tp_chunk {
  struct tp_chunk *next;
  size_t cap;  /* data bytes */
  size_t used; /* end of the last allocation (not rounded up, so tp_realloc can test it exactly) */
  size_t pad_;
} tp_chunk;

#define TP_CHUNK_HDR ((sizeof(tp_chunk) + TP_ALIGN - 1) / TP_ALIGN * TP_ALIGN)
#define TP_CHUNK_FIRST ((size_t)64 * 1024)
#define TP_CHUNK_MAX ((size_t)8 * 1024 * 1024)

struct tp_arena {
  tp_chunk *head;   /* the chunk small allocations come from; others follow */
  size_t next_size; /* data size of the next regular chunk */
  size_t bytes;     /* taken from the system allocator, this struct included */
};

static TP_TLS tp_arena *rt_cur;
static TP_TLS jmp_buf *rt_jmp;

static unsigned char *rt_chunk_data(tp_chunk *c) { return (unsigned char *)c + TP_CHUNK_HDR; }

TP_PRIVATE tp_arena *tp_arena_new(void) {
  tp_arena *a = (tp_arena *)tp_sys_malloc(sizeof *a);
  if (a == NULL) return NULL;
  a->head = NULL;
  a->next_size = TP_CHUNK_FIRST;
  a->bytes = sizeof *a;
  return a;
}

TP_PRIVATE void tp_arena_free(tp_arena *a) {
  tp_chunk *c;
  if (a == NULL) return;
  c = a->head;
  while (c != NULL) {
    tp_chunk *next = c->next;
    tp_sys_free(c);
    c = next;
  }
  if (rt_cur == a) rt_cur = NULL;
  tp_sys_free(a);
}

TP_PRIVATE size_t tp_arena_bytes(const tp_arena *a) { return a == NULL ? 0 : a->bytes; }

TP_PRIVATE TP_NORETURN void tp_oom(void) {
  if (rt_jmp != NULL) longjmp(*rt_jmp, 1);
  abort(); /* tp_alloc outside an armed scope: a bug in the caller */
}

TP_PRIVATE size_t tp_size_add(size_t a, size_t b) {
  if (a > SIZE_MAX - b) tp_oom();
  return a + b;
}

TP_PRIVATE size_t tp_size_mul(size_t a, size_t b) {
  if (a != 0 && b > SIZE_MAX / a) tp_oom();
  return a * b;
}

static size_t rt_align_up(size_t n) { return tp_size_add(n, TP_ALIGN - 1) / TP_ALIGN * TP_ALIGN; }

static tp_chunk *rt_chunk_new(tp_arena *a, size_t data) {
  tp_chunk *c = (tp_chunk *)tp_sys_malloc(tp_size_add(TP_CHUNK_HDR, data));
  if (c == NULL) tp_oom();
  c->next = NULL;
  c->cap = data;
  c->used = 0;
  a->bytes += TP_CHUNK_HDR + data;
  return c;
}

/* n bytes (not zeroed) from arena a. */
static void *rt_arena_take(tp_arena *a, size_t n) {
  tp_chunk *c = a->head;
  size_t at;
  if (c != NULL) {
    at = rt_align_up(c->used);
    if (at <= c->cap && c->cap - at >= n) {
      c->used = at + n;
      return rt_chunk_data(c) + at;
    }
  }
  if (n > a->next_size / 4) {
    /* A large block gets its own chunk, kept behind the head so the head keeps serving. */
    tp_chunk *big = rt_chunk_new(a, rt_align_up(n));
    big->used = n;
    if (c == NULL) {
      a->head = big;
    } else {
      big->next = c->next;
      c->next = big;
    }
    return rt_chunk_data(big);
  }
  c = rt_chunk_new(a, a->next_size);
  if (a->next_size < TP_CHUNK_MAX) a->next_size *= 2;
  c->next = a->head;
  a->head = c;
  c->used = n;
  return rt_chunk_data(c);
}

TP_PRIVATE void *tp_alloc(size_t n) {
  void *p;
  if (rt_cur == NULL) tp_oom();
  p = rt_arena_take(rt_cur, n);
  memset(p, 0, n);
  return p;
}

TP_PRIVATE void *tp_alloc_array(size_t n, size_t size) { return tp_alloc(tp_size_mul(n, size)); }

TP_PRIVATE void *tp_realloc(void *p, size_t old_size, size_t new_size) {
  void *q;
  if (p == NULL || old_size == 0) return tp_alloc(new_size);
  if (rt_cur != NULL && rt_cur->head != NULL) {
    /* The last allocation of the head chunk grows in place. */
    tp_chunk *c = rt_cur->head;
    unsigned char *d = rt_chunk_data(c);
    if ((unsigned char *)p >= d && (unsigned char *)p <= d + c->used &&
        (unsigned char *)p + old_size == d + c->used) {
      size_t at = (size_t)((unsigned char *)p - d);
      if (new_size <= old_size) return p;
      if (c->cap - at >= new_size) {
        memset(d + c->used, 0, new_size - old_size);
        c->used = at + new_size;
        return p;
      }
    }
  }
  q = tp_alloc(new_size);
  memcpy(q, p, old_size < new_size ? old_size : new_size);
  return q;
}

TP_PRIVATE tp_arena *tp_arena_use(tp_arena *a) {
  tp_arena *prev = rt_cur;
  rt_cur = a;
  return prev;
}

TP_PRIVATE tp_arena *tp_arena_current(void) { return rt_cur; }

/* -------------------------------------------------------------------------------------------- */
/* Scopes                                                                                       */

TP_PRIVATE bool tp_scope_open(tp_scope *s) {
  s->arena = tp_arena_new();
  if (s->arena == NULL) return false;
  s->saved_arena = rt_cur;
  s->saved_jmp = rt_jmp;
  rt_cur = s->arena;
  return true;
}

TP_PRIVATE void tp_scope_arm(tp_scope *s) { rt_jmp = &s->jmp; }

TP_PRIVATE void tp_scope_close(tp_scope *s) {
  rt_cur = s->saved_arena;
  rt_jmp = s->saved_jmp;
}

TP_PRIVATE void tp_scope_abort(tp_scope *s) {
  rt_cur = s->saved_arena;
  rt_jmp = s->saved_jmp;
  tp_arena_free(s->arena);
  s->arena = NULL;
}

/* -------------------------------------------------------------------------------------------- */
/* Once                                                                                         */

/* Not pthread_once: PTHREAD_ONCE_INIT is not all-zero bytes everywhere (macOS), and a build that
 * ran out of memory must be retried by a later call. Instead a process-wide lock guards the
 * published value, and build() runs outside the lock (it may call tp_once_get for another table).
 * Two threads may both build; the first to publish wins and the other frees its arena, which is
 * harmless because build() is pure. once->opaque is unused. */
#if defined(_WIN32)
static SRWLOCK rt_once_mutex = SRWLOCK_INIT;
static void rt_once_lock(void) { AcquireSRWLockExclusive(&rt_once_mutex); }
static void rt_once_unlock(void) { ReleaseSRWLockExclusive(&rt_once_mutex); }
#else
static pthread_mutex_t rt_once_mutex = PTHREAD_MUTEX_INITIALIZER;
static void rt_once_lock(void) { (void)pthread_mutex_lock(&rt_once_mutex); }
static void rt_once_unlock(void) { (void)pthread_mutex_unlock(&rt_once_mutex); }
#endif

TP_PRIVATE void *tp_once_get(tp_once *once, void *(*build)(void)) {
  void *v;
  tp_scope s;
  void *volatile built = NULL;
  rt_once_lock();
  v = once->value;
  rt_once_unlock();
  if (v != NULL) return v;
  if (!tp_scope_open(&s)) return NULL;
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    return NULL;
  }
  tp_scope_arm(&s);
  built = build();
  tp_scope_close(&s);
  if (built == NULL) {
    tp_arena_free(s.arena);
    return NULL;
  }
  rt_once_lock();
  v = once->value;
  if (v == NULL) {
    once->value = built;
    once->opaque[0] = s.arena; /* kept forever */
    v = built;
  }
  rt_once_unlock();
  if (v != built) tp_arena_free(s.arena);
  return v;
}

/* -------------------------------------------------------------------------------------------- */
/* Strings                                                                                      */

TP_PRIVATE tp_str tp_str_c(const char *s) {
  tp_str r;
  if (s == NULL) return TP_EMPTY;
  r.p = s;
  r.n = strlen(s);
  return r;
}

TP_PRIVATE tp_str tp_str_copy(const char *p, size_t n) {
  char *d = (char *)tp_alloc(tp_size_add(n, 1));
  tp_str r;
  if (n) memcpy(d, p, n);
  r.p = d;
  r.n = n;
  return r;
}

TP_PRIVATE const char *tp_str_cstr(tp_str s) { return tp_str_copy(s.p, s.n).p; }

TP_PRIVATE bool tp_str_eq(tp_str a, tp_str b) {
  return a.n == b.n && (a.n == 0 || memcmp(a.p, b.p, a.n) == 0);
}

TP_PRIVATE bool tp_str_eq_c(tp_str a, const char *b) { return tp_str_eq(a, tp_str_c(b)); }

TP_PRIVATE int tp_str_cmp(tp_str a, tp_str b) {
  size_t n = a.n < b.n ? a.n : b.n;
  int c = n ? memcmp(a.p, b.p, n) : 0;
  if (c != 0) return c < 0 ? -1 : 1;
  if (a.n == b.n) return 0;
  return a.n < b.n ? -1 : 1;
}

TP_PRIVATE tp_str tp_str_slice(tp_str s, size_t from, size_t to) {
  tp_str r;
  if (to > s.n) to = s.n;
  if (from > to) from = to;
  r.p = s.p + from;
  r.n = to - from;
  return r;
}

TP_PRIVATE bool tp_has_prefix(tp_str s, tp_str prefix) {
  return s.n >= prefix.n && (prefix.n == 0 || memcmp(s.p, prefix.p, prefix.n) == 0);
}

TP_PRIVATE bool tp_has_suffix(tp_str s, tp_str suffix) {
  return s.n >= suffix.n &&
         (suffix.n == 0 || memcmp(s.p + s.n - suffix.n, suffix.p, suffix.n) == 0);
}

TP_PRIVATE tp_str tp_trim_prefix(tp_str s, tp_str prefix) {
  return tp_has_prefix(s, prefix) ? tp_str_slice(s, prefix.n, s.n) : s;
}

TP_PRIVATE tp_str tp_trim_suffix(tp_str s, tp_str suffix) {
  return tp_has_suffix(s, suffix) ? tp_str_slice(s, 0, s.n - suffix.n) : s;
}

TP_PRIVATE ptrdiff_t tp_index(tp_str s, tp_str sub) {
  size_t i;
  if (sub.n == 0) return 0;
  if (sub.n > s.n) return -1;
  for (i = 0; i + sub.n <= s.n; i++) {
    const void *hit = memchr(s.p + i, (unsigned char)sub.p[0], s.n - sub.n + 1 - i);
    if (hit == NULL) return -1;
    i = (size_t)((const char *)hit - s.p);
    if (memcmp(s.p + i, sub.p, sub.n) == 0) return (ptrdiff_t)i;
  }
  return -1;
}

TP_PRIVATE ptrdiff_t tp_index_byte(tp_str s, unsigned char c) {
  const void *hit = s.n ? memchr(s.p, c, s.n) : NULL;
  return hit == NULL ? -1 : (ptrdiff_t)((const char *)hit - s.p);
}

TP_PRIVATE ptrdiff_t tp_last_index_byte(tp_str s, unsigned char c) {
  size_t i = s.n;
  while (i > 0) {
    i--;
    if ((unsigned char)s.p[i] == c) return (ptrdiff_t)i;
  }
  return -1;
}

TP_PRIVATE bool tp_contains(tp_str s, tp_str sub) { return tp_index(s, sub) >= 0; }

TP_PRIVATE bool tp_contains_rune(tp_str s, int32_t r) {
  size_t i = 0;
  while (i < s.n) {
    size_t len;
    tp_rune c = tp_decode_rune(tp_str_slice(s, i, s.n), &len);
    if (c == r) return true;
    i += len;
  }
  return false;
}

TP_PRIVATE bool tp_contains_any(tp_str s, tp_str chars) {
  size_t i = 0;
  while (i < chars.n) {
    size_t len;
    tp_rune c = tp_decode_rune(tp_str_slice(chars, i, chars.n), &len);
    if (tp_contains_rune(s, c)) return true;
    i += len;
  }
  return false;
}

TP_PRIVATE tp_str tp_concat(tp_str a, tp_str b) {
  char *d = (char *)tp_alloc(tp_size_add(tp_size_add(a.n, b.n), 1));
  tp_str r;
  if (a.n) memcpy(d, a.p, a.n);
  if (b.n) memcpy(d + a.n, b.p, b.n);
  r.p = d;
  r.n = a.n + b.n;
  return r;
}

TP_PRIVATE tp_str tp_concat3(tp_str a, tp_str b, tp_str c) {
  size_t n = tp_size_add(tp_size_add(a.n, b.n), c.n);
  char *d = (char *)tp_alloc(tp_size_add(n, 1));
  tp_str r;
  if (a.n) memcpy(d, a.p, a.n);
  if (b.n) memcpy(d + a.n, b.p, b.n);
  if (c.n) memcpy(d + a.n + b.n, c.p, c.n);
  r.p = d;
  r.n = n;
  return r;
}

TP_PRIVATE tp_str tp_repeat(tp_str s, size_t count) {
  size_t n = tp_size_mul(s.n, count), i;
  char *d = (char *)tp_alloc(tp_size_add(n, 1));
  tp_str r;
  for (i = 0; i < count && s.n; i++) memcpy(d + i * s.n, s.p, s.n);
  r.p = d;
  r.n = n;
  return r;
}

/* strings.ReplaceAll; an empty old inserts with before every rune and at the end. */
TP_PRIVATE tp_str tp_replace_all(tp_str s, tp_str old, tp_str with) {
  tp_buf b = {0, 0, 0};
  size_t i = 0;
  if (old.n == 0) {
    tp_buf_str(&b, with);
    while (i < s.n) {
      size_t len;
      (void)tp_decode_rune(tp_str_slice(s, i, s.n), &len);
      tp_buf_bytes(&b, s.p + i, len);
      tp_buf_str(&b, with);
      i += len;
    }
    return tp_buf_done(&b);
  }
  for (;;) {
    ptrdiff_t at = tp_index(tp_str_slice(s, i, s.n), old);
    if (at < 0) break;
    tp_buf_bytes(&b, s.p + i, (size_t)at);
    tp_buf_str(&b, with);
    i += (size_t)at + old.n;
  }
  if (i == 0) return s;
  tp_buf_bytes(&b, s.p + i, s.n - i);
  return tp_buf_done(&b);
}

TP_PRIVATE tp_str tp_itoa(int64_t v) {
  char tmp[24];
  size_t n = 0;
  uint64_t u = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
  char out[24];
  size_t k = 0;
  do {
    tmp[n++] = (char)('0' + (int)(u % 10));
    u /= 10;
  } while (u != 0);
  if (v < 0) out[k++] = '-';
  while (n > 0) out[k++] = tmp[--n];
  return tp_str_copy(out, k);
}

TP_PRIVATE bool tp_atoi(tp_str s, int64_t *out) {
  size_t i = 0;
  bool neg = false;
  uint64_t u = 0, limit;
  if (s.n == 0) return false;
  if (s.p[0] == '-' || s.p[0] == '+') {
    neg = s.p[0] == '-';
    i = 1;
    if (s.n == 1) return false;
  }
  limit = neg ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
  for (; i < s.n; i++) {
    unsigned char c = (unsigned char)s.p[i];
    unsigned d;
    if (c < '0' || c > '9') return false;
    d = (unsigned)(c - '0');
    if (u > (limit - d) / 10) return false;
    u = u * 10 + d;
  }
  if (neg) {
    *out = u == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)u;
  } else {
    *out = (int64_t)u;
  }
  return true;
}

/* -------------------------------------------------------------------------------------------- */
/* UTF-8 and runes                                                                              */

/* Decode one rune at the start of p[0..n) with Go's rules: an invalid or truncated sequence, an
 * overlong form, a surrogate or a value above U+10FFFF gives (U+FFFD, 1). n > 0. */
static tp_rune rt_decode(const unsigned char *p, size_t n, size_t *len) {
  unsigned c = p[0];
  tp_rune r;
  if (c < 0x80) {
    *len = 1;
    return (tp_rune)c;
  }
  if (c >= 0xC2 && c <= 0xDF) {
    if (n >= 2 && (p[1] & 0xC0) == 0x80) {
      *len = 2;
      return (tp_rune)(((c & 0x1Fu) << 6) | (p[1] & 0x3Fu));
    }
  } else if (c >= 0xE0 && c <= 0xEF) {
    unsigned lo = c == 0xE0 ? 0xA0 : 0x80, hi = c == 0xED ? 0x9F : 0xBF;
    if (n >= 3 && p[1] >= lo && p[1] <= hi && (p[2] & 0xC0) == 0x80) {
      *len = 3;
      r = (tp_rune)(((c & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu));
      return r;
    }
  } else if (c >= 0xF0 && c <= 0xF4) {
    unsigned lo = c == 0xF0 ? 0x90 : 0x80, hi = c == 0xF4 ? 0x8F : 0xBF;
    if (n >= 4 && p[1] >= lo && p[1] <= hi && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
      *len = 4;
      r = (tp_rune)(((c & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) |
                    (p[3] & 0x3Fu));
      return r;
    }
  }
  *len = 1;
  return 0xFFFD;
}

TP_PRIVATE bool tp_utf8_valid(tp_str s) {
  const unsigned char *p = (const unsigned char *)s.p;
  size_t i = 0;
  while (i < s.n) {
    size_t len;
    tp_rune r = rt_decode(p + i, s.n - i, &len);
    if (r == 0xFFFD && len == 1) {
      /* U+FFFD itself is three bytes; a one-byte result is an error. */
      return false;
    }
    i += len;
  }
  return true;
}

TP_PRIVATE tp_rune tp_decode_rune(tp_str s, size_t *len) {
  if (s.n == 0) {
    *len = 0;
    return TP_NORUNE;
  }
  return rt_decode((const unsigned char *)s.p, s.n, len);
}

TP_PRIVATE tp_rune tp_decode_last_rune(tp_str s, size_t *len) {
  const unsigned char *p = (const unsigned char *)s.p;
  size_t end = s.n, start, lim, l;
  tp_rune r;
  if (end == 0) {
    *len = 0;
    return TP_NORUNE;
  }
  if (p[end - 1] < 0x80) {
    *len = 1;
    return (tp_rune)p[end - 1];
  }
  /* Go: back up over at most 3 continuation bytes to a start byte. */
  lim = end >= 4 ? end - 4 : 0;
  start = end - 1;
  while (start > lim && (p[start] & 0xC0) == 0x80) start--;
  r = rt_decode(p + start, end - start, &l);
  if (start + l != end) {
    *len = 1;
    return 0xFFFD;
  }
  *len = l;
  return r;
}

TP_PRIVATE size_t tp_utf8_encode(char out[4], tp_rune r) {
  uint32_t u = (uint32_t)r;
  if (r < 0 || u > 0x10FFFF || (u >= 0xD800 && u <= 0xDFFF)) u = 0xFFFD;
  if (u < 0x80) {
    out[0] = (char)u;
    return 1;
  }
  if (u < 0x800) {
    out[0] = (char)(0xC0 | (u >> 6));
    out[1] = (char)(0x80 | (u & 0x3F));
    return 2;
  }
  if (u < 0x10000) {
    out[0] = (char)(0xE0 | (u >> 12));
    out[1] = (char)(0x80 | ((u >> 6) & 0x3F));
    out[2] = (char)(0x80 | (u & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (u >> 18));
  out[1] = (char)(0x80 | ((u >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((u >> 6) & 0x3F));
  out[3] = (char)(0x80 | (u & 0x3F));
  return 4;
}

TP_PRIVATE size_t tp_rune_count(tp_str s) {
  size_t i = 0, count = 0;
  while (i < s.n) {
    size_t len;
    (void)rt_decode((const unsigned char *)s.p + i, s.n - i, &len);
    i += len;
    count++;
  }
  return count;
}

TP_PRIVATE tp_runes tp_runes_of(tp_str s) {
  tp_runes out;
  tp_rune *d = (tp_rune *)tp_alloc_array(s.n, sizeof(tp_rune)); /* runes <= bytes */
  size_t i = 0, k = 0;
  while (i < s.n) {
    size_t len;
    d[k++] = rt_decode((const unsigned char *)s.p + i, s.n - i, &len);
    i += len;
  }
  out.p = d;
  out.n = k;
  return out;
}

TP_PRIVATE tp_str tp_runes_str(tp_runes r) {
  tp_buf b = {0, 0, 0};
  tp_buf_runes(&b, r);
  return tp_buf_done(&b);
}

TP_PRIVATE tp_runes tp_runes_slice(tp_runes r, size_t from, size_t to) {
  tp_runes out;
  if (to > r.n) to = r.n;
  if (from > to) from = to;
  out.p = r.p + from;
  out.n = to - from;
  return out;
}

TP_PRIVATE tp_runes tp_runes_clone(tp_runes r) {
  tp_runes out;
  tp_rune *d = (tp_rune *)tp_alloc_array(r.n, sizeof(tp_rune));
  if (r.n) memcpy(d, r.p, r.n * sizeof(tp_rune));
  out.p = d;
  out.n = r.n;
  return out;
}

TP_PRIVATE bool tp_runes_eq(tp_runes a, tp_runes b) {
  return a.n == b.n && (a.n == 0 || memcmp(a.p, b.p, a.n * sizeof(tp_rune)) == 0);
}

/* -------------------------------------------------------------------------------------------- */
/* Vectors                                                                                      */

TP_PRIVATE void tp_vec_grow_(void *pp, size_t *cap, size_t need, size_t elem) {
  void *p;
  size_t newcap;
  if (need <= *cap) return;
  newcap = *cap < 4 ? 4 : tp_size_mul(*cap, 2);
  if (newcap < need) newcap = need;
  memcpy(&p, pp, sizeof p);
  p = tp_realloc(p, tp_size_mul(*cap, elem), tp_size_mul(newcap, elem));
  memcpy(pp, &p, sizeof p);
  *cap = newcap;
}

TP_PRIVATE void tp_vec_push_n_(void *pp, size_t *n, size_t *cap, size_t k, size_t elem) {
  size_t need = tp_size_add(*n, k);
  void *p;
  memcpy(&p, pp, sizeof p);
  /* Storage even for k == 0, so &v.p[n] is never an offset from NULL. */
  if (need > *cap || p == NULL) tp_vec_grow_(pp, cap, need > *cap ? need : *cap + 1, elem);
  memcpy(&p, pp, sizeof p);
  if (k) memset((unsigned char *)p + *n * elem, 0, k * elem);
  *n = need;
}

TP_PRIVATE tp_runes tp_runevec_view(tp_runevec v) {
  tp_runes r;
  r.p = v.p;
  r.n = v.n;
  return r;
}

TP_PRIVATE tp_str tp_join(tp_strvec parts, tp_str sep) {
  tp_buf b = {0, 0, 0};
  size_t i;
  if (parts.n == 0) return TP_EMPTY;
  if (parts.n == 1) return parts.p[0];
  for (i = 0; i < parts.n; i++) {
    if (i) tp_buf_str(&b, sep);
    tp_buf_str(&b, parts.p[i]);
  }
  return tp_buf_done(&b);
}

TP_PRIVATE tp_strvec tp_split(tp_str s, tp_str sep) {
  tp_strvec out = {0, 0, 0};
  size_t i = 0;
  if (sep.n == 0) { /* Go: one element per rune */
    while (i < s.n) {
      size_t len;
      (void)tp_decode_rune(tp_str_slice(s, i, s.n), &len);
      tp_push(out, tp_str_slice(s, i, i + len));
      i += len;
    }
    return out;
  }
  for (;;) {
    ptrdiff_t at = tp_index(tp_str_slice(s, i, s.n), sep);
    if (at < 0) break;
    tp_push(out, tp_str_slice(s, i, i + (size_t)at));
    i += (size_t)at + sep.n;
  }
  tp_push(out, tp_str_slice(s, i, s.n));
  return out;
}

TP_PRIVATE bool tp_strvec_eq(tp_strvec a, tp_strvec b) {
  size_t i;
  if (a.n != b.n) return false;
  for (i = 0; i < a.n; i++) {
    if (!tp_str_eq(a.p[i], b.p[i])) return false;
  }
  return true;
}

TP_PRIVATE bool tp_strvec_has(tp_strvec v, tp_str s) {
  size_t i;
  for (i = 0; i < v.n; i++) {
    if (tp_str_eq(v.p[i], s)) return true;
  }
  return false;
}

/* -------------------------------------------------------------------------------------------- */
/* Byte builder                                                                                 */

/* Room for k more bytes plus a NUL. */
static void rt_buf_reserve(tp_buf *b, size_t k) {
  size_t need = tp_size_add(tp_size_add(b->n, k), 1);
  size_t newcap;
  if (need <= b->cap) return;
  newcap = b->cap < 32 ? 32 : tp_size_mul(b->cap, 2);
  if (newcap < need) newcap = need;
  b->p = (char *)tp_realloc(b->p, b->cap, newcap);
  b->cap = newcap;
}

TP_PRIVATE void tp_buf_byte(tp_buf *b, char c) {
  rt_buf_reserve(b, 1);
  b->p[b->n++] = c;
}

TP_PRIVATE void tp_buf_bytes(tp_buf *b, const char *p, size_t n) {
  if (n == 0) return;
  rt_buf_reserve(b, n);
  memcpy(b->p + b->n, p, n);
  b->n += n;
}

TP_PRIVATE void tp_buf_str(tp_buf *b, tp_str s) { tp_buf_bytes(b, s.p, s.n); }

TP_PRIVATE void tp_buf_cstr(tp_buf *b, const char *s) { tp_buf_str(b, tp_str_c(s)); }

TP_PRIVATE void tp_buf_rune(tp_buf *b, tp_rune r) {
  char tmp[4];
  size_t n = tp_utf8_encode(tmp, r);
  tp_buf_bytes(b, tmp, n);
}

TP_PRIVATE void tp_buf_runes(tp_buf *b, tp_runes r) {
  size_t i;
  rt_buf_reserve(b, r.n); /* at least one byte each */
  for (i = 0; i < r.n; i++) tp_buf_rune(b, r.p[i]);
}

TP_PRIVATE void tp_buf_int(tp_buf *b, int64_t v) { tp_buf_str(b, tp_itoa(v)); }

/* The returned string keeps its NUL: cap is set to n, so the next append copies into a new block
 * (tp_realloc only grows in place when the old size it is given ends the last allocation, and
 * the real block is at least n + 1 bytes). */
TP_PRIVATE tp_str tp_buf_done(tp_buf *b) {
  tp_str r;
  if (b->n == 0) return TP_EMPTY;
  rt_buf_reserve(b, 0);
  b->p[b->n] = '\0';
  r.p = b->p;
  r.n = b->n;
  b->cap = b->n;
  return r;
}

/* -------------------------------------------------------------------------------------------- */
/* String map: open addressing, linear probing, FNV-1a                                         */

struct tp_map_slot {
  tp_str key;
  int64_t value;
  uint64_t hash; /* 0: empty (real hashes have bit 0 set) */
};

static uint64_t rt_map_hash(tp_str s) {
  uint64_t h = UINT64_C(14695981039346656037);
  size_t i;
  for (i = 0; i < s.n; i++) {
    h ^= (unsigned char)s.p[i];
    h *= UINT64_C(1099511628211);
  }
  return h | 1u;
}

static struct tp_map_slot *rt_map_find(const tp_map *m, tp_str key, uint64_t h) {
  size_t mask = m->cap - 1, i = (size_t)(h ^ (h >> 29)) & mask;
  for (;;) {
    struct tp_map_slot *s = &m->slots[i];
    if (s->hash == 0 || (s->hash == h && tp_str_eq(s->key, key))) return s;
    i = (i + 1) & mask;
  }
}

static void rt_map_grow(tp_map *m) {
  size_t old_cap = m->cap, i;
  struct tp_map_slot *old = m->slots;
  m->cap = old_cap ? tp_size_mul(old_cap, 2) : 16;
  m->slots = (struct tp_map_slot *)tp_alloc_array(m->cap, sizeof *m->slots);
  for (i = 0; i < old_cap; i++) {
    if (old[i].hash != 0) *rt_map_find(m, old[i].key, old[i].hash) = old[i];
  }
}

TP_PRIVATE void tp_map_put(tp_map *m, tp_str key, int64_t value) {
  uint64_t h = rt_map_hash(key);
  struct tp_map_slot *s;
  if (m->cap == 0 || (m->n + 1) * 2 > m->cap) rt_map_grow(m);
  s = rt_map_find(m, key, h);
  if (s->hash == 0) {
    s->hash = h;
    s->key = key;
    m->n++;
  }
  s->value = value;
}

TP_PRIVATE bool tp_map_get(const tp_map *m, tp_str key, int64_t *value) {
  struct tp_map_slot *s;
  if (m->cap == 0) return false;
  s = rt_map_find(m, key, rt_map_hash(key));
  if (s->hash == 0) return false;
  if (value != NULL) *value = s->value;
  return true;
}

TP_PRIVATE bool tp_map_has(const tp_map *m, tp_str key) { return tp_map_get(m, key, NULL); }

/* -------------------------------------------------------------------------------------------- */
/* Stable sort: insertion sort on runs of 8, then bottom-up merges (no recursion)               */

TP_PRIVATE void tp_sort_stable(void *base, size_t count, size_t size,
                               int (*cmp)(const void *a, const void *b, void *ctx), void *ctx) {
  unsigned char *a = (unsigned char *)base, *tmp, *src, *dst;
  unsigned char *hold;
  size_t run = 8, i, width;
  if (count < 2 || size == 0) return;
  hold = (unsigned char *)tp_alloc(size);
  for (i = 0; i < count; i += run) {
    size_t end = i + run < count ? i + run : count, j;
    for (j = i + 1; j < end; j++) {
      size_t k = j;
      if (cmp(a + (j - 1) * size, a + j * size, ctx) <= 0) continue;
      memcpy(hold, a + j * size, size);
      while (k > i && cmp(a + (k - 1) * size, hold, ctx) > 0) {
        memcpy(a + k * size, a + (k - 1) * size, size);
        k--;
      }
      memcpy(a + k * size, hold, size);
    }
  }
  if (count <= run) return;
  tmp = (unsigned char *)tp_alloc_array(count, size);
  src = a;
  dst = tmp;
  for (width = run; width<count; width = width> count / 2 ? count : width * 2) {
    size_t lo;
    for (lo = 0; lo < count; lo += 2 * width) {
      size_t mid = lo + width < count ? lo + width : count;
      size_t hi = mid + width < count ? mid + width : count;
      size_t l = lo, r = mid, o = lo;
      while (l < mid && r < hi) {
        if (cmp(src + r * size, src + l * size, ctx) < 0) {
          memcpy(dst + o * size, src + r * size, size);
          r++;
        } else {
          memcpy(dst + o * size, src + l * size, size);
          l++;
        }
        o++;
      }
      if (l < mid) memcpy(dst + o * size, src + l * size, (mid - l) * size);
      if (r < hi) memcpy(dst + o * size + (mid - l) * size, src + r * size, (hi - r) * size);
    }
    {
      unsigned char *t = src;
      src = dst;
      dst = t;
    }
  }
  if (src != a) memcpy(a, src, count * size);
}

/* -------------------------------------------------------------------------------------------- */
/* JSON values                                                                                  */

TP_PRIVATE tp_json tp_json_null(void) {
  tp_json v;
  memset(&v, 0, sizeof v);
  v.kind = TP_JSON_NULL;
  v.str = TP_EMPTY;
  return v;
}

TP_PRIVATE tp_json tp_json_bool(bool b) {
  tp_json v = tp_json_null();
  v.kind = TP_JSON_BOOL;
  v.b = b;
  return v;
}

TP_PRIVATE tp_json tp_json_num(double n) {
  tp_json v = tp_json_null();
  v.kind = TP_JSON_NUMBER;
  v.num = n;
  return v;
}

TP_PRIVATE tp_json tp_json_str(tp_str s) {
  tp_json v = tp_json_null();
  v.kind = TP_JSON_STRING;
  v.str = s;
  return v;
}

TP_PRIVATE bool tp_json_get(tp_json v, tp_str key, tp_json *out) {
  size_t i;
  if (v.kind != TP_JSON_OBJECT) return false;
  for (i = 0; i < v.obj_n; i++) {
    if (tp_str_eq(v.obj[i].key, key)) {
      if (out != NULL) *out = v.obj[i].val;
      return true;
    }
  }
  return false;
}
