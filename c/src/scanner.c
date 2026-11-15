/* scanner.c: R3 top-level group scanner and skeleton (go/scanner.go, js/src/scanner.ts).
 *
 * A skel is a skeleton (code points) plus, for every code point, its absolute offset in the
 * normalized input. Offsets are in UTF-16 units, as in the JS reference, so every "order of
 * appearance" comparison gives the same answer. Groups are looked up by the offset of their
 * placeholder, so any slice of a skeleton still knows which groups it contains.
 */
#include "internal.h"

TP_PRIVATE void tp_group_map_put(tp_group_map *m, const tp_group *g) { tp_push(m->groups, g); }

TP_PRIVATE const tp_group *tp_group_map_get(const tp_group_map *m, ptrdiff_t pos) {
  if (m == NULL) return NULL; /* a nil Go map */
  size_t lo = 0, hi = m->groups.n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    ptrdiff_t p = m->groups.p[mid]->pos;
    if (p == pos) return m->groups.p[mid];
    if (p < pos) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return NULL;
}

TP_PRIVATE ptrdiff_t tp_offset_at(const tp_offsets *o, ptrdiff_t i) {
  if (i >= 0 && i < (ptrdiff_t)o->at.n) return o->at.p[i];
  return o->end;
}

/* closerOf is the closing bracket for an opener, or TP_NORUNE. */
TP_PRIVATE tp_rune tp_closer_of(tp_rune r) {
  switch (r) {
  case '(':
    return ')';
  case '[':
    return ']';
  case '{':
    return '}';
  default:
    return TP_NORUNE;
  }
}

/* delimiterOf is the version delimiter of an opener. */
TP_PRIVATE tp_delimiter tp_delimiter_of(tp_rune r) {
  switch (r) {
  case '[':
    return TP_DELIMITER_SQUARE;
  case '{':
    return TP_DELIMITER_CURLY;
  default:
    return TP_DELIMITER_PAREN;
  }
}

/* scan is R3.1 to R3.5 on s, whose first code point sits at absolute offset base. *unbalanced is
 * R3.3. */
TP_PRIVATE tp_skel tp_scan(tp_runes s, ptrdiff_t base, bool *unbalanced) {
  /* The absolute offset of every code point, plus one entry for the end of the string. */
  tp_offsets offsets;
  memset(&offsets, 0, sizeof offsets);
  ptrdiff_t off = base;
  for (size_t i = 0; i < s.n; i++) {
    tp_push(offsets.at, off);
    if (s.p[i] >= 0x10000) {
      off += 2;
    } else {
      off++;
    }
  }
  tp_push(offsets.at, off);
  offsets.end = off;

  tp_runevec text = {0};
  tp_idxvec pos = {0};
  tp_group_map *groups = tp_alloc(sizeof *groups);
  tp_runevec stack = {0};
  ptrdiff_t group_start = 0;
  for (size_t ui = 0; ui < s.n; ui++) {
    ptrdiff_t i = (ptrdiff_t)ui;
    tp_rune r = s.p[ui];
    if (stack.n == 0) {
      tp_rune closer = tp_closer_of(r);
      if (closer != TP_NORUNE) {
        tp_push(stack, closer);
        group_start = i;
      } else {
        tp_push(text, r);
        tp_push(pos, tp_offset_at(&offsets, i));
      }
      continue;
    }
    tp_rune closer = tp_closer_of(r);
    if (closer != TP_NORUNE) {
      tp_push(stack, closer);
    } else if (tp_last(stack) == r) {
      stack.n--;
      if (stack.n == 0) {
        tp_add_group(s, group_start, i + 1, true, &offsets, groups);
        tp_push(text, TP_PH);
        tp_push(pos, tp_offset_at(&offsets, group_start));
      }
    }
    /* R3.2: a non-matching closer is literal text inside the group. */
  }
  *unbalanced = stack.n > 0;
  if (*unbalanced) {
    /* R3.3: the group runs to the end of the string. */
    tp_add_group(s, group_start, (ptrdiff_t)s.n, false, &offsets, groups);
    tp_push(text, TP_PH);
    tp_push(pos, tp_offset_at(&offsets, group_start));
  }
  tp_skel sk;
  sk.text = tp_runevec_view(text);
  sk.pos = pos;
  sk.groups = groups;
  return sk;
}

/* addGroup records the group s[start:end] (closed: end includes the closer). */
TP_PRIVATE void tp_add_group(tp_runes s, ptrdiff_t start, ptrdiff_t end, bool closed,
                             const tp_offsets *offsets, tp_group_map *groups) {
  ptrdiff_t inner_end = closed ? end - 1 : end;
  ptrdiff_t p = tp_offset_at(offsets, start);
  tp_group *g = tp_alloc(sizeof *g);
  g->open = tp_delimiter_of(tp_at(s, start));
  g->inner = tp_collapse_runes(tp_sub(s, start + 1, inner_end));
  g->raw = tp_runes_str(tp_sub(s, start, end));
  g->pos = p;
  tp_group_map_put(groups, g);
}

