/* tl_edits.c: R13.9, applyEdits (go/tl_edits.go). Edits apply in order, each to the result of
 * the previous one.
 *
 * Edits are read as loose JSON objects with the number rules of the JS reference (a bool is not
 * a number; null, NaN and infinities are invalid). Typed edits are turned into that form first
 * (tp_edit_json), so both doors share one code path. */
#include "internal.h"

/* An absent key reads as the zero (null) value, as Go's get returns jsonValue{}. */
static tp_json tl_get(tp_json v, const char *key, bool *present) {
  tp_json out = tp_json_null();
  *present = tp_json_get(v, tp_str_c(key), &out);
  if (!*present) out = tp_json_null();
  return out;
}

TP_PRIVATE tp_work_line *tp_new_work_line(tp_str raw, tp_eol eol, bool changed) {
  tp_work_line *l = tp_alloc(sizeof *l);
  l->raw = raw;
  l->eol = eol;
  l->changed = changed;
  l->scan = tp_scan_time(raw);
  return l;
}

/* eolSlot is the index of eol in edit_ctx.eols, -1 for none. */
TP_PRIVATE ptrdiff_t tp_eol_slot(tp_eol eol) {
  if (tp_str_eq(eol, TP_EOL_LF)) return 0;
  if (tp_str_eq(eol, TP_EOL_CRLF)) return 1;
  if (tp_str_eq(eol, TP_EOL_CR)) return 2;
  return -1;
}

/* countHours adds (by = 1) or removes (by = -1) a time from the hour-width counts (R13.9.2). */
TP_PRIVATE void tp_edit_ctx_count_hours(tp_edit_ctx *c, const tp_time_scan *s, ptrdiff_t by) {
  if (s->style == NULL || !s->style->hours) return;
  if (s->style->hour_width == 1) {
    c->one_digit += by;
  } else {
    c->wide_hours += by;
  }
}

TP_PRIVATE void tp_edit_ctx_count_eol(tp_edit_ctx *c, tp_eol eol, ptrdiff_t by) {
  ptrdiff_t k = tp_eol_slot(eol);
  if (k >= 0) c->eols[k] += by;
}

/* countLine is a line entering (by = 1) or leaving (by = -1) the text. */
TP_PRIVATE void tp_edit_ctx_count_line(tp_edit_ctx *c, const tp_work_line *l, ptrdiff_t by) {
  tp_edit_ctx_count_hours(c, &l->scan, by);
  tp_edit_ctx_count_eol(c, l->eol, by);
}

TP_PRIVATE void tp_edit_ctx_set_eol(tp_edit_ctx *c, tp_work_line *l, tp_eol eol) {
  tp_edit_ctx_count_eol(c, l->eol, -1);
  l->eol = eol;
  l->changed = true;
  tp_edit_ctx_count_eol(c, eol, 1);
}

TP_PRIVATE void tp_edit_ctx_set_raw(tp_edit_ctx *c, tp_work_line *l, tp_str raw) {
  tp_edit_ctx_count_hours(c, &l->scan, -1);
  l->raw = raw;
  l->scan = tp_scan_time(raw);
  l->changed = true;
  tp_edit_ctx_count_hours(c, &l->scan, 1);
}

/* line is copied into the arena (Go's &line escapes to the heap). */
TP_PRIVATE void tp_edit_ctx_warn(tp_edit_ctx *c, tp_edit_warning_code code, const double *line) {
  tp_raw_edit_warning w;
  w.code = code;
  w.edit = c->index;
  w.line = NULL;
  if (line) {
    double *p = tp_alloc(sizeof *p);
    *p = *line;
    w.line = p;
  }
  tp_push(c->warnings, w);
}

/* lineIndex is the 0-based index of an existing 1-based line number. */
TP_PRIVATE bool tp_edit_ctx_line_index(const tp_edit_ctx *c, tp_json v, bool present,
                                       ptrdiff_t *out) {
  double n;
  if (!tp_js_integer(v, present, &n) || n < 1 || n > (double)c->lines.n) return false;
  *out = (ptrdiff_t)n - 1;
  return true;
}

