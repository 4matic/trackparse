/**
 * @file trackparse.hpp
 * @brief Header-only C++17 wrapper over the trackparse C API (trackparse.h).
 *
 * Documentation: https://4matic.github.io/trackparse/
 *
 * - Owners (track, artists, tracklist, edit_result, parser and the option builders) are
 *   move-only and free their C object in the destructor. A moved-from owner must not be read.
 * - Views (track_view, artist_view, version_view, entry_view...) borrow from an owner and are
 *   valid while it lives. Strings are std::string_view, nullable fields std::optional, arrays
 *   iterable spans (begin/end, size, operator[]).
 * - Option builders chain: parse_options{}.mode(mode::youtube).known_artist("Chase & Status").
 * - The only exception thrown is std::bad_alloc, when the library runs out of memory (the C
 *   functions return NULL). Without exceptions (-fno-exceptions) that calls std::abort().
 */
#ifndef TRACKPARSE_HPP
#define TRACKPARSE_HPP

#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "trackparse.h"

namespace trackparse {

/* ------------------------------------------------------------------------------------------ */
/* Version                                                                                      */

/** The version of the header (TRACKPARSE_VERSION). */
inline constexpr std::string_view header_version = TRACKPARSE_VERSION;
/** The version of the linked library (can differ from header_version with a shared library). */
inline std::string_view library_version() { return trackparse_library_version(); }
/** The spec version the library implements. */
inline std::string_view spec_version() { return trackparse_spec_version(); }

namespace detail {

[[noreturn]] inline void out_of_memory() {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
  throw std::bad_alloc();
#else
  std::abort();
#endif
}

template <class T> T *check(T *p) {
  if (p == nullptr) out_of_memory();
  return p;
}

inline void check(bool ok) {
  if (!ok) out_of_memory();
}

inline std::string_view sv(const trackparse_str &s) { return std::string_view(s.ptr, s.len); }

inline std::optional<std::string_view> opt(const trackparse_str *s) {
  if (s == nullptr) return std::nullopt;
  return sv(*s);
}

template <class T> std::optional<T> opt(const T *v) {
  if (v == nullptr) return std::nullopt;
  return *v;
}

/* A string the library allocated, copied into a std::string. */
inline std::string take(trackparse_string *s) {
  struct guard {
    trackparse_string *s;
    ~guard() { trackparse_string_free(s); }
  } g{check(s)};
  return std::string(g.s->str.ptr, g.s->str.len);
}

} // namespace detail

/* ------------------------------------------------------------------------------------------ */
/* Enumerations: the values of the C enums, with name() and *_from_name()                       */

enum class mode : int {
  auto_ = TRACKPARSE_MODE_AUTO, /**< options only: detect (the default) */
  clean = TRACKPARSE_MODE_CLEAN,
  youtube = TRACKPARSE_MODE_YOUTUBE,
  filename = TRACKPARSE_MODE_FILENAME,
  tracklist = TRACKPARSE_MODE_TRACKLIST
};

enum class role : int {
  primary = TRACKPARSE_ROLE_PRIMARY,
  featured = TRACKPARSE_ROLE_FEATURED,
  remixer = TRACKPARSE_ROLE_REMIXER,
  producer = TRACKPARSE_ROLE_PRODUCER
};

enum class source : int {
  artist = TRACKPARSE_SOURCE_ARTIST,
  title = TRACKPARSE_SOURCE_TITLE,
  version = TRACKPARSE_SOURCE_VERSION
};

enum class version_type : int {
  remix = TRACKPARSE_VERSION_REMIX,
  bootleg = TRACKPARSE_VERSION_BOOTLEG,
  vip = TRACKPARSE_VERSION_VIP,
  edit = TRACKPARSE_VERSION_EDIT,
  flip = TRACKPARSE_VERSION_FLIP,
  refix = TRACKPARSE_VERSION_REFIX,
  rework = TRACKPARSE_VERSION_REWORK,
  mashup = TRACKPARSE_VERSION_MASHUP,
  blend = TRACKPARSE_VERSION_BLEND,
  dub = TRACKPARSE_VERSION_DUB,
  mix = TRACKPARSE_VERSION_MIX,
  extended = TRACKPARSE_VERSION_EXTENDED,
  radio = TRACKPARSE_VERSION_RADIO,
  club = TRACKPARSE_VERSION_CLUB,
  original = TRACKPARSE_VERSION_ORIGINAL,
  instrumental = TRACKPARSE_VERSION_INSTRUMENTAL,
  acapella = TRACKPARSE_VERSION_ACAPELLA,
  live = TRACKPARSE_VERSION_LIVE,
  acoustic = TRACKPARSE_VERSION_ACOUSTIC,
  remaster = TRACKPARSE_VERSION_REMASTER,
  demo = TRACKPARSE_VERSION_DEMO,
  reprise = TRACKPARSE_VERSION_REPRISE,
  cover = TRACKPARSE_VERSION_COVER,
  version = TRACKPARSE_VERSION_VERSION, /**< also used for names keywords introduced */
  sped_up = TRACKPARSE_VERSION_SPED_UP,
  slowed = TRACKPARSE_VERSION_SLOWED,
  nightcore = TRACKPARSE_VERSION_NIGHTCORE
};

enum class delimiter : int {
  paren = TRACKPARSE_DELIMITER_PAREN,
  bracket = TRACKPARSE_DELIMITER_BRACKET,
  brace = TRACKPARSE_DELIMITER_BRACE,
  dash = TRACKPARSE_DELIMITER_DASH
};

enum class junk_kind : int {
  video = TRACKPARSE_JUNK_VIDEO,
  audio = TRACKPARSE_JUNK_AUDIO,
  lyrics = TRACKPARSE_JUNK_LYRICS,
  quality = TRACKPARSE_JUNK_QUALITY,
  promo = TRACKPARSE_JUNK_PROMO,
  platform = TRACKPARSE_JUNK_PLATFORM,
  label = TRACKPARSE_JUNK_LABEL,
  genre = TRACKPARSE_JUNK_GENRE,
  other = TRACKPARSE_JUNK_OTHER /**< also used for names keywords introduced */
};

/** Track warnings. */
enum class warning : int {
  no_separator = TRACKPARSE_WARNING_NO_SEPARATOR,
  ambiguous_separator = TRACKPARSE_WARNING_AMBIGUOUS_SEPARATOR,
  unspaced_dash_split = TRACKPARSE_WARNING_UNSPACED_DASH_SPLIT,
  asymmetric_dash_split = TRACKPARSE_WARNING_ASYMMETRIC_DASH_SPLIT,
  by_split = TRACKPARSE_WARNING_BY_SPLIT,
  quoted_title_split = TRACKPARSE_WARNING_QUOTED_TITLE_SPLIT,
  ambiguous_mix_credit = TRACKPARSE_WARNING_AMBIGUOUS_MIX_CREDIT,
  unbalanced_brackets = TRACKPARSE_WARNING_UNBALANCED_BRACKETS
};

enum class entry_kind : int {
  track = TRACKPARSE_ENTRY_TRACK,
  marker = TRACKPARSE_ENTRY_MARKER,
  header = TRACKPARSE_ENTRY_HEADER,
  link = TRACKPARSE_ENTRY_LINK,
  note = TRACKPARSE_ENTRY_NOTE,
  blank = TRACKPARSE_ENTRY_BLANK
};

enum class eol : int {
  lf = TRACKPARSE_EOL_LF,
  crlf = TRACKPARSE_EOL_CRLF,
  cr = TRACKPARSE_EOL_CR,
  none = TRACKPARSE_EOL_NONE
};

enum class tracklist_warning_code : int {
  time_missing = TRACKPARSE_TL_TIME_MISSING,
  time_out_of_order = TRACKPARSE_TL_TIME_OUT_OF_ORDER,
  time_duplicate = TRACKPARSE_TL_TIME_DUPLICATE,
  no_start_at_zero = TRACKPARSE_TL_NO_START_AT_ZERO,
  time_after_audio_end = TRACKPARSE_TL_TIME_AFTER_AUDIO_END,
  invalid_time = TRACKPARSE_TL_INVALID_TIME,
  position_gap = TRACKPARSE_TL_POSITION_GAP,
  position_duplicate = TRACKPARSE_TL_POSITION_DUPLICATE,
  line_unparsed = TRACKPARSE_TL_LINE_UNPARSED
};

enum class severity : int {
  info = TRACKPARSE_SEVERITY_INFO,
  warning = TRACKPARSE_SEVERITY_WARNING
};

enum class edit_warning_code : int {
  edit_invalid = TRACKPARSE_EDIT_WARNING_EDIT_INVALID,
  time_clamped = TRACKPARSE_EDIT_WARNING_TIME_CLAMPED,
  style_upgraded = TRACKPARSE_EDIT_WARNING_STYLE_UPGRADED
};

enum class split_and : int {
  auto_ = TRACKPARSE_SPLIT_AND_AUTO,
  always = TRACKPARSE_SPLIT_AND_ALWAYS,
  never = TRACKPARSE_SPLIT_AND_NEVER
};

/** Where format() writes featured artists. */
enum class feat : int {
  source = TRACKPARSE_FEAT_SOURCE,
  artist = TRACKPARSE_FEAT_ARTIST,
  title = TRACKPARSE_FEAT_TITLE,
  omit = TRACKPARSE_FEAT_OMIT
};

enum class joiners : int {
  original = TRACKPARSE_JOINERS_ORIGINAL,
  canonical = TRACKPARSE_JOINERS_CANONICAL
};

enum class versions : int { all = TRACKPARSE_VERSIONS_ALL, none = TRACKPARSE_VERSIONS_NONE };

/** The spec name of a value ("youtube", "noSeparator", "time-out-of-order", "\r\n"...). */
inline std::string_view name(trackparse::mode v) {
  return trackparse_mode_name(static_cast<trackparse_mode>(v));
}
inline std::string_view name(trackparse::role v) {
  return trackparse_role_name(static_cast<trackparse_role>(v));
}
inline std::string_view name(trackparse::source v) {
  return trackparse_source_name(static_cast<trackparse_source>(v));
}
inline std::string_view name(trackparse::version_type v) {
  return trackparse_version_type_name(static_cast<trackparse_version_type>(v));
}
inline std::string_view name(trackparse::delimiter v) {
  return trackparse_delimiter_name(static_cast<trackparse_delimiter>(v));
}
inline std::string_view name(trackparse::junk_kind v) {
  return trackparse_junk_kind_name(static_cast<trackparse_junk_kind>(v));
}
inline std::string_view name(trackparse::warning v) {
  return trackparse_warning_name(static_cast<trackparse_warning>(v));
}
inline std::string_view name(trackparse::entry_kind v) {
  return trackparse_entry_kind_name(static_cast<trackparse_entry_kind>(v));
}
inline std::string_view name(trackparse::eol v) {
  return trackparse_eol_name(static_cast<trackparse_eol>(v));
}
inline std::string_view name(trackparse::tracklist_warning_code v) {
  return trackparse_tracklist_warning_code_name(static_cast<trackparse_tracklist_warning_code>(v));
}
inline std::string_view name(trackparse::severity v) {
  return trackparse_severity_name(static_cast<trackparse_severity>(v));
}
inline std::string_view name(trackparse::edit_warning_code v) {
  return trackparse_edit_warning_code_name(static_cast<trackparse_edit_warning_code>(v));
}

/** Look a value up by its spec name. */
inline std::optional<trackparse::mode> mode_from_name(std::string_view s) {
  trackparse_mode v;
  if (!trackparse_mode_from_name(s.data(), s.size(), &v)) return std::nullopt;
  return static_cast<trackparse::mode>(v);
}
inline std::optional<trackparse::version_type> version_type_from_name(std::string_view s) {
  trackparse_version_type v;
  if (!trackparse_version_type_from_name(s.data(), s.size(), &v)) return std::nullopt;
  return static_cast<trackparse::version_type>(v);
}
inline std::optional<trackparse::junk_kind> junk_kind_from_name(std::string_view s) {
  trackparse_junk_kind v;
  if (!trackparse_junk_kind_from_name(s.data(), s.size(), &v)) return std::nullopt;
  return static_cast<trackparse::junk_kind>(v);
}

/* ------------------------------------------------------------------------------------------ */
/* span: a borrowed array of C items, read through a view type                                  */

namespace detail {
struct str_conv {
  static std::string_view apply(const trackparse_str &s) { return sv(s); }
};
template <class E, class C> struct enum_conv {
  static E apply(const C &v) { return static_cast<E>(v); }
};
template <class T> struct same_conv {
  static T apply(const T &v) { return v; }
};
template <class V, class C> struct view_conv {
  static V apply(const C &c) { return V(&c); }
};
} // namespace detail

/** A read-only array of `count` C items, iterated as values of type V. */
template <class C, class V, class Conv = detail::view_conv<V, C>> class span {
public:
  using value_type = V;
  using size_type = std::size_t;

  class iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = V;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = V;

    iterator() = default;
    explicit iterator(const C *p) : p_(p) {}
    V operator*() const { return Conv::apply(*p_); }
    iterator &operator++() {
      ++p_;
      return *this;
    }
    iterator operator++(int) {
      iterator r = *this;
      ++p_;
      return r;
    }
    friend bool operator==(iterator a, iterator b) { return a.p_ == b.p_; }
    friend bool operator!=(iterator a, iterator b) { return a.p_ != b.p_; }

  private:
    const C *p_ = nullptr;
  };

