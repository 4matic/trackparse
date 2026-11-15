# package.cmake: the installed library, its install rules and the consumer checks.
# Included at the end of c/CMakeLists.txt.
#
# The library `trackparse` (alias trackparse::trackparse) is built from the amalgamation: one
# translation unit in which every internal name is static, so nothing but trackparse_* leaves
# the object file, static or shared. Where the amalgamation comes from:
#   - a git checkout (the repo's .git is present) with Node: generated into the build tree by
#     scripts/amalgamate.mjs, and regenerated when a source changes;
#   - otherwise dist/trackparse.c, which the release tarball ships (no Node needed);
#   - TRACKPARSE_AMALGAMATION_DIR: a folder holding trackparse.c, to use that one instead.
#
# Options:
#   TRACKPARSE_SHARED   build libtrackparse as a shared library (default: BUILD_SHARED_LIBS)
#   TRACKPARSE_INSTALL  install rules (default ON when trackparse is the top-level project)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(trackparse_is_top_level ON)
else()
  set(trackparse_is_top_level OFF)
endif()

option(TRACKPARSE_SHARED "Build libtrackparse as a shared library" ${BUILD_SHARED_LIBS})
option(TRACKPARSE_INSTALL "Generate the install rules" ${trackparse_is_top_level})
set(TRACKPARSE_AMALGAMATION_DIR "" CACHE PATH
  "A folder with a ready trackparse.c amalgamation (empty: generate it or use dist/)")

# The internal build (tests, wire, dump) is not something a FetchContent user wants to build.
if(NOT trackparse_is_top_level)
  foreach(t trackparse_internal trackparse_wire trackparse-dump)
    if(TARGET ${t})
      set_target_properties(${t} PROPERTIES EXCLUDE_FROM_ALL ON)
    endif()
  endforeach()
endif()

# ---------------------------------------------------------------------------------------------
# The amalgamation

