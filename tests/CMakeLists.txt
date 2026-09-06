add_executable(liquify-tests
  test_ai.cpp
  test_editor_buffer.cpp
  test_editor_graph.cpp
  test_interactive_analysis.cpp
  test_repository_view.cpp
  test_review.cpp
  test_text_layout.cpp
  test_stub_tree.cpp
  test_version.cpp)
target_link_libraries(liquify-tests PRIVATE liquify-core liquid_forge_double GTest::gtest_main)
target_compile_definitions(liquify-tests PRIVATE LIQUIFY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
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
