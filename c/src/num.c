/* num.c: exact, locale-independent numbers (rt.h "Numbers").
 *
 * - tp_parse_number: decimal string to the correctly rounded double, for any digit count, without
 *   strtod (locale, PORTING.md hazards 2 and 4).
 * - tp_buf_number: JS Number#toString (shortest round-trip digits, closest of those, ties to an
 *   even digit, as V8's bignum-dtoa), without printf (hazard 3).
 * - tp_half_up_units / tp_round_half_up: floor(v * scale + 0.5) as Go formatTime and JS
 *   times.ts compute it, with no fused multiply-add (hazard 1).
 *
 * Accepted grammar of tp_parse_number (the strings Go hands strconv.ParseFloat: go/tl_times.go
 * secondsOf gives "<digits>.<digits>", go/json.go number() gives any run of [0-9+-.eE]):
 *
 *   number   = [ "+" | "-" ] mantissa [ exponent ]
 *   mantissa = digit+ [ "." digit* ] | "." digit+
 *   exponent = ( "e" | "E" ) [ "+" | "-" ] digit+
 *
 * digit is ASCII 0-9, any count, leading zeros allowed ("007", "1.", ".5", "+1e+007"). Nothing
 * else: no whitespace, no empty string (JS Number("") is 0, ParseFloat fails), no "Infinity",
 * "NaN", hex or underscores (ParseFloat takes some of them, but no caller can pass them). On this
 * set JS Number() and Go ParseFloat agree: both are correctly rounded (ties to even); a value too
 * large gives +-Infinity (Go: with ErrRange, which go/json.go accepts) and a value too small gives
 * +-0. The result keeps the sign, so "-0" is -0.
 */
#include <float.h>

#include "rt.h"

/* FMA contraction would fuse v * scale + 0.5 (PORTING.md hazard 1). The pragma is standard C, but
 * GCC ignores it (and defaults to -ffp-contract=fast in GNU mode), so the build also passes
 * -ffp-contract=off (MSVC: /fp:precise), and tp_half_up_units rounds the product through a
 * volatile so even a build without the flag cannot fuse it. */
#if defined(__clang__) || (!defined(__GNUC__) && !defined(_MSC_VER))
#pragma STDC FP_CONTRACT OFF
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

/* x87 excess precision would make the fast path double-round (PORTING.md hazard 5). */
#if !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD != 0
#error "trackparse needs FLT_EVAL_METHOD == 0 (on 32-bit x86: -msse2 -mfpmath=sse)"
#endif

/* -------------------------------------------------------------------------------------------- */
/* Bits                                                                                         */

static uint64_t num_bits(double v) {
  uint64_t u;
  memcpy(&u, &v, sizeof u);
  return u;
}

static double num_of_bits(uint64_t u) {
  double v;
  memcpy(&v, &u, sizeof v);
  return v;
}

TP_PRIVATE bool tp_is_nan(double v) { return v != v; }

TP_PRIVATE bool tp_is_inf(double v) {
  return (num_bits(v) & ~(UINT64_C(1) << 63)) == UINT64_C(0x7FF0000000000000);
}

TP_PRIVATE double tp_nan(void) { return num_of_bits(UINT64_C(0x7FF8000000000000)); }

TP_PRIVATE double tp_inf(int sign) {
  return num_of_bits(sign < 0 ? UINT64_C(0xFFF0000000000000) : UINT64_C(0x7FF0000000000000));
}

static const double num_pow10[23] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                     1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                     1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

/* -------------------------------------------------------------------------------------------- */
/* Rounding                                                                                     */

TP_PRIVATE double tp_floor(double v) {
  double t;
  if (!(v < 4503599627370496.0 && v > -4503599627370496.0)) return v; /* NaN, inf, integers */
  if (v == 0) return v;                                               /* keeps -0 */
  t = (double)(int64_t)v;                                             /* toward zero */
  if (t > v) t -= 1.0;
  return t;
}

TP_PRIVATE double tp_half_up_units(double v, double scale) {
  /* Separate statements and a volatile: the product is rounded to a double before the add. */
  volatile double product = v * scale;
  double sum = product + 0.5;
  return tp_floor(sum);
}

TP_PRIVATE double tp_round_half_up(double v, int digits) {
  double scale;
  if (digits < 0) digits = 0;
  if (digits > 20) digits = 20;
  scale = num_pow10[digits];
  return tp_half_up_units(v, scale) / scale;
}

