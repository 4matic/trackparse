/* title.c: R8 the title side (go/title.go, js/src/title.ts). */
#include "internal.h"

/* setFlag records an Explicit or Clean flag. */
TP_PRIVATE void tp_extracted_set_flag(tp_extracted *out, tp_flag f) {
  switch (f) {
  case TP_FLAG_EXPLICIT:
    out->explicit_ = true;
    break;
  case TP_FLAG_CLEAN:
    out->clean = true;
    break;
  }
}

/* addPeeled records R6.2 / R8.2 peeled dash suffixes. */
TP_PRIVATE void tp_add_peeled(tp_extracted *out, tp_peeled_vec list) {
  for (size_t i = 0; i < list.n; i++) {
    const tp_peeled *p = &list.p[i];
    switch (p->cls.kind) {
    case TP_CLASS_VERSIONS:
      tp_append(out->versions, p->cls.versions);
      break;
    case TP_CLASS_JUNK: {
      tp_positioned_junk j;
      j.raw = p->raw;
      j.kind = p->cls.junk;
      j.pos = p->pos;
      tp_push(out->junk, j);
      break;
    }
    case TP_CLASS_FLAG:
      tp_extracted_set_flag(out, p->cls.flag);
      break;
    case TP_CLASS_YEAR: {
      tp_positioned_year y;
      y.year = p->cls.year;
      y.pos = p->pos;
      tp_push(out->years, y);
      break;
    }
    default:
      break;
    }
  }
}

/* parseTitleSide is R8. relaxed: an artist side was split off (R8.2 peels without conditions). */
TP_PRIVATE tp_title_side_result tp_parse_title_side(tp_skel input, bool relaxed, bool youtube,
                                                    tp_extracted *out, tp_ctx *c) {
  tp_idxvec removed = tp_extract_groups(input, out, c); /* R8.1 */

  /* R8.2 */
  tp_segment_vec segments = tp_split_at_spaced_dashes(&input, false);
  tp_peeled_vec p;
  tp_segment_vec remaining = tp_peel_suffixes(segments, relaxed, c, &p);
  tp_add_peeled(out, p);
  ptrdiff_t last_kept_end = 0;
  if (remaining.n > 0) last_kept_end = tp_last(remaining).end;
  tp_skel sk = tp_slice_skel(&input, 0, last_kept_end);

  sk = tp_extract_unbracketed_credits(sk, out, c); /* R8.3 */
  sk = tp_remove_groups(&sk, removed);
  if (youtube) sk = tp_strip_trailing_junk(sk, out, c); /* R8.4 */

  tp_title_side_result r;
  r.title = tp_cleanup_title(tp_render_skel(&sk));                       /* R8.5 */
  r.unknown_title = r.title.n > 0 && tp_is_unknown_token(r.title, c->t); /* R8.6 */
  return r;
}

/* extractGroups is R8.1: classify every group. It gives the offsets of the groups to remove,
 * ascending. */
TP_PRIVATE tp_idxvec tp_extract_groups(tp_skel sk, tp_extracted *out, tp_ctx *c) {
  tp_idxvec removed = {0};
  tp_indexed_group_vec groups = tp_groups_of(&sk);
  for (size_t i = 0; i < groups.n; i++) {
    const tp_group *g = groups.p[i].group;
    tp_site s;
    s.source = TP_SOURCE_TITLE;
    s.delimiter = g->open;
    s.pos = g->pos;
    tp_classification cls = tp_classify(g->inner, s, c);
    switch (cls.kind) {
    case TP_CLASS_VERSIONS:
      tp_append(out->versions, cls.versions);
      break;
    case TP_CLASS_JUNK: {
      tp_positioned_junk j;
      j.raw = g->inner;
      j.kind = cls.junk;
      j.pos = g->pos;
      tp_push(out->junk, j);
      break;
    }
    case TP_CLASS_FLAG:
      tp_extracted_set_flag(out, cls.flag);
      break;
    case TP_CLASS_YEAR: {
      tp_positioned_year y;
      y.year = cls.year;
      y.pos = g->pos;
      tp_push(out->years, y);
      break;
    }
    case TP_CLASS_FEAT:
    case TP_CLASS_PRODUCER:
      tp_append(out->credits, cls.list.credits);
      tp_append(out->years, cls.list.years);
      if (cls.list.unknown) out->unknown_artist = true;
      break;
    default:
      continue;
    }
    /* A set (Go map[int]struct{}), kept ascending for tp_sorted_has. */
    size_t k = removed.n;
    while (k > 0 && removed.p[k - 1] > g->pos) k--;
    if (k > 0 && removed.p[k - 1] == g->pos) continue;
    tp_insert(removed, k, g->pos);
  }
  return removed;
}

