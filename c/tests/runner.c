/* runner.c: every shared fixture through the JSON door (tp_wire_call), as go/fixtures_test.go and
 * rust/tests/fixtures.rs do.
 *
 *   trackparse-fixtures [--skip <skip.json>] <fixture.json>...
 *
 * - CTest passes the fixture files (CMake globs spec/fixtures/handwritten and generated); they
 *   are run in sorted path order. With no file at all (the release tarball holds only c/,
 *   no spec) the runner prints a message and exits 77, which CTest reports as skipped.
 * - Matching follows spec/fixtures/README.md (match.c); status pass / xfail / ambiguous.
 * - tests/skip.json lists port-specific known failures by case id; a listed case that passes
 *   fails the run, so the list can only shrink. --skip overrides the path compiled in.
 * - TRACKPARSE_REPORT=1 prints the pass rate per file and per rule instead of failing (the
 *   conformance report).
 * - TRACKPARSE_ONLY=<text> runs the cases whose file name or id contains the text.
 * - TRACKPARSE_ALLOW_FAIL=1 logs the failures but exits 0 (a local opt-in for work in progress).
 * - TRACKPARSE_OUTPUT=<path> writes one {"id","fn","output"} line per case (JSONL), which
 *   scripts/validate_outputs.mjs checks against spec/schema. The C port has no JSON Schema
 *   validator of its own.
 *
 * Every case runs in its own arena scope, so a case cannot leak into the next. Exit status: 0
 * when every case behaves as its status demands, 1 otherwise, 77 when there is nothing to run.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../wire/json_read.h"
#include "../wire/wire.h"
#include "match.h"

#ifndef TP_SKIP_JSON
#define TP_SKIP_JSON "tests/skip.json"
#endif

#define MAX_FAILURES 40
#define REPORT_FAILURES 15

/* ------------------------------------------------------------------------------------------ */
/* Plain heap helpers (the runner's own bookkeeping outlives the per-case arenas)              */

static void *xmalloc(size_t n) {
  void *p = malloc(n ? n : 1);
  if (!p) {
    fprintf(stderr, "runner: out of memory\n");
    exit(2);
  }
  return p;
}

static char *xstrndup(const char *s, size_t n) {
  char *p = (char *)xmalloc(n + 1);
  memcpy(p, s, n);
  p[n] = '\0';
  return p;
}

/* The whole file, or NULL. */
static char *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  char *buf = NULL;
  size_t n = 0, cap = 0;
  if (!f) return NULL;
  for (;;) {
    size_t got;
    if (cap - n < 65536) {
      cap = cap ? cap * 2 : 1 << 20;
      buf = (char *)realloc(buf, cap);
      if (!buf) {
        fclose(f);
        return NULL;
      }
    }
    got = fread(buf + n, 1, cap - n, f);
    n += got;
    if (got == 0) break;
  }
  if (ferror(f)) {
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);
  *len = n;
  return buf;
}

static bool env_set(const char *name) {
  const char *v = getenv(name);
  return v && v[0];
}

/* ------------------------------------------------------------------------------------------ */
/* Counters                                                                                     */

typedef struct rule_count {
  char *name;
  size_t total, ok;
} rule_count;

typedef struct file_count {
  char *name;
  size_t total, ok;
} file_count;

static rule_count *rules;
static size_t rules_n, rules_cap;
static file_count *files;
static size_t files_n;
static char *failures[MAX_FAILURES];
static size_t failures_n;
static FILE *output;

static void count_rule(tp_str name, bool ok) {
  rule_count *rc = NULL;
  for (size_t i = 0; i < rules_n; i++) {
    if (strlen(rules[i].name) == name.n && memcmp(rules[i].name, name.p, name.n) == 0) {
      rc = &rules[i];
      break;
    }
  }
  if (!rc) {
    if (rules_n == rules_cap) {
      rules_cap = rules_cap ? rules_cap * 2 : 64;
      rules = (rule_count *)realloc(rules, rules_cap * sizeof *rules);
      if (!rules) {
        fprintf(stderr, "runner: out of memory\n");
        exit(2);
      }
    }
    rc = &rules[rules_n++];
    rc->name = xstrndup(name.p, name.n);
    rc->total = rc->ok = 0;
  }
  rc->total++;
  if (ok) rc->ok++;
}

/* ------------------------------------------------------------------------------------------ */
/* One case                                                                                     */

static tp_json empty_object(void) {
  tp_json v = tp_json_null();
  v.kind = TP_JSON_OBJECT;
  v.obj = (const tp_json_member *)tp_alloc(0);
  return v;
}

