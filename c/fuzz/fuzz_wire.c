/* fuzz_wire.c: libFuzzer target for the JSON door (tp_wire_line), what the dump tool runs for
 * compare-ports and fuzz-ports.
 *
 * Input: one JSONL case line {"fn","input","options","formatOptions","edits"}, any bytes. The
 * JSON reader and every function behind loose JSON options must return: the output is JSON the
 * reader reads back, and the same line gives the same bytes twice. */
#include "../tests/hardening.h"
#include "../wire/json_read.h"
#include "../wire/wire.h"

static void body(void *p) {
  const tp_str *line = (const tp_str *)p;
  tp_str out = tp_wire_line(*line), again;
  tp_json v;
  if (!tp_json_parse(out, &v)) hz_fail("%s: invalid JSON output %s", hz_q(*line), hz_q(out));
  again = tp_wire_line(*line);
  if (!tp_str_eq(out, again)) hz_fail("%s: two runs differ:\n%s\n%s", hz_q(*line), out.p, again.p);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  tp_str line;
  line.p = size ? (const char *)data : "";
  line.n = size;
  hz_scoped(body, &line);
  return 0;
}
