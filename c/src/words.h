/* words.h: low-level text helpers shared by every rule (go/words.go).
 *
 * The R0.2 whitespace set, R0.3 word keys, R0.4 dedup keys, R0.6 letters and digits,
 * token-sequence (phrase) matching, and the input sanitizer for invalid UTF-8.
 *
 * Text that is indexed is held as tp_runes, so indices are code-point indices (R0.5). Indices are
 * ptrdiff_t (Go int): they can be negative (tp_before(t, 0)) and out of range, and the helpers
 * clamp or return TP_NORUNE. Where the JS reference observes UTF-16 lengths (tp_utf16_len), that
 * is made explicit. Never index a string by byte inside a rule. The letter tests and the JS
 * lowercase/NFC/NFD functions live in unicode.h.
 */
#ifndef TP_WORDS_H
#define TP_WORDS_H

#include "rt.h"
#include "unicode.h"

/* t[i], or TP_NORUNE out of range (JS t[i] is undefined there). TP_NORUNE is not a valid code
 * point, so it never equals a character and fails every class test. */
TP_PRIVATE tp_rune tp_at(tp_runes t, ptrdiff_t i);
/* t[i-1], or TP_NORUNE when i is 0 or out of range. */
TP_PRIVATE tp_rune tp_before(tp_runes t, ptrdiff_t i);
/* t[a:b] with both ends clamped (JS slice with non-negative arguments). No copy. */
TP_PRIVATE tp_runes tp_sub(tp_runes t, ptrdiff_t a, ptrdiff_t b);
/* t[a:], clamped. */
TP_PRIVATE tp_runes tp_tail(tp_runes t, ptrdiff_t a);

/* R0.2: the exact whitespace code-point set. */
TP_PRIVATE bool tp_is_whitespace(tp_rune r);
/* The set of JS String.prototype.trim(): not the R0.2 set (U+FEFF is in, U+0085 is out). */
TP_PRIVATE bool tp_is_js_space(tp_rune r);
/* JS trim(), trimStart(), trimEnd(): substrings of s, no copy. */
TP_PRIVATE tp_str tp_js_trim(tp_str s);
TP_PRIVATE tp_str tp_js_trim_start(tp_str s);
TP_PRIVATE tp_str tp_js_trim_end(tp_str s);
/* JS trim() on a code-point slice (a sub-slice, no copy). */
TP_PRIVATE tp_runes tp_js_trim_runes(tp_runes t);

/* R0.6 digit test: ASCII 0-9 only. */
TP_PRIVATE bool tp_is_digit(tp_rune r);
/* Whether s is a non-empty run of ASCII digits. */
TP_PRIVATE bool tp_is_all_digits(tp_str s);
/* R0.6 letter test (category L) on the code point at i / just before i. */
TP_PRIVATE bool tp_is_letter_at(tp_runes t, ptrdiff_t i);
TP_PRIVATE bool tp_is_letter_before(tp_runes t, ptrdiff_t i);
/* R6.10 uppercase test (category Lu) on the code point at i. */
TP_PRIVATE bool tp_is_upper_at(tp_runes t, ptrdiff_t i);

/* A-Z lowercased only (s itself when there is none). */
TP_PRIVATE tp_str tp_ascii_lower(tp_str s);
/* R0.3: the lowercased word with one trailing . , ! ? : ; removed. */
TP_PRIVATE tp_str tp_strip_key_punct(tp_str lower);
/* R0.3 word key (stripped form). */
TP_PRIVATE tp_str tp_word_key(tp_str word);
/* R0.3: does word match the vocabulary token? The unstripped lowercase form is compared first,
 * so entries ending in punctuation (feat.) match as written. */
TP_PRIVATE bool tp_word_matches(tp_str word, tp_str token);

/* A set of vocabulary words (Go wordSet). The zero value is empty. Keys are not copied. */
typedef struct tp_word_set {
  tp_map m;
} tp_word_set;

/* newWordSet(words...): add every word of a list. */
TP_PRIVATE void tp_word_set_add_all(tp_word_set *s, const tp_str *words, size_t n);
TP_PRIVATE void tp_word_set_add(tp_word_set *s, tp_str word);
TP_PRIVATE bool tp_word_set_has(const tp_word_set *s, tp_str key);
/* R0.3 lookup of a word in a vocabulary set: its lowercase form, then its stripped form. */
TP_PRIVATE bool tp_in_vocab(tp_str word, const tp_word_set *vocab);

