// cpp.cpp: the C++17 wrapper (trackparse.hpp).
#include <iostream>

#include <trackparse.hpp>

int main() {
  auto t = trackparse::parse("Noisia - Stigma (2015)");  // owns the C result
  std::cout << t.title() << '\n';                        // std::string_view
  if (auto y = t.year()) std::cout << *y << '\n';        // std::optional<int>
  for (const auto &a : t.artists()) {
    std::cout << a.name() << " (" << trackparse::name(a.role()) << ")\n";
  }

  // Options chain; a parser keeps them for many calls.
  trackparse::parser p(trackparse::parse_options{}.known_artist("Chase & Status"));
  auto t2 = p.parse("Chase & Status x Bou - Baddadan (Extended Mix)");
  for (const auto &a : t2.artists()) std::cout << a.name() << '\n';
  for (const auto &v : t2.versions()) std::cout << v.raw() << ": " << v.type_name() << '\n';
  auto no_versions = trackparse::format_options{}.versions(trackparse::versions::none);
  std::cout << trackparse::format(t2, no_versions) << '\n';

  // Tracklists and edits.
  const char *text = "0:00 Noisia - Stigma\n4:20 Mefjus - Sinkhole\n";
  auto list = trackparse::parse_tracklist(text, trackparse::tracklist_options{}.audio_seconds(600));
  for (const auto &e : list.entries()) {
    if (auto track = e.track()) std::cout << e.line() << ": " << track->title() << '\n';
  }
  auto r =
      trackparse::apply_edits(text, trackparse::edits{}.shift(30).set_rest(2, "Mefjus - Pivot"));
  std::cout << r.text();
  std::cout << trackparse::parse_artists("Camo & Krooked feat. Ayah Marar").to_json() << '\n';
  return 0;
}
