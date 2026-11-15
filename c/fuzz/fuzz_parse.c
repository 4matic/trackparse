/* fuzz_parse.c: libFuzzer target for parse (go/fuzz_test.go FuzzParse).
 *
 * Input: one byte choosing an option set (hz_fuzz_parse_options), then the string, any bytes
 * (invalid UTF-8 included: the API reads it with the WHATWG replacement rule). Checks invariants
 * 1, 2, 4 and 5 in every mode (hz_check_track_totality), the public API against the internal
 * one, and parse_with against a parser with the same options. */
#include "../tests/hardening.h"

typedef struct arg {
  tp_str s;
  int set;
} arg;

static void body(void *p) {
  const arg *a = (const arg *)p;
  tp_parse_options o = hz_fuzz_parse_options(a->set);
  tp_str clean = tp_sanitize(a->s), x, y;
  tp_track track, again;
  hz_check_track_totality(a->s);
  hz_check_public(a->s);
  track = tp_api_parse_with(a->s, &o);
  if (!tp_str_eq(track.input, clean)) hz_fail("%s: input %s", hz_q(a->s), hz_q(track.input));
  hz_check_artist_hygiene(a->s, &track.artists);
  again = tp_parser_parse(tp_new_parser(&o), a->s);
  x = tp_track_json(&track);
  y = tp_track_json(&again);
  if (!tp_str_eq(x, y)) hz_fail("%s: ParseWith and Parser differ:\n%s\n%s", hz_q(a->s), x.p, y.p);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  arg a;
  a.set = size ? data[0] : 0;
  a.s.p = size ? (const char *)data + 1 : "";
  a.s.n = size ? size - 1 : 0;
  hz_scoped(body, &a);
  return 0;
}
