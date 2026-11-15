/* tables.c: vocabulary tables compiled from spec/data (data_gen.c), optionally extended by
 * options.keywords (R12) (go/tables.go).
 *
 * The default tables are built once and shared; tables with keywords are built per Parser.
 * Tables are read-only after building, so they are safe for concurrent use. The maps in them are
 * only looked up, never iterated. */
#include "internal.h"

TP_PRIVATE bool tp_tables_tight_joiner(const tp_tables *t, tp_str raw, tp_str *canonical) {
  for (size_t i = t->tight_joiners.n; i > 0; i--) {
    if (tp_str_eq(t->tight_joiners.p[i - 1].key, raw)) {
      *canonical = t->tight_joiners.p[i - 1].value;
      return true;
    }
  }
  return false;
}

TP_PRIVATE tp_strvec tp_data_strs(const tp_str *p, size_t n) {
  tp_strvec v;
  /* Read-only view: cap == n, so any append copies first and never writes p. */
  memcpy(&v.p, &p, sizeof v.p);
  v.n = n;
  v.cap = n;
  return v;
}

/* lowerKeys is JS `asciiLower(s).trim()` of each entry, empty ones dropped. */
TP_PRIVATE tp_strvec tp_lower_keys(tp_strvec list) {
  tp_strvec out = {0};
  for (size_t i = 0; i < list.n; i++) {
    tp_str k = tp_js_trim(tp_ascii_lower(list.p[i]));
    if (k.n != 0) tp_push(out, k);
  }
  return out;
}

/* dataEntries are the pairs of a data object in JS Object.entries order. */
TP_PRIVATE tp_kv_vec tp_data_entries(const tp_data_kv *list, size_t n) {
  tp_kv_vec pairs = {0};
  tp_kv *dst = tp_push_n(pairs, n);
  for (size_t i = 0; i < n; i++) {
    dst[i].key = list[i].key;
    dst[i].value = list[i].value;
  }
  return tp_js_entries(pairs);
}

/* markerTable is a set-like phrase table of base plus extra. */
TP_PRIVATE const tp_phrase_table *tp_marker_table(tp_strvec base, tp_strvec extra) {
  tp_phrase_table *t = tp_new_phrase_table();
  for (size_t i = 0; i < base.n; i++) tp_phrase_table_add(t, base.p[i], NULL);
  for (size_t i = 0; i < extra.n; i++) tp_phrase_table_add(t, extra.p[i], NULL);
  return t;
}

static const tp_str *tables_str_ptr(tp_str s) {
  tp_str *p = tp_alloc(sizeof *p);
  *p = s;
  return p;
}

/* kindTable is a phrase table of string values. */
TP_PRIVATE const tp_phrase_table *tp_kind_table(const tp_kv_vec *entries, size_t count) {
  tp_phrase_table *t = tp_new_phrase_table();
  for (size_t l = 0; l < count; l++) {
    for (size_t i = 0; i < entries[l].n; i++) {
      tp_phrase_table_add(t, entries[l].p[i].key, tables_str_ptr(entries[l].p[i].value));
    }
  }
  return t;
}

/* The state of the Go closure note: a JS Map in insertion order (the first note of a key fixes
 * its place, later notes merge into it). */
typedef TP_VEC(tp_run_info) run_info_vec;
typedef struct run_notes {
  tp_strvec keys;
  tp_map index;
  run_info_vec infos;
} run_notes;

/* note: the run info of key, created on first use. Valid until the next note. */
static tp_run_info *note(run_notes *r, tp_str key) {
  tp_str k = tp_js_trim(tp_ascii_lower(key));
  int64_t i;
  if (!tp_map_get(&r->index, k, &i)) {
    tp_run_info zero;
    memset(&zero, 0, sizeof zero);
    i = (int64_t)r->infos.n;
    tp_map_put(&r->index, k, i);
    tp_push(r->keys, k);
    tp_push(r->infos, zero);
  }
  return &r->infos.p[(size_t)i];
}

static tp_word_set word_set(const tp_str *words, size_t n) {
  tp_word_set s;
  memset(&s, 0, sizeof s);
  tp_word_set_add_all(&s, words, n);
  return s;
}

