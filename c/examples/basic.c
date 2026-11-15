/* basic.c: parse one track string and print what was found.
 *
 *   cc -std=c11 basic.c -ltrackparse    (or with the amalgamation: basic.c trackparse.c -lm)
 */
#include <stdio.h>

#include <trackparse.h>

/* trackparse_str is a pointer and a length; it is also NUL-terminated. */
#define STR(s) (int)(s).len, (s).ptr

int main(void) {
  const char *input =
      "Chase & Status feat. Plan B - End Credits (Sub Focus Remix) [Official Video]";
  trackparse_track *t = trackparse_parse(input, TRACKPARSE_NUL_TERMINATED);
  if (t == NULL) return 1; /* out of memory, the only failure */

  printf("title: %.*s\n", STR(t->title));
  printf("full title: %.*s\n", STR(t->full_title));
  for (size_t i = 0; i < t->artists_count; i++) {
    const trackparse_artist *a = &t->artists[i];
    printf("artist: %.*s (%s)\n", STR(a->name), trackparse_role_name(a->role));
  }
  for (size_t i = 0; i < t->versions_count; i++) {
    const trackparse_version *v = &t->versions[i];
    printf("version: %.*s, type %.*s\n", STR(v->raw), STR(v->type_name));
  }
  for (size_t i = 0; i < t->junk_count; i++) {
    printf("junk: %.*s (%.*s)\n", STR(t->junk[i].raw), STR(t->junk[i].kind_name));
  }
  printf("year: %s\n", t->year ? "yes" : "none");
  trackparse_track_free(t);

  /* Nullable fields are pointers. */
  t = trackparse_parse("Noisia - Stigma (2015)", TRACKPARSE_NUL_TERMINATED);
  if (t == NULL) return 1;
  if (t->year) printf("year: %d\n", *t->year);
  trackparse_track_free(t);

  /* R8.7: the record label after a spaced bullet (U+2022); a later segment is a note. */
  t = trackparse_parse("Noisia - concussion \xe2\x80\xa2 VISION \xe2\x80\xa2 VISION RADIO RELOAD",
                       TRACKPARSE_NUL_TERMINATED);
  if (t == NULL) return 1;
  printf("title: %.*s\n", STR(t->title));
  if (t->label) printf("label: %.*s\n", STR(*t->label));
  for (size_t i = 0; i < t->junk_count; i++) {
    printf("junk: %.*s (%.*s)\n", STR(t->junk[i].raw), STR(t->junk[i].kind_name));
  }
  trackparse_track_free(t);
  return 0;
}