/* oneDigitHours is R13.9.2: do the text's times with hours all use one-digit hours (and is there
 * at least one)? */
TP_PRIVATE bool tp_edit_ctx_one_digit_hours(const tp_edit_ctx *c) {
  return c->one_digit > 0 && c->wide_hours == 0;
}

/* singleLine is a string without a line break. */
TP_PRIVATE bool tp_single_line(tp_json v, bool present, tp_str *out) {
  if (!present || v.kind != TP_JSON_STRING || tp_has_line_break(v.str)) return false;
  *out = v.str;
  return true;
}

/* retime is R13.9.2 / R13.9.3: the rewritten line for a new value; false when it cannot be
 * written. */
TP_PRIVATE bool tp_edit_ctx_retime(tp_edit_ctx *c, ptrdiff_t index, double value, bool one_digit,
                                   tp_retimed *out) {
  const tp_time_scan *s;
  const tp_parts *p;
  bool clamped;
  tp_formatted_time f;
  tp_buf b = {0};
  if (index < 0 || index >= (ptrdiff_t)c->lines.n) return false;
  s = &c->lines.p[index]->scan;
  if (s->style == NULL || s->time == NULL) return false;
  clamped = value < 0;
  if (clamped) value = 0;
  if (!tp_format_time(value, s->style, one_digit, &f)) return false;
  p = &s->parts;
  tp_buf_str(&b, p->lead);
  tp_buf_str(&b, f.raw);
  tp_buf_str(&b, p->gap);
  tp_buf_str(&b, p->rest);
  out->index = index;
  out->raw = tp_buf_done(&b);
  out->clamped = clamped;
  out->upgraded = f.upgraded;
  return true;
}

TP_PRIVATE void tp_edit_ctx_commit(tp_edit_ctx *c, tp_retimed_vec changes) {
  for (size_t k = 0; k < changes.n; k++) {
    const tp_retimed *ch = &changes.p[k];
    double line = (double)(ch->index + 1);
    tp_edit_ctx_set_raw(c, c->lines.p[ch->index], ch->raw);
    if (ch->clamped) tp_edit_ctx_warn(c, TP_EDIT_WARNING_CODE_TIME_CLAMPED, &line);
    if (ch->upgraded) tp_edit_ctx_warn(c, TP_EDIT_WARNING_CODE_STYLE_UPGRADED, &line);
  }
}

TP_PRIVATE bool tp_edit_ctx_set_time(tp_edit_ctx *c, tp_json edit, tp_touched *out) {
  bool present;
  tp_json v;
  ptrdiff_t index;
  double value;
  tp_retimed change;
  tp_retimed_vec changes = {0};
  v = tl_get(edit, "line", &present);
  if (!tp_edit_ctx_line_index(c, v, present, &index)) return false;
  v = tl_get(edit, "seconds", &present);
  if (!tp_js_finite(v, present, &value)) return false;
  if (!tp_edit_ctx_retime(c, index, value, tp_edit_ctx_one_digit_hours(c), &change)) return false;
  tp_push(changes, change);
  tp_edit_ctx_commit(c, changes);
  memset(out, 0, sizeof *out); /* a timed line is never empty and keeps its eol */
  return true;
}

static int tl_idx_cmp(const void *a, const void *b, void *ctx) {
  ptrdiff_t x = *(const ptrdiff_t *)a, y = *(const ptrdiff_t *)b;
  (void)ctx;
  return x < y ? -1 : x > y ? 1 : 0;
}

/* slices.Sort + slices.Compact on an index list, in place. */
static void tl_sort_compact(tp_idxvec *v) {
  size_t w = 0;
  if (v->n < 2) return;
  tp_sort_stable(v->p, v->n, sizeof *v->p, tl_idx_cmp, NULL);
  for (size_t r = 0; r < v->n; r++) {
    if (w == 0 || v->p[w - 1] != v->p[r]) v->p[w++] = v->p[r];
  }
  v->n = w;
}

