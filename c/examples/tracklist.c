/* tracklist.c: parse a timed tracklist, then edit it. */
#include <stdio.h>

#include <trackparse.h>

#define STR(s) (int)(s).len, (s).ptr
#define NT TRACKPARSE_NUL_TERMINATED

static const char *text = "Tracklist:\n"
                          "00:00 Noisia - Stigma\n"
                          "03:10 Phace & Misanthrop - Desert Orgy\n"
                          "02:45 Mefjus - Sinkhole (VIP)\n"
                          "09:30 June Miller - Drop That\n";

int main(void) {
  trackparse_tracklist_options *o = trackparse_tracklist_options_new();
  if (o == NULL) return 1;
  trackparse_tracklist_options_set_audio_seconds(o, 540); /* a 9-minute mix */
  trackparse_tracklist *list = trackparse_parse_tracklist_with(text, NT, o);
  trackparse_tracklist_options_free(o);
  if (list == NULL) return 1;

  for (size_t i = 0; i < list->entries_count; i++) {
    const trackparse_entry *e = &list->entries[i];
    printf("%d %s", e->line, trackparse_entry_kind_name(e->kind));
    if (e->time) printf(" @%.*s", STR(e->time->raw));
    if (e->track) {
      printf(" title=%.*s", STR(e->track->title));
      for (size_t j = 0; j < e->track->artists_count; j++) {
        printf(" artist=%.*s", STR(e->track->artists[j].name));
      }
    }
    printf("\n");
  }
  for (size_t i = 0; i < list->warnings_count; i++) {
    const trackparse_tracklist_warning *w = &list->warnings[i];
    printf("warning: %s on line %d (%s)\n", trackparse_tracklist_warning_code_name(w->code),
           w->line, trackparse_severity_name(w->severity));
  }
  printf("stats: %d lines, %d tracks, %d timed\n", list->stats.lines, list->stats.tracks,
         list->stats.timed);
  trackparse_tracklist_free(list);

  /* Fix the out-of-order time, rename a track, and insert a line. */
  trackparse_edits *e = trackparse_edits_new();
  if (e == NULL) return 1;
  trackparse_edits_set_time(e, 4, 400);
  trackparse_edits_set_rest(e, 5, "June Miller - Drop That (VIP)", NT);
  trackparse_edits_insert(e, 5, "12:00 ID - ID", NT);
  trackparse_edit_result *r = trackparse_apply_edits(text, NT, e);
  trackparse_edits_free(e);
  if (r == NULL) return 1;
  printf("--- edited\n%.*s", STR(r->text));
  printf("--- changed lines:");
  for (size_t i = 0; i < r->changed_count; i++) printf(" %lld", r->changed[i]);
  printf("\n");
  for (size_t i = 0; i < r->warnings_count; i++) {
    printf("edit warning: %s (edit %zu)\n", trackparse_edit_warning_code_name(r->warnings[i].code),
           r->warnings[i].edit);
  }
  trackparse_edit_result_free(r);
  return 0;
}
