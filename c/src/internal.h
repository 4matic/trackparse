/* internal.h: every internal type, constant and function of the C port.
 *
 * One section per Go file (go/<file>.go -> c/src/<file>.c), in pipeline order. Each prototype
 * keeps the Go doc comment (shortened) and the rule IDs. Names follow c-brief.md: Go fooBar ->
 * tp_foo_bar, method (t *T) m -> tp_T_m(T *t, ...), constants -> TP_UPPER_CASE. See PORTING.md
 * for the translation decisions every .c file must follow (int widths, nil slices, closures,
 * maps, ownership).
 *
 * Not here: the runtime (rt.h), the R0 primitives and phrase tables (words.h), Unicode
 * (unicode.h, unicode_gen.h), digit runs (digits.h), the generated data (data_gen.h), the JSON
 * reader (wire/json_read.h) and the JSON door entry points (wire/wire.h).
 *
 * Conventions used throughout this header:
 * - Go int is ptrdiff_t (as in words.h), Go []int is tp_idxvec, Go int64 is int64_t. Exception:
 *   the JS-number fields of edits and edit results (tp_edit line/after/from/lines, the edit
 *   warning line, changed) are int64_t / tp_intvec, the public API's long long. Cast explicitly
 *   between ptrdiff_t and size_t (a vector's n).
 * - Go typed strings (Mode, Role, VersionType...) are tp_str, named by a typedef for reading
 *   (tp_mode, tp_role...), with the values as TP_S() constants compared with tp_str_eq. TP_S is
 *   a compound literal: in a static initializer write {"remix", 5} instead.
 * - Nullable Go pointers (*int, *string, *float64, *T) are const pointers into the arena; NULL
 *   is Go nil.
 * - (T, bool) -> bool f(..., T *out); (A, B) -> A f(..., B *out). out is written only when the
 *   result is true, unless the comment says otherwise.
 * - Go passes a struct by value or by pointer; the C prototypes do the same.
 */
#ifndef TP_INTERNAL_H
#define TP_INTERNAL_H

#include "rt.h"

#include "data_gen.h"
#include "digits.h"
#include "unicode.h"
#include "unicode_gen.h"
#include "words.h"

/* ============================================================================================ */
/* Forward declarations and slice types (Go []T -> tp_T_vec), grouped by the Go file of T.      */
/* A TP_VEC only needs T declared, so every vector type is available to every section.         */

/* types.go */
typedef struct tp_artist tp_artist;
typedef struct tp_version tp_version;
typedef struct tp_junk tp_junk;
typedef struct tp_flags tp_flags;
typedef struct tp_position tp_position;
typedef struct tp_timestamp tp_timestamp;
typedef struct tp_track tp_track;
typedef struct tp_parts tp_parts;
typedef struct tp_tracklist_time tp_tracklist_time;
typedef struct tp_entry tp_entry;
typedef struct tp_suggestion tp_suggestion;
typedef struct tp_tracklist_warning tp_tracklist_warning;
typedef struct tp_stats tp_stats;
typedef struct tp_tracklist tp_tracklist;
typedef struct tp_edit tp_edit;
typedef struct tp_edit_warning tp_edit_warning;
typedef struct tp_edit_result tp_edit_result;
typedef struct tp_raw_edit_warning tp_raw_edit_warning;
typedef struct tp_raw_edit_result tp_raw_edit_result;
typedef TP_VEC(tp_artist) tp_artist_vec;
typedef TP_VEC(const tp_artist *) tp_artist_ptr_vec; /* Go []*Artist (format.go) */
typedef TP_VEC(tp_version) tp_version_vec;
typedef TP_VEC(tp_junk) tp_junk_vec;
typedef TP_VEC(tp_entry) tp_entry_vec;
typedef TP_VEC(tp_tracklist_warning) tp_tracklist_warning_vec;
typedef TP_VEC(tp_edit) tp_edit_vec;
typedef TP_VEC(tp_edit_warning) tp_edit_warning_vec;
typedef TP_VEC(tp_raw_edit_warning) tp_raw_edit_warning_vec;

/* Builtin element types not in rt.h. */
typedef TP_VEC(bool) tp_bool_vec;        /* Go []bool */
typedef TP_VEC(tp_runes) tp_runes_vec;   /* Go [][]rune */
typedef TP_VEC(ptrdiff_t) tp_idxvec;     /* Go []int (rt.h's tp_intvec is Go []int64) */
typedef TP_VEC(tp_idxvec) tp_idxvec_vec; /* Go [][]int (creditLists heaps) */

/* options.go */
typedef struct tp_version_head tp_version_head;
typedef struct tp_junk_phrase tp_junk_phrase;
typedef struct tp_keywords tp_keywords;
typedef struct tp_parse_options tp_parse_options;
typedef struct tp_format_options tp_format_options;
typedef struct tp_tracklist_options tp_tracklist_options;
typedef struct tp_requested_mode tp_requested_mode;
typedef struct tp_kv tp_kv;
typedef struct tp_keyword_lists tp_keyword_lists;
typedef struct tp_resolved_options tp_resolved_options;
typedef struct tp_resolved_format tp_resolved_format;
typedef struct tp_resolved_tracklist tp_resolved_tracklist;
typedef TP_VEC(tp_version_head) tp_version_head_vec;
typedef TP_VEC(tp_junk_phrase) tp_junk_phrase_vec;
typedef TP_VEC(tp_kv) tp_kv_vec;

/* context.go */
typedef struct tp_engine tp_engine;
typedef struct tp_ctx tp_ctx;

/* tables.go */
typedef struct tp_spaced_joiner tp_spaced_joiner;
typedef struct tp_run_info tp_run_info;
typedef struct tp_tables tp_tables;
typedef TP_VEC(tp_spaced_joiner) tp_spaced_joiner_vec;

/* mode.go */
typedef struct tp_positioned_junk tp_positioned_junk;
typedef struct tp_piece tp_piece;
typedef struct tp_prelude_result tp_prelude_result;
typedef TP_VEC(tp_positioned_junk) tp_positioned_junk_vec;

/* prefix.go */
typedef struct tp_prefix_result tp_prefix_result;
typedef struct tp_time_match tp_time_match;

/* scanner.go */
typedef struct tp_group tp_group;
typedef struct tp_group_map tp_group_map;
typedef struct tp_skel tp_skel;
typedef struct tp_offsets tp_offsets;
typedef struct tp_indexed_group tp_indexed_group;
typedef struct tp_segment tp_segment;
typedef TP_VEC(const tp_group *) tp_group_ptr_vec;
typedef TP_VEC(tp_indexed_group) tp_indexed_group_vec;
typedef TP_VEC(tp_segment) tp_segment_vec;
typedef TP_VEC(tp_skel) tp_skel_vec;

/* credits.go */
typedef struct tp_credit tp_credit;
typedef struct tp_positioned_year tp_positioned_year;
typedef struct tp_list_options tp_list_options;
typedef struct tp_list_result tp_list_result;
typedef struct tp_joiner_hit tp_joiner_hit;
typedef struct tp_raw_name tp_raw_name;
typedef TP_VEC(tp_credit) tp_credit_vec;
typedef TP_VEC(tp_positioned_year) tp_positioned_year_vec;
typedef TP_VEC(tp_raw_name) tp_raw_name_vec;

/* classify.go */
typedef struct tp_version_draft tp_version_draft;
typedef struct tp_classification tp_classification;
typedef struct tp_site tp_site;
typedef struct tp_run_item tp_run_item;
typedef TP_VEC(tp_version_draft) tp_version_draft_vec;
typedef TP_VEC(tp_run_item) tp_run_item_vec;

/* separator.go */
typedef struct tp_peeled tp_peeled;
typedef struct tp_separator_result tp_separator_result;
typedef struct tp_separator_input tp_separator_input;
typedef struct tp_split_parts tp_split_parts;
typedef TP_VEC(tp_peeled) tp_peeled_vec;

/* artists.go */
typedef struct tp_artist_side_result tp_artist_side_result;
typedef struct tp_feat_marker tp_feat_marker;

/* title.go */
typedef struct tp_extracted tp_extracted;
typedef struct tp_title_side_result tp_title_side_result;
typedef struct tp_bullet_suffix tp_bullet_suffix;
typedef struct tp_marker_hit tp_marker_hit;
typedef TP_VEC(tp_marker_hit) tp_marker_hit_vec;

/* assemble.go */
typedef struct tp_dedup_entry tp_dedup_entry;
typedef struct tp_credit_lists tp_credit_lists;
typedef TP_VEC(tp_dedup_entry) tp_dedup_entry_vec;

/* parser.go */
typedef struct tp_staged tp_staged;
typedef struct tp_line_probe tp_line_probe;

/* tl_split.go */
typedef struct tp_tl_line tp_tl_line;
typedef TP_VEC(tp_tl_line) tp_tl_line_vec;

/* tl_times.go */
typedef struct tp_time_style tp_time_style;
typedef struct tp_time_scan tp_time_scan;
typedef struct tp_time_shape tp_time_shape;
typedef struct tp_formatted_time tp_formatted_time;

/* tl_positions.go */
typedef struct tp_bare_candidate tp_bare_candidate;

/* tl_warnings.go */
typedef struct tp_warning_input tp_warning_input;
typedef struct tp_digit_slot tp_digit_slot;
typedef struct tp_digit_change tp_digit_change;
typedef struct tp_time_digits tp_time_digits;
typedef struct tp_timed_entry tp_timed_entry;
typedef TP_VEC(tp_digit_slot) tp_digit_slot_vec;
typedef TP_VEC(tp_digit_change) tp_digit_change_vec;
typedef TP_VEC(tp_timed_entry) tp_timed_entry_vec;

/* tl_parse.go */
typedef struct tp_tl_row tp_tl_row;
typedef TP_VEC(tp_tl_row) tp_tl_row_vec;

/* tl_edits.go */
typedef struct tp_work_line tp_work_line;
typedef struct tp_edit_ctx tp_edit_ctx;
typedef struct tp_retimed tp_retimed;
typedef struct tp_touched tp_touched;
typedef struct tp_time_block tp_time_block;
typedef TP_VEC(tp_work_line *) tp_work_line_ptr_vec;
typedef TP_VEC(tp_retimed) tp_retimed_vec;
typedef TP_VEC(tp_time_block) tp_time_block_vec;

/* json.go */
typedef struct tp_jw tp_jw;

/* trackparse.go */
typedef struct tp_parser tp_parser;

/* ============================================================================================ */
/* types.go: the result types (mirror parsed-track.schema.json and tracklist.schema.json)       */

/* Mode is the mode a string is parsed in (R2). In a track: clean, youtube, filename or
 * tracklist, never auto. In options, auto asks the parser to pick one, and "" means the
 * option's default (auto for a parse, tracklist for a tracklist). Any other string behaves as
 * JS does with an unknown mode: no youtube detection, filenames still detected. */
typedef tp_str tp_mode;
#define TP_MODE_AUTO TP_S("auto")
#define TP_MODE_CLEAN TP_S("clean")
#define TP_MODE_YOUTUBE TP_S("youtube")
#define TP_MODE_FILENAME TP_S("filename")
#define TP_MODE_TRACKLIST TP_S("tracklist")
/* The modes a track can have (not auto), in the spec's order. A new arena vector per call. */
TP_PRIVATE tp_strvec tp_all_modes(void);

/* Role is what an artist did on the track (R7). */
typedef tp_str tp_role;
#define TP_ROLE_PRIMARY TP_S("primary")
#define TP_ROLE_FEATURED TP_S("featured")
#define TP_ROLE_REMIXER TP_S("remixer")
#define TP_ROLE_PRODUCER TP_S("producer")
TP_PRIVATE tp_strvec tp_all_roles(void);

/* Source is where in the string an artist was found. */
typedef tp_str tp_source;
#define TP_SOURCE_ARTIST TP_S("artist")
#define TP_SOURCE_TITLE TP_S("title")
#define TP_SOURCE_VERSION TP_S("version")
TP_PRIVATE tp_strvec tp_all_sources(void);

/* VersionType is the type of a version (R5). A version head added through keywords can carry
 * any string, which the output keeps as given. */
typedef tp_str tp_version_type;
#define TP_VERSION_TYPE_REMIX TP_S("remix")
#define TP_VERSION_TYPE_BOOTLEG TP_S("bootleg")
#define TP_VERSION_TYPE_VIP TP_S("vip")
#define TP_VERSION_TYPE_EDIT TP_S("edit")
#define TP_VERSION_TYPE_FLIP TP_S("flip")
#define TP_VERSION_TYPE_REFIX TP_S("refix")
#define TP_VERSION_TYPE_REWORK TP_S("rework")
#define TP_VERSION_TYPE_MASHUP TP_S("mashup")
#define TP_VERSION_TYPE_BLEND TP_S("blend")
#define TP_VERSION_TYPE_DUB TP_S("dub")
#define TP_VERSION_TYPE_MIX TP_S("mix")
#define TP_VERSION_TYPE_EXTENDED TP_S("extended")
#define TP_VERSION_TYPE_RADIO TP_S("radio")
#define TP_VERSION_TYPE_CLUB TP_S("club")
#define TP_VERSION_TYPE_ORIGINAL TP_S("original")
#define TP_VERSION_TYPE_INSTRUMENTAL TP_S("instrumental")
#define TP_VERSION_TYPE_ACAPELLA TP_S("acapella")
#define TP_VERSION_TYPE_LIVE TP_S("live")
#define TP_VERSION_TYPE_ACOUSTIC TP_S("acoustic")
#define TP_VERSION_TYPE_REMASTER TP_S("remaster")
#define TP_VERSION_TYPE_DEMO TP_S("demo")
#define TP_VERSION_TYPE_REPRISE TP_S("reprise")
#define TP_VERSION_TYPE_COVER TP_S("cover")
#define TP_VERSION_TYPE_VERSION TP_S("version")
#define TP_VERSION_TYPE_SPED_UP TP_S("spedUp")
#define TP_VERSION_TYPE_SLOWED TP_S("slowed")
#define TP_VERSION_TYPE_NIGHTCORE TP_S("nightcore")
/* The 27 version types of the spec, in the spec's order. */
TP_PRIVATE tp_strvec tp_all_version_types(void);

