/* fuzz_apply_edits.c: libFuzzer target for apply_edits (go/fuzz_test.go FuzzApplyEdits).
 *
 * Input: 8 bytes of generator seed and 4 bytes of text length (both little-endian), the text,
 * then the edits: used as a JSON edit list when they are JSON, else they seed a random loose
 * edit list (hz_loose_edits). Checks R13.11 invariants 2 to 4 on the text and on a timed list
 * drawn from the seed: any edit list returns a sound result, no edits is the identity, edits
 * stay local, insert and delete keep the other lines, shift round trips, sort is sorted and
 * idempotent. */
#include "../tests/hardening.h"
#include "../wire/json_read.h"

typedef struct arg {
  tp_str text;
  tp_str edits;
  uint64_t seed;
} arg;

static void body(void *p) {
  const arg *a = (const arg *)p;
  hz_rng r = hz_rng_new(a->seed);
  tp_json loose;
  tp_str list;
  if (!tp_json_parse(a->edits, &loose)) {
    hz_rng g = hz_rng_new(hz_fnv1a(a->edits.p, a->edits.n) ^ a->seed);
    loose = hz_loose_edits(&g);
  }
  hz_check_loose_edits(a->text, loose);
  hz_check_identity(a->text);
  hz_check_locality(a->text, &r);
  hz_check_insert_delete(a->text, &r);
  (void)hz_check_shift_round_trip(a->text, &r);
  hz_check_sort(a->text);
  /* The same checks on a timed list drawn from the seed, where most edits apply. */
  list = hz_timed_list(&r);
  hz_check_locality(list, &r);
  (void)hz_check_shift_round_trip(list, &r);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  arg a;
  size_t len = 0, rest;
  a.seed = 0;
  for (size_t i = 0; i < 8 && i < size; i++) a.seed |= (uint64_t)data[i] << (8 * i);
  for (size_t i = 0; i < 4 && 8 + i < size; i++) len |= (size_t)data[8 + i] << (8 * i);
  rest = size > 12 ? size - 12 : 0;
  if (len > rest) len = rest;
  a.text.p = size > 12 ? (const char *)data + 12 : "";
  a.text.n = len;
  a.edits.p = a.text.p + len;
  a.edits.n = rest - len;
  hz_scoped(body, &a);
  return 0;
}
