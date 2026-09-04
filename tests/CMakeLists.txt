add_executable(liquify-tests
  test_repository_view.cpp
  test_version.cpp)
target_link_libraries(liquify-tests PRIVATE liquify-core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(liquify-tests)
