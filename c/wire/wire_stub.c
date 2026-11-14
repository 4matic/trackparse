/* wire_stub.c: a temporary JSON door that answers "null" to every call, so the harness builds
 * and reports 0% before the pipeline exists. CMake drops this file as soon as wire/wire.c
 * exists; wire.c then defines tp_wire_call (go/wire.go). */
#include "wire.h"

TP_PRIVATE tp_str tp_wire_call(tp_str fn, tp_str input, tp_json options, tp_json format_options,
                               tp_json edits) {
  (void)fn;
  (void)input;
  (void)options;
  (void)format_options;
  (void)edits;
  return TP_S("null");
}
