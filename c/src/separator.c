/* separator.c: R6 artist/title separator, including the R6.2 suffix peeling shared with R8.2
 * (go/separator.go, js/src/separator.ts). */
#include "internal.h"

/* classifySegment is the R6.2 classification of a bare dash segment: placeholders make it
 * Unknown. */
TP_PRIVATE tp_classification tp_classify_segment(tp_skel seg, tp_ctx *c) {
  if (tp_has_group(&seg)) {
    tp_classification cls;
    memset(&cls, 0, sizeof cls);
    cls.kind = TP_CLASS_UNKNOWN;
    return cls;
  }
  tp_str text = tp_collapse_runes(seg.text);
  tp_site s;
  s.source = TP_SOURCE_TITLE;
  s.delimiter = TP_DELIMITER_DASH;
  s.pos = tp_start_pos(&seg, 0);
  return tp_classify(text, s, c);
}

/* isPeelable reports whether R6.2 / R8.2 can peel a classification. */
TP_PRIVATE bool tp_is_peelable(tp_classification cls) {
  switch (cls.kind) {
  case TP_CLASS_JUNK:
  case TP_CLASS_FLAG:
  case TP_CLASS_YEAR:
  case TP_CLASS_VERSIONS:
    return true;
  default:
    return false;
  }
}

/* peelSuffixes is R6.2 / R8.2: peel classifiable suffixes off the right end. relaxed drops
 * conditions (i) to (iv) (R8.2 with an artist side). It gives the remaining segments and the
 * peeled suffixes, left to right. */
TP_PRIVATE tp_segment_vec tp_peel_suffixes(tp_segment_vec segments, bool relaxed, tp_ctx *c,
                                           tp_peeled_vec *peeled) {
  tp_segment_vec remaining = segments;
  tp_peeled_vec out = {0};
  while (remaining.n >= 2) {
    tp_segment last = tp_last(remaining);
    tp_classification cls = tp_classify_segment(last.skel, c);
    if (!tp_is_peelable(cls)) break;
    tp_strvec words = tp_split_words_runes(last.skel.text);
    /* (ii) does not apply to prefix-form versions ("Andy C b2b Hedex - Live at Printworks"). */
    bool prefix_form = false;
    if (cls.kind == TP_CLASS_VERSIONS) {
      prefix_form = true;
      for (size_t i = 0; i < cls.versions.n; i++) {
        if (!cls.versions.p[i].prefix_form) {
          prefix_form = false;
          break;
        }
      }
    }
    bool has_year = false;
    for (size_t i = 0; i < words.n; i++) {
      int y;
      if (tp_year_of(words.p[i], &y)) {
        has_year = true;
        break;
      }
    }
    bool ok = relaxed || remaining.n > 2 || (words.n >= 2 && !prefix_form) || has_year ||
              cls.kind == TP_CLASS_JUNK;
    if (!ok) break;
    tp_peeled p;
    p.cls = cls;
    p.raw = tp_collapse_runes(last.skel.text);
    p.pos = tp_start_pos(&last.skel, 0);
    tp_push(out, p);
    remaining.n--;
  }
  /* Left to right. */
  if (out.n > 1) {
    for (size_t i = 0, j = out.n - 1; i < j; i++, j--) {
      tp_peeled tmp = out.p[i];
      out.p[i] = out.p[j];
      out.p[j] = tmp;
    }
  }
  *peeled = out;
  return remaining;
}

/* nonEmpty is the trimmed skeleton, or NULL when it is empty. */
TP_PRIVATE const tp_skel *tp_non_empty(tp_skel s) {
  tp_skel t = tp_trim_skel(&s);
  if (tp_skel_is_empty(&t)) return NULL;
  tp_skel *p = tp_alloc(sizeof *p);
  *p = t;
  return p;
}

/* splitAt cuts s around [cut_start, cut_end). */
TP_PRIVATE tp_split_parts tp_split_at(tp_skel s, ptrdiff_t cut_start, ptrdiff_t cut_end) {
  tp_skel right = tp_slice_from(&s, cut_end);
  tp_split_parts parts;
  parts.artist = tp_non_empty(tp_slice_skel(&s, 0, cut_start));
  parts.title = tp_trim_skel(&right);
  return parts;
}

