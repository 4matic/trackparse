/* digits.h: ASCII digit scanning shared by R4.1 (prefix.c) and the tracklist time scanner (R13.2)
 * (go/digits.go). */
#ifndef TP_DIGITS_H
#define TP_DIGITS_H

#include "rt.h"

/* The length of the run of ASCII digits starting at from (0 out of range). */
TP_PRIVATE ptrdiff_t tp_digit_run(tp_runes s, ptrdiff_t from);
/* The value of a run of ASCII digits, saturating at 2^40 (callers pass at most a few digits).
 * Other code points count as 0. */
TP_PRIVATE int64_t tp_digits_value(tp_runes s);
/* The value of exactly two digits at i (not followed by a third) when it is at most 59. */
TP_PRIVATE bool tp_two_digits_up_to_59(tp_runes s, ptrdiff_t i, int *out);

#endif /* TP_DIGITS_H */
