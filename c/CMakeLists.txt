# The C port of trackparse. See AGENTS.md and src/PORTING.md.
#
#   cmake -S c -B c/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build c/build
#   ctest --test-dir c/build --output-on-failure
#
# Options:
#   TRACKPARSE_WERROR    warnings are errors (default ON when the CI environment variable is set)
#   TRACKPARSE_SANITIZE  build everything with AddressSanitizer + UndefinedBehaviorSanitizer
#   TRACKPARSE_FUZZ      libFuzzer targets in fuzz/ (Clang); see cmake/hardening.cmake
#   TRACKPARSE_BUILD_TESTS  the test programs and CTest entries (default ON at top level)
#   TRACKPARSE_SHARED    build the installed library shared (default BUILD_SHARED_LIBS)
#   TRACKPARSE_INSTALL   install rules (default ON at top level); see cmake/package.cmake
cmake_minimum_required(VERSION 3.16)

project(trackparse VERSION 0.6.1 LANGUAGES C) # x-release-please-version

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Only as the top-level project: under FetchContent the parent decides the build type.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_BUILD_TYPE
   AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

if(DEFINED ENV{CI})
  set(trackparse_werror_default ON)
else()
  set(trackparse_werror_default OFF)
endif()
option(TRACKPARSE_WERROR "Treat warnings as errors" ${trackparse_werror_default})
option(TRACKPARSE_SANITIZE "Build with -fsanitize=address,undefined" OFF)
option(TRACKPARSE_FUZZ "Build the libFuzzer targets (Clang)" OFF)
# Tests only by default when trackparse is the top-level project (not under FetchContent).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(trackparse_tests_default ON)
else()
  set(trackparse_tests_default OFF)
endif()
option(TRACKPARSE_BUILD_TESTS "Build the tests and register them with CTest" ${trackparse_tests_default})
# C++ is needed only for the wrapper's tests and the C++ example; users of the library need none.
if(TRACKPARSE_BUILD_TESTS)
  enable_language(CXX)
endif()