/* Delimiter is how a version was written: in a bracket group, or after a dash. */
typedef tp_str tp_delimiter;
#define TP_DELIMITER_PAREN TP_S("(")
#define TP_DELIMITER_SQUARE TP_S("[")
#define TP_DELIMITER_CURLY TP_S("{")
#define TP_DELIMITER_DASH TP_S("-")
TP_PRIVATE tp_strvec tp_all_delimiters(void);

/* JunkKind is what kind of fragment a junk entry is (R5, R8.7). keywords can add any string. */
typedef tp_str tp_junk_kind;
#define TP_JUNK_KIND_VIDEO TP_S("video")
#define TP_JUNK_KIND_AUDIO TP_S("audio")
#define TP_JUNK_KIND_LYRICS TP_S("lyrics")
#define TP_JUNK_KIND_QUALITY TP_S("quality")
#define TP_JUNK_KIND_PROMO TP_S("promo")
#define TP_JUNK_KIND_PLATFORM TP_S("platform")
#define TP_JUNK_KIND_LABEL TP_S("label")
#define TP_JUNK_KIND_GENRE TP_S("genre")
#define TP_JUNK_KIND_OTHER TP_S("other")
#define TP_JUNK_KIND_NOTE TP_S("note") /* a note after the record label (R8.7) */
TP_PRIVATE tp_strvec tp_all_junk_kinds(void);

/* Warning is a warning of the single-track parser. */
typedef tp_str tp_warning;
#define TP_WARNING_NO_SEPARATOR TP_S("noSeparator")
#define TP_WARNING_AMBIGUOUS_SEPARATOR TP_S("ambiguousSeparator")
#define TP_WARNING_UNSPACED_DASH_SPLIT TP_S("unspacedDashSplit")
#define TP_WARNING_ASYMMETRIC_DASH_SPLIT TP_S("asymmetricDashSplit")
#define TP_WARNING_BY_SPLIT TP_S("bySplit")
#define TP_WARNING_QUOTED_TITLE_SPLIT TP_S("quotedTitleSplit")
#define TP_WARNING_AMBIGUOUS_MIX_CREDIT TP_S("ambiguousMixCredit")
#define TP_WARNING_UNBALANCED_BRACKETS TP_S("unbalancedBrackets")
TP_PRIVATE tp_strvec tp_all_warnings(void);

/* EntryKind is what a line of a tracklist is (R13.3). */
typedef tp_str tp_entry_kind;
#define TP_ENTRY_KIND_TRACK TP_S("track")
#define TP_ENTRY_KIND_MARKER TP_S("marker")
#define TP_ENTRY_KIND_HEADER TP_S("header")
#define TP_ENTRY_KIND_LINK TP_S("link")
#define TP_ENTRY_KIND_NOTE TP_S("note")
#define TP_ENTRY_KIND_BLANK TP_S("blank")
TP_PRIVATE tp_strvec tp_all_entry_kinds(void);

/* Eol is the line terminator that followed a line (R13.1.1); NONE is "" (last line). */
typedef tp_str tp_eol;
#define TP_EOL_LF TP_S("\n")
#define TP_EOL_CRLF TP_S("\r\n")
#define TP_EOL_CR TP_S("\r")
#define TP_EOL_NONE TP_S("")
TP_PRIVATE tp_strvec tp_all_eols(void);

/* WarningCode is the code of a tracklist warning (R13.7), in the table order of the spec. */
typedef tp_str tp_warning_code;
#define TP_WARNING_CODE_TIME_MISSING TP_S("time-missing")
#define TP_WARNING_CODE_TIME_OUT_OF_ORDER TP_S("time-out-of-order")
#define TP_WARNING_CODE_TIME_DUPLICATE TP_S("time-duplicate")
#define TP_WARNING_CODE_NO_START_AT_ZERO TP_S("no-start-at-zero")
#define TP_WARNING_CODE_TIME_AFTER_AUDIO_END TP_S("time-after-audio-end")
#define TP_WARNING_CODE_INVALID_TIME TP_S("invalid-time")
#define TP_WARNING_CODE_POSITION_GAP TP_S("position-gap")
#define TP_WARNING_CODE_POSITION_DUPLICATE TP_S("position-duplicate")
#define TP_WARNING_CODE_LINE_UNPARSED TP_S("line-unparsed")
TP_PRIVATE tp_strvec tp_all_warning_codes(void);

/* Severity is how serious a tracklist warning is. */
typedef tp_str tp_severity;
#define TP_SEVERITY_INFO TP_S("info")
#define TP_SEVERITY_WARNING TP_S("warning")
TP_PRIVATE tp_strvec tp_all_severities(void);

/* EditOp names the operation of an edit (R13.9). */
typedef tp_str tp_edit_op;
#define TP_EDIT_OP_SET_TIME TP_S("set-time")
#define TP_EDIT_OP_SHIFT TP_S("shift")
#define TP_EDIT_OP_SORT_BY_TIME TP_S("sort-by-time")
#define TP_EDIT_OP_SET_REST TP_S("set-rest")
#define TP_EDIT_OP_INSERT TP_S("insert")
#define TP_EDIT_OP_DELETE TP_S("delete")
TP_PRIVATE tp_strvec tp_all_edit_ops(void);

/* EditWarningCode is the code of an edit warning (R13.9). */
typedef tp_str tp_edit_warning_code;
#define TP_EDIT_WARNING_CODE_EDIT_INVALID TP_S("edit-invalid")
#define TP_EDIT_WARNING_CODE_TIME_CLAMPED TP_S("time-clamped")
#define TP_EDIT_WARNING_CODE_STYLE_UPGRADED TP_S("style-upgraded")
TP_PRIVATE tp_strvec tp_all_edit_warning_codes(void);

/* C addition (no Go counterpart): the index of name in names, or -1. The public API maps the
 * internal strings to its enums with it (mind tp_all_modes having no auto). */
TP_PRIVATE ptrdiff_t tp_name_index(tp_strvec names, tp_str name);

/* Artist is one credited artist. */
struct tp_artist {
  tp_str name; /* as written, trimmed */
  tp_role role;
  const tp_str *joiner; /* raw joiner or marker before the name; NULL for the first of a list */
  tp_source source;
};

/* Version is one version of the track: a remix, an edit, a live take, a remaster (R5, R8). */
struct tp_version {
  tp_version_type type;
  tp_str raw;            /* without its brackets */
  tp_artist_vec artists; /* role remixer; empty without a credit */
  tp_strvec modifiers;   /* other version words, lowercase keys */
  const tp_str *descriptor;
  const ptrdiff_t *year;
  bool unknown_artist; /* the credit is a placeholder (ID, Unknown) */
  tp_delimiter delimiter;
};

/* Junk is a fragment that says nothing about the track (extracted, not deleted). */
struct tp_junk {
  tp_str raw;
  tp_junk_kind kind;
};

/* Flags are yes/no facts about the track. explicit_ is "explicit" in JSON. */
struct tp_flags {
  bool explicit_;
  bool clean;
  bool unknown_artist;
  bool unknown_title;
};

/* Position is a track number in front of the string (R4.2). */
struct tp_position {
  tp_str raw;
  ptrdiff_t number;
};

/* Timestamp is a cue time in front of a single track string (R4.1), in whole seconds. */
struct tp_timestamp {
  tp_str raw;
  ptrdiff_t seconds;
};

/* Track is the result of parse: everything recognized in one track string. */
struct tp_track {
  tp_str input;
  tp_mode mode; /* never auto */
  const tp_position *position;
  const tp_timestamp *timestamp;
  tp_artist_vec artists; /* primary, featured, producer; remixers are in versions */
  tp_str title;
  tp_str full_title;
  tp_version_vec versions;
  const ptrdiff_t *year;
  const tp_str *label; /* R8.7: the record label after a spaced bullet, or NULL */
  tp_flags flags;
  tp_junk_vec junk;
  tp_strvec warnings; /* tp_warning values */
};

/* Parts is a line cut into lead + time + gap + rest, which join back to it (R13.1.3). */
struct tp_parts {
  tp_str lead;
  tp_str time;
  tp_str gap;
  tp_str rest;
};

/* TracklistTime is the time of a tracklist line (R13.2.5); seconds may carry a fraction. */
struct tp_tracklist_time {
  tp_str raw;
  double seconds;
};

/* Entry is one line of a tracklist. */
struct tp_entry {
  ptrdiff_t line; /* 1-based */
  tp_entry_kind kind;
  tp_eol eol;
  tp_str raw; /* never normalized */
  tp_parts parts;
  const tp_tracklist_time *time;
  const tp_track *track; /* track and marker entries */
  bool layered;          /* R13.13 */
};

/* Suggestion is a fix for a time-out-of-order warning that changes one or two digits (R13.7). */
struct tp_suggestion {
  ptrdiff_t line;
  tp_str raw;
  double seconds;
};

/* TracklistWarning is a warning about a tracklist (R13.7). */
struct tp_tracklist_warning {
  tp_warning_code code;
  ptrdiff_t line;
  tp_severity severity;
  const tp_suggestion *suggestion;
};

/* Stats are counts over a tracklist. */
struct tp_stats {
  ptrdiff_t lines;
  ptrdiff_t tracks;
  ptrdiff_t markers;
  ptrdiff_t timed;
  const double *first_time;
  const double *last_time;
};

/* Tracklist is the result of parseTracklist (R13.1.2: raw + eol joined is the text). */
struct tp_tracklist {
  tp_str spec_version;
  tp_entry_vec entries;
  tp_tracklist_warning_vec warnings; /* ordered by line */
  tp_stats stats;
};

/* Edit is one edit of a tracklist text (R13.9); each operation reads only its own fields. */
struct tp_edit {
  tp_edit_op op;
  int64_t line;        /* set-time, set-rest, delete */
  double seconds;      /* set-time: the new time; shift: the amount */
  bool has_lines;      /* Go Lines != nil: lines was given (it may be empty: shifts no line) */
  tp_intvec lines;     /* shift only these lines */
  const int64_t *from; /* shift from this line on; NULL: not given */
  tp_str rest;         /* set-rest */
  int64_t after;       /* insert after this line; 0: at the start */
  tp_str text;         /* insert, without a terminator */
};

/* EditWarning is a warning about one edit. */
struct tp_edit_warning {
  tp_edit_warning_code code;
  ptrdiff_t edit;      /* index into the edits */
  const int64_t *line; /* see tp_saturating_int */
};

/* EditResult is the result of applyEdits. */
struct tp_edit_result {
  tp_str text;
  tp_intvec changed; /* final line numbers of written lines; sorted, unique */
  tp_edit_warning_vec warnings;
};

/* rawEditWarning is an edit warning with the line as the JS reference holds it: any number an
 * invalid edit carried (possibly negative or fractional behind the JSON door). */
struct tp_raw_edit_warning {
  tp_edit_warning_code code;
  ptrdiff_t edit;
  const double *line;
};

/* rawEditResult is the result of tp_apply_edits before it is typed or written as JSON. */
struct tp_raw_edit_result {
  tp_str text;
  tp_intvec changed;
  tp_raw_edit_warning_vec warnings;
};

/* result is the typed form (lines saturate to int64). */
TP_PRIVATE tp_edit_result tp_raw_edit_result_result(const tp_raw_edit_result *r);
/* saturatingInt converts a float to int64 (Go int on 64-bit), clamping to the range (NaN gives
 * 0). */
TP_PRIVATE int64_t tp_saturating_int(double f);

/* ============================================================================================ */
/* options.go: typed options, the resolved forms, and the typed and JSON resolvers (R12, R0.8)  */

/* SplitAnd is when the word "and" splits a credit list (R7.3); "" means auto. In parse options
 * any other string behaves as always; in tracklist options as auto. */
typedef tp_str tp_split_and;
#define TP_SPLIT_AND_AUTO TP_S("auto")
#define TP_SPLIT_AND_ALWAYS TP_S("always")
#define TP_SPLIT_AND_NEVER TP_S("never")

/* FeatPlacement is where format writes featured artists (R11.2); "" means source. Any other
 * string places them nowhere. */
typedef tp_str tp_feat_placement;
#define TP_FEAT_SOURCE TP_S("source")
#define TP_FEAT_ARTIST TP_S("artist")
#define TP_FEAT_TITLE TP_S("title")
#define TP_FEAT_OMIT TP_S("omit")

/* JoinerStyle (R11.5); "" and unknown mean original. */
typedef tp_str tp_joiner_style;
#define TP_JOINERS_ORIGINAL TP_S("original")
#define TP_JOINERS_CANONICAL TP_S("canonical")

/* VersionsOption (R11.3); "" means all, any other string none. */
typedef tp_str tp_versions_option;
#define TP_VERSIONS_ALL TP_S("all")
#define TP_VERSIONS_NONE TP_S("none")

/* VersionHead is an extra version head word and its version type. */
struct tp_version_head {
  tp_str key;
  tp_version_type type;
};

/* JunkPhrase is an extra junk phrase and its kind. */
struct tp_junk_phrase {
  tp_str phrase;
  tp_junk_kind kind;
};

/* Keywords is extra vocabulary (R12). The two JS maps are ordered vectors: a repeated key keeps
 * its first place and takes the last value; integer-like keys first (tp_js_entries). */
struct tp_keywords {
  tp_version_head_vec version_heads;
  tp_strvec descriptors;
  tp_strvec genres;
  tp_junk_phrase_vec junk;
  tp_strvec feat_markers;
};

/* isEmpty reports whether k adds no word, so the default tables can be shared. */
TP_PRIVATE bool tp_keywords_is_empty(const tp_keywords *k);

/* ParseOptions (R12): the zero value is what parse uses. */
struct tp_parse_options {
  tp_mode mode;            /* "" means auto */
  tp_str uploader;         /* R6.8; "" means none */
  tp_strvec known_artists; /* R7.3a */
  tp_split_and split_and;
  tp_keywords keywords;
};