find_program(TRACKPARSE_NODE_EXECUTABLE node)
file(GLOB trackparse_amalgamation_inputs CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
list(APPEND trackparse_amalgamation_inputs
  "${CMAKE_CURRENT_SOURCE_DIR}/include/trackparse.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/trackparse.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/amalgamate.mjs")

if(TRACKPARSE_AMALGAMATION_DIR)
  set(TRACKPARSE_AMALGAMATION_C "${TRACKPARSE_AMALGAMATION_DIR}/trackparse.c")
  set(trackparse_amalgamation_how "TRACKPARSE_AMALGAMATION_DIR")
elseif(EXISTS "${TRACKPARSE_REPO_DIR}/.git" AND TRACKPARSE_NODE_EXECUTABLE)
  set(TRACKPARSE_AMALGAMATION_C "${CMAKE_CURRENT_BINARY_DIR}/amalgamation/trackparse.c")
  set(trackparse_amalgamation_how "generated with Node")
  add_custom_command(
    OUTPUT "${TRACKPARSE_AMALGAMATION_C}"
    COMMAND "${TRACKPARSE_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/amalgamate.mjs"
            --out "${CMAKE_CURRENT_BINARY_DIR}/amalgamation"
    DEPENDS ${trackparse_amalgamation_inputs}
    COMMENT "Writing the amalgamation (scripts/amalgamate.mjs)"
    VERBATIM)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dist/trackparse.c")
  set(TRACKPARSE_AMALGAMATION_C "${CMAKE_CURRENT_SOURCE_DIR}/dist/trackparse.c")
  set(trackparse_amalgamation_how "dist/")
else()
  message(FATAL_ERROR
    "trackparse: no amalgamation. Building from git needs Node (scripts/amalgamate.mjs writes "
    "it); the release tarball ships dist/trackparse.c.")
endif()
message(STATUS "trackparse: library from the amalgamation (${trackparse_amalgamation_how})")

# ---------------------------------------------------------------------------------------------
# The library

if(TRACKPARSE_SHARED)
  add_library(trackparse SHARED "${TRACKPARSE_AMALGAMATION_C}")
else()
  add_library(trackparse STATIC "${TRACKPARSE_AMALGAMATION_C}")
  # Without it the header declares DLL imports on Windows; elsewhere it changes nothing.
  target_compile_definitions(trackparse PUBLIC TRACKPARSE_STATIC)
endif()
add_library(trackparse::trackparse ALIAS trackparse)

target_include_directories(trackparse PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_definitions(trackparse PRIVATE TRACKPARSE_BUILDING)
target_compile_features(trackparse PUBLIC c_std_11)

# The flags the results depend on (PLAN.md hazards 1, 5 and 12), always. The warning flags of
# trackparse_flags (and -Werror under CI) only when trackparse is the top-level project, so a
# consumer's newer compiler cannot break its build.
if(trackparse_is_top_level)
  # BUILD_INTERFACE: a build-only dependency, so the export set does not need trackparse_flags.
  target_link_libraries(trackparse PRIVATE "$<BUILD_INTERFACE:trackparse_flags>")
  # A sanitizer or fuzz build instruments the library, so whatever links it in this build tree
  # (examples, the C++ tests) needs the same link options.
  target_link_options(trackparse INTERFACE
    "$<BUILD_INTERFACE:$<TARGET_PROPERTY:trackparse_flags,INTERFACE_LINK_OPTIONS>>")
elseif(MSVC)
  target_compile_options(trackparse PRIVATE /fp:precise /utf-8)
  target_compile_definitions(trackparse PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(trackparse PRIVATE -ffp-contract=off)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|X86|AMD64|x86_64)$")
    target_compile_options(trackparse PRIVATE -msse2 -mfpmath=sse)
  endif()
endif()

# Shared library version: no ABI promise while 0.x, so the soname carries the minor version
# (libtrackparse.so.0.6); from 1.0 the major version.
if(PROJECT_VERSION_MAJOR EQUAL 0)
  set(trackparse_soversion "0.${PROJECT_VERSION_MINOR}")
else()
  set(trackparse_soversion "${PROJECT_VERSION_MAJOR}")
endif()
set_target_properties(trackparse PROPERTIES
  C_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${trackparse_soversion}"
  EXPORT_NAME trackparse)

find_package(Threads REQUIRED)
target_link_libraries(trackparse PRIVATE Threads::Threads)
set(trackparse_pc_libs_private "")
if(CMAKE_USE_PTHREADS_INIT OR CMAKE_THREAD_LIBS_INIT)
  set(trackparse_pc_libs_private "${CMAKE_THREAD_LIBS_INIT}")
endif()
if(UNIX)
  target_link_libraries(trackparse PRIVATE m)
  string(APPEND trackparse_pc_libs_private " -lm")
endif()
string(STRIP "${trackparse_pc_libs_private}" trackparse_pc_libs_private)

# ---------------------------------------------------------------------------------------------
# Install: lib, headers, CMake package, pkg-config file

if(TRACKPARSE_INSTALL)
  set(TRACKPARSE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/trackparse" CACHE STRING
    "Where trackparseConfig.cmake is installed")

  install(TARGETS trackparse EXPORT trackparseTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" NAMELINK_COMPONENT Development
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/include/trackparse.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/trackparse.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION "${CMAKE_INSTALL_DOCDIR}")

  install(EXPORT trackparseTargets
    NAMESPACE trackparse::
    FILE trackparseTargets.cmake
    DESTINATION "${TRACKPARSE_INSTALL_CMAKEDIR}")

  set(TRACKPARSE_CONFIG_STATIC OFF)
  if(NOT TRACKPARSE_SHARED)
    set(TRACKPARSE_CONFIG_STATIC ON)
  endif()
  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/trackparseConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/trackparseConfig.cmake"
    INSTALL_DESTINATION "${TRACKPARSE_INSTALL_CMAKEDIR}")
  # 0.x: a minor release may break the API, so only the same minor version is compatible.
  if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(trackparse_compat SameMinorVersion)
  else()
    set(trackparse_compat SameMajorVersion)
  endif()
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/trackparseConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY ${trackparse_compat})
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/trackparseConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/trackparseConfigVersion.cmake"
    DESTINATION "${TRACKPARSE_INSTALL_CMAKEDIR}")

  # pkg-config. With relative install dirs, prefix is found from the .pc file's own place
  # (${pcfiledir}), so `cmake --install --prefix` and a moved tree both work.
  if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(trackparse_pc_prefix "${CMAKE_INSTALL_PREFIX}")
  else()
    file(RELATIVE_PATH trackparse_pc_up "/prefix/${CMAKE_INSTALL_LIBDIR}/pkgconfig" "/prefix")
    string(REGEX REPLACE "/$" "" trackparse_pc_up "${trackparse_pc_up}")
    set(trackparse_pc_prefix "\${pcfiledir}/${trackparse_pc_up}")
  endif()
  foreach(d LIBDIR INCLUDEDIR)
    if(IS_ABSOLUTE "${CMAKE_INSTALL_${d}}")
      set(trackparse_pc_${d} "${CMAKE_INSTALL_${d}}")
    else()
      set(trackparse_pc_${d} "\${prefix}/${CMAKE_INSTALL_${d}}")
    endif()
  endforeach()
  if(TRACKPARSE_SHARED)
    set(trackparse_pc_cflags "")
  else()
    set(trackparse_pc_cflags " -DTRACKPARSE_STATIC")
  endif()
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/trackparse.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/trackparse.pc" @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/trackparse.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif()

