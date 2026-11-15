/* mode.c: R2 mode resolution, platform suffixes, filename handling and youtube pipes
 * (go/mode.go, js/src/mode.ts). */
#include "internal.h"

/* asciiLowerRunes lowercases A-Z only. */
TP_PRIVATE tp_runes tp_ascii_lower_runes(tp_runes t) {
  tp_rune *out = tp_alloc_array(t.n, sizeof *out);
  for (size_t i = 0; i < t.n; i++) {
    tp_rune r = t.p[i];
    if (r >= 'A' && r <= 'Z') r += 'a' - 'A';
    out[i] = r;
  }
  return (tp_runes){out, t.n};
}

/* hasRuneSuffix reports whether t ends with suffix. */
TP_PRIVATE bool tp_has_rune_suffix(tp_runes t, tp_runes suffix) {
  if (suffix.n > t.n) return false;
  size_t off = t.n - suffix.n;
  for (size_t i = 0; i < suffix.n; i++) {
    if (t.p[off + i] != suffix.p[i]) return false;
  }
  return true;
}

/* runeIndex is the first index of r in t, or -1. */
TP_PRIVATE ptrdiff_t tp_rune_index(tp_runes t, tp_rune r) {
  for (size_t i = 0; i < t.n; i++) {
    if (t.p[i] == r) return (ptrdiff_t)i;
  }
  return -1;
}

/* lastRuneIndex is the last index of r in t, or -1. */
TP_PRIVATE ptrdiff_t tp_last_rune_index(tp_runes t, tp_rune r) {
  for (ptrdiff_t i = (ptrdiff_t)t.n - 1; i >= 0; i--) {
    if (t.p[i] == r) return i;
  }
  return -1;
}

/* stripPlatformSuffixes is R2.2: strip trailing platform suffixes (longest first, repeatedly). */
TP_PRIVATE tp_runes tp_strip_platform_suffixes(tp_runes s, tp_ctx *c,
                                               tp_positioned_junk_vec *junk) {
  tp_runes text = tp_runes_clone(s);
  for (bool stripped = true; stripped;) {
    stripped = false;
    tp_runes lower = tp_ascii_lower_runes(text);
    for (size_t k = 0; k < c->t->platform_suffixes.n; k++) {
      tp_runes suffix_lower = tp_runes_of(tp_ascii_lower(c->t->platform_suffixes.p[k]));
      if (!tp_has_rune_suffix(lower, suffix_lower)) continue;
      ptrdiff_t start = (ptrdiff_t)text.n - (ptrdiff_t)suffix_lower.n;
      tp_positioned_junk j;
      j.raw = tp_runes_str(tp_tail(text, start + 3));
      j.kind = TP_JUNK_KIND_PLATFORM;
      j.pos = tp_utf16_len(tp_sub(text, 0, start)) + 3;
      tp_push(*junk, j);
      text = tp_sub(text, 0, start);
      stripped = true;
      break;
    }
  }
  return text;
}

/* hasExtension reports whether s ends in a known file extension (R2.3). */
TP_PRIVATE bool tp_has_extension(tp_runes s, tp_ctx *c) {
  ptrdiff_t dot = tp_last_rune_index(s, '.');
  if (dot < 0) return false;
  return tp_word_set_has(&c->t->extensions, tp_ascii_lower(tp_runes_str(tp_tail(s, dot + 1))));
}

typedef struct pipe_cut {
  ptrdiff_t a, b;
} pipe_cut;
typedef TP_VEC(pipe_cut) pipe_cut_vec;

/* splitPipes splits a skeleton at top-level " | " and " || " separators into trimmed segments
 * (R2.4). */
