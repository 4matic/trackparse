/* json_read.h: the JSON reader of the wire door (go/json.go parseJSON).
 *
 * Never part of the installed library: only the wire library, the fixture runner, the dump tool
 * and the fuzzers read JSON (PLAN.md decision 5).
 */
#ifndef TP_JSON_READ_H
#define TP_JSON_READ_H

#include "../src/rt.h"

/* Nesting deeper than this is rejected, so hostile input cannot exhaust the stack. */
#define TP_JSON_MAX_DEPTH 200

/* Read one JSON value from text (surrounding JSON whitespace allowed). Allocates in the current
 * arena, so it must run inside an armed scope. Lenient where the JS reference has to be matched,
 * exactly as go/json.go: a number is anything tp_parse_number reads from the characters
 * 0-9 + - . e E (so 1e999 is infinity), an unknown escape stands for its character, a repeated
 * key keeps its first place and takes the last value, and unpaired surrogate escapes become
 * U+FFFD. Bytes that are not valid UTF-8 are first replaced (WHATWG, tp_sanitize).
 * false (and *out = null) when the text is not JSON it can read. */
TP_PRIVATE bool tp_json_parse(tp_str text, tp_json *out);

#endif /* TP_JSON_READ_H */
