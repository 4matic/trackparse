/**
 * @file trackparse.h
 * @brief Turn messy track names into structured data: artists with roles, remixes and versions,
 * junk, and whole tracklists. One spec, the same output as the JavaScript, Python, Rust and Go
 * packages.
 *
 * Documentation: https://4matic.github.io/trackparse/
 *
 * Conventions:
 * - Input text is UTF-8 given as a pointer and a byte length. Pass TRACKPARSE_NUL_TERMINATED as
 *   the length to use strlen(). Invalid UTF-8 is replaced with U+FFFD per maximal ill-formed
 *   subpart (the WHATWG rule, as in the Rust and Go ports). An explicit length lets U+0000 in.
 * - Every function is total: any input gives a result. The only failure is running out of
 *   memory, reported as NULL (or false).
 * - Results are read-only and own all their memory; free each with its _free function. Strings
 *   in results are trackparse_str: a pointer and a length, also NUL-terminated.
 * - Nullable fields are pointers that are NULL when the value is absent.
 * - Option objects are opaque. Their setters copy what they are given. Bad values fall back to
 *   the defaults, as in every port. A NULL options pointer means the defaults.
 * - All functions are thread-safe. A trackparse_parser is immutable once created and can be
 *   shared between threads. The default word tables are built on first use and kept for the
 *   life of the process (a leak checker reports them as still reachable).
 */
#ifndef TRACKPARSE_H
#define TRACKPARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------------------------ */
/* Version and linkage                                                                         */

/** The library version this header belongs to. */
#define TRACKPARSE_VERSION "0.6.1" /* x-release-please-version */
#define TRACKPARSE_VERSION_MAJOR 0 /* x-release-please-major */
#define TRACKPARSE_VERSION_MINOR 6 /* x-release-please-minor */
#define TRACKPARSE_VERSION_PATCH 1 /* x-release-please-patch */

#if defined(TRACKPARSE_STATIC) || defined(TRACKPARSE_AMALGAMATION)
#define TRACKPARSE_API
#elif defined(_WIN32)
#ifdef TRACKPARSE_BUILDING
#define TRACKPARSE_API __declspec(dllexport)
#else
#define TRACKPARSE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define TRACKPARSE_API __attribute__((visibility("default")))
#else
#define TRACKPARSE_API
#endif

/** Pass as a length to mean "the string is NUL-terminated". */
#define TRACKPARSE_NUL_TERMINATED SIZE_MAX

/** The version of the linked library ("X.Y.Z"), which can differ from TRACKPARSE_VERSION when a
 * shared library is replaced. */
TRACKPARSE_API const char *trackparse_library_version(void);
/** The spec version the library implements (spec/VERSION), "X.Y.Z". */
TRACKPARSE_API const char *trackparse_spec_version(void);

/** A UTF-8 string in a result: ptr[len] is always '\0'. */
typedef struct trackparse_str {
  const char *ptr;
  size_t len;
} trackparse_str;

/** Replace the allocator for the whole process. Call it before any other function, at most
 * once; NULL restores malloc/realloc/free. realloc_fn may be NULL. */
typedef struct trackparse_allocator {
  void *(*malloc_fn)(size_t size, void *user);
  void *(*realloc_fn)(void *ptr, size_t size, void *user);
  void (*free_fn)(void *ptr, void *user);
  void *user;
} trackparse_allocator;
TRACKPARSE_API void trackparse_set_allocator(const trackparse_allocator *allocator);

/* ------------------------------------------------------------------------------------------ */
/* Enumerations. Each has _name() (the spec's string, as in JSON) and _from_name() (returns   */
/* false for an unknown name).                                                                  */

/** R2: how the input was written. */
typedef enum trackparse_mode {
  TRACKPARSE_MODE_AUTO, /**< options only: detect (the default) */
  TRACKPARSE_MODE_CLEAN,
  TRACKPARSE_MODE_YOUTUBE,
  TRACKPARSE_MODE_FILENAME,
  TRACKPARSE_MODE_TRACKLIST
} trackparse_mode;