/* Runs of R0.2 whitespace collapsed to one U+0020, trimmed. */
TP_PRIVATE tp_str tp_collapse_spaces(tp_str s);
/* tp_collapse_spaces on a code-point slice. */
TP_PRIVATE tp_str tp_collapse_runes(tp_runes t);

/* R0.7: the maximal runs of non-whitespace code points. */
TP_PRIVATE tp_strvec tp_split_words(tp_str s);
TP_PRIVATE tp_strvec tp_split_words_runes(tp_runes t);

/* A word with its code-point span [start, end) inside the slice it was taken from. */
typedef struct tp_word_span {
  tp_str text;
  ptrdiff_t start;
  ptrdiff_t end;
} tp_word_span;
typedef TP_VEC(tp_word_span) tp_word_span_vec;

/* The R0.7 words of t with their spans. */
TP_PRIVATE tp_word_span_vec tp_word_spans(tp_runes t);
/* The text of each span. */
TP_PRIVATE tp_strvec tp_span_texts(tp_word_span_vec spans);

/* R0.4 dedup key: JS toLowerCase(), NFD, U+0300-U+036F removed, whitespace collapsed. */
TP_PRIVATE tp_str tp_dedup_key(tp_str s);

/* A year word: 4 ASCII digits in 1900-2099 (word key, so "2015," counts). *out is the value of a
 * 4-digit key even when it is out of range (as in Go), else 0. */
TP_PRIVATE bool tp_year_of(tp_str word, int *out);

/* Length in UTF-16 code units (JS string.length). */
TP_PRIVATE ptrdiff_t tp_utf16_len(tp_runes t);
TP_PRIVATE ptrdiff_t tp_utf16_len_string(tp_str s);

/* Every maximal subpart of an ill-formed UTF-8 sequence replaced with one U+FFFD (WHATWG, as
 * TextDecoder and Rust's String::from_utf8_lossy). Valid input is returned unchanged. */
TP_PRIVATE tp_str tp_sanitize(tp_str s);

/* ------------------------------------------------------------------------------------------ */
/* Phrase tables: multi-word vocabulary matched as token sequences, longest first.            */
/* Go's phraseTable[V]: the value is `const void *` (NULL for struct{}, else a pointer to an  */
/* arena-allocated V).                                                                        */

typedef struct tp_phrase_entry {
  tp_strvec tokens;
  const void *value;
} tp_phrase_entry;
typedef TP_VEC(tp_phrase_entry) tp_phrase_entry_vec;

/* A match: the entry's value and the number of words used. */
typedef struct tp_phrase_match {
  const void *value;
  ptrdiff_t length;
} tp_phrase_match;

typedef TP_VEC(tp_intvec) tp_intvec_vec;

/* The maps are only looked up, never iterated. by_first and by_last map a token to an index
 * into lists (Go map[string][]int). The zero value is an empty table. */
typedef struct tp_phrase_table {
  tp_phrase_entry_vec entries;
  tp_map by_first;
  tp_map by_last;
  tp_map keys;
  tp_intvec_vec lists;
} tp_phrase_table;

/* An empty table in the current arena. */
TP_PRIVATE tp_phrase_table *tp_new_phrase_table(void);
/* Add a phrase. The first entry for a key wins. Keys are ASCII-lowercased and trimmed like JS
 * trim() (not collapsed). */
TP_PRIVATE void tp_phrase_table_add(tp_phrase_table *t, tp_str raw_key, const void *value);
/* The longest entry matching words[start:]. Read-only (safe on a shared table); no allocation. */
TP_PRIVATE bool tp_phrase_table_match_at(const tp_phrase_table *t, tp_strvec words,
                                         ptrdiff_t start, tp_phrase_match *out);
/* The longest entry matching words[:end] at its end (end exclusive). */
TP_PRIVATE bool tp_phrase_table_match_ending(const tp_phrase_table *t, tp_strvec words,
                                             ptrdiff_t end, tp_phrase_match *out);

#endif /* TP_WORDS_H */
