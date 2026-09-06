add_executable(liquify-tests
  test_ai.cpp
  test_app_state.cpp
  test_command.cpp
  test_editor_buffer.cpp
  test_editor_graph.cpp
  test_interactive_analysis.cpp
  test_plugin.cpp
  test_repository_view.cpp
  test_review.cpp
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
  LIQUIFY_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(liquify-tests PRIVATE
  LIQUIFY_SAMPLE_PLUGIN="$<TARGET_FILE:liquify-sample-plugin>"
  LIQUIFY_C_COMPILER="${CMAKE_C_COMPILER}"
  LIQUIFY_EXECUTABLE="$<TARGET_FILE:liquify>"
  LIQUIFY_CXX_COMPILER="${CMAKE_CXX_COMPILER}")
add_dependencies(liquify-tests liquify)
target_include_directories(liquify-tests PRIVATE
  ${CMAKE_SOURCE_DIR}/modules/corosio/include)
include(GoogleTest)
gtest_discover_tests(liquify-tests)

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