/* --------------------------------------------------------------------------------------------- */
/* Skeleton operations                                                                           */

static void clamp_bounds(size_t n, ptrdiff_t *a, ptrdiff_t *b) {
  if (*b > (ptrdiff_t)n) *b = (ptrdiff_t)n;
  if (*b < 0) *b = 0;
  if (*a > *b) *a = *b;
  if (*a < 0) *a = 0;
}

/* clamped is t[a:b] with both ends clamped to the slice (JS slice with non-negative bounds). */
TP_PRIVATE tp_runes tp_clamped_runes(tp_runes t, ptrdiff_t a, ptrdiff_t b) {
  clamp_bounds(t.n, &a, &b);
  tp_runes out = {t.p ? t.p + a : NULL, (size_t)(b - a)};
  return out;
}

TP_PRIVATE tp_idxvec tp_clamped_ints(tp_idxvec t, ptrdiff_t a, ptrdiff_t b) {
  clamp_bounds(t.n, &a, &b);
  tp_idxvec out;
  out.p = t.p ? t.p + a : NULL;
  out.n = (size_t)(b - a);
  out.cap = out.n; /* appending to the view copies */
  return out;
}

/* len is the number of code points. */
TP_PRIVATE ptrdiff_t tp_skel_len(const tp_skel *s) { return (ptrdiff_t)s->text.n; }

/* isEmpty reports whether the skeleton has no code points. */
TP_PRIVATE bool tp_skel_is_empty(const tp_skel *s) { return s->text.n == 0; }

/* sliceSkel is s[start:end], clamped. The text and offsets are copied, the groups shared. */
TP_PRIVATE tp_skel tp_slice_skel(const tp_skel *s, ptrdiff_t start, ptrdiff_t end) {
  tp_skel out;
  out.text = tp_runes_clone(tp_clamped_runes(s->text, start, end));
  tp_idxvec view = tp_clamped_ints(s->pos, start, end);
  tp_clone(out.pos, view);
  out.groups = s->groups;
  return out;
}

/* sliceFrom is s[start:]. */
TP_PRIVATE tp_skel tp_slice_from(const tp_skel *s, ptrdiff_t start) {
  return tp_slice_skel(s, start, (ptrdiff_t)s->text.n);
}

/* trimSkel removes R0.2 whitespace at both ends. */
TP_PRIVATE tp_skel tp_trim_skel(const tp_skel *s) {
  ptrdiff_t a = 0, b = (ptrdiff_t)s->text.n;
  while (a < b && tp_is_whitespace(tp_at(s->text, a))) a++;
  while (b > a && tp_is_whitespace(tp_before(s->text, b))) b--;
  return tp_slice_skel(s, a, b);
}

/* concatSkel is a followed by b (the groups of a). */
TP_PRIVATE tp_skel tp_concat_skel(const tp_skel *a, const tp_skel *b) {
  size_t n = tp_size_add(a->text.n, b->text.n);
  tp_rune *text = tp_alloc_array(n, sizeof *text);
  if (a->text.n) memcpy(text, a->text.p, a->text.n * sizeof *text);
  if (b->text.n) memcpy(text + a->text.n, b->text.p, b->text.n * sizeof *text);
  tp_skel out;
  out.text = (tp_runes){text, n};
  memset(&out.pos, 0, sizeof out.pos);
  tp_append(out.pos, a->pos);
  tp_append(out.pos, b->pos);
  out.groups = a->groups;
  return out;
}

/* groupAt is the group behind the placeholder at index, or NULL. */
TP_PRIVATE const tp_group *tp_group_at(const tp_skel *s, ptrdiff_t index) {
  if (tp_at(s->text, index) != TP_PH || index < 0 || index >= (ptrdiff_t)s->pos.n) return NULL;
  return tp_group_map_get(s->groups, s->pos.p[index]);
}

/* groupsOf is the groups of the skeleton, left to right. */
TP_PRIVATE tp_indexed_group_vec tp_groups_of(const tp_skel *s) {
  tp_indexed_group_vec out = {0};
  for (size_t i = 0; i < s->text.n; i++) {
    const tp_group *g = tp_group_at(s, (ptrdiff_t)i);
    if (g != NULL) {
      tp_indexed_group ig;
      ig.index = (ptrdiff_t)i;
      ig.group = g;
      tp_push(out, ig);
    }
  }
  return out;
}

/* hasGroup reports whether the skeleton holds a placeholder. */
TP_PRIVATE bool tp_has_group(const tp_skel *s) {
  for (size_t i = 0; i < s->text.n; i++) {
    if (s->text.p[i] == TP_PH) return true;
  }
  return false;
}