  span() = default;
  span(const C *items, std::size_t count) : items_(items), count_(items ? count : 0) {}

  iterator begin() const { return iterator(items_); }
  iterator end() const { return iterator(items_ + count_); }
  std::size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  /** No bounds check, like std::vector::operator[]. */
  V operator[](std::size_t i) const { return Conv::apply(items_[i]); }
  V front() const { return Conv::apply(items_[0]); }
  V back() const { return Conv::apply(items_[count_ - 1]); }
  /** The C array. */
  const C *data() const { return items_; }

private:
  const C *items_ = nullptr;
  std::size_t count_ = 0;
};

/* ------------------------------------------------------------------------------------------ */
/* Parsed track views                                                                           */

class artist_view {
public:
  explicit artist_view(const trackparse_artist *a) : a_(a) {}
  std::string_view name() const { return detail::sv(a_->name); }
  trackparse::role role() const { return static_cast<trackparse::role>(a_->role); }
  /** The text that joined this artist to the previous one; nullopt for the first. */
  std::optional<std::string_view> joiner() const { return detail::opt(a_->joiner); }
  trackparse::source source() const { return static_cast<trackparse::source>(a_->source); }
  const trackparse_artist *c_ptr() const { return a_; }

private:
  const trackparse_artist *a_;
};

