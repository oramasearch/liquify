add_executable(liquify-tests
  test_editor_buffer.cpp
  test_repository_view.cpp
  test_text_layout.cpp
  test_stub_tree.cpp
  test_version.cpp)
target_link_libraries(liquify-tests PRIVATE liquify-core GTest::gtest_main)
target_compile_definitions(liquify-tests PRIVATE LIQUIFY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
include(GoogleTest)
gtest_discover_tests(liquify-tests)

add_test(
  NAME liquify-print-tree-unreadable
  COMMAND ${CMAKE_COMMAND}
          -DLIQUIFY_EXECUTABLE=$<TARGET_FILE:liquify>
          -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/test_print_tree_error.cmake)
