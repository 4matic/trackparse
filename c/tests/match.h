/* match.h: the fixture matcher of spec/fixtures/README.md (partial and exact), over tp_json
 * values. Same rules as go/match_test.go, js/test/match.ts, python/tests/match.py and
 * rust/tests/fixtures.rs. Everything allocates in the current arena. */
#ifndef TP_TEST_MATCH_H
#define TP_TEST_MATCH_H

#include "../src/rt.h"

/* The fixture matcher: an empty string on a match, else the first difference ("$.title: expected
 * \"A\", got \"B\""). A top-level specVersion is never compared (README rule 6). */
tp_str tp_match(tp_json expected, tp_json actual, bool exact);

/* A value as compact JSON, for messages (non-finite numbers as null, as JSON.stringify). */
tp_str tp_json_show(tp_json v);
/* Append s as a JSON string literal (only ", \ and controls escaped, as JSON.stringify). */
void tp_json_quote(tp_buf *b, tp_str s);

#endif /* TP_TEST_MATCH_H */