/* FormatOptions (R11): the zero value is what format uses. */
struct tp_format_options {
  tp_feat_placement feat;
  const tp_str *feat_marker; /* NULL: the input's marker; pointer to "": an empty marker */
  tp_joiner_style joiners;
  tp_versions_option versions;
  bool omit_producers; /* JS producers: false */
  bool position;
  bool omit_label; /* R11.3; JS label: false */
};

/* TracklistOptions (R13.10): the zero value is what parseTracklist uses. */
struct tp_tracklist_options {
  tp_mode mode; /* "" means tracklist; auto picks a mode per line */
  tp_strvec known_artists;
  tp_split_and split_and;
  tp_keywords keywords;
  const double *audio_seconds; /* NULL, negative, non-finite: unknown; pointer to 0 is 0 */
  tp_strvec marker_words;      /* R13.3 */
};

/* The edit constructors (Go SetTime, Shift, ...). Renamed with tp_edit_ because rt.h has a
 * tp_insert macro. */
/* SetTime rewrites the time of line, keeping its written style (R13.9.2). */
TP_PRIVATE tp_edit tp_edit_set_time(int64_t line, double seconds);
/* Shift adds seconds to every timed line. */
TP_PRIVATE tp_edit tp_edit_shift(double seconds);
/* ShiftFrom adds seconds to every timed line from line from on. */
TP_PRIVATE tp_edit tp_edit_shift_from(double seconds, int64_t from);
/* ShiftLines adds seconds to the given lines only (has_lines is true even for count 0). */
TP_PRIVATE tp_edit tp_edit_shift_lines(double seconds, const int64_t *lines, size_t count);
/* SortByTime sorts the timed lines stably, each with the untimed lines after it (R13.9.4). */
TP_PRIVATE tp_edit tp_edit_sort_by_time(void);
/* SetRest replaces everything after the time and gap of line. */
TP_PRIVATE tp_edit tp_edit_set_rest(int64_t line, tp_str rest);
/* Insert inserts text as a new line after line after (0: at the start). */
TP_PRIVATE tp_edit tp_edit_insert(int64_t after, tp_str text);
/* Delete removes line. */
TP_PRIVATE tp_edit tp_edit_delete(int64_t line);

/* requestKind is how options.mode reached the pipeline. */
typedef enum tp_request_kind {
  TP_REQUEST_AUTO, /* missing, null or "auto" */
  TP_REQUEST_MODE, /* one of the four mode names */
  TP_REQUEST_OTHER /* anything else: JS skips the R2.1 youtube detection, still detects
                      filenames */
} tp_request_kind;

/* requestedMode is options.mode as the pipeline sees it; mode is set for TP_REQUEST_MODE only. */
struct tp_requested_mode {
  tp_request_kind kind;
  tp_mode mode;
};

/* splitAndMode is options.splitAnd as the pipeline sees it. */
typedef enum tp_split_and_mode { TP_SPLIT_AUTO, TP_SPLIT_ALWAYS, TP_SPLIT_NEVER } tp_split_and_mode;

/* kv is one key/value pair of an options object, in JS Object.entries order. */
struct tp_kv {
  tp_str key;
  tp_str value;
};

/* keywordLists is options.keywords with the maps in JS Object.entries order. Types and kinds
 * stay strings: JS passes any string through to the output. */
struct tp_keyword_lists {
  tp_kv_vec version_heads;
  tp_strvec descriptors;
  tp_strvec genres;
  tp_kv_vec junk;
  tp_strvec feat_markers;
};

/* resolvedOptions are parse options after the R0.8 fallbacks. The zero value is the default. */
struct tp_resolved_options {
  tp_requested_mode mode;
  tp_str uploader; /* R6.8; "" means none */
  tp_strvec known_artists;
  tp_split_and_mode split_and;
  const tp_keyword_lists *keywords; /* NULL for the default tables */
};

/* featMode is options.feat as R11 reads it. Prefixed TP_FEAT_MODE_ (deviation): TP_FEAT_SOURCE
 * is already the FeatPlacement string. */
typedef enum tp_feat_mode {
  TP_FEAT_MODE_SOURCE,
  TP_FEAT_MODE_ARTIST,
  TP_FEAT_MODE_TITLE,
  TP_FEAT_MODE_OMIT /* also any unknown value: featured artists placed nowhere */
} tp_feat_mode;

/* resolvedFormat are format options after the fallbacks. */
struct tp_resolved_format {
  tp_feat_mode feat;
  const tp_str *feat_marker; /* NULL: keep the input's marker */
  bool canonical;
  bool versions;
  bool producers;
  bool position;
  bool label;
};

/* resolvedTracklist are tracklist options after the R13.10 fallbacks. */
struct tp_resolved_tracklist {
  tp_resolved_options parse;   /* the per-line parse */
  const double *audio_seconds; /* NULL when unknown */
  tp_strvec marker_words;
};

/* modeByName is a mode for one of the four names of a parsed mode. */
TP_PRIVATE bool tp_mode_by_name(tp_str name, tp_mode *out);
/* requestFor is the R2 request for a mode name given as a string. */
TP_PRIVATE tp_requested_mode tp_request_for(tp_str name);
/* parseSplitAnd: anything but auto and never (or missing) behaves as always in JS. */
TP_PRIVATE tp_split_and_mode tp_parse_split_and(tp_str name);
/* tracklistSplitAnd: only always and never count. */
TP_PRIVATE tp_split_and_mode tp_tracklist_split_and(tp_str name);
/* tracklistRequest: a mode name or auto; anything else means tracklist. */
TP_PRIVATE tp_requested_mode tp_tracklist_request(tp_str name);
/* arrayIndex reports whether key is a canonical array index ("0", "17"; not "01"). */
TP_PRIVATE bool tp_array_index(tp_str key, uint64_t *out);
/* jsEntries is the Object.entries order of an object built from pairs: a repeated key keeps its
 * first place and takes the last value; integer-like keys first ascending, then insertion. */
TP_PRIVATE tp_kv_vec tp_js_entries(tp_kv_vec pairs);
/* sanitizeAll is tp_sanitize over a list (a fresh vector, never the caller's). */
TP_PRIVATE tp_strvec tp_sanitize_all(tp_strvec list);
/* resolveKeywords is the typed keywords; NULL when they add nothing. */
TP_PRIVATE const tp_keyword_lists *tp_resolve_keywords(const tp_keywords *k);
/* resolveParse is the typed door to resolvedOptions. o NULL is the zero value. */
TP_PRIVATE tp_resolved_options tp_resolve_parse(const tp_parse_options *o);
/* resolveFormat is the typed door to resolvedFormat. o NULL is the zero value. */
TP_PRIVATE tp_resolved_format tp_resolve_format(const tp_format_options *o);
/* audio is the R13.10 audio length: NULL unless given, finite and not negative. */
TP_PRIVATE const double *tp_audio(const double *seconds);
/* resolveTracklist is the typed door to resolvedTracklist. o NULL is the zero value. */
TP_PRIVATE tp_resolved_tracklist tp_resolve_tracklist(const tp_tracklist_options *o);
/* stringList is an option list as JS reads it at run time (R0.8): only an array's strings. */
TP_PRIVATE tp_strvec tp_string_list(tp_json v, bool ok);
/* stringEntries is an option map as JS reads it: a plain object's string values, in
 * Object.entries order. */
TP_PRIVATE tp_kv_vec tp_string_entries(tp_json v, bool ok);
/* keywordsJSON is options.keywords from loose JSON (a plain object only). */
TP_PRIVATE const tp_keyword_lists *tp_keywords_json(tp_json v);
/* resolveParseJSON is the JSON door to resolvedOptions, with the JS fallbacks. */
TP_PRIVATE tp_resolved_options tp_resolve_parse_json(tp_json o);
/* truthy is JS truthiness of a JSON value. */
TP_PRIVATE bool tp_truthy(tp_json v);
/* jsString is JS String(value), what a template literal does with a non-string featMarker. */
TP_PRIVATE tp_str tp_js_string(tp_json v);
/* resolveFormatJSON is the JSON door to resolvedFormat (`??` for missing and null). Its Go
 * closure get becomes a static helper. */
TP_PRIVATE tp_resolved_format tp_resolve_format_json(tp_json o);
/* jsNumber is a JS number view of a JSON value: true only for a number (a bool is not one). The
 * value may be NaN or infinite. */
TP_PRIVATE bool tp_js_number(tp_json v, bool present, double *out);
/* resolveTracklistJSON is the JSON door to resolvedTracklist; uploader is not a tracklist
 * option. */
TP_PRIVATE tp_resolved_tracklist tp_resolve_tracklist_json(tp_json o);

/* ============================================================================================ */
/* context.go: the engine (built once per Parser) and the per-call ctx                          */

/* engine is resolved options with the tables and knownArtists keys built once. Read-only after
 * tp_engine_new, so safe for concurrent use; every call makes its own ctx. */
struct tp_engine {
  tp_resolved_options opts;
  const tp_tables *t;
  tp_word_set known_keys;
  ptrdiff_t known_max_len; /* longest knownArtists entry, in UTF-16 units */
};

/* ctx is the state of one parse. */
struct tp_ctx {
  const tp_tables *t;
  const tp_word_set *known_keys; /* the engine's */
  ptrdiff_t known_max_len;       /* UTF-16 units; bounds the R7.3a search */
  tp_split_and_mode split_and;
  tp_strvec warnings; /* tp_warning values, each once */
  ptrdiff_t list_seq; /* R7.6: the next credit list id (ids start at 0) */
};

/* newEngine builds the tables and known keys for o, in the current arena. */
TP_PRIVATE tp_engine *tp_engine_new(tp_resolved_options o);
/* sharedEngine is the engine behind parse and parseArtists without options (tp_once_get; built
 * into the current arena when the once-build ran out of memory). */
TP_PRIVATE const tp_engine *tp_shared_engine(void);
/* newCtx is a fresh per-call context. */
TP_PRIVATE tp_ctx *tp_engine_new_ctx(const tp_engine *e);
/* knownKeysOf is the dedup keys of known and the longest entry in UTF-16 units (*max_len). */
TP_PRIVATE tp_word_set tp_known_keys_of(tp_strvec known, ptrdiff_t *max_len);
/* warn adds w once. */
TP_PRIVATE void tp_ctx_warn(tp_ctx *c, tp_warning w);
/* matchKnownAt is R7.3a: the length (code points) of the longest knownArtists entry matching
 * text at start (dedup-key comparison), followed by the end, whitespace, "," or ";" (or extra,
 * unless TP_NORUNE). 0 when none. Window: 2 * longest + 8 UTF-16 units, as in JS. */
TP_PRIVATE ptrdiff_t tp_match_known_at(tp_runes text, ptrdiff_t start, tp_ctx *c, tp_rune extra);
/* isKnownArtist reports whether text is a knownArtists entry (dedup-key comparison). */
TP_PRIVATE bool tp_is_known_artist(tp_str text, tp_ctx *c);

/* ============================================================================================ */
/* tables.go: the vocabulary tables (R12)                                                       */

/* spacedJoiner is one spaced joiner of joiners.json (R7.3). */
struct tp_spaced_joiner {
  tp_str raw;
  tp_str canonical;
  bool is_and;
  bool case_sensitive;
};

/* runInfo is the category of a word in the R5.7.2 modifier run. NULL / false is not set (JS
 * undefined). Phrase-table values of tables.run_words are const tp_run_info *. */
struct tp_run_info {
  const tp_str *type_capable;
  const tp_str *descriptor;
  bool genre;
  const tp_str *head;
};

/* tables are the vocabulary the pipeline reads. Phrase-table values: NULL for the set-like
 * ones (Go struct{}), const tp_str * for the string ones, const tp_run_info * for run_words. */
struct tp_tables {
  const tp_phrase_table *feat_markers;
  const tp_phrase_table *bracket_only_feat;
  const tp_phrase_table *producer_markers;
  const tp_phrase_table *title_feat_markers;     /* R8.3, plus keywords.featMarkers */
  const tp_phrase_table *title_producer_markers; /* R8.3 */
  const tp_phrase_table *junk_or_genre;          /* string: R5.1 tokenizer vocabulary */
  const tp_phrase_table *junk_phrases;           /* string: R8.4 */
  tp_word_set junk_connectors;
  tp_word_set label_suffixes;
  const tp_phrase_table *heads; /* string */
  tp_word_set generic_head_types;
  const tp_phrase_table *run_words;    /* run_info: R5.7.2 */
  const tp_phrase_table *prefix_forms; /* string */
  tp_spaced_joiner_vec spaced_joiners;
  /* Go map tightJoiners (raw -> canonical) as pairs; look up with tp_tables_tight_joiner. */
  tp_kv_vec tight_joiners;
  tp_str feat_canonical;
  tp_word_set no_split_before;
  tp_word_set stopwords;
  tp_word_set unknown_case_sensitive;
  tp_word_set unknown_case_insensitive;
  tp_word_set extensions;
  tp_strvec platform_suffixes; /* by UTF-16 length, longest first (stable) */
};

/* C addition: the Go map lookup t.tightJoiners[raw]. A later pair with the same raw wins, as a
 * Go map assignment does. */
TP_PRIVATE bool tp_tables_tight_joiner(const tp_tables *t, tp_str raw, tp_str *canonical);
/* C addition: a read-only tp_strvec view of a generated data array (cap == n, so appending to it
 * copies, as a Go append to a full slice does). */
TP_PRIVATE tp_strvec tp_data_strs(const tp_str *p, size_t n);
/* lowerKeys is JS `asciiLower(s).trim()` of each entry, empty ones dropped. */
TP_PRIVATE tp_strvec tp_lower_keys(tp_strvec list);
/* dataEntries are the pairs of a data object in JS Object.entries order. */
TP_PRIVATE tp_kv_vec tp_data_entries(const tp_data_kv *list, size_t n);
/* markerTable is a set-like phrase table of base plus extra. */
TP_PRIVATE const tp_phrase_table *tp_marker_table(tp_strvec base, tp_strvec extra);
/* kindTable is a phrase table of string values over several entry lists (Go variadic). */
TP_PRIVATE const tp_phrase_table *tp_kind_table(const tp_kv_vec *entries, size_t count);
/* buildTables compiles the tables; kw NULL means no extra words. The Go closure note becomes a
 * static helper over a context struct (run keys, index map, infos). */