TP_PRIVATE bool tp_edit_ctx_shift(tp_edit_ctx *c, tp_json edit, tp_touched *out) {
  bool present, one_digit;
  tp_json v;
  double delta;
  tp_idxvec targets = {0};
  tp_retimed_vec changes = {0};
  v = tl_get(edit, "seconds", &present);
  if (!tp_js_finite(v, present, &delta)) return false;
  v = tl_get(edit, "lines", &present);
  if (present) {
    if (v.kind != TP_JSON_ARRAY) return false;
    for (size_t k = 0; k < v.arr_n; k++) {
      ptrdiff_t i;
      if (!tp_edit_ctx_line_index(c, v.arr[k], true, &i)) return false;
      tp_push(targets, i);
    }
    tl_sort_compact(&targets);
  } else {
    ptrdiff_t from = 0;
    v = tl_get(edit, "from", &present);
    if (present && !tp_edit_ctx_line_index(c, v, true, &from)) return false;
    for (ptrdiff_t i = from; i < (ptrdiff_t)c->lines.n; i++) tp_push(targets, i);
  }
  one_digit = tp_edit_ctx_one_digit_hours(c);
  for (size_t k = 0; k < targets.n; k++) {
    ptrdiff_t i = targets.p[k];
    const tp_tracklist_time *t = c->lines.p[i]->scan.time;
    tp_retimed change;
    if (t == NULL) continue;
    if (!tp_edit_ctx_retime(c, i, t->seconds + delta, one_digit, &change)) return false;
    tp_push(changes, change);
  }
  tp_edit_ctx_commit(c, changes);
  memset(out, 0, sizeof *out);
  return true;
}

/* cmp.Compare on float64: NaN sorts before every other value and equals itself. */
static int tl_block_cmp(const void *a, const void *b, void *ctx) {
  double x = ((const tp_time_block *)a)->seconds, y = ((const tp_time_block *)b)->seconds;
  bool xn = tp_is_nan(x), yn = tp_is_nan(y);
  (void)ctx;
  if (xn || yn) return xn && yn ? 0 : xn ? -1 : 1;
  return x < y ? -1 : x > y ? 1 : 0;
}

/* sortByTime is R13.9.4: a stable sort of the blocks. */
TP_PRIVATE tp_touched tp_edit_ctx_sort_by_time(tp_edit_ctx *c) {
  tp_touched t;
  ptrdiff_t first = -1, n = (ptrdiff_t)c->lines.n;
  tp_time_block_vec blocks = {0};
  tp_idxvec order = {0};
  tp_work_line_ptr_vec old = c->lines, lines = {0};
  memset(&t, 0, sizeof t);
  for (ptrdiff_t i = 0; i < n; i++) {
    if (c->lines.p[i]->scan.time != NULL) {
      first = i;
      break;
    }
  }
  if (first < 0) return t;
  for (ptrdiff_t i = first; i < n; i++) {
    const tp_tracklist_time *tm = c->lines.p[i]->scan.time;
    if (tm) {
      tp_time_block b;
      b.seconds = tm->seconds;
      b.from = i;
      b.to = i + 1;
      tp_push(blocks, b);
    } else if (blocks.n > 0) {
      tp_last(blocks).to = i + 1;
    }
  }
  /* Stable: equal times keep their order. */
  tp_sort_stable(blocks.p, blocks.n, sizeof *blocks.p, tl_block_cmp, NULL);
  for (ptrdiff_t i = 0; i < first; i++) tp_push(order, i);
  for (size_t k = 0; k < blocks.n; k++) {
    for (ptrdiff_t i = blocks.p[k].from; i < blocks.p[k].to; i++) tp_push(order, i);
  }
  for (size_t slot = 0; slot < order.n; slot++) {
    ptrdiff_t src = order.p[slot];
    const tp_work_line *l;
    tp_work_line *nl;
    if (src < 0 || src >= (ptrdiff_t)old.n || slot >= old.n) continue;
    l = old.p[src];
    nl = tp_alloc(sizeof *nl);
    nl->raw = l->raw;
    nl->eol = old.p[slot]->eol; /* eol values stay in their slots */
    nl->changed = l->changed || src != (ptrdiff_t)slot;
    nl->scan = l->scan;
    tp_push(lines, nl);
  }
  c->lines = lines;
  t.all = true;
  return t;
}

