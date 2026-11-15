/* classify.c: R5, classification of a group's inner text (also used for dash suffixes and pipe
 * segments) (go/classify.go). */
#include "internal.h"

static const tp_str explicit_flags[] = {
    {"explicit", 8}, {"explicit version", 16}, {"dirty", 5}, {"dirty version", 13}};
static const tp_str clean_flags[] = {{"clean", 5}, {"clean version", 13}, {"radio clean", 11}};

static tp_classification class_of(tp_class_kind kind) {
  tp_classification out;
  memset(&out, 0, sizeof out);
  out.kind = kind;
  return out;
}

static const tp_str *classify_str_ptr(tp_str s) {
  tp_str *p = tp_alloc(sizeof *p);
  *p = s;
  return p;
}

static const ptrdiff_t *int_ptr(ptrdiff_t v) {
  ptrdiff_t *p = tp_alloc(sizeof *p);
  *p = v;
  return p;
}

/* words[a:b] as a read-only view (cap == length, so an append copies). */
static tp_strvec words_view(tp_strvec words, ptrdiff_t a, ptrdiff_t b) {
  tp_strvec v;
  v.p = words.p + a;
  v.n = (size_t)(b - a);
  v.cap = v.n;
  return v;
}

/* classify is R5 on inner (already trimmed and whitespace-collapsed). */
TP_PRIVATE tp_classification tp_classify(tp_str inner, tp_site s, tp_ctx *c) {
  tp_strvec words = tp_split_words(inner);
  tp_str kind;
  tp_classification credit;
  tp_flag f;
  tp_version_draft_vec versions;
  if (words.n == 0) return class_of(TP_CLASS_UNKNOWN);
  if (tp_junk_kind_of(words, c, &kind)) {
    tp_classification out = class_of(TP_CLASS_JUNK);
    out.junk = kind;
    return out;
  }
  if (tp_feat_or_producer(inner, s, c, &credit)) return credit;
  if (tp_flag_of(words, &f)) {
    tp_classification out = class_of(TP_CLASS_FLAG);
    out.flag = f;
    return out;
  }
  if (words.n == 1 && tp_is_all_digits(words.p[0])) {
    int year;
    if (tp_year_of(words.p[0], &year)) {
      tp_classification out = class_of(TP_CLASS_YEAR);
      out.year = (ptrdiff_t)year;
      return out;
    }
  }
  if (tp_multi_version(inner, s, c, &versions) || tp_single_version(inner, s, c, &versions)) {
    tp_classification out = class_of(TP_CLASS_VERSIONS);
    out.versions = versions;
    return out;
  }
  return class_of(TP_CLASS_UNKNOWN);
}

/* ------------------------------------------------------------------------------------------ */
/* R5.1 Junk */

/* junkKindOf is R5.1: the junk kind if the whole word list is junk vocabulary. */
TP_PRIVATE bool tp_junk_kind_of(tp_strvec words, tp_ctx *c, tp_str *kind) {
  const tp_tables *t = c->t;
  tp_str first_kind = TP_EMPTY;
  bool found = false;
  ptrdiff_t n = (ptrdiff_t)words.n;
  ptrdiff_t i = 0;
  while (i < n) {
    tp_str w = words.p[i];
    tp_phrase_match m;
    int year;
    bool is_year;
    if (tp_phrase_table_match_at(t->junk_or_genre, words, i, &m)) {
      if (!found) {
        first_kind = *(const tp_str *)m.value;
        found = true;
      }
      i += m.length;
      continue;
    }
    /* R5.1: connectors and year words may sit between junk phrases: Official Video 2015. */
    is_year = tp_year_of(w, &year);
    if (tp_word_set_has(&t->junk_connectors, w) ||
        tp_word_set_has(&t->junk_connectors, tp_word_key(w)) || is_year) {
      i++;
      continue;
    }
    found = false;
    break;
  }
  /* JS truthiness: an empty kind (possible through keywords.junk) does not count. */
  if (found && first_kind.n != 0) {
    *kind = first_kind;
    return true;
  }
  if (n >= 2 && tp_in_vocab(words.p[n - 1], &t->label_suffixes)) {
    *kind = TP_JUNK_KIND_LABEL;
    return true;
  }
  return false;
}

/* ------------------------------------------------------------------------------------------ */
/* R5.2 Feat, R5.3 Producer */

