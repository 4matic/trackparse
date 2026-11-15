# consumer_check.cmake: builds and runs the examples the way a user of the library would.
# Run by the consumer_* tests (cmake/package.cmake); each example's output must match
# examples/<name>.expected.
#
#   cmake -DMODE=<mode> -DSOURCE_DIR=<c/> -DWORK_DIR=... -DPREFIX=... -DAMALGAMATION_C=...
#         -DGENERATOR=... -DC_COMPILER=... -DCXX_COMPILER=... -DCONFIG=... -DSHARED=ON|OFF
#         [-DBUILD_DIR=...] -P consumer_check.cmake
#
# MODE:
#   install             cmake --install BUILD_DIR into PREFIX (fresh)
#   find_package        examples/ as a CMake project, find_package(trackparse) from PREFIX
#   pkg_config          each example compiled by hand with `pkg-config --cflags --libs trackparse`
#   amalgamation        the amalgamation alone: trackparse.c compiled with the strict warning set,
#                       as a program part and as a shared-library object, then each example
#                       compiled by hand against it
#   amalgamation_cmake  examples/ as a CMake project that compiles trackparse.c in
#   fetchcontent        examples/ as a CMake project with FetchContent(SOURCE_DIR c/)
#
# Output "SKIP: ..." makes the test count as skipped.

cmake_minimum_required(VERSION 3.16)

set(examples basic options tracklist json)
set(work "${WORK_DIR}/${MODE}")
get_filename_component(amalgamation_dir "${AMALGAMATION_C}" DIRECTORY)

if(C_COMPILER MATCHES "(^|[/\\\\])(cl|clang-cl)(\\.exe)?$")
  set(msvc_like ON)
else()
  set(msvc_like OFF)
endif()

function(run)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    string(REPLACE ";" " " cmd "${ARGN}")
    message(FATAL_ERROR "failed (${rc}): ${cmd}\n${out}\n${err}")
  endif()
endfunction()

