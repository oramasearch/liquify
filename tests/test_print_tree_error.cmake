# SPDX-License-Identifier: MIT

if(NOT DEFINED LIQUIFY_EXECUTABLE)
  message(FATAL_ERROR "LIQUIFY_EXECUTABLE is required")
endif()

if(NOT DEFINED TEST_DIRECTORY)
  message(FATAL_ERROR "TEST_DIRECTORY is required")
endif()

set(REPOSITORY_DIRECTORY "${TEST_DIRECTORY}/unreadable-repository")
file(REMOVE_RECURSE "${REPOSITORY_DIRECTORY}")
file(MAKE_DIRECTORY "${REPOSITORY_DIRECTORY}")
file(WRITE "${REPOSITORY_DIRECTORY}/LIQUID_REPO" "not-a-liquid-digest\n")

execute_process(
  COMMAND "${LIQUIFY_EXECUTABLE}" --print-tree "${REPOSITORY_DIRECTORY}"
  RESULT_VARIABLE RESULT
  OUTPUT_VARIABLE OUTPUT
  ERROR_VARIABLE ERROR)

if("${RESULT}" STREQUAL "0")
  message(FATAL_ERROR "--print-tree succeeded for an unreadable repository")
endif()

set(EXPECTED_ERROR "liquify: not-a-liquid-digest: unknown digest algorithm")
string(FIND "${ERROR}" "${EXPECTED_ERROR}" ERROR_POSITION)
if(ERROR_POSITION EQUAL -1)
  message(FATAL_ERROR "--print-tree did not report the liquid error: ${ERROR}")
endif()