using artist_span = span<trackparse_artist, artist_view>;
using string_span = span<trackparse_str, std::string_view, detail::str_conv>;

class version_view {
public:
  explicit version_view(const trackparse_version *v) : v_(v) {}
  trackparse::version_type type() const { return static_cast<trackparse::version_type>(v_->type); }
  /** The spec name of the type, also for names options keywords introduced. */
  std::string_view type_name() const { return detail::sv(v_->type_name); }
  std::string_view raw() const { return detail::sv(v_->raw); }
  artist_span artists() const { return artist_span(v_->artists, v_->artists_count); }
  string_span modifiers() const { return string_span(v_->modifiers, v_->modifiers_count); }
  std::optional<std::string_view> descriptor() const { return detail::opt(v_->descriptor); }
  std::optional<int> year() const { return detail::opt(v_->year); }
  bool unknown_artist() const { return v_->unknown_artist; }
  trackparse::delimiter delimiter() const {
    return static_cast<trackparse::delimiter>(v_->delimiter);
  }
  const trackparse_version *c_ptr() const { return v_; }

private:
  const trackparse_version *v_;
};

class junk_view {
public:
  explicit junk_view(const trackparse_junk *j) : j_(j) {}
  std::string_view raw() const { return detail::sv(j_->raw); }
  trackparse::junk_kind kind() const { return static_cast<trackparse::junk_kind>(j_->kind); }
  /** The spec name of the kind, also for names options keywords introduced. */
  std::string_view kind_name() const { return detail::sv(j_->kind_name); }
  const trackparse_junk *c_ptr() const { return j_; }

private:
  const trackparse_junk *j_;
};

/** The flags of a track. "explicit" is a C++ keyword, hence explicit_(). */
class flags_view {
public:
  explicit flags_view(const trackparse_flags *f) : f_(f) {}
  bool explicit_() const { return f_->explicit_; }
  bool clean() const { return f_->clean; }
  bool unknown_artist() const { return f_->unknown_artist; }
  bool unknown_title() const { return f_->unknown_title; }

private:
  const trackparse_flags *f_;
};

struct position {
  std::string_view raw;
  int number;
};