/* -------------------------------------------------------------------------------------------- */
/* Big unsigned integers (fixed capacity; the callers bound the sizes)                          */

/* Parsing needs about 3,800 bits (800 digits over 10^1125, shifted), formatting about 1,200. */
#define NUM_BIG_LIMBS 160

typedef struct num_big {
  uint32_t d[NUM_BIG_LIMBS]; /* little endian */
  size_t n;                  /* used limbs, no leading zero limb */
} num_big;

static void num_big_trim(num_big *a) {
  while (a->n > 0 && a->d[a->n - 1] == 0) a->n--;
}

static void num_big_set(num_big *a, uint64_t v) {
  a->n = 0;
  while (v != 0) {
    a->d[a->n++] = (uint32_t)v;
    v >>= 32;
  }
}

static void num_big_copy(num_big *dst, const num_big *src) {
  dst->n = src->n;
  if (src->n) memcpy(dst->d, src->d, src->n * sizeof(uint32_t));
}

/* a = a * m + add */
static void num_big_mul_add(num_big *a, uint32_t m, uint32_t add) {
  uint64_t carry = add;
  size_t i;
  for (i = 0; i < a->n; i++) {
    uint64_t t = (uint64_t)a->d[i] * m + carry;
    a->d[i] = (uint32_t)t;
    carry = t >> 32;
  }
  if (carry != 0 && a->n < NUM_BIG_LIMBS) a->d[a->n++] = (uint32_t)carry;
}

static void num_big_mul_pow10(num_big *a, int64_t e) {
  static const uint32_t small[10] = {1,      10,      100,      1000,      10000,
                                     100000, 1000000, 10000000, 100000000, 1000000000};
  while (e >= 9) {
    num_big_mul_add(a, 1000000000u, 0);
    e -= 9;
  }
  if (e > 0) num_big_mul_add(a, small[e], 0);
}

static void num_big_shl(num_big *a, uint64_t bits) {
  size_t limbs = (size_t)(bits / 32), i;
  unsigned s = (unsigned)(bits % 32);
  if (a->n == 0 || a->n >= NUM_BIG_LIMBS) return;
  /* Unreachable by the callers' bounds; keeps the writes inside d (and shows GCC the bound). */
  if (limbs > NUM_BIG_LIMBS - 1 - a->n) limbs = NUM_BIG_LIMBS - 1 - a->n;
  if (s == 0) {
    for (i = a->n; i > 0; i--) a->d[i - 1 + limbs] = a->d[i - 1];
    a->n += limbs;
  } else {
    a->d[a->n + limbs] = 0;
    for (i = a->n; i > 0; i--) {
      a->d[i + limbs] |= a->d[i - 1] >> (32 - s);
      a->d[i - 1 + limbs] = a->d[i - 1] << s;
    }
    a->n += limbs + 1;
  }
  for (i = 0; i < limbs; i++) a->d[i] = 0;
  num_big_trim(a);
}

static int num_big_cmp(const num_big *a, const num_big *b) {
  size_t i;
  if (a->n != b->n) return a->n < b->n ? -1 : 1;
  for (i = a->n; i > 0; i--) {
    if (a->d[i - 1] != b->d[i - 1]) return a->d[i - 1] < b->d[i - 1] ? -1 : 1;
  }
  return 0;
}

/* a -= b, a >= b */
static void num_big_sub(num_big *a, const num_big *b) {
  uint64_t borrow = 0;
  size_t i;
  for (i = 0; i < a->n; i++) {
    uint64_t bi = i < b->n ? b->d[i] : 0;
    uint64_t t = (uint64_t)a->d[i] - bi - borrow;
    a->d[i] = (uint32_t)t;
    borrow = (t >> 63) & 1;
  }
  num_big_trim(a);
}

/* out = a + b */
static void num_big_add(num_big *out, const num_big *a, const num_big *b) {
  const num_big *l = a->n >= b->n ? a : b, *s = a->n >= b->n ? b : a;
  uint64_t carry = 0;
  size_t i;
  for (i = 0; i < l->n; i++) {
    uint64_t t = (uint64_t)l->d[i] + (i < s->n ? s->d[i] : 0) + carry;
    out->d[i] = (uint32_t)t;
    carry = t >> 32;
  }
  out->n = l->n;
  if (carry && out->n < NUM_BIG_LIMBS) out->d[out->n++] = (uint32_t)carry;
}