# ---------------------------------------------------------------------------------------------
# Tests: the C++ wrapper, the examples, the exported symbols, and the consumer checks

if(TRACKPARSE_BUILD_TESTS AND trackparse_is_top_level)
  # The C++ wrapper, against the installed library target, in C++17 and C++20.
  foreach(std 17 20)
    if(std EQUAL 20 AND NOT "cxx_std_20" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
      continue()
    endif()
    add_executable(test_cpp${std} tests/test_cpp.cpp)
    target_link_libraries(test_cpp${std} PRIVATE trackparse::trackparse)
    set_target_properties(test_cpp${std} PROPERTIES CXX_STANDARD ${std} CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF)
    if(MSVC)
      target_compile_options(test_cpp${std} PRIVATE /W4 /utf-8 $<$<BOOL:${TRACKPARSE_WERROR}>:/WX>)
    else()
      target_compile_options(test_cpp${std} PRIVATE -Wall -Wextra -Wpedantic -Wshadow
        $<$<BOOL:${TRACKPARSE_WERROR}>:-Werror>)
    endif()
    add_test(NAME cpp${std} COMMAND test_cpp${std})
  endforeach()

  # The examples, built against the library target; each output must match its .expected.
  set(trackparse_examples basic options tracklist json cpp)
  foreach(example IN LISTS trackparse_examples)
    if(example STREQUAL "cpp")
      add_executable(example_${example} examples/${example}.cpp)
    else()
      add_executable(example_${example} examples/${example}.c)
    endif()
    target_link_libraries(example_${example} PRIVATE trackparse::trackparse)
    add_test(NAME example_${example}
      COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:example_${example}>"
              "-DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/examples/${example}.expected"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_example.cmake")
    set_tests_properties(example_${example} PROPERTIES LABELS examples)
  endforeach()

  # Only trackparse_* may leave the library (nm; skipped where there is none).
  find_program(TRACKPARSE_NM_EXECUTABLE NAMES nm llvm-nm)
  add_test(NAME exports
    COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:trackparse>"
            "-DNM=${TRACKPARSE_NM_EXECUTABLE}"
            "-DSHARED=${TRACKPARSE_SHARED}" "-DAPPLE=${APPLE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_exports.cmake")
  set_tests_properties(exports PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")

  # Consumer checks (label "consumer"): install into a prefix under the build tree, then build
  # the examples from it with find_package and with pkg-config; build them from the amalgamation
  # alone, with plain compiler commands and with CMake; and as a FetchContent dependency.
  set(trackparse_consumer_dir "${CMAKE_CURRENT_BINARY_DIR}/consumer")
  set(trackparse_prefix "${trackparse_consumer_dir}/prefix")
  set(trackparse_consumer_args
    "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DWORK_DIR=${trackparse_consumer_dir}"
    "-DPREFIX=${trackparse_prefix}"
    "-DAMALGAMATION_C=${TRACKPARSE_AMALGAMATION_C}"
    "-DGENERATOR=${CMAKE_GENERATOR}"
    "-DC_COMPILER=${CMAKE_C_COMPILER}"
    "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCONFIG=$<CONFIG>"
    "-DSHARED=${TRACKPARSE_SHARED}")
  if(TRACKPARSE_INSTALL)
    add_test(NAME consumer_install
      COMMAND "${CMAKE_COMMAND}" -DMODE=install "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
              ${trackparse_consumer_args}
              -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/consumer_check.cmake")
    set_tests_properties(consumer_install PROPERTIES FIXTURES_SETUP trackparse_installed)
    foreach(how find_package pkg_config)
      add_test(NAME consumer_${how}
        COMMAND "${CMAKE_COMMAND}" "-DMODE=${how}" ${trackparse_consumer_args}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/consumer_check.cmake")
      set_tests_properties(consumer_${how} PROPERTIES FIXTURES_REQUIRED trackparse_installed)
    endforeach()
  endif()
  foreach(how amalgamation amalgamation_cmake fetchcontent)
    add_test(NAME consumer_${how}
      COMMAND "${CMAKE_COMMAND}" "-DMODE=${how}" ${trackparse_consumer_args}
              -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/consumer_check.cmake")
  endforeach()
  set(trackparse_consumer_tests consumer_amalgamation consumer_amalgamation_cmake
    consumer_fetchcontent)
  if(TRACKPARSE_INSTALL)
    list(APPEND trackparse_consumer_tests
      consumer_install consumer_find_package consumer_pkg_config)
  endif()
  set_tests_properties(${trackparse_consumer_tests} PROPERTIES
    LABELS consumer SKIP_REGULAR_EXPRESSION "SKIP:" TIMEOUT 900)
endif()