struct timestamp {
  std::string_view raw;
  int seconds;
};

using version_span = span<trackparse_version, version_view>;
using junk_span = span<trackparse_junk, junk_view>;
using warning_span = span<trackparse_warning, trackparse::warning,
                          detail::enum_conv<trackparse::warning, trackparse_warning>>;

class format_options;
class artists;
class track;

/** A parsed track, borrowed (from a track, or from a tracklist entry). */
class track_view {
public:
  explicit track_view(const trackparse_track *t) : t_(t) {}
  track_view(const track_view &) = default;
  track_view &operator=(const track_view &) = default;
  /** A view of a temporary track would dangle at once: keep the track in a variable. */
  track_view(track &&) = delete;
  std::string_view input() const { return detail::sv(t_->input); }
  trackparse::mode mode() const { return static_cast<trackparse::mode>(t_->mode); }
  std::optional<trackparse::position> position() const {
    if (t_->position == nullptr) return std::nullopt;
    return trackparse::position{detail::sv(t_->position->raw), t_->position->number};
  }
  std::optional<trackparse::timestamp> timestamp() const {
    if (t_->timestamp == nullptr) return std::nullopt;
    return trackparse::timestamp{detail::sv(t_->timestamp->raw), t_->timestamp->seconds};
  }
  artist_span artists() const { return artist_span(t_->artists, t_->artists_count); }
  std::string_view title() const { return detail::sv(t_->title); }
  std::string_view full_title() const { return detail::sv(t_->full_title); }
  version_span versions() const { return version_span(t_->versions, t_->versions_count); }
  std::optional<int> year() const { return detail::opt(t_->year); }
  flags_view flags() const { return flags_view(&t_->flags); }
  junk_span junk() const { return junk_span(t_->junk, t_->junk_count); }
  warning_span warnings() const { return warning_span(t_->warnings, t_->warnings_count); }

  /** The JSON.stringify bytes of the track. */
  std::string to_json() const { return detail::take(trackparse_track_to_json(t_)); }
  /** R11: "Artists - Title (Versions)". */
  std::string format() const { return detail::take(trackparse_format(t_)); }
  std::string format(const format_options &o) const;
  /** Every artist: the artist side, then each version's. */
  trackparse::artists all_artists() const;

  const trackparse_track *c_ptr() const { return t_; }

protected:
  const trackparse_track *t_;
};

/** The result of parse(): owns the C trackparse_track. Move-only. */
class track : public track_view {
public:
  /** Takes ownership; throws std::bad_alloc on NULL. */
  explicit track(trackparse_track *t) : track_view(detail::check(t)) {}
  track(track &&o) noexcept : track_view(o.t_) { o.t_ = nullptr; }
  track &operator=(track &&o) noexcept {
    if (this != &o) {
      trackparse_track_free(const_cast<trackparse_track *>(t_));
      t_ = o.t_;
      o.t_ = nullptr;
    }
    return *this;
  }
  track(const track &) = delete;
  track &operator=(const track &) = delete;
  ~track() { trackparse_track_free(const_cast<trackparse_track *>(t_)); }

  /** Gives up ownership: free the result with trackparse_track_free. */
  trackparse_track *release() {
    auto p = const_cast<trackparse_track *>(t_);
    t_ = nullptr;
    return p;
  }
};

/** A list of artists (parse_artists, all_artists). Move-only. */
class artists {
public:
  explicit artists(trackparse_artists *a) : a_(detail::check(a)) {}
  artists(artists &&o) noexcept : a_(o.a_) { o.a_ = nullptr; }
  artists &operator=(artists &&o) noexcept {
    if (this != &o) {
      trackparse_artists_free(a_);
      a_ = o.a_;
      o.a_ = nullptr;
    }
    return *this;
  }
  artists(const artists &) = delete;
  artists &operator=(const artists &) = delete;
  ~artists() { trackparse_artists_free(a_); }

  artist_span items() const { return artist_span(a_->items, a_->count); }
  artist_span::iterator begin() const { return items().begin(); }
  artist_span::iterator end() const { return items().end(); }
  std::size_t size() const { return a_->count; }
  bool empty() const { return a_->count == 0; }
  artist_view operator[](std::size_t i) const { return items()[i]; }
  std::string to_json() const { return detail::take(trackparse_artists_to_json(a_)); }
  const trackparse_artists *c_ptr() const { return a_; }

private:
  trackparse_artists *a_;
};

inline trackparse::artists track_view::all_artists() const {
  return trackparse::artists(trackparse_all_artists(t_));
}

/* ------------------------------------------------------------------------------------------ */
/* Options (move-only builders; setters chain on lvalues and on temporaries)                    */

/** Options for parse(), parse_artists() and parser (R12). */
class parse_options {
public:
  parse_options() : o_(detail::check(trackparse_parse_options_new())) {}
  parse_options(parse_options &&o) noexcept : o_(o.o_) { o.o_ = nullptr; }
  parse_options &operator=(parse_options &&o) noexcept {
    if (this != &o) {
      trackparse_parse_options_free(o_);
      o_ = o.o_;
      o.o_ = nullptr;
    }
    return *this;
  }
  parse_options(const parse_options &) = delete;
  parse_options &operator=(const parse_options &) = delete;
  ~parse_options() { trackparse_parse_options_free(o_); }

  parse_options &mode(trackparse::mode v) & {
    detail::check(trackparse_parse_options_set_mode(o_, static_cast<trackparse_mode>(v)));
    return *this;
  }
  parse_options &&mode(trackparse::mode v) && { return std::move(mode(v)); }

