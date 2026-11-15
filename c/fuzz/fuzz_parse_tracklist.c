/* fuzz_parse_tracklist.c: libFuzzer target for parse_tracklist (go/fuzz_test.go
 * FuzzParseTracklist).
 *
 * Input: 8 bytes of generator seed (little-endian), then the text. Checks R13.11 invariants 1
 * and 2 with the default options and with an option set drawn from the seed, invariant 5 on the
 * text as one line, invariant 6 on a time and rest drawn from the seed, and the public tracklist
 * and edit functions against the internal ones. */
#include "../tests/hardening.h"

typedef struct arg {
  tp_str text;
  uint64_t seed;
} arg;

static void body(void *p) {
  const arg *a = (const arg *)p;
  hz_rng r = hz_rng_new(a->seed);
  tp_tracklist_options none, o;
  tp_str style, t, rest;
  int seconds, frac;
  memset(&none, 0, sizeof none);
  hz_check_tracklist(a->text, &none);
  o = hz_fuzz_tracklist_options(hz_below(&r, HZ_FUZZ_TRACKLIST_OPTIONS));
  hz_check_tracklist(a->text, &o);
  (void)hz_check_one_line(a->text);
  style = hz_time_styles[hz_below(&r, (int)hz_time_styles_n)];
  seconds = hz_below(&r, 3600);
  frac = hz_below(&r, 1000);
  t = hz_write_time(style, seconds, frac);
  rest = hz_emphasis_rests[hz_below(&r, (int)hz_emphasis_rests_n)];
  hz_check_emphasis(t, rest);
  hz_check_public_tracklist(a->text);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  arg a;
  size_t k = size < 8 ? size : 8;
  a.seed = 0;
  for (size_t i = 0; i < k; i++) a.seed |= (uint64_t)data[i] << (8 * i);
  a.text.p = size > 8 ? (const char *)data + 8 : "";
  a.text.n = size > 8 ? size - 8 : 0;
  hz_scoped(body, &a);
  return 0;
}