function(check_output program name)
  set(launcher "")
  if(WIN32)
    # Windows finds a DLL next to the program or on PATH: add every folder that holds one of ours
    # (the install prefix's bin/, a FetchContent build folder).
    file(GLOB_RECURSE dlls "${WORK_DIR}/*trackparse*.dll" "${PREFIX}/*trackparse*.dll")
    set(dirs "")
    foreach(dll IN LISTS dlls)
      get_filename_component(dir "${dll}" DIRECTORY)
      file(TO_NATIVE_PATH "${dir}" dir)
      list(APPEND dirs "${dir}")
    endforeach()
    if(dirs)
      list(REMOVE_DUPLICATES dirs)
      string(REPLACE ";" "\\;" path "${dirs};$ENV{PATH}")
      set(launcher "${CMAKE_COMMAND}" -E env "PATH=${path}")
    endif()
  endif()
  execute_process(COMMAND ${launcher} "${program}" RESULT_VARIABLE rc OUTPUT_VARIABLE out
                  ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${program} exited with ${rc}\n${err}")
  endif()
  file(READ "${SOURCE_DIR}/examples/${name}.expected" expected)
  file(STRINGS "${SOURCE_DIR}/VERSION" version LIMIT_COUNT 1)
  string(STRIP "${version}" version)
  # @TRACKPARSE_VERSION@ stands for c/VERSION (see run_example.cmake).
  string(REPLACE "@TRACKPARSE_VERSION@" "${version}" expected "${expected}")
  string(REPLACE "\r\n" "\n" out "${out}")
  string(REPLACE "\r\n" "\n" expected "${expected}")
  if(NOT out STREQUAL expected)
    message(FATAL_ERROR "${MODE}: ${name} output differs from examples/${name}.expected:\n${out}")
  endif()
endfunction()

# Configure, build and run examples/ as a project of its own.
function(cmake_project)
  file(REMOVE_RECURSE "${work}")
  set(args -S "${SOURCE_DIR}/examples" -B "${work}" -G "${GENERATOR}"
    "-DCMAKE_C_COMPILER=${C_COMPILER}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" ${ARGN})
  if(CONFIG)
    list(APPEND args "-DCMAKE_BUILD_TYPE=${CONFIG}")
  endif()
  run("${CMAKE_COMMAND}" ${args})
  if(CONFIG)
    run("${CMAKE_COMMAND}" --build "${work}" --config "${CONFIG}")
  else()
    run("${CMAKE_COMMAND}" --build "${work}")
  endif()
  foreach(name IN LISTS examples ITEMS cpp)
    set(exe "")
    foreach(candidate "${work}/${name}" "${work}/${name}.exe" "${work}/${CONFIG}/${name}.exe")
      if(EXISTS "${candidate}" AND NOT IS_DIRECTORY "${candidate}")
        set(exe "${candidate}")
        break()
      endif()
    endforeach()
    if(NOT exe)
      message(FATAL_ERROR "${MODE}: no ${name} program in ${work}")
    endif()
    check_output("${exe}" ${name})
  endforeach()
endfunction()

set(strict_c -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Werror)
set(strict_cxx -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -Werror)

if(MODE STREQUAL "install")
  file(REMOVE_RECURSE "${PREFIX}")
  if(CONFIG)
    run("${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${PREFIX}" --config "${CONFIG}")
  else()
    run("${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${PREFIX}")
  endif()
  foreach(f include/trackparse.h include/trackparse.hpp)
    if(NOT EXISTS "${PREFIX}/${f}")
      message(FATAL_ERROR "install: ${f} is missing")
    endif()
  endforeach()
  file(GLOB_RECURSE configs "${PREFIX}/*/trackparseConfig.cmake")
  file(GLOB_RECURSE pcs "${PREFIX}/*/trackparse.pc")
  if(NOT configs OR NOT pcs)
    message(FATAL_ERROR "install: trackparseConfig.cmake or trackparse.pc is missing")
  endif()

elseif(MODE STREQUAL "find_package")
  cmake_project(-DTRACKPARSE_EXAMPLES_FROM=package "-DCMAKE_PREFIX_PATH=${PREFIX}")

elseif(MODE STREQUAL "pkg_config")
  find_program(pkg_config NAMES pkg-config pkgconf)
  if(NOT pkg_config OR msvc_like)
    message("SKIP: no pkg-config, or an MSVC-style compiler")
    return()
  endif()
  file(GLOB_RECURSE pcs "${PREFIX}/*/pkgconfig/trackparse.pc")
  list(GET pcs 0 pc)
  get_filename_component(pcdir "${pc}" DIRECTORY)
  set(ENV{PKG_CONFIG_PATH} "${pcdir}")
  set(ENV{PKG_CONFIG_LIBDIR} "${pcdir}")
  set(static "")
  if(NOT SHARED)
    set(static --static)
  endif()
  execute_process(COMMAND "${pkg_config}" --cflags trackparse OUTPUT_VARIABLE cflags
    RESULT_VARIABLE rc1 OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND "${pkg_config}" ${static} --libs trackparse OUTPUT_VARIABLE libs
    RESULT_VARIABLE rc2 OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND "${pkg_config}" --variable=libdir trackparse OUTPUT_VARIABLE libdir
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT rc1 EQUAL 0 OR NOT rc2 EQUAL 0)
    message(FATAL_ERROR "pkg-config does not know trackparse (${pcdir})")
  endif()
  separate_arguments(cflags UNIX_COMMAND "${cflags}")
  separate_arguments(libs UNIX_COMMAND "${libs}")
  set(rpath "")
  if(SHARED)
    set(rpath "-Wl,-rpath,${libdir}")
  endif()
  file(REMOVE_RECURSE "${work}")
  file(MAKE_DIRECTORY "${work}")
  foreach(name IN LISTS examples)
    run("${C_COMPILER}" ${strict_c} ${cflags} "${SOURCE_DIR}/examples/${name}.c"
      -o "${work}/${name}" ${libs} ${rpath})
    check_output("${work}/${name}" ${name})
  endforeach()
  run("${CXX_COMPILER}" ${strict_cxx} ${cflags} "${SOURCE_DIR}/examples/cpp.cpp"
    -o "${work}/cpp" ${libs} ${rpath})
  check_output("${work}/cpp" cpp)

elseif(MODE STREQUAL "amalgamation")
  if(msvc_like)
    message("SKIP: plain compiler commands are written for GCC and Clang (see amalgamation_cmake)")
    return()
  endif()
  if(NOT EXISTS "${AMALGAMATION_C}")
    message(FATAL_ERROR "no amalgamation at ${AMALGAMATION_C} (build the library first)")
  endif()
  file(REMOVE_RECURSE "${work}")
  file(MAKE_DIRECTORY "${work}")
  # Copy the three files somewhere alone, so nothing else in c/ can be picked up.
  foreach(f trackparse.c trackparse.h trackparse.hpp)
    file(COPY "${amalgamation_dir}/${f}" DESTINATION "${work}/dist")
  endforeach()
  set(dist "${work}/dist")
  set(libs -lm)
  if(NOT APPLE)
    list(APPEND libs -pthread)
  endif()
  # As part of a program, and as a shared library object (exports only, -fvisibility=hidden).
  run("${C_COMPILER}" ${strict_c} -ffp-contract=off -O2 -c "${dist}/trackparse.c"
    -o "${work}/trackparse.o")
  run("${C_COMPILER}" ${strict_c} -ffp-contract=off -O2 -fPIC -fvisibility=hidden
    -DTRACKPARSE_BUILDING -c "${dist}/trackparse.c" -o "${work}/trackparse_pic.o")
  foreach(name IN LISTS examples)
    run("${C_COMPILER}" ${strict_c} -DTRACKPARSE_STATIC "-I${dist}"
      "${SOURCE_DIR}/examples/${name}.c" "${work}/trackparse.o" -o "${work}/${name}" ${libs})
    check_output("${work}/${name}" ${name})
  endforeach()
  run("${CXX_COMPILER}" ${strict_cxx} -DTRACKPARSE_STATIC "-I${dist}"
    "${SOURCE_DIR}/examples/cpp.cpp" "${work}/trackparse.o" -o "${work}/cpp" ${libs})
  check_output("${work}/cpp" cpp)

elseif(MODE STREQUAL "amalgamation_cmake")
  if(NOT EXISTS "${AMALGAMATION_C}")
    message(FATAL_ERROR "no amalgamation at ${AMALGAMATION_C} (build the library first)")
  endif()
  cmake_project(-DTRACKPARSE_EXAMPLES_FROM=amalgamation "-DTRACKPARSE_SOURCE=${amalgamation_dir}")

elseif(MODE STREQUAL "fetchcontent")
  cmake_project(-DTRACKPARSE_EXAMPLES_FROM=fetchcontent "-DTRACKPARSE_SOURCE=${SOURCE_DIR}"
    "-DTRACKPARSE_SHARED=${SHARED}")

else()
  message(FATAL_ERROR "consumer_check.cmake: unknown MODE ${MODE}")
endif()