typedef enum trackparse_role {
  TRACKPARSE_ROLE_PRIMARY,
  TRACKPARSE_ROLE_FEATURED,
  TRACKPARSE_ROLE_REMIXER,
  TRACKPARSE_ROLE_PRODUCER
} trackparse_role;

/** Where an artist was found. */
typedef enum trackparse_source {
  TRACKPARSE_SOURCE_ARTIST,
  TRACKPARSE_SOURCE_TITLE,
  TRACKPARSE_SOURCE_VERSION
} trackparse_source;

/** The 27 version types. keywords can add other names: see trackparse_version.type_name. */
typedef enum trackparse_version_type {
  TRACKPARSE_VERSION_REMIX,
  TRACKPARSE_VERSION_BOOTLEG,
  TRACKPARSE_VERSION_VIP,
  TRACKPARSE_VERSION_EDIT,
  TRACKPARSE_VERSION_FLIP,
  TRACKPARSE_VERSION_REFIX,
  TRACKPARSE_VERSION_REWORK,
  TRACKPARSE_VERSION_MASHUP,
  TRACKPARSE_VERSION_BLEND,
  TRACKPARSE_VERSION_DUB,
  TRACKPARSE_VERSION_MIX,
  TRACKPARSE_VERSION_EXTENDED,
  TRACKPARSE_VERSION_RADIO,
  TRACKPARSE_VERSION_CLUB,
  TRACKPARSE_VERSION_ORIGINAL,
  TRACKPARSE_VERSION_INSTRUMENTAL,
  TRACKPARSE_VERSION_ACAPELLA,
  TRACKPARSE_VERSION_LIVE,
  TRACKPARSE_VERSION_ACOUSTIC,
  TRACKPARSE_VERSION_REMASTER,
  TRACKPARSE_VERSION_DEMO,
  TRACKPARSE_VERSION_REPRISE,
  TRACKPARSE_VERSION_COVER,
  TRACKPARSE_VERSION_VERSION, /**< also used for names keywords introduced */
  TRACKPARSE_VERSION_SPED_UP,
  TRACKPARSE_VERSION_SLOWED,
  TRACKPARSE_VERSION_NIGHTCORE
} trackparse_version_type;

typedef enum trackparse_delimiter {
  TRACKPARSE_DELIMITER_PAREN,   /**< "(" */
  TRACKPARSE_DELIMITER_BRACKET, /**< "[" */
  TRACKPARSE_DELIMITER_BRACE,   /**< "{" */
  TRACKPARSE_DELIMITER_DASH     /**< "-" */
} trackparse_delimiter;

/** Junk kinds. keywords can add other names: see trackparse_junk.kind_name. */
typedef enum trackparse_junk_kind {
  TRACKPARSE_JUNK_VIDEO,
  TRACKPARSE_JUNK_AUDIO,
  TRACKPARSE_JUNK_LYRICS,
  TRACKPARSE_JUNK_QUALITY,
  TRACKPARSE_JUNK_PROMO,
  TRACKPARSE_JUNK_PLATFORM,
  TRACKPARSE_JUNK_LABEL,
  TRACKPARSE_JUNK_GENRE,
  TRACKPARSE_JUNK_OTHER /**< also used for names keywords introduced */
} trackparse_junk_kind;

/** Track warnings (reference/warnings, "track warnings"). */
typedef enum trackparse_warning {
  TRACKPARSE_WARNING_NO_SEPARATOR,
  TRACKPARSE_WARNING_AMBIGUOUS_SEPARATOR,
  TRACKPARSE_WARNING_UNSPACED_DASH_SPLIT,
  TRACKPARSE_WARNING_ASYMMETRIC_DASH_SPLIT,
  TRACKPARSE_WARNING_BY_SPLIT,
  TRACKPARSE_WARNING_QUOTED_TITLE_SPLIT,
  TRACKPARSE_WARNING_AMBIGUOUS_MIX_CREDIT,
  TRACKPARSE_WARNING_UNBALANCED_BRACKETS
} trackparse_warning;

