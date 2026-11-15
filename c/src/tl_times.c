/* tl_times.c: R13.2, the tracklist time scanner (separate from R4.1), and R13.9.2 time
 * formatting (go/tl_times.go). */
#include "internal.h"

/* isGapSeparator is an R13.2.3 separator after the time. */
TP_PRIVATE bool tp_is_gap_separator(tp_rune r) {
  switch (r) {
  case '-':
  case 0x2013: /* – */
  case 0x2014: /* — */
  case '|':
  case '.':
  case ':':
    return true;
  default:
    return false;
  }
}

/* skipWhitespace is the index past the R0.2 whitespace at from. */
TP_PRIVATE ptrdiff_t tp_skip_whitespace(tp_runes s, ptrdiff_t from) {
  ptrdiff_t i = from;
  while (tp_is_whitespace(tp_at(s, i))) i++;
  return i;
}

/* isBullet is an R13.2.1 bullet (before a time, or alone on a line without one). */
TP_PRIVATE bool tp_is_bullet(tp_rune r) {
  switch (r) {
  case '-':
  case 0x2013: /* – */
  case 0x2014: /* — */
  case '*':
  case 0x2022: /* • */
    return true;
  default:
    return false;
  }
}

/* untimed is R13.1.3 / R13.2.1: a line without a time. lead holds an optional bullet: R0.2
 * whitespace, one of - – — * •, then at least one R0.2 whitespace (all of it); rest is the
 * remainder. */
TP_PRIVATE tp_time_scan tp_untimed(tp_runes raw, bool invalid) {
  ptrdiff_t start = tp_skip_whitespace(raw, 0);
  ptrdiff_t lead = 0;
  tp_time_scan s;
  if (tp_is_bullet(tp_at(raw, start)) && tp_is_whitespace(tp_at(raw, start + 1))) {
    lead = tp_skip_whitespace(raw, start + 1);
  }
  memset(&s, 0, sizeof s);
  s.parts.lead = tp_runes_str(tp_sub(raw, 0, lead));
  s.parts.time = TP_EMPTY;
  s.parts.gap = TP_EMPTY;
  s.parts.rest = tp_runes_str(tp_tail(raw, lead));
  s.invalid = invalid;
  s.text = tp_without_markers(raw, lead, TP_NORUNE);
  return s;
}

/* closingRun is R13.2.3: the index past a closing run for an opening run of open (the same
 * character, 1 or 2 long), or from when there is none. */
TP_PRIVATE ptrdiff_t tp_closing_run(tp_runes raw, ptrdiff_t from, tp_rune open) {
  if (open != TP_NORUNE && tp_at(raw, from) == open) return from + tp_emphasis_run(raw, from);
  return from;
}

/* secondsOf is whole.frac read as one decimal literal, so every port gets the same double (JS
 * Number(), correctly rounded). */
TP_PRIVATE double tp_seconds_of(ptrdiff_t whole, tp_str frac) {
  double v;
  if (frac.n == 0) return (double)whole;
  if (!tp_parse_number(tp_concat3(tp_itoa((int64_t)whole), TP_S("."), frac), &v)) {
    return (double)whole;
  }
  return v;
}

/* shapeAt is R13.2.4: the maximal digits(:digits)+ run at from. */
TP_PRIVATE bool tp_shape_at(tp_runes s, ptrdiff_t from, tp_time_shape *out) {
  ptrdiff_t na = tp_digit_run(s, from);
  tp_time_shape shape;
  if (na == 0 || tp_at(s, from + na) != ':' || !tp_is_digit(tp_at(s, from + na + 1))) {
    return false;
  }
  memset(&shape, 0, sizeof shape);
  tp_push(shape.parts, tp_sub(s, from, from + na));
  shape.end = from + na;
  while (tp_at(s, shape.end) == ':' && tp_is_digit(tp_at(s, shape.end + 1))) {
    ptrdiff_t n = tp_digit_run(s, shape.end + 1);
    tp_push(shape.parts, tp_sub(s, shape.end + 1, shape.end + 1 + n));
    shape.end += 1 + n;
  }
  *out = shape;
  return true;
}