static int64_t num_big_bitlen(const num_big *a) {
  uint32_t top;
  int64_t bits;
  if (a->n == 0) return 0;
  top = a->d[a->n - 1];
  bits = (int64_t)(a->n - 1) * 32;
  while (top != 0) {
    bits++;
    top >>= 1;
  }
  return bits;
}

/* -------------------------------------------------------------------------------------------- */
/* Parsing                                                                                      */

/* Significant digits kept; any digit past them only breaks ties, so a nonzero tail becomes one
 * extra "1" digit. Exact halfway points between doubles have at most 767 significant digits. */
#define NUM_MAX_DIGITS 800

static bool num_is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

/* The exact value digits * 10^e (digits: nd ASCII digits, the first nonzero) as a double. */
static double num_decimal_to_double(const char *digits, size_t nd, int64_t e) {
  num_big num, den;
  int64_t e2, t, sh, i;
  uint64_t q = 0, raw;
  size_t at;
  int c;
  /* Magnitude bounds: the value lies in [10^(nd+e-1), 10^(nd+e)). */
  if ((int64_t)nd + e > 310) return tp_inf(1);
  if ((int64_t)nd + e < -330) return 0.0;
  if (nd <= 15 && e >= -22 && e <= 22) {
    /* Fast path: the digits and the power of ten are exact doubles, so one IEEE operation gives
     * the correctly rounded result. */
    uint64_t m = 0;
    double d;
    for (at = 0; at < nd; at++) m = m * 10 + (uint64_t)(digits[at] - '0');
    d = (double)m;
    return e >= 0 ? d * num_pow10[e] : d / num_pow10[-e];
  }
  num_big_set(&num, 0);
  for (at = 0; at < nd;) {
    uint32_t chunk = 0, mul = 1;
    size_t k;
    for (k = 0; k < 9 && at < nd; k++, at++) {
      chunk = chunk * 10 + (uint32_t)(digits[at] - '0');
      mul *= 10;
    }
    num_big_mul_add(&num, mul, chunk);
  }
  num_big_set(&den, 1);
  if (e >= 0) {
    num_big_mul_pow10(&num, e);
  } else {
    num_big_mul_pow10(&den, -e);
  }
  /* Scale so that 1 <= num / den < 2; the value is that ratio times 2^e2. */
  e2 = num_big_bitlen(&num) - num_big_bitlen(&den);
  if (e2 >= 0) {
    num_big_shl(&den, (uint64_t)e2);
  } else {
    num_big_shl(&num, (uint64_t)-e2);
  }
  if (num_big_cmp(&num, &den) < 0) {
    num_big_shl(&num, 1);
    e2--;
  }
  if (e2 > 1023) return tp_inf(1);
  /* The result is q * 2^(t - 52) with t the binary exponent (or -1022 for subnormals). */
  t = e2 > -1022 ? e2 : -1022;
  sh = e2 - t + 52;
  if (sh < 0) {
    if (sh < -1) return 0.0;
    q = num_big_cmp(&num, &den) == 0 ? 0 : 1; /* r / 2 with r in [1, 2): a tie only at r == 1 */
  } else {
    num_big_sub(&num, &den);
    q = 1;
    for (i = 0; i < sh; i++) {
      num_big_shl(&num, 1);
      q <<= 1;
      if (num_big_cmp(&num, &den) >= 0) {
        num_big_sub(&num, &den);
        q |= 1;
      }
    }
    num_big_shl(&num, 1);
    c = num_big_cmp(&num, &den);
    if (c > 0 || (c == 0 && (q & 1))) q++;
  }
  /* For t == -1022 the bits are q itself (subnormal, or the smallest normals); a carry into
   * 2^53 moves to the next exponent, and past the largest one to infinity. */
  raw = ((uint64_t)(t + 1022) << 52) + q;
  if (raw >= UINT64_C(0x7FF0000000000000)) return tp_inf(1);
  return num_of_bits(raw);
}