TP_PRIVATE tp_tables *tp_build_tables(const tp_keyword_lists *kw);
/* defaultTables are the tables without keywords, built once (tp_once_get, with the current-arena
 * fallback). */
TP_PRIVATE const tp_tables *tp_default_tables(void);
/* tablesFor is the default tables when kw is NULL, else fresh tables with the extra words. */
TP_PRIVATE const tp_tables *tp_tables_for(const tp_keyword_lists *kw);
/* isUnknownToken is R7.5 / R8.6: an unknown placeholder name (ID, ???, Untitled...). */
TP_PRIVATE bool tp_is_unknown_token(tp_str text, const tp_tables *t);

/* ============================================================================================ */
/* normalize.go: R1                                                                             */

/* isRemoved is R1.2: the invisible characters removed outright (ZWNJ, ZWJ kept; U+E000 is the
 * R3.4 placeholder, so it can never occur in input). */
TP_PRIVATE bool tp_is_removed(tp_rune r);
/* mapRune is R1.3 brackets, R1.4 quotes and R1.5 dashes to ASCII. false for a removed
 * character. */
TP_PRIVATE bool tp_map_rune(tp_rune r, tp_rune *out);
/* collapseSpacedDashRuns is the second half of R1.5: a run of two or more "-" with whitespace on
 * both sides becomes one "-". */
TP_PRIVATE tp_runes tp_collapse_spaced_dash_runs(tp_runes t);
/* normalize is R1 on a track string. It is idempotent. */
TP_PRIVATE tp_str tp_normalize(tp_str input);

/* ============================================================================================ */
/* mode.go: R2                                                                                  */

/* positionedJunk is a junk fragment with its absolute offset (R9 orders junk by it). */
struct tp_positioned_junk {
  tp_str raw;
  tp_junk_kind kind; /* keywords.junk values pass through as given */
  ptrdiff_t pos;
};

/* piece is a contiguous piece of the normalized string and its absolute offset. */
struct tp_piece {
  tp_runes text;
  ptrdiff_t base;
};

/* preludeResult is the outcome of R2. */
struct tp_prelude_result {
  tp_mode mode;
  tp_piece working;           /* R2.4 step 2 or 4, or the artist piece of "artist | title" */
  const tp_piece *pipe_title; /* R2.4 step 3: the title piece; NULL otherwise */
  tp_positioned_junk_vec junk;
};

/* asciiLowerRunes lowercases A-Z only. */
TP_PRIVATE tp_runes tp_ascii_lower_runes(tp_runes t);
/* hasRuneSuffix reports whether t ends with suffix. */
TP_PRIVATE bool tp_has_rune_suffix(tp_runes t, tp_runes suffix);
/* runeIndex is the first index of r in t, or -1. */
TP_PRIVATE ptrdiff_t tp_rune_index(tp_runes t, tp_rune r);
/* lastRuneIndex is the last index of r in t, or -1. */
TP_PRIVATE ptrdiff_t tp_last_rune_index(tp_runes t, tp_rune r);
/* stripPlatformSuffixes is R2.2: strip trailing platform suffixes (longest first, repeatedly),
 * appending each to junk. */
TP_PRIVATE tp_runes tp_strip_platform_suffixes(tp_runes s, tp_ctx *c, tp_positioned_junk_vec *junk);
/* hasExtension reports whether s ends in a known file extension (R2.3). */
TP_PRIVATE bool tp_has_extension(tp_runes s, tp_ctx *c);
/* splitPipes splits a skeleton at top-level " | " and " || " into trimmed segments (R2.4). */
TP_PRIVATE tp_segment_vec tp_split_pipes(tp_skel sk);
/* trailingJunkPhrase is the R8.4 test: text ends with an unbracketed junk phrase (genres
 * excluded): the code-point index where it starts (*start) and its kind (*kind). */
TP_PRIVATE bool tp_trailing_junk_phrase(tp_runes text, tp_ctx *c, ptrdiff_t *start, tp_str *kind);
/* junkTextKind is the R5.1 junk kind of a whole text, if it is junk. */
TP_PRIVATE bool tp_junk_text_kind(tp_str text, tp_ctx *c, tp_str *kind);
/* looksLikeYoutube is the R2.1 youtube signals besides platform suffixes. */
TP_PRIVATE bool tp_looks_like_youtube(tp_skel sk, tp_ctx *c);
/* prelude is R2 on the normalized string. */
TP_PRIVATE tp_prelude_result tp_prelude(tp_str normalized, tp_requested_mode requested, tp_ctx *c);
/* pieceOf is the rendered text of a segment with its absolute offset. */
TP_PRIVATE tp_piece tp_piece_of(tp_segment seg);
/* splitYoutubePipes is R2.4: the working piece, and the title piece (*pipe_title, NULL when
 * none; always written). The Go closure pushOther becomes a static helper. */
TP_PRIVATE tp_piece tp_split_youtube_pipes(tp_skel sk, tp_ctx *c, tp_positioned_junk_vec *junk,
                                           const tp_piece **pipe_title);

/* ============================================================================================ */
/* prefix.go: R4                                                                                */

/* prefixResult is the input after R4. */
struct tp_prefix_result {
  tp_runes rest;
  ptrdiff_t base; /* absolute offset of rest */
  const tp_timestamp *timestamp;
  const tp_position *position;
};

/* timeMatch is a time at the start of a prefix. */
struct tp_time_match {
  tp_str raw;
  ptrdiff_t seconds;
  ptrdiff_t end;
};

/* matchTime is H:MM:SS, HH:MM:SS, M:SS or MM:SS starting at from (R4.1). */
TP_PRIVATE bool tp_match_time(tp_runes s, ptrdiff_t from, tp_time_match *out);
/* skipSpaceAndDash skips, after a prefix, one space and then an optional "- " (spaced dash). */
TP_PRIVATE ptrdiff_t tp_skip_space_and_dash(tp_runes s, ptrdiff_t from);
/* matchTimestamp is R4.1: the timestamp (*ts) and the index where the rest starts (*rest). */
TP_PRIVATE bool tp_match_timestamp(tp_runes s, tp_timestamp *ts, ptrdiff_t *rest);
/* containsSpacedDash reports whether the skeleton of s has a spaced dash. */
TP_PRIVATE bool tp_contains_spaced_dash(tp_runes s);
/* matchPosition is R4.2: the end index of the position prefix. Forms (a), (b), (c), then (d)
 * and (e). */
TP_PRIVATE bool tp_match_position(tp_runes s, tp_mode mode, ptrdiff_t *end);
/* stripPrefixes is R4 on text, whose first code point sits at absolute offset base. */
TP_PRIVATE tp_prefix_result tp_strip_prefixes(tp_runes text, ptrdiff_t base, tp_mode mode,
                                              tp_ctx *c);

/* ============================================================================================ */
/* scanner.go: R3                                                                               */

/* ph is the R3.4 placeholder code point. R1.2 removes it from input. */
#define TP_PH ((tp_rune)0xE000)
/* bullet is the R8.7 bullet, U+2022. */
#define TP_BULLET ((tp_rune)0x2022)
/* TP_BULLET in UTF-8, for format (R11.3). */
#define TP_BULLET_UTF8 "\xe2\x80\xa2"

/* group is one top-level bracket group (R3.1). */
struct tp_group {
  tp_delimiter open;
  tp_str inner;  /* R3.5: trimmed and whitespace-collapsed */
  tp_str raw;    /* exactly as written, brackets included */
  ptrdiff_t pos; /* absolute offset of the opener */
};

/* groupMap holds the groups of one scan by the offset of their opener (Go map[int]*group). Only
 * looked up, never iterated; shared read-only by every slice of the skeleton. tp_scan adds the
 * groups in strictly ascending pos, so the C form is a sorted vector with binary search. */
struct tp_group_map {
  tp_group_ptr_vec groups; /* ascending pos */
};
/* C addition: add g; g->pos must be greater than every pos already in m. */
TP_PRIVATE void tp_group_map_put(tp_group_map *m, const tp_group *g);
/* C addition: the group whose opener is at pos, or NULL (Go groups[pos]). */
TP_PRIVATE const tp_group *tp_group_map_get(const tp_group_map *m, ptrdiff_t pos);

/* skel is a skeleton: text with each top-level group replaced by TP_PH, and the absolute offset
 * of every code point (pos.n == text.n). */
struct tp_skel {
  tp_runes text;
  tp_idxvec pos;
  const tp_group_map *groups;
};

/* C form of the Go closure offsetAt in scan: the absolute offset of every code point plus one
 * entry for the end; out of range gives end. */
struct tp_offsets {
  tp_idxvec at;
  ptrdiff_t end;
};
TP_PRIVATE ptrdiff_t tp_offset_at(const tp_offsets *o, ptrdiff_t i);

/* closerOf is the closing bracket for an opener, or TP_NORUNE. */
TP_PRIVATE tp_rune tp_closer_of(tp_rune r);
/* delimiterOf is the version delimiter of an opener. */
TP_PRIVATE tp_delimiter tp_delimiter_of(tp_rune r);
/* scan is R3.1 to R3.5 on s, whose first code point sits at absolute offset base. *unbalanced
 * is R3.3 (always written). */
TP_PRIVATE tp_skel tp_scan(tp_runes s, ptrdiff_t base, bool *unbalanced);
/* addGroup records the group s[start:end] (closed: end includes the closer). */
TP_PRIVATE void tp_add_group(tp_runes s, ptrdiff_t start, ptrdiff_t end, bool closed,
                             const tp_offsets *offsets, tp_group_map *groups);
/* clamped is t[a:b] with both ends clamped (JS slice with non-negative bounds). Go generic:
 * one function per element type used. No copy. */
TP_PRIVATE tp_runes tp_clamped_runes(tp_runes t, ptrdiff_t a, ptrdiff_t b);
TP_PRIVATE tp_idxvec tp_clamped_ints(tp_idxvec t, ptrdiff_t a, ptrdiff_t b);
/* len is the number of code points. */
TP_PRIVATE ptrdiff_t tp_skel_len(const tp_skel *s);
/* isEmpty reports whether the skeleton has no code points. */
TP_PRIVATE bool tp_skel_is_empty(const tp_skel *s);
/* sliceSkel is s[start:end], clamped. Text and offsets copied, groups shared. */
TP_PRIVATE tp_skel tp_slice_skel(const tp_skel *s, ptrdiff_t start, ptrdiff_t end);
/* sliceFrom is s[start:]. */
TP_PRIVATE tp_skel tp_slice_from(const tp_skel *s, ptrdiff_t start);
/* trimSkel removes R0.2 whitespace at both ends. */
TP_PRIVATE tp_skel tp_trim_skel(const tp_skel *s);
/* concatSkel is a followed by b (the groups of a). */
TP_PRIVATE tp_skel tp_concat_skel(const tp_skel *a, const tp_skel *b);
/* groupAt is the group behind the placeholder at index, or NULL. */
TP_PRIVATE const tp_group *tp_group_at(const tp_skel *s, ptrdiff_t index);

/* indexedGroup is a group with the index of its placeholder. */
struct tp_indexed_group {
  ptrdiff_t index;
  const tp_group *group;
};

/* groupsOf is the groups of the skeleton, left to right. */
TP_PRIVATE tp_indexed_group_vec tp_groups_of(const tp_skel *s);
/* hasGroup reports whether the skeleton holds a placeholder. */
TP_PRIVATE bool tp_has_group(const tp_skel *s);
/* renderSkel substitutes the groups back (R3.4 reassembly). Whitespace is not touched. */
TP_PRIVATE tp_str tp_render_skel(const tp_skel *s);
/* startPos is the absolute offset of the first code point, or fallback when empty. */
TP_PRIVATE ptrdiff_t tp_start_pos(const tp_skel *s, ptrdiff_t fallback);
/* removeGroups removes the placeholders of the groups at the given offsets. Go takes a
 * map[int]struct{}; C takes the offsets sorted ascending (tp_extract_groups and
 * tp_parse_artist_side produce them in groupsOf order) and binary-searches them. */
TP_PRIVATE tp_skel tp_remove_groups(const tp_skel *s, tp_idxvec remove);
/* C addition: whether the ascending vector v holds x (binary search). */
TP_PRIVATE bool tp_sorted_has(tp_idxvec v, ptrdiff_t x);
/* isSpacedDashAt is R6.1: "-" with a space immediately on both sides. */
TP_PRIVATE bool tp_is_spaced_dash_at(tp_runes t, ptrdiff_t i);
/* hasSpacedDash reports whether t has a spaced dash. */
TP_PRIVATE bool tp_has_spaced_dash(tp_runes t);
/* isSpacedBulletAt is R8.7: U+2022 with a space immediately on both sides (the ends of the
 * string do not count). */
TP_PRIVATE bool tp_is_spaced_bullet_at(tp_runes t, ptrdiff_t i);

/* segment is a trimmed piece of a skeleton and its range inside the parent. */
struct tp_segment {
  tp_skel skel;
  ptrdiff_t start;
  ptrdiff_t end;
};

/* trimmedSegment is s[start:end] trimmed of R0.2 whitespace. */
TP_PRIVATE tp_segment tp_trimmed_segment(const tp_skel *s, ptrdiff_t start, ptrdiff_t end);
/* splitAtSpacedDashes is R6.1 / R8.2: split at every spaced dash into trimmed segments. With
 * allow_leading, a string starting with "- " yields an empty first segment (R6.3). */
TP_PRIVATE tp_segment_vec tp_split_at_spaced_dashes(const tp_skel *s, bool allow_leading);

/* ============================================================================================ */
/* credits.go: R7.3 to R7.6 credit lists (before classify.go here: its types are needed there)  */

/* credit is an artist credit plus its absolute offset (UTF-16 units), for ordering (R9.1). */
struct tp_credit {
  tp_str name;
  tp_role role;
  const tp_str *joiner;
  tp_source source;
  ptrdiff_t pos;
  ptrdiff_t list; /* the credit list id (R7.6 joiner inheritance in R9.2), from ctx.list_seq */
};