typedef enum trackparse_entry_kind {
  TRACKPARSE_ENTRY_TRACK,
  TRACKPARSE_ENTRY_MARKER,
  TRACKPARSE_ENTRY_HEADER,
  TRACKPARSE_ENTRY_LINK,
  TRACKPARSE_ENTRY_NOTE,
  TRACKPARSE_ENTRY_BLANK
} trackparse_entry_kind;

typedef enum trackparse_eol {
  TRACKPARSE_EOL_LF,   /**< "\n" */
  TRACKPARSE_EOL_CRLF, /**< "\r\n" */
  TRACKPARSE_EOL_CR,   /**< "\r" */
  TRACKPARSE_EOL_NONE  /**< "" (the last line) */
} trackparse_eol;

/** Tracklist warnings (reference/warnings, "tracklist warnings"). */
typedef enum trackparse_tracklist_warning_code {
  TRACKPARSE_TL_TIME_MISSING,
  TRACKPARSE_TL_TIME_OUT_OF_ORDER,
  TRACKPARSE_TL_TIME_DUPLICATE,
  TRACKPARSE_TL_NO_START_AT_ZERO,
  TRACKPARSE_TL_TIME_AFTER_AUDIO_END,
  TRACKPARSE_TL_INVALID_TIME,
  TRACKPARSE_TL_POSITION_GAP,
  TRACKPARSE_TL_POSITION_DUPLICATE,
  TRACKPARSE_TL_LINE_UNPARSED
} trackparse_tracklist_warning_code;

typedef enum trackparse_severity {
  TRACKPARSE_SEVERITY_INFO,
  TRACKPARSE_SEVERITY_WARNING
} trackparse_severity;

typedef enum trackparse_edit_op {
  TRACKPARSE_EDIT_SET_TIME,
  TRACKPARSE_EDIT_SHIFT,
  TRACKPARSE_EDIT_SORT_BY_TIME,
  TRACKPARSE_EDIT_SET_REST,
  TRACKPARSE_EDIT_INSERT,
  TRACKPARSE_EDIT_DELETE
} trackparse_edit_op;

/** Edit warnings (reference/warnings, "edit warnings"). */
typedef enum trackparse_edit_warning_code {
  TRACKPARSE_EDIT_WARNING_EDIT_INVALID,
  TRACKPARSE_EDIT_WARNING_TIME_CLAMPED,
  TRACKPARSE_EDIT_WARNING_STYLE_UPGRADED
} trackparse_edit_warning_code;

/** R12 splitAnd. */
typedef enum trackparse_split_and {
  TRACKPARSE_SPLIT_AND_AUTO,
  TRACKPARSE_SPLIT_AND_ALWAYS,
  TRACKPARSE_SPLIT_AND_NEVER
} trackparse_split_and;

/** R11 feat placement. */
typedef enum trackparse_feat {
  TRACKPARSE_FEAT_SOURCE,
  TRACKPARSE_FEAT_ARTIST,
  TRACKPARSE_FEAT_TITLE,
  TRACKPARSE_FEAT_OMIT
} trackparse_feat;

typedef enum trackparse_joiners {
  TRACKPARSE_JOINERS_ORIGINAL,
  TRACKPARSE_JOINERS_CANONICAL
} trackparse_joiners;
typedef enum trackparse_versions {
  TRACKPARSE_VERSIONS_ALL,
  TRACKPARSE_VERSIONS_NONE
} trackparse_versions;

/* Number of values in each enum, for loops over all of them. */
#define TRACKPARSE_MODE_COUNT 5
#define TRACKPARSE_ROLE_COUNT 4
#define TRACKPARSE_SOURCE_COUNT 3
#define TRACKPARSE_VERSION_TYPE_COUNT 27
#define TRACKPARSE_DELIMITER_COUNT 4
#define TRACKPARSE_JUNK_KIND_COUNT 9
#define TRACKPARSE_WARNING_COUNT 8
#define TRACKPARSE_ENTRY_KIND_COUNT 6
#define TRACKPARSE_EOL_COUNT 4
#define TRACKPARSE_TRACKLIST_WARNING_CODE_COUNT 9
#define TRACKPARSE_SEVERITY_COUNT 2
#define TRACKPARSE_EDIT_OP_COUNT 6
#define TRACKPARSE_EDIT_WARNING_CODE_COUNT 3

