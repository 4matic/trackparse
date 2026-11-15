/* parser.c: the pipeline R1, R2, R4, R3/R6, R7, R8, R9, plus R10 runArtists (go/parser.go,
 * js/src/parser.ts). The engine itself (options, tables, known keys) is in context.c. */
#include "internal.h"

/* skelOf scans a piece and trims it, warning on unbalanced brackets (R3.3). */
TP_PRIVATE tp_skel tp_skel_of(tp_piece p, tp_ctx *c) {
  bool unbalanced;
  tp_skel sk = tp_scan(p.text, p.base, &unbalanced);
  if (unbalanced) tp_ctx_warn(c, TP_WARNING_UNBALANCED_BRACKETS);
  return tp_trim_skel(&sk);
}

/* uploaderSkel is R6.8: the uploader minus a trailing " - Topic" and "VEVO", or NULL. */
TP_PRIVATE const tp_skel *tp_uploader_skel(tp_str uploader) {
  if (uploader.n == 0) return NULL;
  tp_str u = tp_normalize(uploader);
  if (tp_has_suffix(u, TP_S(" - Topic"))) u = tp_trim_suffix(u, TP_S(" - Topic"));
  if (tp_has_suffix(u, TP_S("VEVO"))) u = tp_js_trim_end(tp_trim_suffix(u, TP_S("VEVO")));
  if (u.n == 0) return NULL;
  bool unbalanced;
  tp_skel *sk = tp_alloc(sizeof *sk);
  *sk = tp_scan(tp_runes_of(u), TP_UPLOADER_BASE, &unbalanced);
  return sk;
}

/* emptyTrack is the R9.5 result for an input that is empty after R1, and the start of every
 * other result. (Empty vectors are written as [] whatever their pointer.) */
TP_PRIVATE tp_track tp_empty_track(tp_str input, tp_mode mode) {
  tp_track t;
  memset(&t, 0, sizeof t);
  t.input = input;
  t.mode = mode;
  return t;
}

/* stage runs R1, R2 and R4. */
TP_PRIVATE tp_staged tp_engine_stage(const tp_engine *e, tp_str input) {
  tp_staged st;
  memset(&st, 0, sizeof st);
  tp_ctx *c = tp_engine_new_ctx(e);
  tp_str norm = tp_normalize(input);
  tp_prelude_result pre = tp_prelude(norm, e->opts.mode, c);
  tp_track track = tp_empty_track(input, pre.mode);
  st.e = e;
  st.c = c;
  st.pre = pre;
  if (norm.n == 0) { /* R9.5 */
    st.track = track;
    return st;
  }

  /* R4 */
  tp_prefix_result prefix = tp_strip_prefixes(pre.working.text, pre.working.base, pre.mode, c);
  track.timestamp = prefix.timestamp;
  track.position = prefix.position;
  tp_piece p;
  p.text = prefix.rest;
  p.base = prefix.base;
  tp_skel *working = tp_alloc(sizeof *working);
  *working = tp_skel_of(p, c);
  st.track = track;
  st.working = working;
  return st;
}

/* run is R1 to R9 on one (already sanitized) input; list_context is the R13.4.2 flag that only
 * matters in tracklist mode (R6.10). */
TP_PRIVATE tp_track tp_engine_run(const tp_engine *e, tp_str input, bool list_context) {
  return tp_finish(tp_engine_stage(e, input), list_context);
}

/* runArtists is R10 on one (already sanitized) input. */
TP_PRIVATE tp_artist_vec tp_engine_run_artists(const tp_engine *e, tp_str input) {
  tp_ctx *c = tp_engine_new_ctx(e);
  tp_str norm = tp_normalize(input);
  tp_artist_vec none = {0};
  if (norm.n == 0) return none;
  bool unbalanced;
  tp_skel sk = tp_scan(tp_runes_of(norm), 0, &unbalanced);
  tp_artist_side_result side = tp_parse_artist_side(&sk, c);
  tp_artist_vec artists = tp_dedup_artists(side.credits);
  for (size_t i = 0; i < artists.n; i++) artists.p[i].source = TP_SOURCE_ARTIST;
  return artists;
}

/* probe is R1, R2 and R4 only, as run does them: no artist or title parsing. finish completes
 * the parse from the same stage, so a probed line is not normalized twice. */
TP_PRIVATE tp_line_probe tp_engine_probe(const tp_engine *e, tp_str input) {
  tp_staged st = tp_engine_stage(e, input);
  tp_line_probe p;
  p.position = st.track.position != NULL;
  p.spaced_dash = st.pre.pipe_title == NULL && st.working != NULL &&
                  tp_split_at_spaced_dashes(st.working, true).n >= 2;
  p.empty = st.working == NULL;
  p.staged = st;
  return p;
}

/* finish is the parse of the probed input (R6 to R9 only). Call it once per probe: the probe's
 * per-call state (warnings) is consumed. */
TP_PRIVATE tp_track tp_line_probe_finish(tp_line_probe *p, bool list_context) {
  return tp_finish(p->staged, list_context);
}

/* byPos sorts items by offset, stably (like the JS sort). Go cmp.Compare on ints. */
static int cmp_pos(ptrdiff_t a, ptrdiff_t b) { return (a > b) - (a < b); }