/* renderSkel substitutes the groups back (R3.4 reassembly). Whitespace is not touched. */
TP_PRIVATE tp_str tp_render_skel(const tp_skel *s) {
  tp_buf b = {0};
  for (size_t i = 0; i < s->text.n; i++) {
    const tp_group *g = tp_group_at(s, (ptrdiff_t)i);
    if (g != NULL) {
      tp_buf_str(&b, g->raw);
    } else {
      tp_buf_rune(&b, s->text.p[i]);
    }
  }
  return tp_buf_done(&b);
}

/* startPos is the absolute offset of the first code point, or fallback for an empty skeleton. */
TP_PRIVATE ptrdiff_t tp_start_pos(const tp_skel *s, ptrdiff_t fallback) {
  if (s->pos.n == 0) return fallback;
  return s->pos.p[0];
}

TP_PRIVATE bool tp_sorted_has(tp_idxvec v, ptrdiff_t x) {
  size_t lo = 0, hi = v.n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (v.p[mid] == x) return true;
    if (v.p[mid] < x) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return false;
}

/* removeGroups removes the placeholders of the groups at the given offsets (ascending). */
TP_PRIVATE tp_skel tp_remove_groups(const tp_skel *s, tp_idxvec remove) {
  if (remove.n == 0) return *s;
  tp_runevec text = {0};
  tp_idxvec pos = {0};
  for (size_t i = 0; i < s->text.n; i++) {
    if (i >= s->pos.n) break;
    tp_rune r = s->text.p[i];
    ptrdiff_t p = s->pos.p[i];
    if (r == TP_PH && tp_sorted_has(remove, p)) continue;
    tp_push(text, r);
    tp_push(pos, p);
  }
  tp_skel out;
  out.text = tp_runevec_view(text);
  out.pos = pos;
  out.groups = s->groups;
  return out;
}

/* isSpacedDashAt is R6.1: "-" with a space immediately on both sides. */
TP_PRIVATE bool tp_is_spaced_dash_at(tp_runes t, ptrdiff_t i) {
  return tp_at(t, i) == '-' && tp_before(t, i) == ' ' && tp_at(t, i + 1) == ' ';
}

/* hasSpacedDash reports whether t has a spaced dash. */
TP_PRIVATE bool tp_has_spaced_dash(tp_runes t) {
  for (ptrdiff_t i = 1; i + 1 < (ptrdiff_t)t.n; i++) {
    if (tp_is_spaced_dash_at(t, i)) return true;
  }
  return false;
}

/* isSpacedBulletAt is R8.7: U+2022 with a space immediately on both sides (the ends of the
 * string do not count). */
TP_PRIVATE bool tp_is_spaced_bullet_at(tp_runes t, ptrdiff_t i) {
  return tp_at(t, i) == TP_BULLET && tp_before(t, i) == ' ' && tp_at(t, i + 1) == ' ';
}

/* trimmedSegment is s[start:end] trimmed of R0.2 whitespace. */
TP_PRIVATE tp_segment tp_trimmed_segment(const tp_skel *s, ptrdiff_t start, ptrdiff_t end) {
  ptrdiff_t a = start;
  ptrdiff_t b = end;
  if (b < start) b = start;
  while (a < b && tp_is_whitespace(tp_at(s->text, a))) a++;
  while (b > a && tp_is_whitespace(tp_before(s->text, b))) b--;
  tp_segment seg;
  seg.skel = tp_slice_skel(s, a, b);
  seg.start = a;
  seg.end = b;
  return seg;
}

/* splitAtSpacedDashes is R6.1 / R8.2: split a skeleton at every spaced dash into trimmed
 * segments. With allow_leading, a string starting with "- " yields an empty first segment
 * (R6.3). */
TP_PRIVATE tp_segment_vec tp_split_at_spaced_dashes(const tp_skel *s, bool allow_leading) {
  tp_segment_vec segments = {0};
  tp_runes t = s->text;
  ptrdiff_t start = 0;
  if (allow_leading && tp_at(t, 0) == '-' && tp_at(t, 1) == ' ') {
    tp_segment seg;
    seg.skel = tp_slice_skel(s, 0, 0);
    seg.start = 0;
    seg.end = 0;
    tp_push(segments, seg);
    start = 2;
  }
  ptrdiff_t i = start > 1 ? start : 1;
  for (; i + 1 < (ptrdiff_t)t.n; i++) {
    if (tp_is_spaced_dash_at(t, i)) {
      tp_push(segments, tp_trimmed_segment(s, start, i - 1));
      start = i + 2;
    }
  }
  tp_push(segments, tp_trimmed_segment(s, start, (ptrdiff_t)t.n));
  return segments;
}
