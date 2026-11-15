/* test_locale.c: the output does not depend on the process locale (PORTING.md hazard 2).
 *
 * The library never calls a locale-dependent function (no <ctype.h>, strtod or float printf in
 * src/), so a program that calls setlocale() must still get the bytes the "C" locale gives. This
 * test computes every fixture case through the JSON door and a set of number- and case-sensitive
 * calls through the public API in the "C" locale, then again after setlocale(LC_ALL, "") and
 * after each of de_DE.UTF-8 (decimal comma), tr_TR.UTF-8 (dotted and dotless i) and others that
 * are installed, and compares the bytes.
 *
 *   test_locale <fixture.json>...
 *
 * Exit 77 (skipped) when no locale other than C / POSIX is installed. CI installs de_DE.UTF-8
 * and tr_TR.UTF-8 (locale-gen, or language-pack-de / language-pack-tr). */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/trackparse.h"
#include "../wire/wire.h"
#include "hardening.h"

typedef struct result {
  char *text;
  size_t len;
} result;

typedef struct results {
  result *items;
  size_t n, cap;
} results;

static void add(results *r, const char *p, size_t n) {
  if (r->n == r->cap) {
    r->cap = r->cap ? r->cap * 2 : 1024;
    r->items = (result *)realloc(r->items, r->cap * sizeof *r->items);
    if (!r->items) exit(2);
  }
  r->items[r->n].text = (char *)malloc(n + 1);
  if (!r->items[r->n].text) exit(2);
  memcpy(r->items[r->n].text, p, n);
  r->items[r->n].text[n] = '\0';
  r->items[r->n].len = n;
  r->n++;
}

static void add_string(results *r, trackparse_string *s) {
  if (!s) {
    fprintf(stderr, "out of memory\n");
    exit(2);
  }
  add(r, s->str.ptr, s->str.len);
  trackparse_string_free(s);
}

static void free_results(results *r) {
  for (size_t i = 0; i < r->n; i++) free(r->items[i].text);
  free(r->items);
  memset(r, 0, sizeof *r);
}

/* Inputs where a locale could show: fractional times and edit numbers (decimal comma), upper and
 * lower case with I, İ, ı (Turkish case mapping), and keywords in capitals. */
static const char *const tracks[] = {
    "I - \xc4\xb0stanbul (IIIII Remix) FEAT. I\xc5\x9eIK",
    "\xc4\xb0lhan feat. IRMAK & \xc4\xb1\xc5\x9f\xc4\xb1k - TITLE (IDIOT VIP) [OFFICIAL VIDEO]",
    "INXS - I NEED YOU TONIGHT (EXTENDED MIX) 1987",
    "01. ILIRIA - Infinite (Radio Edit)",
    "Phace & Misanthrop - Desert Orgy (Noisia Remix) [HD] 3:45",
    "\xc3\x9c"
    "BER - \xc3\x84rger (Stra\xc3\x9f"
    "e Mix)",
};

static const char *const lists[] = {
    "00:00 Intro\n01:02:03.5 I - \xc4\xb0stanbul\n12:34,250 ILIRIA - Infinite\n0:00.25 E - F",
    "[00:00.5] **INTRO**\r\n[01:30,75] IDIOT - IDEA (VIP)\r\n[59:59.999] Break\r\n",
    "1. Noisia - Stigma 00:00\n2. Phace - Plastic Acid 04:37.5\n3. I - Dot 1:2",
};

/* Edits through the JSON door: fractional seconds, fractional and huge line numbers, numbers
 * echoed in warnings. */
static const char *const edit_lines[] = {
    "{\"fn\":\"applyEdits\",\"input\":\"00:00 A - B\\n01:02.5 C - D\\n\","
    "\"edits\":[{\"op\":\"set-time\",\"line\":2,\"seconds\":3167.5},"
    "{\"op\":\"shift\",\"seconds\":-0.25},{\"op\":\"set-time\",\"line\":1.5,\"seconds\":2.5},"
    "{\"op\":\"delete\",\"line\":1e21},{\"op\":\"shift\",\"seconds\":1e-7,\"from\":2}]}",
    "{\"fn\":\"parseTracklist\",\"input\":\"00:00 A\\n10:00 B\",\"options\":"
    "{\"audioSeconds\":599.5}}",
    "{\"fn\":\"parse\",\"input\":\"IDIOT - IIII (WHATEVER REMIX)\",\"options\":"
    "{\"keywords\":{\"versionHeads\":{\"WHATEVER\":\"remix\"},"
    "\"junk\":{\"\\u0130\\u0130\":\"other\"}},"
    "\"knownArtists\":[\"idiot\"]}}",
};

typedef struct calc_arg {
  const hz_cases *cases;
  results *out;
} calc_arg;

static void wire_one(results *out, const char *p, size_t n) {
  tp_str line, o;
  line.p = p;
  line.n = n;
  o = tp_wire_line(line);
  add(out, o.p, o.n);
}

static void calc_wire(void *p) {
  calc_arg *a = (calc_arg *)p;
  for (size_t i = 0; i < a->cases->n; i++) {
    wire_one(a->out, a->cases->items[i].line, a->cases->items[i].line_n);
  }
  for (size_t i = 0; i < TP_COUNT(edit_lines); i++) {
    wire_one(a->out, edit_lines[i], strlen(edit_lines[i]));
  }
}