  parse_options &uploader(std::string_view s) & {
    detail::check(trackparse_parse_options_set_uploader(o_, s.data(), s.size()));
    return *this;
  }
  parse_options &&uploader(std::string_view s) && { return std::move(uploader(s)); }

  parse_options &known_artist(std::string_view s) & {
    detail::check(trackparse_parse_options_add_known_artist(o_, s.data(), s.size()));
    return *this;
  }
  parse_options &&known_artist(std::string_view s) && { return std::move(known_artist(s)); }

  /** Adds every name of a container of strings. */
  template <class Names> parse_options &known_artists(const Names &names) & {
    for (const auto &n : names) known_artist(std::string_view(n));
    return *this;
  }
  template <class Names> parse_options &&known_artists(const Names &names) && {
    return std::move(known_artists(names));
  }
  parse_options &known_artists(std::initializer_list<std::string_view> names) & {
    for (auto n : names) known_artist(n);
    return *this;
  }
  parse_options &&known_artists(std::initializer_list<std::string_view> names) && {
    return std::move(known_artists(names));
  }

  parse_options &split_and(trackparse::split_and v) & {
    detail::check(trackparse_parse_options_set_split_and(o_, static_cast<trackparse_split_and>(v)));
    return *this;
  }
  parse_options &&split_and(trackparse::split_and v) && { return std::move(split_and(v)); }

  /** keywords.versionHeads: `key` names a version of `type` (a spec name; new names allowed). */
  parse_options &version_head(std::string_view key, std::string_view type) & {
    detail::check(trackparse_parse_options_add_version_head(o_, key.data(), key.size(), type.data(),
                                                            type.size()));
    return *this;
  }
  parse_options &&version_head(std::string_view key, std::string_view type) && {
    return std::move(version_head(key, type));
  }
  parse_options &version_head(std::string_view key, trackparse::version_type type) & {
    return version_head(key, trackparse::name(type));
  }
  parse_options &&version_head(std::string_view key, trackparse::version_type type) && {
    return std::move(version_head(key, type));
  }

  parse_options &descriptor(std::string_view s) & {
    detail::check(trackparse_parse_options_add_descriptor(o_, s.data(), s.size()));
    return *this;
  }
  parse_options &&descriptor(std::string_view s) && { return std::move(descriptor(s)); }

  parse_options &genre(std::string_view s) & {
    detail::check(trackparse_parse_options_add_genre(o_, s.data(), s.size()));
    return *this;
  }
  parse_options &&genre(std::string_view s) && { return std::move(genre(s)); }

  /** keywords.junk: `phrase` is junk of `kind` (a spec name; new names allowed). */
  parse_options &junk(std::string_view phrase, std::string_view kind) & {
    detail::check(trackparse_parse_options_add_junk(o_, phrase.data(), phrase.size(), kind.data(),
                                                    kind.size()));
    return *this;
  }
  parse_options &&junk(std::string_view phrase, std::string_view kind) && {
    return std::move(junk(phrase, kind));
  }
  parse_options &junk(std::string_view phrase, trackparse::junk_kind kind) & {
    return junk(phrase, trackparse::name(kind));
  }
  parse_options &&junk(std::string_view phrase, trackparse::junk_kind kind) && {
    return std::move(junk(phrase, kind));
  }

  parse_options &feat_marker(std::string_view s) & {
    detail::check(trackparse_parse_options_add_feat_marker(o_, s.data(), s.size()));
    return *this;
  }
  parse_options &&feat_marker(std::string_view s) && { return std::move(feat_marker(s)); }

  const trackparse_parse_options *c_ptr() const { return o_; }

private:
  trackparse_parse_options *o_;
};

/** Options for format() (R11). */
class format_options {
public:
  format_options() : o_(detail::check(trackparse_format_options_new())) {}
  format_options(format_options &&o) noexcept : o_(o.o_) { o.o_ = nullptr; }
  format_options &operator=(format_options &&o) noexcept {
    if (this != &o) {
      trackparse_format_options_free(o_);
      o_ = o.o_;
      o.o_ = nullptr;
    }
    return *this;
  }
  format_options(const format_options &) = delete;
  format_options &operator=(const format_options &) = delete;
  ~format_options() { trackparse_format_options_free(o_); }

  format_options &feat(trackparse::feat v) & {
    detail::check(trackparse_format_options_set_feat(o_, static_cast<trackparse_feat>(v)));
    return *this;
  }
  format_options &&feat(trackparse::feat v) && { return std::move(feat(v)); }

  /** The marker written before featured artists ("" is a valid marker). */
  format_options &feat_marker(std::string_view s) & {
    detail::check(trackparse_format_options_set_feat_marker(o_, s.data(), s.size()));
    return *this;
  }
  format_options &&feat_marker(std::string_view s) && { return std::move(feat_marker(s)); }

  format_options &joiners(trackparse::joiners v) & {
    detail::check(trackparse_format_options_set_joiners(o_, static_cast<trackparse_joiners>(v)));
    return *this;
  }
  format_options &&joiners(trackparse::joiners v) && { return std::move(joiners(v)); }

  format_options &versions(trackparse::versions v) & {
    detail::check(trackparse_format_options_set_versions(o_, static_cast<trackparse_versions>(v)));
    return *this;
  }
  format_options &&versions(trackparse::versions v) && { return std::move(versions(v)); }

  format_options &omit_producers(bool v) & {
    detail::check(trackparse_format_options_set_omit_producers(o_, v));
    return *this;
  }
  format_options &&omit_producers(bool v) && { return std::move(omit_producers(v)); }

  format_options &position(bool v) & {
    detail::check(trackparse_format_options_set_position(o_, v));
    return *this;
  }
  format_options &&position(bool v) && { return std::move(position(v)); }

