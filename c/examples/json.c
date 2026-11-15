/* json.c: results as JSON, the bytes JavaScript's JSON.stringify writes. */
#include <stdio.h>

#include <trackparse.h>

#define NT TRACKPARSE_NUL_TERMINATED

static int print_json(const char *label, trackparse_string *json) {
  if (json == NULL) return 1;
  printf("%s: %s\n", label, json->str.ptr);
  trackparse_string_free(json);
  return 0;
}

int main(void) {
  trackparse_track *t = trackparse_parse("01. Noisia - Stigma (2015) [HD]", NT);
  if (t == NULL || print_json("track", trackparse_track_to_json(t))) return 1;
  trackparse_track_free(t);

  trackparse_artists *a = trackparse_parse_artists("Camo & Krooked feat. Ayah Marar", NT);
  if (a == NULL || print_json("artists", trackparse_artists_to_json(a))) return 1;
  trackparse_artists_free(a);

  const char *text = "0:00 Noisia - Stigma\n4:20 Mefjus - Sinkhole";
  trackparse_tracklist *l = trackparse_parse_tracklist(text, NT);
  if (l == NULL || print_json("tracklist", trackparse_tracklist_to_json(l))) return 1;
  trackparse_tracklist_free(l);

  trackparse_edits *e = trackparse_edits_new();
  if (e == NULL) return 1;
  trackparse_edits_shift(e, 90.5);
  trackparse_edit_result *r = trackparse_apply_edits(text, NT, e);
  trackparse_edits_free(e);
  if (r == NULL || print_json("edit result", trackparse_edit_result_to_json(r))) return 1;
  trackparse_edit_result_free(r);

  printf("library %s, spec %s\n", trackparse_library_version(), trackparse_spec_version());
  return 0;
}