/* positionedYear is a year with its absolute offset (R9.6: the first in text order). */
struct tp_positioned_year {
  ptrdiff_t year;
  ptrdiff_t pos;
};

/* listOptions are how one credit list is split. */
struct tp_list_options {
  tp_role role;
  tp_source source;
  const tp_str *lead;     /* joiner of the first name: NULL, or the marker as written */
  bool split_with;        /* R7.3: "with" splits only in the main artist-side list */
  bool feat_markers_join; /* R7.2: later feat markers are joiners in a featured list */
};

/* listResult is a split credit list. */
struct tp_list_result {
  tp_credit_vec credits;
  bool unknown;                 /* R7.5: an unknown token was dropped */
  tp_positioned_year_vec years; /* R7.5: dropped year-only names */
};

/* joinerHit is a joiner found in a list: its code-point span and its text as written. */
struct tp_joiner_hit {
  ptrdiff_t start;
  ptrdiff_t end;
  tp_str raw;
};

/* rawName is a name before the R7.4 / R7.5 drops. */
struct tp_raw_name {
  tp_str name;
  const tp_str *joiner;
  ptrdiff_t pos;
};

/* (Go var andLike = {"&", "and", "+"} is a static array in credits.c.) */

/* wordAt is the end of the word that starts at start. */
TP_PRIVATE ptrdiff_t tp_word_at(tp_runes text, ptrdiff_t start);
/* nextWord is the word after a joiner; a tight joiner (, ;) ends it. */
TP_PRIVATE tp_str tp_next_word(tp_runes text, ptrdiff_t from);
/* findJoiner is R7.3b: the leftmost joiner at or after from, honouring the guards. */
TP_PRIVATE bool tp_find_joiner(tp_runes text, ptrdiff_t from, bool split_at_comma,
                               const tp_list_options *opts, tp_ctx *c, tp_joiner_hit *out);
TP_PRIVATE bool tp_is_spaced_joiner(tp_str word, tp_runes text, ptrdiff_t end, bool split_at_comma,
                                    const tp_list_options *opts, tp_ctx *c);
/* cleanName is R7.4: clean one raw name. */
TP_PRIVATE tp_str tp_clean_name(tp_str raw);
/* unwrapList is R7.4: strip one pair of wrapping quotes from the whole list. */
TP_PRIVATE tp_skel tp_unwrap_list(const tp_skel *list);
/* splitCredits is R7.3: split input into credits. */
TP_PRIVATE tp_list_result tp_split_credits(const tp_skel *input, const tp_list_options *opts,
                                           tp_ctx *c);
/* finishNames is the R7.4 empty drops and the R7.5 year/unknown drops. R7.6: the first survivor
 * takes the lead joiner. Takes a new list id from c->list_seq. */
TP_PRIVATE tp_list_result tp_finish_names(tp_raw_name_vec raw, const tp_list_options *opts,
                                          tp_ctx *c);

/* ============================================================================================ */
/* classify.go: R5                                                                              */

/* versionDraft is a version as R5 found it, before R9 orders and assembles it. */
struct tp_version_draft {
  tp_version_type vtype; /* keywords.versionHeads values pass through as given */
  tp_str raw;
  tp_credit_vec artists;
  tp_strvec modifiers;
  const tp_str *descriptor;
  const ptrdiff_t *year;
  bool unknown_artist; /* R7.5: the credit was an unknown token */
  tp_delimiter delimiter;
  ptrdiff_t pos;
  bool ambiguous_mix; /* R5.7.2 ambiguousMixCredit; emitted only when the version is used */
  bool prefix_form;   /* R5.7.3 recognized it (R6.2 condition (ii) does not apply) */
};

/* flag is an R5.4 flag. */
typedef enum tp_flag { TP_FLAG_EXPLICIT, TP_FLAG_CLEAN } tp_flag;

/* classKind is the kind of a classification. */
typedef enum tp_class_kind {
  TP_CLASS_UNKNOWN,
  TP_CLASS_JUNK,
  TP_CLASS_FEAT,
  TP_CLASS_PRODUCER,
  TP_CLASS_FLAG,
  TP_CLASS_YEAR,
  TP_CLASS_VERSIONS
} tp_class_kind;

/* classification is the result of R5. Only the fields of its kind are set. */
struct tp_classification {
  tp_class_kind kind;
  tp_junk_kind junk;             /* TP_CLASS_JUNK */
  tp_list_result list;           /* TP_CLASS_FEAT, TP_CLASS_PRODUCER */
  tp_flag flag;                  /* TP_CLASS_FLAG */
  ptrdiff_t year;                /* TP_CLASS_YEAR */
  tp_version_draft_vec versions; /* TP_CLASS_VERSIONS */
};

/* site is where the classified text sits: credits' source, versions' delimiter, absolute offset
 * (UTF-16 units). */
struct tp_site {
  tp_source source;
  tp_delimiter delimiter;
  ptrdiff_t pos;
};

/* runItem is one entry of the R5.7.2 modifier run: a run word (info) or a year. */
struct tp_run_item {
  ptrdiff_t start;
  ptrdiff_t end;
  tp_run_info info;
  const ptrdiff_t *year;
};

/* (Go vars explicitFlags and cleanFlags are static arrays in classify.c.) */

/* classify is R5 on inner (already trimmed and whitespace-collapsed). */
TP_PRIVATE tp_classification tp_classify(tp_str inner, tp_site s, tp_ctx *c);
/* junkKindOf is R5.1: the junk kind if the whole word list is junk vocabulary. */
TP_PRIVATE bool tp_junk_kind_of(tp_strvec words, tp_ctx *c, tp_str *kind);
/* phraseKindOf is the R5.1 tokenization over phrases with connectors and year words, at least
 * one phrase: the kind of the first phrase. The kind can be empty (through keywords.junk);
 * callers apply JS truthiness. R8.7 uses it without genres. */
TP_PRIVATE bool tp_phrase_kind_of(tp_strvec words, const tp_phrase_table *phrases, tp_ctx *c,
                                  tp_str *kind);
TP_PRIVATE bool tp_feat_or_producer(tp_str inner, tp_site s, tp_ctx *c, tp_classification *out);
TP_PRIVATE bool tp_credits_after(tp_runes inner, tp_word_span_vec spans, ptrdiff_t marker_words,
                                 tp_role role, tp_site s, tp_ctx *c, tp_list_result *out);
/* listFromText splits a plain string (nested brackets protected by a fresh scan) as a credit
 * list. */
TP_PRIVATE tp_list_result tp_list_from_text(tp_runes text, ptrdiff_t base, tp_ctx *c,
                                            const tp_list_options *opts);
TP_PRIVATE bool tp_flag_of(tp_strvec words, tp_flag *out);
/* splitMultiParts splits at top-level " / " or "; " (outside nested brackets). */
TP_PRIVATE tp_strvec tp_split_multi_parts(tp_runes inner);
TP_PRIVATE bool tp_multi_version(tp_str inner, tp_site s, tp_ctx *c, tp_version_draft_vec *out);
TP_PRIVATE bool tp_single_version(tp_str raw, tp_site s, tp_ctx *c, tp_version_draft_vec *out);
TP_PRIVATE tp_version_draft tp_base_version(tp_str raw, tp_str vtype, tp_site s);
TP_PRIVATE tp_list_options tp_remixer_opts(void);
/* dedupRemixers is R9.2 for remixers: deduped within their own version only. */
TP_PRIVATE tp_credit_vec tp_dedup_remixers(tp_credit_vec credits);
TP_PRIVATE void tp_set_credits(tp_version_draft *v, tp_runes text, tp_site s, tp_ctx *c);
/* headBy is R5.7.1: Remix by Skrillex. */
TP_PRIVATE bool tp_head_by(tp_str raw, tp_runes text, tp_word_span_vec spans, tp_strvec words,
                           tp_site s, tp_ctx *c, tp_version_draft *out);
/* collectRun walks left from end collecting the modifier run (R5.7.2). */
TP_PRIVATE tp_run_item_vec tp_collect_run(tp_strvec words, ptrdiff_t end, tp_ctx *c);
/* truthyName is a JS truthiness test on an optional type name: NULL and "" do not count. */
TP_PRIVATE bool tp_truthy_name(const tp_str *s, tp_str *out);
/* headScan is R5.7.2: Extended VIP Mix, Skrillex Remix, Taylor's Version. */
TP_PRIVATE bool tp_head_scan(tp_str raw, tp_strvec words, tp_site s, tp_ctx *c,
                             tp_version_draft *out);
/* containsString reports whether list holds s. */
TP_PRIVATE bool tp_contains_string(tp_strvec list, tp_str s);
/* nearestNonGenericHead is R5.7.2 type rule 2: index of the non-generic head in the run nearest
 * the (generic) head, or -1. */
TP_PRIVATE ptrdiff_t tp_nearest_non_generic_head(tp_run_item_vec run, tp_ctx *c);
/* isAllJunkPhrases is R5.7.2: the credit span is made entirely of junk phrases. */
TP_PRIVATE bool tp_is_all_junk_phrases(tp_strvec words, tp_ctx *c);
/* prefixForm is R5.7.3: Live at Wembley 1986, Remastered 2015, Sped Up. */
TP_PRIVATE bool tp_prefix_form(tp_str raw, tp_strvec words, tp_site s, tp_ctx *c,
                               tp_version_draft *out);
/* tailWords is words[from:], or empty when from is past the end. */
TP_PRIVATE tp_strvec tp_tail_words(tp_strvec words, ptrdiff_t from);

/* ============================================================================================ */
/* separator.go: R6                                                                             */

/* peeled is a dash suffix taken off by R6.2 or R8.2; cls is junk, flag, year or versions. */
struct tp_peeled {
  tp_classification cls;
  tp_str raw;
  ptrdiff_t pos;
};

/* separatorResult is the outcome of R6. */
struct tp_separator_result {
  const tp_skel *artist;
  tp_skel title;
  bool split; /* a separator split the string (R6.3 to R6.7): R8.2 peels without conditions */
  tp_peeled_vec peeled;
};

/* separatorInput is what R6 reads. */
struct tp_separator_input {
  tp_skel skel;
  tp_mode mode;
  const tp_skel *uploader; /* R6.8 fallback artist, or NULL */
  bool list_context;       /* R13.4.2 (tracklist module only): enables the R6.10 tight dash */
  bool bullet_head;        /* R8.7: skel is the head before a bullet suffix; its first spaced dash
                              always separates, as in R6.10 (no R6.2 peeling), in every mode */
};

/* splitParts is an artist side (NULL when empty) and a title side. */
struct tp_split_parts {
  const tp_skel *artist;
  tp_skel title;
};

/* classifySegment is the R6.2 classification of a bare dash segment: placeholders make it
 * Unknown. */
TP_PRIVATE tp_classification tp_classify_segment(tp_skel seg, tp_ctx *c);
/* isPeelable reports whether R6.2 / R8.2 can peel a classification. */
TP_PRIVATE bool tp_is_peelable(tp_classification cls);
/* peelSuffixes is R6.2 / R8.2: peel classifiable suffixes off the right end. relaxed drops
 * conditions (i) to (iv). Returns the remaining segments; *peeled gets the suffixes, left to
 * right (always written). */
TP_PRIVATE tp_segment_vec tp_peel_suffixes(tp_segment_vec segments, bool relaxed, tp_ctx *c,
                                           tp_peeled_vec *peeled);
/* nonEmpty is the trimmed skeleton, or NULL when it is empty. */
TP_PRIVATE const tp_skel *tp_non_empty(tp_skel s);
/* splitAt cuts s around [cut_start, cut_end). */
TP_PRIVATE tp_split_parts tp_split_at(tp_skel s, ptrdiff_t cut_start, ptrdiff_t cut_end);
/* resultOf is a separator result for split parts. */
TP_PRIVATE tp_separator_result tp_result_of(tp_split_parts parts, tp_peeled_vec p);
/* firstDashSplit is R6.3: the first segment is the artist side, the rest (up to the last kept
 * one) the title. R6.10 and R8.7 call it on the unpeeled segments, where the first spaced dash
 * always separates (R8.2 still peels later suffixes on the title side). */
TP_PRIVATE bool tp_first_dash_split(tp_skel sk, tp_segment_vec segments, tp_split_parts *out);
/* findSeparator is R6. */
TP_PRIVATE tp_separator_result tp_find_separator(tp_separator_input in, tp_ctx *c);
/* tracklistSeparator is R6.10, tracklist mode without a spaced dash (tp_first_dash_split
 * handles one): R6.4, then the first "_-_", then (list context only) the last tight
 * letter-Uppercase dash; both of these warn unspacedDashSplit. */
TP_PRIVATE tp_separator_result tp_tracklist_separator(tp_skel rest, bool list_context, tp_ctx *c);
/* underscoreDash is the index of the first "_-_". */
TP_PRIVATE bool tp_underscore_dash(tp_runes t, ptrdiff_t *out);
/* tightUpperDash is R6.10: the last "-" with a letter (R0.6) left and Lu right. */
TP_PRIVATE bool tp_tight_upper_dash(tp_runes t, ptrdiff_t *out);
/* splitWithoutSpacedDash is R6.4 to R6.7. */
TP_PRIVATE bool tp_split_without_spaced_dash(tp_skel s, tp_mode mode, tp_ctx *c,
                                             tp_split_parts *out);
/* asymmetricDash is R6.4: the first "-" with a space on exactly one side. */
TP_PRIVATE bool tp_asymmetric_dash(tp_runes t, ptrdiff_t *out);
/* quotedTitle is R6.5: Artist "Title" (extra). */
TP_PRIVATE bool tp_quoted_title(tp_skel s, tp_split_parts *out);
/* bySplit is R6.6: index of the space before the last usable " by " (right side non-empty and
 * not a single stopword). */
TP_PRIVATE bool tp_by_split(tp_skel s, tp_ctx *c, ptrdiff_t *out);
/* unspacedDash is R6.7: the chosen unspaced dash index ("Jay-Z-Numb"). */
TP_PRIVATE bool tp_unspaced_dash(tp_skel s, tp_ctx *c, ptrdiff_t *out);
/* renderPrefix is the rendered text of s[:end]. */
TP_PRIVATE tp_str tp_render_prefix(const tp_skel *s, ptrdiff_t end);