/* titleMarkers are the R8.3 markers, left to right, each with a space on both sides: feat
 * markers from title_feat_markers ("feat.", "ft.", "featuring"; undotted feat and ft stay text)
 * and producer markers from title_producer_markers ("prod.", "prod. by", "produced by",
 * "prod.by"). */
TP_PRIVATE tp_marker_hit_vec tp_title_markers(tp_skel sk, tp_ctx *c) {
  tp_word_span_vec spans = tp_word_spans(sk.text);
  tp_strvec words = tp_span_texts(spans);
  tp_marker_hit_vec hits = {0};
  ptrdiff_t n = (ptrdiff_t)spans.n;
  for (ptrdiff_t i = 0; i < n;) {
    tp_word_span span = spans.p[i];
    ptrdiff_t step = i + 1;
    if (tp_before(sk.text, span.start) != ' ') {
      i = step;
      continue;
    }
    tp_role role;
    ptrdiff_t last_index = 0;
    tp_phrase_match m;
    if (tp_phrase_table_match_at(c->t->title_feat_markers, words, i, &m)) {
      role = TP_ROLE_FEATURED;
      last_index = i + m.length - 1;
    } else if (tp_phrase_table_match_at(c->t->title_producer_markers, words, i, &m)) {
      role = TP_ROLE_PRODUCER;
      last_index = i + m.length - 1;
    } else {
      i = step;
      continue;
    }
    if (last_index < 0 || last_index >= n) {
      i = step;
      continue;
    }
    tp_word_span last = spans.p[last_index];
    if (tp_at(sk.text, last.end) != ' ') {
      i = step;
      continue;
    }
    tp_marker_hit h;
    h.start = span.start;
    h.end = last.end;
    h.marker = tp_runes_str(tp_sub(sk.text, span.start, last.end));
    h.role = role;
    tp_push(hits, h);
    i = last_index + 1;
  }
  return hits;
}

/* Go closure nextGroupAt of extractUnbracketedCredits. */
static ptrdiff_t next_group_at(const ptrdiff_t *next_group, ptrdiff_t n, ptrdiff_t k) {
  if (k >= 0 && k <= n) return next_group[k];
  return n;
}

typedef struct credit_cut {
  ptrdiff_t start, end;
} credit_cut;
typedef TP_VEC(credit_cut) credit_cut_vec;

/* extractUnbracketedCredits is R8.3: "Title feat. X" and "Title prod. by Y" become credits,
 * removed from the title. A list runs to the next placeholder, the next marker of the other
 * kind, or the end; later feat markers inside a featured list are joiners (R7.2). */
TP_PRIVATE tp_skel tp_extract_unbracketed_credits(tp_skel input, tp_extracted *out, tp_ctx *c) {
  tp_marker_hit_vec markers = tp_title_markers(input, c);
  if (markers.n == 0) return input;
  /* indexOf(PH, from) for every from, computed once so many markers stay linear (R0.8). */
  ptrdiff_t n = (ptrdiff_t)input.text.n;
  ptrdiff_t *next_group = tp_alloc_array((size_t)n + 1, sizeof *next_group);
  next_group[n] = n;
  for (ptrdiff_t k = n - 1; k >= 0; k--) {
    next_group[k] = input.text.p[k] == TP_PH ? k : next_group[k + 1];
  }
  credit_cut_vec cuts = {0};
  ptrdiff_t count = (ptrdiff_t)markers.n;
  for (ptrdiff_t i = 0; i < count;) {
    tp_marker_hit hit = markers.p[i];
    ptrdiff_t list_end = next_group_at(next_group, n, hit.end);
    for (ptrdiff_t k = i + 1; k < count; k++) {
      const tp_marker_hit *m = &markers.p[k];
      if (!tp_str_eq(m->role, hit.role) && m->start >= hit.end) {
        if (m->start < list_end) list_end = m->start;
        break;
      }
    }
    tp_skel part = tp_slice_skel(&input, hit.end, list_end);
    tp_skel list = tp_trim_skel(&part);
    if (!tp_skel_is_empty(&list)) {
      tp_str *marker = tp_alloc(sizeof *marker);
      *marker = hit.marker;
      tp_list_options opts;
      memset(&opts, 0, sizeof opts);
      opts.role = hit.role;
      opts.source = TP_SOURCE_TITLE;
      opts.lead = marker;
      opts.split_with = false;
      opts.feat_markers_join = tp_str_eq(hit.role, TP_ROLE_FEATURED);
      tp_list_result r = tp_split_credits(&list, &opts, c);
      tp_append(out->credits, r.credits);
      tp_append(out->years, r.years);
      if (r.unknown) out->unknown_artist = true;
      credit_cut k = {hit.start, list_end};
      tp_push(cuts, k);
    } else {
      list_end = hit.end;
    }
    while (i < count && markers.p[i].start < list_end) i++;
  }
  /* The cuts are disjoint and ascending: remove them in one pass. */
  tp_runevec text = {0};
  tp_idxvec pos = {0};
  ptrdiff_t from = 0;
  for (size_t ci = 0; ci < cuts.n; ci++) {
    credit_cut k = cuts.p[ci];
    ptrdiff_t start = k.start < n ? k.start : n;
    if (start < from) start = from;
    tp_runes tr = tp_clamped_runes(input.text, from, start);
    if (tr.n) memcpy(tp_push_n(text, tr.n), tr.p, tr.n * sizeof *tr.p);
    tp_idxvec pr = tp_clamped_ints(input.pos, from, start);
    tp_append(pos, pr);
    ptrdiff_t e = k.end < n ? k.end : n;
    if (e > from) from = e;
  }
  tp_runes tr = tp_clamped_runes(input.text, from, n);
  if (tr.n) memcpy(tp_push_n(text, tr.n), tr.p, tr.n * sizeof *tr.p);
  tp_idxvec pr = tp_clamped_ints(input.pos, from, (ptrdiff_t)input.pos.n);
  tp_append(pos, pr);
  tp_skel res;
  res.text = tp_runevec_view(text);
  res.pos = pos;
  res.groups = input.groups;
  return res;
}

