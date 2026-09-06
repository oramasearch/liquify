execute_process(
  COMMAND "${LIQUIFY_EXECUTABLE}" --command help.version
  RESULT_VARIABLE version_result
  OUTPUT_VARIABLE version_output
  ERROR_VARIABLE version_error)
if(NOT version_result EQUAL 0 OR NOT version_output MATCHES "^liquify .+ \\(libliquid .+\\)\n$")
  message(FATAL_ERROR
    "headless version command failed (${version_result}): ${version_output}${version_error}")
endif()

execute_process(
  COMMAND "${LIQUIFY_EXECUTABLE}" --command missing.command
  RESULT_VARIABLE missing_result
  OUTPUT_VARIABLE missing_output
  ERROR_VARIABLE missing_error)
if(NOT missing_result EQUAL 2 OR NOT missing_error MATCHES "command is not registered")
  message(FATAL_ERROR
    "unknown headless command was not rejected (${missing_result}): ${missing_output}${missing_error}")
endif()

execute_process(
  COMMAND "${LIQUIFY_EXECUTABLE}" docs commands --check
          "${LIQUIFY_SOURCE_DIR}/docs/ui/commands.md"
  RESULT_VARIABLE docs_result
  OUTPUT_VARIABLE docs_output
  ERROR_VARIABLE docs_error)
if(NOT docs_result EQUAL 0)
  message(FATAL_ERROR
    "command reference check failed (${docs_result}): ${docs_output}${docs_error}")
endif()

execute_process(
  COMMAND "${LIQUIFY_EXECUTABLE}" docs keymap --check
          "${LIQUIFY_SOURCE_DIR}/docs/ui/default-keymap.liquid"
  RESULT_VARIABLE keymap_result
  OUTPUT_VARIABLE keymap_output
  ERROR_VARIABLE keymap_error)
if(NOT keymap_result EQUAL 0)
  message(FATAL_ERROR
    "default keymap check failed (${keymap_result}): ${keymap_output}${keymap_error}")
endif()
