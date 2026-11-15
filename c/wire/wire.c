/* wire.c: the JSON door (go/wire.go): fixture-style calls with loose JSON arguments, for the
 * fixture runner, the dump tool (compare-ports, fuzz-ports) and the fuzzers. Never installed. */
#include "wire.h"

#include "../src/internal.h"

/* artistList.write: a list of artists as JSON. */
static void tp_artist_list_write(tp_artist_vec l, tp_jw *w) { tp_write_artists(w, l); }

/* wireString.write: a JSON string. */
static void tp_wire_string_write(tp_str s, tp_jw *w) { tp_jw_str(w, s); }

/* wireCall is one fixture-style call: fn is parse, parseArtists, format, normalize,
 * parseTracklist or applyEdits; format parses input with options first, then formats with
 * formatOptions. Unknown functions give null. input must be valid UTF-8 (the JSON reader
 * guarantees it). */
TP_PRIVATE tp_str tp_wire_call(tp_str fn, tp_str input, tp_json options, tp_json format_options,
                               tp_json edits) {
  tp_jw w = tp_jw_new();
  if (tp_str_eq_c(fn, "parse")) {
    tp_track t = tp_engine_run(tp_engine_new(tp_resolve_parse_json(options)), input, false);
    tp_track_write(&t, &w);
  } else if (tp_str_eq_c(fn, "parseArtists")) {
    tp_artist_list_write(
        tp_engine_run_artists(tp_engine_new(tp_resolve_parse_json(options)), input), &w);
  } else if (tp_str_eq_c(fn, "format")) {
    tp_track t = tp_engine_run(tp_engine_new(tp_resolve_parse_json(options)), input, false);
    tp_wire_string_write(tp_format_track(&t, tp_resolve_format_json(format_options)), &w);
  } else if (tp_str_eq_c(fn, "normalize")) {
    tp_wire_string_write(tp_normalize(input), &w);
  } else if (tp_str_eq_c(fn, "parseTracklist")) {
    tp_tracklist l = tp_parse_tracklist(input, tp_resolve_tracklist_json(options));
    tp_tracklist_write(&l, &w);
  } else if (tp_str_eq_c(fn, "applyEdits")) {
    /* Only an array has edits. */
    const tp_json *list = NULL;
    size_t n = 0;
    tp_raw_edit_result r;
    if (edits.kind == TP_JSON_ARRAY) {
      list = edits.arr;
      n = edits.arr_n;
    }
    r = tp_apply_edits(input, list, n);
    tp_raw_edit_result_write(&r, &w);
  } else {
    return TP_S("null");
  }
  return tp_jw_done(&w);
}