/* The spec name of each value ("remix", "noSeparator", "time-out-of-order"...), or "" for a
 * value out of range. trackparse_eol_name returns the line ending itself ("\n", "\r\n", "\r",
 * ""), so raw + eol rebuilds a line. */
TRACKPARSE_API const char *trackparse_mode_name(trackparse_mode v);
TRACKPARSE_API const char *trackparse_role_name(trackparse_role v);
TRACKPARSE_API const char *trackparse_source_name(trackparse_source v);
TRACKPARSE_API const char *trackparse_version_type_name(trackparse_version_type v);
TRACKPARSE_API const char *trackparse_delimiter_name(trackparse_delimiter v);
TRACKPARSE_API const char *trackparse_junk_kind_name(trackparse_junk_kind v);
TRACKPARSE_API const char *trackparse_warning_name(trackparse_warning v);
TRACKPARSE_API const char *trackparse_entry_kind_name(trackparse_entry_kind v);
TRACKPARSE_API const char *trackparse_eol_name(trackparse_eol v);
TRACKPARSE_API const char *
trackparse_tracklist_warning_code_name(trackparse_tracklist_warning_code v);
TRACKPARSE_API const char *trackparse_severity_name(trackparse_severity v);
TRACKPARSE_API const char *trackparse_edit_op_name(trackparse_edit_op v);
TRACKPARSE_API const char *trackparse_edit_warning_code_name(trackparse_edit_warning_code v);

/* Look a value up by its spec name (len may be TRACKPARSE_NUL_TERMINATED). */
TRACKPARSE_API bool trackparse_mode_from_name(const char *name, size_t len, trackparse_mode *out);
TRACKPARSE_API bool trackparse_version_type_from_name(const char *name, size_t len,
                                                      trackparse_version_type *out);
TRACKPARSE_API bool trackparse_junk_kind_from_name(const char *name, size_t len,
                                                   trackparse_junk_kind *out);

/* ------------------------------------------------------------------------------------------ */
/* Parsed track (parsed-track.schema.json)                                                     */

typedef struct trackparse_artist {
  trackparse_str name;
  trackparse_role role;
  /** The text that joined this artist to the previous one (" & ", ", "...); NULL for the first. */
  const trackparse_str *joiner;
  trackparse_source source;
} trackparse_artist;

typedef struct trackparse_version {
  trackparse_version_type type;
  /** The type's spec name; differs from trackparse_version_type_name(type) only for names that
   * options.keywords introduced. */
  trackparse_str type_name;
  trackparse_str raw;
  const trackparse_artist *artists;
  size_t artists_count;
  const trackparse_str *modifiers;
  size_t modifiers_count;
  const trackparse_str *descriptor;
  const int *year;
  bool unknown_artist;
  trackparse_delimiter delimiter;
} trackparse_version;

typedef struct trackparse_junk {
  trackparse_str raw;
  trackparse_junk_kind kind;
  /** The kind's spec name (see trackparse_version.type_name). */
  trackparse_str kind_name;
} trackparse_junk;

typedef struct trackparse_flags {
  bool explicit_; /**< "explicit" in JSON (explicit is a C++ keyword) */
  bool clean;
  bool unknown_artist;
  bool unknown_title;
} trackparse_flags;

typedef struct trackparse_position {
  trackparse_str raw;
  int number;
} trackparse_position;

typedef struct trackparse_timestamp {
  trackparse_str raw;
  int seconds;
} trackparse_timestamp;

/** The result of trackparse_parse. */
typedef struct trackparse_track {
  trackparse_str input;
  trackparse_mode mode;
  const trackparse_position *position;
  const trackparse_timestamp *timestamp;
  const trackparse_artist *artists;
  size_t artists_count;
  trackparse_str title;
  trackparse_str full_title;
  const trackparse_version *versions;
  size_t versions_count;
  const int *year;
  trackparse_flags flags;
  const trackparse_junk *junk;
  size_t junk_count;
  const trackparse_warning *warnings;
  size_t warnings_count;
} trackparse_track;

