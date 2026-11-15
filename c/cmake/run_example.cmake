# Runs PROGRAM and compares its standard output with the file EXPECTED (line ends normalized).
# @TRACKPARSE_VERSION@ in EXPECTED stands for the version in c/VERSION, so a release bump
# (which changes what json.c prints) needs no edit of the expected files.
#   cmake -DPROGRAM=... -DEXPECTED=... -P run_example.cmake
execute_process(COMMAND "${PROGRAM}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "${PROGRAM} exited with ${rc}\n${err}")
endif()
file(READ "${EXPECTED}" expected)
get_filename_component(examples_dir "${EXPECTED}" DIRECTORY)
file(STRINGS "${examples_dir}/../VERSION" version LIMIT_COUNT 1)
string(STRIP "${version}" version)
string(REPLACE "@TRACKPARSE_VERSION@" "${version}" expected "${expected}")
string(REPLACE "\r\n" "\n" out "${out}")
string(REPLACE "\r\n" "\n" expected "${expected}")
if(NOT out STREQUAL expected)
  get_filename_component(name "${EXPECTED}" NAME)
  file(WRITE "${PROGRAM}.actual" "${out}")
  message(FATAL_ERROR "output differs from ${name}; got (also in ${PROGRAM}.actual):\n${out}")
endif()
