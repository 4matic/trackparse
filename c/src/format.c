/* format.c: R11 render a parsed track back to a string, and allArtists (go/format.go,
 * js/src/format.ts). R11 always reads the default tables: keyword options are parse options and
 * do not reach it. */
#include "internal.h"

/* isFeatMarker reports whether raw, as one word, is a feat marker. */
TP_PRIVATE bool tp_is_feat_marker(tp_str raw, const tp_tables *t) {
  tp_str one[1];
  one[0] = raw;
  tp_strvec w;
  w.p = one;
  w.n = 1;
  w.cap = 1;
  tp_phrase_match m;
  if (tp_phrase_table_match_at(t->feat_markers, w, 0, &m)) return true;
  return tp_phrase_table_match_at(t->bracket_only_feat, w, 0, &m);
}

/* canonicalJoiner is R11.5: map a raw joiner to its canonical form through data/joiners.json;
 * feat markers become feat_canonical. A featured list's lead marker is always a feat marker
 * (with becomes feat.), while between names the joiner table wins (Sigala with Ella Eyre keeps
 * with). */
TP_PRIVATE tp_str tp_canonical_joiner(tp_str raw, const tp_tables *t, bool feat_lead) {
  if (feat_lead && tp_is_feat_marker(raw, t)) return t->feat_canonical;
  tp_str tight;
  if (tp_tables_tight_joiner(t, raw, &tight)) return tight;
  for (size_t i = 0; i < t->spaced_joiners.n; i++) {
    const tp_spaced_joiner *j = &t->spaced_joiners.p[i];
    bool matches = tp_str_eq(raw, j->raw);
    if (!j->case_sensitive) matches = tp_word_matches(raw, j->raw);
    if (matches) return j->canonical;
  }
  if (tp_is_feat_marker(raw, t)) return t->feat_canonical;
  return raw;
}

/* joinSep is R11.1: " " + joiner + " "; tight joiners as the raw character + " "; a missing
 * joiner as &. */
TP_PRIVATE tp_str tp_join_sep(const tp_str *joiner, const tp_resolved_format *o,
                              const tp_tables *t) {
  tp_str j = TP_S("&");
  if (joiner != NULL) {
    j = *joiner;
    if (o->canonical) j = tp_canonical_joiner(j, t, false);
  }
  tp_str tight;
  if (tp_tables_tight_joiner(t, j, &tight)) return tp_concat(j, TP_S(" "));
  return tp_concat3(TP_S(" "), j, TP_S(" "));
}

/* joinNames is the names of one credit list joined by their own joiners (the first joiner is
 * not rendered). */
TP_PRIVATE tp_str tp_join_names(tp_artist_ptr_vec list, const tp_resolved_format *o,
                                const tp_tables *t) {
  tp_buf b = {0};
  for (size_t i = 0; i < list.n; i++) {
    if (i > 0) tp_buf_str(&b, tp_join_sep(list.p[i]->joiner, o, t));
    tp_buf_str(&b, list.p[i]->name);
  }
  return tp_buf_done(&b);
}

/* leadMarker is the marker in front of a featured list. */
TP_PRIVATE tp_str tp_lead_marker(const tp_artist *first, const tp_resolved_format *o,
                                 const tp_tables *t, tp_str fallback) {
  if (o->feat_marker != NULL) return *o->feat_marker;
  tp_str raw = fallback;
  if (first->joiner != NULL) raw = *first->joiner;
  if (o->canonical) return tp_canonical_joiner(raw, t, true);
  return raw;
}

/* withRole is the artists of track with role, in order. */
TP_PRIVATE tp_artist_ptr_vec tp_with_role(tp_artist_vec artists, tp_role role) {
  tp_artist_ptr_vec out = {0};
  for (size_t i = 0; i < artists.n; i++) {
    if (tp_str_eq(artists.p[i].role, role)) tp_push(out, (const tp_artist *)&artists.p[i]);
  }
  return out;
}