/** A list of artists (the result of trackparse_parse_artists). */
typedef struct trackparse_artists {
  const trackparse_artist *items;
  size_t count;
} trackparse_artists;

/* ------------------------------------------------------------------------------------------ */
/* Options                                                                                      */

typedef struct trackparse_parse_options trackparse_parse_options;
typedef struct trackparse_format_options trackparse_format_options;
typedef struct trackparse_tracklist_options trackparse_tracklist_options;

TRACKPARSE_API trackparse_parse_options *trackparse_parse_options_new(void);
TRACKPARSE_API void trackparse_parse_options_free(trackparse_parse_options *o);
/* Setters return false only when out of memory (the option is then unchanged). */
TRACKPARSE_API bool trackparse_parse_options_set_mode(trackparse_parse_options *o,
                                                      trackparse_mode mode);
TRACKPARSE_API bool trackparse_parse_options_set_uploader(trackparse_parse_options *o,
                                                          const char *s, size_t len);
TRACKPARSE_API bool trackparse_parse_options_add_known_artist(trackparse_parse_options *o,
                                                              const char *s, size_t len);
TRACKPARSE_API bool trackparse_parse_options_set_split_and(trackparse_parse_options *o,
                                                           trackparse_split_and v);
/* R12 keywords: extra vocabulary. type and kind are spec names, so new names are possible. */
TRACKPARSE_API bool trackparse_parse_options_add_version_head(trackparse_parse_options *o,
                                                              const char *key, size_t key_len,
                                                              const char *type, size_t type_len);
TRACKPARSE_API bool trackparse_parse_options_add_descriptor(trackparse_parse_options *o,
                                                            const char *s, size_t len);
TRACKPARSE_API bool trackparse_parse_options_add_genre(trackparse_parse_options *o, const char *s,
                                                       size_t len);
TRACKPARSE_API bool trackparse_parse_options_add_junk(trackparse_parse_options *o,
                                                      const char *phrase, size_t phrase_len,
                                                      const char *kind, size_t kind_len);
TRACKPARSE_API bool trackparse_parse_options_add_feat_marker(trackparse_parse_options *o,
                                                             const char *s, size_t len);

TRACKPARSE_API trackparse_format_options *trackparse_format_options_new(void);
TRACKPARSE_API void trackparse_format_options_free(trackparse_format_options *o);
TRACKPARSE_API bool trackparse_format_options_set_feat(trackparse_format_options *o,
                                                       trackparse_feat v);
/** The marker written before featured artists; "" is a valid marker. Unset: the input's. */
TRACKPARSE_API bool trackparse_format_options_set_feat_marker(trackparse_format_options *o,
                                                              const char *s, size_t len);
TRACKPARSE_API bool trackparse_format_options_set_joiners(trackparse_format_options *o,
                                                          trackparse_joiners v);
TRACKPARSE_API bool trackparse_format_options_set_versions(trackparse_format_options *o,
                                                           trackparse_versions v);
TRACKPARSE_API bool trackparse_format_options_set_omit_producers(trackparse_format_options *o,
                                                                 bool v);
TRACKPARSE_API bool trackparse_format_options_set_position(trackparse_format_options *o, bool v);

TRACKPARSE_API trackparse_tracklist_options *trackparse_tracklist_options_new(void);
TRACKPARSE_API void trackparse_tracklist_options_free(trackparse_tracklist_options *o);
/** Default for tracklists: TRACKPARSE_MODE_TRACKLIST. */
TRACKPARSE_API bool trackparse_tracklist_options_set_mode(trackparse_tracklist_options *o,
                                                          trackparse_mode mode);
TRACKPARSE_API bool trackparse_tracklist_options_add_known_artist(trackparse_tracklist_options *o,
                                                                  const char *s, size_t len);
TRACKPARSE_API bool trackparse_tracklist_options_set_split_and(trackparse_tracklist_options *o,
                                                               trackparse_split_and v);
TRACKPARSE_API bool trackparse_tracklist_options_add_version_head(trackparse_tracklist_options *o,
                                                                  const char *key, size_t key_len,
                                                                  const char *type,
                                                                  size_t type_len);