static int by_utf16_len_desc(const void *a, const void *b, void *ctx) {
  ptrdiff_t la = tp_utf16_len_string(*(const tp_str *)a);
  ptrdiff_t lb = tp_utf16_len_string(*(const tp_str *)b);
  (void)ctx;
  return lb < la ? -1 : (lb > la ? 1 : 0);
}

TP_PRIVATE tp_tables *tp_build_tables(const tp_keyword_lists *kw) {
  tp_keyword_lists none;
  tp_tables *t = tp_alloc(sizeof *t);
  tp_strvec extra_feat, genres, descriptors, suffixes;
  tp_kv_vec junk_entries, genre_entries = {0}, head_entries, both[2], prefix_entries;
  run_notes notes;
  tp_phrase_table *run_words;
  tp_strvec empty = {0};

  if (kw == NULL) {
    memset(&none, 0, sizeof none);
    kw = &none;
  }
  extra_feat = tp_lower_keys(kw->feat_markers);

  junk_entries = tp_data_entries(tp_data_junk_phrases, TP_DATA_JUNK_PHRASES_N);
  tp_append(junk_entries, kw->junk);
  tp_clone(genres, tp_data_strs(tp_data_genres_genres, TP_DATA_GENRES_GENRES_N));
  {
    tp_strvec extra = tp_lower_keys(kw->genres);
    tp_append(genres, extra);
  }
  for (size_t i = 0; i < genres.n; i++) {
    tp_kv e;
    e.key = genres.p[i];
    e.value = TP_JUNK_KIND_GENRE;
    tp_push(genre_entries, e);
  }

  head_entries = tp_data_entries(tp_data_version_keywords_heads, TP_DATA_VERSION_KEYWORDS_HEADS_N);
  tp_append(head_entries, kw->version_heads);

  memset(&notes, 0, sizeof notes);
  {
    tp_kv_vec capable = tp_data_entries(tp_data_version_keywords_type_capable_modifiers,
                                        TP_DATA_VERSION_KEYWORDS_TYPE_CAPABLE_MODIFIERS_N);
    for (size_t i = 0; i < capable.n; i++) {
      note(&notes, capable.p[i].key)->type_capable = tables_str_ptr(capable.p[i].value);
    }
  }
  tp_clone(descriptors,
           tp_data_strs(tp_data_descriptors_descriptors, TP_DATA_DESCRIPTORS_DESCRIPTORS_N));
  {
    tp_strvec extra = tp_lower_keys(kw->descriptors);
    tp_append(descriptors, extra);
  }
  for (size_t i = 0; i < descriptors.n; i++) {
    tp_str d = descriptors.p[i];
    const tp_str *value = tables_str_ptr(tp_js_trim(tp_ascii_lower(d)));
    note(&notes, d)->descriptor = value;
  }
  for (size_t i = 0; i < genres.n; i++) note(&notes, genres.p[i])->genre = true;
  for (size_t i = 0; i < head_entries.n; i++) {
    note(&notes, head_entries.p[i].key)->head = tables_str_ptr(head_entries.p[i].value);
  }
  run_words = tp_new_phrase_table();
  for (size_t i = 0; i < notes.keys.n; i++) {
    /* The infos vector no longer grows, so its elements are stable. */
    tp_phrase_table_add(run_words, notes.keys.p[i], &notes.infos.p[i]);
  }

  for (size_t i = 0; i < TP_DATA_JOINERS_SPACED_N; i++) {
    const tp_data_joiner *j = &tp_data_joiners_spaced[i];
    tp_spaced_joiner s;
    s.raw = j->raw;
    s.canonical = j->canonical;
    s.is_and = tp_str_eq(j->kind, TP_S("and"));
    s.case_sensitive = j->case_sensitive;
    tp_push(t->spaced_joiners, s);
  }
  for (size_t i = 0; i < TP_DATA_JOINERS_TIGHT_N; i++) {
    tp_kv e;
    e.key = tp_data_joiners_tight[i].raw;
    e.value = tp_data_joiners_tight[i].canonical;
    tp_push(t->tight_joiners, e);
  }
  tp_clone(suffixes,
           tp_data_strs(tp_data_platform_suffixes_suffixes, TP_DATA_PLATFORM_SUFFIXES_SUFFIXES_N));
  /* JS sorts by UTF-16 length, longest first; the sort is stable. */
  tp_sort_stable(suffixes.p, suffixes.n, sizeof *suffixes.p, by_utf16_len_desc, NULL);

  t->feat_markers = tp_marker_table(
      tp_data_strs(tp_data_feat_markers_markers, TP_DATA_FEAT_MARKERS_MARKERS_N), extra_feat);
  t->bracket_only_feat = tp_marker_table(
      tp_data_strs(tp_data_feat_markers_bracket_only, TP_DATA_FEAT_MARKERS_BRACKET_ONLY_N), empty);
  t->producer_markers = tp_marker_table(
      tp_data_strs(tp_data_feat_markers_producer, TP_DATA_FEAT_MARKERS_PRODUCER_N), empty);
  t->title_feat_markers = tp_marker_table(
      tp_data_strs(tp_data_feat_markers_title_markers, TP_DATA_FEAT_MARKERS_TITLE_MARKERS_N),
      extra_feat);
  t->title_producer_markers = tp_marker_table(
      tp_data_strs(tp_data_feat_markers_title_producer, TP_DATA_FEAT_MARKERS_TITLE_PRODUCER_N),
      empty);
  both[0] = junk_entries; /* junk first: it wins over a genre */
  both[1] = genre_entries;
  t->junk_or_genre = tp_kind_table(both, 2);
  t->junk_phrases = tp_kind_table(&junk_entries, 1);
  t->junk_connectors = word_set(tp_data_junk_connectors, TP_DATA_JUNK_CONNECTORS_N);
  t->label_suffixes = word_set(tp_data_junk_label_suffixes, TP_DATA_JUNK_LABEL_SUFFIXES_N);
  t->heads = tp_kind_table(&head_entries, 1);
  t->generic_head_types =
      word_set(tp_data_version_keywords_generic_heads, TP_DATA_VERSION_KEYWORDS_GENERIC_HEADS_N);
  t->run_words = run_words;
  prefix_entries = tp_data_entries(tp_data_version_keywords_prefix_forms,
                                   TP_DATA_VERSION_KEYWORDS_PREFIX_FORMS_N);
  t->prefix_forms = tp_kind_table(&prefix_entries, 1);
  t->feat_canonical = TP_DATA_JOINERS_FEAT_CANONICAL;
  t->no_split_before = word_set(tp_data_no_split_before_words, TP_DATA_NO_SPLIT_BEFORE_WORDS_N);
  t->stopwords = word_set(tp_data_stopwords_words, TP_DATA_STOPWORDS_WORDS_N);
  t->unknown_case_sensitive =
      word_set(tp_data_unknown_tokens_case_sensitive, TP_DATA_UNKNOWN_TOKENS_CASE_SENSITIVE_N);
  t->unknown_case_insensitive =
      word_set(tp_data_unknown_tokens_case_insensitive, TP_DATA_UNKNOWN_TOKENS_CASE_INSENSITIVE_N);
  t->extensions = word_set(tp_data_extensions_extensions, TP_DATA_EXTENSIONS_EXTENSIONS_N);
  t->platform_suffixes = suffixes;
  return t;
}

static void *build_default_tables(void) { return tp_build_tables(NULL); }

static tp_once default_tables_once = TP_ONCE_INIT;

/* defaultTables are the tables without keywords, built once. */
TP_PRIVATE const tp_tables *tp_default_tables(void) {
  const tp_tables *t = tp_once_get(&default_tables_once, build_default_tables);
  if (t == NULL) t = tp_build_tables(NULL);
  return t;
}

TP_PRIVATE const tp_tables *tp_tables_for(const tp_keyword_lists *kw) {
  if (kw == NULL) return tp_default_tables();
  return tp_build_tables(kw);
}

/* isUnknownToken is R7.5 / R8.6: an unknown placeholder name (ID, ???, Untitled...). */
TP_PRIVATE bool tp_is_unknown_token(tp_str text, const tp_tables *t) {
  return tp_word_set_has(&t->unknown_case_sensitive, text) ||
         tp_word_set_has(&t->unknown_case_insensitive, tp_ascii_lower(text));
}