TP_PRIVATE bool tp_feat_or_producer(tp_str inner, tp_site s, tp_ctx *c, tp_classification *out) {
  const tp_tables *t = c->t;
  tp_runes text = tp_runes_of(inner);
  tp_word_span_vec spans = tp_word_spans(text);
  tp_strvec words = tp_span_texts(spans);
  ptrdiff_t nwords = (ptrdiff_t)words.n;
  ptrdiff_t feat_len = 0, n;
  bool bracket_only = false;
  tp_phrase_match m, prod;
  if (tp_phrase_table_match_at(t->feat_markers, words, 0, &m)) {
    feat_len = m.length;
  } else if (tp_phrase_table_match_at(t->bracket_only_feat, words, 0, &m)) {
    feat_len = m.length;
    bracket_only = true;
  }
  if (feat_len > 0 && nwords > feat_len) {
    bool stop = bracket_only && feat_len < nwords && tp_in_vocab(words.p[feat_len], &t->stopwords);
    if (!stop) {
      tp_list_result r;
      if (!tp_credits_after(text, spans, feat_len, TP_ROLE_FEATURED, s, c, &r)) return false;
      *out = class_of(TP_CLASS_FEAT);
      out->list = r;
      return true;
    }
  }
  if (!tp_phrase_table_match_at(t->producer_markers, words, 0, &prod)) return false;
  n = prod.length;
  if (n < nwords && tp_str_eq(tp_word_key(words.p[n]), TP_S("by"))) n++;
  if (nwords > n) {
    tp_list_result r;
    if (!tp_credits_after(text, spans, n, TP_ROLE_PRODUCER, s, c, &r)) return false;
    *out = class_of(TP_CLASS_PRODUCER);
    out->list = r;
    return true;
  }
  return false;
}

TP_PRIVATE bool tp_credits_after(tp_runes inner, tp_word_span_vec spans, ptrdiff_t marker_words,
                                 tp_role role, tp_site s, tp_ctx *c, tp_list_result *out) {
  tp_word_span first, last_marker, first_name;
  tp_list_options opts;
  ptrdiff_t base;
  if (spans.n == 0 || marker_words < 1 || marker_words >= (ptrdiff_t)spans.n) return false;
  first = spans.p[0];
  last_marker = spans.p[marker_words - 1];
  first_name = spans.p[marker_words];
  memset(&opts, 0, sizeof opts);
  opts.role = role;
  opts.source = s.source;
  opts.lead = classify_str_ptr(tp_runes_str(tp_sub(inner, first.start, last_marker.end)));
  opts.split_with = false;
  opts.feat_markers_join = tp_str_eq(role, TP_ROLE_FEATURED);
  /* Offsets are UTF-16 units (see the scanner). */
  base = s.pos + 1 + tp_utf16_len(tp_sub(inner, 0, first_name.start));
  *out = tp_list_from_text(tp_tail(inner, first_name.start), base, c, &opts);
  return true;
}

/* listFromText splits a plain string (nested brackets protected by a fresh scan) as a credit
 * list. */
TP_PRIVATE tp_list_result tp_list_from_text(tp_runes text, ptrdiff_t base, tp_ctx *c,
                                            const tp_list_options *opts) {
  bool unbalanced;
  tp_skel s = tp_scan(text, base, &unbalanced);
  return tp_split_credits(&s, opts, c);
}

/* ------------------------------------------------------------------------------------------ */
/* R5.4 Flag */

TP_PRIVATE bool tp_flag_of(tp_strvec words, tp_flag *out) {
  tp_strvec keys = {0};
  tp_str key;
  for (size_t i = 0; i < words.n; i++) tp_push(keys, tp_word_key(words.p[i]));
  key = tp_join(keys, TP_S(" "));
  for (size_t i = 0; i < TP_COUNT(explicit_flags); i++) {
    if (tp_str_eq(key, explicit_flags[i])) {
      *out = TP_FLAG_EXPLICIT;
      return true;
    }
  }
  for (size_t i = 0; i < TP_COUNT(clean_flags); i++) {
    if (tp_str_eq(key, clean_flags[i])) {
      *out = TP_FLAG_CLEAN;
      return true;
    }
  }
  return false;
}

/* ------------------------------------------------------------------------------------------ */
/* R5.6 Multi-version */