/* ============================================================================================ */
/* artists.go: R7.1, R7.2 (the list splitting is credits.go)                                    */

/* artistSideResult is what R7 found on the artist side. */
struct tp_artist_side_result {
  tp_credit_vec credits;
  tp_positioned_junk_vec junk;
  tp_positioned_year_vec years;
  bool unknown_artist;
  bool explicit_; /* R7.1: artist-side Flag groups */
  bool clean;
};

/* featMarker is an unbracketed feat marker: its code-point span and its text as written. */
struct tp_feat_marker {
  ptrdiff_t start;
  ptrdiff_t end;
  tp_str marker;
};

/* findFeatMarker is R7.2: the leftmost unbracketed feat marker at or after from, with a space
 * (or placeholder) on its left and a space on its right. */
TP_PRIVATE bool tp_find_feat_marker(const tp_skel *s, tp_ctx *c, ptrdiff_t from,
                                    tp_feat_marker *out);
/* parseArtistSide is R7.1 to R7.5 over an artist-side skeleton. */
TP_PRIVATE tp_artist_side_result tp_parse_artist_side(const tp_skel *side, tp_ctx *c);

/* ============================================================================================ */
/* title.go: R8                                                                                 */

/* extracted is everything the title side (and R6.2 peeling) can extract. */
struct tp_extracted {
  tp_credit_vec credits;
  tp_version_draft_vec versions;
  tp_positioned_junk_vec junk;
  tp_positioned_year_vec years;
  bool explicit_;
  bool clean;
  bool unknown_artist;
};

/* setFlag records an Explicit or Clean flag. */
TP_PRIVATE void tp_extracted_set_flag(tp_extracted *out, tp_flag f);
/* addPeeled records R6.2 / R8.2 peeled dash suffixes. */
TP_PRIVATE void tp_add_peeled(tp_extracted *out, tp_peeled_vec list);

/* titleSideResult is the title and whether it is an unknown placeholder (R8.6). */
struct tp_title_side_result {
  tp_str title;
  bool unknown_title;
};

/* parseTitleSide is R8. relaxed: an artist side was split off (R8.2 peels without conditions). */
TP_PRIVATE tp_title_side_result tp_parse_title_side(tp_skel input, bool relaxed, bool youtube,
                                                    tp_extracted *out, tp_ctx *c);
/* extractGroups is R8.1: classify every group. Returns the offsets of the groups to remove,
 * ascending (Go map[int]struct{}; see tp_remove_groups). */
TP_PRIVATE tp_idxvec tp_extract_groups(tp_skel sk, tp_extracted *out, tp_ctx *c);

/* markerHit is an R8.3 marker in the title. */
struct tp_marker_hit {
  ptrdiff_t start;
  ptrdiff_t end;
  tp_str marker;
  tp_role role;
};

/* titleMarkers are the R8.3 markers, left to right, each with a space on both sides. */
TP_PRIVATE tp_marker_hit_vec tp_title_markers(tp_skel sk, tp_ctx *c);
/* extractUnbracketedCredits is R8.3: "Title feat. X" and "Title prod. by Y" become credits,
 * removed from the title. The Go closure nextGroupAt becomes a static helper. */
TP_PRIVATE tp_skel tp_extract_unbracketed_credits(tp_skel input, tp_extracted *out, tp_ctx *c);
/* stripTrailingJunk is R8.4: strip trailing unbracketed junk phrases (optionally after a lone
 * "-" or "|"); linear (word spans computed once, walked backwards). */
TP_PRIVATE tp_skel tp_strip_trailing_junk(tp_skel input, tp_extracted *out, tp_ctx *c);
/* isLoneEdge reports whether a word is a lone "-" or "|". */
TP_PRIVATE bool tp_is_lone_edge(tp_str word);
/* stripLoneEdges removes lone "-" and "|" words at both ends of a space-separated string. */
TP_PRIVATE tp_str tp_strip_lone_edges(tp_str s);
/* cleanupTitle is R8.5. */
TP_PRIVATE tp_str tp_cleanup_title(tp_str raw);

/* bulletSuffix is what R8.7 cuts off the working skeleton. */
struct tp_bullet_suffix {
  tp_skel head;                /* the working skeleton before the first cut, trimmed (R8.7.3) */
  const tp_str *label;         /* the record label, or NULL */
  tp_positioned_junk_vec junk; /* junk from the tail segments, positions as in R9.7 */
};

/* bulletSegments is R8.7.1 to R8.7.2: the head and tail segments when the rule fires. It cuts
 * at every spaced bullet after the first spaced dash D, which must have text before and after
 * it. */
TP_PRIVATE bool tp_bullet_segments(const tp_skel *sk, tp_skel_vec *out);
/* cutBulletSuffix is R8.7: cut a bullet suffix off the working skeleton (after R2 and R4,
 * before R6). False when the rule does not fire. */
TP_PRIVATE bool tp_cut_bullet_suffix(const tp_skel *sk, tp_ctx *c, tp_bullet_suffix *out);

/* ============================================================================================ */
/* assemble.go: R9                                                                              */

/* precedence is the R9.2 role order: a later duplicate with a higher one upgrades the kept
 * entry. */
TP_PRIVATE ptrdiff_t tp_precedence(tp_role role);
/* toArtist is the public form of a credit. */
TP_PRIVATE tp_artist tp_to_artist(tp_credit c);

/* dedupEntry is one credit during R9.2. */
struct tp_dedup_entry {
  tp_artist a;
  ptrdiff_t list;
  bool alive;
};

/* creditLists holds the remaining names of each credit list (a min-heap of entry indexes per
 * list, lazy deletion), so dedup stays near-linear. entries shares its storage with the caller's
 * vector (as the Go slice does): dedupArtists mutates entries and the lists see it. Go map
 * heaps[list] is a vector indexed by list id (ids are 0..ctx.list_seq-1), grown on demand. */
struct tp_credit_lists {
  tp_dedup_entry_vec entries;
  tp_idxvec_vec heaps;
};

/* newCreditLists indexes the lists of entries. */
TP_PRIVATE tp_credit_lists *tp_new_credit_lists(tp_dedup_entry_vec entries);
/* member reports whether entry x is a remaining name of list. */
TP_PRIVATE bool tp_credit_lists_member(const tp_credit_lists *l, ptrdiff_t list, ptrdiff_t x);
/* insert puts entry e on the heap of list. */
TP_PRIVATE void tp_credit_lists_insert(tp_credit_lists *l, ptrdiff_t list, ptrdiff_t e);
/* pop removes the smallest index of list's heap. */
TP_PRIVATE void tp_credit_lists_pop(tp_credit_lists *l, ptrdiff_t list);
/* first is the smallest remaining index of list after dropping stale heap entries, or -1. */
TP_PRIVATE ptrdiff_t tp_credit_lists_first(tp_credit_lists *l, ptrdiff_t list);
/* heir is the entry after e in list when e is the list's first remaining name, else -1. Only
 * when e is about to leave list: it takes e off the heap. */
TP_PRIVATE ptrdiff_t tp_credit_lists_heir(tp_credit_lists *l, ptrdiff_t list, ptrdiff_t e);
/* passJoiner is R7.6: entry e leaves its list; if it was the first remaining name, the next one
 * takes joiner. */
TP_PRIVATE void tp_credit_lists_pass_joiner(tp_credit_lists *l, ptrdiff_t e, const tp_str *joiner);
/* dedupKeyOf is the R9.2 key: producers are a separate class, deduped among themselves. */
TP_PRIVATE tp_str tp_dedup_key_of(const tp_artist *a);
/* dedupArtists is R9.2: keep the first occurrence; R7.6 joiner inheritance; role upgrades. */
TP_PRIVATE tp_artist_vec tp_dedup_artists(tp_credit_vec credits);
/* toVersion is the public form of a version draft. */
TP_PRIVATE tp_version tp_to_version(tp_version_draft v);
/* renderVersion is R9.4: a version rendered with its delimiter. */
TP_PRIVATE tp_str tp_render_version(tp_str raw, tp_delimiter delimiter);
/* fullTitleOf is R9.4: the title followed by its versions as written, empty pieces skipped. */
TP_PRIVATE tp_str tp_full_title_of(tp_str title, tp_version_vec versions);

/* ============================================================================================ */
/* parser.go: the pipeline R1 -> R2 -> R4 -> R3/R6 -> R7 -> R8 -> R9, R10, and the probe       */

/* uploaderBase is the offset used for uploader text, so it sorts before everything else. */
#define TP_UPLOADER_BASE ((ptrdiff_t)-1000000000)

/* staged is the R1, R2 and R4 stage of run, kept so the tracklist probe's work is not redone. */
struct tp_staged {
  const tp_engine *e;
  tp_ctx *c;
  tp_prelude_result pre;
  tp_track track;
  const tp_skel *working; /* after R4; NULL when empty after R1 (R9.5: track is final) */
};

/* lineProbe is facts about a line's prefix and separator search (R13.4.2, R13.5). */
struct tp_line_probe {
  bool position;    /* R4.2 took a position */
  bool spaced_dash; /* the skeleton R6 searches (after R2 and R4) has a spaced dash (R6.1) */
  bool empty;       /* empty after R1 */
  tp_staged staged;
};

/* skelOf scans a piece and trims it, warning on unbalanced brackets (R3.3). */
TP_PRIVATE tp_skel tp_skel_of(tp_piece p, tp_ctx *c);
/* uploaderSkel is R6.8: the uploader minus a trailing " - Topic" and "VEVO", or NULL. */
TP_PRIVATE const tp_skel *tp_uploader_skel(tp_str uploader);
/* emptyTrack is the R9.5 result for an input empty after R1, and the start of every result. */
TP_PRIVATE tp_track tp_empty_track(tp_str input, tp_mode mode);
/* stage runs R1, R2 and R4. */
TP_PRIVATE tp_staged tp_engine_stage(const tp_engine *e, tp_str input);
/* run is R1 to R9 on one (already sanitized) input; list_context is the R13.4.2 flag (R6.10,
 * tracklist mode only). */
TP_PRIVATE tp_track tp_engine_run(const tp_engine *e, tp_str input, bool list_context);
/* runArtists is R10 on one (already sanitized) input. */
TP_PRIVATE tp_artist_vec tp_engine_run_artists(const tp_engine *e, tp_str input);
/* probe is R1, R2 and R4 only, as run does them. tp_line_probe_finish completes the parse. */
TP_PRIVATE tp_line_probe tp_engine_probe(const tp_engine *e, tp_str input);
/* finish is the parse of the probed input (R6 to R9). Call it once per probe: the probe's ctx
 * (warnings) is consumed. */
TP_PRIVATE tp_track tp_line_probe_finish(tp_line_probe *p, bool list_context);
/* byPos sorts items by offset, stably, in place (Go generic: one per element type used). */
TP_PRIVATE tp_version_draft_vec tp_by_pos_versions(tp_version_draft_vec items);
TP_PRIVATE tp_credit_vec tp_by_pos_credits(tp_credit_vec items);
TP_PRIVATE tp_positioned_year_vec tp_by_pos_years(tp_positioned_year_vec items);
TP_PRIVATE tp_positioned_junk_vec tp_by_pos_junk(tp_positioned_junk_vec items);
/* finish is R6 to R9 on a staged input. */
TP_PRIVATE tp_track tp_finish(tp_staged st, bool list_context);

/* ============================================================================================ */
/* format.go: R11 and allArtists                                                                */

/* isFeatMarker reports whether raw, as one word, is a feat marker. */
TP_PRIVATE bool tp_is_feat_marker(tp_str raw, const tp_tables *t);
/* canonicalJoiner is R11.5: map a raw joiner to its canonical form (joiners.json); feat markers
 * become featCanonical. A featured list's lead marker is always a feat marker. */
TP_PRIVATE tp_str tp_canonical_joiner(tp_str raw, const tp_tables *t, bool feat_lead);
/* joinSep is R11.1: " " + joiner + " "; tight joiners as the raw character + " "; a missing
 * joiner as &. */
TP_PRIVATE tp_str tp_join_sep(const tp_str *joiner, const tp_resolved_format *o,
                              const tp_tables *t);
/* joinNames is the names of one credit list joined by their own joiners. */
TP_PRIVATE tp_str tp_join_names(tp_artist_ptr_vec list, const tp_resolved_format *o,
                                const tp_tables *t);
/* leadMarker is the marker in front of a featured list. */
TP_PRIVATE tp_str tp_lead_marker(const tp_artist *first, const tp_resolved_format *o,
                                 const tp_tables *t, tp_str fallback);
/* withRole is the artists of track with role, in order (pointers into artists). */
TP_PRIVATE tp_artist_ptr_vec tp_with_role(tp_artist_vec artists, tp_role role);
/* formatTrack is R11: track rendered back to a string with the resolved options (default
 * tables). */
TP_PRIVATE tp_str tp_format_track(const tp_track *track, tp_resolved_format o);
/* allArtists is every credited name: the artists plus every version's remixers, deduplicated by
 * the R0.4 key, first kept. The Go closure add becomes a static helper. */
TP_PRIVATE tp_artist_vec tp_all_artists(const tp_track *track);

/* ============================================================================================ */
/* tl_options.go: R13.10 number views                                                           */

/* jsInteger is a JS number that Number.isInteger accepts. */
TP_PRIVATE bool tp_js_integer(tp_json v, bool present, double *out);
/* jsFinite is a JS number that Number.isFinite accepts. */
TP_PRIVATE bool tp_js_finite(tp_json v, bool present, double *out);

/* ============================================================================================ */
/* tl_split.go: R13.1                                                                           */

/* tlLine is one line of a tracklist text: the line without its terminator, and the terminator. */
struct tp_tl_line {
  tp_str raw;
  tp_eol eol;
};

/* splitLines is R13.1.1: split at LF, CRLF and CR only. A trailing empty segment after the final
 * terminator is not a line; "" has no lines. R13.1.2: raw + eol joined is the text. */
