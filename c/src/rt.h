/* rt.h: the internal runtime every src/ file builds on.
 *
 * The C port is a file-by-file translation of the Go port (the go/ files), which mirrors js/src.
 * This header gives C the pieces of Go the pipeline uses: strings (tp_str), rune slices
 * (tp_runes), growable vectors (TP_VEC), byte builders (tp_buf), string-keyed maps (tp_map), a
 * stable sort, and memory that never needs freeing inside the pipeline.
 *
 * Memory model
 * ------------
 * Every allocation inside a public call comes from the *current arena* (thread-local). The public
 * entry point (src/trackparse.c) opens a scope: a fresh arena plus a jmp_buf. Allocation never
 * returns NULL: when the system allocator fails, tp_alloc longjmps back to the scope, which frees
 * the arena and returns NULL to the caller. So pipeline code never checks for NULL and never
 * calls free; everything dies with the arena. Pipeline code must not hold resources other than
 * arena memory (no FILE*, no malloc), so a longjmp can never leak.
 *
 * All memory from tp_alloc is zeroed (Go zero values) and aligned to TP_ALIGN.
 *
 * Linkage
 * -------
 * Every non-static internal function and global is declared TP_PRIVATE. The installed library is
 * built from the amalgamation (one translation unit, TP_PRIVATE = static), so no internal symbol
 * is ever visible to users, even in a static library. Tests and wire/ use the separate-files
 * build, where TP_PRIVATE is empty.
 *
 * Determinism rules (AGENTS.md, PLAN.md hazards)
 * -----------------------------------------------
 * - No <ctype.h>, no strtod/atof/strtol, no printf-family on floats, no locale-dependent call.
 * - No qsort: use tp_sort_stable.
 * - Never iterate a tp_map for anything that reaches the output; keep ordered vectors for that.
 * - Bytes are unsigned char when inspected; runes are int32_t; TP_NORUNE (-1) means "out of range".
 * - No recursion in src/ unless its depth is bounded by a small constant.
 */
