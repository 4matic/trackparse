/* tl_positions.c: R13.5, bare positions (go/tl_positions.go). */
#include "internal.h"

/* findBareCandidate is R13.5: a track part starting with 1 to 3 ASCII digits and a space (U+0020
 * only), where R4.2 took no position (position_taken), is a bare candidate. The remainder must
 * not be empty or only R0.2 whitespace. */
TP_PRIVATE const tp_bare_candidate *tp_find_bare_candidate(tp_str rest, bool position_taken) {
  tp_runes t, stripped, digits;
  ptrdiff_t n;
  bool blank = true;
  tp_bare_candidate *c;
  if (position_taken) return NULL;
  t = tp_runes_of(rest);
  n = tp_digit_run(t, 0);
  if (n < 1 || n > 3 || tp_at(t, n) != ' ') return NULL;
  stripped = tp_tail(t, n + 1);
  for (size_t k = 0; k < stripped.n; k++) {
    if (!tp_is_whitespace(stripped.p[k])) {
      blank = false;
      break;
    }
  }
  if (blank) return NULL;
  digits = tp_sub(t, 0, n);
  c = tp_alloc(sizeof *c);
  c->raw = tp_runes_str(digits);
  c->number = (ptrdiff_t)tp_digits_value(digits);
  c->stripped = tp_runes_str(stripped);
  return c;
}

/* Go confirmPositions' closure isCandidate. */
static bool tl_is_candidate(tp_bool_vec candidate_line, ptrdiff_t i) {
  return i >= 0 && i < (ptrdiff_t)candidate_line.n && candidate_line.p[i];
}

/* Go confirmPositions' closure adjacent: line j has a bare number next to b. */
static bool tl_adjacent(tp_idxvec bare, ptrdiff_t b, ptrdiff_t j) {
  ptrdiff_t o;
  if (j < 0 || j >= (ptrdiff_t)bare.n || bare.p[j] < 0) return false;
  o = bare.p[j];
  return o + 1 == b || o == b + 1;
}

/* confirmPositions is R13.5: candidate i is a position when the nearest preceding or the nearest
 * following candidate line (R13.4.2, candidate_line) also has a bare candidate equal to N - 1 or
 * N + 1. bare holds each line's bare number, or -1 for none. Linear time. */
TP_PRIVATE tp_bool_vec tp_confirm_positions(tp_idxvec bare, tp_bool_vec candidate_line) {
  ptrdiff_t n = (ptrdiff_t)bare.n, last;
  ptrdiff_t *prev = tp_alloc_array(bare.n, sizeof *prev);
  ptrdiff_t *next = tp_alloc_array(bare.n, sizeof *next);
  tp_bool_vec out = {0};
  last = -1;
  for (ptrdiff_t i = 0; i < n; i++) {
    prev[i] = last;
    if (tl_is_candidate(candidate_line, i)) last = i;
  }
  last = -1;
  for (ptrdiff_t i = n - 1; i >= 0; i--) {
    next[i] = last;
    if (tl_is_candidate(candidate_line, i)) last = i;
  }
  for (ptrdiff_t i = 0; i < n; i++) {
    ptrdiff_t b = bare.p[i];
    bool v = b >= 0 && (tl_adjacent(bare, b, prev[i]) || tl_adjacent(bare, b, next[i]));
    tp_push(out, v);
  }
  return out;
}
