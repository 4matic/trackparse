/* assemble.c: R9 assembly and dedup (go/assemble.go, js/src/assemble.ts). */
#include "internal.h"

/* precedence is the R9.2 role order: a later duplicate with a higher one upgrades the kept
 * entry. */
TP_PRIVATE ptrdiff_t tp_precedence(tp_role role) {
  if (tp_str_eq(role, TP_ROLE_PRIMARY)) return 2;
  if (tp_str_eq(role, TP_ROLE_FEATURED)) return 1;
  return 0;
}

/* toArtist is the public form of a credit. */
TP_PRIVATE tp_artist tp_to_artist(tp_credit c) {
  tp_artist a;
  a.name = c.name;
  a.role = c.role;
  a.joiner = c.joiner;
  a.source = c.source;
  return a;
}

/* The heap of list (Go l.heaps[list]), grown on demand. List ids are 0..ctx.list_seq-1; a
 * negative id (never produced) gets no heap. */
static tp_idxvec *heap_of(tp_credit_lists *l, ptrdiff_t list) {
  if (list < 0) return NULL;
  if ((size_t)list >= l->heaps.n) {
    size_t k = (size_t)list + 1 - l->heaps.n;
    (void)tp_push_n(l->heaps, k);
  }
  return &l->heaps.p[list];
}

/* creditLists holds the remaining names of each credit list, so "the list's first remaining
 * name" and "the next one" are cheap and dedup stays near-linear (O(n log n)) on inputs with
 * thousands of duplicate credits. Each list is a min-heap of entry indexes with lazy deletion.
 */
TP_PRIVATE tp_credit_lists *tp_new_credit_lists(tp_dedup_entry_vec entries) {
  tp_credit_lists *l = tp_alloc(sizeof *l);
  l->entries = entries;
  for (size_t i = 0; i < entries.n; i++) {
    tp_credit_lists_insert(l, entries.p[i].list, (ptrdiff_t)i);
  }
  return l;
}

/* member reports whether entry x is a remaining name of list. */
TP_PRIVATE bool tp_credit_lists_member(const tp_credit_lists *l, ptrdiff_t list, ptrdiff_t x) {
  return x >= 0 && x < (ptrdiff_t)l->entries.n && l->entries.p[x].alive &&
         l->entries.p[x].list == list;
}

/* insert puts entry e on the heap of list. */
TP_PRIVATE void tp_credit_lists_insert(tp_credit_lists *l, ptrdiff_t list, ptrdiff_t e) {
  tp_idxvec *h = heap_of(l, list);
  if (h == NULL) return;
  tp_push(*h, e);
  /* Sift up. */
  for (size_t i = h->n - 1; i > 0;) {
    size_t p = (i - 1) / 2;
    if (h->p[p] <= h->p[i]) break;
    ptrdiff_t tmp = h->p[p];
    h->p[p] = h->p[i];
    h->p[i] = tmp;
    i = p;
  }
}

/* pop removes the smallest index of list's heap. */
TP_PRIVATE void tp_credit_lists_pop(tp_credit_lists *l, ptrdiff_t list) {
  tp_idxvec *h = heap_of(l, list);
  if (h == NULL || h->n == 0) return;
  h->p[0] = h->p[h->n - 1];
  h->n--;
  /* Sift down. */
  for (size_t i = 0;;) {
    size_t a = 2 * i + 1, b = 2 * i + 2, m = i;
    if (a < h->n && h->p[a] < h->p[m]) m = a;
    if (b < h->n && h->p[b] < h->p[m]) m = b;
    if (m == i) break;
    ptrdiff_t tmp = h->p[m];
    h->p[m] = h->p[i];
    h->p[i] = tmp;
    i = m;
  }
}

/* first is the smallest remaining index of list after dropping stale heap entries, or -1. */
TP_PRIVATE ptrdiff_t tp_credit_lists_first(tp_credit_lists *l, ptrdiff_t list) {
  for (;;) {
    tp_idxvec *h = heap_of(l, list);
    if (h == NULL || h->n == 0) return -1;
    if (tp_credit_lists_member(l, list, h->p[0])) return h->p[0];
    tp_credit_lists_pop(l, list);
  }
}

/* heir is the entry that follows e in list when e is the list's first remaining name, else -1.
 * It must only be called when e is about to leave list: it takes e off the heap. */
TP_PRIVATE ptrdiff_t tp_credit_lists_heir(tp_credit_lists *l, ptrdiff_t list, ptrdiff_t e) {
  if (tp_credit_lists_first(l, list) != e) return -1;
  for (;;) {
    tp_idxvec *h = heap_of(l, list);
    if (h == NULL || h->n == 0) return -1;
    ptrdiff_t x = h->p[0];
    if (x != e && tp_credit_lists_member(l, list, x)) return x;
    tp_credit_lists_pop(l, list);
  }
}