TRACKPARSE_API bool trackparse_tracklist_options_add_descriptor(trackparse_tracklist_options *o,
                                                                const char *s, size_t len);
TRACKPARSE_API bool trackparse_tracklist_options_add_genre(trackparse_tracklist_options *o,
                                                           const char *s, size_t len);
TRACKPARSE_API bool trackparse_tracklist_options_add_junk(trackparse_tracklist_options *o,
                                                          const char *phrase, size_t phrase_len,
                                                          const char *kind, size_t kind_len);
TRACKPARSE_API bool trackparse_tracklist_options_add_feat_marker(trackparse_tracklist_options *o,
                                                                 const char *s, size_t len);
/** The audio length in seconds, for time-after-audio-end; 0 is a valid length. Unset: none. */
TRACKPARSE_API bool trackparse_tracklist_options_set_audio_seconds(trackparse_tracklist_options *o,
                                                                   double seconds);
TRACKPARSE_API bool trackparse_tracklist_options_add_marker_word(trackparse_tracklist_options *o,
                                                                 const char *s, size_t len);

/* ------------------------------------------------------------------------------------------ */
/* Single tracks                                                                                */

/** R1-R9: parse one track string. NULL only when out of memory. */
TRACKPARSE_API trackparse_track *trackparse_parse(const char *s, size_t len);
TRACKPARSE_API trackparse_track *trackparse_parse_with(const char *s, size_t len,
                                                       const trackparse_parse_options *o);
TRACKPARSE_API void trackparse_track_free(trackparse_track *t);

/** R10: parse an artist string ("A feat. B & C") into artists. */
TRACKPARSE_API trackparse_artists *trackparse_parse_artists(const char *s, size_t len);
TRACKPARSE_API trackparse_artists *trackparse_parse_artists_with(const char *s, size_t len,
                                                                 const trackparse_parse_options *o);
TRACKPARSE_API void trackparse_artists_free(trackparse_artists *a);

/** A string the caller owns (format, normalize, JSON). */
typedef struct trackparse_string {
  trackparse_str str;
} trackparse_string;
TRACKPARSE_API void trackparse_string_free(trackparse_string *s);

/** R11: write a parsed track back as "Artists - Title (Versions)". */
TRACKPARSE_API trackparse_string *trackparse_format(const trackparse_track *t);
TRACKPARSE_API trackparse_string *trackparse_format_with(const trackparse_track *t,
                                                         const trackparse_format_options *o);
/** R1: the normalized form of a string. */
TRACKPARSE_API trackparse_string *trackparse_normalize(const char *s, size_t len);
/** Every artist of a track: the artist side, then each version's, in order. */
TRACKPARSE_API trackparse_artists *trackparse_all_artists(const trackparse_track *t);

/** A parser that keeps its options and their tables, for many calls. Immutable, thread-safe. */
typedef struct trackparse_parser trackparse_parser;
TRACKPARSE_API trackparse_parser *trackparse_parser_new(const trackparse_parse_options *o);
TRACKPARSE_API void trackparse_parser_free(trackparse_parser *p);
TRACKPARSE_API trackparse_track *trackparse_parser_parse(const trackparse_parser *p, const char *s,
                                                         size_t len);
TRACKPARSE_API trackparse_artists *trackparse_parser_parse_artists(const trackparse_parser *p,
                                                                   const char *s, size_t len);

/* ------------------------------------------------------------------------------------------ */
/* Tracklists (R13, tracklist.schema.json)                                                      */

typedef struct trackparse_parts {
  trackparse_str lead;
  trackparse_str time;
  trackparse_str gap;
  trackparse_str rest;
} trackparse_parts;

typedef struct trackparse_tracklist_time {
  trackparse_str raw;
  double seconds;
} trackparse_tracklist_time;

typedef struct trackparse_entry {
  int line;
  trackparse_entry_kind kind;
  trackparse_eol eol;
  trackparse_str raw;
  trackparse_parts parts;
  const trackparse_tracklist_time *time;
  const trackparse_track *track;
  bool layered;
} trackparse_entry;

