/* context.c: the per-call parse context and the engine that holds what is computed once per
 * Parser (go/context.go). */
#include "internal.h"

TP_PRIVATE tp_engine *tp_engine_new(tp_resolved_options o) {
  tp_engine *e = tp_alloc(sizeof *e);
  ptrdiff_t max_len = 0;
  e->known_keys = tp_known_keys_of(o.known_artists, &max_len);
  e->opts = o;
  e->t = tp_tables_for(o.keywords);
  e->known_max_len = max_len;
  return e;
}

static void *build_shared_engine(void) {
  tp_resolved_options o;
  memset(&o, 0, sizeof o);
  return tp_engine_new(o);
}

static tp_once shared_engine_once = TP_ONCE_INIT;

/* sharedEngine is the engine behind Parse and ParseArtists without options. */
TP_PRIVATE const tp_engine *tp_shared_engine(void) {
  const tp_engine *e = tp_once_get(&shared_engine_once, build_shared_engine);
  if (e == NULL) e = build_shared_engine();
  return e;
}

TP_PRIVATE tp_ctx *tp_engine_new_ctx(const tp_engine *e) {
  tp_ctx *c = tp_alloc(sizeof *c);
  c->t = e->t;
  c->known_keys = &e->known_keys;
  c->known_max_len = e->known_max_len;
  c->split_and = e->opts.split_and;
  return c;
}

/* knownKeysOf is the knownArtists part of a context: the dedup keys and the longest entry in
 * UTF-16 units. */
TP_PRIVATE tp_word_set tp_known_keys_of(tp_strvec known, ptrdiff_t *max_len) {
  tp_word_set keys;
  memset(&keys, 0, sizeof keys);
  *max_len = 0;
  for (size_t i = 0; i < known.n; i++) {
    tp_str key = tp_dedup_key(known.p[i]);
    ptrdiff_t n;
    if (key.n != 0) tp_word_set_add(&keys, key);
    n = tp_utf16_len_string(known.p[i]);
    if (n > *max_len) *max_len = n;
  }
  return keys;
}

TP_PRIVATE void tp_ctx_warn(tp_ctx *c, tp_warning w) {
  if (tp_strvec_has(c->warnings, w)) return;
  tp_push(c->warnings, w);
}

/* matchKnownAt is R7.3a: the length (in code points) of the longest knownArtists entry matching
 * text at start (dedup-key comparison), followed by the end, whitespace, "," or ";" (or extra,
 * unless it is TP_NORUNE: R4.2 also accepts "-", so 3 Doors Down-Kryptonite is protected). 0 when
 * none matches. The search window is 2 * longest + 8 UTF-16 units, exactly as in the JS
 * reference. */
TP_PRIVATE ptrdiff_t tp_match_known_at(tp_runes text, ptrdiff_t start, tp_ctx *c, tp_rune extra) {
  ptrdiff_t n = (ptrdiff_t)text.n;
  ptrdiff_t budget, limit, used;
  if (c->known_keys == NULL || c->known_keys->m.n == 0 || start < 0) return 0;
  budget = c->known_max_len * 2 + 8;
  limit = start;
  used = 0;
  while (limit < n) {
    ptrdiff_t w = text.p[limit] >= 0x10000 ? 2 : 1;
    if (used + w > budget) break;
    used += w;
    limit++;
  }
  for (ptrdiff_t end = limit; end > start; end--) {
    tp_rune next = tp_at(text, end);
    bool boundary = end == n || next == ',' || next == ';' || tp_is_whitespace(next) ||
                    (extra != TP_NORUNE && next == extra);
    if (boundary) {
      /* The key only answers yes or no: give its memory back at once. Without this a long
       * input with a long known artist keeps every key (C port only; the GC ports free them). */
      tp_mark mark = tp_arena_mark();
      bool hit =
          tp_word_set_has(c->known_keys, tp_dedup_key(tp_runes_str(tp_sub(text, start, end))));
      tp_arena_release(mark);
      if (hit) return end - start;
    }
  }
  return 0;
}

TP_PRIVATE bool tp_is_known_artist(tp_str text, tp_ctx *c) {
  return c->known_keys != NULL && c->known_keys->m.n > 0 &&
         tp_word_set_has(c->known_keys, tp_dedup_key(text));
}
