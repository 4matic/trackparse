/* tl_warnings.c: R13.7, tracklist warnings and the time-out-of-order suggestion search
 * (go/tl_warnings.go). */
#include "internal.h"

/* valueWith is the value with changes applied, when it is a valid time inside the bounds. */
TP_PRIVATE bool tp_time_digits_value_with(const tp_time_digits *d, tp_digit_change_vec changes,
                                          double *out) {
  /* A time has at most three fields; the copy stays on the stack (the search calls this up to
   * about 1,800 times per line). */
  int64_t buf[4];
  int64_t *v = buf;
  size_t nv = d->values.n;
  int64_t mult[3] = {60, 1, 0};
  int64_t whole = 0;
  double t;
  if (nv > TP_COUNT(buf)) v = tp_alloc_array(nv, sizeof *v);
  if (nv > 0) memcpy(v, d->values.p, nv * sizeof *v);
  for (size_t k = 0; k < changes.n; k++) {
    const tp_digit_change *c = &changes.p[k];
    if (c->slot.field >= 0 && c->slot.field < (ptrdiff_t)nv) {
      v[c->slot.field] += (c->digit - c->slot.digit) * c->slot.place;
    }
  }
  if (nv == 3) {
    mult[0] = 3600;
    mult[1] = 60;
    mult[2] = 1;
  }
  for (size_t f = 0; f < nv; f++) {
    int64_t m = 0;
    if (f > 0 && v[f] > 59) return false;
    if (f < TP_COUNT(mult)) m = mult[f];
    whole += v[f] * m;
  }
  if (whole < 0 || whole > (((int64_t)1 << 32) - 1)) return false;
  t = tp_seconds_of((ptrdiff_t)whole, d->frac);
  if ((d->lo == NULL || *d->lo < t) && (d->hi == NULL || t < *d->hi)) {
    *out = t;
    return true;
  }
  return false;
}

/* render is the raw time with changes applied. */
TP_PRIVATE tp_str tp_time_digits_render(const tp_time_digits *d, tp_digit_change_vec changes) {
  tp_runes c = tp_runes_clone(d->raw);
  tp_rune *out = (tp_rune *)(uintptr_t)c.p; /* our own copy */
  for (size_t k = 0; k < changes.n; k++) {
    const tp_digit_change *ch = &changes.p[k];
    if (ch->slot.at >= 0 && ch->slot.at < (ptrdiff_t)c.n && ch->digit >= 0 && ch->digit <= 9) {
      out[ch->slot.at] = (tp_rune)('0' + ch->digit);
    }
  }
  return tp_runes_str(c);
}

/* searchTime is the R13.7 suggestion search on one raw time: change 1, then 2 digits (fraction
 * digits excluded); keep the candidates that are valid times (R13.2.2: fields after the first at
 * most 59) with lo < t (when lo exists) and t < hi (when hi exists). Only the smallest number of
 * changes with any candidate counts. Bounded and arithmetic: at most 7 digits, 9 * 7 + 81 * 21
 * candidates. */
