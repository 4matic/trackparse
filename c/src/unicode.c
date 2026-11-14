/* unicode.c: Unicode lookups over the generated tables in unicode_gen.c (go/unicode.go).
 *
 * General categories (R0.6), JS toLowerCase() (R0.4) and NFC/NFD (R1, R0.4). The tables come
 * from Node, the reference engine, so the results do not depend on the platform. Every function
 * is total: invalid UTF-8 is read as U+FFFD per bad byte, as Go's `for range` does.
 */
#include "unicode.h"

#include "unicode_gen.h"

#define TP_RUNE_ERROR ((tp_rune)0xfffd)

/* Go's utf8.DecodeRuneInString / DecodeLastRuneInString on p[0..n) (rt.c has Go's rules: an
 * ill-formed byte is U+FFFD of length 1). n > 0. */
static tp_rune dec(const char *p, size_t n, size_t *size) {
  tp_str s = {p, n};
  return tp_decode_rune(s, size);
}

static tp_rune dec_last(const char *p, size_t n, size_t *size) {
  tp_str s = {p, n};
  return tp_decode_last_rune(s, size);
}

/* ------------------------------------------------------------------------------------------ */
/* Categories                                                                                 */

TP_PRIVATE bool tp_uni_in_ranges(const tp_uni_range *table, size_t n, tp_rune r) {
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    if (r < table[m].lo) {
      hi = m;
    } else if (r > table[m].hi) {
      lo = m + 1;
    } else {
      return true;
    }
  }
  return false;
}

#define TP_IN(table, r) tp_uni_in_ranges(table, TP_COUNT(table), r)

TP_PRIVATE bool tp_is_letter(tp_rune r) {
  if (r < 0x80) return (r | 0x20) >= 'a' && (r | 0x20) <= 'z';
  return TP_IN(tp_uni_letter, r);
}

TP_PRIVATE bool tp_is_upper_letter(tp_rune r) {
  if (r < 0x80) return r >= 'A' && r <= 'Z';
  return TP_IN(tp_uni_upper, r);
}

/* ------------------------------------------------------------------------------------------ */
/* R0.4: JS String.prototype.toLowerCase().                                                   */

/* Appends the lowercase of one code point out of context. */
static void append_lower(tp_buf *out, tp_rune r) {
  if (r < 0x80) {
    if (r >= 'A' && r <= 'Z') r += 'a' - 'A';
    tp_buf_byte(out, (char)r);
    return;
  }
  /* The last run that starts at or before r. */
  size_t lo = 0, hi = TP_COUNT(tp_uni_lower);
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    if (tp_uni_lower[m].first <= r) {
      lo = m + 1;
    } else {
      hi = m;
    }
  }
  if (lo > 0) {
    const tp_lower_run *e = &tp_uni_lower[lo - 1];
    if (r <= e->last && (r - e->first) % (tp_rune)e->step == 0) {
      tp_buf_rune(out, r + e->delta);
      return;
    }
  }
  lo = 0;
  hi = TP_COUNT(tp_uni_lower_multi);
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    if (tp_uni_lower_multi[m].cp < r) {
      lo = m + 1;
    } else {
      hi = m;
    }
  }
  if (lo < TP_COUNT(tp_uni_lower_multi) && tp_uni_lower_multi[lo].cp == r) {
    tp_buf_str(out, tp_uni_lower_multi[lo].to);
    return;
  }
  tp_buf_rune(out, r);
}

/* The final-sigma rule's "a cased letter precedes", looking back through case-ignorable code
 * points. A code point that is both cased and case-ignorable (U+0345, modifier letters) is
 * skipped, as in ICU, which is what JS engines run. */
static bool cased_before(const char *p, size_t n) {
  while (n > 0) {
    size_t size;
    tp_rune r = dec_last(p, n, &size);
    if (!TP_IN(tp_uni_case_ignorable, r)) return TP_IN(tp_uni_cased, r);
    n -= size;
  }
  return false;
}

/* cased_before looking forward. */
static bool cased_after(const char *p, size_t n) {
  for (size_t i = 0; i < n;) {
    size_t size;
    tp_rune r = dec(p + i, n - i, &size);
    if (!TP_IN(tp_uni_case_ignorable, r)) return TP_IN(tp_uni_cased, r);
    i += size;
  }
  return false;
}

TP_PRIVATE tp_str tp_js_to_lower(tp_str s) {
  bool ascii = true, upper = false;
  for (size_t i = 0; i < s.n; i++) {
    unsigned char c = (unsigned char)s.p[i];
    if (c >= 0x80) {
      ascii = false;
      break;
    }
    if (c >= 'A' && c <= 'Z') upper = true;
  }
  if (ascii && !upper) return s;
  tp_buf out = {0};
  if (ascii) {
    for (size_t i = 0; i < s.n; i++) {
      char c = s.p[i];
      if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
      tp_buf_byte(&out, c);
    }
    return tp_buf_done(&out);
  }
  for (size_t i = 0; i < s.n;) {
    size_t size;
    tp_rune r = dec(s.p + i, s.n - i, &size);
    if (r != 0x3a3) {
      append_lower(&out, r);
    } else if (cased_before(s.p, i) && !cased_after(s.p + i + 2, s.n - i - 2)) {
      /* U+03A3 is U+03C2 after a cased letter when no cased letter follows (Final_Sigma). */
      tp_buf_rune(&out, 0x3c2);
    } else {
      tp_buf_rune(&out, 0x3c3);
    }
    i += size;
  }
  return tp_buf_done(&out);
}

