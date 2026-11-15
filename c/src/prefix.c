/* prefix.c: R4 leading timestamp and track-position prefixes (go/prefix.go, js/src/prefix.ts). */
#include "internal.h"

/* matchTime is H:MM:SS, HH:MM:SS, M:SS or MM:SS starting at from (R4.1). */
TP_PRIVATE bool tp_match_time(tp_runes s, ptrdiff_t from, tp_time_match *out) {
  ptrdiff_t lead = tp_digit_run(s, from);
  if (lead < 1 || lead > 2 || tp_at(s, from + lead) != ':') return false;
  ptrdiff_t first = (ptrdiff_t)tp_digits_value(tp_sub(s, from, from + lead));
  int mid;
  if (!tp_two_digits_up_to_59(s, from + lead + 1, &mid)) return false;
  ptrdiff_t after_mid = from + lead + 3;
  if (tp_at(s, after_mid) == ':') {
    int sec;
    if (tp_two_digits_up_to_59(s, after_mid + 1, &sec)) {
      ptrdiff_t end = after_mid + 3;
      out->raw = tp_runes_str(tp_sub(s, from, end));
      out->seconds = first * 3600 + (ptrdiff_t)mid * 60 + (ptrdiff_t)sec;
      out->end = end;
      return true;
    }
  }
  if (lead == 2 && first > 59) return false; /* MM is 00-59 */
  out->raw = tp_runes_str(tp_sub(s, from, after_mid));
  out->seconds = first * 60 + (ptrdiff_t)mid;
  out->end = after_mid;
  return true;
}

/* skipSpaceAndDash skips, after a prefix, one space and then an optional "- " (spaced dash). */
TP_PRIVATE ptrdiff_t tp_skip_space_and_dash(tp_runes s, ptrdiff_t from) {
  ptrdiff_t i = from;
  if (tp_at(s, i) == ' ') i++;
  if (tp_at(s, i) == '-' && tp_at(s, i + 1) == ' ') i += 2;
  return i;
}

/* matchTimestamp is R4.1: the timestamp and the index where the rest starts. */
TP_PRIVATE bool tp_match_timestamp(tp_runes s, tp_timestamp *ts, ptrdiff_t *rest) {
  tp_rune closer = TP_NORUNE;
  switch (tp_at(s, 0)) {
  case '[':
    closer = ']';
    break;
  case '(':
    closer = ')';
    break;
  default:
    break;
  }
  ptrdiff_t from = closer != TP_NORUNE ? 1 : 0;
  tp_time_match t;
  if (!tp_match_time(s, from, &t)) return false;
  ptrdiff_t end = t.end;
  if (closer != TP_NORUNE) {
    if (tp_at(s, end) != closer) return false;
    end++;
  }
  if (end < (ptrdiff_t)s.n && tp_at(s, end) != ' ') return false;
  ts->raw = t.raw;
  ts->seconds = t.seconds;
  *rest = tp_skip_space_and_dash(s, end);
  return true;
}

/* containsSpacedDash reports whether the skeleton of s has a spaced dash. */
TP_PRIVATE bool tp_contains_spaced_dash(tp_runes s) {
  bool unbalanced;
  tp_skel sk = tp_scan(s, 0, &unbalanced);
  return tp_has_spaced_dash(sk.text);
}

/* matchPosition is R4.2: the end index of the position prefix. Forms are tried (a), (b), (c),
 * then (d) and (e). */
TP_PRIVATE bool tp_match_position(tp_runes s, tp_mode mode, ptrdiff_t *end) {
  ptrdiff_t n = tp_digit_run(s, 0);
  if (n < 1 || n > 3) return false;
  tp_rune c = tp_at(s, n);
  bool zero_padded = tp_at(s, 0) == '0' && n >= 2;
  bool filename = tp_str_eq(mode, TP_MODE_FILENAME);
  /* (a) "01. ", "1) " */
  if ((c == '.' || c == ')') && (tp_at(s, n + 1) == ' ' || n + 1 == (ptrdiff_t)s.n)) {
    *end = tp_skip_space_and_dash(s, n + 1);
    return true;
  }
  /* (b) "01 ": a position in every mode. */
  if (zero_padded && c == ' ') {
    *end = tp_skip_space_and_dash(s, n);
    return true;
  }
  /* (c) "3 - Artist - Title"; in filename mode "N - " is always a position ("311 - Amber.mp3"). */
  if (c == ' ' && tp_at(s, n + 1) == '-' && tp_at(s, n + 2) == ' ') {
    if (filename || tp_contains_spaced_dash(tp_tail(s, n + 3))) {
      *end = n + 3;
      return true;
    }
    return false;
  }
  if (filename) {
    /* (e) "01-Track", "03_name", "01.Track" */
    if ((c == '-' || c == '_' || c == '.') && tp_at(s, n + 1) != TP_NORUNE &&
        tp_at(s, n + 1) != ' ') {
      *end = n + 1;
      return true;
    }
    /* (e) "3 My Song.mp3": only when the remainder has no spaced dash. */
    if (c == ' ' && !tp_contains_spaced_dash(tp_tail(s, n + 1))) {
      *end = tp_skip_space_and_dash(s, n);
      return true;
    }
  }
  /* (d) "2 Unlimited", "50 Cent": never a position outside filename mode. */
  return false;
}

/* stripPrefixes is R4 on text, whose first code point sits at absolute offset base. */
TP_PRIVATE tp_prefix_result tp_strip_prefixes(tp_runes text, ptrdiff_t base, tp_mode mode,
                                              tp_ctx *c) {
  tp_runes rest = text;
  ptrdiff_t offset = base;
  const tp_timestamp *timestamp = NULL;
  const tp_position *position = NULL;

  tp_timestamp ts;
  ptrdiff_t end;
  if (tp_match_timestamp(rest, &ts, &end)) {
    tp_timestamp *p = tp_alloc(sizeof *p);
    *p = ts;
    timestamp = p;
    offset += tp_utf16_len(tp_sub(rest, 0, end));
    rest = tp_tail(rest, end);
  }
  /* R7.3a: a known artist that starts with digits is not a position. */
  if (tp_match_known_at(rest, 0, c, '-') == 0) {
    if (tp_match_position(rest, mode, &end) && end < (ptrdiff_t)rest.n) {
      tp_runes digits = tp_sub(rest, 0, tp_digit_run(rest, 0));
      tp_position *p = tp_alloc(sizeof *p);
      p->raw = tp_runes_str(digits);
      p->number = (ptrdiff_t)tp_digits_value(digits);
      position = p;
      offset += tp_utf16_len(tp_sub(rest, 0, end));
      rest = tp_tail(rest, end);
    }
  }
  tp_prefix_result r;
  r.rest = rest;
  r.base = offset;
  r.timestamp = timestamp;
  r.position = position;
  return r;
}
