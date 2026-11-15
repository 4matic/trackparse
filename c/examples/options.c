/* options.c: parse options, a reusable parser, and format options. */
#include <stdio.h>

#include <trackparse.h>

#define STR(s) (int)(s).len, (s).ptr
#define NT TRACKPARSE_NUL_TERMINATED

static void print_artists(const trackparse_track *t) {
  for (size_t i = 0; i < t->artists_count; i++) {
    printf("%s%.*s", i ? " | " : "  artists: ", STR(t->artists[i].name));
  }
  printf("\n");
}

int main(void) {
  /* A YouTube title: the uploader's name is a hint for the artist. */
  trackparse_parse_options *o = trackparse_parse_options_new();
  if (o == NULL) return 1;
  trackparse_parse_options_set_mode(o, TRACKPARSE_MODE_YOUTUBE);
  trackparse_parse_options_set_uploader(o, "Noisia", NT);
  trackparse_track *t = trackparse_parse_with("Stigma (Official Video) HD", NT, o);
  if (t == NULL) return 1;
  printf("youtube: %.*s\n", STR(t->title));
  print_artists(t);
  trackparse_track_free(t);
  trackparse_parse_options_free(o);

  /* known artists keep a name with a joiner in it together. A parser compiles the options once
   * and can be shared between threads. */
  o = trackparse_parse_options_new();
  if (o == NULL) return 1;
  trackparse_parse_options_add_known_artist(o, "Chase & Status", NT);
  trackparse_parser *p = trackparse_parser_new(o);
  trackparse_parse_options_free(o); /* the parser keeps its own copy */
  if (p == NULL) return 1;
  const char *inputs[] = {"Chase & Status - Baddadan", "Chase & Status x Bou - Baddadan"};
  for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
    t = trackparse_parser_parse(p, inputs[i], NT);
    if (t == NULL) return 1;
    printf("parser: %s\n", inputs[i]);
    print_artists(t);
    trackparse_track_free(t);
  }
  trackparse_parser_free(p);

  /* Format a track back, with the featured artists moved to the title. */
  t = trackparse_parse("Sub Focus feat. Kele - Turn It Around (VIP)", NT);
  trackparse_format_options *f = trackparse_format_options_new();
  if (t == NULL || f == NULL) return 1;
  trackparse_format_options_set_feat(f, TRACKPARSE_FEAT_TITLE);
  trackparse_format_options_set_feat_marker(f, "ft.", NT);
  trackparse_string *s = trackparse_format(t);
  trackparse_string *s2 = trackparse_format_with(t, f);
  if (s == NULL || s2 == NULL) return 1;
  printf("format: %s\n", s->str.ptr);
  printf("format_with: %s\n", s2->str.ptr);
  trackparse_string_free(s);
  trackparse_string_free(s2);
  trackparse_format_options_free(f);
  trackparse_track_free(t);
  return 0;
}
