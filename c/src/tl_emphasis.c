/* tl_emphasis.c: R13.12, Markdown emphasis markers in a tracklist line (go/tl_emphasis.go). */
#include "internal.h"

/* runLength is the length of the maximal run of '*' or '_' at from (0 when there is none). */
TP_PRIVATE ptrdiff_t tp_run_length(tp_runes s, ptrdiff_t from) {
  tp_rune ch = tp_at(s, from);
  ptrdiff_t j = from;
  if (ch != '*' && ch != '_') return 0;
  while (tp_at(s, j) == ch) j++;
  return j - from;
}

/* emphasisRun is R13.2.1 / R13.2.3: the length of an emphasis run at from (a maximal run of 1 or
 * 2 '*' or '_'), else 0. It is used next to the time, where runs are read by position rather
 * than by pairing. */
TP_PRIVATE ptrdiff_t tp_emphasis_run(tp_runes s, ptrdiff_t from) {
  ptrdiff_t n = tp_run_length(s, from);
  return n <= 2 ? n : 0;
}

/* isEmphasisOuter is the R13.12.2 outer side: the line edge, R0.2 whitespace, a bracket, or a '*'
 * / '_' of another run. */
TP_PRIVATE bool tp_is_emphasis_outer(tp_rune r) {
  switch (r) {
  case TP_NORUNE:
  case '[':
  case ']':
  case '(':
  case ')':
  case '*':
  case '_':
    return true;
  default:
    return tp_is_whitespace(r);
  }
}

/* isEmphasisInner is the R13.12.2 inner side: a character that is not R0.2 whitespace. */
TP_PRIVATE bool tp_is_emphasis_inner(tp_rune r) { return r != TP_NORUNE && !tp_is_whitespace(r); }

/* emphasisStack is the stack of a run: one each for '*', '**', '_' and '__'; -1 for longer runs
 * and other characters. */
TP_PRIVATE ptrdiff_t tp_emphasis_stack(tp_rune ch, ptrdiff_t n) {
  if (ch == '*' && n == 1) return 0;
  if (ch == '*' && n == 2) return 1;
  if (ch == '_' && n == 1) return 2;
  if (ch == '_' && n == 2) return 3;
  return -1;
}

/* markRange sets marked[from:from+n], clamped. */
TP_PRIVATE void tp_mark_range(tp_bool_vec marked, ptrdiff_t from, ptrdiff_t n) {
  for (ptrdiff_t k = from; k < from + n && k < (ptrdiff_t)marked.n; k++) {
    if (k >= 0) marked.p[k] = true;
  }
}

/* withoutMarkers is R13.12: raw[from:] (the line's rest) without its emphasis markers. A run of 1
 * or 2 '*' or '_' closes when the character before it is an inner side and the one after it an
 * outer side, and opens the other way round. A closer pairs with the nearest opener of the same
 * characters; that opener and every opener after it leave the stack. One stack per run ('*',
 * '**', '_', '__') keeps this linear. Runs before from (lead and gap) never pair, except that
 * open, a lead run the gap left open (R13.12.4), pairs with a closing run of the same character
 * at the end of the line. open is TP_NORUNE when there is none. */
TP_PRIVATE tp_str tp_without_markers(tp_runes raw, ptrdiff_t from, tp_rune open) {
  ptrdiff_t len = (ptrdiff_t)raw.n;
  tp_bool_vec marked = {0};
  tp_idxvec stacks[4];
  tp_buf out = {0};
  memset(stacks, 0, sizeof stacks);
  if (len > 0) (void)tp_push_n(marked, raw.n);
  if (from < 0) from = 0;
  for (ptrdiff_t i = from; i < len;) {
    ptrdiff_t n = tp_run_length(raw, i);
    ptrdiff_t own, opener = -1;
    tp_rune prev, next;
    if (n < 1) n = 1;
    own = tp_emphasis_stack(tp_at(raw, i), n);
    prev = tp_before(raw, i);
    next = tp_at(raw, i + n);
    if (own >= 0 && tp_is_emphasis_inner(prev) && tp_is_emphasis_outer(next)) {
      if (stacks[own].n > 0) {
        opener = tp_last(stacks[own]);
        stacks[own].n--;
      }
    }
    if (opener >= 0) {
      tp_mark_range(marked, opener, n);
      tp_mark_range(marked, i, n);
      for (size_t s = 0; s < 4; s++) {
        while (stacks[s].n > 0 && tp_last(stacks[s]) > opener) stacks[s].n--;
      }
    } else if (own >= 0 && tp_is_emphasis_outer(prev) && tp_is_emphasis_inner(next)) {
      tp_push(stacks[own], i);
    }
    i += n;
  }
  /* R13.12.4: the last run of the line, after which there is only R0.2 whitespace. */
  if (open != TP_NORUNE) {
    ptrdiff_t end = len, start;
    while (end > from && tp_is_whitespace(tp_before(raw, end))) end--;
    start = end;
    while (start > from && tp_before(raw, start) == open) start--;
    if (end - start <= 2 && start > from && tp_is_emphasis_inner(tp_before(raw, start))) {
      for (ptrdiff_t k = start; k < end; k++) {
        if (marked.p[k]) break;
        marked.p[k] = true;
      }
    }
  }
  for (ptrdiff_t k = from; k < len; k++) {
    if (!marked.p[k]) tp_buf_rune(&out, raw.p[k]);
  }
  return tp_buf_done(&out);
}