TP_PRIVATE tp_search_outcome tp_search_time(tp_str raw_time, const double *lo, const double *hi,
                                            tp_tracklist_time *found_time) {
  tp_runes raw = tp_runes_of(raw_time);
  size_t frac_start = raw.n;
  tp_str frac;
  tp_intvec values = {0};
  tp_digit_slot_vec slots = {0};
  int64_t place = 1;
  ptrdiff_t field = -1;
  tp_time_digits digits;
  tp_digit_change found[2];
  size_t found_n = 0; /* Go found == nil when 0 */
  ptrdiff_t count = 0;
  double dummy;

  for (size_t i = 0; i < raw.n; i++) {
    if (raw.p[i] == '.' || raw.p[i] == ',') {
      frac_start = i;
      break;
    }
  }
  frac = tp_runes_str(tp_tail(raw, (ptrdiff_t)frac_start + 1));
  tp_push(values, 0);
  for (size_t i = 0; i < frac_start; i++) {
    tp_rune ch = raw.p[i];
    if (ch == ':') {
      tp_push(values, 0);
    } else if (tp_is_digit(ch)) {
      int64_t d = (int64_t)(ch - '0');
      size_t f = values.n - 1;
      tp_digit_slot s;
      values.p[f] = values.p[f] * 10 + d;
      s.at = (ptrdiff_t)i;
      s.field = (ptrdiff_t)f;
      s.place = 0;
      s.digit = d;
      tp_push(slots, s);
    }
  }
  /* The place value of each digit inside its field. */
  for (size_t k = slots.n; k-- > 0;) {
    if (slots.p[k].field != field) {
      field = slots.p[k].field;
      place = 1;
    }
    slots.p[k].place = place;
    place *= 10;
  }
  digits.raw = raw;
  digits.frac = frac;
  digits.values = values;
  digits.lo = lo;
  digits.hi = hi;

  for (size_t x = 0; x < slots.n; x++) {
    for (int64_t d = 0; d <= 9; d++) {
      tp_digit_change change[1];
      tp_digit_change_vec cv;
      if (d == slots.p[x].digit) continue;
      change[0].slot = slots.p[x];
      change[0].digit = d;
      cv.p = change;
      cv.n = cv.cap = 1;
      if (tp_time_digits_value_with(&digits, cv, &dummy)) {
        count++;
        found[0] = change[0];
        found_n = 1;
      }
    }
  }
  if (count == 0) {
    for (size_t x = 0; x < slots.n && count < 2; x++) {
      for (size_t y = x + 1; y < slots.n && count < 2; y++) {
        tp_digit_slot a = slots.p[x], b = slots.p[y];
        for (int64_t da = 0; da <= 9 && count < 2; da++) {
          if (da == a.digit) continue;
          for (int64_t db = 0; db <= 9; db++) {
            tp_digit_change change[2];
            tp_digit_change_vec cv;
            if (db == b.digit) continue;
            change[0].slot = a;
            change[0].digit = da;
            change[1].slot = b;
            change[1].digit = db;
            cv.p = change;
            cv.n = cv.cap = 2;
            if (tp_time_digits_value_with(&digits, cv, &dummy)) {
              count++;
              found[0] = change[0];
              found[1] = change[1];
              found_n = 2;
              if (count >= 2) break; /* Go: break pairs (the outer loops test count < 2) */
            }
          }
        }
      }
    }
  }
  if (found_n > 0 && count == 1) {
    tp_digit_change_vec fv;
    double seconds;
    fv.p = found;
    fv.n = fv.cap = found_n;
    if (!tp_time_digits_value_with(&digits, fv, &seconds)) return TP_SEARCH_NONE;
    found_time->raw = tp_time_digits_render(&digits, fv);
    found_time->seconds = seconds;
    return TP_SEARCH_ONE;
  }
  if (found_n > 0 && count > 1) return TP_SEARCH_MANY;
  return TP_SEARCH_NONE;
}

/* suggestFix is the R13.7 suggestion for the out-of-order timed entry k (an index into times):
 * search its own time between its neighbours first; only when that finds no candidate at all,
 * search the previous timed entry's time between the one before it and entry k. */
TP_PRIVATE const tp_suggestion *tp_suggest_fix(tp_timed_entry_vec times, ptrdiff_t k) {
  tp_timed_entry cur, prev;
  const double *next = NULL, *lo = NULL;
  tp_tracklist_time t;
  tp_search_outcome outcome;
  tp_suggestion *s;
  if (k < 1 || k >= (ptrdiff_t)times.n) return NULL;
  cur = times.p[k];
  prev = times.p[k - 1];
  if (k + 1 < (ptrdiff_t)times.n) next = &times.p[k + 1].time->seconds;
  outcome = tp_search_time(cur.time->raw, &prev.time->seconds, next, &t);
  if (outcome == TP_SEARCH_ONE) {
    s = tp_alloc(sizeof *s);
    s->line = cur.line;
    s->raw = t.raw;
    s->seconds = t.seconds;
    return s;
  }
  if (outcome == TP_SEARCH_MANY) return NULL;
  if (k >= 2) lo = &times.p[k - 2].time->seconds;
  if (tp_search_time(prev.time->raw, lo, &cur.time->seconds, &t) == TP_SEARCH_ONE) {
    s = tp_alloc(sizeof *s);
    s->line = prev.line;
    s->raw = t.raw;
    s->seconds = t.seconds;
    return s;
  }
  return NULL;
}

/* warningRank is the table order of R13.7; the warnings of one line are emitted in this order.
 * It is a chain of comparisons, not a lookup in tp_all_warning_codes, so no caller can change
 * the order. */
TP_PRIVATE ptrdiff_t tp_warning_rank(tp_warning_code code) {
  if (tp_str_eq(code, TP_WARNING_CODE_TIME_MISSING)) return 0;
  if (tp_str_eq(code, TP_WARNING_CODE_TIME_OUT_OF_ORDER)) return 1;
  if (tp_str_eq(code, TP_WARNING_CODE_TIME_DUPLICATE)) return 2;
  if (tp_str_eq(code, TP_WARNING_CODE_NO_START_AT_ZERO)) return 3;
  if (tp_str_eq(code, TP_WARNING_CODE_TIME_AFTER_AUDIO_END)) return 4;
  if (tp_str_eq(code, TP_WARNING_CODE_INVALID_TIME)) return 5;
  if (tp_str_eq(code, TP_WARNING_CODE_POSITION_GAP)) return 6;
  if (tp_str_eq(code, TP_WARNING_CODE_POSITION_DUPLICATE)) return 7;
  if (tp_str_eq(code, TP_WARNING_CODE_LINE_UNPARSED)) return 8;
  return 9;
}