/* passJoiner is R7.6: entry e leaves its list. If it was the list's first remaining name, the
 * next remaining name of that list takes joiner. */
TP_PRIVATE void tp_credit_lists_pass_joiner(tp_credit_lists *l, ptrdiff_t e, const tp_str *joiner) {
  if (e < 0 || e >= (ptrdiff_t)l->entries.n) return;
  ptrdiff_t next = tp_credit_lists_heir(l, l->entries.p[e].list, e);
  if (next >= 0) l->entries.p[next].a.joiner = joiner;
}

/* dedupKeyOf is the R9.2 key: producers are a separate credit class, deduped only among
 * themselves. */
TP_PRIVATE tp_str tp_dedup_key_of(const tp_artist *a) {
  tp_str cls = tp_str_eq(a->role, TP_ROLE_PRODUCER) ? TP_S("p:") : TP_S("c:");
  return tp_concat(cls, tp_dedup_key(a->name));
}

/* dedupArtists is R9.2: keep the first occurrence. A later lower/equal-precedence duplicate is
 * removed (R7.6 joiner inheritance); a later higher-precedence duplicate (role upgrade) gives the
 * kept entry its role, joiner and source, and the kept entry moves into that occurrence's list. */
TP_PRIVATE tp_artist_vec tp_dedup_artists(tp_credit_vec credits) {
  tp_dedup_entry_vec entries = {0};
  if (credits.n > 0) (void)tp_push_n(entries, credits.n);
  for (size_t i = 0; i < credits.n; i++) {
    entries.p[i].a = tp_to_artist(credits.p[i]);
    entries.p[i].list = credits.p[i].list;
    entries.p[i].alive = true;
  }
  tp_credit_lists *lists = tp_new_credit_lists(entries);
  tp_map index = {0};
  for (size_t i = 0; i < entries.n; i++) {
    tp_dedup_entry *e = &entries.p[i];
    tp_str key = tp_dedup_key_of(&e->a);
    int64_t k64;
    if (!tp_map_get(&index, key, &k64)) {
      tp_map_put(&index, key, (int64_t)i);
      continue;
    }
    ptrdiff_t k = (ptrdiff_t)k64;
    tp_dedup_entry *kept = &entries.p[k];
    if (tp_precedence(e->a.role) > tp_precedence(kept->a.role)) {
      tp_credit_lists_pass_joiner(lists, k, kept->a.joiner);
      kept->a.role = e->a.role;
      kept->a.joiner = e->a.joiner;
      kept->a.source = e->a.source;
      kept->list = e->list;
      tp_credit_lists_insert(lists, e->list, k);
    } else {
      tp_credit_lists_pass_joiner(lists, (ptrdiff_t)i, e->a.joiner);
    }
    e->alive = false;
  }
  tp_artist_vec out = {0};
  for (size_t i = 0; i < entries.n; i++) {
    if (entries.p[i].alive) tp_push(out, entries.p[i].a);
  }
  return out;
}

/* toVersion is the public form of a version draft. Version types are strings, so a type added
 * through keywords.versionHeads reaches the output as given. */
TP_PRIVATE tp_version tp_to_version(tp_version_draft v) {
  tp_version out;
  memset(&out, 0, sizeof out);
  for (size_t i = 0; i < v.artists.n; i++) tp_push(out.artists, tp_to_artist(v.artists.p[i]));
  tp_clone(out.modifiers, v.modifiers);
  out.type = v.vtype;
  out.raw = v.raw;
  out.descriptor = v.descriptor;
  out.year = v.year;
  out.unknown_artist = v.unknown_artist;
  out.delimiter = v.delimiter;
  return out;
}

/* renderVersion is R9.4: a version rendered with its delimiter. */
TP_PRIVATE tp_str tp_render_version(tp_str raw, tp_delimiter delimiter) {
  if (tp_str_eq(delimiter, TP_DELIMITER_PAREN)) return tp_concat3(TP_S("("), raw, TP_S(")"));
  if (tp_str_eq(delimiter, TP_DELIMITER_SQUARE)) return tp_concat3(TP_S("["), raw, TP_S("]"));
  if (tp_str_eq(delimiter, TP_DELIMITER_CURLY)) return tp_concat3(TP_S("{"), raw, TP_S("}"));
  return tp_concat(TP_S("- "), raw);
}

/* fullTitleOf is R9.4: the title followed by its versions as written, empty pieces skipped. */
TP_PRIVATE tp_str tp_full_title_of(tp_str title, tp_version_vec versions) {
  tp_strvec pieces = {0};
  if (title.n > 0) tp_push(pieces, title);
  for (size_t i = 0; i < versions.n; i++) {
    tp_str s = tp_render_version(versions.p[i].raw, versions.p[i].delimiter);
    if (s.n > 0) tp_push(pieces, s);
  }
  return tp_join(pieces, TP_S(" "));
}