/* stripTrailingJunk is R8.4: strip trailing unbracketed junk phrases (optionally after a lone "-"
 * or "|"). Word spans are computed once and walked backwards, so repeated junk stays linear
 * (R0.8). */
TP_PRIVATE tp_skel tp_strip_trailing_junk(tp_skel input, tp_extracted *out, tp_ctx *c) {
  tp_skel sk = tp_trim_skel(&input);
  tp_word_span_vec spans = tp_word_spans(sk.text);
  tp_strvec words = tp_span_texts(spans);
  ptrdiff_t nspans = (ptrdiff_t)spans.n;
  ptrdiff_t end = nspans; /* words still in the title */
  for (;;) {
    tp_phrase_match m;
    if (!tp_phrase_table_match_ending(c->t->junk_phrases, words, end, &m)) break;
    ptrdiff_t first = end - (m.length < end ? m.length : end);
    ptrdiff_t keep = first;
    if (keep > 0 && tp_is_lone_edge(words.p[keep - 1])) keep--;
    if (keep == 0) break; /* never strip the whole title */
    ptrdiff_t from = 0;
    if (first < nspans) from = spans.p[first].start;
    ptrdiff_t to = (ptrdiff_t)sk.text.n;
    if (end >= 1 && end - 1 < nspans) to = spans.p[end - 1].end;
    ptrdiff_t p = 0;
    if (from < (ptrdiff_t)sk.pos.n) p = sk.pos.p[from];
    tp_positioned_junk j;
    j.raw = tp_collapse_runes(tp_sub(sk.text, from, to));
    j.kind = *(const tp_str *)m.value;
    j.pos = p;
    tp_push(out->junk, j);
    end = keep;
  }
  if (end == nspans) return sk;
  ptrdiff_t cut_at = 0;
  if (end >= 1 && end - 1 < nspans) cut_at = spans.p[end - 1].end;
  tp_skel kept = tp_slice_skel(&sk, 0, cut_at);
  return tp_trim_skel(&kept);
}

/* isLoneEdge reports whether a word is a lone "-" or "|". */
TP_PRIVATE bool tp_is_lone_edge(tp_str word) {
  return tp_str_eq(word, TP_S("-")) || tp_str_eq(word, TP_S("|"));
}

/* stripLoneEdges removes lone "-" and "|" words at both ends of a space-separated string. */
TP_PRIVATE tp_str tp_strip_lone_edges(tp_str s) {
  tp_strvec words = tp_split(s, TP_S(" "));
  size_t a = 0, b = words.n;
  while (a < b && tp_is_lone_edge(words.p[a])) a++;
  while (b > a && tp_is_lone_edge(words.p[b - 1])) b--;
  tp_strvec mid;
  mid.p = words.p + a;
  mid.n = b - a;
  mid.cap = mid.n;
  return tp_join(mid, TP_S(" "));
}

