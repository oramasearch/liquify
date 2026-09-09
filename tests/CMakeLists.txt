add_executable(liquify-tests
  test_ai.cpp
  test_app_state.cpp
  test_command.cpp
  test_editor_buffer.cpp
  test_editor_model.cpp
  test_editor_graph.cpp
  test_first_run.cpp
  test_interactive_analysis.cpp
  test_input.cpp
  test_panel.cpp
  test_navigation.cpp
  test_multiplayer.cpp
  test_plugin.cpp
  test_repository_view.cpp
  test_repository_error.cpp
  test_review.cpp
  test_settings.cpp
  test_screenshot.cpp
  test_structural_edit.cpp
  test_text_layout.cpp
  test_stub_tree.cpp
  test_version.cpp)
target_link_libraries(liquify-tests PRIVATE
  liquify-app
  liquid_forge_double
  liquify-sample-plugin-static
  GTest::gtest_main)
add_dependencies(liquify-tests liquify-sample-plugin)
target_compile_definitions(liquify-tests PRIVATE LIQUIFY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(liquify-tests PRIVATE
  LIQUIFY_LIQUID_DIR="${LIQUIFY_LIQUID_SOURCE_DIR}")
target_compile_definitions(liquify-tests PRIVATE
  LIQUIFY_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(liquify-tests PRIVATE
  LIQUIFY_SAMPLE_PLUGIN="$<TARGET_FILE:liquify-sample-plugin>"
  LIQUIFY_TREE_SITTER_FRONTEND="$<TARGET_FILE:liquid-frontend-tree-sitter>"
  LIQUIFY_C_COMPILER="${CMAKE_C_COMPILER}"
  LIQUIFY_EXECUTABLE="$<TARGET_FILE:liquify>"
  LIQUIFY_CXX_COMPILER="${CMAKE_CXX_COMPILER}")
add_dependencies(liquify-tests liquify)
target_include_directories(liquify-tests PRIVATE
  ${CMAKE_SOURCE_DIR}/modules/corosio/include)
include(GoogleTest)
gtest_discover_tests(liquify-tests)

add_executable(liquify-compat-editor compat_editor.cpp)
target_link_libraries(liquify-compat-editor PRIVATE liquify-core)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(LIQUIFY_COMPAT_CLI "" CACHE FILEPATH "Separately configured and built supported liquid CLI")
# Read the pinned liquid revision from the liquid checkout rather than repeating it
# here: a literal breaks this test on every deliberate pin bump, which teaches
# the next person that a red compat test is normal.
execute_process(
  COMMAND ${GIT_EXECUTABLE} -C "${LIQUIFY_LIQUID_SOURCE_DIR}" rev-parse HEAD
  OUTPUT_VARIABLE LIQUIFY_PINNED_LIQUID_REVISION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE LIQUIFY_PINNED_LIQUID_RESULT)
if(NOT LIQUIFY_PINNED_LIQUID_RESULT EQUAL 0 OR NOT LIQUIFY_PINNED_LIQUID_REVISION)
  message(FATAL_ERROR "could not read the pinned liquid revision from ${LIQUIFY_LIQUID_SOURCE_DIR}")
endif()
add_test(NAME liquify-independent-cli-compat
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/independent_cli_compat.py
    --ide $<TARGET_FILE:liquify>
    --editor $<TARGET_FILE:liquify-compat-editor>
    --work ${CMAKE_CURRENT_BINARY_DIR}/independent-compat
    --revision ${LIQUIFY_PINNED_LIQUID_REVISION}
    --cli "${LIQUIFY_COMPAT_CLI}")
set_tests_properties(liquify-independent-cli-compat PROPERTIES RUN_SERIAL TRUE)
if(LIQUIFY_NATIVE_WINDOW_TESTS)
  add_executable(liquify-native-acceptance native_acceptance.cpp)
  target_link_libraries(liquify-native-acceptance PRIVATE liquify-app)
  foreach(scenario IN ITEMS launch-1 launch-2 edit quick-open palette modal-input tabs build)
    add_test(NAME liquify-native-acceptance-${scenario}
      COMMAND liquify-native-acceptance ${scenario}
        ${CMAKE_CURRENT_BINARY_DIR}/native-acceptance/${scenario})
    set_tests_properties(liquify-native-acceptance-${scenario} PROPERTIES
      LABELS "native;display;acceptance" RUN_SERIAL TRUE TIMEOUT 120
      ENVIRONMENT "LIQUID_HOME=${CMAKE_CURRENT_BINARY_DIR}/native-acceptance/${scenario}/home")
  endforeach()
  # #39 remains open: retained-editor text input stops after an ImGui field.
  set_tests_properties(liquify-native-acceptance-edit PROPERTIES WILL_FAIL TRUE)
  # #33 remains open: Quick Open has no keyboard result dispatch.
  set_tests_properties(liquify-native-acceptance-quick-open PROPERTIES WILL_FAIL TRUE)
  # #34 remains open: palette keyboard selection does not dispatch a command.
  set_tests_properties(liquify-native-acceptance-palette PROPERTIES WILL_FAIL TRUE)
  # #39 remains open: modal ImGui text can also reach the obscured retained editor.
  set_tests_properties(liquify-native-acceptance-modal-input PROPERTIES WILL_FAIL TRUE)
  set_tests_properties(liquify-native-acceptance-launch-1 PROPERTIES SKIP_RETURN_CODE 77)
  add_executable(liquify-native-window-test test_native_window.cpp)
  target_link_libraries(liquify-native-window-test PRIVATE liquify-app)
  add_test(NAME liquify-native-window
    COMMAND liquify-native-window-test ${CMAKE_CURRENT_BINARY_DIR}/native-window)
  set_tests_properties(liquify-native-window PROPERTIES
    LABELS "native;display" RUN_SERIAL TRUE TIMEOUT 180)
endif()

if(APPLE)
  add_executable(liquify-native-buffer-regression native_buffer_regression.cpp)
  target_link_libraries(liquify-native-buffer-regression PRIVATE liquify-app)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/.cache/t")
  add_test(
    NAME liquify-native-buffer-regression
    COMMAND liquify-native-buffer-regression
            "${CMAKE_CURRENT_BINARY_DIR}/native-buffer-regression-fixture")
  set_tests_properties(
    liquify-native-buffer-regression
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
      ENVIRONMENT
        "LIQUID_HOME=${CMAKE_BINARY_DIR}/.cache/liquify-native-buffer-home;TMPDIR=.cache/t"
      SKIP_RETURN_CODE 77)
endif()

add_test(
  NAME liquify-print-tree-unreadable
  COMMAND ${CMAKE_COMMAND}
          -DLIQUIFY_EXECUTABLE=$<TARGET_FILE:liquify>
          -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/test_print_tree_error.cmake)

add_test(
  NAME liquify-command-cli
  COMMAND ${CMAKE_COMMAND}
          -DLIQUIFY_EXECUTABLE=$<TARGET_FILE:liquify>
          -DLIQUIFY_SOURCE_DIR=${CMAKE_SOURCE_DIR}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/test_command_cli.cmake)