get_filename_component(TRACKPARSE_REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(TRACKPARSE_SPEC_DIR "${TRACKPARSE_REPO_DIR}/spec")

# ---------------------------------------------------------------------------------------------
# Compiler flags shared by every target

add_library(trackparse_flags INTERFACE)
if(MSVC)
  # /fp:precise: no contraction into FMA (PORTING.md hazard 1). /utf-8: hazard 12.
  target_compile_options(trackparse_flags INTERFACE /W4 /fp:precise /utf-8)
  target_compile_definitions(trackparse_flags INTERFACE _CRT_SECURE_NO_WARNINGS)
  if(TRACKPARSE_WERROR)
    target_compile_options(trackparse_flags INTERFACE /WX)
  endif()
else()
  # -ffp-contract=off: GCC's GNU-mode default is fast, which can fuse floor(v * p + 0.5) into an
  # FMA and change the rounding (PORTING.md hazard 1).
  target_compile_options(trackparse_flags INTERFACE
    -ffp-contract=off
    -Wall -Wextra -Wpedantic -Wshadow
    $<$<COMPILE_LANGUAGE:C>:-Wconversion -Wno-sign-conversion>)
  if(TRACKPARSE_WERROR)
    target_compile_options(trackparse_flags INTERFACE -Werror)
  endif()
  if(TRACKPARSE_SANITIZE)
    target_compile_options(trackparse_flags INTERFACE
      -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(trackparse_flags INTERFACE -fsanitize=address,undefined)
  endif()
  # 32-bit x86 without SSE2 computes in 80-bit x87 registers (PORTING.md hazard 5).
  if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|X86|AMD64|x86_64)$")
    target_compile_options(trackparse_flags INTERFACE -msse2 -mfpmath=sse)
  endif()
endif()

find_package(Threads)

# ---------------------------------------------------------------------------------------------
# trackparse_internal: every src/*.c as separate translation units (TP_PRIVATE empty), for the
# tests, the wire library and the tools. Never installed.

file(GLOB TRACKPARSE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
add_library(trackparse_internal STATIC ${TRACKPARSE_SOURCES})
target_include_directories(trackparse_internal PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(trackparse_internal PUBLIC TRACKPARSE_STATIC TRACKPARSE_BUILDING)
target_link_libraries(trackparse_internal PUBLIC trackparse_flags)
if(Threads_FOUND)
  target_link_libraries(trackparse_internal PUBLIC Threads::Threads)
endif()
if(UNIX)
  target_link_libraries(trackparse_internal PUBLIC m)
endif()

# ---------------------------------------------------------------------------------------------
# trackparse_wire: the JSON door (wire/*.c). wire_stub.c answers null to everything until
# wire.c exists, then it is left out.

file(GLOB TRACKPARSE_WIRE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/wire/*.c")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/wire/wire.c")
  list(REMOVE_ITEM TRACKPARSE_WIRE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/wire/wire_stub.c")
endif()
add_library(trackparse_wire STATIC ${TRACKPARSE_WIRE_SOURCES})
target_include_directories(trackparse_wire PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/wire")
target_link_libraries(trackparse_wire PUBLIC trackparse_internal)

# ---------------------------------------------------------------------------------------------
# Tools

add_executable(trackparse-dump tools/dump.c)
target_link_libraries(trackparse-dump PRIVATE trackparse_wire)

# The installed library `trackparse` (built from the amalgamation), install() rules,
# trackparseConfig.cmake, trackparse.pc, the C++ wrapper tests and the consumer checks are in
# cmake/package.cmake, included at the end.
# ---------------------------------------------------------------------------------------------

if(TRACKPARSE_FUZZ)
  # libFuzzer targets in fuzz/ (parse, parse_artists, format, parse_tracklist, apply_edits, wire),
  # defined in cmake/hardening.cmake; scripts/fuzz_corpus.mjs seeds them from the fixture inputs.
  # Clang only. Every target gets coverage instrumentation and ASan + UBSan; the fuzz programs
  # add libFuzzer's main.
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "TRACKPARSE_FUZZ needs Clang (libFuzzer)")
  endif()
  target_compile_options(trackparse_flags INTERFACE
    -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_link_options(trackparse_flags INTERFACE -fsanitize=address,undefined)
  if(NOT TRACKPARSE_BUILD_TESTS)
    include(cmake/hardening.cmake)
  endif()
endif()

# ---------------------------------------------------------------------------------------------
# Tests

if(TRACKPARSE_BUILD_TESTS)
  enable_testing()

  # Every test program gets the source and repo paths, so it can find its data.
  function(trackparse_test_program name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE trackparse_wire)
    target_compile_definitions(${name} PRIVATE
      "TRACKPARSE_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\""
      "TRACKPARSE_REPO_DIR=\"${TRACKPARSE_REPO_DIR}\"")
  endfunction()

  # The matcher and the JSON reader.
  trackparse_test_program(test_match tests/test_match.c tests/match.c)
  add_test(NAME match COMMAND test_match)

  # Unit tests written alongside the runtime and the primitives; each is registered once its
  # source exists.
  foreach(t test_rt test_num test_unicode test_primitives test_api)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}.c")
      trackparse_test_program(${t} tests/${t}.c)
      string(REPLACE "test_" "" test_name ${t})
      # The tests read their data relative to c/.
      add_test(NAME ${test_name} COMMAND ${t} WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
      set_tests_properties(${test_name} PROPERTIES SKIP_RETURN_CODE 77)
    endif()
  endforeach()

  # The fixture runner over every shared fixture file. Without ../spec (the release tarball holds
  # only c/) it gets no files and reports the test as skipped (exit 77).
  trackparse_test_program(trackparse-fixtures tests/runner.c tests/match.c)
  target_compile_definitions(trackparse-fixtures PRIVATE
    "TP_SKIP_JSON=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/skip.json\"")
  set(TRACKPARSE_FIXTURE_FILES "")
  if(EXISTS "${TRACKPARSE_SPEC_DIR}/fixtures")
    file(GLOB TRACKPARSE_FIXTURE_FILES CONFIGURE_DEPENDS
      "${TRACKPARSE_SPEC_DIR}/fixtures/handwritten/*.json"
      "${TRACKPARSE_SPEC_DIR}/fixtures/generated/*.json")
    list(SORT TRACKPARSE_FIXTURE_FILES)
  endif()
  set(TRACKPARSE_OUTPUTS "${CMAKE_CURRENT_BINARY_DIR}/fixture-outputs.jsonl")
  add_test(NAME fixtures COMMAND trackparse-fixtures ${TRACKPARSE_FIXTURE_FILES})
  set_tests_properties(fixtures PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "TRACKPARSE_OUTPUT=${TRACKPARSE_OUTPUTS}"
    FIXTURES_SETUP trackparse_outputs)

  # Schema validation of every output, with the ajv the JS package already has. Runs after the
  # fixtures test and only when it passed (FIXTURES_REQUIRED).
  find_program(TRACKPARSE_NODE node)
  if(TRACKPARSE_NODE AND TRACKPARSE_FIXTURE_FILES
     AND EXISTS "${TRACKPARSE_REPO_DIR}/js/node_modules/ajv")
    add_test(NAME schema
      COMMAND "${TRACKPARSE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_outputs.mjs"
              "${TRACKPARSE_OUTPUTS}")
    set_tests_properties(schema PROPERTIES FIXTURES_REQUIRED trackparse_outputs)
  endif()
endif()

# Hardening: properties, threads, locale, bench, OOM sweep, fuzz targets (cmake/hardening.cmake).
if(TRACKPARSE_BUILD_TESTS)
  include(cmake/hardening.cmake OPTIONAL)
endif()

# Packaging: the installed library built from the amalgamation, install rules, the C++ header
# tests (cmake/package.cmake).
include(cmake/package.cmake OPTIONAL)