/* ------------------------------------------------------------------------------------------ */
/* R1, R0.4: canonical normalization (Unicode Standard Annex #15), identical to JS            */
/* String.prototype.normalize("NFC") and normalize("NFD").                                    */

/* Hangul syllables, decomposed and composed algorithmically (Unicode 3.12). */
#define HANGUL_S_BASE 0xac00
#define HANGUL_L_BASE 0x1100
#define HANGUL_V_BASE 0x1161
#define HANGUL_T_BASE 0x11a7
#define HANGUL_L_COUNT 19
#define HANGUL_V_COUNT 21
#define HANGUL_T_COUNT 28
#define HANGUL_N_COUNT (HANGUL_V_COUNT * HANGUL_T_COUNT)
#define HANGUL_S_COUNT (HANGUL_L_COUNT * HANGUL_N_COUNT)

TP_PRIVATE uint8_t tp_combining_class(tp_rune r) {
  if (r < TP_UNI_QUICK_MIN) return 0;
  size_t lo = 0, hi = TP_COUNT(tp_uni_ccc);
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    if (r < tp_uni_ccc[m].lo) {
      hi = m;
    } else if (r > tp_uni_ccc[m].hi) {
      lo = m + 1;
    } else {
      return tp_uni_ccc[m].rank;
    }
  }
  return 0;
}

/* The full canonical decomposition of r from the table, or NULL when r has none (Hangul
 * syllables are handled by the caller). */
static const tp_decomp_entry *uni_decomposition(tp_rune r) {
  if (r < TP_UNI_QUICK_MIN) return NULL;
  size_t lo = 0, hi = TP_COUNT(tp_uni_decomp);
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    if (tp_uni_decomp[m].cp < r) {
      lo = m + 1;
    } else {
      hi = m;
    }
  }
  if (lo < TP_COUNT(tp_uni_decomp) && tp_uni_decomp[lo].cp == r) return &tp_uni_decomp[lo];
  return NULL;
}

static bool is_hangul_syllable(tp_rune r) {
  return r >= HANGUL_S_BASE && r < HANGUL_S_BASE + HANGUL_S_COUNT;
}

/* The primary composite of a + b, if there is one. */
static bool compose_two(tp_rune a, tp_rune b, tp_rune *out) {
  /* Hangul: L + V -> LV, LV + T -> LVT. */
  if (a >= HANGUL_L_BASE && a < HANGUL_L_BASE + HANGUL_L_COUNT && b >= HANGUL_V_BASE &&
      b < HANGUL_V_BASE + HANGUL_V_COUNT) {
    *out = HANGUL_S_BASE + ((a - HANGUL_L_BASE) * HANGUL_V_COUNT + (b - HANGUL_V_BASE)) *
                               HANGUL_T_COUNT;
    return true;
  }
  if (is_hangul_syllable(a) && (a - HANGUL_S_BASE) % HANGUL_T_COUNT == 0 && b > HANGUL_T_BASE &&
      b < HANGUL_T_BASE + HANGUL_T_COUNT) {
    *out = a + (b - HANGUL_T_BASE);
    return true;
  }
  size_t lo = 0, hi = TP_COUNT(tp_uni_compose);
  while (lo < hi) {
    size_t m = lo + (hi - lo) / 2;
    const tp_compose_pair *e = &tp_uni_compose[m];
    if (e->first < a || (e->first == a && e->second < b)) {
      lo = m + 1;
    } else {
      hi = m;
    }
  }
  if (lo < TP_COUNT(tp_uni_compose) && tp_uni_compose[lo].first == a &&
      tp_uni_compose[lo].second == b) {
    *out = tp_uni_compose[lo].composite;
    return true;
  }
  return false;
}

static bool uni_is_ascii(tp_str s) {
  for (size_t i = 0; i < s.n; i++) {
    if ((unsigned char)s.p[i] >= 0x80) return false;
  }
  return true;
}

/* Whether s is already NFD: valid UTF-8, nothing that decomposes and the non-starters in
 * canonical order. */
static bool nfd_quick(tp_str s) {
  uint8_t last = 0;
  for (size_t i = 0; i < s.n;) {
    tp_rune r = (unsigned char)s.p[i];
    size_t size = 1;
    if (r >= 0x80) {
      r = dec(s.p + i, s.n - i, &size);
      if (r == TP_RUNE_ERROR && size == 1) return false;
    }
    i += size;
    if (r < TP_UNI_QUICK_MIN) {
      last = 0;
      continue;
    }
    if (is_hangul_syllable(r) || uni_decomposition(r) != NULL) return false;
    uint8_t cc = tp_combining_class(r);
    if (cc != 0 && last > cc) return false;
    last = cc;
  }
  return true;
}

