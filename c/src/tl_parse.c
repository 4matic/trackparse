/* tl_parse.c: R13.1 to R13.8, parseTracklist (go/tl_parse.go, js/src/tracklist/parse.ts). */
#include "internal.h"

/* parseTracklist is R13.1 to R13.8 on one (already sanitized) text. */
TP_PRIVATE tp_tracklist tp_parse_tracklist(tp_str text, tp_resolved_tracklist o) {
  const tp_phrase_table *markers = tp_marker_table_for(o.marker_words);
  const tp_engine *e = tp_engine_new(o.parse);
  tp_tl_line_vec lines;
  tp_tl_row *rows;
  size_t nrows;
  ptrdiff_t candidates = 0, dashes = 0;
  bool list_context;
  tp_idxvec bare_numbers = {0};
  tp_bool_vec candidate_lines = {0};
  tp_bool_vec confirmed;
  tp_entry_vec entries = {0};
  tp_idxvec positions = {0};
  tp_bool_vec invalid = {0}, unparsed = {0};
  tp_warning_input in;
  tp_tracklist out;

  /* R13.2 time scan. R13.4.2: a candidate line has a time or a spaced dash in the skeleton R6
   * searches (after R2 and R4); the probe runs R1, R2 and R4 (R13.4.1). */
  lines = tp_split_lines(text);
  nrows = lines.n;
  rows = tp_alloc_array(nrows, sizeof *rows);
  for (size_t i = 0; i < nrows; i++) {
    tp_tl_row *r = &rows[i];
    tp_runes scanned;
    ptrdiff_t layered = 0;
    r->line = lines.p[i];
    r->scan = tp_scan_time(r->line.raw);
    r->blank = tp_is_blank(r->line.raw);
    scanned = tp_runes_of(r->scan.text);
    if (!r->blank) layered = tp_layered_prefix(scanned);
    r->text = tp_runes_str(tp_tail(scanned, layered));
    r->layered = layered > 0;
    if (r->blank) continue;
    r->probe = tp_engine_probe(e, r->text); /* R13.4.1 */
    r->probed = true;
    r->dash = r->probe.spaced_dash;
    r->candidate = r->scan.time != NULL || r->dash;
    /* R13.5 / R13.13.3: a layered line never has a bare position. */
    if (!r->layered) r->bare = tp_find_bare_candidate(r->text, r->probe.position);
    if (r->candidate) {
      candidates++;
      if (r->dash) dashes++;
    }
  }
  list_context = candidates > 0 && 2 * dashes >= candidates;

  /* R13.5: confirmed bare positions are removed before parsing. R13.4.1: one parse per line.
   * R13.13.3: layered lines are not neighbours for the R13.5 search. */
  for (size_t i = 0; i < nrows; i++) {
    tp_push(bare_numbers, rows[i].bare ? rows[i].bare->number : -1);
    tp_push(candidate_lines, rows[i].candidate && !rows[i].layered);
  }
  confirmed = tp_confirm_positions(bare_numbers, candidate_lines);
  for (size_t i = 0; i < nrows; i++) {
    tp_tl_row *r = &rows[i];
    const tp_bare_candidate *bare = NULL;
    tp_str input;
    tp_line_probe *probe;
    tp_track *track;
    if (!r->probed) continue;
    if (tp_flag_at(confirmed, (ptrdiff_t)i)) bare = r->bare;
    /* The probe already ran R1, R2 and R4; finish completes the parse from it. A line that
     * loses a bare position is probed again on the stripped text, so each probe is finished
     * exactly once. */
    input = r->text;
    probe = &r->probe;
    if (bare) {
      input = bare->stripped;
      probe = tp_alloc(sizeof *probe);
      *probe = tp_engine_probe(e, input);
    }
    r->marker = tp_has_marker_word(input, markers);
    track = tp_alloc(sizeof *track);
    /* R13.4.2: the R6.10 tight dash only on timed lines without a marker word. */
    *track = tp_line_probe_finish(probe, list_context && r->scan.time != NULL && !r->marker);
    if (bare) {
      tp_position *p = tp_alloc(sizeof *p);
      p->raw = bare->raw;
      p->number = bare->number;
      track->position = p;
    }
    r->separated = tp_has_separator(track, probe->empty);
    r->track = track;
  }

  /* R13.3 */
  for (size_t i = 0; i < nrows; i++) {
    const tp_tl_row *r = &rows[i];
    tp_entry_kind kind = tp_kind_of(r);
    const tp_track *track = NULL;
    tp_entry en;
    if (tp_str_eq(kind, TP_ENTRY_KIND_TRACK) || tp_str_eq(kind, TP_ENTRY_KIND_MARKER)) {
      track = r->track;
    }
    tp_push(positions, track && track->position ? track->position->number : -1);
    tp_push(invalid, r->scan.invalid);
    /* R13.7 line-unparsed: a track entry whose track part has no separator (R13.3). */
    tp_push(unparsed, tp_str_eq(kind, TP_ENTRY_KIND_TRACK) && !r->separated);
    memset(&en, 0, sizeof en);
    en.line = (ptrdiff_t)i + 1;
    en.kind = kind;
    en.eol = r->line.eol;
    en.raw = r->line.raw;
    en.parts = r->scan.parts;
    en.time = r->scan.time;
    en.track = track;
    en.layered = r->layered;
    tp_push(entries, en);
  }

  memset(&in, 0, sizeof in);
  in.entries = entries;
  in.positions = positions;
  in.invalid = invalid;
  in.unparsed = unparsed;
  in.audio_seconds = o.audio_seconds;
  memset(&out, 0, sizeof out);
  out.spec_version = TP_S(TP_SPEC_VERSION);
  out.entries = entries;
  out.warnings = tp_tracklist_warnings(&in);
  out.stats = tp_stats_of(entries);
  return out;
}

/* kindOf is R13.3. */
TP_PRIVATE tp_entry_kind tp_kind_of(const tp_tl_row *r) {
  bool marker;
  if (r->blank || r->track == NULL) return TP_ENTRY_KIND_BLANK;
  marker = !r->separated && r->marker;
  if (r->scan.time != NULL) return marker ? TP_ENTRY_KIND_MARKER : TP_ENTRY_KIND_TRACK;
  if (tp_is_link(r->text)) return TP_ENTRY_KIND_LINK;
  if (tp_is_header(r->text)) return TP_ENTRY_KIND_HEADER;
  if (marker) return TP_ENTRY_KIND_MARKER;
  if (r->separated) return TP_ENTRY_KIND_TRACK;
  return TP_ENTRY_KIND_NOTE;
}

/* statsOf counts the entries. */
TP_PRIVATE tp_stats tp_stats_of(tp_entry_vec entries) {
  tp_stats s;
  memset(&s, 0, sizeof s);
  s.lines = (ptrdiff_t)entries.n;
  for (size_t i = 0; i < entries.n; i++) {
    const tp_entry *e = &entries.p[i];
    if (tp_str_eq(e->kind, TP_ENTRY_KIND_TRACK)) {
      s.tracks++;
    } else if (tp_str_eq(e->kind, TP_ENTRY_KIND_MARKER)) {
      s.markers++;
    }
    if (e->time) {
      double *last = tp_alloc(sizeof *last);
      s.timed++;
      *last = e->time->seconds;
      if (s.first_time == NULL) {
        double *first = tp_alloc(sizeof *first);
        *first = e->time->seconds;
        s.first_time = first;
      }
      s.last_time = last;
    }
  }
  return s;
}