typedef struct trackparse_suggestion {
  int line;
  trackparse_str raw;
  double seconds;
} trackparse_suggestion;

typedef struct trackparse_tracklist_warning {
  trackparse_tracklist_warning_code code;
  int line;
  trackparse_severity severity;
  const trackparse_suggestion *suggestion;
} trackparse_tracklist_warning;

typedef struct trackparse_stats {
  int lines;
  int tracks;
  int markers;
  int timed;
  const double *first_time;
  const double *last_time;
} trackparse_stats;

typedef struct trackparse_tracklist {
  trackparse_str spec_version;
  const trackparse_entry *entries;
  size_t entries_count;
  const trackparse_tracklist_warning *warnings;
  size_t warnings_count;
  trackparse_stats stats;
} trackparse_tracklist;

TRACKPARSE_API trackparse_tracklist *trackparse_parse_tracklist(const char *s, size_t len);
TRACKPARSE_API trackparse_tracklist *
trackparse_parse_tracklist_with(const char *s, size_t len, const trackparse_tracklist_options *o);
TRACKPARSE_API void trackparse_tracklist_free(trackparse_tracklist *t);

/** A list of R13.9 edits, applied in order. */
typedef struct trackparse_edits trackparse_edits;
TRACKPARSE_API trackparse_edits *trackparse_edits_new(void);
TRACKPARSE_API void trackparse_edits_free(trackparse_edits *e);
/* Each returns false only when out of memory. Lines are 1-based. */
TRACKPARSE_API bool trackparse_edits_set_time(trackparse_edits *e, long long line, double seconds);
/** Shift every timed line. */
TRACKPARSE_API bool trackparse_edits_shift(trackparse_edits *e, double seconds);
/** Shift the timed lines from `from` on. */
TRACKPARSE_API bool trackparse_edits_shift_from(trackparse_edits *e, double seconds,
                                                long long from);
/** Shift the listed lines. */
TRACKPARSE_API bool trackparse_edits_shift_lines(trackparse_edits *e, double seconds,
                                                 const long long *lines, size_t count);
TRACKPARSE_API bool trackparse_edits_sort_by_time(trackparse_edits *e);
TRACKPARSE_API bool trackparse_edits_set_rest(trackparse_edits *e, long long line, const char *rest,
                                              size_t len);
/** Insert text as a new line after `after` (0: at the top). */
TRACKPARSE_API bool trackparse_edits_insert(trackparse_edits *e, long long after, const char *text,
                                            size_t len);
TRACKPARSE_API bool trackparse_edits_delete(trackparse_edits *e, long long line);

typedef struct trackparse_edit_warning {
  trackparse_edit_warning_code code;
  /** The index of the edit in the list (0-based). */
  size_t edit;
  /** The line the warning is about, when there is one. */
  const long long *line;
} trackparse_edit_warning;

typedef struct trackparse_edit_result {
  trackparse_str text;
  const long long *changed;
  size_t changed_count;
  const trackparse_edit_warning *warnings;
  size_t warnings_count;
} trackparse_edit_result;

/** R13.9: apply edits to a tracklist text. Lines not named stay byte for byte. */
TRACKPARSE_API trackparse_edit_result *trackparse_apply_edits(const char *s, size_t len,
                                                              const trackparse_edits *e);
TRACKPARSE_API void trackparse_edit_result_free(trackparse_edit_result *r);

/* ------------------------------------------------------------------------------------------ */
/* JSON: the bytes JavaScript's JSON.stringify writes for the same result                       */

TRACKPARSE_API trackparse_string *trackparse_track_to_json(const trackparse_track *t);
TRACKPARSE_API trackparse_string *trackparse_artists_to_json(const trackparse_artists *a);
TRACKPARSE_API trackparse_string *trackparse_tracklist_to_json(const trackparse_tracklist *t);
TRACKPARSE_API trackparse_string *trackparse_edit_result_to_json(const trackparse_edit_result *r);

#ifdef __cplusplus
}
#endif

#endif /* TRACKPARSE_H */