TP_PRIVATE bool tp_edit_ctx_set_rest(tp_edit_ctx *c, tp_json edit, tp_touched *out) {
  bool present, spaced = false;
  tp_json v;
  ptrdiff_t index;
  tp_str rest;
  tp_work_line *l;
  tp_parts p;
  tp_buf b = {0};
  v = tl_get(edit, "line", &present);
  if (!tp_edit_ctx_line_index(c, v, present, &index)) return false;
  v = tl_get(edit, "rest", &present);
  if (!tp_single_line(v, present, &rest)) return false;
  l = c->lines.p[index];
  p = l->scan.parts;
  /* R13.9 set-rest: a non-empty rest after a time is preceded by whitespace; a gap that does not
   * end with R0.2 whitespace ("", "]", ")") gets one space. */
  if (p.gap.n > 0) {
    size_t len;
    spaced = tp_is_whitespace(tp_decode_last_rune(p.gap, &len));
  }
  tp_buf_str(&b, p.lead);
  tp_buf_str(&b, p.time);
  tp_buf_str(&b, p.gap);
  if (p.time.n > 0 && !spaced && rest.n > 0) tp_buf_byte(&b, ' ');
  tp_buf_str(&b, rest);
  tp_edit_ctx_set_raw(c, l, tp_buf_done(&b));
  memset(out, 0, sizeof *out);
  tp_push(out->lines, index);
  return true;
}

/* Go commonEol's closure isTop. */
static bool tl_is_top(const tp_edit_ctx *c, tp_eol eol, ptrdiff_t best) {
  ptrdiff_t k = tp_eol_slot(eol);
  return k >= 0 && c->eols[k] == best;
}

/* commonEol is R13.9.5: the most common non-empty eol (ties: the one seen first in line order),
 * "\n" if none. The counts are kept up to date; only a tie needs a look at the lines. */
TP_PRIVATE tp_eol tp_edit_ctx_common_eol(const tp_edit_ctx *c) {
  ptrdiff_t best = c->eols[0];
  tp_eol all[3];
  tp_eol top = TP_EOL_LF;
  int ntop = 0;
  if (c->eols[1] > best) best = c->eols[1];
  if (c->eols[2] > best) best = c->eols[2];
  if (best <= 0) return TP_EOL_LF;
  all[0] = TP_EOL_LF;
  all[1] = TP_EOL_CRLF;
  all[2] = TP_EOL_CR;
  for (int k = 0; k < 3; k++) {
    if (tl_is_top(c, all[k], best)) {
      if (ntop == 0) top = all[k];
      ntop++;
    }
  }
  if (ntop == 1) return top;
  for (size_t i = 0; i < c->lines.n; i++) {
    if (tl_is_top(c, c->lines.p[i]->eol, best)) return c->lines.p[i]->eol;
  }
  return TP_EOL_LF;
}