  const trackparse_format_options *c_ptr() const { return o_; }

private:
  trackparse_format_options *o_;
};

inline std::string track_view::format(const format_options &o) const {
  return detail::take(trackparse_format_with(t_, o.c_ptr()));
}

/** Options for parse_tracklist() (R13.10). */
class tracklist_options {
public:
  tracklist_options() : o_(detail::check(trackparse_tracklist_options_new())) {}
  tracklist_options(tracklist_options &&o) noexcept : o_(o.o_) { o.o_ = nullptr; }
  tracklist_options &operator=(tracklist_options &&o) noexcept {
    if (this != &o) {
      trackparse_tracklist_options_free(o_);
      o_ = o.o_;
      o.o_ = nullptr;
    }
    return *this;
  }
  tracklist_options(const tracklist_options &) = delete;
  tracklist_options &operator=(const tracklist_options &) = delete;
  ~tracklist_options() { trackparse_tracklist_options_free(o_); }

  /** Default for tracklists: mode::tracklist. */
  tracklist_options &mode(trackparse::mode v) & {
    detail::check(trackparse_tracklist_options_set_mode(o_, static_cast<trackparse_mode>(v)));
    return *this;
  }
  tracklist_options &&mode(trackparse::mode v) && { return std::move(mode(v)); }

  tracklist_options &known_artist(std::string_view s) & {
    detail::check(trackparse_tracklist_options_add_known_artist(o_, s.data(), s.size()));
    return *this;
  }
  tracklist_options &&known_artist(std::string_view s) && { return std::move(known_artist(s)); }

  template <class Names> tracklist_options &known_artists(const Names &names) & {
    for (const auto &n : names) known_artist(std::string_view(n));
    return *this;
  }
  template <class Names> tracklist_options &&known_artists(const Names &names) && {
    return std::move(known_artists(names));
  }
  tracklist_options &known_artists(std::initializer_list<std::string_view> names) & {
    for (auto n : names) known_artist(n);
    return *this;
  }
  tracklist_options &&known_artists(std::initializer_list<std::string_view> names) && {
    return std::move(known_artists(names));
  }

  tracklist_options &split_and(trackparse::split_and v) & {
    detail::check(
        trackparse_tracklist_options_set_split_and(o_, static_cast<trackparse_split_and>(v)));
    return *this;
  }
  tracklist_options &&split_and(trackparse::split_and v) && { return std::move(split_and(v)); }

  tracklist_options &version_head(std::string_view key, std::string_view type) & {
    detail::check(trackparse_tracklist_options_add_version_head(o_, key.data(), key.size(),
                                                                type.data(), type.size()));
    return *this;
  }
  tracklist_options &&version_head(std::string_view key, std::string_view type) && {
    return std::move(version_head(key, type));
  }
  tracklist_options &version_head(std::string_view key, trackparse::version_type type) & {
    return version_head(key, trackparse::name(type));
  }
  tracklist_options &&version_head(std::string_view key, trackparse::version_type type) && {
    return std::move(version_head(key, type));
  }

  tracklist_options &descriptor(std::string_view s) & {
    detail::check(trackparse_tracklist_options_add_descriptor(o_, s.data(), s.size()));
    return *this;
  }
  tracklist_options &&descriptor(std::string_view s) && { return std::move(descriptor(s)); }

  tracklist_options &genre(std::string_view s) & {
    detail::check(trackparse_tracklist_options_add_genre(o_, s.data(), s.size()));
    return *this;
  }
  tracklist_options &&genre(std::string_view s) && { return std::move(genre(s)); }

  tracklist_options &junk(std::string_view phrase, std::string_view kind) & {
    detail::check(trackparse_tracklist_options_add_junk(o_, phrase.data(), phrase.size(),
                                                        kind.data(), kind.size()));
    return *this;
  }
  tracklist_options &&junk(std::string_view phrase, std::string_view kind) && {
    return std::move(junk(phrase, kind));
  }
  tracklist_options &junk(std::string_view phrase, trackparse::junk_kind kind) & {
    return junk(phrase, trackparse::name(kind));
  }
  tracklist_options &&junk(std::string_view phrase, trackparse::junk_kind kind) && {
    return std::move(junk(phrase, kind));
  }

  tracklist_options &feat_marker(std::string_view s) & {
    detail::check(trackparse_tracklist_options_add_feat_marker(o_, s.data(), s.size()));
    return *this;
  }
  tracklist_options &&feat_marker(std::string_view s) && { return std::move(feat_marker(s)); }

  /** The audio length in seconds, for time-after-audio-end (0 is a valid length). */
  tracklist_options &audio_seconds(double v) & {
    detail::check(trackparse_tracklist_options_set_audio_seconds(o_, v));
    return *this;
  }
  tracklist_options &&audio_seconds(double v) && { return std::move(audio_seconds(v)); }

  tracklist_options &marker_word(std::string_view s) & {
    detail::check(trackparse_tracklist_options_add_marker_word(o_, s.data(), s.size()));
    return *this;
  }
  tracklist_options &&marker_word(std::string_view s) && { return std::move(marker_word(s)); }

  const trackparse_tracklist_options *c_ptr() const { return o_; }

private:
  trackparse_tracklist_options *o_;
};

/* ------------------------------------------------------------------------------------------ */
/* Single tracks                                                                                */

/** R1-R9: parse one track string. */
inline track parse(std::string_view s) { return track(trackparse_parse(s.data(), s.size())); }
inline track parse(std::string_view s, const parse_options &o) {
  return track(trackparse_parse_with(s.data(), s.size(), o.c_ptr()));
}

