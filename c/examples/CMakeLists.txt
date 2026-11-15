# The examples as a consumer builds them: a project of its own that finds trackparse one of three
# ways. The consumer checks (cmake/package.cmake, ctest -L consumer) configure it with each.
#
#   TRACKPARSE_EXAMPLES_FROM=package       find_package(trackparse CONFIG) from an installed copy
#                                          (set CMAKE_PREFIX_PATH to the install prefix)
#   TRACKPARSE_EXAMPLES_FROM=fetchcontent  FetchContent from TRACKPARSE_SOURCE (the c/ folder or
#                                          an unpacked release tarball)
#   TRACKPARSE_EXAMPLES_FROM=amalgamation  dist/trackparse.c compiled in, from TRACKPARSE_SOURCE
#                                          (a folder holding trackparse.c, .h and .hpp)
cmake_minimum_required(VERSION 3.16)
project(trackparse_examples LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TRACKPARSE_EXAMPLES_FROM "package" CACHE STRING "package, fetchcontent or amalgamation")
set(TRACKPARSE_SOURCE "" CACHE PATH "The trackparse source (fetchcontent, amalgamation)")

if(TRACKPARSE_EXAMPLES_FROM STREQUAL "package")
  find_package(trackparse CONFIG REQUIRED)
elseif(TRACKPARSE_EXAMPLES_FROM STREQUAL "fetchcontent")
  include(FetchContent)
  # A real project gives URL and URL_HASH of the release tarball instead of SOURCE_DIR.
  FetchContent_Declare(trackparse SOURCE_DIR "${TRACKPARSE_SOURCE}")
  FetchContent_MakeAvailable(trackparse)
elseif(TRACKPARSE_EXAMPLES_FROM STREQUAL "amalgamation")
  add_library(trackparse STATIC "${TRACKPARSE_SOURCE}/trackparse.c")
  target_include_directories(trackparse PUBLIC "${TRACKPARSE_SOURCE}")
  target_compile_definitions(trackparse PUBLIC TRACKPARSE_STATIC)
  if(MSVC)
    target_compile_options(trackparse PRIVATE /fp:precise /utf-8)
  else()
    target_compile_options(trackparse PRIVATE -ffp-contract=off)
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(trackparse PRIVATE Threads::Threads)
  if(UNIX)
    target_link_libraries(trackparse PRIVATE m)
  endif()
  add_library(trackparse::trackparse ALIAS trackparse)
else()
  message(FATAL_ERROR "TRACKPARSE_EXAMPLES_FROM: unknown value ${TRACKPARSE_EXAMPLES_FROM}")
endif()

foreach(example basic options tracklist json)
  add_executable(${example} ${example}.c)
  target_link_libraries(${example} PRIVATE trackparse::trackparse)
endforeach()
add_executable(cpp cpp.cpp)
target_link_libraries(cpp PRIVATE trackparse::trackparse)