/* flagAt is list[i], false out of range. */
TP_PRIVATE bool tp_flag_at(tp_bool_vec list, ptrdiff_t i) {
  return i >= 0 && i < (ptrdiff_t)list.n && list.p[i];
}

typedef TP_VEC(tp_tracklist_warning_vec) tl_warning_lines;

/* Go tracklistWarnings' closure add. */
static void tl_add(tl_warning_lines *per_line, const tp_entry_vec *entries, ptrdiff_t i,
                   tp_warning_code code, const tp_suggestion *suggestion) {
  tp_tracklist_warning w;
  if (i < 0 || i >= (ptrdiff_t)entries->n) return;
  w.code = code;
  w.line = entries->p[i].line;
  w.severity =
      tp_str_eq(code, TP_WARNING_CODE_NO_START_AT_ZERO) ? TP_SEVERITY_INFO : TP_SEVERITY_WARNING;
  w.suggestion = suggestion;
  tp_push(per_line->p[i], w);
}

static int tl_rank_cmp(const void *a, const void *b, void *ctx) {
  const tp_tracklist_warning *x = a, *y = b;
  ptrdiff_t d = tp_warning_rank(x->code) - tp_warning_rank(y->code);
  (void)ctx;
  return d < 0 ? -1 : d > 0 ? 1 : 0;
}

/* tracklistWarnings is R13.7. */
TP_PRIVATE tp_tracklist_warning_vec tp_tracklist_warnings(const tp_warning_input *in) {
  const tp_entry_vec *entries = &in->entries;
  ptrdiff_t n = (ptrdiff_t)entries->n;
  tl_warning_lines per_line = {0};
  tp_idxvec timed_idx = {0};
  tp_timed_entry_vec times = {0};
  const double *prev = NULL;
  ptrdiff_t prev_pos = -1;
  tp_tracklist_warning_vec out = {0};

  if (n > 0) (void)tp_push_n(per_line, entries->n);
  for (ptrdiff_t i = 0; i < n; i++) {
    const tp_tracklist_time *t = entries->p[i].time;
    if (t) {
      tp_timed_entry te;
      te.line = entries->p[i].line;
      te.time = t;
      tp_push(timed_idx, i);
      tp_push(times, te);
    }
  }

  for (ptrdiff_t i = 0; i < n; i++) {
    const tp_entry *e = &entries->p[i];
    /* R13.13.3: an untimed layered line plays over the previous track, at its time. */
    if (tp_str_eq(e->kind, TP_ENTRY_KIND_TRACK) && e->time == NULL && !e->layered && times.n > 0) {
      tl_add(&per_line, entries, i, TP_WARNING_CODE_TIME_MISSING, NULL);
    }
    if (tp_flag_at(in->invalid, i))
      tl_add(&per_line, entries, i, TP_WARNING_CODE_INVALID_TIME, NULL);
    if (tp_flag_at(in->unparsed, i)) {
      tl_add(&per_line, entries, i, TP_WARNING_CODE_LINE_UNPARSED, NULL);
    }
  }

  for (size_t k = 0; k < timed_idx.n; k++) {
    ptrdiff_t i = timed_idx.p[k];
    double t = times.p[k].time->seconds;
    if (prev == NULL) {
      if (t != 0) tl_add(&per_line, entries, i, TP_WARNING_CODE_NO_START_AT_ZERO, NULL);
    } else if (t < *prev) {
      tl_add(&per_line, entries, i, TP_WARNING_CODE_TIME_OUT_OF_ORDER,
             tp_suggest_fix(times, (ptrdiff_t)k));
    } else if (t == *prev) {
      tl_add(&per_line, entries, i, TP_WARNING_CODE_TIME_DUPLICATE, NULL);
    }
    if (in->audio_seconds != NULL && t > *in->audio_seconds) {
      tl_add(&per_line, entries, i, TP_WARNING_CODE_TIME_AFTER_AUDIO_END, NULL);
    }
    prev = &times.p[k].time->seconds;
  }

  for (ptrdiff_t i = 0; i < n; i++) {
    ptrdiff_t pos;
    if (!tp_str_eq(entries->p[i].kind, TP_ENTRY_KIND_TRACK) || i >= (ptrdiff_t)in->positions.n ||
        in->positions.p[i] < 0) {
      continue;
    }
    pos = in->positions.p[i];
    if (prev_pos >= 0) {
      if (pos == prev_pos) {
        tl_add(&per_line, entries, i, TP_WARNING_CODE_POSITION_DUPLICATE, NULL);
      } else if (prev_pos + 1 != pos) {
        tl_add(&per_line, entries, i, TP_WARNING_CODE_POSITION_GAP, NULL);
      }
    }
    prev_pos = pos;
  }

  for (size_t i = 0; i < per_line.n; i++) {
    tp_tracklist_warning_vec ws = per_line.p[i];
    if (ws.n > 1) tp_sort_stable(ws.p, ws.n, sizeof *ws.p, tl_rank_cmp, NULL);
    tp_append(out, ws);
  }
  return out;
}