static bool get_string(tp_json v, const char *key, tp_str *out) {
  tp_json x;
  if (!tp_json_get(v, tp_str_c(key), &x) || x.kind != TP_JSON_STRING) return false;
  *out = x.str;
  return true;
}

/* {...defaults[key], ...c[key]} (shallow). */
static tp_json merged(tp_json defaults, tp_json c, const char *key) {
  TP_VEC(tp_json_member) members = {0};
  tp_json out = empty_object();
  tp_json srcs[2];
  srcs[0] = defaults;
  srcs[1] = c;
  for (size_t s = 0; s < 2; s++) {
    tp_json v;
    if (!tp_json_get(srcs[s], tp_str_c(key), &v) || v.kind != TP_JSON_OBJECT) continue;
    for (size_t i = 0; i < v.obj_n; i++) {
      bool replaced = false;
      for (size_t j = 0; j < members.n; j++) {
        if (tp_str_eq(members.p[j].key, v.obj[i].key)) {
          members.p[j].val = v.obj[i].val;
          replaced = true;
        }
      }
      if (!replaced) tp_push(members, v.obj[i]);
    }
  }
  if (members.n) {
    out.obj = members.p;
    out.obj_n = members.n;
  }
  return out;
}

/* Runs one case; the empty string when it behaves as its status demands. Allocates in the
 * current arena. */
static tp_str run_fixture(tp_json file, tp_json c, tp_str id) {
  tp_json defaults = tp_json_null(), edits, actual;
  tp_str fn, input = TP_EMPTY, out, status = TP_EMPTY, match_mode = TP_EMPTY, mismatch;
  bool exact;
  tp_json_get(file, TP_S("defaults"), &defaults);
  if (!get_string(c, "fn", &fn) && !get_string(defaults, "fn", &fn)) fn = TP_S("parse");
  get_string(c, "input", &input);
  if (!tp_json_get(c, TP_S("edits"), &edits)) {
    edits = tp_json_null();
    edits.kind = TP_JSON_ARRAY;
    edits.arr = (const tp_json *)tp_alloc(0);
  }
  out = tp_wire_call(fn, input, merged(defaults, c, "options"),
                     merged(defaults, c, "formatOptions"), edits);
  if (!tp_json_parse(out, &actual)) {
    return tp_concat(TP_S("the port wrote invalid JSON: "), out);
  }
  if (output) {
    tp_buf b = {0};
    tp_str line;
    tp_buf_cstr(&b, "{\"id\":");
    tp_json_quote(&b, id);
    tp_buf_cstr(&b, ",\"fn\":");
    tp_json_quote(&b, fn);
    tp_buf_cstr(&b, ",\"output\":");
    tp_buf_str(&b, out);
    tp_buf_cstr(&b, "}\n");
    line = tp_buf_done(&b);
    fwrite(line.p, 1, line.n, output);
  }

  get_string(c, "status", &status);
  get_string(c, "match", &match_mode);
  exact = tp_str_eq(match_mode, TP_S("exact"));
  if (tp_str_eq(status, TP_S("ambiguous"))) {
    tp_json alts = tp_json_null();
    tp_strvec diffs = {0};
    tp_json_get(c, TP_S("expectedAny"), &alts);
    for (size_t i = 0; alts.kind == TP_JSON_ARRAY && i < alts.arr_n; i++) {
      tp_str d = tp_match(alts.arr[i], actual, exact);
      if (d.n == 0) {
        diffs.n = 0;
        break;
      }
      tp_push(diffs, d);
    }
    if (diffs.n > 0 || alts.kind != TP_JSON_ARRAY || alts.arr_n == 0) {
      mismatch = tp_concat(TP_S("no expectedAny entry matched: "), tp_join(diffs, TP_S("; ")));
    } else {
      mismatch = TP_EMPTY;
    }
  } else {
    tp_json expected = tp_json_null();
    tp_json_get(c, TP_S("expected"), &expected);
    mismatch = tp_match(expected, actual, exact);
  }
  if (tp_str_eq(status, TP_S("xfail"))) {
    return mismatch.n == 0 ? TP_S("xfail case unexpectedly passes") : TP_EMPTY;
  }
  return mismatch;
}

/* Runs one case in its own scope and does the bookkeeping: -1 when TRACKPARSE_ONLY filters it
 * out, 1 when it behaves as its status demands, 0 when not. */
