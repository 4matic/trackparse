/* tl_options.c: the JS number tests the edits (R13.9) apply to loose JSON values
 * (go/tl_options.go).
 *
 * R13.10 tracklist options are resolved in options.c (tp_resolve_tracklist,
 * tp_resolve_tracklist_json, tp_js_number). */
#include "internal.h"

/* Number.isInteger without libm: finite and equal to its floor. */
static bool tl_is_whole(double n) { return !tp_is_nan(n) && !tp_is_inf(n) && n == tp_floor(n); }

TP_PRIVATE bool tp_js_integer(tp_json v, bool present, double *out) {
  double n;
  if (!tp_js_number(v, present, &n) || !tl_is_whole(n)) return false;
  *out = n;
  return true;
}

TP_PRIVATE bool tp_js_finite(tp_json v, bool present, double *out) {
  double n;
  if (!tp_js_number(v, present, &n) || tp_is_nan(n) || tp_is_inf(n)) return false;
  *out = n;
  return true;
}