/* cleanupTitle is R8.5. */
TP_PRIVATE tp_str tp_cleanup_title(tp_str raw) {
  tp_str title = tp_strip_lone_edges(tp_collapse_spaces(raw));
  tp_runes t = tp_runes_of(title);
  if (t.n >= 2) {
    tp_rune q = t.p[0], last = t.p[t.n - 1];
    if ((q == '"' || q == '\'') && last == q) {
      return tp_strip_lone_edges(tp_collapse_runes(tp_sub(t, 1, (ptrdiff_t)t.n - 1)));
    }
  }
  return title;
}

/* ------------------------------------------------------------------------------------------ */
/* R8.7 Bullet suffix */

/* bulletSegments is R8.7.1 to R8.7.2: the head and tail segments when the rule fires. It cuts
 * at every spaced bullet after the first spaced dash D, which must have text before and after
 * it. */
TP_PRIVATE bool tp_bullet_segments(const tp_skel *sk, tp_skel_vec *out) {
  tp_runes t = sk->text;
  ptrdiff_t n = (ptrdiff_t)t.n;
  if (tp_at(t, 0) == '-' && tp_at(t, 1) == ' ') return false; /* R6.1: D at the start */
  ptrdiff_t dash = -1;
  for (ptrdiff_t i = 1; i + 1 < n; i++) {
    if (tp_is_spaced_dash_at(t, i)) {
      dash = i;
      break;
    }
  }
  if (dash < 0) return false;
  tp_skel_vec parts = {0};
  ptrdiff_t start = 0;
  for (ptrdiff_t i = dash + 2; i + 1 < n; i++) {
    if (!tp_is_spaced_bullet_at(t, i)) continue;
    tp_skel part = tp_slice_skel(sk, start, i);
    tp_push(parts, tp_trim_skel(&part));
    start = i + 1;
  }
  if (parts.n == 0) return false;
  tp_skel last = tp_slice_from(sk, start);
  tp_push(parts, tp_trim_skel(&last));
  for (size_t i = 0; i < parts.n; i++) {
    if (tp_skel_is_empty(&parts.p[i])) return false;
  }
  /* R8.7.2: the title part between D and the first bullet counts as a segment ("A - • L"). */
  if (tp_skel_len(&parts.p[0]) <= dash + 1) return false;
  *out = parts;
  return true;
}

/* cutBulletSuffix is R8.7: cut a bullet suffix off the working skeleton (after R2 and R4,
 * before R6). False when the rule does not fire. */
TP_PRIVATE bool tp_cut_bullet_suffix(const tp_skel *sk, tp_ctx *c, tp_bullet_suffix *out) {
  tp_skel_vec parts;
  if (!tp_bullet_segments(sk, &parts)) return false;
  tp_positioned_junk_vec junk = {0};
  const tp_str *label = NULL;
  for (size_t i = 1; i < parts.n; i++) {
    tp_skel part = parts.p[i];
    /* R8.7.4: junk groups go to junk; every other group stays verbatim. */
    tp_idxvec removed = {0};
    tp_indexed_group_vec groups = tp_groups_of(&part);
    for (size_t k = 0; k < groups.n; k++) {
      const tp_group *g = groups.p[k].group;
      tp_str kind;
      if (!tp_junk_kind_of(tp_split_words(g->inner), c, &kind)) continue;
      tp_positioned_junk j;
      j.raw = g->inner;
      j.kind = kind;
      j.pos = g->pos;
      tp_push(junk, j);
      tp_push(removed, g->pos);
    }
    tp_skel kept = tp_remove_groups(&part, removed);
    tp_skel rest = tp_trim_skel(&kept);
    if (tp_skel_is_empty(&rest)) continue;
    tp_str text = tp_collapse_spaces(tp_render_skel(&rest));
    ptrdiff_t pos = tp_start_pos(&rest, 0);
    /* R8.7.4: only junk phrases, connectors and year words (no genres, no label suffixes).
     * JS truthiness: an empty kind (possible through keywords.junk) does not count. */
    tp_str kind;
    if (tp_phrase_kind_of(tp_split_words(text), c->t->junk_phrases, c, &kind) && kind.n != 0) {
      tp_positioned_junk j;
      j.raw = text;
      j.kind = kind;
      j.pos = pos;
      tp_push(junk, j);
    } else if (label == NULL) {
      tp_str *l = tp_alloc(sizeof *l);
      *l = text;
      label = l;
    } else {
      tp_positioned_junk j;
      j.raw = text;
      j.kind = TP_JUNK_KIND_NOTE;
      j.pos = pos;
      tp_push(junk, j);
    }
  }
  out->head = parts.p[0];
  out->label = label;
  out->junk = junk;
  return true;
}
