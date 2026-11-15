/* credits.c: R7.3 to R7.5, splitting a credit list into names (go/credits.go). */
#include "internal.h"

static const tp_str and_like[] = {{"&", 1}, {"and", 3}, {"+", 1}};

/* wordAt is the end of the word that starts at start. */
TP_PRIVATE ptrdiff_t tp_word_at(tp_runes text, ptrdiff_t start) {
  ptrdiff_t end = start;
  while (end < (ptrdiff_t)text.n && !tp_is_whitespace(text.p[end])) end++;
  return end;
}

/* nextWord is the word after a joiner; a tight joiner (, ;) ends it, since the name ends there. */
TP_PRIVATE tp_str tp_next_word(tp_runes text, ptrdiff_t from) {
  ptrdiff_t n = (ptrdiff_t)text.n;
  ptrdiff_t a = from, b;
  while (a < n && tp_is_whitespace(text.p[a])) a++;
  b = a;
  while (b < n && !tp_is_whitespace(text.p[b]) && text.p[b] != ',' && text.p[b] != ';') b++;
  return tp_runes_str(tp_sub(text, a, b));
}

/* findJoiner is R7.3b: the leftmost joiner occurrence at or after from, honouring the guards. */
TP_PRIVATE bool tp_find_joiner(tp_runes text, ptrdiff_t from, bool split_at_comma,
                               const tp_list_options *opts, tp_ctx *c, tp_joiner_hit *out) {
  ptrdiff_t n = (ptrdiff_t)text.n;
  for (ptrdiff_t p = from > 0 ? from : 0; p < n; p++) {
    tp_rune ch = text.p[p];
    ptrdiff_t end;
    tp_str word;
    if (ch == ',' || ch == ';') {
      if (ch == ',' && tp_is_digit(tp_before(text, p)) && tp_is_digit(tp_at(text, p + 1))) {
        continue; /* 1,000 */
      }
      out->start = p;
      out->end = p + 1;
      out->raw = tp_runes_str(tp_sub(text, p, p + 1));
      return true;
    }
    if (p == 0 || tp_before(text, p) != ' ' || tp_is_whitespace(ch)) continue;
    end = tp_word_at(text, p);
    if (end >= n || text.p[end] != ' ') continue; /* spaced joiners need a space after */
    word = tp_runes_str(tp_sub(text, p, end));
    if (tp_is_spaced_joiner(word, text, end, split_at_comma, opts, c)) {
      out->start = p;
      out->end = end;
      out->raw = word;
      return true;
    }
  }
  return false;
}

TP_PRIVATE bool tp_is_spaced_joiner(tp_str word, tp_runes text, ptrdiff_t end, bool split_at_comma,
                                    const tp_list_options *opts, tp_ctx *c) {
  if (opts->feat_markers_join) {
    tp_strvec one;
    tp_phrase_match m;
    one.p = &word;
    one.n = 1;
    one.cap = 1;
    if (tp_phrase_table_match_at(c->t->feat_markers, one, 0, &m)) return true;
  }
  for (size_t i = 0; i < c->t->spaced_joiners.n; i++) {
    const tp_spaced_joiner *j = &c->t->spaced_joiners.p[i];
    bool matches = j->case_sensitive ? tp_str_eq(word, j->raw) : tp_word_matches(word, j->raw);
    if (!matches) continue;
    if (tp_str_eq(j->raw, TP_S("with")) && !opts->split_with) return false;
    if (j->is_and) {
      if (c->split_and == TP_SPLIT_NEVER) return false;
      if (c->split_and == TP_SPLIT_AUTO && !split_at_comma) return false;
    }
    if (tp_contains_string(tp_data_strs(and_like, TP_COUNT(and_like)), j->raw) &&
        tp_in_vocab(tp_next_word(text, end), &c->t->no_split_before)) {
      return false;
    }
    return true;
  }
  return false;
}

/* cleanName is R7.4: clean one raw name. */
TP_PRIVATE tp_str tp_clean_name(tp_str raw) {
  tp_runes name = tp_runes_of(tp_collapse_spaces(raw));
  tp_str s;
  ptrdiff_t space;
  size_t n = name.n;
  if (n >= 2) {
    tp_rune first = name.p[0];
    if ((first == '"' || first == '\'') && name.p[n - 1] == first) {
      name = tp_runes_of(tp_collapse_runes(tp_runes_slice(name, 1, n - 1)));
    }
  }
  s = tp_runes_str(name);
  while (tp_has_suffix(s, TP_S(",")) || tp_has_suffix(s, TP_S(";"))) {
    s = tp_js_trim_end(tp_str_slice(s, 0, s.n - 1));
  }
  space = tp_index_byte(s, ' ');
  if (space > 0 && tp_str_eq(tp_word_key(tp_str_slice(s, 0, (size_t)space)), TP_S("by"))) {
    s = tp_str_slice(s, (size_t)space + 1, s.n);
  }
  return tp_collapse_spaces(s);
}

