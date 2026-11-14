/* wire_line.c: one JSONL case through the JSON door (go/wire_hook.go wireLine), for the dump
 * tool and the wire fuzzer. */
#include "wire.h"

static tp_json empty_object(void) {
  tp_json v = tp_json_null();
  v.kind = TP_JSON_OBJECT;
  v.obj = (const tp_json_member *)tp_alloc(0);
  return v;
}

static tp_json empty_array(void) {
  tp_json v = tp_json_null();
  v.kind = TP_JSON_ARRAY;
  v.arr = (const tp_json *)tp_alloc(0);
  return v;
}

TP_PRIVATE tp_str tp_wire_line(tp_str line) {
  tp_json c, v, options, format_options, edits;
  tp_str fn = TP_S("parse");
  tp_str input = TP_EMPTY;
  if (!tp_json_parse(line, &c) || c.kind != TP_JSON_OBJECT) return TP_S("null");
  if (tp_json_get(c, TP_S("fn"), &v) && v.kind == TP_JSON_STRING) fn = v.str;
  if (tp_json_get(c, TP_S("input"), &v) && v.kind == TP_JSON_STRING) input = v.str;
  if (!tp_json_get(c, TP_S("options"), &options)) options = empty_object();
  if (!tp_json_get(c, TP_S("formatOptions"), &format_options)) format_options = empty_object();
  if (!tp_json_get(c, TP_S("edits"), &edits)) edits = empty_array();
  return tp_wire_call(fn, input, options, format_options, edits);
}