TP_PRIVATE tp_segment_vec tp_split_pipes(tp_skel sk) {
  tp_runes t = sk.text;
  ptrdiff_t n = (ptrdiff_t)t.n;
  pipe_cut_vec cuts = {0};
  for (ptrdiff_t i = 1; i < n; i++) {
    if (tp_before(t, i) == ' ' && tp_at(t, i) == '|') {
      ptrdiff_t bars = 1;
      if (tp_at(t, i + 1) == '|') bars = 2;
      if (tp_at(t, i + bars) == ' ') {
        pipe_cut k = {i - 1, i + bars + 1};
        tp_push(cuts, k);
        i += bars;
      }
    }
  }
  pipe_cut last = {n, n};
  tp_push(cuts, last);
  tp_segment_vec segments = {0};
  ptrdiff_t start = 0;
  for (size_t ci = 0; ci < cuts.n; ci++) {
    pipe_cut k = cuts.p[ci];
    ptrdiff_t s = start, e = k.a;
    while (s < e && tp_at(t, s) == ' ') s++;
    while (e > s && tp_before(t, e) == ' ') e--;
    tp_segment seg;
    seg.skel = tp_slice_skel(&sk, s, e);
    seg.start = s;
    seg.end = e;
    tp_push(segments, seg);
    start = k.b;
  }
  return segments;
}

/* trailingJunkPhrase is the R8.4 test: the text ends with an unbracketed junk phrase (genres
 * excluded). It gives the code-point index where the phrase starts and its kind. */
TP_PRIVATE bool tp_trailing_junk_phrase(tp_runes text, tp_ctx *c, ptrdiff_t *start, tp_str *kind) {
  tp_word_span_vec spans = tp_word_spans(text);
  tp_strvec words = tp_span_texts(spans);
  tp_phrase_match m;
  if (!tp_phrase_table_match_ending(c->t->junk_phrases, words, (ptrdiff_t)spans.n, &m)) {
    return false;
  }
  ptrdiff_t first = (ptrdiff_t)spans.n - m.length;
  if (first < 0 || first >= (ptrdiff_t)spans.n) return false;
  *start = spans.p[first].start;
  *kind = *(const tp_str *)m.value;
  return true;
}

/* junkTextKind is the R5.1 junk kind of a whole text, if it is junk. */
TP_PRIVATE bool tp_junk_text_kind(tp_str text, tp_ctx *c, tp_str *kind) {
  tp_strvec words = tp_split_words(text);
  if (words.n == 0) return false;
  return tp_junk_kind_of(words, c, kind);
}

/* looksLikeYoutube is the R2.1 youtube signals besides platform suffixes. */
TP_PRIVATE bool tp_looks_like_youtube(tp_skel sk, tp_ctx *c) {
  if (tp_split_pipes(sk).n > 1) return true;
  tp_indexed_group_vec groups = tp_groups_of(&sk);
  for (size_t i = 0; i < groups.n; i++) {
    tp_str kind;
    if (tp_junk_text_kind(groups.p[i].group->inner, c, &kind) && !tp_str_eq(kind, TP_S("genre"))) {
      return true;
    }
  }
  ptrdiff_t start;
  tp_str kind;
  return tp_trailing_junk_phrase(sk.text, c, &start, &kind);
}

/* prelude is R2 on the normalized string. */
TP_PRIVATE tp_prelude_result tp_prelude(tp_str normalized, tp_requested_mode requested, tp_ctx *c) {
  tp_positioned_junk_vec junk = {0};
  tp_runes text = tp_strip_platform_suffixes(tp_runes_of(normalized), c, &junk);
  bool had_platform = junk.n > 0;

  tp_mode mode;
  if (requested.kind == TP_REQUEST_MODE) {
    mode = requested.mode; /* R13.6: tracklist is explicit only; R2.3 and R2.4 do not run. */
  } else if (tp_has_extension(text, c)) {
    mode = TP_MODE_FILENAME;
  } else {
    mode = TP_MODE_CLEAN;
  }

  if (tp_str_eq(mode, TP_MODE_FILENAME)) {
    /* R2.3: strip the extension, trim, then "_" becomes a space when there is no space. */
    if (tp_has_extension(text, c)) {
      ptrdiff_t dot = tp_last_rune_index(text, '.');
      if (dot >= 0) text = tp_sub(text, 0, dot);
    }
    text = tp_js_trim_runes(text);
    if (tp_rune_index(text, ' ') < 0 && tp_rune_index(text, '_') >= 0) {
      tp_rune *spaced = tp_alloc_array(text.n, sizeof *spaced);
      for (size_t i = 0; i < text.n; i++) {
        tp_rune r = text.p[i];
        if (r == '_') r = ' ';
        spaced[i] = r;
      }
      text = tp_js_trim_runes((tp_runes){spaced, text.n});
    }
  }

  bool unbalanced;
  tp_skel sk = tp_scan(text, 0, &unbalanced);
  if (unbalanced) tp_ctx_warn(c, TP_WARNING_UNBALANCED_BRACKETS);
  if (requested.kind == TP_REQUEST_AUTO && tp_str_eq(mode, TP_MODE_CLEAN) &&
      (had_platform || tp_looks_like_youtube(sk, c))) {
    mode = TP_MODE_YOUTUBE;
  }

  tp_prelude_result res;
  memset(&res, 0, sizeof res);
  res.mode = mode;
  if (!tp_str_eq(mode, TP_MODE_YOUTUBE)) {
    res.working.text = text;
    res.working.base = 0;
    res.junk = junk;
    return res;
  }
  const tp_piece *pipe_title;
  res.working = tp_split_youtube_pipes(sk, c, &junk, &pipe_title);
  res.pipe_title = pipe_title;
  res.junk = junk;
  return res;
}