TP_PRIVATE bool tp_edit_ctx_insert(tp_edit_ctx *c, tp_json edit, tp_touched *out) {
  bool present;
  tp_json v;
  double after;
  tp_str text;
  ptrdiff_t pos, count = (ptrdiff_t)c->lines.n;
  tp_eol eol, line_eol;
  tp_work_line *line;
  v = tl_get(edit, "after", &present);
  if (!tp_js_integer(v, present, &after) || after < 0 || after > (double)count) return false;
  v = tl_get(edit, "text", &present);
  if (!tp_single_line(v, present, &text)) return false;
  pos = (ptrdiff_t)after;
  eol = tp_edit_ctx_common_eol(c);
  /* R13.9.5: inserting into an empty text keeps it without a trailing newline. */
  line_eol = count == 0 ? TP_EOL_NONE : eol;
  line = tp_new_work_line(text, line_eol, true);
  if (pos == count && count > 0 && tp_str_eq(c->lines.p[count - 1]->eol, TP_EOL_NONE)) {
    tp_edit_ctx_set_eol(c, c->lines.p[count - 1], eol);
    line->eol = TP_EOL_NONE;
  }
  tp_edit_ctx_count_line(c, line, 1);
  tp_insert(c->lines, (size_t)pos, line);
  memset(out, 0, sizeof *out);
  tp_push(out->lines, pos);
  return true;
}

TP_PRIVATE bool tp_edit_ctx_remove(tp_edit_ctx *c, tp_json edit, tp_touched *out) {
  bool present;
  tp_json v;
  ptrdiff_t index, count;
  tp_work_line *gone;
  v = tl_get(edit, "line", &present);
  if (!tp_edit_ctx_line_index(c, v, present, &index)) return false;
  gone = c->lines.p[index];
  tp_remove(c->lines, (size_t)index, (size_t)index + 1);
  tp_edit_ctx_count_line(c, gone, -1);
  count = (ptrdiff_t)c->lines.n;
  if (index == count && count > 0 && !tp_str_eq(c->lines.p[count - 1]->eol, gone->eol)) {
    /* R13.9.5: the deleted last line's eol moves up */
    tp_edit_ctx_set_eol(c, c->lines.p[count - 1], gone->eol);
  }
  memset(out, 0, sizeof *out);
  tp_push(out->lines, index);
  return true;
}

/* mergeAt joins a "\r" line followed by an empty "\n" line into one CRLF line, as a re-split
 * would read them. */
TP_PRIVATE void tp_edit_ctx_merge_at(tp_edit_ctx *c, ptrdiff_t k) {
  tp_work_line *prev, *next;
  if (k < 0 || k + 1 >= (ptrdiff_t)c->lines.n) return;
  prev = c->lines.p[k];
  next = c->lines.p[k + 1];
  if (!tp_str_eq(prev->eol, TP_EOL_CR) || next->raw.n != 0 || !tp_str_eq(next->eol, TP_EOL_LF)) {
    return;
  }
  tp_edit_ctx_set_eol(c, prev, TP_EOL_CRLF);
  tp_remove(c->lines, (size_t)k + 1, (size_t)k + 2);
  tp_edit_ctx_count_line(c, next, -1);
}

/* canonical is R13.9.5: keep the work lines equal to what splitLines(join(lines)) would give. A
 * "\r" followed by an empty "\n" line reads back as one CRLF, and an empty last line without eol
 * is not a line. The lines are canonical before each edit, so only the neighbourhood of the
 * lines the edit touched (and the end) is checked; all checks every line. */
TP_PRIVATE void tp_edit_ctx_canonical(tp_edit_ctx *c, tp_touched t) {
  ptrdiff_t count = (ptrdiff_t)c->lines.n;
  /* Descending, so a merge at k never moves a pair still to be checked. */
  tp_idxvec starts = {0};
  if (t.all) {
    for (ptrdiff_t k = count - 2; k >= 0; k--) tp_push(starts, k);
  } else {
    tp_push(starts, count - 2);
    for (size_t k = 0; k < t.lines.n; k++) {
      tp_push(starts, t.lines.p[k] - 1);
      tp_push(starts, t.lines.p[k]);
    }
    tl_sort_compact(&starts);
    for (size_t a = 0, b = starts.n; a + 1 < b; a++, b--) {
      ptrdiff_t x = starts.p[a];
      starts.p[a] = starts.p[b - 1];
      starts.p[b - 1] = x;
    }
  }
  for (size_t k = 0; k < starts.n; k++) tp_edit_ctx_merge_at(c, starts.p[k]);
  if (c->lines.n > 0) {
    tp_work_line *last = tp_last(c->lines);
    if (last->raw.n == 0 && tp_str_eq(last->eol, TP_EOL_NONE)) {
      c->lines.n--;
      tp_edit_ctx_count_line(c, last, -1);
    }
  }
}