TP_PRIVATE tp_tl_line_vec tp_split_lines(tp_str text);
/* hasLineBreak reports a line break inside a single-line value (R13.9.5, R13.9.6). */
TP_PRIVATE bool tp_has_line_break(tp_str s);

/* ============================================================================================ */
/* tl_times.go: R13.2 and the R13.9.2 time writer                                               */

/* maxSafeInteger is JS Number.MAX_SAFE_INTEGER. */
#define TP_MAX_SAFE_INTEGER 9007199254740991.0

/* timeStyle is the written shape of a time, kept by edits (R13.9.2). */
struct tp_time_style {
  bool hours;
  ptrdiff_t hour_width;   /* digits written for the hours (minimum width), 0 without hours */
  ptrdiff_t minute_width; /* digits written for the minutes (minimum width) */
  bool long_minutes;      /* a two-part time already at 60 minutes or more */
  tp_rune frac_sep;       /* '.' or ',' with a fraction, else TP_NORUNE */
  ptrdiff_t frac_digits;
};

/* timeScan is what R13.2 finds at the start of a line. */
struct tp_time_scan {
  tp_parts parts;
  const tp_tracklist_time *time;
  const tp_time_style *style;
  bool invalid; /* R13.2.4: a digits:digits shape after the lead that fails R13.2.2 */
  tp_str text;  /* R13.12: rest without its paired emphasis markers */
};

/* timeShape is a digits(:digits)+ run: the digit runs between the colons (2 or more) and the
 * index past it. */
struct tp_time_shape {
  tp_runes_vec parts;
  ptrdiff_t end;
};

/* formattedTime is a time written by formatTime. */
struct tp_formatted_time {
  tp_str raw;
  bool upgraded;
};

/* isGapSeparator is an R13.2.3 separator after the time. */
TP_PRIVATE bool tp_is_gap_separator(tp_rune r);
/* skipWhitespace is the index past the R0.2 whitespace at from. */
TP_PRIVATE ptrdiff_t tp_skip_whitespace(tp_runes s, ptrdiff_t from);
/* isBullet is an R13.2.1 bullet. */
TP_PRIVATE bool tp_is_bullet(tp_rune r);
/* untimed is R13.1.3 / R13.2.1: a line without a time (lead: optional bullet). */
TP_PRIVATE tp_time_scan tp_untimed(tp_runes raw, bool invalid);
/* closingRun is R13.2.3: the index past a closing run for an opening run of open, or from. */
TP_PRIVATE ptrdiff_t tp_closing_run(tp_runes raw, ptrdiff_t from, tp_rune open);
/* secondsOf is whole.frac read as one decimal literal (JS Number(), correctly rounded:
 * tp_parse_number). */
TP_PRIVATE double tp_seconds_of(ptrdiff_t whole, tp_str frac);
/* shapeAt is R13.2.4: the maximal digits(:digits)+ run at from. */
TP_PRIVATE bool tp_shape_at(tp_runes s, ptrdiff_t from, tp_time_shape *out);
/* shapeSeconds is the R13.2.2 validity test: the whole seconds of A:B or A:B:C. */
TP_PRIVATE bool tp_shape_seconds(tp_runes_vec parts, ptrdiff_t *out);
/* startsWithAt reports whether s has word at from. */
TP_PRIVATE bool tp_starts_with_at(tp_runes s, ptrdiff_t from, tp_str word);
/* scanTime is R13.2: the lead, time and gap at the start of a line; the rest is everything
 * after. The Go closure followsTime becomes a static helper. */
TP_PRIVATE tp_time_scan tp_scan_time(tp_str line);
/* pad writes n with at least width digits. */
TP_PRIVATE tp_str tp_pad(int64_t n, ptrdiff_t width);
/* formatTime is R13.9.2: seconds (finite, not negative) written in style, rounded half up to
 * the fraction precision (tp_round_half_up). false when it cannot be a valid R13.2.2 time. */
TP_PRIVATE bool tp_format_time(double seconds, const tp_time_style *style, bool one_digit_hours,
                               tp_formatted_time *out);

/* ============================================================================================ */
/* tl_emphasis.go: R13.12                                                                       */

/* runLength is the length of the maximal run of '*' or '_' at from (0 when none). */
TP_PRIVATE ptrdiff_t tp_run_length(tp_runes s, ptrdiff_t from);
/* emphasisRun is R13.2.1 / R13.2.3: the length of an emphasis run at from (1 or 2), else 0. */
TP_PRIVATE ptrdiff_t tp_emphasis_run(tp_runes s, ptrdiff_t from);
/* isEmphasisOuter is the R13.12.2 outer side. */
TP_PRIVATE bool tp_is_emphasis_outer(tp_rune r);
/* isEmphasisInner is the R13.12.2 inner side: not R0.2 whitespace. */
TP_PRIVATE bool tp_is_emphasis_inner(tp_rune r);
/* emphasisStack is the stack of a run ('*', '**', '_', '__'); -1 for others. */
TP_PRIVATE ptrdiff_t tp_emphasis_stack(tp_rune ch, ptrdiff_t n);
/* markRange sets marked[from:from+n], clamped (writes through the shared storage). */
TP_PRIVATE void tp_mark_range(tp_bool_vec marked, ptrdiff_t from, ptrdiff_t n);
/* withoutMarkers is R13.12: raw[from:] without its emphasis markers; open is a lead run the gap
 * left open (R13.12.4), or TP_NORUNE. */
TP_PRIVATE tp_str tp_without_markers(tp_runes raw, ptrdiff_t from, tp_rune open);

/* ============================================================================================ */
/* tl_kinds.go: R13.3, R13.13                                                                   */

/* Go sync.OnceValue vars tlHeaderWords, tlLayeredWords, tlMarkerTable (tp_once_get, with the
 * current-arena fallback). */
TP_PRIVATE const tp_word_set *tp_tl_header_words(void);
TP_PRIVATE const tp_word_set *tp_tl_layered_words(void);
TP_PRIVATE const tp_phrase_table *tp_tl_marker_table(void);
/* buildMarkerTable is data/marker-words.json plus extra, split into words as the track part is
 * (R13.3). */
TP_PRIVATE const tp_phrase_table *tp_build_marker_table(tp_strvec extra);
/* markerTableFor is the marker table with options.markerWords; the default built once. */
TP_PRIVATE const tp_phrase_table *tp_marker_table_for(tp_strvec extra);
/* isMarkerBreak splits marker words: '/', '-' and R0.2 whitespace. */
TP_PRIVATE bool tp_is_marker_break(tp_rune r);
/* isMarkerBracket is one of ()[]{}. */
TP_PRIVATE bool tp_is_marker_bracket(tp_rune r);
/* markerWords is R13.3: split on R0.2 whitespace, '/' and '-', strip leading and trailing
 * ()[]{} from each piece, drop empty ones. The Go closure flush becomes a static helper. */
TP_PRIVATE tp_strvec tp_marker_words(tp_str text);
/* layeredPrefix is R13.13: the length (code points) of a layered prefix, or 0. */
TP_PRIVATE ptrdiff_t tp_layered_prefix(tp_runes text);
/* isBlank is R13.3.1: the line is only R0.2 whitespace. */
TP_PRIVATE bool tp_is_blank(tp_str raw);
/* trimWS trims R0.2 whitespace from both ends. */
TP_PRIVATE tp_runes tp_trim_ws(tp_str s);
/* isLink is R13.3.3: starts with http://, https:// or www. (ASCII case-insensitive). */
TP_PRIVATE bool tp_is_link(tp_str rest);
/* isHeader is R13.3.3: ends with ':', or is a header word plus at most one more word. */
TP_PRIVATE bool tp_is_header(tp_str rest);
/* hasMarkerWord is R13.3: any marker word (or multi-word entry) among the track part's words. */
TP_PRIVATE bool tp_has_marker_word(tp_str track_part, const tp_phrase_table *markers);
/* hasSeparator is R13.3: not empty after R1, no noSeparator warning, and a non-empty artist side
 * (a primary artist or flags.unknown_artist). */
TP_PRIVATE bool tp_has_separator(const tp_track *t, bool empty);

/* ============================================================================================ */
/* tl_positions.go: R13.5                                                                       */

/* bareCandidate is a leading number that may be a position. */
struct tp_bare_candidate {
  tp_str raw;
  ptrdiff_t number;
  tp_str stripped; /* the track part without the digits and the following space */
};

/* findBareCandidate is R13.5: 1 to 3 ASCII digits and a U+0020 at the start, where R4.2 took no
 * position, with a non-blank remainder. NULL when none. */
TP_PRIVATE const tp_bare_candidate *tp_find_bare_candidate(tp_str rest, bool position_taken);
/* confirmPositions is R13.5: candidate i is a position when the nearest preceding or following
 * candidate line also has a bare number N - 1 or N + 1. bare: -1 for none. Linear. The Go
 * closures isCandidate and adjacent become static helpers. */
TP_PRIVATE tp_bool_vec tp_confirm_positions(tp_idxvec bare, tp_bool_vec candidate_line);

/* ============================================================================================ */
/* tl_warnings.go: R13.7                                                                        */

/* warningInput is what tracklistWarnings reads, one slot per entry. */
struct tp_warning_input {
  tp_entry_vec entries;
  tp_idxvec positions;         /* R4.2 / R13.5 position number of each entry's track, or -1 */
  tp_bool_vec invalid;         /* R13.2.4 */
  tp_bool_vec unparsed;        /* R13.7 line-unparsed */
  const double *audio_seconds; /* NULL when unknown */
};

/* digitSlot is one digit of a time that the search may change. */
struct tp_digit_slot {
  ptrdiff_t at; /* index of the digit in the raw time */
  ptrdiff_t field;
  int64_t place;
  int64_t digit;
};

/* digitChange is one digit change: the slot and its new digit. */
struct tp_digit_change {
  tp_digit_slot slot;
  int64_t digit;
};

/* searchOutcome is the result of one line's digit search. */
typedef enum tp_search_outcome { TP_SEARCH_NONE, TP_SEARCH_ONE, TP_SEARCH_MANY } tp_search_outcome;

/* timeDigits is a raw time taken apart for the search. */
struct tp_time_digits {
  tp_runes raw;
  tp_str frac;
  tp_intvec values; /* Go []int64 */
  const double *lo;
  const double *hi;
};

/* timedEntry is a timed entry: its line number and time. */
struct tp_timed_entry {
  ptrdiff_t line;
  const tp_tracklist_time *time;
};

/* valueWith is the value with changes applied, when it is a valid time inside the bounds. */
TP_PRIVATE bool tp_time_digits_value_with(const tp_time_digits *d, tp_digit_change_vec changes,
                                          double *out);
/* render is the raw time with changes applied. */
TP_PRIVATE tp_str tp_time_digits_render(const tp_time_digits *d, tp_digit_change_vec changes);
/* searchTime is the R13.7 suggestion search on one raw time: change 1, then 2 digits (not
 * fraction digits); keep valid times with lo < t < hi (each bound when present); only the
 * smallest change count with any candidate counts. *found is written for TP_SEARCH_ONE. */
TP_PRIVATE tp_search_outcome tp_search_time(tp_str raw_time, const double *lo, const double *hi,
                                            tp_tracklist_time *found);
/* suggestFix is the R13.7 suggestion for the out-of-order timed entry k: its own time between
 * its neighbours first; only when that finds no candidate at all, the previous timed entry's
 * time between the one before it and entry k. NULL when none. */
TP_PRIVATE const tp_suggestion *tp_suggest_fix(tp_timed_entry_vec times, ptrdiff_t k);
/* warningRank is the table order of R13.7 (a switch, not a lookup). */
TP_PRIVATE ptrdiff_t tp_warning_rank(tp_warning_code code);
/* flagAt is list[i], false out of range. */
TP_PRIVATE bool tp_flag_at(tp_bool_vec list, ptrdiff_t i);
/* tracklistWarnings is R13.7. The Go closure add becomes a static helper. */
TP_PRIVATE tp_tracklist_warning_vec tp_tracklist_warnings(const tp_warning_input *in);

/* ============================================================================================ */
/* tl_parse.go: R13.1 to R13.8                                                                  */

/* tlRow is one line on its way to an entry. */
struct tp_tl_row {
  tp_tl_line line;
  tp_time_scan scan;
  tp_str text;  /* R13.4.1: rest without emphasis markers (R13.12) and layered prefix (R13.13) */
  bool layered; /* R13.13 */
  bool blank;
  bool candidate; /* R13.4.2: the line has a time or a spaced dash */
  bool dash;
  bool probed;
  tp_line_probe probe;
  const tp_bare_candidate *bare;
  const tp_track *track;
  bool separated; /* R13.3: the track part has a separator */
  bool marker;    /* R13.3: the track part has a marker word */
};

/* parseTracklist is R13.1 to R13.8 on one (already sanitized) text. */
TP_PRIVATE tp_tracklist tp_parse_tracklist(tp_str text, tp_resolved_tracklist o);
/* kindOf is R13.3. */
TP_PRIVATE tp_entry_kind tp_kind_of(const tp_tl_row *r);
/* statsOf counts the entries. */
TP_PRIVATE tp_stats tp_stats_of(tp_entry_vec entries);

/* ============================================================================================ */
/* tl_edits.go: R13.9                                                                           */

/* workLine is one line of the text being edited. */
struct tp_work_line {
  tp_str raw;
  tp_eol eol;
  bool changed;      /* an edit wrote the line (R13.9.1) */
  tp_time_scan scan; /* tp_scan_time(raw), kept in step with raw */
};

/* editCtx is the edit state. Every edit validates before it mutates, so an invalid edit
 * (R13.9.6) only drops the warnings it added. */
struct tp_edit_ctx {
  tp_work_line_ptr_vec lines;
  ptrdiff_t index; /* the edit being applied */
  tp_raw_edit_warning_vec warnings;
  ptrdiff_t one_digit;  /* times with one-digit hours (R13.9.2) */
  ptrdiff_t wide_hours; /* times with wider hours */
  ptrdiff_t eols[3];    /* lines ending with LF, CRLF, CR (R13.9.5) */
};

