/* wire.h: the JSON door (go/wire.go wireCall). Fixture-style calls with loose JSON arguments, for
 * the fixture runner, the dump tool (compare-ports, fuzz-ports) and the fuzzers. Never installed.
 */
#ifndef TP_WIRE_H
#define TP_WIRE_H

#include "../src/rt.h"
#include "json_read.h"

/* One fixture-style call: fn is parse, parseArtists, format, normalize, parseTracklist or
 * applyEdits; format parses input with options first, then formats with format_options.
 * applyEdits reads edits only when it is an array. Unknown functions give "null". input must be
 * valid UTF-8 (the JSON reader guarantees it). Returns the JSON text of the result, written as
 * JSON.stringify writes it, allocated in the current arena (run it inside an armed scope). */
TP_PRIVATE tp_str tp_wire_call(tp_str fn, tp_str input, tp_json options, tp_json format_options,
                               tp_json edits);

/* One case object {"fn", "input", "options", "formatOptions", "edits"} as JSON text (go/wire_hook.go
 * wireLine): missing fn means parse, missing options are {}, missing edits []. Text that is not
 * a JSON object gives "null". Same arena rules as tp_wire_call. */
TP_PRIVATE tp_str tp_wire_line(tp_str line);

#endif /* TP_WIRE_H */
