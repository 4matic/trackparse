/* digits.c: ASCII digit scanning shared by R4.1 (prefix.c) and the tracklist time scanner (R13.2)
 * (go/digits.go). */
#include "digits.h"

#include "words.h"

TP_PRIVATE ptrdiff_t tp_digit_run(tp_runes s, ptrdiff_t from) {
  ptrdiff_t i = from;
  while (tp_is_digit(tp_at(s, i))) i++;
  return i - from;
}

TP_PRIVATE int64_t tp_digits_value(tp_runes s) {
  const int64_t limit = (int64_t)1 << 40;
  int64_t n = 0;
  for (size_t i = 0; i < s.n; i++) {
    int64_t d = 0;
    if (tp_is_digit(s.p[i])) d = s.p[i] - '0';
    n = n * 10 + d;
    if (n > limit) n = limit;
  }
  return n;
}

TP_PRIVATE bool tp_two_digits_up_to_59(tp_runes s, ptrdiff_t i, int *out) {
  *out = 0;
  if (!tp_is_digit(tp_at(s, i)) || !tp_is_digit(tp_at(s, i + 1)) || tp_is_digit(tp_at(s, i + 2))) {
    return false;
  }
  int n = (int)tp_digits_value(tp_sub(s, i, i + 2));
  *out = n;
  return n <= 59;
}