/** R10: parse an artist string ("A feat. B & C"). */
inline artists parse_artists(std::string_view s) {
  return artists(trackparse_parse_artists(s.data(), s.size()));
}
inline artists parse_artists(std::string_view s, const parse_options &o) {
  return artists(trackparse_parse_artists_with(s.data(), s.size(), o.c_ptr()));
}

/** R11: write a parsed track back as "Artists - Title (Versions)". */
inline std::string format(const track_view &t) { return t.format(); }
inline std::string format(const track_view &t, const format_options &o) { return t.format(o); }

/** R1: the normalized form of a string. */
inline std::string normalize(std::string_view s) {
  return detail::take(trackparse_normalize(s.data(), s.size()));
}

/** Every artist of a track: the artist side, then each version's, in order. */
inline artists all_artists(const track_view &t) { return t.all_artists(); }

/** A parser that keeps its options and compiled tables, for many calls. Immutable and safe to
 * share between threads. Move-only. */
class parser {
public:
  /** A parser with the default options. */
  parser() : p_(detail::check(trackparse_parser_new(nullptr))) {}
  explicit parser(const parse_options &o) : p_(detail::check(trackparse_parser_new(o.c_ptr()))) {}
  parser(parser &&o) noexcept : p_(o.p_) { o.p_ = nullptr; }
  parser &operator=(parser &&o) noexcept {
    if (this != &o) {
      trackparse_parser_free(p_);
      p_ = o.p_;
      o.p_ = nullptr;
    }
    return *this;
  }
  parser(const parser &) = delete;
  parser &operator=(const parser &) = delete;
  ~parser() { trackparse_parser_free(p_); }

  track parse(std::string_view s) const {
    return track(trackparse_parser_parse(p_, s.data(), s.size()));
  }
  trackparse::artists parse_artists(std::string_view s) const {
    return trackparse::artists(trackparse_parser_parse_artists(p_, s.data(), s.size()));
  }
  const trackparse_parser *c_ptr() const { return p_; }

private:
  trackparse_parser *p_;
};

/* ------------------------------------------------------------------------------------------ */
/* Tracklists (R13)                                                                             */

struct parts {
  std::string_view lead;
  std::string_view time;
  std::string_view gap;
  std::string_view rest;
};

struct tracklist_time {
  std::string_view raw;
  double seconds;
};

struct suggestion {
  int line;
  std::string_view raw;
  double seconds;
};

class entry_view {
public:
  explicit entry_view(const trackparse_entry *e) : e_(e) {}
  int line() const { return e_->line; }
  entry_kind kind() const { return static_cast<entry_kind>(e_->kind); }
  trackparse::eol eol() const { return static_cast<trackparse::eol>(e_->eol); }
  std::string_view raw() const { return detail::sv(e_->raw); }
  trackparse::parts parts() const {
    return trackparse::parts{detail::sv(e_->parts.lead), detail::sv(e_->parts.time),
                             detail::sv(e_->parts.gap), detail::sv(e_->parts.rest)};
  }
  std::optional<tracklist_time> time() const {
    if (e_->time == nullptr) return std::nullopt;
    return tracklist_time{detail::sv(e_->time->raw), e_->time->seconds};
  }
  std::optional<track_view> track() const {
    if (e_->track == nullptr) return std::nullopt;
    return track_view(e_->track);
  }
  bool layered() const { return e_->layered; }
  const trackparse_entry *c_ptr() const { return e_; }

private:
  const trackparse_entry *e_;
};

class tracklist_warning_view {
public:
  explicit tracklist_warning_view(const trackparse_tracklist_warning *w) : w_(w) {}
  tracklist_warning_code code() const { return static_cast<tracklist_warning_code>(w_->code); }
  int line() const { return w_->line; }
  trackparse::severity severity() const { return static_cast<trackparse::severity>(w_->severity); }
  std::optional<trackparse::suggestion> suggestion() const {
    if (w_->suggestion == nullptr) return std::nullopt;
    return trackparse::suggestion{w_->suggestion->line, detail::sv(w_->suggestion->raw),
                                  w_->suggestion->seconds};
  }
  const trackparse_tracklist_warning *c_ptr() const { return w_; }

private:
  const trackparse_tracklist_warning *w_;
};

struct stats {
  int lines;
  int tracks;
  int markers;
  int timed;
  std::optional<double> first_time;
  std::optional<double> last_time;
};

using entry_span = span<trackparse_entry, entry_view>;
using tracklist_warning_span = span<trackparse_tracklist_warning, tracklist_warning_view>;

/** The result of parse_tracklist(). Move-only. */
class tracklist {
public:
  explicit tracklist(trackparse_tracklist *t) : t_(detail::check(t)) {}
  tracklist(tracklist &&o) noexcept : t_(o.t_) { o.t_ = nullptr; }
  tracklist &operator=(tracklist &&o) noexcept {
    if (this != &o) {
      trackparse_tracklist_free(t_);
      t_ = o.t_;
      o.t_ = nullptr;
    }
    return *this;
  }
  tracklist(const tracklist &) = delete;
  tracklist &operator=(const tracklist &) = delete;
  ~tracklist() { trackparse_tracklist_free(t_); }

  std::string_view spec_version() const { return detail::sv(t_->spec_version); }
  entry_span entries() const { return entry_span(t_->entries, t_->entries_count); }
  tracklist_warning_span warnings() const {
    return tracklist_warning_span(t_->warnings, t_->warnings_count);
  }
  trackparse::stats stats() const {
    const trackparse_stats &s = t_->stats;
    return trackparse::stats{
        s.lines, s.tracks, s.markers, s.timed, detail::opt(s.first_time), detail::opt(s.last_time)};
  }
  std::string to_json() const { return detail::take(trackparse_tracklist_to_json(t_)); }
  const trackparse_tracklist *c_ptr() const { return t_; }

private:
  trackparse_tracklist *t_;
};

