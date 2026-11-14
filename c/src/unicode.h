/* unicode.h: Unicode lookups over the generated tables in unicode_gen.c (go/unicode.go).
 *
 * General categories (R0.6), JS toLowerCase() (R0.4) and NFC/NFD (R1, R0.4). The tables come
 * from Node, the reference engine, so the results depend neither on the platform nor on the C
 * library. Never use <ctype.h>, <wctype.h> or towlower here.
 *
 * Every function is total: invalid UTF-8 is read as one U+FFFD per bad byte, as Go's `for range`
 * does (rt.c's tp_decode_rune), and nothing fails. Results are arena strings, or the input itself
 * when nothing changes.
 */
#ifndef TP_UNICODE_H
#define TP_UNICODE_H

#include "rt.h"

/* An inclusive range of code points. */
typedef struct tp_uni_range {
  tp_rune lo, hi;
} tp_uni_range;

/* Maps first, first+step, ..., up to last, each to itself plus delta. */
typedef struct tp_lower_run {
  tp_rune first, last;
  uint8_t step;
  int32_t delta;
} tp_lower_run;

/* A code point whose lowercase is several code points (UTF-8). */
typedef struct tp_lower_multi {
  tp_rune cp;
  tp_str to;
} tp_lower_multi;

/* The combining class rank of an inclusive range of code points. */
typedef struct tp_ccc_range {
  tp_rune lo, hi;
  uint8_t rank;
} tp_ccc_range;

/* The full canonical decomposition of one code point: tp_uni_decomp_pool[off .. off+len). */
typedef struct tp_decomp_entry {
  tp_rune cp;
  uint16_t off;
  uint8_t len;
} tp_decomp_entry;

/* A canonical composition: first + second -> composite. */
typedef struct tp_compose_pair {
  tp_rune first, second, composite;
} tp_compose_pair;

/* Whether r is in one of the sorted, disjoint ranges. */
TP_PRIVATE bool tp_uni_in_ranges(const tp_uni_range *table, size_t n, tp_rune r);

/* R0.6 letter test: general category L, as JS /\p{L}/u. */
TP_PRIVATE bool tp_is_letter(tp_rune r);
/* R6.10 uppercase test: general category Lu, as JS /\p{Lu}/u. */
TP_PRIVATE bool tp_is_upper_letter(tp_rune r);

/* JS toLowerCase() (R0.4), with the Final_Sigma rule. */
TP_PRIVATE tp_str tp_js_to_lower(tp_str s);

/* JS s.normalize("NFD") / normalize("NFC"). They return s itself, without allocating, when s is
 * already in that form. */
TP_PRIVATE tp_str tp_nfd(tp_str s);
TP_PRIVATE tp_str tp_nfc(tp_str s);

/* The canonical combining class rank of r (0 for starters). */
TP_PRIVATE uint8_t tp_combining_class(tp_rune r);

#endif /* TP_UNICODE_H */