/* retimed is a line rewritten for a new time. */
struct tp_retimed {
  ptrdiff_t index;
  tp_str raw;
  bool clamped;
  bool upgraded;
};

/* touched are the lines whose neighbourhood canonical must check after an edit; all means every
 * line (sort-by-time). */
struct tp_touched {
  bool all;
  tp_idxvec lines;
};

/* timeBlock is a timed line plus the untimed lines after it. */
struct tp_time_block {
  double seconds;
  ptrdiff_t from;
  ptrdiff_t to;
};

TP_PRIVATE tp_work_line *tp_new_work_line(tp_str raw, tp_eol eol, bool changed);
/* eolSlot is the index of eol in edit_ctx.eols, -1 for none. */
TP_PRIVATE ptrdiff_t tp_eol_slot(tp_eol eol);
/* countHours adds (by = 1) or removes (by = -1) a time from the hour-width counts (R13.9.2). */
TP_PRIVATE void tp_edit_ctx_count_hours(tp_edit_ctx *c, const tp_time_scan *s, ptrdiff_t by);
TP_PRIVATE void tp_edit_ctx_count_eol(tp_edit_ctx *c, tp_eol eol, ptrdiff_t by);
/* countLine is a line entering (by = 1) or leaving (by = -1) the text. */
TP_PRIVATE void tp_edit_ctx_count_line(tp_edit_ctx *c, const tp_work_line *l, ptrdiff_t by);
TP_PRIVATE void tp_edit_ctx_set_eol(tp_edit_ctx *c, tp_work_line *l, tp_eol eol);
TP_PRIVATE void tp_edit_ctx_set_raw(tp_edit_ctx *c, tp_work_line *l, tp_str raw);
TP_PRIVATE void tp_edit_ctx_warn(tp_edit_ctx *c, tp_edit_warning_code code, const double *line);
/* lineIndex is the 0-based index of an existing 1-based line number. */
TP_PRIVATE bool tp_edit_ctx_line_index(const tp_edit_ctx *c, tp_json v, bool present,
                                       ptrdiff_t *out);
/* oneDigitHours is R13.9.2: do the text's times with hours all use one-digit hours (and is there
 * at least one)? */
TP_PRIVATE bool tp_edit_ctx_one_digit_hours(const tp_edit_ctx *c);
/* singleLine is a string without a line break. */
TP_PRIVATE bool tp_single_line(tp_json v, bool present, tp_str *out);
/* retime is R13.9.2 / R13.9.3: the rewritten line for a new value; false when it cannot be
 * written. */
TP_PRIVATE bool tp_edit_ctx_retime(tp_edit_ctx *c, ptrdiff_t index, double value, bool one_digit,
                                   tp_retimed *out);
TP_PRIVATE void tp_edit_ctx_commit(tp_edit_ctx *c, tp_retimed_vec changes);
TP_PRIVATE bool tp_edit_ctx_set_time(tp_edit_ctx *c, tp_json edit, tp_touched *out);
TP_PRIVATE bool tp_edit_ctx_shift(tp_edit_ctx *c, tp_json edit, tp_touched *out);
/* sortByTime is R13.9.4: a stable sort of the blocks (tp_sort_stable). */
TP_PRIVATE tp_touched tp_edit_ctx_sort_by_time(tp_edit_ctx *c);
TP_PRIVATE bool tp_edit_ctx_set_rest(tp_edit_ctx *c, tp_json edit, tp_touched *out);
/* commonEol is R13.9.5: the most common non-empty eol (ties: first seen in line order), "\n" if
 * none. The Go closure isTop becomes a static helper. */
TP_PRIVATE tp_eol tp_edit_ctx_common_eol(const tp_edit_ctx *c);
TP_PRIVATE bool tp_edit_ctx_insert(tp_edit_ctx *c, tp_json edit, tp_touched *out);
TP_PRIVATE bool tp_edit_ctx_remove(tp_edit_ctx *c, tp_json edit, tp_touched *out);
/* mergeAt joins a "\r" line followed by an empty "\n" line into one CRLF line. */
TP_PRIVATE void tp_edit_ctx_merge_at(tp_edit_ctx *c, ptrdiff_t k);
/* canonical is R13.9.5: keep the work lines equal to what splitLines(join(lines)) would give,
 * checking only the neighbourhood of touched lines (and the end); all checks every line. */
TP_PRIVATE void tp_edit_ctx_canonical(tp_edit_ctx *c, tp_touched t);
/* applyOne applies one edit; false when it is invalid (R13.9.6). */
TP_PRIVATE bool tp_edit_ctx_apply_one(tp_edit_ctx *c, tp_json edit, tp_touched *out);
/* invalidLine is R13.9.6: the edit's integer line, else its integer after, else NULL. */
TP_PRIVATE const double *tp_invalid_line(tp_json edit);
/* applyEdits is R13.9 on one (already sanitized) text: apply edits (loose JSON objects) in
 * order. Never fails: an invalid edit changes nothing and warns edit-invalid. */
TP_PRIVATE tp_raw_edit_result tp_apply_edits(tp_str text, const tp_json *edits, size_t count);

/* ============================================================================================ */
/* json.go, writer half (src/json.c): JSON.stringify bytes. The reader half is wire/json_read.c */

/* appendJSString appends s as JSON.stringify writes a string: only ", \ and code points below
 * U+0020 escaped, with \b \f \n \r \t. Invalid UTF-8 is replaced as tp_sanitize does. */
TP_PRIVATE void tp_append_js_string(tp_buf *b, tp_str s);
/* appendJSNumber appends n as Number#toString writes it (tp_buf_number); NaN and the infinities
 * as null, as JSON.stringify does. */
TP_PRIVATE void tp_append_js_number(tp_buf *b, double n);

/* jw is a small streaming writer for JSON objects and arrays. */
struct tp_jw {
  tp_buf b;
  bool first; /* right after an opening brace or bracket */
};

/* C addition: a writer ready for one value (first = true, as marshal starts it). */
TP_PRIVATE tp_jw tp_jw_new(void);
/* C addition: the text written so far. */
TP_PRIVATE tp_str tp_jw_done(tp_jw *w);
TP_PRIVATE void tp_jw_sep(tp_jw *w);
TP_PRIVATE void tp_jw_open(tp_jw *w, char c);
TP_PRIVATE void tp_jw_close(tp_jw *w, char c);
/* key writes an object key; the value that follows must not call sep again. */
TP_PRIVATE void tp_jw_key(tp_jw *w, tp_str k);
TP_PRIVATE void tp_jw_str(tp_jw *w, tp_str s);
TP_PRIVATE void tp_jw_num(tp_jw *w, double n);
TP_PRIVATE void tp_jw_integer(tp_jw *w, int64_t n); /* written as the double (double)n */
TP_PRIVATE void tp_jw_null(tp_jw *w);
TP_PRIVATE void tp_jw_boolean(tp_jw *w, bool v);
TP_PRIVATE void tp_jw_opt_str(tp_jw *w, const tp_str *s);
TP_PRIVATE void tp_jw_opt_int(tp_jw *w, const ptrdiff_t *n);
/* C addition: optInt for the int64_t edit-warning line. */
TP_PRIVATE void tp_jw_opt_int64(tp_jw *w, const int64_t *n);
TP_PRIVATE void tp_jw_opt_num(tp_jw *w, const double *n);
TP_PRIVATE void tp_jw_strings(tp_jw *w, tp_strvec list);
TP_PRIVATE void tp_jw_ints(tp_jw *w, tp_intvec list); /* edit lines, changed */

/* The JSON forms: camelCase keys in the schema's order, every key present. A vector is written
 * as [] whether or not it was ever allocated (Go nil and empty are the same here). */
TP_PRIVATE void tp_artist_write(const tp_artist *a, tp_jw *w);
TP_PRIVATE void tp_write_artists(tp_jw *w, tp_artist_vec list);
TP_PRIVATE void tp_version_write(const tp_version *v, tp_jw *w);
TP_PRIVATE void tp_junk_write(const tp_junk *j, tp_jw *w);
TP_PRIVATE void tp_flags_write(const tp_flags *f, tp_jw *w);
TP_PRIVATE void tp_position_write(const tp_position *p, tp_jw *w);
TP_PRIVATE void tp_timestamp_write(const tp_timestamp *t, tp_jw *w);
TP_PRIVATE void tp_track_write(const tp_track *t, tp_jw *w);
TP_PRIVATE void tp_parts_write(const tp_parts *p, tp_jw *w);
TP_PRIVATE void tp_tracklist_time_write(const tp_tracklist_time *t, tp_jw *w);
TP_PRIVATE void tp_entry_write(const tp_entry *e, tp_jw *w);
TP_PRIVATE void tp_suggestion_write(const tp_suggestion *s, tp_jw *w);
TP_PRIVATE void tp_tracklist_warning_write(const tp_tracklist_warning *x, tp_jw *w);
TP_PRIVATE void tp_stats_write(const tp_stats *s, tp_jw *w);
TP_PRIVATE void tp_tracklist_write(const tp_tracklist *t, tp_jw *w);
/* Edit: only the fields of its operation ({"op":"set-time","line":3,"seconds":90}); lines only
 * when has_lines, from only when non-NULL. */
TP_PRIVATE void tp_edit_write(const tp_edit *e, tp_jw *w);
TP_PRIVATE void tp_edit_warning_write(const tp_edit_warning *x, tp_jw *w);
TP_PRIVATE void tp_edit_result_write(const tp_edit_result *r, tp_jw *w);
TP_PRIVATE void tp_raw_edit_result_write(const tp_raw_edit_result *r, tp_jw *w);
/* jsonValue.write. Recursive: depth is bounded by TP_JSON_MAX_DEPTH of the reader (200) or by
 * the builder (tp_edit_json: 2). */
TP_PRIVATE void tp_json_write(const tp_json *v, tp_jw *w);

/* marshal (Go takes the writer interface; C has one per type): compact JSON text. */
TP_PRIVATE tp_str tp_track_json(const tp_track *t);
TP_PRIVATE tp_str tp_artists_json(tp_artist_vec list);
TP_PRIVATE tp_str tp_tracklist_json(const tp_tracklist *t);
TP_PRIVATE tp_str tp_edit_result_json(const tp_edit_result *r);
TP_PRIVATE tp_str tp_raw_edit_result_json(const tp_raw_edit_result *r);
TP_PRIVATE tp_str tp_string_json(tp_str s);

/* jsonValue.getString: the string at key, or false. (jsonValue.get is tp_json_get in rt.h.) */
TP_PRIVATE bool tp_json_get_string(tp_json v, tp_str key, tp_str *out);
/* jsonOf(&edit): the edit's JSON value, exactly what reading tp_edit_write's text back gives,
 * built directly (the library has no reader): strings tp_sanitize'd, line/after/from/lines as
 * doubles, a NaN or infinite seconds as null, -0 as 0, keys in tp_edit_write's order. */
TP_PRIVATE tp_json tp_edit_json(const tp_edit *e);

/* ============================================================================================ */
/* wire.go (wire/wire.c): the JSON door. tp_wire_call itself is declared in wire/wire.h, and
 * artistList.write / wireString.write are static helpers of wire.c (declared here they would be
 * static-but-undefined in the amalgamation, which leaves wire/ out). */

/* ============================================================================================ */
/* trackparse.go: the public functions over internal types (src/trackparse.c). Each sanitizes  */
/* its string input (tp_sanitize). Prefixed tp_api_ (deviation): tp_normalize,                 */
/* tp_parse_tracklist, tp_apply_edits and tp_all_artists already name the internal functions.  */

/* Parse parses one track string with the default options. */
TP_PRIVATE tp_track tp_api_parse(tp_str input);
/* ParseWith parses with options (R12); tables compiled per call. o NULL: defaults. */
TP_PRIVATE tp_track tp_api_parse_with(tp_str input, const tp_parse_options *o);
/* ParseArtists splits an artist credit string into artists (R10). */
TP_PRIVATE tp_artist_vec tp_api_parse_artists(tp_str input);
TP_PRIVATE tp_artist_vec tp_api_parse_artists_with(tp_str input, const tp_parse_options *o);
/* Format renders a parsed track back to a string (R11) with the default options. */
TP_PRIVATE tp_str tp_api_format(const tp_track *t);
TP_PRIVATE tp_str tp_api_format_with(const tp_track *t, const tp_format_options *o);
/* Normalize is R1 on sanitized input. */
TP_PRIVATE tp_str tp_api_normalize(tp_str input);
/* AllArtists is every artist of the track, remixers included, without duplicates. */
TP_PRIVATE tp_artist_vec tp_api_all_artists(const tp_track *t);

/* Parser parses with fixed options, compiled once; safe for concurrent use. A NULL parser or a
 * NULL e uses the shared engine. */
struct tp_parser {
  const tp_engine *e;
};

/* NewParser compiles options (R12) into a parser, in the current arena. */
TP_PRIVATE tp_parser *tp_new_parser(const tp_parse_options *o);
TP_PRIVATE const tp_engine *tp_parser_engine(const tp_parser *p);
TP_PRIVATE tp_track tp_parser_parse(const tp_parser *p, tp_str input);
TP_PRIVATE tp_artist_vec tp_parser_parse_artists(const tp_parser *p, tp_str input);
/* ParseTracklist parses a multi-line tracklist (R13). */
TP_PRIVATE tp_tracklist tp_api_parse_tracklist(tp_str text);
TP_PRIVATE tp_tracklist tp_api_parse_tracklist_with(tp_str text, const tp_tracklist_options *o);
/* ApplyEdits applies edits in order, each to the previous result (R13.9), through each edit's
 * JSON form (tp_edit_json), then types the result (tp_raw_edit_result_result). */
TP_PRIVATE tp_edit_result tp_api_apply_edits(tp_str text, const tp_edit *edits, size_t count);
/* ApplyEditsWith: options accepted for symmetry (R13.10); edits do not depend on them. */
TP_PRIVATE tp_edit_result tp_api_apply_edits_with(tp_str text, const tp_edit *edits, size_t count,
                                                  const tp_tracklist_options *o);

#endif /* TP_INTERNAL_H */