/* resultOf is a separator result for split parts. */
TP_PRIVATE tp_separator_result tp_result_of(tp_split_parts parts, tp_peeled_vec p) {
  tp_separator_result r;
  r.artist = parts.artist;
  r.title = parts.title;
  r.split = parts.artist != NULL;
  r.peeled = p;
  return r;
}

/* firstDashSplit is R6.3: the first segment is the artist side, the rest (up to the last kept
 * one) the title. */
TP_PRIVATE bool tp_first_dash_split(tp_skel sk, tp_segment_vec segments, tp_split_parts *out) {
  if (segments.n < 2) return false;
  tp_segment first = segments.p[0], second = segments.p[1], last = tp_last(segments);
  const tp_skel *artist = NULL;
  if (!tp_skel_is_empty(&first.skel)) {
    tp_skel *a = tp_alloc(sizeof *a);
    *a = first.skel;
    artist = a;
  }
  out->artist = artist;
  out->title = tp_slice_skel(&sk, second.start, last.end);
  return true;
}

/* findSeparator is R6. */
TP_PRIVATE tp_separator_result tp_find_separator(tp_separator_input in, tp_ctx *c) {
  tp_skel sk = in.skel;
  tp_mode mode = in.mode;
  tp_segment_vec segments = tp_split_at_spaced_dashes(&sk, true);
  if (tp_str_eq(mode, TP_MODE_TRACKLIST)) {
    return tp_tracklist_separator(sk, segments, in.list_context, c);
  }
  tp_peeled_vec p;
  tp_segment_vec remaining = tp_peel_suffixes(segments, false, c, &p);

  /* R6.3 */
  tp_split_parts parts;
  if (tp_first_dash_split(sk, remaining, &parts)) return tp_result_of(parts, p);

  tp_skel rest = tp_slice_skel(&sk, 0, 0);
  if (remaining.n > 0) rest = remaining.p[0].skel;
  tp_split_parts other;
  if (tp_split_without_spaced_dash(rest, mode, c, &other)) return tp_result_of(other, p);

  /* R6.8: the uploader fallback does not warn noSeparator. */
  const tp_skel *artist = NULL;
  if (tp_str_eq(mode, TP_MODE_YOUTUBE)) artist = in.uploader;
  if (!tp_skel_is_empty(&rest) && artist == NULL) tp_ctx_warn(c, TP_WARNING_NO_SEPARATOR);
  tp_separator_result r;
  r.artist = artist;
  r.title = rest;
  r.split = false;
  r.peeled = p;
  return r;
}

/* tracklistSeparator is R6.10, tracklist mode. The first spaced dash always separates (no R6.2
 * peeling; R8.2 still peels later suffixes on the title side). Without one: R6.4, then the
 * first "_-_", then (list context only) the last tight "letter-Uppercase" dash; both of these
 * warn unspacedDashSplit. */
TP_PRIVATE tp_separator_result tp_tracklist_separator(tp_skel sk, tp_segment_vec segments,
                                                      bool list_context, tp_ctx *c) {
  tp_peeled_vec none = {0};
  tp_split_parts parts;
  if (tp_first_dash_split(sk, segments, &parts)) return tp_result_of(parts, none);
  tp_skel rest = tp_slice_skel(&sk, 0, 0);
  if (segments.n > 0) rest = segments.p[0].skel;
  ptrdiff_t at;
  if (tp_asymmetric_dash(rest.text, &at)) {
    tp_ctx_warn(c, TP_WARNING_ASYMMETRIC_DASH_SPLIT);
    return tp_result_of(tp_split_at(rest, at, at + 1), none);
  }
  if (tp_underscore_dash(rest.text, &at)) {
    tp_ctx_warn(c, TP_WARNING_UNSPACED_DASH_SPLIT);
    return tp_result_of(tp_split_at(rest, at, at + 3), none);
  }
  if (list_context) {
    if (tp_tight_upper_dash(rest.text, &at)) {
      tp_ctx_warn(c, TP_WARNING_UNSPACED_DASH_SPLIT);
      return tp_result_of(tp_split_at(rest, at, at + 1), none);
    }
  }
  if (!tp_skel_is_empty(&rest)) tp_ctx_warn(c, TP_WARNING_NO_SEPARATOR);
  tp_separator_result r;
  memset(&r, 0, sizeof r);
  r.title = rest;
  return r;
}