/* Every output for this locale, in a fixed order. */
static results compute(const hz_cases *cases) {
  results out = {NULL, 0, 0};
  calc_arg a;
  for (size_t i = 0; i < TP_COUNT(tracks); i++) {
    trackparse_track *t = trackparse_parse(tracks[i], TRACKPARSE_NUL_TERMINATED);
    trackparse_artists *ar = trackparse_parse_artists(tracks[i], TRACKPARSE_NUL_TERMINATED);
    if (!t || !ar) exit(2);
    add_string(&out, trackparse_track_to_json(t));
    add_string(&out, trackparse_format(t));
    add_string(&out, trackparse_artists_to_json(ar));
    add_string(&out, trackparse_normalize(tracks[i], TRACKPARSE_NUL_TERMINATED));
    trackparse_track_free(t);
    trackparse_artists_free(ar);
  }
  for (size_t i = 0; i < TP_COUNT(lists); i++) {
    trackparse_tracklist_options *o = trackparse_tracklist_options_new();
    trackparse_edits *e = trackparse_edits_new();
    trackparse_tracklist *l;
    trackparse_edit_result *r;
    if (!o || !e || !trackparse_tracklist_options_set_audio_seconds(o, 3599.75) ||
        !trackparse_edits_set_time(e, 2, 62.125) || !trackparse_edits_shift(e, 0.5) ||
        !trackparse_edits_shift_from(e, -1.75, 3) || !trackparse_edits_set_time(e, 1, 1e21)) {
      exit(2);
    }
    l = trackparse_parse_tracklist_with(lists[i], TRACKPARSE_NUL_TERMINATED, o);
    r = trackparse_apply_edits(lists[i], TRACKPARSE_NUL_TERMINATED, e);
    if (!l || !r) exit(2);
    add_string(&out, trackparse_tracklist_to_json(l));
    add_string(&out, trackparse_edit_result_to_json(r));
    trackparse_tracklist_free(l);
    trackparse_edit_result_free(r);
    trackparse_tracklist_options_free(o);
    trackparse_edits_free(e);
  }
  a.cases = cases;
  a.out = &out;
  hz_scoped(calc_wire, &a);
  return out;
}

static int compare(const char *name, const results *want, const results *got) {
  int bad = 0;
  if (want->n != got->n) {
    fprintf(stderr, "%s: %lu outputs, want %lu\n", name, (unsigned long)got->n,
            (unsigned long)want->n);
    return 1;
  }
  for (size_t i = 0; i < want->n; i++) {
    if (want->items[i].len != got->items[i].len ||
        memcmp(want->items[i].text, got->items[i].text, want->items[i].len) != 0) {
      if (bad < 5) {
        fprintf(stderr, "%s: output %lu differs from the C locale:\n  C:    %s\n  here: %s\n", name,
                (unsigned long)i, want->items[i].text, got->items[i].text);
      }
      bad++;
    }
  }
  return bad;
}

int main(int argc, char **argv) {
  /* "" is the environment's locale (LANG, LC_ALL): what a program calling setlocale gets. */
  static const char *const names[] = {
      "",
      "de_DE.UTF-8",
      "de_DE.utf8",
      "tr_TR.UTF-8",
      "tr_TR.utf8",
      "fr_FR.UTF-8",
      "ru_RU.UTF-8",
      "et_EE.UTF-8",
      "et_EE.utf8",
      "en_DK.utf8",
      "C.UTF-8",
      "de_DE",
      "tr_TR",
      "German_Germany.1252",
      "Turkish_Turkey.1254",
  };
  hz_cases cases = hz_load_cases(argc - 1, argv + 1);
  results want;
  int tried = 0, bad = 0;
  if (!setlocale(LC_ALL, "C")) {
    fprintf(stderr, "setlocale(LC_ALL, \"C\") failed\n");
    return 1;
  }
  want = compute(&cases);
  printf("C locale: %lu outputs (%lu fixture cases)\n", (unsigned long)want.n,
         (unsigned long)cases.n);
  for (size_t i = 0; i < TP_COUNT(names); i++) {
    const char *got_name = setlocale(LC_ALL, names[i]);
    char applied[256];
    results got;
    int d;
    if (!got_name) {
      printf("  %-20s not installed\n", names[i][0] ? names[i] : "\"\"");
      continue;
    }
    /* Copy: the next setlocale call may overwrite the string. */
    strncpy(applied, got_name, sizeof applied - 1);
    applied[sizeof applied - 1] = '\0';
    got = compute(&cases);
    setlocale(LC_ALL, "C");
    d = compare(applied, &want, &got);
    {
      /* The decimal point shows that the locale would change a printf("%g"). */
      const char *dp;
      setlocale(LC_ALL, names[i]);
      dp = localeconv()->decimal_point;
      printf("  %-20s -> %-24.24s decimal point \"%s\": %s\n", names[i][0] ? names[i] : "\"\"",
             applied, dp ? dp : "?", d ? "DIFFERS" : "same bytes");
      setlocale(LC_ALL, "C");
    }
    if (strcmp(applied, "C") != 0 && strcmp(applied, "POSIX") != 0) tried++;
    bad += d;
    free_results(&got);
  }
  free_results(&want);
  hz_free_cases(&cases);
  if (bad) {
    fprintf(stderr, "locale: %d outputs depend on the locale\n", bad);
    return 1;
  }
  if (tried == 0) {
    printf("no locale other than C / POSIX is installed: skipped\n");
    return 77;
  }
  printf("locale: ok (%d locales)\n", tried);
  return 0;
}