/* applyOne applies one edit; false when it is invalid (R13.9.6). */
TP_PRIVATE bool tp_edit_ctx_apply_one(tp_edit_ctx *c, tp_json edit, tp_touched *out) {
  tp_str op = TP_EMPTY;
  if (edit.kind != TP_JSON_OBJECT) return false;
  (void)tp_json_get_string(edit, TP_S("op"), &op);
  if (tp_str_eq(op, TP_EDIT_OP_SET_TIME)) return tp_edit_ctx_set_time(c, edit, out);
  if (tp_str_eq(op, TP_EDIT_OP_SHIFT)) return tp_edit_ctx_shift(c, edit, out);
  if (tp_str_eq(op, TP_EDIT_OP_SORT_BY_TIME)) {
    *out = tp_edit_ctx_sort_by_time(c);
    return true;
  }
  if (tp_str_eq(op, TP_EDIT_OP_SET_REST)) return tp_edit_ctx_set_rest(c, edit, out);
  if (tp_str_eq(op, TP_EDIT_OP_INSERT)) return tp_edit_ctx_insert(c, edit, out);
  if (tp_str_eq(op, TP_EDIT_OP_DELETE)) return tp_edit_ctx_remove(c, edit, out);
  return false;
}

/* invalidLine is R13.9.6: the line of an edit-invalid warning is the edit's integer line, else
 * its integer after, else none. */
TP_PRIVATE const double *tp_invalid_line(tp_json edit) {
  static const char *const keys[2] = {"line", "after"};
  for (size_t k = 0; k < 2; k++) {
    bool present;
    tp_json v = tl_get(edit, keys[k], &present);
    double n;
    if (tp_js_integer(v, present, &n)) {
      double *p = tp_alloc(sizeof *p);
      *p = n;
      return p;
    }
  }
  return NULL;
}

/* applyEdits is R13.9 on one (already sanitized) text: apply edits (loose JSON objects) in
 * order. Never fails (R13.9.6): an invalid edit changes nothing and warns edit-invalid. */
TP_PRIVATE tp_raw_edit_result tp_apply_edits(tp_str text, const tp_json *edits, size_t count) {
  tp_raw_edit_result r;
  tp_edit_ctx *c;
  tp_tl_line_vec split;
  tp_buf b = {0};
  memset(&r, 0, sizeof r);
  if (count == 0) {
    r.text = text;
    return r;
  }
  c = tp_alloc(sizeof *c);
  split = tp_split_lines(text);
  for (size_t i = 0; i < split.n; i++) {
    tp_work_line *line = tp_new_work_line(split.p[i].raw, split.p[i].eol, false);
    tp_edit_ctx_count_line(c, line, 1);
    tp_push(c->lines, line);
  }
  for (size_t i = 0; i < count; i++) {
    size_t before = c->warnings.n;
    tp_touched t;
    memset(&t, 0, sizeof t);
    c->index = (ptrdiff_t)i;
    /* Validation fails before any mutation, so the lines need no rollback (R13.9.6). */
    if (tp_edit_ctx_apply_one(c, edits[i], &t)) {
      tp_edit_ctx_canonical(c, t);
    } else {
      c->warnings.n = before;
      tp_edit_ctx_warn(c, TP_EDIT_WARNING_CODE_EDIT_INVALID, tp_invalid_line(edits[i]));
    }
  }
  for (size_t i = 0; i < c->lines.n; i++) {
    const tp_work_line *l = c->lines.p[i];
    if (l->changed) tp_push(r.changed, (int64_t)i + 1);
    tp_buf_str(&b, l->raw);
    tp_buf_str(&b, l->eol);
  }
  r.text = tp_buf_done(&b);
  r.warnings = c->warnings;
  return r;
}