inline tracklist parse_tracklist(std::string_view s) {
  return tracklist(trackparse_parse_tracklist(s.data(), s.size()));
}
inline tracklist parse_tracklist(std::string_view s, const tracklist_options &o) {
  return tracklist(trackparse_parse_tracklist_with(s.data(), s.size(), o.c_ptr()));
}

/** A list of R13.9 edits, applied in order. Lines are 1-based. Move-only. */
class edits {
public:
  edits() : e_(detail::check(trackparse_edits_new())) {}
  edits(edits &&o) noexcept : e_(o.e_) { o.e_ = nullptr; }
  edits &operator=(edits &&o) noexcept {
    if (this != &o) {
      trackparse_edits_free(e_);
      e_ = o.e_;
      o.e_ = nullptr;
    }
    return *this;
  }
  edits(const edits &) = delete;
  edits &operator=(const edits &) = delete;
  ~edits() { trackparse_edits_free(e_); }

  edits &set_time(long long line, double seconds) & {
    detail::check(trackparse_edits_set_time(e_, line, seconds));
    return *this;
  }
  edits &&set_time(long long line, double seconds) && { return std::move(set_time(line, seconds)); }

  /** Shift every timed line. */
  edits &shift(double seconds) & {
    detail::check(trackparse_edits_shift(e_, seconds));
    return *this;
  }
  edits &&shift(double seconds) && { return std::move(shift(seconds)); }

  /** Shift the timed lines from `from` on. */
  edits &shift_from(double seconds, long long from) & {
    detail::check(trackparse_edits_shift_from(e_, seconds, from));
    return *this;
  }
  edits &&shift_from(double seconds, long long from) && {
    return std::move(shift_from(seconds, from));
  }

  /** Shift the listed lines. */
  edits &shift_lines(double seconds, const long long *lines, std::size_t count) & {
    detail::check(trackparse_edits_shift_lines(e_, seconds, lines, count));
    return *this;
  }
  edits &&shift_lines(double seconds, const long long *lines, std::size_t count) && {
    return std::move(shift_lines(seconds, lines, count));
  }
  edits &shift_lines(double seconds, std::initializer_list<long long> lines) & {
    return shift_lines(seconds, lines.begin(), lines.size());
  }
  edits &&shift_lines(double seconds, std::initializer_list<long long> lines) && {
    return std::move(shift_lines(seconds, lines));
  }

  edits &sort_by_time() & {
    detail::check(trackparse_edits_sort_by_time(e_));
    return *this;
  }
  edits &&sort_by_time() && { return std::move(sort_by_time()); }

  edits &set_rest(long long line, std::string_view rest) & {
    detail::check(trackparse_edits_set_rest(e_, line, rest.data(), rest.size()));
    return *this;
  }
  edits &&set_rest(long long line, std::string_view rest) && {
    return std::move(set_rest(line, rest));
  }

  /** Insert `text` as a new line after `after` (0: at the top). */
  edits &insert(long long after, std::string_view text) & {
    detail::check(trackparse_edits_insert(e_, after, text.data(), text.size()));
    return *this;
  }
  edits &&insert(long long after, std::string_view text) && {
    return std::move(insert(after, text));
  }

  /** The delete edit ("delete" is a C++ keyword). */
  edits &delete_line(long long line) & {
    detail::check(trackparse_edits_delete(e_, line));
    return *this;
  }
  edits &&delete_line(long long line) && { return std::move(delete_line(line)); }

  const trackparse_edits *c_ptr() const { return e_; }

private:
  trackparse_edits *e_;
};

class edit_warning_view {
public:
  explicit edit_warning_view(const trackparse_edit_warning *w) : w_(w) {}
  edit_warning_code code() const { return static_cast<edit_warning_code>(w_->code); }
  /** The index of the edit in the list (0-based). */
  std::size_t edit() const { return w_->edit; }
  std::optional<long long> line() const { return detail::opt(w_->line); }
  const trackparse_edit_warning *c_ptr() const { return w_; }

private:
  const trackparse_edit_warning *w_;
};

using line_span = span<long long, long long, detail::same_conv<long long>>;
using edit_warning_span = span<trackparse_edit_warning, edit_warning_view>;

/** The result of apply_edits(). Move-only. */
class edit_result {
public:
  explicit edit_result(trackparse_edit_result *r) : r_(detail::check(r)) {}
  edit_result(edit_result &&o) noexcept : r_(o.r_) { o.r_ = nullptr; }
  edit_result &operator=(edit_result &&o) noexcept {
    if (this != &o) {
      trackparse_edit_result_free(r_);
      r_ = o.r_;
      o.r_ = nullptr;
    }
    return *this;
  }
  edit_result(const edit_result &) = delete;
  edit_result &operator=(const edit_result &) = delete;
  ~edit_result() { trackparse_edit_result_free(r_); }

  std::string_view text() const { return detail::sv(r_->text); }
  /** The lines that changed, in the new text. */
  line_span changed() const { return line_span(r_->changed, r_->changed_count); }
  edit_warning_span warnings() const { return edit_warning_span(r_->warnings, r_->warnings_count); }
  std::string to_json() const { return detail::take(trackparse_edit_result_to_json(r_)); }
  const trackparse_edit_result *c_ptr() const { return r_; }

private:
  trackparse_edit_result *r_;
};

/** R13.9: apply edits to a tracklist text. Lines not named stay byte for byte. */
inline edit_result apply_edits(std::string_view s, const edits &e) {
  return edit_result(trackparse_apply_edits(s.data(), s.size(), e.c_ptr()));
}

} // namespace trackparse

#endif /* TRACKPARSE_HPP */