/* splitMultiParts splits at top-level " / " or "; " (outside nested brackets). */
TP_PRIVATE tp_strvec tp_split_multi_parts(tp_runes inner) {
  tp_runes_vec parts = {0};
  tp_strvec out = {0};
  ptrdiff_t depth = 0, start = 0;
  for (ptrdiff_t i = 0; i < (ptrdiff_t)inner.n; i++) {
    tp_rune ch = inner.p[i];
    if (ch == '(' || ch == '[' || ch == '{') {
      depth++;
    } else if ((ch == ')' || ch == ']' || ch == '}') && depth > 0) {
      depth--;
    } else if (depth == 0 && ch == '/' && tp_before(inner, i) == ' ' &&
               tp_at(inner, i + 1) == ' ') {
      tp_push(parts, tp_sub(inner, start, i - 1));
      start = i + 2;
    } else if (depth == 0 && ch == ';' && tp_at(inner, i + 1) == ' ') {
      tp_push(parts, tp_sub(inner, start, i));
      start = i + 2;
    }
  }
  tp_push(parts, tp_tail(inner, start));
  for (size_t i = 0; i < parts.n; i++) tp_push(out, tp_js_trim(tp_runes_str(parts.p[i])));
  return out;
}

TP_PRIVATE bool tp_multi_version(tp_str inner, tp_site s, tp_ctx *c, tp_version_draft_vec *out) {
  tp_strvec parts = tp_split_multi_parts(tp_runes_of(inner));
  tp_version_draft_vec all = {0};
  if (parts.n < 2) return false;
  for (size_t i = 0; i < parts.n; i++) {
    tp_version_draft_vec vs;
    if (parts.p[i].n == 0) return false;
    if (!tp_single_version(parts.p[i], s, c, &vs)) return false;
    tp_append(all, vs);
  }
  *out = all;
  return true;
}

/* ------------------------------------------------------------------------------------------ */
/* R5.7 Version */

static tp_version_draft_vec one_version(tp_version_draft v) {
  tp_version_draft_vec out = {0};
  tp_push(out, v);
  return out;
}

TP_PRIVATE bool tp_single_version(tp_str raw, tp_site s, tp_ctx *c, tp_version_draft_vec *out) {
  tp_runes text = tp_runes_of(raw);
  tp_word_span_vec spans = tp_word_spans(text);
  tp_strvec words = tp_span_texts(spans);
  tp_version_draft v;
  if (tp_head_by(raw, text, spans, words, s, c, &v) || tp_head_scan(raw, words, s, c, &v) ||
      tp_prefix_form(raw, words, s, c, &v)) {
    *out = one_version(v);
    return true;
  }
  return false;
}

TP_PRIVATE tp_version_draft tp_base_version(tp_str raw, tp_str vtype, tp_site s) {
  tp_version_draft v;
  memset(&v, 0, sizeof v);
  v.vtype = vtype;
  v.raw = raw;
  v.delimiter = s.delimiter;
  v.pos = s.pos;
  return v;
}

TP_PRIVATE tp_list_options tp_remixer_opts(void) {
  tp_list_options o;
  memset(&o, 0, sizeof o);
  o.role = TP_ROLE_REMIXER;
  o.source = TP_SOURCE_VERSION;
  return o;
}

/* dedupRemixers is R9.2 for remixers: deduped within their own version only. */
TP_PRIVATE tp_credit_vec tp_dedup_remixers(tp_credit_vec credits) {
  tp_map seen;
  tp_credit_vec out = {0};
  memset(&seen, 0, sizeof seen);
  for (size_t i = 0; i < credits.n; i++) {
    tp_str key = tp_dedup_key(credits.p[i].name);
    if (tp_map_has(&seen, key)) continue;
    tp_map_put(&seen, key, 0);
    tp_push(out, credits.p[i]);
  }
  return out;
}

TP_PRIVATE void tp_set_credits(tp_version_draft *v, tp_runes text, tp_site s, tp_ctx *c) {
  tp_list_options opts = tp_remixer_opts();
  tp_list_result r = tp_list_from_text(text, s.pos, c, &opts);
  v->artists = tp_dedup_remixers(r.credits);
  if (r.unknown) v->unknown_artist = true;
  /* R7.5: a dropped year-only name sets the version's year if it is still null. */
  if (r.years.n > 0 && v->year == NULL) v->year = int_ptr(r.years.p[0].year);
}