static int cmp_version(const void *a, const void *b, void *ctx) {
  (void)ctx;
  return cmp_pos(((const tp_version_draft *)a)->pos, ((const tp_version_draft *)b)->pos);
}
static int cmp_credit(const void *a, const void *b, void *ctx) {
  (void)ctx;
  return cmp_pos(((const tp_credit *)a)->pos, ((const tp_credit *)b)->pos);
}
static int cmp_year(const void *a, const void *b, void *ctx) {
  (void)ctx;
  return cmp_pos(((const tp_positioned_year *)a)->pos, ((const tp_positioned_year *)b)->pos);
}
static int cmp_junk(const void *a, const void *b, void *ctx) {
  (void)ctx;
  return cmp_pos(((const tp_positioned_junk *)a)->pos, ((const tp_positioned_junk *)b)->pos);
}

TP_PRIVATE tp_version_draft_vec tp_by_pos_versions(tp_version_draft_vec items) {
  if (items.n > 1) tp_sort_stable(items.p, items.n, sizeof *items.p, cmp_version, NULL);
  return items;
}
TP_PRIVATE tp_credit_vec tp_by_pos_credits(tp_credit_vec items) {
  if (items.n > 1) tp_sort_stable(items.p, items.n, sizeof *items.p, cmp_credit, NULL);
  return items;
}
TP_PRIVATE tp_positioned_year_vec tp_by_pos_years(tp_positioned_year_vec items) {
  if (items.n > 1) tp_sort_stable(items.p, items.n, sizeof *items.p, cmp_year, NULL);
  return items;
}
TP_PRIVATE tp_positioned_junk_vec tp_by_pos_junk(tp_positioned_junk_vec items) {
  if (items.n > 1) tp_sort_stable(items.p, items.n, sizeof *items.p, cmp_junk, NULL);
  return items;
}

/* finish is R6 to R9 on a staged input. */
TP_PRIVATE tp_track tp_finish(tp_staged st, bool list_context) {
  tp_ctx *c = st.c;
  tp_prelude_result pre = st.pre;
  tp_track track = st.track;
  if (st.working == NULL) return track;
  tp_skel working = *st.working;

  tp_extracted out;
  memset(&out, 0, sizeof out);
  const tp_skel *artist_skel = NULL;
  tp_skel title_skel;
  bool relaxed;
  if (pre.pipe_title != NULL) {
    /* R2.4 step 3: "artist | title", R6 skipped. */
    title_skel = tp_skel_of(*pre.pipe_title, c);
    if (!tp_skel_is_empty(&working)) artist_skel = st.working;
    relaxed = artist_skel != NULL;
  } else {
    /* R8.7: a bullet suffix is cut before R6, which then sees only the head. */
    tp_skel head = working;
    tp_bullet_suffix bullet;
    bool cut = tp_cut_bullet_suffix(&working, c, &bullet);
    if (cut) {
      head = bullet.head;
      track.label = bullet.label;
      tp_append(out.junk, bullet.junk);
    }
    tp_separator_input in;
    in.skel = head;
    in.mode = pre.mode;
    in.uploader = tp_uploader_skel(st.e->opts.uploader);
    in.list_context = list_context;
    in.bullet_head = cut;
    tp_separator_result sep = tp_find_separator(in, c);
    tp_add_peeled(&out, sep.peeled);
    artist_skel = sep.artist;
    title_skel = sep.title;
    relaxed = sep.split;
  }

  tp_artist_side_result artist_side;
  memset(&artist_side, 0, sizeof artist_side);
  if (artist_skel != NULL) artist_side = tp_parse_artist_side(artist_skel, c);
  tp_title_side_result title_side =
      tp_parse_title_side(title_skel, relaxed, tp_str_eq(pre.mode, TP_MODE_YOUTUBE), &out, c);

  /* R9 */
  tp_version_draft_vec versions = tp_by_pos_versions(out.versions);
  for (size_t i = 0; i < versions.n; i++) {
    if (versions.p[i].ambiguous_mix) {
      tp_ctx_warn(c, TP_WARNING_AMBIGUOUS_MIX_CREDIT);
      break;
    }
  }
  tp_credit_vec credits = {0};
  tp_append(credits, artist_side.credits);
  tp_credit_vec title_credits = tp_by_pos_credits(out.credits);
  tp_append(credits, title_credits);
  track.artists = tp_dedup_artists(credits);
  track.title = title_side.title;
  memset(&track.versions, 0, sizeof track.versions);
  for (size_t i = 0; i < versions.n; i++) tp_push(track.versions, tp_to_version(versions.p[i]));
  track.full_title = tp_full_title_of(track.title, track.versions);
  /* R9.6: the first year in text order. */
  tp_positioned_year_vec years = {0};
  tp_append(years, artist_side.years);
  tp_append(years, out.years);
  years = tp_by_pos_years(years);
  if (years.n > 0) {
    ptrdiff_t *y = tp_alloc(sizeof *y);
    *y = years.p[0].year;
    track.year = y;
  }
  track.flags.explicit_ = out.explicit_ || artist_side.explicit_;
  track.flags.clean = out.clean || artist_side.clean;
  track.flags.unknown_artist = artist_side.unknown_artist || out.unknown_artist;
  track.flags.unknown_title = title_side.unknown_title;
  tp_positioned_junk_vec junk = {0};
  tp_append(junk, pre.junk);
  tp_append(junk, artist_side.junk);
  tp_append(junk, out.junk);
  junk = tp_by_pos_junk(junk);
  memset(&track.junk, 0, sizeof track.junk);
  for (size_t i = 0; i < junk.n; i++) {
    tp_junk j;
    j.raw = junk.p[i].raw;
    j.kind = junk.p[i].kind;
    tp_push(track.junk, j);
  }
  tp_clone(track.warnings, c->warnings);
  return track;
}