TP_PRIVATE bool tp_parse_number(tp_str s, double *out) {
  const unsigned char *p = (const unsigned char *)s.p;
  size_t i = 0, n = s.n, nd = 0, mant_digits = 0;
  int64_t point = 0; /* significant-stream digits before the decimal point */
  int64_t exp10 = 0, e;
  bool neg = false, seen_sig = false, sticky = false;
  char digits[NUM_MAX_DIGITS + 1];
  double v;
  if (i < n && (p[i] == '+' || p[i] == '-')) {
    neg = p[i] == '-';
    i++;
  }
  for (; i < n && num_is_digit(p[i]); i++) {
    mant_digits++;
    if (p[i] != '0') seen_sig = true;
    if (!seen_sig) continue;
    point++;
    if (nd < NUM_MAX_DIGITS) {
      digits[nd++] = (char)p[i];
    } else if (p[i] != '0') {
      sticky = true;
    }
  }
  if (i < n && p[i] == '.') {
    i++;
    for (; i < n && num_is_digit(p[i]); i++) {
      mant_digits++;
      if (p[i] != '0') seen_sig = true;
      if (!seen_sig) {
        point--;
        continue;
      }
      if (nd < NUM_MAX_DIGITS) {
        digits[nd++] = (char)p[i];
      } else if (p[i] != '0') {
        sticky = true;
      }
    }
  }
  if (mant_digits == 0) return false;
  if (i < n && (p[i] == 'e' || p[i] == 'E')) {
    bool eneg = false;
    size_t start;
    i++;
    if (i < n && (p[i] == '+' || p[i] == '-')) {
      eneg = p[i] == '-';
      i++;
    }
    start = i;
    for (; i < n && num_is_digit(p[i]); i++) {
      if (exp10 < 100000000) exp10 = exp10 * 10 + (p[i] - '0'); /* saturates far past the range */
    }
    if (i == start) return false;
    if (eneg) exp10 = -exp10;
  }
  if (i != n) return false;
  if (nd == 0) {
    *out = neg ? -0.0 : 0.0;
    return true;
  }
  /* value = digits * 10^e, with the dropped digits (if any) as one trailing sticky "1". */
  e = exp10 + point - (int64_t)nd;
  if (sticky) {
    digits[nd++] = '1';
    e--;
  } else {
    while (nd > 1 && digits[nd - 1] == '0') {
      nd--;
      e++;
    }
  }
  v = num_decimal_to_double(digits, nd, e);
  *out = neg ? -v : v;
  return true;
}

/* -------------------------------------------------------------------------------------------- */
/* Formatting                                                                                   */

static int64_t num_floor_div(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}

/* Shortest digits of v (finite, > 0) by Steele & White / Burger & Dybvig free-format generation
 * on exact num_big integers, with V8's final digit choice (nearer, ties to an even digit). Returns
 * the digit count; the value is 0.digits * 10^point. */