/* headBy is R5.7.1: Remix by Skrillex. */
TP_PRIVATE bool tp_head_by(tp_str raw, tp_runes text, tp_word_span_vec spans, tp_strvec words,
                           tp_site s, tp_ctx *c, tp_version_draft *out) {
  tp_phrase_match head;
  tp_str by;
  tp_word_span rest;
  tp_version_draft v;
  if (!tp_phrase_table_match_at(c->t->heads, words, 0, &head) ||
      head.length + 1 >= (ptrdiff_t)spans.n || head.length >= (ptrdiff_t)words.n) {
    return false;
  }
  by = words.p[head.length];
  rest = spans.p[head.length + 1];
  if (!tp_str_eq(tp_word_key(by), TP_S("by"))) return false;
  v = tp_base_version(raw, *(const tp_str *)head.value, s);
  tp_set_credits(&v, tp_tail(text, rest.start), s, c);
  *out = v;
  return true;
}

/* collectRun walks left from end collecting the modifier run (R5.7.2). */
TP_PRIVATE tp_run_item_vec tp_collect_run(tp_strvec words, ptrdiff_t end, tp_ctx *c) {
  tp_run_item_vec items = {0};
  ptrdiff_t j = end;
  while (j > 0) {
    tp_phrase_match m;
    tp_run_item item;
    int year;
    memset(&item, 0, sizeof item);
    if (tp_phrase_table_match_ending(c->t->run_words, words, j, &m)) {
      item.start = j - m.length;
      item.end = j;
      item.info = *(const tp_run_info *)m.value;
      tp_push(items, item);
      j -= m.length;
      continue;
    }
    if (!tp_year_of(words.p[j - 1], &year)) break;
    item.start = j - 1;
    item.end = j;
    item.year = int_ptr((ptrdiff_t)year);
    tp_push(items, item);
    j--;
  }
  if (items.n > 1) {
    for (size_t a = 0, b = items.n - 1; a < b; a++, b--) {
      tp_run_item tmp = items.p[a];
      items.p[a] = items.p[b];
      items.p[b] = tmp;
    }
  }
  return items;
}

/* truthyName is a JS truthiness test on an optional type name: NULL and "" do not count. */
TP_PRIVATE bool tp_truthy_name(const tp_str *s, tp_str *out) {
  if (s == NULL || s->n == 0) return false;
  *out = *s;
  return true;
}

/* headScan is R5.7.2: Extended VIP Mix, Skrillex Remix, Taylor's Version. */
TP_PRIVATE bool tp_head_scan(tp_str raw, tp_strvec words, tp_site s, tp_ctx *c,
                             tp_version_draft *out) {
  const tp_tables *t = c->t;
  tp_phrase_match head;
  tp_str head_type, vtype, left_capable = TP_EMPTY, nearest_type = TP_EMPTY;
  ptrdiff_t head_start, credit_end, left, type_item = -1, nearest_head = -1;
  tp_run_item_vec run;
  tp_strvec credit_words, descriptors = {0};
  bool generic, via_mix = false, left_ok = false, nearest_ok = false;
  tp_version_draft v;

  if (!tp_phrase_table_match_ending(t->heads, words, (ptrdiff_t)words.n, &head)) return false;
  head_type = *(const tp_str *)head.value;
  head_start = (ptrdiff_t)words.n - head.length;
  run = tp_collect_run(words, head_start, c);
  credit_end = head_start;
  if (run.n > 0) credit_end = run.p[0].start;
  credit_words = words_view(words, 0, credit_end);
  left = (ptrdiff_t)run.n - 1; /* -1: no run */

  /* R5.7.2 type resolution. Generic = canonical type mix/edit/version, any spelling. */
  generic = tp_word_set_has(&t->generic_head_types, head_type);
  vtype = head_type;
  if (generic) nearest_head = tp_nearest_non_generic_head(run, c);
  if (left >= 0) left_ok = tp_truthy_name(run.p[left].info.type_capable, &left_capable);
  if (nearest_head >= 0) nearest_ok = tp_truthy_name(run.p[nearest_head].info.head, &nearest_type);
  if (generic && left_ok) {
    vtype = left_capable;
    type_item = left;
  } else if (nearest_ok) {
    /* 2011 Remastered Version: remaster */
    vtype = nearest_type;
    type_item = nearest_head;
  } else if (tp_str_eq(head_type, TP_S("mix")) && credit_words.n > 0) {
    vtype = TP_S("remix");
    via_mix = true;
  }
  v = tp_base_version(raw, vtype, s);

  for (size_t i = 0; i < run.n; i++) {
    const tp_run_item *item = &run.p[i];
    const tp_str *modifier;
    if (item->year != NULL) {
      if (v.year == NULL) v.year = int_ptr(*item->year);
      continue;
    }
    modifier = item->info.type_capable;
    if (modifier == NULL) modifier = item->info.descriptor;
    if (modifier != NULL) {
      if ((ptrdiff_t)i != type_item && !tp_contains_string(v.modifiers, *modifier)) {
        tp_push(v.modifiers, *modifier);
      }
    } else if (item->info.genre) {
      tp_push(descriptors, tp_join(words_view(words, item->start, item->end), TP_S(" ")));
    }
  }

  if (credit_words.n > 0) {
    tp_str credit = tp_join(credit_words, TP_S(" "));
    tp_str last_word = tp_ascii_lower(tp_last(credit_words));
    if (tp_is_unknown_token(credit, t)) {
      v.unknown_artist = true;
    } else if (tp_str_eq(vtype, TP_S("version")) || tp_str_eq(vtype, TP_S("cover")) ||
               tp_is_all_junk_phrases(credit_words, c)) {
      /* R5.7.2: Japanese Version, Taylor's Version, Adele Cover, Video Edit. */
      tp_strvec d = {0};
      tp_push(d, credit);
      tp_append(d, descriptors);
      descriptors = d;
    } else {
      tp_runes name = tp_runes_of(credit);
      size_t n;
      if (tp_has_suffix(last_word, TP_S("'s"))) name.n = name.n >= 2 ? name.n - 2 : 0;
      tp_set_credits(&v, name, s, c);
      n = v.artists.n;
      if (via_mix && n > 0 && tp_split_words(v.artists.p[n - 1].name).n >= 3) {
        v.ambiguous_mix = true;
      }
    }
  }
  if (descriptors.n > 0) v.descriptor = classify_str_ptr(tp_join(descriptors, TP_S(" ")));
  *out = v;
  return true;
}

