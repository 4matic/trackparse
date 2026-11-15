/* artists.c: R7, the artist side (go/artists.go). The list splitting is credits.c. */
#include "internal.h"

/* findFeatMarker is R7.2: the leftmost unbracketed feat marker at or after from, with a space (or
 * placeholder) on its left and a space on its right. */
TP_PRIVATE bool tp_find_feat_marker(const tp_skel *s, tp_ctx *c, ptrdiff_t from,
                                    tp_feat_marker *out) {
  tp_word_span_vec spans = tp_word_spans(s->text);
  tp_strvec words = tp_span_texts(spans);
  for (size_t i = 0; i < spans.n; i++) {
    tp_word_span span = spans.p[i];
    tp_rune b;
    tp_phrase_match m;
    ptrdiff_t last_index;
    tp_word_span last;
    if (span.start < from) continue;
    b = tp_before(s->text, span.start);
    if (b != ' ' && b != TP_PH) continue;
    if (!tp_phrase_table_match_at(c->t->feat_markers, words, (ptrdiff_t)i, &m)) continue;
    last_index = (ptrdiff_t)i + m.length - 1;
    if (last_index < 0 || last_index >= (ptrdiff_t)spans.n) continue;
    last = spans.p[last_index];
    if (tp_at(s->text, last.end) != ' ') continue;
    out->start = span.start;
    out->end = last.end;
    out->marker = tp_runes_str(tp_sub(s->text, span.start, last.end));
    return true;
  }
  return false;
}

static int by_credit_pos(const void *a, const void *b, void *ctx) {
  ptrdiff_t pa = ((const tp_credit *)a)->pos, pb = ((const tp_credit *)b)->pos;
  (void)ctx;
  return pa < pb ? -1 : (pa > pb ? 1 : 0);
}

static void take_list(tp_artist_side_result *result, const tp_list_result *l) {
  tp_append(result->credits, l->credits);
  tp_append(result->years, l->years);
  if (l->unknown) result->unknown_artist = true;
}

/* parseArtistSide is R7.1 to R7.5 over an artist-side skeleton. */
TP_PRIVATE tp_artist_side_result tp_parse_artist_side(const tp_skel *side, tp_ctx *c) {
  tp_artist_side_result result;
  tp_idxvec removed = {0}; /* ascending: groupsOf order */
  tp_indexed_group_vec groups = tp_groups_of(side);
  tp_skel text, main_skel;
  tp_feat_marker feat;
  bool has_feat;
  tp_list_options opts;
  tp_list_result main_list;
  memset(&result, 0, sizeof result);

  /* R7.1 */
  for (size_t i = 0; i < groups.n; i++) {
    const tp_group *g = groups.p[i].group;
    tp_site st;
    tp_classification cl;
    st.source = TP_SOURCE_ARTIST;
    st.delimiter = g->open;
    st.pos = g->pos;
    cl = tp_classify(g->inner, st, c);
    switch (cl.kind) {
    case TP_CLASS_FEAT:
    case TP_CLASS_PRODUCER:
      take_list(&result, &cl.list);
      break;
    case TP_CLASS_FLAG:
      if (cl.flag == TP_FLAG_EXPLICIT) {
        result.explicit_ = true;
      } else {
        result.clean = true;
      }
      break;
    case TP_CLASS_YEAR: {
      tp_positioned_year y;
      y.year = cl.year;
      y.pos = g->pos;
      tp_push(result.years, y);
      break;
    }
    case TP_CLASS_JUNK: {
      tp_positioned_junk j;
      j.raw = g->inner;
      j.kind = cl.junk;
      j.pos = g->pos;
      tp_push(result.junk, j);
      break;
    }
    case TP_CLASS_UNKNOWN:
    case TP_CLASS_VERSIONS:
    default:
      /* Versions and unknown groups stay in the names. */
      continue;
    }
    tp_push(removed, g->pos);
  }
  text = tp_remove_groups(side, removed);

  /* R7.2 */
  has_feat = tp_find_feat_marker(&text, c, 0, &feat);
  if (has_feat) {
    tp_skel head = tp_slice_skel(&text, 0, feat.start);
    main_skel = tp_trim_skel(&head);
  } else {
    main_skel = tp_trim_skel(&text);
  }
  memset(&opts, 0, sizeof opts);
  opts.role = TP_ROLE_PRIMARY;
  opts.source = TP_SOURCE_ARTIST;
  opts.split_with = true;
  main_list = tp_split_credits(&main_skel, &opts, c);
  take_list(&result, &main_list);
  if (has_feat) {
    tp_skel rest = tp_slice_from(&text, feat.end);
    tp_skel trimmed = tp_trim_skel(&rest);
    tp_str *marker = tp_alloc(sizeof *marker);
    tp_list_result featured;
    *marker = feat.marker;
    memset(&opts, 0, sizeof opts);
    opts.role = TP_ROLE_FEATURED;
    opts.source = TP_SOURCE_ARTIST;
    opts.lead = marker;
    opts.feat_markers_join = true;
    featured = tp_split_credits(&trimmed, &opts, c);
    take_list(&result, &featured);
  }
  /* Stable, like the JS sort. */
  tp_sort_stable(result.credits.p, result.credits.n, sizeof *result.credits.p, by_credit_pos, NULL);
  return result;
}