/* shapeSeconds is the R13.2.2 validity test: the whole seconds of A:B or A:B:C (no other part
 * count). */
TP_PRIVATE bool tp_shape_seconds(tp_runes_vec parts, ptrdiff_t *out) {
  if (parts.n == 3) {
    tp_runes a = parts.p[0], b = parts.p[1], c = parts.p[2];
    int64_t m, sec;
    if (a.n == 0 || a.n > 2 || b.n != 2 || c.n != 2) return false;
    m = tp_digits_value(b);
    sec = tp_digits_value(c);
    if (m > 59 || sec > 59) return false;
    *out = (ptrdiff_t)(tp_digits_value(a) * 3600 + m * 60 + sec);
    return true;
  }
  if (parts.n == 2) {
    tp_runes a = parts.p[0], b = parts.p[1];
    int64_t sec;
    if (a.n == 0 || a.n > 3 || b.n != 2) return false;
    sec = tp_digits_value(b);
    if (sec > 59) return false;
    *out = (ptrdiff_t)(tp_digits_value(a) * 60 + sec);
    return true;
  }
  return false;
}

/* startsWithAt reports whether s has word at from. */
TP_PRIVATE bool tp_starts_with_at(tp_runes s, ptrdiff_t from, tp_str word) {
  tp_runes w = tp_runes_of(word);
  tp_runes got = tp_sub(s, from, from + (ptrdiff_t)w.n);
  if (got.n != w.n) return false;
  for (size_t k = 0; k < w.n; k++) {
    if (got.p[k] != w.p[k]) return false;
  }
  return true;
}

/* Go scanTime's closure followsTime: the end, R0.2 whitespace or a gap separator at k. */
static bool tl_follows_time(tp_runes raw, ptrdiff_t k) {
  return k >= (ptrdiff_t)raw.n || tp_is_whitespace(tp_at(raw, k)) ||
         tp_is_gap_separator(tp_at(raw, k));
}

/* scanTime is R13.2: the lead, time and gap at the start of a line; the rest is everything
 * after. */
