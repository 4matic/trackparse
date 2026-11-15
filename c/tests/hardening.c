/* hardening.c: the generator, the invariant checks, the fixture loader and the timers shared by
 * the hardening tests and the fuzz targets (hardening.h). A translation of the helpers of
 * go/properties_test.go, go/fuzz_test.go and go/bench_test.go. */
#include "hardening.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../include/trackparse.h"
#include "../wire/json_read.h"
#include "../wire/wire.h"

/* A tp_str initializer (TP_S is a compound literal, not a constant expression). */
#define HZ_S(lit) {(lit), sizeof(lit) - 1}
#define S TP_S

/* -------------------------------------------------------------------------------------------- */
/* Scopes, failures, timing                                                                     */

void hz_scoped(hz_fn fn, void *arg) {
  tp_scope s;
  if (!tp_scope_open(&s)) hz_fail("out of memory (opening a scope)");
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    hz_fail("out of memory");
  }
  tp_scope_arm(&s);
  fn(arg);
  tp_scope_close(&s);
  tp_arena_free(s.arena);
}

void hz_fail(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fputs("FAIL: ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  fflush(stderr);
  abort();
}

const char *hz_q(tp_str s) {
  tp_buf b = {0};
  tp_append_js_string(&b, s);
  return tp_buf_done(&b).p;
}

double hz_now(void) {
  struct timespec ts;
  /* Zeroed first: MemorySanitizer does not intercept timespec_get, so it would take the fields
   * libc writes for uninitialized. */
  memset(&ts, 0, sizeof ts);
  if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return 0;
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

double hz_best_of3(hz_fn fn, void *arg) {
  double best = 1e300;
  for (int i = 0; i < 3; i++) {
    double start = hz_now(), d;
    hz_scoped(fn, arg);
    d = hz_now() - start;
    if (d < best) best = d;
  }
  return best;
}

double hz_slowdown(void) {
  const char *v = getenv("TRACKPARSE_SLOWDOWN");
  if (v && v[0]) {
    double d = 0;
    /* Digits only (no strtod: the ban covers tests too, so a grep stays simple). */
    for (const char *p = v; *p >= '0' && *p <= '9'; p++) d = d * 10 + (*p - '0');
    if (d >= 1) return d;
  }
#ifdef TRACKPARSE_SANITIZED
  return 10;
#else
  return 1;
#endif
}

/* -------------------------------------------------------------------------------------------- */
/* Small string helpers                                                                         */

static tp_str cat(tp_str a, tp_str b) { return tp_concat(a, b); }
static tp_str cat3(tp_str a, tp_str b, tp_str c) { return tp_concat3(a, b, c); }

static tp_str pick(hz_rng *r, const tp_str *items, size_t n) { return items[hz_below(r, (int)n)]; }
#define PICK(r, arr) pick((r), (arr), TP_COUNT(arr))

static bool strvec_eq(tp_strvec a, tp_strvec b) { return tp_strvec_eq(a, b); }

static const char *strvec_show(tp_strvec v) {
  tp_buf b = {0};
  tp_buf_byte(&b, '[');
  for (size_t i = 0; i < v.n; i++) {
    if (i) tp_buf_byte(&b, ',');
    tp_append_js_string(&b, v.p[i]);
  }
  tp_buf_byte(&b, ']');
  return tp_buf_done(&b).p;
}

/* -------------------------------------------------------------------------------------------- */
/* Generator                                                                                    */

hz_rng hz_rng_new(uint64_t seed) {
  hz_rng r;
  r.s = (uint32_t)seed ^ (uint32_t)(seed >> 32);
  return r;
}

uint32_t hz_next(hz_rng *r) {
  uint32_t t;
  r->s += 0x6d2b79f5u;
  t = r->s;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return t ^ (t >> 14);
}

int hz_below(hz_rng *r, int n) {
  if (n <= 0) return 0;
  return (int)(hz_next(r) % (uint32_t)n);
}

bool hz_chance(hz_rng *r, int percent) { return hz_below(r, 100) < percent; }

uint64_t hz_fnv1a(const void *p, size_t n) {
  const unsigned char *b = (const unsigned char *)p;
  uint64_t h = 14695981039346656037u;
  for (size_t i = 0; i < n; i++) {
    h ^= b[i];
    h *= 1099511628211u;
  }
  return h;
}

/* The tricky set of the JS tests (its lone surrogate is U+FFFD, plus invalid UTF-8 bytes). */
const tp_str hz_tricky_fragments[] = {
    HZ_S(" "),
    HZ_S("  "),
    HZ_S("-"),
    HZ_S(" - "),
    HZ_S(" \u2013 "),
    HZ_S("\u2014"),
    HZ_S("("),
    HZ_S(")"),
    HZ_S("["),
    HZ_S("]"),
    HZ_S("{"),
    HZ_S("}"),
    HZ_S("\""),
    HZ_S("'"),
    HZ_S("\u201c"),
    HZ_S("|"),
    HZ_S(" | "),
    HZ_S(","),
    HZ_S(";"),
    HZ_S("&"),
    HZ_S(" & "),
    HZ_S(" x "),
    HZ_S(" feat. "),
    HZ_S(" ft "),
    HZ_S("prod. by "),
    HZ_S(" by "),
    HZ_S("."),
    HZ_S(":"),
    HZ_S("_"),
    HZ_S("0"),
    HZ_S("1"),
    HZ_S("01. "),
    HZ_S("3:45 "),
    HZ_S("Remix"),
    HZ_S(" Mix"),
    HZ_S("VIP"),
    HZ_S("Official Video"),
    HZ_S("ID"),
    HZ_S("\u200b"),
    HZ_S("\u00a0"),
    HZ_S("\u3000"),
    HZ_S("\u00e9"),
    HZ_S("e\u0301"),
    HZ_S("\U0001d11e"),
    HZ_S("\ufffd"),
    HZ_S("\u00d7"),
    HZ_S("\uff0f"),
    HZ_S(".mp3"),
    HZ_S("\xff"),
    HZ_S("\xed\xa0\x80"),
    HZ_S("\xe2\x82"),
};
const size_t hz_tricky_fragments_n = TP_COUNT(hz_tricky_fragments);

/* One random code point, biased to the ranges the rules look at. */
static tp_rune any_rune(hz_rng *r) {
  int cp;
  switch (hz_below(r, 10)) {
  case 0:
  case 1:
  case 2:
  case 3:
    cp = hz_below(r, 0x80);
    break;
  case 4:
    cp = hz_below(r, 0x100);
    break;
  case 5:
    cp = 0x300 + hz_below(r, 0x70); /* combining marks */
    break;
  case 6:
    cp = 0x2000 + hz_below(r, 0x70); /* spaces, dashes, quotes, invisibles */
    break;
  case 7:
    cp = hz_below(r, 0x10000);
    break;
  case 8:
    cp = 0xff00 + hz_below(r, 0xf0); /* fullwidth forms */
    break;
  default:
    cp = hz_below(r, 0x110000);
  }
  if (cp >= 0xd800 && cp <= 0xdfff) return 0xfffd;
  return (tp_rune)cp;
}

static void random_chars(hz_rng *r, tp_buf *b, int max) {
  for (int n = hz_below(r, max + 1); n > 0; n--) tp_buf_rune(b, any_rune(r));
}

static tp_str noisy_string(hz_rng *r) {
  tp_buf b = {0};
  for (int n = hz_below(r, 25); n > 0; n--) {
    if (hz_chance(r, 50)) {
      tp_buf_str(&b, pick(r, hz_tricky_fragments, hz_tricky_fragments_n));
    } else {
      random_chars(r, &b, 4);
    }
  }
  return tp_buf_done(&b);
}

tp_str hz_any_string(hz_rng *r) {
  if (hz_chance(r, 50)) {
    tp_buf b = {0};
    random_chars(r, &b, 60);
    return tp_buf_done(&b);
  }
  return noisy_string(r);
}

static const tp_str prop_artists[] = {
    HZ_S("Noisia"), HZ_S("Phace"),          HZ_S("Fox Stevenson"), HZ_S("Sub Focus"),
    HZ_S("Mefjus"), HZ_S("Camo & Krooked"), HZ_S("Alix Perez"),
};
static const tp_str prop_titles[] = {HZ_S("Tentacles"),  HZ_S("Bruises"), HZ_S("Out the Blue"),
                                     HZ_S("Dead Limit"), HZ_S("Stigma"),  HZ_S("Blind Faith")};
static const tp_str prop_remixers[] = {HZ_S("Skrillex"), HZ_S("Magnetude"), HZ_S("Wilkinson"),
                                       HZ_S("Break")};
static const tp_str prop_joiners[] = {HZ_S(" & "), HZ_S(", "), HZ_S(" x "), HZ_S(" vs. ")};

tp_str hz_track_string(hz_rng *r) {
  tp_strvec artists = {0};
  tp_str s;
  for (int n = 1 + hz_below(r, 3); n > 0; n--) {
    tp_str a = PICK(r, prop_artists);
    if (!tp_strvec_has(artists, a)) tp_push(artists, a);
  }
  s = tp_join(artists, PICK(r, prop_joiners));
  if (hz_chance(r, 50)) {
    tp_str feat = PICK(r, prop_artists);
    if (!tp_strvec_has(artists, feat)) s = cat3(s, S(" feat. "), feat);
  }
  s = cat3(s, S(" - "), PICK(r, prop_titles));
  if (hz_chance(r, 50)) s = cat(s, cat3(S(" ("), PICK(r, prop_remixers), S(" Remix)")));
  return s;
}

tp_str hz_quiet_track(hz_rng *r) {
  for (;;) {
    tp_str s = hz_track_string(r);
    if (tp_api_parse(s).warnings.n == 0) return s;
  }
}

/* The fragments of the JS and Rust tracklist property tests. U+0085 is written as bytes: C does
 * not allow a universal character name below U+00A0. */
static const tp_str tracklist_fragments[] = {
    HZ_S("\n"),          HZ_S("\r\n"),        HZ_S("\r"),
    HZ_S("\n\r"),        HZ_S(" "),           HZ_S("\u2028"),
    HZ_S("\xc2\x85"),    HZ_S("\u00a0"),      HZ_S("  "),
    HZ_S("\t"),          HZ_S("\u3000"),      HZ_S("00:00"),
    HZ_S("0:00 "),       HZ_S("04:37 "),      HZ_S("1:02:03 "),
    HZ_S("01:02:03.5 "), HZ_S("125:30 "),     HZ_S("12:34,250"),
    HZ_S("1:75 "),       HZ_S("0:61:00 "),    HZ_S("1:2"),
    HZ_S("["),           HZ_S("]"),           HZ_S("("),
    HZ_S(")"),           HZ_S("[00:10] "),    HZ_S("(1:00)"),
    HZ_S(" - "),         HZ_S(" \u2013 "),    HZ_S("-"),
    HZ_S("|"),           HZ_S("."),           HZ_S(":"),
    HZ_S("=> "),         HZ_S("==>"),         HZ_S("1 "),
    HZ_S("2 "),          HZ_S("01. "),        HZ_S("3) "),
    HZ_S("Artist"),      HZ_S("Title"),       HZ_S("Noisia - Stigma"),
    HZ_S("Intro"),       HZ_S("interview"),   HZ_S("Tracklist:"),
    HZ_S("Part 2"),      HZ_S("https://x.y"), HZ_S("www."),
    HZ_S("_-_"),         HZ_S("Ab-Cd"),       HZ_S("ID"),
    HZ_S("(VIP)"),       HZ_S("**"),          HZ_S("*"),
    HZ_S("__"),          HZ_S("_"),           HZ_S("***"),
    HZ_S("* "),          HZ_S("- "),          HZ_S("\u2022 "),
    HZ_S("**["),         HZ_S("]**"),         HZ_S("w/ "),
    HZ_S("W/"),          HZ_S("\u200b"),      HZ_S("\U0001d11e"),
    HZ_S("\U0001f3b5"),  HZ_S("\u00e9"),      HZ_S("\u041a\u0438\u043d\u043e"),
    HZ_S("a"),           HZ_S("7"),           HZ_S("\x00"),
    HZ_S("\""),          HZ_S("\\"),          HZ_S("\xff"),
};

tp_str hz_noisy_text(hz_rng *r) {
  tp_buf b = {0};
  for (int n = hz_below(r, 41); n > 0; n--) tp_buf_str(&b, PICK(r, tracklist_fragments));
  return tp_buf_done(&b);
}

const tp_str hz_time_styles[] = {HZ_S("M:SS"),     HZ_S("MM:SS"),   HZ_S("H:MM:SS"),
                                 HZ_S("HH:MM:SS"), HZ_S("MM:SS.d"), HZ_S("M:SS,ddd")};
const size_t hz_time_styles_n = TP_COUNT(hz_time_styles);

/* n in decimal, at least width digits (fmt %0*d for n >= 0). */
static void put_int(tp_buf *b, int n, int width) {
  tp_str d = tp_itoa(n);
  for (int i = (int)d.n; i < width; i++) tp_buf_byte(b, '0');
  tp_buf_str(b, d);
}

tp_str hz_write_time(tp_str style, int seconds, int frac) {
  int h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
  tp_buf b = {0};
  if (tp_str_eq(style, S("M:SS"))) {
    put_int(&b, seconds / 60, 1);
    tp_buf_byte(&b, ':');
    put_int(&b, s, 2);
  } else if (tp_str_eq(style, S("MM:SS"))) {
    put_int(&b, seconds / 60, 2);
    tp_buf_byte(&b, ':');
    put_int(&b, s, 2);
  } else if (tp_str_eq(style, S("H:MM:SS")) || tp_str_eq(style, S("HH:MM:SS"))) {
    put_int(&b, h, style.n == 7 ? 1 : 2);
    tp_buf_byte(&b, ':');
    put_int(&b, m, 2);
    tp_buf_byte(&b, ':');
    put_int(&b, s, 2);
  } else if (tp_str_eq(style, S("MM:SS.d"))) {
    put_int(&b, seconds / 60, 2);
    tp_buf_byte(&b, ':');
    put_int(&b, s, 2);
    tp_buf_byte(&b, '.');
    put_int(&b, frac % 10, 1);
  } else {
    put_int(&b, seconds / 60, 1);
    tp_buf_byte(&b, ':');
    put_int(&b, s, 2);
    tp_buf_byte(&b, ',');
    put_int(&b, frac, 3);
  }
  return tp_buf_done(&b);
}

static const tp_str list_rests[] = {
    HZ_S("Noisia - Stigma"),
    HZ_S("01. Phace - Plastic Acid"),
    HZ_S("2 Unlimited - No Limit"),
    HZ_S("Intro"),
    HZ_S("Paperclip interview"),
    HZ_S("Mefjus - Suicide Bassline (VIP)"),
    HZ_S(""),
    HZ_S("ID - ID"),
};

/* A random time in a random style (the argument order of Go's writeTime(pick, below, below)). */
static tp_str random_time(hz_rng *r) {
  tp_str style = pick(r, hz_time_styles, hz_time_styles_n);
  int seconds = hz_below(r, 3600);
  int frac = hz_below(r, 1000);
  return hz_write_time(style, seconds, frac);
}

tp_str hz_list_line(hz_rng *r) {
  static const tp_str plain[] = {HZ_S(""), HZ_S("Tracklist:"), HZ_S("a note"),
                                 HZ_S("https://example.com")};
  static const tp_str gaps[] = {HZ_S(" "), HZ_S("  "), HZ_S(" - "), HZ_S(" | "), HZ_S(" => ")};
  static const tp_str leads[] = {HZ_S(""), HZ_S(" "), HZ_S("[")};
  tp_str t, gap, rest, lead;
  if (hz_below(r, 7) == 0) return PICK(r, plain);
  t = random_time(r);
  gap = PICK(r, gaps);
  rest = PICK(r, list_rests);
  lead = PICK(r, leads);
  if (!tp_str_eq(lead, S("["))) return cat(cat3(lead, t, gap), rest);
  return cat(cat3(S("["), t, S("]")), cat(gap, rest));
}

tp_str hz_timed_list(hz_rng *r) {
  static const tp_str eols[] = {HZ_S("\n"), HZ_S("\r\n")};
  int n = 1 + hz_below(r, 12);
  tp_str eol = PICK(r, eols), text;
  tp_strvec lines = {0};
  for (int i = 0; i < n; i++) tp_push(lines, hz_list_line(r));
  text = tp_join(lines, eol);
  if (hz_chance(r, 50)) text = cat(text, eol);
  return text;
}

tp_str hz_any_text(hz_rng *r) {
  if (hz_chance(r, 30)) return hz_timed_list(r);
  return hz_noisy_text(r);
}

tp_json hz_loose_edits(hz_rng *r) {
  static const tp_str fields[] = {
      HZ_S("-3"),       HZ_S("0"),      HZ_S("1"),     HZ_S("2"),     HZ_S("5"),
      HZ_S("15"),       HZ_S("1.5"),    HZ_S("-0.25"), HZ_S("1e300"), HZ_S("-1e300"),
      HZ_S("1e999"),    HZ_S("-1e999"), HZ_S("null"),  HZ_S("true"),  HZ_S("false"),
      HZ_S("\"x\""),    HZ_S("\"\""),   HZ_S("[1,2]"), HZ_S("{}"),    HZ_S("9007199254740993"),
      HZ_S("359999.5"),
  };
  static const tp_str ops[] = {HZ_S("set-time"), HZ_S("shift"),  HZ_S("sort-by-time"),
                               HZ_S("set-rest"), HZ_S("insert"), HZ_S("delete"),
                               HZ_S("x")};
  static const tp_str texts[] = {HZ_S("\"a\\nb\""), HZ_S("\"c\\rd\""), HZ_S("\"00:10 New - Line\""),
                                 HZ_S("\"w/ x\""),  HZ_S("\"\""),      HZ_S("\"\\ud800\"")};
  static const tp_str odd[] = {HZ_S("null"), HZ_S("5"), HZ_S("\"shift\""), HZ_S("[]"), HZ_S("{}")};
  static const tp_str keys[] = {HZ_S("line"), HZ_S("seconds"), HZ_S("from"), HZ_S("after")};
  static const tp_str text_keys[] = {HZ_S("rest"), HZ_S("text")};
  tp_strvec edits = {0};
  tp_json v;
  for (int n = hz_below(r, 7); n > 0; n--) {
    tp_strvec parts = {0};
    if (hz_chance(r, 8)) {
      tp_push(edits, PICK(r, odd));
      continue;
    }
    tp_push(parts, cat3(S("\"op\":\""), PICK(r, ops), S("\"")));
    for (size_t k = 0; k < TP_COUNT(keys); k++) {
      if (hz_chance(r, 60)) tp_push(parts, cat3(S("\""), keys[k], cat(S("\":"), PICK(r, fields))));
    }
    if (hz_chance(r, 30)) {
      tp_strvec items = {0};
      for (int k = hz_below(r, 4); k > 0; k--) tp_push(items, PICK(r, fields));
      tp_push(parts, cat3(S("\"lines\":["), tp_join(items, S(",")), S("]")));
    }
    for (size_t k = 0; k < TP_COUNT(text_keys); k++) {
      if (hz_chance(r, 60)) {
        tp_str value = PICK(r, fields);
        if (hz_chance(r, 80)) value = PICK(r, texts);
        tp_push(parts, cat3(S("\""), text_keys[k], cat(S("\":"), value)));
      }
    }
    tp_push(edits, cat3(S("{"), tp_join(parts, S(",")), S("}")));
  }
  if (!tp_json_parse(cat3(S("["), tp_join(edits, S(",")), S("]")), &v)) {
    hz_fail("loose edits: the generator wrote invalid JSON");
  }
  return v;
}

tp_format_options hz_format_options_from(hz_rng *r, tp_str s) {
  static const tp_str feats[] = {HZ_S(""),      HZ_S("source"), HZ_S("artist"),
                                 HZ_S("title"), HZ_S("omit"),   HZ_S("x")};
  static const tp_str markers[] = {HZ_S(""), HZ_S("feat."), HZ_S("ft."), HZ_S("&")};
  static const tp_str joiners[] = {HZ_S(""), HZ_S("original"), HZ_S("canonical"), HZ_S("x")};
  static const tp_str versions[] = {HZ_S(""), HZ_S("all"), HZ_S("none"), HZ_S("x")};
  tp_format_options o;
  int m;
  memset(&o, 0, sizeof o);
  o.feat = PICK(r, feats);
  m = hz_below(r, 7); /* nil, nil, "", "feat.", "ft.", "&", s */
  if (m >= 2 && m <= 5) {
    o.feat_marker = &markers[m - 2];
  } else if (m == 6) {
    tp_str *p = (tp_str *)tp_alloc(sizeof *p);
    *p = s;
    o.feat_marker = p;
  }
  o.joiners = PICK(r, joiners);
  o.versions = PICK(r, versions);
  o.omit_producers = hz_chance(r, 30);
  o.position = hz_chance(r, 50);
  return o;
}

static tp_strvec strs(const tp_str *p, size_t n) {
  tp_strvec v = {0};
  for (size_t i = 0; i < n; i++) tp_push(v, p[i]);
  return v;
}

tp_parse_options hz_fuzz_parse_options(int i) {
  tp_parse_options o;
  memset(&o, 0, sizeof o);
  switch (i % HZ_FUZZ_PARSE_OPTIONS) {
  case 1:
    o.mode = TP_MODE_YOUTUBE;
    o.uploader = S("Noisia");
    break;
  case 2: {
    static const tp_str known[] = {HZ_S("Camo & Krooked"), HZ_S("Chase & Status"), HZ_S(" "),
                                   HZ_S("")};
    o.mode = TP_MODE_FILENAME;
    o.known_artists = strs(known, TP_COUNT(known));
    break;
  }
  case 3:
    o.mode = TP_MODE_CLEAN;
    o.split_and = TP_SPLIT_AND_NEVER;
    break;
  case 4:
    o.mode = TP_MODE_TRACKLIST;
    o.split_and = TP_SPLIT_AND_ALWAYS;
    break;
  case 5:
    o.mode = S("nonsense");
    o.split_and = S("nonsense");
    break;
  case 6: {
    static const tp_str descriptors[] = {HZ_S("Deluxe"), HZ_S(""), HZ_S(" ")};
    static const tp_str genres[] = {HZ_S("neurofunk"), HZ_S("x")};
    static const tp_str feat[] = {HZ_S("with"), HZ_S("&"), HZ_S("-")};
    tp_version_head h;
    tp_junk_phrase j;
    h.key = S("rerub");
    h.type = TP_VERSION_TYPE_REMIX;
    tp_push(o.keywords.version_heads, h);
    h.key = S("2");
    h.type = S("x");
    tp_push(o.keywords.version_heads, h);
    h.key = S("10");
    h.type = TP_VERSION_TYPE_DUB;
    tp_push(o.keywords.version_heads, h);
    o.keywords.descriptors = strs(descriptors, TP_COUNT(descriptors));
    o.keywords.genres = strs(genres, TP_COUNT(genres));
    j.phrase = S("out now");
    j.kind = TP_JUNK_KIND_PROMO;
    tp_push(o.keywords.junk, j);
    j.phrase = S("1");
    j.kind = S("weird");
    tp_push(o.keywords.junk, j);
    j.phrase = S("\xef\xbb\xbf");
    j.kind = TP_EMPTY;
    tp_push(o.keywords.junk, j);
    o.keywords.feat_markers = strs(feat, TP_COUNT(feat));
    break;
  }
  default:
    break;
  }
  return o;
}

static const double *num(double v) {
  double *p = (double *)tp_alloc(sizeof *p);
  *p = v;
  return p;
}

tp_tracklist_options hz_fuzz_tracklist_options(int i) {
  tp_tracklist_options o;
  memset(&o, 0, sizeof o);
  switch (i % HZ_FUZZ_TRACKLIST_OPTIONS) {
  case 1:
    o.mode = TP_MODE_AUTO;
    break;
  case 2:
    o.mode = TP_MODE_YOUTUBE;
    o.audio_seconds = num(600);
    break;
  case 3: {
    static const tp_str known[] = {HZ_S("Camo & Krooked")};
    static const tp_str markers[] = {HZ_S("Break"), HZ_S(""), HZ_S("ID")};
    o.known_artists = strs(known, TP_COUNT(known));
    o.marker_words = strs(markers, TP_COUNT(markers));
    break;
  }
  case 4:
    o.split_and = TP_SPLIT_AND_NEVER;
    o.audio_seconds = num(-1);
    break;
  case 5:
    o.audio_seconds = num(0);
    break;
  case 6: {
    tp_junk_phrase j;
    j.phrase = S("out now");
    j.kind = TP_JUNK_KIND_PROMO;
    tp_push(o.keywords.junk, j);
    break;
  }
  default:
    break;
  }
  return o;
}

/* -------------------------------------------------------------------------------------------- */
/* Single-track checks                                                                          */

tp_str hz_track_json(tp_track t) {
  t.input = TP_EMPTY;
  return tp_track_json(&t);
}

static tp_str full_json(const tp_track *t) { return tp_track_json(t); }

/* trimmedWS: no R0.2 whitespace at either end. */
static bool trimmed_ws(tp_str s) {
  tp_runes r = tp_runes_of(s);
  return r.n == 0 || (!tp_is_whitespace(r.p[0]) && !tp_is_whitespace(r.p[r.n - 1]));
}

/* Invariant 5: names are non-empty and trimmed, and no two credits share a dedup key (R9.2:
 * producers are deduped only among producers). */
void hz_check_artist_hygiene(tp_str s, const tp_artist_vec *artists) {
  tp_map seen = {0};
  for (size_t i = 0; i < artists->n; i++) {
    const tp_artist *a = &artists->p[i];
    tp_str k;
    if (a->name.n == 0 || !trimmed_ws(a->name)) {
      hz_fail("%s: bad artist name %s", hz_q(s), hz_q(a->name));
    }
    k = cat(tp_str_eq(a->role, TP_ROLE_PRODUCER) ? S("p:") : S("c:"), tp_dedup_key(a->name));
    if (tp_map_has(&seen, k)) hz_fail("%s: duplicate artist %s", hz_q(s), hz_q(a->name));
    tp_map_put(&seen, k, 1);
  }
}

static tp_strvec all_modes_plus(void) {
  tp_strvec modes = {0}, rest = tp_all_modes();
  tp_push(modes, TP_MODE_AUTO);
  tp_push(modes, S("bogus"));
  tp_append(modes, rest);
  return modes;
}

/* Invariants 1, 2, 4 and 5 on any string: every single-track function in every mode, with the
 * string as its own options, returns; Parser and the one-shot functions agree; normalize is
 * idempotent. */
void hz_check_track_totality(tp_str s) {
  static const tp_str feats[] = {HZ_S(""),      HZ_S("source"), HZ_S("artist"),
                                 HZ_S("title"), HZ_S("omit"),   HZ_S("nope")};
  tp_str clean = tp_sanitize(s), once, twice;
  tp_track def = tp_api_parse(s);
  tp_strvec modes = all_modes_plus();
  if (!tp_str_eq(def.input, clean)) {
    hz_fail("%s: input %s is not the sanitized input", hz_q(s), hz_q(def.input));
  }
  hz_check_artist_hygiene(s, &def.artists);
  (void)tp_api_format(&def);
  (void)tp_api_all_artists(&def);
  (void)tp_api_parse_artists(s);
  once = tp_api_normalize(s);
  twice = tp_api_normalize(once);
  if (!tp_str_eq(once, twice)) {
    hz_fail("normalize is not idempotent on %s: %s then %s", hz_q(s), hz_q(once), hz_q(twice));
  }
  for (size_t m = 0; m < modes.n; m++) {
    tp_parse_options o;
    tp_parser *p;
    tp_track track, with;
    tp_str a, b;
    memset(&o, 0, sizeof o);
    o.mode = modes.p[m];
    o.uploader = s;
    tp_push(o.known_artists, s);
    tp_push(o.known_artists, S("a"));
    tp_push(o.known_artists, TP_EMPTY);
    o.split_and = TP_SPLIT_AND_ALWAYS;
    p = tp_new_parser(&o);
    track = tp_parser_parse(p, s);
    if (!tp_str_eq(track.input, clean)) {
      hz_fail("%s in %s: input %s", hz_q(s), hz_q(o.mode), hz_q(track.input));
    }
    with = tp_api_parse_with(s, &o);
    a = full_json(&track);
    b = full_json(&with);
    if (!tp_str_eq(a, b)) {
      hz_fail("%s in %s: Parser and ParseWith differ:\n%s\n%s", hz_q(s), hz_q(o.mode), a.p, b.p);
    }
    hz_check_artist_hygiene(s, &track.artists);
    a = tp_artists_json(tp_parser_parse_artists(p, s));
    b = tp_artists_json(tp_api_parse_artists_with(s, &o));
    if (!tp_str_eq(a, b)) {
      hz_fail("%s in %s: Parser.ParseArtists and ParseArtistsWith differ", hz_q(s), hz_q(o.mode));
    }
    for (size_t f = 0; f < TP_COUNT(feats); f++) {
      tp_format_options fo;
      memset(&fo, 0, sizeof fo);
      fo.feat = feats[f];
      fo.feat_marker = &s;
      fo.joiners = TP_JOINERS_CANONICAL;
      fo.position = true;
      (void)tp_api_format_with(&track, &fo);
    }
  }
}

/* A public string result as an arena string (the result is freed). */
static tp_str own(trackparse_string *x, const char *what, tp_str s) {
  tp_str r;
  if (x == NULL) hz_fail("%s(%s) returned NULL", what, hz_q(s));
  r = tp_str_copy(x->str.ptr, x->str.len);
  if (x->str.ptr[x->str.len] != '\0') hz_fail("%s(%s): not NUL-terminated", what, hz_q(s));
  trackparse_string_free(x);
  return r;
}

static void same(const char *what, tp_str s, tp_str got, tp_str want) {
  if (!tp_str_eq(got, want)) {
    hz_fail("%s(%s): the public API differs from the internal one:\n  public   %s\n  internal %s",
            what, hz_q(s), hz_q(got), hz_q(want));
  }
}

static const char *nz(tp_str s) { return s.p ? s.p : ""; }

void hz_check_public(tp_str s) {
  trackparse_track *t = trackparse_parse(nz(s), s.n);
  trackparse_artists *a;
  trackparse_parse_options *po;
  trackparse_format_options *fo;
  trackparse_parser *parser;
  tp_track it = tp_api_parse(s);
  tp_parse_options o;
  tp_format_options ifo;
  if (t == NULL) hz_fail("trackparse_parse(%s) returned NULL", hz_q(s));
  same("trackparse_parse", s, own(trackparse_track_to_json(t), "to_json", s), full_json(&it));
  same("trackparse_format", s, own(trackparse_format(t), "format", s), tp_api_format(&it));
  a = trackparse_all_artists(t);
  if (a == NULL) hz_fail("trackparse_all_artists(%s) returned NULL", hz_q(s));
  same("trackparse_all_artists", s, own(trackparse_artists_to_json(a), "to_json", s),
       tp_artists_json(tp_api_all_artists(&it)));
  trackparse_artists_free(a);

  /* Format options through the public setters and the typed struct. */
  fo = trackparse_format_options_new();
  if (fo == NULL || !trackparse_format_options_set_feat(fo, TRACKPARSE_FEAT_TITLE) ||
      !trackparse_format_options_set_feat_marker(fo, nz(s), s.n) ||
      !trackparse_format_options_set_joiners(fo, TRACKPARSE_JOINERS_CANONICAL) ||
      !trackparse_format_options_set_versions(fo, TRACKPARSE_VERSIONS_NONE) ||
      !trackparse_format_options_set_omit_producers(fo, true) ||
      !trackparse_format_options_set_position(fo, true)) {
    hz_fail("format options: out of memory");
  }
  memset(&ifo, 0, sizeof ifo);
  ifo.feat = TP_FEAT_TITLE;
  ifo.feat_marker = &s;
  ifo.joiners = TP_JOINERS_CANONICAL;
  ifo.versions = TP_VERSIONS_NONE;
  ifo.omit_producers = true;
  ifo.position = true;
  same("trackparse_format_with", s, own(trackparse_format_with(t, fo), "format_with", s),
       tp_api_format_with(&it, &ifo));
  trackparse_format_options_free(fo);
  trackparse_track_free(t);

  a = trackparse_parse_artists(nz(s), s.n);
  if (a == NULL) hz_fail("trackparse_parse_artists(%s) returned NULL", hz_q(s));
  same("trackparse_parse_artists", s, own(trackparse_artists_to_json(a), "to_json", s),
       tp_artists_json(tp_api_parse_artists(s)));
  trackparse_artists_free(a);
  same("trackparse_normalize", s, own(trackparse_normalize(nz(s), s.n), "normalize", s),
       tp_api_normalize(s));

  /* Parse options: the string as uploader and known artist, in youtube mode. */
  po = trackparse_parse_options_new();
  if (po == NULL || !trackparse_parse_options_set_mode(po, TRACKPARSE_MODE_YOUTUBE) ||
      !trackparse_parse_options_set_uploader(po, nz(s), s.n) ||
      !trackparse_parse_options_add_known_artist(po, nz(s), s.n) ||
      !trackparse_parse_options_set_split_and(po, TRACKPARSE_SPLIT_AND_ALWAYS) ||
      !trackparse_parse_options_add_feat_marker(po, "with", TRACKPARSE_NUL_TERMINATED)) {
    hz_fail("parse options: out of memory");
  }
  memset(&o, 0, sizeof o);
  o.mode = TP_MODE_YOUTUBE;
  o.uploader = s;
  tp_push(o.known_artists, s);
  o.split_and = TP_SPLIT_AND_ALWAYS;
  tp_push(o.keywords.feat_markers, S("with"));
  it = tp_api_parse_with(s, &o);
  t = trackparse_parse_with(nz(s), s.n, po);
  if (t == NULL) hz_fail("trackparse_parse_with(%s) returned NULL", hz_q(s));
  same("trackparse_parse_with", s, own(trackparse_track_to_json(t), "to_json", s), full_json(&it));
  trackparse_track_free(t);
  parser = trackparse_parser_new(po);
  trackparse_parse_options_free(po); /* the parser keeps its own copy */
  if (parser == NULL) hz_fail("trackparse_parser_new: out of memory");
  t = trackparse_parser_parse(parser, nz(s), s.n);
  if (t == NULL) hz_fail("trackparse_parser_parse(%s) returned NULL", hz_q(s));
  same("trackparse_parser_parse", s, own(trackparse_track_to_json(t), "to_json", s),
       full_json(&it));
  trackparse_track_free(t);
  a = trackparse_parser_parse_artists(parser, nz(s), s.n);
  if (a == NULL) hz_fail("trackparse_parser_parse_artists(%s) returned NULL", hz_q(s));
  same("trackparse_parser_parse_artists", s, own(trackparse_artists_to_json(a), "to_json", s),
       tp_artists_json(tp_api_parse_artists_with(s, &o)));
  trackparse_artists_free(a);
  trackparse_parser_free(parser);
}

/* Invariant 3 on a track-shaped input: doubled spaces, dash variants and a zero-width space
 * change nothing but the input. */
void hz_check_noise_insensitive(tp_str s, int cut) {
  tp_str base = hz_track_json(tp_api_parse(s));
  tp_runes rs = tp_runes_of(s);
  tp_str noisy[4];
  size_t c = (size_t)cut % (rs.n + 1);
  noisy[0] = tp_replace_all(s, S(" "), S("  "));
  noisy[1] = tp_replace_all(s, S(" - "), S(" \u2013 "));
  noisy[2] = tp_replace_all(s, S(" - "), S(" \u2014 "));
  noisy[3] = cat3(tp_runes_str(tp_runes_slice(rs, 0, c)), S("\u200b"),
                  tp_runes_str(tp_runes_slice(rs, c, rs.n)));
  for (size_t i = 0; i < 4; i++) {
    tp_str got = hz_track_json(tp_api_parse(noisy[i]));
    if (!tp_str_eq(got, base)) {
      hz_fail("noise changed the parse of %s (%s):\n%s\n%s", hz_q(s), hz_q(noisy[i]), base.p,
              got.p);
    }
  }
}

/* What R11.6 promises to keep: names with roles, the title, each version's type and names. */
static tp_str shape(const tp_track *t) {
  tp_buf b = {0};
  for (size_t i = 0; i < t->artists.n; i++) {
    tp_buf_str(&b, t->artists.p[i].name);
    tp_buf_byte(&b, '/');
    tp_buf_str(&b, t->artists.p[i].role);
    tp_buf_byte(&b, ';');
  }
  tp_buf_byte(&b, '|');
  tp_buf_str(&b, t->title);
  tp_buf_byte(&b, '|');
  for (size_t i = 0; i < t->versions.n; i++) {
    const tp_version *v = &t->versions.p[i];
    tp_buf_str(&b, v->type);
    tp_buf_byte(&b, ':');
    for (size_t k = 0; k < v->artists.n; k++) {
      tp_buf_str(&b, v->artists.p[k].name);
      tp_buf_byte(&b, ',');
    }
    tp_buf_byte(&b, ';');
  }
  return tp_buf_done(&b);
}

/* Invariant 8 (R11.6) on a warning-free track-shaped input. */
void hz_check_round_trip(tp_str s) {
  tp_track first = tp_api_parse(s), second;
  tp_str formatted, a, b;
  if (first.warnings.n > 0) return;
  formatted = tp_api_format(&first);
  second = tp_api_parse(formatted);
  a = shape(&first);
  b = shape(&second);
  if (!tp_str_eq(a, b)) hz_fail("round trip %s -> %s:\n%s\n%s", hz_q(s), hz_q(formatted), a.p, b.p);
}

/* strings.Trim(w, "()[],."): ASCII cutset. */
static bool in_cutset(char c) {
  return c == '(' || c == ')' || c == '[' || c == ']' || c == ',' || c == '.';
}

/* Invariant 6: the words of normalize(input), minus separators, joiners, feat markers and bracket
 * characters. */
static tp_strvec content_words(tp_str normalized) {
  tp_strvec words = tp_split_words(normalized), out = {0};
  for (size_t i = 0; i < words.n; i++) {
    tp_str w = words.p[i];
    size_t a = 0, b = w.n;
    if (tp_str_eq(w, S("-")) || tp_str_eq(w, S("&")) || tp_str_eq(w, S("x")) ||
        tp_str_eq(w, S("vs.")) || tp_str_eq(w, S("feat."))) {
      continue;
    }
    while (a < b && in_cutset(w.p[a])) a++;
    while (b > a && in_cutset(w.p[b - 1])) b--;
    if (b > a) tp_push(out, tp_str_slice(w, a, b));
  }
  return out;
}

static tp_strvec output_strings(const tp_track *t) {
  tp_strvec out = {0};
  tp_push(out, t->title);
  tp_push(out, t->full_title);
  for (size_t i = 0; i < t->artists.n; i++) tp_push(out, t->artists.p[i].name);
  for (size_t i = 0; i < t->versions.n; i++) {
    tp_push(out, t->versions.p[i].raw);
    for (size_t k = 0; k < t->versions.p[i].artists.n; k++) {
      tp_push(out, t->versions.p[i].artists.p[k].name);
    }
  }
  for (size_t i = 0; i < t->junk.n; i++) tp_push(out, t->junk.p[i].raw);
  if (t->position) tp_push(out, t->position->raw);
  if (t->timestamp) tp_push(out, t->timestamp->raw);
  return out;
}

/* Invariant 6 ("no word goes missing") on a track-shaped input. */
void hz_check_conservation(tp_str s) {
  tp_track track = tp_api_parse(s);
  tp_strvec fields = output_strings(&track), words = content_words(tp_api_normalize(s));
  for (size_t i = 0; i < words.n; i++) {
    bool found = false;
    for (size_t k = 0; k < fields.n && !found; k++) found = tp_contains(fields.p[k], words.p[i]);
    if (!found) {
      hz_fail("%s of %s is in no output field: %s", hz_q(words.p[i]), hz_q(s), full_json(&track).p);
    }
  }
}

tp_str hz_decorated(hz_rng *r) {
  static const tp_str prefixes[] = {HZ_S("01. "), HZ_S("7. "), HZ_S("12. "), HZ_S("[03:45] "),
                                    HZ_S("1:02:03 ")};
  static const tp_str suffixes[] = {HZ_S(" (Official Video)"), HZ_S(" [HD]"),
                                    HZ_S(" [Free Download]"), HZ_S(" (VIP)")};
  tp_str s = hz_track_string(r);
  if (hz_chance(r, 30)) s = cat(PICK(r, prefixes), s);
  if (hz_chance(r, 30)) s = cat(s, PICK(r, suffixes));
  return s;
}

static bool junk_eq(tp_junk_vec a, tp_junk_vec b) {
  if (a.n != b.n) return false;
  for (size_t i = 0; i < a.n; i++) {
    if (!tp_str_eq(a.p[i].raw, b.p[i].raw) || !tp_str_eq(a.p[i].kind, b.p[i].kind)) return false;
  }
  return true;
}

/* A copy of a track's versions that can be changed without touching the original. */
static tp_version_vec own_versions(tp_version_vec v) {
  tp_version_vec out;
  tp_clone(out, v);
  return out;
}

/* Invariant 7 on a warning-free track-shaped input. */
void hz_check_metamorphic(tp_str s) {
  tp_track plain = tp_api_parse(s), numbered, noisy, square, upper;
  tp_junk_vec want = {0};
  tp_junk video;
  tp_str head, group, shouted, a, b;
  ptrdiff_t open;
  if (plain.warnings.n > 0) return;
  /* Prefixing `01. ` only sets the position. */
  numbered = tp_api_parse(cat(S("01. "), s));
  if (numbered.position == NULL || !tp_str_eq(numbered.position->raw, S("01")) ||
      numbered.position->number != 1) {
    hz_fail("01. %s: position %s", s.p, full_json(&numbered).p);
  }
  numbered.position = NULL;
  a = hz_track_json(plain);
  b = hz_track_json(numbered);
  if (!tp_str_eq(a, b)) {
    hz_fail("01. changed more than the position of %s:\n%s\n%s", hz_q(s), a.p, b.p);
  }
  /* Appending ` (Official Video)` only adds junk (and may switch the mode to youtube). */
  noisy = tp_api_parse(cat(s, S(" (Official Video)")));
  tp_append(want, plain.junk);
  video.raw = S("Official Video");
  video.kind = TP_JUNK_KIND_VIDEO;
  tp_push(want, video);
  if (!junk_eq(noisy.junk, want)) {
    hz_fail("%s (Official Video): junk %s", hz_q(s), full_json(&noisy).p);
  }
  if (!tp_str_eq(noisy.mode, plain.mode) && !tp_str_eq(noisy.mode, TP_MODE_YOUTUBE)) {
    hz_fail("%s (Official Video): mode %s", hz_q(s), hz_q(noisy.mode));
  }
  noisy.junk = plain.junk;
  noisy.mode = plain.mode;
  b = hz_track_json(noisy);
  if (!tp_str_eq(a, b)) {
    hz_fail("(Official Video) changed more than the junk of %s:\n%s\n%s", hz_q(s), a.p, b.p);
  }
  if (!tp_has_suffix(s, S(" Remix)"))) return;
  open = tp_last_index_byte(s, '(');
  head = tp_str_slice(s, 0, (size_t)open);
  group = tp_str_slice(s, (size_t)open + 1, s.n - 1);
  /* (X Remix) and [X Remix] differ only in the delimiter. */
  square = tp_api_parse(cat(head, cat3(S("["), group, S("]"))));
  if (square.versions.n == 0 ||
      !tp_str_eq(tp_last(square.versions).delimiter, TP_DELIMITER_SQUARE)) {
    hz_fail("%s: no square version", hz_q(s));
  }
  square.versions = own_versions(square.versions);
  for (size_t i = 0; i < square.versions.n; i++) {
    square.versions.p[i].delimiter = TP_DELIMITER_PAREN;
  }
  square.full_title = plain.full_title;
  b = hz_track_json(square);
  if (!tp_str_eq(a, b)) {
    hz_fail("[ ] changed more than the delimiter of %s:\n%s\n%s", hz_q(s), a.p, b.p);
  }
  /* REMIX and Remix differ only in raw. */
  shouted = tp_replace_all(group, S(" Remix"), S(" REMIX"));
  upper = tp_api_parse(cat(head, cat3(S("("), shouted, S(")"))));
  if (upper.versions.n == 0 || !tp_str_eq(tp_last(upper.versions).raw, shouted)) {
    hz_fail("%s: REMIX raw %s", hz_q(s), full_json(&upper).p);
  }
  upper.versions = own_versions(upper.versions);
  for (size_t i = 0; i < upper.versions.n; i++) {
    if (i < plain.versions.n) upper.versions.p[i].raw = plain.versions.p[i].raw;
  }
  upper.full_title = plain.full_title;
  b = hz_track_json(upper);
  if (!tp_str_eq(a, b)) hz_fail("REMIX changed more than raw in %s:\n%s\n%s", hz_q(s), a.p, b.p);
}

void hz_check_shaped_track(hz_rng *r) {
  tp_str s = hz_track_string(r);
  tp_track track;
  tp_format_options fo;
  hz_check_noise_insensitive(s, hz_below(r, 1000));
  hz_check_round_trip(s);
  hz_check_metamorphic(s);
  hz_check_conservation(hz_decorated(r));
  track = tp_api_parse(s);
  fo = hz_format_options_from(r, s);
  (void)tp_api_format_with(&track, &fo);
}

/* -------------------------------------------------------------------------------------------- */
/* Tracklist checks                                                                             */

static tp_strvec raw_lines(const tp_tracklist *l) {
  tp_strvec out = {0};
  for (size_t i = 0; i < l->entries.n; i++) {
    tp_push(out, cat(l->entries.p[i].raw, l->entries.p[i].eol));
  }
  return out;
}

static tp_tracklist parse_list(tp_str text) { return tp_api_parse_tracklist(text); }

static tp_strvec raws(tp_str text) {
  tp_tracklist l = parse_list(text);
  tp_strvec out = {0};
  for (size_t i = 0; i < l.entries.n; i++) tp_push(out, l.entries.p[i].raw);
  return out;
}

/* R13.11 invariants 1 and 2 on any text: lossless, parts concatenate to raw, line numbering and
 * terminators, the kind/track pairing. */
void hz_check_tracklist(tp_str text, const tp_tracklist_options *o) {
  tp_str clean = tp_sanitize(text), got;
  tp_tracklist l = tp_api_parse_tracklist_with(text, o);
  got = tp_join(raw_lines(&l), TP_EMPTY);
  if (!tp_str_eq(got, clean)) hz_fail("not lossless: %s gives %s", hz_q(text), hz_q(got));
  if (l.stats.lines != (ptrdiff_t)l.entries.n) {
    hz_fail("%s: stats.lines %ld, %lu entries", hz_q(text), (long)l.stats.lines,
            (unsigned long)l.entries.n);
  }
  for (size_t i = 0; i < l.entries.n; i++) {
    const tp_entry *e = &l.entries.p[i];
    const tp_parts *p = &e->parts;
    bool is_track =
        tp_str_eq(e->kind, TP_ENTRY_KIND_TRACK) || tp_str_eq(e->kind, TP_ENTRY_KIND_MARKER);
    if (!tp_str_eq(cat(cat3(p->lead, p->time, p->gap), p->rest), e->raw)) {
      hz_fail("%s line %lu: parts do not join to %s", hz_q(text), (unsigned long)i + 1,
              hz_q(e->raw));
    }
    /* R13.1.3: without a time, `lead` holds at most a bullet; `time` and `gap` are empty. */
    if ((e->time == NULL) != (p->time.n == 0) || (e->time == NULL && p->gap.n != 0)) {
      hz_fail("%s line %lu: time and parts disagree", hz_q(text), (unsigned long)i + 1);
    }
    if ((e->track != NULL) != is_track) {
      hz_fail("%s line %lu: kind %s with track %d", hz_q(text), (unsigned long)i + 1, hz_q(e->kind),
              e->track != NULL);
    }
    if (tp_contains_any(e->raw, S("\r\n"))) {
      hz_fail("%s line %lu: raw %s holds a line break", hz_q(text), (unsigned long)i + 1,
              hz_q(e->raw));
    }
    if (e->line != (ptrdiff_t)i + 1) {
      hz_fail("%s: entry %lu has line %ld", hz_q(text), (unsigned long)i, (long)e->line);
    }
    /* Only the last line may lack a terminator. */
    if (e->eol.n == 0 && i != l.entries.n - 1) {
      hz_fail("%s line %lu: no terminator before the last line", hz_q(text), (unsigned long)i + 1);
    }
  }
}

static tp_json empty_object(void) {
  tp_json v = tp_json_null();
  v.kind = TP_JSON_OBJECT;
  v.obj = (const tp_json_member *)tp_alloc(0);
  return v;
}

/* R13.11 invariant 2 for edits: any edit list through the JSON door returns a result whose
 * changed lines are sorted, unique and in range, and whose text parses losslessly. */
void hz_check_loose_edits(tp_str text, tp_json edits) {
  tp_str clean = tp_sanitize(text), out, new_text = TP_EMPTY;
  tp_json v, changed;
  tp_jw ew = tp_jw_new();
  const char *what;
  size_t n;
  double prev = 0;
  tp_tracklist l;
  tp_json_write(&edits, &ew);
  {
    tp_buf b = {0};
    tp_buf_cstr(&b, "applyEdits(");
    tp_buf_cstr(&b, hz_q(text));
    tp_buf_cstr(&b, ", ");
    tp_buf_str(&b, tp_jw_done(&ew));
    tp_buf_byte(&b, ')');
    what = tp_buf_done(&b).p;
  }
  out = tp_wire_call(S("applyEdits"), clean, empty_object(), empty_object(), edits);
  if (!tp_json_parse(out, &v)) hz_fail("%s: invalid JSON %s", what, hz_q(out));
  tp_json_get_string(v, S("text"), &new_text);
  l = parse_list(new_text);
  n = l.entries.n;
  if (!tp_json_get(v, S("changed"), &changed) || changed.kind != TP_JSON_ARRAY) {
    hz_fail("%s: no changed array in %s", what, out.p);
  }
  for (size_t i = 0; i < changed.arr_n; i++) {
    const tp_json *c = &changed.arr[i];
    if (c->kind != TP_JSON_NUMBER || c->num <= prev || c->num > (double)n) {
      hz_fail("%s: changed out of order or range with %lu lines: %s", what, (unsigned long)n,
              out.p);
    }
    prev = c->num;
  }
  if (!tp_str_eq(tp_join(raw_lines(&l), TP_EMPTY), new_text)) {
    hz_fail("%s: the result does not parse losslessly", what);
  }
}

static bool warned(const tp_edit_result *r, const tp_str *codes, size_t n) {
  for (size_t i = 0; i < r->warnings.n; i++) {
    for (size_t k = 0; k < n; k++) {
      if (tp_str_eq(r->warnings.p[i].code, codes[k])) return true;
    }
  }
  return false;
}

static tp_edit_result apply1(tp_str text, tp_edit e) { return tp_api_apply_edits(text, &e, 1); }

/* R13.11 invariant 3: no edits return the text unchanged. */
void hz_check_identity(tp_str text) {
  tp_edit none[1];
  for (int k = 0; k < 2; k++) {
    tp_edit_result r = tp_api_apply_edits(text, k == 0 ? NULL : none, 0);
    if (!tp_str_eq(r.text, tp_sanitize(text)) || r.changed.n != 0 || r.warnings.n != 0) {
      hz_fail("applyEdits(%s, none) = %s", hz_q(text), tp_edit_result_json(&r).p);
    }
  }
}

/* R13.11 invariant 3: an edit on one line leaves every other line byte for byte, and changes only
 * that line. */
void hz_check_locality(tp_str text, hz_rng *r) {
  static const tp_str invalid[] = {HZ_S("edit-invalid")};
  tp_tracklist before, after_list;
  tp_strvec lines, after;
  int64_t line;
  double seconds;
  tp_edit edit;
  tp_edit_result res;
  const char *what;
  text = tp_sanitize(text);
  before = parse_list(text);
  lines = raw_lines(&before);
  if (lines.n == 0) return;
  line = 1 + hz_below(r, (int)lines.n);
  seconds = (double)(hz_below(r, 8001) - 4000);
  switch (hz_below(r, 3)) {
  case 0:
    edit = tp_edit_set_time(line, seconds);
    break;
  case 1:
    edit = tp_edit_set_rest(line, S("New - Rest"));
    break;
  default:
    edit = tp_edit_shift_lines(seconds, &line, 1);
  }
  {
    tp_jw w = tp_jw_new();
    tp_edit_write(&edit, &w);
    what = tp_jw_done(&w).p;
  }
  res = apply1(text, edit);
  after_list = parse_list(res.text);
  after = raw_lines(&after_list);
  if (after.n != lines.n) {
    hz_fail("%s on %s: %lu lines became %lu", what, hz_q(text), (unsigned long)lines.n,
            (unsigned long)after.n);
  }
  for (size_t i = 0; i < after.n; i++) {
    if ((int64_t)i != line - 1 && !tp_str_eq(after.p[i], lines.p[i])) {
      hz_fail("%s on %s: line %lu changed: %s -> %s", what, hz_q(text), (unsigned long)i + 1,
              hz_q(lines.p[i]), hz_q(after.p[i]));
    }
  }
  for (size_t i = 0; i < res.changed.n; i++) {
    if (res.changed.p[i] != line) {
      hz_fail("%s on %s: changed %s", what, hz_q(text), tp_edit_result_json(&res).p);
    }
  }
  if (warned(&res, invalid, 1) && (!tp_str_eq(res.text, text) || res.changed.n != 0)) {
    hz_fail("%s on %s: an invalid edit changed the text", what, hz_q(text));
  }
}

/* hasBareCR: a "\r" that is not part of a "\r\n". */
static bool has_bare_cr(tp_str text) {
  for (size_t i = 0; i < text.n; i++) {
    if (text.p[i] == '\r' && (i + 1 >= text.n || text.p[i + 1] != '\n')) return true;
  }
  return false;
}

/* R13.11 invariant 3 for insert and delete: the other lines stay. */
void hz_check_insert_delete(tp_str text, hz_rng *r) {
  tp_strvec before, expected = {0}, got, kept = {0};
  tp_str new_line = S("00:00 New - Line");
  int at, line;
  tp_edit_result ins, del;
  bool found = false;
  text = tp_sanitize(text);
  before = raws(text);
  /* R13.9.5: after an edit the lines are re-read, so a line ending in a bare "\r" next to an
   * empty "\n" line merges with it. The simple expectation below does not model that. */
  if (before.n == 0 || has_bare_cr(text)) return;
  at = hz_below(r, (int)before.n + 1);
  ins = apply1(text, tp_edit_insert(at, new_line));
  tp_append(expected, before);
  tp_insert(expected, (size_t)at, new_line);
  got = raws(ins.text);
  if (!strvec_eq(got, expected)) {
    hz_fail("insert after %d in %s: %s, want %s", at, hz_q(text), strvec_show(got),
            strvec_show(expected));
  }
  for (size_t i = 0; i < ins.changed.n; i++) found = found || ins.changed.p[i] == at + 1;
  if (!found) {
    hz_fail("insert after %d in %s: changed %s", at, hz_q(text), tp_edit_result_json(&ins).p);
  }
  line = 1 + hz_below(r, (int)before.n);
  del = apply1(text, tp_edit_delete(line));
  tp_append(kept, before);
  tp_remove(kept, (size_t)line - 1, (size_t)line);
  got = raws(del.text);
  /* Deleting the last line moves its eol ("" here) up: an empty line that becomes last without
   * an eol is not a line (R13.9.5), so trailing empty lines may disappear. */
  while (kept.n > 0 && tp_last(kept).n == 0 && got.n < kept.n && (size_t)line == before.n) kept.n--;
  if (!strvec_eq(got, kept)) {
    hz_fail("delete %d in %s: %s, want %s", line, hz_q(text), strvec_show(got), strvec_show(kept));
  }
}

static tp_intvec time_widths(tp_str text) {
  tp_tracklist l = parse_list(text);
  tp_intvec out = {0};
  for (size_t i = 0; i < l.entries.n; i++) {
    const tp_entry *e = &l.entries.p[i];
    tp_push(out, e->time ? (int64_t)e->time->raw.n : -1);
  }
  return out;
}

static bool intvec_eq(tp_intvec a, tp_intvec b) {
  return a.n == b.n && (a.n == 0 || memcmp(a.p, b.p, a.n * sizeof *a.p) == 0);
}

/* R13.11 invariant 4: shift(+s) then shift(-s) restores the text, unless a time was clamped or
 * upgraded or its digit count grew (R13.9.7). Reports whether the round trip was checked. */
bool hz_check_shift_round_trip(tp_str text, hz_rng *r) {
  static const tp_str lossy[] = {HZ_S("time-clamped"), HZ_S("style-upgraded"),
                                 HZ_S("edit-invalid")};
  double s;
  size_t n;
  int64_t from;
  bool use_from;
  tp_edit_result up, down;
  text = tp_sanitize(text);
  s = (double)(1 + hz_below(r, 5000));
  n = parse_list(text).entries.n;
  from = 1 + hz_below(r, 12);
  use_from = hz_chance(r, 50) && (size_t)from <= n;
  up = apply1(text, use_from ? tp_edit_shift_from(s, from) : tp_edit_shift(s));
  down = apply1(up.text, use_from ? tp_edit_shift_from(-s, from) : tp_edit_shift(-s));
  if (warned(&up, lossy, TP_COUNT(lossy)) || warned(&down, lossy, TP_COUNT(lossy)) ||
      !intvec_eq(time_widths(up.text), time_widths(text))) {
    return false;
  }
  if (!tp_str_eq(down.text, text)) {
    hz_fail("shift +-%s on %s gives %s", tp_number_str(s).p, hz_q(text), hz_q(down.text));
  }
  return true;
}

static tp_strvec eols_of(tp_str text) {
  tp_tracklist l = parse_list(text);
  tp_strvec out = {0};
  for (size_t i = 0; i < l.entries.n; i++) tp_push(out, l.entries.p[i].eol);
  return out;
}

/* R13.11 invariant 4 for sort-by-time: the eol slots stay, times ascend, a second sort changes
 * nothing. */
void hz_check_sort(tp_str text) {
  tp_edit_result once, twice;
  tp_strvec a, b;
  tp_tracklist l;
  double prev = -1;
  text = tp_sanitize(text);
  once = apply1(text, tp_edit_sort_by_time());
  /* An empty line sorted into the last slot (eol "") is not a line any more (R13.1.1). */
  a = eols_of(once.text);
  b = eols_of(text);
  if (a.n == b.n && !strvec_eq(a, b)) {
    hz_fail("sort of %s moved the terminators: %s", hz_q(text), hz_q(once.text));
  }
  l = parse_list(once.text);
  for (size_t i = 0; i < l.entries.n; i++) {
    const tp_entry *e = &l.entries.p[i];
    if (e->time) {
      if (e->time->seconds < prev) {
        hz_fail("sort of %s is not sorted: %s", hz_q(text), hz_q(once.text));
      }
      prev = e->time->seconds;
    }
  }
  twice = apply1(once.text, tp_edit_sort_by_time());
  if (!tp_str_eq(twice.text, once.text)) hz_fail("sort of %s is not idempotent", hz_q(text));
}

/* R13.11 invariant 5: the track of a one-line text is the parse of its line text in tracklist
 * mode. Reports whether the line had a track. */
bool hz_check_one_line(tp_str text) {
  tp_tracklist l;
  const tp_entry *e;
  tp_parse_options o;
  tp_track want;
  tp_str a, b;
  text = tp_replace_all(tp_replace_all(tp_sanitize(text), S("\r"), S(" ")), S("\n"), S(" "));
  l = parse_list(text);
  if (l.entries.n == 0) return false;
  if (l.entries.n != 1) hz_fail("%s: %lu entries", hz_q(text), (unsigned long)l.entries.n);
  e = &l.entries.p[0];
  if (e->track == NULL) return false;
  memset(&o, 0, sizeof o);
  o.mode = TP_MODE_TRACKLIST;
  want = tp_api_parse_with(e->track->input, &o);
  a = full_json(e->track);
  b = full_json(&want);
  if (!tp_str_eq(a, b)) {
    hz_fail("%s: the entry's track is not the parse of its text:\n%s\n%s", hz_q(text), a.p, b.p);
  }
  if (!tp_contains_any(e->parts.rest, S("*_")) && !e->layered &&
      !tp_str_eq(e->track->input, e->parts.rest)) {
    hz_fail("%s: track input %s, rest %s", hz_q(text), hz_q(e->track->input), hz_q(e->parts.rest));
  }
  return true;
}

const tp_str hz_emphasis_rests[] = {
    HZ_S("Noisia - Stigma"),
    HZ_S("01. Phace - Plastic Acid"),
    HZ_S("Intro"),
    HZ_S("Paperclip interview"),
    HZ_S("Mefjus - Suicide Bassline (VIP)"),
    HZ_S("ID - ID"),
    HZ_S("Noisia feat. Foreign Beggars - Shellshock"),
};
const size_t hz_emphasis_rests_n = TP_COUNT(hz_emphasis_rests);

/* R13.11 invariant 6: emphasis around the time or the track part changes neither the time, the
 * kind nor the track. */
void hz_check_emphasis(tp_str t, tp_str rest) {
  static const tp_str wraps[][2] = {
      {HZ_S(""), HZ_S("")},       {HZ_S("**"), HZ_S("**")},     {HZ_S("__"), HZ_S("__")},
      {HZ_S("*"), HZ_S("*")},     {HZ_S("_"), HZ_S("_")},       {HZ_S("**["), HZ_S("]**")},
      {HZ_S("[**"), HZ_S("**]")}, {HZ_S("**[**"), HZ_S("**]")}, {HZ_S("_["), HZ_S("]_")},
  };
  static const tp_str rest_wraps[] = {HZ_S(""), HZ_S("**"), HZ_S("__"), HZ_S("*"), HZ_S("_")};
  tp_str plain = cat3(t, S(" "), rest);
  tp_tracklist bl = parse_list(plain);
  const tp_entry *bare = &bl.entries.p[0];
  for (size_t w = 0; w < TP_COUNT(wraps); w++) {
    for (size_t k = 0; k < TP_COUNT(rest_wraps); k++) {
      tp_str decorated = cat(cat3(wraps[w][0], t, wraps[w][1]),
                             cat(S(" "), cat3(rest_wraps[k], rest, rest_wraps[k])));
      tp_tracklist l = parse_list(decorated);
      const tp_entry *e = &l.entries.p[0];
      if ((e->time == NULL) != (bare->time == NULL) ||
          (e->time != NULL && e->time->seconds != bare->time->seconds)) {
        hz_fail("%s: time differs from %s", hz_q(decorated), hz_q(plain));
      }
      if (!tp_str_eq(e->kind, bare->kind)) {
        hz_fail("%s: kind %s, want %s", hz_q(decorated), hz_q(e->kind), hz_q(bare->kind));
      }
      if ((e->track == NULL) != (bare->track == NULL) ||
          (e->track != NULL && !tp_str_eq(full_json(e->track), full_json(bare->track)))) {
        hz_fail("%s: track differs from %s", hz_q(decorated), hz_q(plain));
      }
    }
  }
}

/* -------------------------------------------------------------------------------------------- */
/* The public tracklist and edit functions against the internal ones                            */

void hz_check_public_tracklist(tp_str text) {
  static const long long shift_lines[] = {1, 2};
  static const int64_t shift_lines64[] = {1, 2};
  trackparse_tracklist *l = trackparse_parse_tracklist(nz(text), text.n);
  trackparse_tracklist_options *po;
  trackparse_edits *pe;
  trackparse_edit_result *pr;
  tp_tracklist_options o;
  tp_edit edits[7];
  tp_edit_result ir;
  tp_tracklist il;
  if (l == NULL) hz_fail("trackparse_parse_tracklist(%s) returned NULL", hz_q(text));
  il = tp_api_parse_tracklist(text);
  same("trackparse_parse_tracklist", text, own(trackparse_tracklist_to_json(l), "to_json", text),
       tp_tracklist_json(&il));
  trackparse_tracklist_free(l);

  po = trackparse_tracklist_options_new();
  if (po == NULL || !trackparse_tracklist_options_set_mode(po, TRACKPARSE_MODE_AUTO) ||
      !trackparse_tracklist_options_set_audio_seconds(po, 600.5) ||
      !trackparse_tracklist_options_add_known_artist(po, "Camo & Krooked", 14) ||
      !trackparse_tracklist_options_add_marker_word(po, "Break", TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_tracklist_options_add_junk(po, "out now", TRACKPARSE_NUL_TERMINATED, "promo",
                                             TRACKPARSE_NUL_TERMINATED)) {
    hz_fail("tracklist options: out of memory");
  }
  memset(&o, 0, sizeof o);
  o.mode = TP_MODE_AUTO;
  o.audio_seconds = num(600.5);
  tp_push(o.known_artists, S("Camo & Krooked"));
  tp_push(o.marker_words, S("Break"));
  {
    tp_junk_phrase j;
    j.phrase = S("out now");
    j.kind = TP_JUNK_KIND_PROMO;
    tp_push(o.keywords.junk, j);
  }
  l = trackparse_parse_tracklist_with(nz(text), text.n, po);
  trackparse_tracklist_options_free(po);
  if (l == NULL) hz_fail("trackparse_parse_tracklist_with(%s) returned NULL", hz_q(text));
  il = tp_api_parse_tracklist_with(text, &o);
  same("trackparse_parse_tracklist_with", text,
       own(trackparse_tracklist_to_json(l), "to_json", text), tp_tracklist_json(&il));
  trackparse_tracklist_free(l);

  pe = trackparse_edits_new();
  if (pe == NULL || !trackparse_edits_set_time(pe, 1, 62.5) ||
      !trackparse_edits_shift_from(pe, -1.25, 2) ||
      !trackparse_edits_shift_lines(pe, 3, shift_lines, 2) || !trackparse_edits_sort_by_time(pe) ||
      !trackparse_edits_insert(pe, 0, "00:00 X - Y", TRACKPARSE_NUL_TERMINATED) ||
      !trackparse_edits_set_rest(pe, 2, nz(text), text.n) || !trackparse_edits_delete(pe, 3)) {
    hz_fail("edits: out of memory");
  }
  edits[0] = tp_edit_set_time(1, 62.5);
  edits[1] = tp_edit_shift_from(-1.25, 2);
  edits[2] = tp_edit_shift_lines(3, shift_lines64, 2);
  edits[3] = tp_edit_sort_by_time();
  edits[4] = tp_edit_insert(0, S("00:00 X - Y"));
  edits[5] = tp_edit_set_rest(2, text);
  edits[6] = tp_edit_delete(3);
  pr = trackparse_apply_edits(nz(text), text.n, pe);
  trackparse_edits_free(pe);
  if (pr == NULL) hz_fail("trackparse_apply_edits(%s) returned NULL", hz_q(text));
  ir = tp_api_apply_edits(text, edits, TP_COUNT(edits));
  same("trackparse_apply_edits", text, own(trackparse_edit_result_to_json(pr), "to_json", text),
       tp_edit_result_json(&ir));
  trackparse_edit_result_free(pr);
}

/* -------------------------------------------------------------------------------------------- */
/* Fixture cases                                                                                */

static char *heap_copy(tp_str s) {
  char *p = (char *)malloc(s.n + 1);
  if (!p) {
    fprintf(stderr, "out of memory\n");
    exit(2);
  }
  if (s.n) memcpy(p, s.p, s.n);
  p[s.n] = '\0';
  return p;
}

static char *read_whole(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  char *buf = NULL;
  size_t n = 0, cap = 0;
  if (!f) return NULL;
  for (;;) {
    size_t got;
    if (cap - n < 65536) {
      char *nb;
      cap = cap ? cap * 2 : 1 << 20;
      nb = (char *)realloc(buf, cap);
      if (!nb) {
        free(buf);
        fclose(f);
        return NULL;
      }
      buf = nb;
    }
    got = fread(buf + n, 1, cap - n, f);
    n += got;
    if (got == 0) break;
  }
  fclose(f);
  *len = n;
  return buf;
}

/* {...defaults[key], ...c[key]} (shallow), as the fixture runner merges options. */
static tp_json merged(tp_json defaults, tp_json c, tp_str key) {
  TP_VEC(tp_json_member) members = {0};
  tp_json out = empty_object();
  tp_json srcs[2];
  srcs[0] = defaults;
  srcs[1] = c;
  for (size_t s = 0; s < 2; s++) {
    tp_json v;
    if (!tp_json_get(srcs[s], key, &v) || v.kind != TP_JSON_OBJECT) continue;
    for (size_t i = 0; i < v.obj_n; i++) {
      bool replaced = false;
      for (size_t j = 0; j < members.n; j++) {
        if (tp_str_eq(members.p[j].key, v.obj[i].key)) {
          members.p[j].val = v.obj[i].val;
          replaced = true;
        }
      }
      if (!replaced) tp_push(members, v.obj[i]);
    }
  }
  if (members.n) {
    out.obj = members.p;
    out.obj_n = members.n;
  }
  return out;
}

typedef struct load_arg {
  const char *path;
  const char *text;
  size_t len;
  hz_cases *out;
  size_t cap;
} load_arg;

static void load_file(void *p) {
  load_arg *a = (load_arg *)p;
  tp_json data, cases, defaults = tp_json_null();
  tp_str whole;
  whole.p = a->text;
  whole.n = a->len;
  if (!tp_json_parse(whole, &data)) {
    fprintf(stderr, "%s: invalid JSON\n", a->path);
    exit(2);
  }
  if (!tp_json_get(data, S("cases"), &cases) || cases.kind != TP_JSON_ARRAY) return;
  tp_json_get(data, S("defaults"), &defaults);
  for (size_t i = 0; i < cases.arr_n; i++) {
    tp_json c = cases.arr[i], options, format_options, edits;
    tp_str fn, input = TP_EMPTY, id = TP_EMPTY;
    tp_jw w = tp_jw_new();
    hz_case *hc;
    if (!tp_json_get_string(c, S("fn"), &fn) && !tp_json_get_string(defaults, S("fn"), &fn)) {
      fn = S("parse");
    }
    tp_json_get_string(c, S("input"), &input);
    tp_json_get_string(c, S("id"), &id);
    options = merged(defaults, c, S("options"));
    format_options = merged(defaults, c, S("formatOptions"));
    if (!tp_json_get(c, S("edits"), &edits)) {
      edits = tp_json_null();
      edits.kind = TP_JSON_ARRAY;
      edits.arr = (const tp_json *)tp_alloc(0);
    }
    tp_jw_open(&w, '{');
    tp_jw_key(&w, S("fn"));
    tp_jw_str(&w, fn);
    tp_jw_key(&w, S("input"));
    tp_jw_str(&w, input);
    tp_jw_key(&w, S("options"));
    tp_json_write(&options, &w);
    tp_jw_key(&w, S("formatOptions"));
    tp_json_write(&format_options, &w);
    tp_jw_key(&w, S("edits"));
    tp_json_write(&edits, &w);
    tp_jw_close(&w, '}');
    if (a->out->n == a->cap) {
      hz_case *ni;
      a->cap = a->cap ? a->cap * 2 : 1024;
      ni = (hz_case *)realloc(a->out->items, a->cap * sizeof *ni);
      if (!ni) {
        fprintf(stderr, "out of memory\n");
        exit(2);
      }
      a->out->items = ni;
    }
    hc = &a->out->items[a->out->n++];
    hc->id = heap_copy(id);
    hc->fn = heap_copy(fn);
    hc->input = heap_copy(input);
    hc->input_n = input.n;
    {
      tp_str line = tp_jw_done(&w);
      hc->line = heap_copy(line);
      hc->line_n = line.n;
    }
    hc->has_options = options.obj_n > 0 || format_options.obj_n > 0;
  }
}

hz_cases hz_load_cases(int count, char **paths) {
  hz_cases out = {NULL, 0};
  load_arg a;
  memset(&a, 0, sizeof a);
  a.out = &out;
  for (int i = 0; i < count; i++) {
    size_t len = 0;
    char *text = read_whole(paths[i], &len);
    if (!text) {
      fprintf(stderr, "cannot read %s\n", paths[i]);
      exit(2);
    }
    a.path = paths[i];
    a.text = text;
    a.len = len;
    hz_scoped(load_file, &a);
    free(text);
  }
  return out;
}

void hz_free_cases(hz_cases *c) {
  for (size_t i = 0; i < c->n; i++) {
    free(c->items[i].id);
    free(c->items[i].fn);
    free(c->items[i].input);
    free(c->items[i].line);
  }
  free(c->items);
  c->items = NULL;
  c->n = 0;
}
