// test_cpp.cpp: the C++ wrapper (include/trackparse.hpp). Built in C++17 and C++20 against the
// installed library target (cmake/package.cmake). Every value is checked against the C API or
// against output the other ports give for the same input.
#include <trackparse.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace tp = trackparse;

static int failures = 0;

#define CHECK(cond)                                                                                \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);                \
      failures++;                                                                                  \
    }                                                                                              \
  } while (0)

#define CHECK_EQ(a, b)                                                                             \
  do {                                                                                             \
    if (!((a) == (b))) {                                                                           \
      std::fprintf(stderr, "%s:%d: CHECK_EQ(%s, %s) failed\n", __FILE__, __LINE__, #a, #b);        \
      failures++;                                                                                  \
    }                                                                                              \
  } while (0)

// The C JSON of a result, as a std::string, for comparisons with the wrapper's.
static std::string c_json(trackparse_string *s) {
  std::string r(s->str.ptr, s->str.len);
  trackparse_string_free(s);
  return r;
}

// The C JSON of a C track, which it then frees.
static std::string c_track_json(trackparse_track *t) {
  std::string r = c_json(trackparse_track_to_json(t));
  trackparse_track_free(t);
  return r;
}

// Owners are move-only with noexcept moves; views and spans are trivially copyable.
static_assert(!std::is_copy_constructible_v<tp::track>);
static_assert(!std::is_copy_assignable_v<tp::track>);
static_assert(std::is_nothrow_move_constructible_v<tp::track>);
static_assert(std::is_nothrow_move_assignable_v<tp::track>);
static_assert(!std::is_copy_constructible_v<tp::artists>);
static_assert(!std::is_copy_constructible_v<tp::tracklist>);
static_assert(!std::is_copy_constructible_v<tp::edit_result>);
static_assert(!std::is_copy_constructible_v<tp::parser>);
static_assert(!std::is_copy_constructible_v<tp::parse_options>);
static_assert(!std::is_copy_constructible_v<tp::format_options>);
static_assert(!std::is_copy_constructible_v<tp::tracklist_options>);
static_assert(!std::is_copy_constructible_v<tp::edits>);
static_assert(std::is_nothrow_move_constructible_v<tp::parse_options>);
static_assert(std::is_nothrow_move_constructible_v<tp::edits>);
static_assert(std::is_trivially_copyable_v<tp::track_view>);
static_assert(std::is_trivially_copyable_v<tp::artist_view>);
static_assert(std::is_trivially_copyable_v<tp::artist_span>);
static_assert(std::is_trivially_copyable_v<tp::entry_view>);
// The enums carry the C values.
static_assert(static_cast<int>(tp::mode::youtube) == TRACKPARSE_MODE_YOUTUBE);
static_assert(static_cast<int>(tp::version_type::nightcore) == TRACKPARSE_VERSION_NIGHTCORE);
static_assert(static_cast<int>(tp::tracklist_warning_code::line_unparsed) ==
              TRACKPARSE_TL_LINE_UNPARSED);
// Setters chain on temporaries (rvalue overloads) and on named builders (lvalue overloads).
static_assert(
    std::is_same_v<decltype(tp::parse_options{}.mode(tp::mode::clean)), tp::parse_options &&>);
static_assert(std::is_same_v<decltype(std::declval<tp::parse_options &>().mode(tp::mode::clean)),
                             tp::parse_options &>);
static_assert(std::is_same_v<decltype(tp::edits{}.shift(1)), tp::edits &&>);

static void test_version() {
  CHECK_EQ(tp::header_version, std::string_view(TRACKPARSE_VERSION));
  CHECK_EQ(tp::library_version(), std::string_view(trackparse_library_version()));
  CHECK_EQ(tp::spec_version(), std::string_view(trackparse_spec_version()));
}

static void test_parse() {
  auto t = tp::parse("Noisia - Stigma (2015)");
  CHECK_EQ(t.title(), "Stigma");
  CHECK_EQ(t.full_title(), "Stigma");
  CHECK_EQ(t.input(), "Noisia - Stigma (2015)");
  CHECK(t.year().has_value());
  CHECK_EQ(t.year().value_or(0), 2015);
  CHECK_EQ(t.artists().size(), 1u);
  CHECK_EQ(t.artists()[0].name(), "Noisia");
  CHECK(t.artists()[0].role() == tp::role::primary);
  CHECK(t.artists()[0].source() == tp::source::artist);
  CHECK(!t.artists()[0].joiner());
  CHECK(!t.position());
  CHECK(!t.timestamp());
  CHECK(t.versions().empty());
  CHECK(t.junk().empty());
  CHECK(t.warnings().empty());
  CHECK(!t.flags().explicit_());
  CHECK_EQ(t.to_json(), c_json(trackparse_track_to_json(t.c_ptr())));

  auto u =
      tp::parse("Chase & Status feat. Plan B - End Credits (Sub Focus Remix) [Official Video]");
  std::vector<std::string> names;
  for (const auto &a : u.artists()) names.emplace_back(a.name());
  CHECK((names == std::vector<std::string>{"Chase", "Status", "Plan B"}));
  CHECK(u.artists()[1].joiner().value_or("") == "&");
  CHECK(u.artists()[2].role() == tp::role::featured);
  CHECK_EQ(u.versions().size(), 1u);
  auto v = u.versions().front();
  CHECK(v.type() == tp::version_type::remix);
  CHECK_EQ(v.type_name(), "remix");
  CHECK_EQ(v.raw(), "Sub Focus Remix");
  CHECK(v.delimiter() == tp::delimiter::paren);
  CHECK_EQ(v.artists().size(), 1u);
  CHECK_EQ(v.artists()[0].name(), "Sub Focus");
  CHECK(v.artists()[0].role() == tp::role::remixer);
  CHECK(v.artists()[0].source() == tp::source::version);
  CHECK(!v.year());
  CHECK_EQ(u.junk().size(), 1u);
  CHECK_EQ(u.junk()[0].raw(), "Official Video");
  CHECK(u.junk()[0].kind() == tp::junk_kind::video);
  CHECK_EQ(u.full_title(), "End Credits (Sub Focus Remix)");

  // Positions, timestamps and an explicit length with U+0000 in it.
  auto p = tp::parse("03. Noisia - Stigma");
  CHECK(p.position().has_value());
  CHECK_EQ(p.position()->number, 3);
  CHECK_EQ(p.position()->raw, "03");
  const char with_nul[] = "Noisia - Sti\0gma";
  auto n = tp::parse(std::string_view(with_nul, sizeof with_nul - 1));
  CHECK_EQ(n.input().size(), sizeof with_nul - 1);
  CHECK_EQ(n.to_json(), c_track_json(trackparse_parse(with_nul, sizeof with_nul - 1)));

  // Warnings are enum values with spec names.
  auto w = tp::parse("Just A Title");
  CHECK_EQ(w.warnings().size(), 1u);
  CHECK(w.warnings()[0] == tp::warning::no_separator);
  CHECK_EQ(tp::name(w.warnings()[0]), "noSeparator");
  CHECK(w.artists().empty());
}

static void test_moves() {
  auto t = tp::parse("Noisia - Stigma");
  tp::track_view view = t; // borrows
  tp::track moved = std::move(t);
  CHECK_EQ(moved.title(), "Stigma");
  CHECK_EQ(view.title(), "Stigma");
  moved = tp::parse("Mefjus - Sinkhole");
  CHECK_EQ(moved.title(), "Sinkhole");
  trackparse_track *raw = moved.release();
  CHECK(raw != nullptr);
  CHECK_EQ(std::string_view(raw->title.ptr, raw->title.len), "Sinkhole");
  trackparse_track_free(raw);
  tp::track adopted(trackparse_parse("Noisia - Stigma", TRACKPARSE_NUL_TERMINATED));
  CHECK_EQ(adopted.title(), "Stigma");

  tp::parse_options o;
  o.mode(tp::mode::clean);
  tp::parse_options o2 = std::move(o);
  o2.known_artist("Chase & Status");
  CHECK_EQ(tp::parse("Chase & Status - Baddadan", o2).artists().size(), 1u);
}

static void test_options_and_parser() {
  auto yt = tp::parse("Stigma (Official Video) HD",
                      tp::parse_options{}.mode(tp::mode::youtube).uploader("Noisia"));
  CHECK(yt.mode() == tp::mode::youtube);
  CHECK_EQ(yt.title(), "Stigma");
  CHECK_EQ(yt.artists().size(), 1u);
  CHECK_EQ(yt.artists()[0].name(), "Noisia");

  // known_artists from an initializer list and from a container.
  std::vector<std::string> known{"Chase & Status", "Camo & Krooked"};
  tp::parser p(tp::parse_options{}.known_artists(known));
  CHECK_EQ(p.parse("Chase & Status - Baddadan").artists().size(), 1u);
  CHECK_EQ(p.parse("Camo & Krooked - Ember").artists().size(), 1u);
  CHECK_EQ(p.parse_artists("Camo & Krooked & Chase & Status").size(), 2u);
  tp::parser q(tp::parse_options{}.known_artists({"Chase & Status"}));
  CHECK_EQ(q.parse("Chase & Status - Baddadan").artists()[0].name(), "Chase & Status");
  tp::parser defaults;
  CHECK_EQ(defaults.parse("Chase & Status - Baddadan").artists().size(), 2u);

  // The same options through the C API give the same bytes.
  auto opts = tp::parse_options{}
                  .split_and(tp::split_and::never)
                  .version_head("rmx", tp::version_type::remix)
                  .version_head("fix", "refix")
                  .descriptor("rough")
                  .genre("neurofunk")
                  .junk("free dl", tp::junk_kind::promo)
                  .junk("my kind", "custom")
                  .feat_marker("with");
  const char *input = "Noisia with Foreign Beggars - Stigma (Mefjus Rmx) [Neurofunk] FREE DL";
  trackparse_parse_options *co = trackparse_parse_options_new();
  trackparse_parse_options_set_split_and(co, TRACKPARSE_SPLIT_AND_NEVER);
  trackparse_parse_options_add_version_head(co, "rmx", 3, "remix", 5);
  trackparse_parse_options_add_version_head(co, "fix", 3, "refix", 5);
  trackparse_parse_options_add_descriptor(co, "rough", 5);
  trackparse_parse_options_add_genre(co, "neurofunk", 9);
  trackparse_parse_options_add_junk(co, "free dl", 7, "promo", 5);
  trackparse_parse_options_add_junk(co, "my kind", 7, "custom", 6);
  trackparse_parse_options_add_feat_marker(co, "with", 4);
  auto expected = c_track_json(trackparse_parse_with(input, TRACKPARSE_NUL_TERMINATED, co));
  trackparse_parse_options_free(co);
  auto got = tp::parse(input, opts);
  CHECK_EQ(got.to_json(), expected);
  CHECK_EQ(got.versions().size(), 1u);
  CHECK(got.versions()[0].type() == tp::version_type::remix);
  CHECK_EQ(got.artists().size(), 2u);
  CHECK(got.artists()[1].role() == tp::role::featured);
  CHECK_EQ(got.junk().size(), 2u);
}

static void test_artists_format_normalize() {
  auto a = tp::parse_artists("Camo & Krooked feat. Ayah Marar");
  CHECK_EQ(a.size(), 3u);
  CHECK(!a.empty());
  std::vector<std::string> names;
  for (const auto &x : a) names.emplace_back(x.name());
  CHECK((names == std::vector<std::string>{"Camo", "Krooked", "Ayah Marar"}));
  CHECK(a[2].role() == tp::role::featured);
  CHECK_EQ(a.to_json(), c_json(trackparse_artists_to_json(a.c_ptr())));
  auto a2 =
      tp::parse_artists("Camo and Krooked", tp::parse_options{}.split_and(tp::split_and::never));
  CHECK_EQ(a2.size(), 1u);

  auto t = tp::parse("Sub Focus feat. Kele - Turn It Around (VIP)");
  CHECK_EQ(tp::format(t), "Sub Focus feat. Kele - Turn It Around (VIP)");
  CHECK_EQ(t.format(), tp::format(t));
  CHECK_EQ(tp::format(t, tp::format_options{}.feat(tp::feat::title).feat_marker("ft.")),
           "Sub Focus - Turn It Around (ft. Kele) (VIP)");
  CHECK_EQ(tp::format(t, tp::format_options{}.versions(tp::versions::none).feat(tp::feat::omit)),
           "Sub Focus - Turn It Around");
  auto fo =
      tp::format_options{}.joiners(tp::joiners::canonical).omit_producers(true).position(false);
  CHECK_EQ(tp::format(t, fo), c_json(trackparse_format_with(t.c_ptr(), fo.c_ptr())));

  auto all = tp::all_artists(tp::parse("Noisia - Stigma (Mefjus Remix)"));
  CHECK_EQ(all.size(), 2u);
  CHECK_EQ(all[1].name(), "Mefjus");
  CHECK(all[1].role() == tp::role::remixer);

  CHECK_EQ(tp::normalize("Noisia  \xe2\x80\x93  Stigma"),
           c_json(trackparse_normalize("Noisia  \xe2\x80\x93  Stigma", TRACKPARSE_NUL_TERMINATED)));
  // Invalid UTF-8 becomes U+FFFD.
  CHECK_EQ(tp::normalize("a\xff"), "a\xef\xbf\xbd");
}

static void test_tracklist() {
  const char *text = "Tracklist:\n"
                     "00:00 Noisia - Stigma\n"
                     "03:10 Phace & Misanthrop - Desert Orgy\n"
                     "02:45 Mefjus - Sinkhole (VIP)\r\n"
                     "09:30 June Miller - Drop That";
  auto list = tp::parse_tracklist(text, tp::tracklist_options{}.audio_seconds(540));
  CHECK_EQ(list.spec_version(), tp::spec_version());
  CHECK_EQ(list.entries().size(), 5u);
  auto first = list.entries()[0];
  CHECK_EQ(first.line(), 1);
  CHECK(first.kind() != tp::entry_kind::track);
  CHECK(!first.time());
  auto e = list.entries()[2];
  CHECK(e.kind() == tp::entry_kind::track);
  CHECK(e.eol() == tp::eol::lf);
  CHECK_EQ(e.raw(), "03:10 Phace & Misanthrop - Desert Orgy");
  CHECK_EQ(e.parts().time, "03:10");
  CHECK_EQ(e.parts().rest, "Phace & Misanthrop - Desert Orgy");
  CHECK(e.time().has_value());
  CHECK_EQ(e.time()->seconds, 190.0);
  CHECK(e.track().has_value());
  CHECK_EQ(e.track()->title(), "Desert Orgy");
  CHECK_EQ(e.track()->artists().size(), 2u);
  CHECK(list.entries()[3].eol() == tp::eol::crlf);
  CHECK(list.entries()[4].eol() == tp::eol::none);

  bool out_of_order = false, after_end = false;
  for (const auto &w : list.warnings()) {
    if (w.code() == tp::tracklist_warning_code::time_out_of_order) {
      out_of_order = true;
      CHECK_EQ(w.line(), 4);
    }
    if (w.code() == tp::tracklist_warning_code::time_after_audio_end) after_end = true;
    CHECK(!tp::name(w.code()).empty());
    CHECK(!tp::name(w.severity()).empty());
  }
  CHECK(out_of_order);
  CHECK(after_end);
  auto s = list.stats();
  CHECK_EQ(s.lines, 5);
  CHECK_EQ(s.tracks, 4);
  CHECK_EQ(s.timed, 4);
  CHECK_EQ(s.first_time.value_or(-1), 0.0);
  CHECK_EQ(s.last_time.value_or(-1), 570.0);
  CHECK_EQ(list.to_json(), c_json(trackparse_tracklist_to_json(list.c_ptr())));

  // Default options, and every tracklist option through the builder.
  auto plain = tp::parse_tracklist(text);
  CHECK_EQ(plain.entries().size(), 5u);
  auto opts = tp::tracklist_options{}
                  .mode(tp::mode::tracklist)
                  .known_artist("Phace & Misanthrop")
                  .known_artists({"June Miller"})
                  .split_and(tp::split_and::auto_)
                  .version_head("rmx", tp::version_type::remix)
                  .descriptor("rough")
                  .genre("neurofunk")
                  .junk("free dl", tp::junk_kind::promo)
                  .feat_marker("with")
                  .marker_word("Intro")
                  .audio_seconds(0);
  auto known = tp::parse_tracklist(text, opts);
  CHECK_EQ(known.entries()[2].track()->artists().size(), 1u);
}

static void test_edits() {
  const char *text = "0:00 Noisia - Stigma\n4:20 Mefjus - Sinkhole\n";
  auto r = tp::apply_edits(text, tp::edits{}.shift(30).set_rest(2, "Mefjus - Pivot"));
  CHECK_EQ(r.text(), "0:30 Noisia - Stigma\n4:50 Mefjus - Pivot\n");
  std::vector<long long> changed(r.changed().begin(), r.changed().end());
  CHECK((changed == std::vector<long long>{1, 2}));
  CHECK(r.warnings().empty());
  CHECK_EQ(r.to_json(), c_json(trackparse_edit_result_to_json(r.c_ptr())));

  tp::edits e;
  e.set_time(2, 61).insert(0, "Tracklist:").delete_line(1).sort_by_time();
  e.shift_from(1, 2).shift_lines(1, {1, 2});
  long long lines[] = {2};
  e.shift_lines(-1, lines, 1);
  auto r2 = tp::apply_edits(text, e);
  trackparse_edits *ce = trackparse_edits_new();
  trackparse_edits_set_time(ce, 2, 61);
  trackparse_edits_insert(ce, 0, "Tracklist:", 10);
  trackparse_edits_delete(ce, 1);
  trackparse_edits_sort_by_time(ce);
  trackparse_edits_shift_from(ce, 1, 2);
  long long both[] = {1, 2};
  trackparse_edits_shift_lines(ce, 1, both, 2);
  trackparse_edits_shift_lines(ce, -1, lines, 1);
  trackparse_edit_result *cr = trackparse_apply_edits(text, TRACKPARSE_NUL_TERMINATED, ce);
  CHECK_EQ(r2.to_json(), c_json(trackparse_edit_result_to_json(cr)));
  trackparse_edit_result_free(cr);
  trackparse_edits_free(ce);

  // An edit that names a missing line warns.
  auto bad = tp::apply_edits(text, tp::edits{}.set_time(99, 1));
  CHECK_EQ(bad.text(), text);
  CHECK_EQ(bad.warnings().size(), 1u);
  CHECK(bad.warnings()[0].code() == tp::edit_warning_code::edit_invalid);
  CHECK_EQ(bad.warnings()[0].edit(), 0u);
  CHECK_EQ(tp::name(bad.warnings()[0].code()), "edit-invalid");
}

static void test_names() {
  CHECK_EQ(tp::name(tp::mode::auto_), "auto");
  CHECK_EQ(tp::name(tp::version_type::sped_up), "spedUp");
  CHECK_EQ(tp::name(tp::eol::crlf), "\r\n");
  CHECK_EQ(tp::name(tp::tracklist_warning_code::time_out_of_order), "time-out-of-order");
  CHECK_EQ(tp::name(tp::entry_kind::marker), "marker");
  CHECK_EQ(tp::name(tp::delimiter::bracket), "[");
  CHECK_EQ(tp::name(tp::source::title), "title");
  CHECK_EQ(tp::name(tp::severity::info), "info");
  CHECK(tp::mode_from_name("youtube") == tp::mode::youtube);
  CHECK(!tp::mode_from_name("nope"));
  CHECK(tp::version_type_from_name("vip") == tp::version_type::vip);
  CHECK(tp::junk_kind_from_name("genre") == tp::junk_kind::genre);
  CHECK(!tp::junk_kind_from_name(""));
}

#if __cplusplus >= 202002L && defined(__has_include)
#if __has_include(<ranges>)
#include <ranges>
#endif
#endif
#if defined(__cpp_lib_ranges)
// The spans work with the C++20 range algorithms that take input ranges.
static_assert(std::ranges::input_range<tp::artist_span>);
static_assert(std::ranges::sized_range<tp::artist_span>);
static void test_cpp20() {
  auto t = tp::parse("Chase & Status - Baddadan");
  auto count =
      std::ranges::count_if(t.artists(), [](tp::artist_view a) { return a.name() == "Status"; });
  CHECK_EQ(count, 1);
}
#else
static void test_cpp20() {}
#endif

int main() {
  test_version();
  test_parse();
  test_moves();
  test_options_and_parser();
  test_artists_format_normalize();
  test_tracklist();
  test_edits();
  test_names();
  test_cpp20();
  if (failures) {
    std::fprintf(stderr, "test_cpp: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("test_cpp: ok (__cplusplus %ld)\n", static_cast<long>(__cplusplus));
  return 0;
}
