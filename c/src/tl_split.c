/* tl_split.c: R13.1, the line split (LF, CRLF and CR only) with the lossless guarantee
 * (go/tl_split.go). */
#include "internal.h"

/* R13.1.1: split at LF, CRLF and CR. U+2028, U+2029, U+0085 and the other R0.2 characters are
 * not line breaks. A trailing empty segment after the final terminator is not a line, and "" has
 * no lines. R13.1.2: the lines' raw + eol, joined, are the text. */
TP_PRIVATE tp_tl_line_vec tp_split_lines(tp_str text) {
  tp_tl_line_vec lines = {0};
  size_t start = 0;
  /* LF and CR are ASCII and never part of a multi-byte sequence, so every cut is at a character
   * boundary. */
  for (size_t i = 0; i < text.n; i++) {
    unsigned char ch = (unsigned char)text.p[i];
    if (ch == '\n') {
      tp_tl_line l = {tp_str_slice(text, start, i), TP_EOL_LF};
      tp_push(lines, l);
      start = i + 1;
    } else if (ch == '\r') {
      if (i + 1 < text.n && text.p[i + 1] == '\n') {
        tp_tl_line l = {tp_str_slice(text, start, i), TP_EOL_CRLF};
        tp_push(lines, l);
        i++;
      } else {
        tp_tl_line l = {tp_str_slice(text, start, i), TP_EOL_CR};
        tp_push(lines, l);
      }
      start = i + 1;
    }
  }
  if (start < text.n) {
    tp_tl_line l = {tp_str_slice(text, start, text.n), TP_EOL_NONE};
    tp_push(lines, l);
  }
  return lines;
}

/* hasLineBreak reports a line break inside a single-line value (R13.9.5, R13.9.6). */
TP_PRIVATE bool tp_has_line_break(tp_str s) {
  for (size_t i = 0; i < s.n; i++) {
    if (s.p[i] == '\n' || s.p[i] == '\r') return true;
  }
  return false;
}
