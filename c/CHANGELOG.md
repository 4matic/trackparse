# Changelog

## [0.8.0](https://github.com/4matic/trackparse/compare/c-v0.7.1...c-v0.8.0) (2026-11-15)


### Features

* **c:** R8.7 bullet suffix: trackparse_track.label, TRACKPARSE_JUNK_NOTE, trackparse_format_options_set_omit_label; C++ label(), omit_label() ([0104542](https://github.com/4matic/trackparse/commit/01045422e3de29d7d1bd2a9cbfb9ad9ab0203314))


### Documentation

* C install links and notes point at 0.7.1, the first C release with files ([931c499](https://github.com/4matic/trackparse/commit/931c499ef11d56622f405cabf8e2d1f6fd1f60a3))
* the label field, junk kind note and the format label option in every README; fixture counts ([a6f19dd](https://github.com/4matic/trackparse/commit/a6f19dd4583784288f3ca126be05f5617e4e0637))

## [0.7.1](https://github.com/4matic/trackparse/compare/c-v0.7.0...c-v0.7.1) (2026-11-15)


### Bug Fixes

* **c:** the json example's expected output takes the version from c/VERSION, so the release bump no longer breaks example_json and the consumer checks ([0e0e297](https://github.com/4matic/trackparse/commit/0e0e2975cfb697010ca2dc09c075ed89fd34a103))

## [0.7.0](https://github.com/4matic/trackparse/compare/c-v0.6.1...c-v0.7.0) (2026-11-15)


### Features

* **c:** header-only C++17 wrapper, trackparse.hpp ([9a5f8b9](https://github.com/4matic/trackparse/commit/9a5f8b9aef0cee0eacd7fb36207eca6afee049c9))
* **c:** the C library: parse, parse_artists, format, normalize, parsers, tracklists, edits and JSON output, with the same output as the other ports ([a19c13b](https://github.com/4matic/trackparse/commit/a19c13ba6fb4273c066cbf07d44c65f08d1f4802))


### Bug Fixes

* **c:** review fixes: C++20 test includes, MSVC C4132 in the amalgamation, no leak in the C++ string copy, no view of a temporary track, lock-free reads of the built tables, mark/release keeps chunk growth; CI: CMake 3.16.8 and a Windows DLL job ([4823cfd](https://github.com/4matic/trackparse/commit/4823cfdf1004023a47bb8aca738cb0d37b7b6d45))
* **c:** the exports check ignores the i386 PIC thunks ([e83db4e](https://github.com/4matic/trackparse/commit/e83db4ea58e507ba5b9fbf29b652d8b604204436))
* **c:** warnings newer compilers raise: MSVC C4324 on tp_scope, GCC 11 memset bound on 32-bit, GCC 13 -Wclobbered in test_num, Clang -Wstring-concatenation in test_oom ([45c8f3e](https://github.com/4matic/trackparse/commit/45c8f3ec3cd28f4f6096b16d54931a8577f48da0))
* **c:** Windows consumer checks find the DLL, the exports check ignores sanitizer symbols, test_num avoids -Wclobbered on 32-bit ([fdc4cff](https://github.com/4matic/trackparse/commit/fdc4cff197074f0c995c6db4df524f6c18c576a5))


### Performance

* **c:** give the known-artist search's candidate keys back at once (arena mark and release): 1 GB to 5 MB on a 10,000-code-point input ([3cbc219](https://github.com/4matic/trackparse/commit/3cbc219aee236d7c58f1522143ab42a462cef125))


### Documentation

* AGENTS.md, the testing and ports pages and the C README describe the C port, its commands, hazards, release and known gaps ([347d5c9](https://github.com/4matic/trackparse/commit/347d5c9643122b3b2ba393deee6832cc06564f7d))
* **c:** the numbered hazards move into c/src/PORTING.md (they ship with the tarball); README fixes for 0.7.0, test build folders, splitAnd and the C++ string types ([4d55f74](https://github.com/4matic/trackparse/commit/4d55f747a8d275748718ab934f0163b090182949))
* the C port on every site page, in the playground code panels and install switch, and in the READMEs ([365e7fd](https://github.com/4matic/trackparse/commit/365e7fdd54c850f8fd8c449819932ea431b957b1))