#ifndef TP_RT_H
#define TP_RT_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* In the amalgamation (scripts/amalgamate.mjs) every internal name is static, and a given
 * configuration may never call some of them: the unused attribute keeps GCC and Clang quiet
 * about that, and MSVC's matching /W4 warning (C4505, unreferenced function removed) is turned
 * off for the amalgamation. TP_PRIVATE_DATA declares a generated table in data_gen.h /
 * unicode_gen.h (their own fallback definition is guarded by #ifndef, so this one wins). */
#if defined(__GNUC__) || defined(__clang__)
#define TP_UNUSED __attribute__((unused))
#else
#define TP_UNUSED
#endif
#if defined(TRACKPARSE_AMALGAMATION) && defined(_MSC_VER)
#pragma warning(disable : 4505)
#endif
#ifdef TRACKPARSE_AMALGAMATION
#define TP_PRIVATE static TP_UNUSED
#define TP_PRIVATE_DATA static TP_UNUSED
#else
#define TP_PRIVATE
#define TP_PRIVATE_DATA extern
#endif

#if defined(_MSC_VER)
#define TP_TLS __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define TP_TLS __thread
#else
#define TP_TLS _Thread_local
#endif

#if defined(__GNUC__) || defined(__clang__)
#define TP_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define TP_NORETURN __declspec(noreturn)
#else
#define TP_NORETURN
#endif

#define TP_ALIGN 16
#define TP_NORUNE ((tp_rune) - 1)
#define TP_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* -------------------------------------------------------------------------------------------- */
/* Allocator hook and arenas                                                                    */

typedef struct tp_allocator {
  void *(*malloc_fn)(size_t size, void *user);
  void *(*realloc_fn)(void *ptr, size_t size, void *user);
  void (*free_fn)(void *ptr, void *user);
  void *user;
} tp_allocator;

/* The process-wide allocator (trackparse_set_allocator writes it). Defaults to malloc/free. */
TP_PRIVATE void *tp_sys_malloc(size_t size); /* NULL on failure, never longjmps */
TP_PRIVATE void tp_sys_free(void *ptr);
TP_PRIVATE void tp_set_allocator(const tp_allocator *a);

typedef struct tp_arena tp_arena;

/* A new empty arena, or NULL when the system allocator fails (never longjmps). */
TP_PRIVATE tp_arena *tp_arena_new(void);
TP_PRIVATE void tp_arena_free(tp_arena *a);
/* Bytes taken from the system allocator so far (for tests and the bench). */
TP_PRIVATE size_t tp_arena_bytes(const tp_arena *a);

/* A scope makes an arena current and catches out-of-memory. Pattern (the only place that may
 * call setjmp is the public entry point):
 *
 *   tp_scope s;
 *   if (!tp_scope_open(&s)) return NULL;          // could not even create the arena
 *   if (setjmp(s.jmp) != 0) { tp_scope_abort(&s); return NULL; }   // out of memory
 *   tp_scope_arm(&s);
 *   ... pipeline code: tp_alloc never fails ...
 *   tp_scope_close(&s);                            // restores the previous arena; s.arena stays
 *                                                  // alive, the caller owns or frees it
 *
 * Variables written after setjmp and read in the abort branch must be volatile. Scopes nest
 * (a scope saves and restores the previous arena and handler).
 */
typedef struct tp_scope {
  tp_arena *arena;
  tp_arena *saved_arena;
  jmp_buf *saved_jmp;
  jmp_buf jmp;
} tp_scope;

TP_PRIVATE bool tp_scope_open(tp_scope *s);
TP_PRIVATE void tp_scope_arm(tp_scope *s);
TP_PRIVATE void tp_scope_close(tp_scope *s);
/* After the longjmp: restore, free s->arena. */
TP_PRIVATE void tp_scope_abort(tp_scope *s);
/* Make another arena current inside an armed scope (used to build a result into its own arena
 * while scratch data lives in the scope's). Returns the previous one. */
TP_PRIVATE tp_arena *tp_arena_use(tp_arena *a);
TP_PRIVATE tp_arena *tp_arena_current(void);

/* Zeroed, TP_ALIGN-aligned memory from the current arena. Longjmps on failure. n == 0 returns a
 * valid non-NULL pointer. */
TP_PRIVATE void *tp_alloc(size_t n);
/* Checked n * size (longjmps on overflow, like an allocation failure). */
TP_PRIVATE void *tp_alloc_array(size_t n, size_t size);
/* Grow an arena block: copies min(old, n) bytes into a new block; the old one is abandoned. */
TP_PRIVATE void *tp_realloc(void *p, size_t old_size, size_t new_size);
TP_PRIVATE TP_NORETURN void tp_oom(void);
/* Size arithmetic that longjmps on overflow. */
TP_PRIVATE size_t tp_size_add(size_t a, size_t b);
TP_PRIVATE size_t tp_size_mul(size_t a, size_t b);

/* Run build() once per process and cache its result (built into a private arena that lives
 * forever). Thread-safe. Returns NULL if building ran out of memory; callers then build into the
 * current arena instead. Used for the default tables (Go's sync.OnceValue). */
typedef struct tp_once {
  void *opaque[2]; /* platform once-control storage, zero-initialized */
  void *value;
} tp_once;
#define TP_ONCE_INIT {{0, 0}, 0}
TP_PRIVATE void *tp_once_get(tp_once *once, void *(*build)(void));

/* -------------------------------------------------------------------------------------------- */
/* Strings (Go string): UTF-8 bytes, not NUL-terminated in general, never mutated after creation */

typedef struct tp_str {
  const char *p;
  size_t n;
} tp_str;

#define TP_S(lit) ((tp_str){(lit), sizeof(lit) - 1})
#define TP_EMPTY ((tp_str){"", 0})
/* printf("%.*s", TP_FMT(s)) for debugging only; never in output paths. */
#define TP_FMT(s) (int)(s).n, (s).p

TP_PRIVATE tp_str tp_str_c(const char *nul_terminated); /* view, no copy */
TP_PRIVATE tp_str tp_str_copy(const char *p, size_t n); /* arena copy, NUL-terminated */
TP_PRIVATE const char *tp_str_cstr(tp_str s);           /* arena copy with a trailing NUL */
TP_PRIVATE bool tp_str_eq(tp_str a, tp_str b);
TP_PRIVATE bool tp_str_eq_c(tp_str a, const char *b);
TP_PRIVATE int tp_str_cmp(tp_str a, tp_str b); /* bytewise, like Go's < on strings */
TP_PRIVATE tp_str tp_str_slice(tp_str s, size_t from, size_t to); /* byte offsets, clamped */
TP_PRIVATE bool tp_has_prefix(tp_str s, tp_str prefix);
TP_PRIVATE bool tp_has_suffix(tp_str s, tp_str suffix);
TP_PRIVATE tp_str tp_trim_prefix(tp_str s, tp_str prefix);
TP_PRIVATE tp_str tp_trim_suffix(tp_str s, tp_str suffix);
/* Byte index of the first / last occurrence, or -1. */
TP_PRIVATE ptrdiff_t tp_index(tp_str s, tp_str sub);
TP_PRIVATE ptrdiff_t tp_index_byte(tp_str s, unsigned char c);
TP_PRIVATE ptrdiff_t tp_last_index_byte(tp_str s, unsigned char c);
TP_PRIVATE bool tp_contains(tp_str s, tp_str sub);
TP_PRIVATE bool tp_contains_rune(tp_str s, int32_t r);
TP_PRIVATE bool tp_contains_any(tp_str s, tp_str chars); /* any rune of chars */
TP_PRIVATE tp_str tp_concat(tp_str a, tp_str b);
TP_PRIVATE tp_str tp_concat3(tp_str a, tp_str b, tp_str c);
TP_PRIVATE tp_str tp_repeat(tp_str s, size_t count);
TP_PRIVATE tp_str tp_replace_all(tp_str s, tp_str old, tp_str with);
TP_PRIVATE tp_str tp_itoa(int64_t v); /* strconv.Itoa / FormatInt base 10 */
/* strconv.Atoi on ASCII digits with an optional leading '-' or '+'; false on anything else or on
 * overflow of int64. */
TP_PRIVATE bool tp_atoi(tp_str s, int64_t *out);
TP_PRIVATE bool tp_utf8_valid(tp_str s);

/* -------------------------------------------------------------------------------------------- */
/* Runes (Go rune / []rune)                                                                     */

typedef int32_t tp_rune;

/* A read-only rune slice (Go []rune used as a view). */
typedef struct tp_runes {
  const tp_rune *p;
  size_t n;
} tp_runes;

/* Decode UTF-8 (the input must be valid: the API sanitizes first; invalid bytes give U+FFFD). */
TP_PRIVATE tp_runes tp_runes_of(tp_str s);
TP_PRIVATE tp_str tp_runes_str(tp_runes r); /* encode to an arena string */
TP_PRIVATE tp_runes tp_runes_slice(tp_runes r, size_t from, size_t to); /* clamped, no copy */
TP_PRIVATE tp_runes tp_runes_clone(tp_runes r);
TP_PRIVATE bool tp_runes_eq(tp_runes a, tp_runes b);
TP_PRIVATE size_t tp_rune_count(tp_str s); /* utf8.RuneCountInString */
/* utf8.DecodeRuneInString / DecodeLastRuneInString on valid UTF-8: the rune and its byte length
 * (TP_NORUNE, 0 on an empty string). */
TP_PRIVATE tp_rune tp_decode_rune(tp_str s, size_t *len);
TP_PRIVATE tp_rune tp_decode_last_rune(tp_str s, size_t *len);
TP_PRIVATE size_t tp_utf8_encode(char out[4], tp_rune r); /* r must be a valid scalar */

/* -------------------------------------------------------------------------------------------- */
/* Vectors (Go slices you append to)                                                            */

/* Declare a vector type:  typedef TP_VEC(tp_str) tp_strvec;
 * The zero value is an empty vector. Elements live in the current arena. Passing a vector by
 * value copies the header (like a Go slice header): appending to the copy does not change the
 * original's length, exactly as in Go. */
#define TP_VEC(T)                                                                                  \
  struct {                                                                                         \
    T *p;                                                                                          \
    size_t n;                                                                                      \
    size_t cap;                                                                                    \
  }

/* Grow the vector whose element pointer is at pp (the address of a T *; read and written with
 * memcpy, so no T ** to void ** aliasing) to hold at least need elements. */
TP_PRIVATE void tp_vec_grow_(void *pp, size_t *cap, size_t need, size_t elem);
/* Grow by k zeroed elements (zeroed even when the slots held earlier elements). */
TP_PRIVATE void tp_vec_push_n_(void *pp, size_t *n, size_t *cap, size_t k, size_t elem);

/* Append one element (an expression of the element type). */
#define tp_push(v, x)                                                                              \
  do {                                                                                             \
    if ((v).n == (v).cap) tp_vec_grow_(&(v).p, &(v).cap, (v).n + 1, sizeof *(v).p);                \
    (v).p[(v).n++] = (x);                                                                          \
  } while (0)

/* Make room for k more elements and return a pointer to the first new one (zeroed). k is
 * evaluated twice: it must have no side effects. */
#define tp_push_n(v, k)                                                                            \
  (tp_vec_push_n_(&(v).p, &(v).n, &(v).cap, (k), sizeof *(v).p), &(v).p[(v).n - (k)])

/* Append all elements of another vector of the same type. */
#define tp_append(v, w)                                                                            \
  do {                                                                                             \
    size_t tp_k_ = (w).n;                                                                          \
    if (tp_k_) {                                                                                   \
      if ((v).n + tp_k_ > (v).cap) tp_vec_grow_(&(v).p, &(v).cap, (v).n + tp_k_, sizeof *(v).p);   \
      memcpy(&(v).p[(v).n], (w).p, tp_k_ * sizeof *(v).p);                                         \
      (v).n += tp_k_;                                                                              \
    }                                                                                              \
  } while (0)

/* Insert x at index i (0 <= i <= n). */
#define tp_insert(v, i, x)                                                                         \
  do {                                                                                             \
    size_t tp_i_ = (i);                                                                            \
    if ((v).n == (v).cap) tp_vec_grow_(&(v).p, &(v).cap, (v).n + 1, sizeof *(v).p);                \
    memmove(&(v).p[tp_i_ + 1], &(v).p[tp_i_], ((v).n - tp_i_) * sizeof *(v).p);                    \
    (v).p[tp_i_] = (x);                                                                            \
    (v).n++;                                                                                       \
  } while (0)

/* Remove elements [i, j). */
#define tp_remove(v, i, j)                                                                         \
  do {                                                                                             \
    size_t tp_a_ = (i), tp_b_ = (j);                                                               \
    memmove(&(v).p[tp_a_], &(v).p[tp_b_], ((v).n - tp_b_) * sizeof *(v).p);                        \
    (v).n -= tp_b_ - tp_a_;                                                                        \
  } while (0)

/* A copy with its own storage (slices.Clone). Use: tp_clone(dst, src). */
#define tp_clone(dst, src)                                                                         \
  do {                                                                                             \
    (dst).n = (dst).cap = 0;                                                                       \
    (dst).p = NULL;                                                                                \
    tp_append(dst, src);                                                                           \
  } while (0)

#define tp_last(v) ((v).p[(v).n - 1])

/* Common vector types. */
typedef TP_VEC(tp_str) tp_strvec;
typedef TP_VEC(tp_rune) tp_runevec;
typedef TP_VEC(int64_t) tp_intvec;
typedef TP_VEC(double) tp_numvec;

TP_PRIVATE tp_runes tp_runevec_view(tp_runevec v);
TP_PRIVATE tp_str tp_join(tp_strvec parts, tp_str sep); /* strings.Join */
TP_PRIVATE tp_strvec tp_split(tp_str s, tp_str sep);    /* strings.Split (sep non-empty) */
TP_PRIVATE bool tp_strvec_eq(tp_strvec a, tp_strvec b);
TP_PRIVATE bool tp_strvec_has(tp_strvec v, tp_str s);

/* -------------------------------------------------------------------------------------------- */
/* Byte builder (strings.Builder / []byte append)                                               */

typedef struct tp_buf {
  char *p;
  size_t n;
  size_t cap;
} tp_buf;

TP_PRIVATE void tp_buf_byte(tp_buf *b, char c);
TP_PRIVATE void tp_buf_bytes(tp_buf *b, const char *p, size_t n);
TP_PRIVATE void tp_buf_str(tp_buf *b, tp_str s);
TP_PRIVATE void tp_buf_cstr(tp_buf *b, const char *s);
TP_PRIVATE void tp_buf_rune(tp_buf *b, tp_rune r); /* UTF-8 encode */
TP_PRIVATE void tp_buf_runes(tp_buf *b, tp_runes r);
TP_PRIVATE void tp_buf_int(tp_buf *b, int64_t v);
/* The contents as a string (NUL-terminated; the builder may keep appending afterwards, which does
 * not change the returned string). */
TP_PRIVATE tp_str tp_buf_done(tp_buf *b);

/* -------------------------------------------------------------------------------------------- */
/* String-keyed hash map (Go map[string]V for lookups only)                                     */

/* Values are int64_t (store an index into a vector for anything bigger). Never iterate a map on
 * a path that reaches the output: keep an ordered vector next to it instead. Keys are not
 * copied: they must outlive the map (arena strings or static data). */
typedef struct tp_map {
  struct tp_map_slot *slots;
  size_t cap; /* power of two, 0 when empty */
  size_t n;
} tp_map;

TP_PRIVATE void tp_map_put(tp_map *m, tp_str key, int64_t value); /* insert or overwrite */
TP_PRIVATE bool tp_map_get(const tp_map *m, tp_str key, int64_t *value);
TP_PRIVATE bool tp_map_has(const tp_map *m, tp_str key);

/* -------------------------------------------------------------------------------------------- */
/* Sorting                                                                                      */

/* Stable merge sort (slices.SortStableFunc). cmp returns <0, 0, >0. Scratch from the arena. */
TP_PRIVATE void tp_sort_stable(void *base, size_t count, size_t size,
                               int (*cmp)(const void *a, const void *b, void *ctx), void *ctx);

/* -------------------------------------------------------------------------------------------- */
/* Numbers (src/num.c): locale-independent, exact                                               */

/* JS Number(s) for the JSON number grammar and for plain decimal strings ("12", "3.25", "1e21",
 * "-0.5"): the correctly rounded double. false when s is not such a number. */
TP_PRIVATE bool tp_parse_number(tp_str s, double *out);
/* JS Number#toString (String(n)): shortest round-trip digits, "1e+21", "1e-7", "-0" as "0",
 * "NaN", "Infinity", "-Infinity". */
TP_PRIVATE void tp_buf_number(tp_buf *b, double v);
TP_PRIVATE tp_str tp_number_str(double v);
/* floor(v * 10^digits + 0.5) / 10^digits, computed without FMA contraction; digits 0..20. */
TP_PRIVATE double tp_round_half_up(double v, int digits);
/* floor(v * scale + 0.5): the "units" of R13.9.2 (Go formatTime, JS Math.floor(s * scale + 0.5)),
 * with the product rounded to a double before the add (no FMA). */
TP_PRIVATE double tp_half_up_units(double v, double scale);
/* math.Floor / Math.floor, exact, without libm (-0 and non-finite values pass through). */
TP_PRIVATE double tp_floor(double v);
TP_PRIVATE bool tp_is_nan(double v);
TP_PRIVATE bool tp_is_inf(double v);
TP_PRIVATE double tp_nan(void);
TP_PRIVATE double tp_inf(int sign);

/* -------------------------------------------------------------------------------------------- */
/* JSON values (Go jsonValue in go/json.go). The library uses them for edits and the loose JSON  */
/* options of the wire door; the reader that parses JSON text lives in wire/json_read.c.         */

typedef enum tp_json_kind {
  TP_JSON_NULL, /* the zero value */
  TP_JSON_BOOL,
  TP_JSON_NUMBER,
  TP_JSON_STRING,
  TP_JSON_ARRAY,
  TP_JSON_OBJECT
} tp_json_kind;

typedef struct tp_json tp_json;
typedef struct tp_json_member tp_json_member;

struct tp_json {
  tp_json_kind kind;
  bool b;
  double num;
  tp_str str;         /* valid UTF-8; lone surrogate escapes became U+FFFD */
  const tp_json *arr; /* kind == ARRAY */
  size_t arr_n;
  const tp_json_member *obj; /* kind == OBJECT, in document order; a repeated key keeps its first
                                place and the last value (JS object semantics) */
  size_t obj_n;
};

struct tp_json_member {
  tp_str key;
  tp_json val;
};

TP_PRIVATE tp_json tp_json_null(void);
TP_PRIVATE tp_json tp_json_bool(bool b);
TP_PRIVATE tp_json tp_json_num(double n);
TP_PRIVATE tp_json tp_json_str(tp_str s);
/* Object lookup (Go jsonValue.get): false when v is not an object or has no such key. */
TP_PRIVATE bool tp_json_get(tp_json v, tp_str key, tp_json *out);

#endif /* TP_RT_H */
