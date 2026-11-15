/* types.c: the result types (go/types.go). The structs themselves are in internal.h; this file
 * has the All* lists of the typed strings, the C name lookup, and the typing of raw edit
 * results. */
#include "internal.h"

/* A new arena vector holding the n names of a static table (Go: a fresh slice per call). */
static tp_strvec names_of(const tp_str *names, size_t n) {
  tp_strvec v = {0, 0, 0};
  size_t i;
  for (i = 0; i < n; i++) tp_push(v, names[i]);
  return v;
}

#define TP_NAMES(table) names_of((table), TP_COUNT(table))

static const tp_str modes[] = {{"clean", 5}, {"youtube", 7}, {"filename", 8}, {"tracklist", 9}};

/* AllModes: the modes a track can have, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_modes(void) { return TP_NAMES(modes); }

static const tp_str roles[] = {{"primary", 7}, {"featured", 8}, {"remixer", 7}, {"producer", 8}};

/* AllRoles: every Role, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_roles(void) { return TP_NAMES(roles); }

static const tp_str sources[] = {{"artist", 6}, {"title", 5}, {"version", 7}};

/* AllSources: every Source, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_sources(void) { return TP_NAMES(sources); }

static const tp_str version_types[] = {
    {"remix", 5},         {"bootleg", 7},  {"vip", 3},    {"edit", 4},     {"flip", 4},
    {"refix", 5},         {"rework", 6},   {"mashup", 6}, {"blend", 5},    {"dub", 3},
    {"mix", 3},           {"extended", 8}, {"radio", 5},  {"club", 4},     {"original", 8},
    {"instrumental", 12}, {"acapella", 8}, {"live", 4},   {"acoustic", 8}, {"remaster", 8},
    {"demo", 4},          {"reprise", 7},  {"cover", 5},  {"version", 7},  {"spedUp", 6},
    {"slowed", 6},        {"nightcore", 9}};

/* AllVersionTypes: the 27 version types of the spec, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_version_types(void) { return TP_NAMES(version_types); }

static const tp_str delimiters[] = {{"(", 1}, {"[", 1}, {"{", 1}, {"-", 1}};

/* AllDelimiters: every Delimiter, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_delimiters(void) { return TP_NAMES(delimiters); }

static const tp_str junk_kinds[] = {{"video", 5}, {"audio", 5},    {"lyrics", 6}, {"quality", 7},
                                    {"promo", 5}, {"platform", 8}, {"label", 5},  {"genre", 5},
                                    {"other", 5}, {"note", 4}};

/* AllJunkKinds: the junk kinds of the spec, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_junk_kinds(void) { return TP_NAMES(junk_kinds); }

static const tp_str warnings[] = {{"noSeparator", 11},
                                  {"ambiguousSeparator", 18},
                                  {"unspacedDashSplit", 17},
                                  {"asymmetricDashSplit", 19},
                                  {"bySplit", 7},
                                  {"quotedTitleSplit", 16},
                                  {"ambiguousMixCredit", 18},
                                  {"unbalancedBrackets", 18}};

/* AllWarnings: every Warning, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_warnings(void) { return TP_NAMES(warnings); }

static const tp_str entry_kinds[] = {{"track", 5}, {"marker", 6}, {"header", 6},
                                     {"link", 4},  {"note", 4},   {"blank", 5}};

/* AllEntryKinds: every EntryKind, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_entry_kinds(void) { return TP_NAMES(entry_kinds); }

static const tp_str eols[] = {{"\n", 1}, {"\r\n", 2}, {"\r", 1}, {"", 0}};

/* AllEols: every Eol, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_eols(void) { return TP_NAMES(eols); }

static const tp_str warning_codes[] = {
    {"time-missing", 12},     {"time-out-of-order", 17},    {"time-duplicate", 14},
    {"no-start-at-zero", 16}, {"time-after-audio-end", 20}, {"invalid-time", 12},
    {"position-gap", 12},     {"position-duplicate", 18},   {"line-unparsed", 13}};

/* AllWarningCodes: every WarningCode, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_warning_codes(void) { return TP_NAMES(warning_codes); }

static const tp_str severities[] = {{"info", 4}, {"warning", 7}};

/* AllSeverities: every Severity, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_severities(void) { return TP_NAMES(severities); }

static const tp_str edit_ops[] = {{"set-time", 8}, {"shift", 5},  {"sort-by-time", 12},
                                  {"set-rest", 8}, {"insert", 6}, {"delete", 6}};

/* AllEditOps: every EditOp, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_edit_ops(void) { return TP_NAMES(edit_ops); }

static const tp_str edit_warning_codes[] = {
    {"edit-invalid", 12}, {"time-clamped", 12}, {"style-upgraded", 14}};

/* AllEditWarningCodes: every EditWarningCode, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_edit_warning_codes(void) { return TP_NAMES(edit_warning_codes); }

/* C addition: the index of name in names, or -1. */
TP_PRIVATE ptrdiff_t tp_name_index(tp_strvec names, tp_str name) {
  size_t i;
  for (i = 0; i < names.n; i++) {
    if (tp_str_eq(names.p[i], name)) return (ptrdiff_t)i;
  }
  return -1;
}

/* result is the typed form. Typed edits carry integer lines, so every line is a whole number in
 * range; the conversion saturates anyway. A nil changed is written as [] either way. */
TP_PRIVATE tp_edit_result tp_raw_edit_result_result(const tp_raw_edit_result *r) {
  tp_edit_result out;
  size_t i;
  memset(&out, 0, sizeof out);
  out.text = r->text;
  out.changed = r->changed;
  for (i = 0; i < r->warnings.n; i++) {
    const tp_raw_edit_warning *w = &r->warnings.p[i];
    tp_edit_warning ew;
    ew.code = w->code;
    ew.edit = w->edit;
    ew.line = NULL;
    if (w->line != NULL) {
      int64_t *n = (int64_t *)tp_alloc(sizeof *n);
      *n = tp_saturating_int(*w->line);
      ew.line = n;
    }
    tp_push(out.warnings, ew);
  }
  return out;
}

/* saturatingInt converts a float to int64 (Go int on 64-bit), clamping to the range (NaN gives
 * 0). 9223372036854775808.0 is float64(maxInt): 2^63. */
TP_PRIVATE int64_t tp_saturating_int(double f) {
  if (tp_is_nan(f)) return 0;
  if (f >= 9223372036854775808.0) return INT64_MAX;
  if (f <= -9223372036854775808.0) return INT64_MIN;
  return (int64_t)f;
}
