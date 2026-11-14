/* dump.c: the C side of the cross-port diff (scripts/compare-ports.mjs, scripts/fuzz-ports.mjs):
 * JSONL in, JSON out, the protocol of go/internal/cmd/dump.
 *
 * Each stdin line is an object {"fn", "input", "options", "formatOptions", "edits"} as in the
 * shared fixtures; each stdout line is the JSON result of that call, written as the JavaScript
 * package writes it. A line that is not a JSON object gives null. Non-finite numbers travel as
 * 1e999 / -1e999, which the reader takes as infinity. A trailing CR is dropped with the LF.
 *
 *   trackparse-dump < cases.jsonl > results.jsonl
 *
 * Each line runs in its own arena scope. Exit status 1 on a read or write error.
 */
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "../wire/wire.h"

/* One line through the JSON door, written with its newline; false on a write error. */
static bool dump_line(const char *p, size_t n) {
  tp_scope s;
  tp_str line, out;
  bool ok;
  if (!tp_scope_open(&s)) {
    fprintf(stderr, "dump: out of memory\n");
    exit(1);
  }
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    fprintf(stderr, "dump: out of memory\n");
    exit(1);
  }
  tp_scope_arm(&s);
  line.p = p;
  line.n = n;
  out = tp_wire_line(line);
  ok = fwrite(out.p, 1, out.n, stdout) == out.n && fputc('\n', stdout) != EOF;
  tp_scope_close(&s);
  tp_arena_free(s.arena);
  return ok;
}

int main(void) {
  char *buf = NULL;
  size_t start = 0, len = 0, cap = 0; /* the unread bytes are buf[start, len) */
  bool eof = false;
#ifdef _WIN32
  (void)_setmode(_fileno(stdin), _O_BINARY);
  (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
  while (!eof || start < len) {
    char *nl = start < len ? (char *)memchr(buf + start, '\n', len - start) : NULL;
    if (!nl && !eof) {
      size_t got;
      /* Keep only the unread bytes, then make room. */
      if (start > 0) {
        memmove(buf, buf + start, len - start);
        len -= start;
        start = 0;
      }
      if (cap - len < 65536) {
        size_t ncap = cap ? cap * 2 : 1 << 16;
        char *nbuf = (char *)realloc(buf, ncap);
        if (!nbuf) {
          fprintf(stderr, "dump: out of memory\n");
          return 1;
        }
        buf = nbuf;
        cap = ncap;
      }
      got = fread(buf + len, 1, cap - len, stdin);
      len += got;
      if (got == 0) {
        if (ferror(stdin)) {
          fprintf(stderr, "dump: read error\n");
          return 1;
        }
        eof = true;
      }
      continue;
    }
    {
      /* A line: up to the LF, or the rest of the input at the end. */
      size_t end = nl ? (size_t)(nl - buf) : len;
      size_t n = end - start;
      if (n > 0 && buf[start + n - 1] == '\r') n--;
      if (!dump_line(buf + start, n)) {
        fprintf(stderr, "dump: write error\n");
        return 1;
      }
      start = nl ? end + 1 : len;
    }
  }
  free(buf);
  if (fflush(stdout) != 0) {
    fprintf(stderr, "dump: write error\n");
    return 1;
  }
  return 0;
}