/* underscoreDash is the index of the first "_-_". */
TP_PRIVATE bool tp_underscore_dash(tp_runes t, ptrdiff_t *out) {
  for (size_t i = 0; i + 2 < t.n; i++) {
    if (t.p[i] == '_' && t.p[i + 1] == '-' && t.p[i + 2] == '_') {
      *out = (ptrdiff_t)i;
      return true;
    }
  }
  return false;
}

/* tightUpperDash is R6.10: the last "-" with a letter (R0.6) on its left and an uppercase letter
 * (Lu) on its right, as in R6.7 ("Jay-Z-Numb" gives "Jay-Z" and "Numb"). */
TP_PRIVATE bool tp_tight_upper_dash(tp_runes t, ptrdiff_t *out) {
  for (ptrdiff_t i = (ptrdiff_t)t.n - 2; i >= 1; i--) {
    if (tp_at(t, i) == '-' && tp_is_letter_before(t, i) && tp_is_upper_at(t, i + 1)) {
      *out = i;
      return true;
    }
  }
  return false;
}

/* splitWithoutSpacedDash is R6.4 to R6.7. */
TP_PRIVATE bool tp_split_without_spaced_dash(tp_skel s, tp_mode mode, tp_ctx *c,
                                             tp_split_parts *out) {
  ptrdiff_t at;
  if (tp_asymmetric_dash(s.text, &at)) {
    tp_ctx_warn(c, TP_WARNING_ASYMMETRIC_DASH_SPLIT);
    *out = tp_split_at(s, at, at + 1);
    return true;
  }
  bool youtube = tp_str_eq(mode, TP_MODE_YOUTUBE);
  if (youtube) {
    tp_split_parts quoted;
    if (tp_quoted_title(s, &quoted)) {
      tp_ctx_warn(c, TP_WARNING_QUOTED_TITLE_SPLIT);
      *out = quoted;
      return true;
    }
    ptrdiff_t by;
    if (tp_by_split(s, c, &by)) {
      tp_ctx_warn(c, TP_WARNING_BY_SPLIT);
      tp_split_parts lr = tp_split_at(s, by, by + 4);
      const tp_skel *artist = NULL;
      if (!tp_skel_is_empty(&lr.title)) {
        tp_skel *right = tp_alloc(sizeof *right);
        *right = lr.title;
        artist = right;
      }
      tp_skel title = s;
      if (lr.artist != NULL) title = *lr.artist;
      out->artist = artist;
      out->title = title;
      return true;
    }
  }
  if (youtube || tp_str_eq(mode, TP_MODE_FILENAME)) {
    ptrdiff_t dash;
    if (tp_unspaced_dash(s, c, &dash)) {
      tp_ctx_warn(c, TP_WARNING_UNSPACED_DASH_SPLIT);
      *out = tp_split_at(s, dash, dash + 1);
      return true;
    }
  }
  return false;
}

/* asymmetricDash is R6.4: the first "-" with a space on exactly one side. */
TP_PRIVATE bool tp_asymmetric_dash(tp_runes t, ptrdiff_t *out) {
  for (ptrdiff_t i = 1; i + 1 < (ptrdiff_t)t.n; i++) {
    if (tp_at(t, i) == '-' &&
        tp_is_whitespace(tp_before(t, i)) != tp_is_whitespace(tp_at(t, i + 1))) {
      *out = i;
      return true;
    }
  }
  return false;
}