/* containsString reports whether list holds s. */
TP_PRIVATE bool tp_contains_string(tp_strvec list, tp_str s) {
  for (size_t i = 0; i < list.n; i++) {
    if (tp_str_eq(list.p[i], s)) return true;
  }
  return false;
}

/* nearestNonGenericHead is R5.7.2 type rule 2: the index of the non-generic head in the run
 * nearest the (generic) head, or -1. */
TP_PRIVATE ptrdiff_t tp_nearest_non_generic_head(tp_run_item_vec run, tp_ctx *c) {
  for (ptrdiff_t i = (ptrdiff_t)run.n - 1; i >= 0; i--) {
    const tp_str *h = run.p[i].info.head;
    if (h != NULL && !tp_word_set_has(&c->t->generic_head_types, *h)) return i;
  }
  return -1;
}

/* isAllJunkPhrases is R5.7.2: the credit span is made entirely of junk phrases (Video in Video
 * Edit). */
TP_PRIVATE bool tp_is_all_junk_phrases(tp_strvec words, tp_ctx *c) {
  ptrdiff_t i = 0;
  while (i < (ptrdiff_t)words.n) {
    tp_phrase_match m;
    if (!tp_phrase_table_match_at(c->t->junk_phrases, words, i, &m)) return false;
    i += m.length;
  }
  return words.n > 0;
}

/* prefixForm is R5.7.3: Live at Wembley 1986, Remastered 2015, Sped Up. */
TP_PRIVATE bool tp_prefix_form(tp_str raw, tp_strvec words, tp_site s, tp_ctx *c,
                               tp_version_draft *out) {
  tp_phrase_match m;
  tp_version_draft v;
  tp_strvec rest = {0}, tail;
  if (!tp_phrase_table_match_at(c->t->prefix_forms, words, 0, &m)) return false;
  v = tp_base_version(raw, *(const tp_str *)m.value, s);
  v.prefix_form = true;
  tail = tp_tail_words(words, m.length);
  for (size_t i = 0; i < tail.n; i++) {
    tp_str w = tail.p[i];
    if (v.year == NULL) {
      int year;
      if (tp_year_of(w, &year)) {
        v.year = int_ptr((ptrdiff_t)year);
        continue;
      }
    }
    tp_push(rest, w);
  }
  if (rest.n > 0) v.descriptor = classify_str_ptr(tp_join(rest, TP_S(" ")));
  *out = v;
  return true;
}

/* tailWords is words[from:], or empty when from is past the end. */
TP_PRIVATE tp_strvec tp_tail_words(tp_strvec words, ptrdiff_t from) {
  tp_strvec none = {0};
  if (from < 0 || from >= (ptrdiff_t)words.n) return none;
  return words_view(words, from, (ptrdiff_t)words.n);
}
