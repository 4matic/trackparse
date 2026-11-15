# hardening.cmake: the hardening tests and the fuzz targets (AGENTS.md, C hazards). Included from
# CMakeLists.txt when TRACKPARSE_BUILD_TESTS or TRACKPARSE_FUZZ is on.
#
#   properties  SPEC invariants and R13.11 on seeded inputs, and the time ceilings
#   threads     16 threads against a sequential run (run it under TRACKPARSE_SANITIZE_THREAD)
#   locale      outputs under setlocale(LC_ALL, "") and de_DE / tr_TR equal the "C" locale's
#   oom         the n-th allocation fails, for every n, on ~50 inputs across the API
#   test_bench  not a test: ns per call, printed like go test -bench (run it by hand)
#   fuzz_*      libFuzzer targets (TRACKPARSE_FUZZ, Clang)
#
# Options added here:
#   TRACKPARSE_SANITIZE_THREAD  build everything with ThreadSanitizer (not with TRACKPARSE_SANITIZE)
#   TRACKPARSE_SANITIZE_MEMORY  build everything with MemorySanitizer (Clang). The C programs run
#                               against the uninstrumented glibc (MSan intercepts what they call);
#                               the C++ targets would need an instrumented libc++, so build the C
#                               targets only (cmake --build ... --target test_properties ...)
#
# A build with any sanitizer (or the fuzz flags) defines TRACKPARSE_SANITIZED for the hardening
# programs: they then run fewer property cases and widen the time ceilings ten times.

option(TRACKPARSE_SANITIZE_THREAD "Build with -fsanitize=thread" OFF)
option(TRACKPARSE_SANITIZE_MEMORY "Build with -fsanitize=memory (Clang)" OFF)

if(NOT MSVC)
  if(TRACKPARSE_SANITIZE_THREAD)
    if(TRACKPARSE_SANITIZE OR TRACKPARSE_SANITIZE_MEMORY OR TRACKPARSE_FUZZ)
      message(FATAL_ERROR "TRACKPARSE_SANITIZE_THREAD cannot be combined with another sanitizer")
    endif()
    target_compile_options(trackparse_flags INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(trackparse_flags INTERFACE -fsanitize=thread)
  endif()
  if(TRACKPARSE_SANITIZE_MEMORY)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
      message(FATAL_ERROR "TRACKPARSE_SANITIZE_MEMORY needs Clang")
    endif()
    if(TRACKPARSE_SANITIZE OR TRACKPARSE_FUZZ)
      message(FATAL_ERROR "TRACKPARSE_SANITIZE_MEMORY cannot be combined with ASan")
    endif()
    target_compile_options(trackparse_flags INTERFACE
      -fsanitize=memory -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer)
    target_link_options(trackparse_flags INTERFACE -fsanitize=memory)
  endif()
endif()

set(TRACKPARSE_SANITIZED OFF)
if(TRACKPARSE_SANITIZE OR TRACKPARSE_SANITIZE_THREAD OR TRACKPARSE_SANITIZE_MEMORY
   OR TRACKPARSE_FUZZ)
  set(TRACKPARSE_SANITIZED ON)
endif()

# The generator, the checks, the fixture loader (tests/hardening.c), shared by the tests and the
# fuzz targets.
add_library(trackparse_hardening STATIC tests/hardening.c)
target_link_libraries(trackparse_hardening PUBLIC trackparse_wire)
if(TRACKPARSE_SANITIZED)
  target_compile_definitions(trackparse_hardening PUBLIC TRACKPARSE_SANITIZED)
endif()

if(TRACKPARSE_BUILD_TESTS)
  function(trackparse_hardening_program name)
    trackparse_test_program(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE trackparse_hardening)
  endfunction()

  trackparse_hardening_program(test_properties tests/test_properties.c)
  add_test(NAME properties COMMAND test_properties WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(properties PROPERTIES TIMEOUT 3000)

  # The fixture files are the inputs of threads, locale, oom and the bench (no spec/: the
  # programs fall back to a few inputs of their own, or skip).
  trackparse_hardening_program(test_threads tests/test_threads.c)
  if(Threads_FOUND)
    target_link_libraries(test_threads PRIVATE Threads::Threads)
  endif()
  add_test(NAME threads COMMAND test_threads ${TRACKPARSE_FIXTURE_FILES})
  set_tests_properties(threads PROPERTIES TIMEOUT 3000)

  trackparse_hardening_program(test_locale tests/test_locale.c)
  add_test(NAME locale COMMAND test_locale ${TRACKPARSE_FIXTURE_FILES})
  set_tests_properties(locale PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 3000)

  trackparse_hardening_program(test_oom tests/test_oom.c)
  add_test(NAME oom COMMAND test_oom ${TRACKPARSE_FIXTURE_FILES})
  set_tests_properties(oom PROPERTIES TIMEOUT 3000)

  # Not registered with CTest: test_bench [--fixtures <files>...], or TRACKPARSE_BENCH=1 ctest
  # -R bench (registered only then, so a plain ctest never runs it).
  trackparse_hardening_program(test_bench tests/test_bench.c)
  if(DEFINED ENV{TRACKPARSE_BENCH})
    add_test(NAME bench COMMAND test_bench --fixtures ${TRACKPARSE_FIXTURE_FILES})
  endif()
endif()

# libFuzzer targets: one per public function, and the JSON door. CMakeLists.txt sets the
# coverage and sanitizer flags for every target when TRACKPARSE_FUZZ is on; these add libFuzzer's
# main. Run one:  ./fuzz_parse -max_total_time=60 corpus/parse
# (scripts/fuzz_corpus.mjs writes the seed corpus from the fixture inputs).
if(TRACKPARSE_FUZZ)
  foreach(t parse parse_artists format parse_tracklist apply_edits wire)
    add_executable(fuzz_${t} fuzz/fuzz_${t}.c)
    target_link_libraries(fuzz_${t} PRIVATE trackparse_hardening)
    target_link_options(fuzz_${t} PRIVATE -fsanitize=fuzzer)
  endforeach()
endif()