TP_PRIVATE tp_time_scan tp_scan_time(tp_str line) {
  tp_runes raw = tp_runes_of(line);
  ptrdiff_t i, n, lead_end, whole, j, time_end;
  tp_rune outer_open = TP_NORUNE, closer = TP_NORUNE, inner_open = TP_NORUNE, next;
  tp_rune frac_sep = TP_NORUNE, open = TP_NORUNE, sep;
  tp_str frac = TP_EMPTY, time_raw;
  tp_time_shape shape;
  tp_runes first = {0}, second = {0};
  bool hours, long_minutes;
  tp_time_style *style;
  tp_tracklist_time *tt;
  tp_time_scan out;

  /* R13.2.1 lead: whitespace, an optional bullet, an optional emphasis run, an optional '[' or
   * '(', then (after a bracket) another optional emphasis run. A run counts only when the next
   * character is a bracket (first run) or a digit. */
  i = tp_skip_whitespace(raw, 0);
  if (tp_is_bullet(tp_at(raw, i)) && tp_is_whitespace(tp_at(raw, i + 1))) {
    i = tp_skip_whitespace(raw, i + 1);
  }
  n = tp_emphasis_run(raw, i);
  next = tp_at(raw, i + n);
  if (n > 0 && (next == '[' || next == '(' || tp_is_digit(next))) {
    outer_open = tp_at(raw, i);
    i += n;
  }
  switch (tp_at(raw, i)) {
  case '[':
    closer = ']';
    break;
  case '(':
    closer = ')';
    break;
  default:
    break;
  }
  if (closer != TP_NORUNE) {
    i++;
    n = tp_emphasis_run(raw, i);
    if (n > 0 && tp_is_digit(tp_at(raw, i + n))) {
      inner_open = tp_at(raw, i);
      i += n;
    }
  }
  lead_end = i;

  /* R13.2.2 time */
  if (!tp_shape_at(raw, i, &shape)) return tp_untimed(raw, false);
  if (!tp_shape_seconds(shape.parts, &whole)) return tp_untimed(raw, true); /* R13.2.4 */
  j = shape.end;
  sep = tp_at(raw, j);
  if (sep == '.' || sep == ',') {
    ptrdiff_t fn = tp_digit_run(raw, j + 1);
    if (fn >= 1 && fn <= 3) {
      frac_sep = sep;
      frac = tp_runes_str(tp_sub(raw, j + 1, j + 1 + fn));
      j += 1 + fn;
    }
  }
  time_end = j;

  /* R13.2.3 gap: closing runs for the lead's runs (the same character), around the closing
   * bracket. R13.12.4: a lead run the gap leaves open may pair with a run at the end of the
   * line. */
  if (closer != TP_NORUNE) {
    ptrdiff_t k = tp_closing_run(raw, j, inner_open);
    if (k == j) open = inner_open;
    j = k;
    if (tp_at(raw, j) != closer) return tp_untimed(raw, false);
    j++;
    k = tp_closing_run(raw, j, outer_open);
    if (k > j && tl_follows_time(raw, k)) {
      j = k;
    } else if (outer_open != TP_NORUNE) {
      open = outer_open;
    }
  } else {
    ptrdiff_t k = tp_closing_run(raw, j, outer_open);
    if (k == j) open = outer_open;
    j = k;
    if (!tl_follows_time(raw, j)) return tp_untimed(raw, false);
  }
  j = tp_skip_whitespace(raw, j);
  if (tp_is_gap_separator(tp_at(raw, j))) j = tp_skip_whitespace(raw, j + 1);
  if (tp_starts_with_at(raw, j, TP_S("==>"))) {
    j = tp_skip_whitespace(raw, j + 3);
  } else if (tp_starts_with_at(raw, j, TP_S("=>"))) {
    j = tp_skip_whitespace(raw, j + 2);
  }

  time_raw = tp_runes_str(tp_sub(raw, lead_end, time_end));
  hours = shape.parts.n == 3;
  if (shape.parts.n > 0) first = shape.parts.p[0];
  if (shape.parts.n > 1) second = shape.parts.p[1];
  /* R13.9.2 long-minute style: two parts with 60 minutes or more. */
  long_minutes = !hours && tp_digits_value(first) >= 60;
  style = tp_alloc(sizeof *style);
  style->hours = hours;
  style->long_minutes = long_minutes;
  style->frac_sep = frac_sep;
  style->frac_digits = (ptrdiff_t)frac.n; /* ASCII digits: bytes are code points */
  if (hours) {
    style->hour_width = (ptrdiff_t)first.n;
    style->minute_width = (ptrdiff_t)second.n;
  } else if (long_minutes) {
    /* R13.9.2: a long-minute time's minimum minute width is min(width, 2), so it never keeps 3
     * minute digits once it is short again (125:30 minus 7000 s gives 08:50). */
    style->minute_width = first.n < 2 ? (ptrdiff_t)first.n : 2;
  } else {
    style->minute_width = (ptrdiff_t)first.n;
  }
  tt = tp_alloc(sizeof *tt);
  tt->raw = time_raw;
  tt->seconds = tp_seconds_of(whole, frac);

  memset(&out, 0, sizeof out);
  out.parts.lead = tp_runes_str(tp_sub(raw, 0, lead_end));
  out.parts.time = time_raw;
  out.parts.gap = tp_runes_str(tp_sub(raw, time_end, j));
  out.parts.rest = tp_runes_str(tp_tail(raw, j));
  out.time = tt;
  out.style = style;
  out.text = tp_without_markers(raw, j, open);
  return out;
}