static size_t num_shortest_digits(double v, char out[32], int *point) {
  uint64_t bits = num_bits(v), f = bits & ((UINT64_C(1) << 52) - 1);
  int be = (int)((bits >> 52) & 0x7FF), e, k;
  bool lower_closer, even;
  num_big r, s, mp, mm, tmp;
  size_t nd = 0;
  int64_t x, bl;
  if (be == 0) {
    e = -1074;
  } else {
    f |= UINT64_C(1) << 52;
    e = be - 1075;
  }
  lower_closer = f == (UINT64_C(1) << 52) && be > 1;
  even = (f & 1) == 0; /* round-half-even reading: the bounds are inclusive */
  num_big_set(&r, f);
  if (e >= 0) {
    num_big_set(&mp, 1);
    num_big_shl(&mp, (uint64_t)e);
    num_big_copy(&mm, &mp);
    if (!lower_closer) {
      num_big_shl(&r, (uint64_t)e + 1);
      num_big_set(&s, 2);
    } else {
      num_big_shl(&r, (uint64_t)e + 2);
      num_big_set(&s, 4);
      num_big_shl(&mp, 1);
    }
  } else {
    num_big_set(&mm, 1);
    if (!lower_closer) {
      num_big_shl(&r, 1);
      num_big_set(&s, 1);
      num_big_shl(&s, (uint64_t)(1 - e));
      num_big_set(&mp, 1);
    } else {
      num_big_shl(&r, 2);
      num_big_set(&s, 1);
      num_big_shl(&s, (uint64_t)(2 - e));
      num_big_set(&mp, 2);
    }
  }
  /* Estimate k = ceil(log10 v) from floor(log2 v); the loops below correct it. */
  bl = 0;
  {
    uint64_t t = f;
    while (t) {
      bl++;
      t >>= 1;
    }
  }
  x = e + bl - 1;
  k = (int)(num_floor_div(x * 78913, 262144) + 1);
  if (k >= 0) {
    num_big_mul_pow10(&s, k);
  } else {
    num_big_mul_pow10(&r, -k);
    num_big_mul_pow10(&mp, -k);
    num_big_mul_pow10(&mm, -k);
  }
  for (;;) { /* too small: high reaches 1 */
    int c;
    num_big_add(&tmp, &r, &mp);
    c = num_big_cmp(&tmp, &s);
    if (even ? c >= 0 : c > 0) {
      num_big_mul_add(&s, 10, 0);
      k++;
      continue;
    }
    break;
  }
  for (;;) { /* too large: 10 * high still below 1 */
    int c;
    num_big_add(&tmp, &r, &mp);
    num_big_mul_add(&tmp, 10, 0);
    c = num_big_cmp(&tmp, &s);
    if (even ? c < 0 : c <= 0) {
      num_big_mul_add(&r, 10, 0);
      num_big_mul_add(&mp, 10, 0);
      num_big_mul_add(&mm, 10, 0);
      k--;
      continue;
    }
    break;
  }
  *point = k;
  while (nd < 30) {
    int d = 0, c1, c2;
    bool low, high;
    num_big_mul_add(&r, 10, 0);
    num_big_mul_add(&mp, 10, 0);
    num_big_mul_add(&mm, 10, 0);
    while (num_big_cmp(&r, &s) >= 0) {
      num_big_sub(&r, &s);
      d++;
    }
    c1 = num_big_cmp(&r, &mm);
    num_big_add(&tmp, &r, &mp);
    c2 = num_big_cmp(&tmp, &s);
    low = even ? c1 <= 0 : c1 < 0;
    high = even ? c2 >= 0 : c2 > 0;
    if (!low && !high) {
      out[nd++] = (char)('0' + d);
      continue;
    }
    if (low && high) {
      int c;
      num_big_add(&tmp, &r, &r);
      c = num_big_cmp(&tmp, &s);
      if (c > 0 || (c == 0 && (d & 1))) d++;
    } else if (high) {
      d++;
    }
    out[nd++] = (char)('0' + d);
    break;
  }
  return nd;
}

TP_PRIVATE void tp_buf_number(tp_buf *b, double v) {
  char digits[32];
  size_t k, i;
  int point;
  if (tp_is_nan(v)) {
    tp_buf_cstr(b, "NaN");
    return;
  }
  if (tp_is_inf(v)) {
    tp_buf_cstr(b, v < 0 ? "-Infinity" : "Infinity");
    return;
  }
  if (v == 0) {
    tp_buf_byte(b, '0');
    return;
  }
  if (v < 0) {
    tp_buf_byte(b, '-');
    v = -v;
  }
  k = num_shortest_digits(v, digits, &point);
  /* ECMA-262 Number::toString: the value is 0.digits * 10^point. */
  if ((int)k <= point && point <= 21) {
    tp_buf_bytes(b, digits, k);
    for (i = 0; i < (size_t)point - k; i++) tp_buf_byte(b, '0');
  } else if (0 < point && point <= 21) {
    tp_buf_bytes(b, digits, (size_t)point);
    tp_buf_byte(b, '.');
    tp_buf_bytes(b, digits + point, k - (size_t)point);
  } else if (-6 < point && point <= 0) {
    tp_buf_bytes(b, "0.", 2);
    for (i = 0; i < (size_t)-point; i++) tp_buf_byte(b, '0');
    tp_buf_bytes(b, digits, k);
  } else {
    int e = point - 1;
    tp_buf_byte(b, digits[0]);
    if (k > 1) {
      tp_buf_byte(b, '.');
      tp_buf_bytes(b, digits + 1, k - 1);
    }
    tp_buf_byte(b, 'e');
    tp_buf_byte(b, e >= 0 ? '+' : '-');
    tp_buf_int(b, e < 0 ? -e : e);
  }
}

TP_PRIVATE tp_str tp_number_str(double v) {
  tp_buf b = {0, 0, 0};
  tp_buf_number(&b, v);
  return tp_buf_done(&b);
}
