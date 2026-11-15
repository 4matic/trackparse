/* fuzz_parse_artists.c: libFuzzer target for parse_artists (go/fuzz_test.go FuzzParseArtists).
 *
 * Input: one byte choosing an option set, then the string. Checks invariant 5 (names non-empty,
 * trimmed, no duplicate dedup keys), that every artist comes from the artist side, and that the
 * public function gives the internal JSON. */
#include "../include/trackparse.h"
#include "../tests/hardening.h"

typedef struct arg {
  tp_str s;
  int set;
} arg;

static void check(tp_str s, tp_artist_vec artists) {
  hz_check_artist_hygiene(s, &artists);
  for (size_t i = 0; i < artists.n; i++) {
    if (!tp_str_eq(artists.p[i].source, TP_SOURCE_ARTIST)) {
      hz_fail("%s: source %s", hz_q(s), hz_q(artists.p[i].source));
    }
  }
}

static void body(void *p) {
  const arg *a = (const arg *)p;
  tp_parse_options o = hz_fuzz_parse_options(a->set);
  tp_artist_vec plain = tp_api_parse_artists(a->s);
  trackparse_artists *pub = trackparse_parse_artists(a->s.n ? a->s.p : "", a->s.n);
  trackparse_string *j;
  check(a->s, plain);
  check(a->s, tp_api_parse_artists_with(a->s, &o));
  if (!pub) hz_fail("trackparse_parse_artists returned NULL");
  j = trackparse_artists_to_json(pub);
  if (!j) hz_fail("trackparse_artists_to_json returned NULL");
  {
    tp_str want = tp_artists_json(plain), got;
    got.p = j->str.ptr;
    got.n = j->str.len;
    if (!tp_str_eq(got, want)) hz_fail("%s: public %s, internal %s", hz_q(a->s), got.p, want.p);
  }
  trackparse_string_free(j);
  trackparse_artists_free(pub);
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