/* pad writes n with at least width digits. */
TP_PRIVATE tp_str tp_pad(int64_t n, ptrdiff_t width) {
  tp_str s = tp_itoa(n);
  if ((ptrdiff_t)s.n >= width) return s;
  return tp_concat(tp_repeat(TP_S("0"), (size_t)(width - (ptrdiff_t)s.n)), s);
}

/* Joins a, ":", b (and ":", c when c is non-NULL), then frac. */
static tp_str tl_join_time(tp_str a, tp_str b, const tp_str *c, tp_str frac) {
  tp_buf buf = {0};
  tp_buf_str(&buf, a);
  tp_buf_byte(&buf, ':');
  tp_buf_str(&buf, b);
  if (c) {
    tp_buf_byte(&buf, ':');
    tp_buf_str(&buf, *c);
  }
  tp_buf_str(&buf, frac);
  return tp_buf_done(&buf);
}

/* formatTime is R13.9.2: seconds (finite, not negative) written in style. Values are rounded
 * half up to the fraction precision (floor(v * 10^d + 0.5), never a library round, and with no
 * FMA: tp_half_up_units). A value of an hour or more without hours is upgraded to HH:MM:SS
 * (H:MM:SS when one_digit_hours), except in long-minute style. false when the value cannot be
 * written as a valid R13.2.2 time (100 hours or more, more than 999 long-style minutes). */
TP_PRIVATE bool tp_format_time(double seconds, const tp_time_style *style, bool one_digit_hours,
                               tp_formatted_time *out) {
  double scale = 1000.0, units, abs_units, whole_f;
  volatile double whole_scaled;
  int64_t frac_units, whole, sec;
  tp_str frac = TP_EMPTY;
  switch (style->frac_digits) {
  case 0:
    scale = 1;
    break;
  case 1:
    scale = 10;
    break;
  case 2:
    scale = 100;
    break;
  default:
    break;
  }
  /* The same double arithmetic as the JS reference; integers only after the range check. */
  units = tp_half_up_units(seconds, scale);
  abs_units = units < 0 ? -units : units;
  if (tp_is_nan(units) || tp_is_inf(units) || abs_units > TP_MAX_SAFE_INTEGER || units < 0) {
    return false;
  }
  whole_f = tp_floor(units / scale);
  whole_scaled = whole_f * scale; /* rounded before the subtraction (no FMA) */
  frac_units = (int64_t)(units - whole_scaled);
  /* The written time must stay valid under R13.2.2 (hours are 1 or 2 digits). */
  if (whole_f >= 100.0 * 3600.0) return false;
  whole = (int64_t)whole_f;
  if (style->frac_sep != TP_NORUNE && style->frac_digits > 0) {
    tp_buf b = {0};
    tp_buf_rune(&b, style->frac_sep);
    tp_buf_str(&b, tp_pad(frac_units, style->frac_digits));
    frac = tp_buf_done(&b);
  }
  sec = whole % 60;
  memset(out, 0, sizeof *out);
  if (style->hours) {
    int64_t h = whole / 3600, m = (whole % 3600) / 60;
    tp_str s = tp_pad(sec, 2);
    out->raw = tl_join_time(tp_pad(h, style->hour_width), tp_pad(m, style->minute_width), &s, frac);
    return true;
  }
  if (style->long_minutes) {
    int64_t m = whole / 60;
    if (m > 999) return false; /* R13.2.2: two-part minutes are 1 to 3 digits */
    out->raw = tl_join_time(tp_pad(m, style->minute_width), tp_pad(sec, 2), NULL, frac);
    return true;
  }
  if (whole >= 3600) {
    int64_t h = whole / 3600, m = (whole % 3600) / 60;
    tp_str s = tp_pad(sec, 2);
    out->raw = tl_join_time(tp_pad(h, one_digit_hours ? 1 : 2), tp_pad(m, 2), &s, frac);
    out->upgraded = true;
    return true;
  }
  out->raw = tl_join_time(tp_pad(whole / 60, style->minute_width), tp_pad(sec, 2), NULL, frac);
  return true;
}