/* formatTrack is R11: track rendered back to a string with the resolved format options. */
TP_PRIVATE tp_str tp_format_track(const tp_track *track, tp_resolved_format o) {
  const tp_tables *t = tp_default_tables();
  tp_artist_ptr_vec primaries = tp_with_role(track->artists, TP_ROLE_PRIMARY);
  tp_artist_ptr_vec featured = tp_with_role(track->artists, TP_ROLE_FEATURED);
  tp_artist_ptr_vec producers = tp_with_role(track->artists, TP_ROLE_PRODUCER);

  tp_artist_ptr_vec artist_feat = {0}, title_feat = {0};
  switch (o.feat) {
  case TP_FEAT_MODE_SOURCE:
    for (size_t i = 0; i < featured.n; i++) {
      if (tp_str_eq(featured.p[i]->source, TP_SOURCE_ARTIST)) {
        tp_push(artist_feat, featured.p[i]);
      } else {
        tp_push(title_feat, featured.p[i]);
      }
    }
    break;
  case TP_FEAT_MODE_ARTIST:
    artist_feat = featured;
    break;
  case TP_FEAT_MODE_TITLE:
    title_feat = featured;
    break;
  case TP_FEAT_MODE_OMIT:
    break;
  }

  /* R11.1 / R11.2 */
  tp_str artist_part = tp_join_names(primaries, &o, t);
  if (artist_feat.n > 0) {
    tp_str lead = tp_lead_marker(artist_feat.p[0], &o, t, t->feat_canonical);
    tp_str names = tp_join_names(artist_feat, &o, t);
    if (artist_part.n == 0) {
      artist_part = tp_concat3(lead, TP_S(" "), names);
    } else {
      tp_buf b = {0};
      tp_buf_str(&b, artist_part);
      tp_buf_byte(&b, ' ');
      tp_buf_str(&b, lead);
      tp_buf_byte(&b, ' ');
      tp_buf_str(&b, names);
      artist_part = tp_buf_done(&b);
    }
  }

  /* R11.3 */
  tp_strvec pieces = {0};
  if (track->title.n > 0) tp_push(pieces, track->title);
  if (title_feat.n > 0) {
    tp_str lead = tp_lead_marker(title_feat.p[0], &o, t, t->feat_canonical);
    tp_buf b = {0};
    tp_buf_byte(&b, '(');
    tp_buf_str(&b, lead);
    tp_buf_byte(&b, ' ');
    tp_buf_str(&b, tp_join_names(title_feat, &o, t));
    tp_buf_byte(&b, ')');
    tp_push(pieces, tp_buf_done(&b));
  }
  if (o.versions) {
    for (size_t i = 0; i < track->versions.n; i++) {
      tp_push(pieces, tp_render_version(track->versions.p[i].raw, track->versions.p[i].delimiter));
    }
  }
  if (producers.n > 0 && o.producers) {
    tp_str marker = TP_S("prod.");
    if (producers.p[0]->joiner != NULL) marker = *producers.p[0]->joiner;
    tp_buf b = {0};
    tp_buf_byte(&b, '(');
    tp_buf_str(&b, marker);
    tp_buf_byte(&b, ' ');
    tp_buf_str(&b, tp_join_names(producers, &o, t));
    tp_buf_byte(&b, ')');
    tp_push(pieces, tp_buf_done(&b));
  }
  tp_str title_part = tp_join(pieces, TP_S(" "));

  /* R11.4 */
  tp_str result = title_part;
  if (artist_part.n > 0) result = tp_concat3(artist_part, TP_S(" - "), title_part);
  if (o.position && track->position != NULL) {
    result = tp_concat3(track->position->raw, TP_S(". "), result);
  }
  return result;
}

/* Go closure add of allArtists. */
typedef struct all_artists_state {
  tp_map seen;
  tp_artist_vec out;
} all_artists_state;

static void add_artist(all_artists_state *s, tp_artist a) {
  tp_str key = tp_dedup_key(a.name);
  if (tp_map_has(&s->seen, key)) return;
  tp_map_put(&s->seen, key, 1);
  tp_push(s->out, a);
}

/* allArtists is every credited name: the track's artists plus every version's remixers,
 * deduplicated by the R0.4 key, first occurrence kept. */
TP_PRIVATE tp_artist_vec tp_all_artists(const tp_track *track) {
  all_artists_state s;
  memset(&s, 0, sizeof s);
  for (size_t i = 0; i < track->artists.n; i++) add_artist(&s, track->artists.p[i]);
  for (size_t v = 0; v < track->versions.n; v++) {
    const tp_version *ver = &track->versions.p[v];
    for (size_t i = 0; i < ver->artists.n; i++) add_artist(&s, ver->artists.p[i]);
  }
  return s.out;
}