static int run_case(tp_json file, tp_json c, tp_json skip, const char *name, const char *only) {
  tp_scope s;
  tp_str id = TP_EMPTY, reason, problem;
  tp_json rules_v = tp_json_null(), input = tp_json_null();
  char *volatile kept = NULL;
  volatile bool ok = false;
  get_string(c, "id", &id);
  if (only && !strstr(name, only) && !strstr(tp_str_cstr(id), only)) return -1;
  if (!tp_scope_open(&s)) {
    fprintf(stderr, "runner: out of memory\n");
    exit(2);
  }
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    kept = xstrndup("out of memory", 13);
  } else {
    tp_scope_arm(&s);
    problem = run_fixture(file, c, id);
    if (get_string(skip, tp_str_cstr(id), &reason)) {
      if (problem.n == 0) {
        problem = tp_concat3(TP_S("skip-listed ("), reason,
                             TP_S(") but now passes: remove it from tests/skip.json"));
      } else {
        problem = TP_EMPTY;
      }
    }
    ok = problem.n == 0;
    if (!ok && failures_n < MAX_FAILURES) {
      tp_buf b = {0};
      tp_str text;
      tp_json_get(c, TP_S("input"), &input);
      tp_buf_str(&b, id);
      tp_buf_cstr(&b, "\n  input: ");
      tp_buf_str(&b, tp_json_show(input));
      tp_buf_cstr(&b, "\n  ");
      tp_buf_str(&b, problem);
      text = tp_buf_done(&b);
      kept = xstrndup(text.p, text.n);
    }
    tp_scope_close(&s);
    tp_arena_free(s.arena);
  }
  if (kept) {
    if (failures_n < MAX_FAILURES) {
      failures[failures_n++] = kept;
    } else {
      free(kept);
    }
  }
  tp_json_get(c, TP_S("rules"), &rules_v);
  for (size_t i = 0; rules_v.kind == TP_JSON_ARRAY && i < rules_v.arr_n; i++) {
    if (rules_v.arr[i].kind == TP_JSON_STRING) count_rule(rules_v.arr[i].str, ok);
  }
  return ok ? 1 : 0;
}

/* The path relative to spec/fixtures ("handwritten/junk.json"), for the report. */
static const char *display_name(const char *path) {
  const char *best = NULL, *p = path;
  while ((p = strstr(p, "fixtures")) != NULL) {
    if (p[8] == '/' || p[8] == '\\') best = p + 9;
    p += 8;
  }
  return best ? best : path;
}

/* Runs every case of one file inside a scope that holds the file's JSON. false when the file
 * could not be read. */
static bool run_file(const char *path, tp_json skip, const char *only) {
  tp_scope s;
  size_t len = 0;
  char *text = read_file(path, &len);
  volatile bool good = true;
  if (!text) {
    fprintf(stderr, "runner: cannot read %s\n", path);
    return false;
  }
  if (!tp_scope_open(&s)) {
    free(text);
    return false;
  }
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    fprintf(stderr, "runner: out of memory reading %s\n", path);
    free(text);
    return false;
  }
  tp_scope_arm(&s);
  {
    tp_json data, cases;
    tp_str whole;
    whole.p = text;
    whole.n = len;
    if (!tp_json_parse(whole, &data)) {
      fprintf(stderr, "runner: %s: invalid JSON\n", path);
      good = false;
    } else if (tp_json_get(data, TP_S("cases"), &cases) && cases.kind == TP_JSON_ARRAY) {
      const char *name = display_name(path);
      file_count *fc = &files[files_n++];
      fc->name = xstrndup(name, strlen(name));
      fc->total = fc->ok = 0;
      for (size_t i = 0; i < cases.arr_n; i++) {
        int r = run_case(data, cases.arr[i], skip, name, only);
        if (r < 0) continue;
        fc->total++;
        if (r > 0) fc->ok++;
      }
    }
    /* else: metadata such as generated/summary.json */
  }
  tp_scope_close(&s);
  tp_arena_free(s.arena);
  free(text);
  return good;
}

/* Reads skip.json into the current arena; false when it is missing or not an object. */
static bool read_skip(const char *path, tp_json *out) {
  size_t len = 0;
  char *text = read_file(path, &len);
  bool ok;
  tp_str whole;
  if (!text) {
    fprintf(stderr, "runner: cannot read %s\n", path);
    return false;
  }
  whole.p = tp_str_copy(text, len).p;
  whole.n = len;
  free(text);
  ok = tp_json_parse(whole, out) && out->kind == TP_JSON_OBJECT;
  if (!ok) fprintf(stderr, "runner: %s is not a JSON object\n", path);
  return ok;
}

static int cmp_cstr(const char *a, const char *b) { return strcmp(a, b); }

/* Insertion sorts: a few dozen files and a few hundred rules. */
static void sort_paths(const char **p, size_t n) {
  for (size_t i = 1; i < n; i++) {
    const char *x = p[i];
    size_t j = i;
    while (j > 0 && cmp_cstr(p[j - 1], x) > 0) {
      p[j] = p[j - 1];
      j--;
    }
    p[j] = x;
  }
}

