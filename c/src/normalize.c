/* normalize.c: R1 normalization (go/normalize.go, js/src/normalize.ts). */
#include "internal.h"

/* isRemoved is R1.2: the invisible characters removed outright. ZWNJ and ZWJ (U+200C, U+200D)
 * are kept (ZWJ emoji). U+E000 is reserved as the R3.4 group placeholder, so it can never occur
 * in input. */
TP_PRIVATE bool tp_is_removed(tp_rune r) {
  switch (r) {
  case 0x200b:
  case 0x2060:
  case 0xfeff:
  case 0x00ad:
  case TP_PH:
    return true;
  default:
    return false;
  }
}

/* mapRune is R1.3 brackets, R1.4 quotes and R1.5 dashes to ASCII. false for a removed
 * character. */
TP_PRIVATE bool tp_map_rune(tp_rune r, tp_rune *out) {
  if (tp_is_removed(r)) return false;
  if (r >= 0x2010 && r <= 0x2015) { /* R1.5 */
    *out = '-';
    return true;
  }
  switch (r) {
  case 0xff08:
    *out = '(';
    return true;
  case 0xff09:
    *out = ')';
    return true;
  case 0xff3b:
  case 0x3010:
  case 0x3014:
    *out = '[';
    return true;
  case 0xff3d:
  case 0x3011:
  case 0x3015:
    *out = ']';
    return true;
  case 0xff5b:
    *out = '{';
    return true;
  case 0xff5d:
    *out = '}';
    return true;
  case 0x201c:
  case 0x201d:
  case 0x201e:
  case 0x00ab:
  case 0x00bb:
  case 0x300c:
  case 0x300d:
  case 0x300e:
  case 0x300f:
  case 0xff02:
    *out = '"';
    return true;
  case 0x2018:
  case 0x2019:
  case 0x201a:
  case 0x0060:
  case 0x00b4:
    *out = '\'';
    return true;
  case 0x2212:
  case 0xfe58:
  case 0xfe63:
  case 0xff0d:
    *out = '-';
    return true;
  default:
    *out = r;
    return true;
  }
}

/* collapseSpacedDashRuns is the second half of R1.5: a run of two or more "-" with whitespace on
 * both sides becomes one "-". */
TP_PRIVATE tp_runes tp_collapse_spaced_dash_runs(tp_runes t) {
  tp_runevec out = {0};
  ptrdiff_t n = (ptrdiff_t)t.n;
  ptrdiff_t i = 0;
  while (i < n) {
    tp_rune r = t.p[i];
    if (r != '-') {
      tp_push(out, r);
      i++;
      continue;
    }
    ptrdiff_t j = i;
    while (tp_at(t, j) == '-') j++;
    bool spaced = j - i >= 2 && tp_is_whitespace(tp_before(t, i)) && tp_is_whitespace(tp_at(t, j));
    if (spaced) {
      tp_push(out, (tp_rune)'-');
    } else {
      for (ptrdiff_t k = i; k < j; k++) tp_push(out, (tp_rune)'-');
    }
    i = j;
  }
  return tp_runevec_view(out);
}

/* normalize is R1 on a track string. It is idempotent. */
TP_PRIVATE tp_str tp_normalize(tp_str input) {
  tp_runes src = tp_runes_of(tp_nfc(input));
  tp_runevec mapped = {0};
  for (size_t i = 0; i < src.n; i++) {
    tp_rune m;
    if (tp_map_rune(src.p[i], &m)) tp_push(mapped, m);
  }
  /* R1.6: every whitespace code point becomes one space, runs collapsed, trimmed. R1.8: removing
   * invisibles can leave a decomposed sequence behind; compose again so that normalize stays
   * idempotent. */
  return tp_nfc(tp_collapse_runes(tp_collapse_spaced_dash_runs(tp_runevec_view(mapped))));
}
