/* fuzz_format.c: libFuzzer target for format (go/fuzz_test.go FuzzFormat).
 *
 * Input: 8 bytes of generator seed (little-endian; missing bytes are 0), then the string. Formats
 * the parse of the string with the default options and with options drawn from the seed, takes
 * all_artists, then runs every track-shaped invariant (3, 6, 7, 8) on a track drawn from the
 * seed (hz_check_shaped_track). */
#include "../tests/hardening.h"

typedef struct arg {
  tp_str s;
  uint64_t seed;
} arg;

static void body(void *p) {
  const arg *a = (const arg *)p;
  hz_rng r = hz_rng_new(a->seed);
  tp_parse_options o = hz_fuzz_parse_options(hz_below(&r, HZ_FUZZ_PARSE_OPTIONS));
  tp_track track = tp_api_parse_with(a->s, &o);
  tp_format_options fo;
  (void)tp_api_format(&track);
  fo = hz_format_options_from(&r, a->s);
  (void)tp_api_format_with(&track, &fo);
  (void)tp_api_all_artists(&track);
  hz_check_shaped_track(&r);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  arg a;
  size_t k = size < 8 ? size : 8;
  a.seed = 0;
  for (size_t i = 0; i < k; i++) a.seed |= (uint64_t)data[i] << (8 * i);
  a.s.p = size > 8 ? (const char *)data + 8 : "";
  a.s.n = size > 8 ? size - 8 : 0;
  hz_scoped(body, &a);
  return 0;
}