/* pieceOf is the rendered text of a segment with its absolute offset. */
TP_PRIVATE tp_piece tp_piece_of(tp_segment seg) {
  tp_piece p;
  p.text = tp_runes_of(tp_render_skel(&seg.skel));
  p.base = tp_start_pos(&seg.skel, 0);
  return p;
}

/* Go closure pushOther of splitYoutubePipes: every remaining segment but keep becomes "other"
 * junk. */
static void push_other(tp_segment_vec remaining, ptrdiff_t keep, tp_positioned_junk_vec *junk) {
  for (size_t i = 0; i < remaining.n; i++) {
    if ((ptrdiff_t)i == keep) continue;
    tp_positioned_junk j;
    j.raw = tp_collapse_spaces(tp_render_skel(&remaining.p[i].skel));
    j.kind = TP_JUNK_KIND_OTHER;
    j.pos = tp_start_pos(&remaining.p[i].skel, 0);
    tp_push(*junk, j);
  }
}

/* splitYoutubePipes is R2.4. */
TP_PRIVATE tp_piece tp_split_youtube_pipes(tp_skel sk, tp_ctx *c, tp_positioned_junk_vec *junk,
                                           const tp_piece **pipe_title) {
  *pipe_title = NULL;
  tp_segment_vec segments = tp_split_pipes(sk);
  if (segments.n <= 1) {
    tp_piece p;
    p.text = tp_runes_of(tp_render_skel(&sk));
    p.base = 0;
    return p;
  }

  tp_segment_vec remaining = {0};
  for (size_t i = 0; i < segments.n; i++) {
    tp_segment seg = segments.p[i];
    tp_str text = tp_collapse_spaces(tp_render_skel(&seg.skel));
    /* R2.4 step 1: a segment with a spaced dash is an artist/title candidate, never junk. */
    tp_str kind = TP_EMPTY;
    bool is_junk = false;
    if (!tp_has_spaced_dash(seg.skel.text)) is_junk = tp_junk_text_kind(text, c, &kind);
    if (is_junk && kind.n > 0) {
      tp_positioned_junk j;
      j.raw = text;
      j.kind = kind;
      j.pos = tp_start_pos(&seg.skel, 0);
      tp_push(*junk, j);
    } else if (text.n > 0) {
      tp_push(remaining, seg);
    }
  }

  for (size_t i = 0; i < remaining.n; i++) {
    if (tp_has_spaced_dash(remaining.p[i].skel.text)) {
      push_other(remaining, (ptrdiff_t)i, junk);
      return tp_piece_of(remaining.p[i]);
    }
  }
  if (remaining.n == 2) {
    tp_ctx_warn(c, TP_WARNING_AMBIGUOUS_SEPARATOR);
    tp_piece *title = tp_alloc(sizeof *title);
    *title = tp_piece_of(remaining.p[1]);
    *pipe_title = title;
    return tp_piece_of(remaining.p[0]);
  }
  if (remaining.n > 0) {
    push_other(remaining, 0, junk);
    return tp_piece_of(remaining.p[0]);
  }
  /* Every segment was junk: nothing left to parse. */
  tp_piece empty;
  memset(&empty, 0, sizeof empty);
  return empty;
}