/* quotedTitle is R6.5: Artist "Title" (extra). */
TP_PRIVATE bool tp_quoted_title(tp_skel s, tp_split_parts *out) {
  tp_runes t = s.text;
  ptrdiff_t open = tp_rune_index(t, '"');
  if (open <= 0) return false;
  ptrdiff_t rel = tp_rune_index(tp_tail(t, open + 1), '"');
  if (rel < 0) return false;
  ptrdiff_t close_at = rel + open + 1;
  tp_skel left = tp_slice_skel(&s, 0, open);
  tp_skel artist = tp_trim_skel(&left);
  tp_rune last = tp_at(artist.text, tp_skel_len(&artist) - 1);
  if (last == ':' || last == '-') {
    tp_skel shorter = tp_slice_skel(&artist, 0, tp_skel_len(&artist) - 1);
    artist = tp_trim_skel(&shorter);
  }
  tp_skel quoted = tp_slice_skel(&s, open + 1, close_at);
  tp_skel after = tp_slice_from(&s, close_at + 1);
  tp_skel joined = tp_concat_skel(&quoted, &after);
  tp_skel title = tp_trim_skel(&joined);
  const tp_skel *a = NULL;
  if (!tp_skel_is_empty(&artist)) {
    tp_skel *p = tp_alloc(sizeof *p);
    *p = artist;
    a = p;
  }
  out->artist = a;
  out->title = title;
  return true;
}

/* bySplit is R6.6: the index of the space before the last usable " by ". The right side must be
 * non-empty and not a single stopword ("Stand by Me"). */
TP_PRIVATE bool tp_by_split(tp_skel s, tp_ctx *c, ptrdiff_t *out) {
  static const char ph_utf8[] = "\xEE\x80\x80"; /* Go string(ph) */
  tp_str ph = {ph_utf8, sizeof ph_utf8 - 1};
  tp_runes t = s.text;
  /* Candidates right to left; index 0 is never one (i > 0 in the JS loop). */
  for (ptrdiff_t i = (ptrdiff_t)t.n - 4; i > 0; i--) {
    if (tp_at(t, i) != ' ' || tp_at(t, i + 1) != 'b' || tp_at(t, i + 2) != 'y' ||
        tp_at(t, i + 3) != ' ') {
      continue;
    }
    /* Placeholder-only words (groups such as "(Official Video)") don't count here. */
    tp_strvec words = tp_split_words_runes(tp_tail(t, i + 4));
    tp_strvec right = {0};
    for (size_t k = 0; k < words.n; k++) {
      if (tp_str_eq(words.p[k], ph)) continue;
      tp_push(right, words.p[k]);
    }
    bool single_stopword = right.n == 1 && tp_in_vocab(right.p[0], &c->t->stopwords);
    if (right.n > 0 && !single_stopword) {
      *out = i;
      return true;
    }
  }
  return false;
}

/* unspacedDash is R6.7: the chosen unspaced dash index ("Jay-Z-Numb"). */
TP_PRIVATE bool tp_unspaced_dash(tp_skel s, tp_ctx *c, ptrdiff_t *out) {
  tp_runes t = s.text;
  ptrdiff_t last = 0;
  bool found = false;
  bool has_known = c->known_keys != NULL && c->known_keys->m.n > 0;
  for (ptrdiff_t i = 1; i + 1 < (ptrdiff_t)t.n; i++) {
    if (tp_at(t, i) != '-' || tp_is_whitespace(tp_before(t, i)) ||
        tp_is_whitespace(tp_at(t, i + 1))) {
      continue;
    }
    if (!(tp_at(t, i + 1) == '"' || tp_is_letter_at(t, i + 1))) continue;
    if (has_known && tp_is_known_artist(tp_render_prefix(&s, i), c)) {
      *out = i;
      return true;
    }
    last = i;
    found = true;
  }
  if (found) *out = last;
  return found;
}

/* renderPrefix is the rendered text of s[:end]. */
TP_PRIVATE tp_str tp_render_prefix(const tp_skel *s, ptrdiff_t end) {
  tp_skel head = tp_slice_skel(s, 0, end);
  return tp_render_skel(&head);
}