/* The NFC quick check of UAX #15 section 9, with Maybe treated as No: true only when s is
 * certainly NFC. */
static bool nfc_quick(tp_str s) {
  uint8_t last = 0;
  for (size_t i = 0; i < s.n;) {
    tp_rune r = (unsigned char)s.p[i];
    size_t size = 1;
    if (r >= 0x80) {
      r = dec(s.p + i, s.n - i, &size);
      if (r == TP_RUNE_ERROR && size == 1) return false;
    }
    i += size;
    if (r < TP_UNI_QUICK_MIN) {
      last = 0;
      continue;
    }
    if (TP_IN(tp_uni_nfc_no, r) || TP_IN(tp_uni_compose_second, r)) return false;
    uint8_t cc = tp_combining_class(r);
    if (cc != 0 && last > cc) return false;
    last = cc;
  }
  return true;
}

static int cmp_ccc(const void *a, const void *b, void *ctx) {
  (void)ctx;
  int ca = tp_combining_class(*(const tp_rune *)a);
  int cb = tp_combining_class(*(const tp_rune *)b);
  return ca - cb;
}

/* The canonical ordering algorithm: each run of non-starters is sorted by combining class,
 * stably. */
static void canonical_order(tp_rune *buf, size_t n) {
  for (size_t i = 0; i < n;) {
    if (tp_combining_class(buf[i]) == 0) {
      i++;
      continue;
    }
    size_t j = i + 1;
    while (j < n && tp_combining_class(buf[j]) != 0) j++;
    tp_rune *run = buf + i;
    size_t len = j - i;
    if (len <= 8) {
      /* Insertion sort: stable, and runs are almost always short. */
      for (size_t k = 1; k < len; k++) {
        tp_rune x = run[k];
        uint8_t cx = tp_combining_class(x);
        size_t m = k;
        while (m > 0 && tp_combining_class(run[m - 1]) > cx) {
          run[m] = run[m - 1];
          m--;
        }
        run[m] = x;
      }
    } else {
      tp_sort_stable(run, len, sizeof(tp_rune), cmp_ccc, NULL);
    }
    i = j;
  }
}

/* The canonical decomposition of s in canonical order (NFD as runes). */
static tp_runevec nfd_runes(tp_str s) {
  tp_runevec buf = {0};
  for (size_t i = 0; i < s.n;) {
    size_t size;
    tp_rune r = dec(s.p + i, s.n - i, &size);
    i += size;
    if (r < TP_UNI_QUICK_MIN) {
      tp_push(buf, r);
    } else if (is_hangul_syllable(r)) {
      tp_rune si = r - HANGUL_S_BASE;
      tp_push(buf, HANGUL_L_BASE + si / HANGUL_N_COUNT);
      tp_push(buf, HANGUL_V_BASE + (si % HANGUL_N_COUNT) / HANGUL_T_COUNT);
      tp_rune t = si % HANGUL_T_COUNT;
      if (t != 0) tp_push(buf, HANGUL_T_BASE + t);
    } else {
      const tp_decomp_entry *d = uni_decomposition(r);
      if (d != NULL) {
        for (size_t k = 0; k < d->len; k++) tp_push(buf, tp_uni_decomp_pool[d->off + k]);
      } else {
        tp_push(buf, r);
      }
    }
  }
  canonical_order(buf.p, buf.n);
  return buf;
}

/* The canonical composition algorithm on decomposed, canonically ordered runes, in place; returns
 * the length of the composed prefix. */
static size_t canonical_compose(tp_rune *buf, size_t n) {
  ptrdiff_t starter = -1; /* index in the output of the last starter, -1 before the first */
  uint8_t last = 0;
  size_t w = 0;
  for (size_t i = 0; i < n; i++) {
    tp_rune c = buf[i];
    if (c < TP_UNI_QUICK_MIN) {
      /* A starter that composes with nothing before it. */
      starter = (ptrdiff_t)w;
      last = 0;
      buf[w++] = c;
      continue;
    }
    uint8_t cc = tp_combining_class(c);
    if (starter >= 0) {
      /* c is blocked from the starter when something sits between them whose class is 0 or not
       * lower than c's. */
      if ((ptrdiff_t)w - 1 == starter || (last != 0 && last < cc)) {
        tp_rune p;
        if (compose_two(buf[starter], c, &p)) {
          buf[starter] = p;
          continue;
        }
      }
    }
    if (cc == 0) starter = (ptrdiff_t)w;
    last = cc;
    buf[w++] = c;
  }
  return w;
}

TP_PRIVATE tp_str tp_nfd(tp_str s) {
  if (uni_is_ascii(s) || nfd_quick(s)) return s;
  tp_runevec buf = nfd_runes(s);
  return tp_runes_str((tp_runes){buf.p, buf.n});
}

TP_PRIVATE tp_str tp_nfc(tp_str s) {
  if (uni_is_ascii(s) || nfc_quick(s)) return s;
  tp_runevec buf = nfd_runes(s);
  size_t n = canonical_compose(buf.p, buf.n);
  return tp_runes_str((tp_runes){buf.p, n});
}