static void sort_rules(void) {
  for (size_t i = 1; i < rules_n; i++) {
    rule_count x = rules[i];
    size_t j = i;
    while (j > 0 && cmp_cstr(rules[j - 1].name, x.name) > 0) {
      rules[j] = rules[j - 1];
      j--;
    }
    rules[j] = x;
  }
}

static void print_report(size_t passed, size_t total) {
  printf("\nconformance: %lu / %lu\n", (unsigned long)passed, (unsigned long)total);
  for (size_t i = 0; i < files_n; i++) {
    printf("  %5lu / %-5lu %s%s\n", (unsigned long)files[i].ok, (unsigned long)files[i].total,
           files[i].name, files[i].ok != files[i].total ? "  <--" : "");
  }
  printf("by rule (failing only):\n");
  sort_rules();
  for (size_t i = 0; i < rules_n; i++) {
    if (rules[i].ok != rules[i].total) {
      printf("  %-10s %lu / %lu\n", rules[i].name, (unsigned long)rules[i].ok,
             (unsigned long)rules[i].total);
    }
  }
  for (size_t i = 0; i < failures_n && i < REPORT_FAILURES; i++) printf("\n%s\n", failures[i]);
}

/* Every file, inside an outer scope that holds skip.json for the whole run: 0, or 2 when a file
 * or skip.json could not be read. */
static int run_files(const char *skip_path, const char **paths, size_t n, const char *only) {
  tp_scope s;
  volatile int status = 0;
  if (!tp_scope_open(&s)) return 2;
  if (setjmp(s.jmp) != 0) {
    tp_scope_abort(&s);
    fprintf(stderr, "runner: out of memory\n");
    return 2;
  }
  tp_scope_arm(&s);
  {
    tp_json skip;
    if (!read_skip(skip_path, &skip)) {
      status = 2;
    } else {
      for (size_t i = 0; i < n; i++) {
        if (!run_file(paths[i], skip, only)) status = 2;
      }
    }
  }
  tp_scope_close(&s);
  tp_arena_free(s.arena);
  return status;
}

static int run_all(int argc, char **argv, const char **paths) {
  const char *skip_path = TP_SKIP_JSON;
  const char *only = getenv("TRACKPARSE_ONLY");
  const char *output_path = getenv("TRACKPARSE_OUTPUT");
  bool report = env_set("TRACKPARSE_REPORT"), allow_fail = env_set("TRACKPARSE_ALLOW_FAIL");
  size_t n = 0, total = 0, passed = 0;
  int status;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--skip") == 0 && i + 1 < argc) {
      skip_path = argv[++i];
    } else {
      paths[n++] = argv[i];
    }
  }
  if (only && !only[0]) only = NULL;
  if (n == 0) {
    printf("spec/fixtures not found (the release tarball holds only c/): skipping the fixtures\n");
    return 77;
  }
  sort_paths(paths, n);
  files = (file_count *)xmalloc(n * sizeof *files);
  if (output_path && output_path[0]) {
    output = fopen(output_path, "wb");
    if (!output) {
      fprintf(stderr, "runner: cannot write %s\n", output_path);
      return 2;
    }
  }

  status = run_files(skip_path, paths, n, only);
  if (output) fclose(output);
  output = NULL;
  if (status) return status;

  for (size_t i = 0; i < files_n; i++) {
    total += files[i].total;
    passed += files[i].ok;
  }
  if (report) {
    print_report(passed, total);
    return 0;
  }
  if (total == 0) {
    fprintf(stderr, "no fixtures found\n");
    return 1;
  }
  if (passed != total) {
    if (allow_fail) {
      printf("TRACKPARSE_ALLOW_FAIL: %lu / %lu pass\n", (unsigned long)passed,
             (unsigned long)total);
      return 0;
    }
    printf("%lu of %lu fixture cases fail; first ones:\n", (unsigned long)(total - passed),
           (unsigned long)total);
    for (size_t i = 0; i < failures_n; i++) printf("\n%s\n", failures[i]);
    return 1;
  }
  printf("fixtures: %lu / %lu pass\n", (unsigned long)passed, (unsigned long)total);
  return 0;
}

int main(int argc, char **argv) {
  const char **paths = (const char **)xmalloc((size_t)argc * sizeof *paths);
  int status = run_all(argc, argv, paths);
  if (output) fclose(output);
  for (size_t i = 0; i < files_n; i++) free(files[i].name);
  for (size_t i = 0; i < rules_n; i++) free(rules[i].name);
  for (size_t i = 0; i < failures_n; i++) free(failures[i]);
  free(files);
  free(rules);
  free(paths);
  return status;
}