/* unwrapList is R7.4: strip one pair of wrapping quotes from the whole list (no other such quote
 * inside). */
TP_PRIVATE tp_skel tp_unwrap_list(const tp_skel *list) {
  tp_skel t = tp_trim_skel(list);
  ptrdiff_t n = (ptrdiff_t)t.text.n;
  tp_rune q;
  ptrdiff_t next = -1;
  tp_skel inner;
  if (n < 2) return t;
  q = t.text.p[0];
  if ((q != '"' && q != '\'') || t.text.p[n - 1] != q) return t;
  /* indexOf(q, 1): the next such quote must be the last character. */
  for (ptrdiff_t i = 1; i < n; i++) {
    if (t.text.p[i] == q) {
      next = i;
      break;
    }
  }
  if (next != n - 1) return t;
  inner = tp_slice_skel(&t, 1, n - 1);
  return tp_trim_skel(&inner);
}

/* splitCredits is R7.3: split input into credits. */
TP_PRIVATE tp_list_result tp_split_credits(const tp_skel *input, const tp_list_options *opts,
                                           tp_ctx *c) {
  tp_skel list = tp_unwrap_list(input); /* R7.4: "Camo & Krooked" */
  tp_runes text = list.text;
  ptrdiff_t n = (ptrdiff_t)text.n;
  tp_raw_name_vec raw = {0};
  const tp_str *joiner = opts->lead;
  bool split_at_comma = false;
  ptrdiff_t start = 0;
  while (start <= n) {
    ptrdiff_t known, end, pos;
    tp_joiner_hit hit = {0};
    bool found;
    tp_skel piece;
    tp_raw_name r;
    tp_str *j;
    while (start < n && tp_is_whitespace(text.p[start])) start++;
    known = tp_match_known_at(text, start, c, '-'); /* R7.3a */
    found = tp_find_joiner(text, start + known, split_at_comma, opts, c, &hit);
    end = found ? hit.start : n;
    piece = tp_slice_skel(&list, start, end);
    pos = 0;
    if (start < (ptrdiff_t)list.pos.n) {
      pos = list.pos.p[start];
    } else if (list.pos.n > 0) {
      pos = tp_last(list.pos);
    }
    r.name = tp_clean_name(tp_render_skel(&piece));
    r.joiner = joiner;
    r.pos = pos;
    tp_push(raw, r);
    joiner = NULL;
    if (!found) break;
    if (tp_str_eq(hit.raw, TP_S(",")) || tp_str_eq(hit.raw, TP_S(";"))) {
      split_at_comma = true; /* R7.3: and after a tight joiner */
    }
    start = hit.end;
    j = tp_alloc(sizeof *j);
    *j = hit.raw;
    joiner = j;
  }
  return tp_finish_names(raw, opts, c);
}

/* finishNames is the R7.4 empty drops and the R7.5 year/unknown drops. R7.6: the first survivor
 * takes the lead joiner (NULL, or the feat/producer marker). */
TP_PRIVATE tp_list_result tp_finish_names(tp_raw_name_vec raw, const tp_list_options *opts,
                                          tp_ctx *c) {
  tp_raw_name_vec named = {0}, kept = {0};
  size_t count;
  tp_list_result out;
  ptrdiff_t list;
  memset(&out, 0, sizeof out);
  for (size_t i = 0; i < raw.n; i++) {
    if (raw.p[i].name.n != 0) tp_push(named, raw.p[i]);
  }
  count = named.n;
  for (size_t i = 0; i < named.n; i++) {
    tp_raw_name r = named.p[i];
    if (tp_is_unknown_token(r.name, c->t)) {
      out.unknown = true;
      continue;
    }
    if (tp_is_all_digits(r.name) && count > 1) {
      int year;
      if (tp_year_of(r.name, &year)) {
        tp_positioned_year y;
        y.year = (ptrdiff_t)year;
        y.pos = r.pos;
        tp_push(out.years, y);
        continue;
      }
    }
    tp_push(kept, r);
  }
  list = c->list_seq;
  c->list_seq++;
  for (size_t i = 0; i < kept.n; i++) {
    tp_credit cr;
    cr.name = kept.p[i].name;
    cr.role = opts->role;
    cr.joiner = i == 0 ? opts->lead : kept.p[i].joiner;
    cr.source = opts->source;
    cr.pos = kept.p[i].pos;
    cr.list = list;
    tp_push(out.credits, cr);
  }
  return out;
}
