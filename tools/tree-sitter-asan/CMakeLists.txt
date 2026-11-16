# SPDX-License-Identifier: MIT
#
# AddressSanitizer harness for tree-sitter and the pinned C and C++ grammars (liquify#92).
# A standalone project, configured into its own build directory, so the sanitizer flags never
# reach liquify's own build. See docs/build/tree-sitter-asan.md.
cmake_minimum_required(VERSION 3.28)
project(liquify_tree_sitter_asan LANGUAGES C CXX)

set(LIQUIFY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(TS_ROOT "${LIQUIFY_ROOT}/modules/tree-sitter")
set(TS_C_ROOT "${LIQUIFY_ROOT}/modules/tree-sitter-c")
set(TS_CPP_ROOT "${LIQUIFY_ROOT}/modules/tree-sitter-cpp")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(LIQUIFY_ASAN_FLAGS -fsanitize=address -fno-omit-frame-pointer -fsanitize-recover=address)
add_compile_options(${LIQUIFY_ASAN_FLAGS})
add_link_options(-fsanitize=address)

# The same sources liquify's own CMakeLists.txt compiles, with the sanitizer added.
add_library(ts-runtime-asan STATIC "${TS_ROOT}/lib/src/lib.c")
target_include_directories(ts-runtime-asan PUBLIC "${TS_ROOT}/lib/include")
target_include_directories(ts-runtime-asan PRIVATE "${TS_ROOT}/lib/src")

add_library(ts-c-asan STATIC "${TS_C_ROOT}/src/parser.c")
target_include_directories(ts-c-asan PRIVATE "${TS_ROOT}/lib/include")
target_link_libraries(ts-c-asan PUBLIC ts-runtime-asan)

add_library(ts-cpp-asan STATIC "${TS_CPP_ROOT}/src/parser.c" "${TS_CPP_ROOT}/src/scanner.c")
target_include_directories(ts-cpp-asan PRIVATE "${TS_ROOT}/lib/include")
target_link_libraries(ts-cpp-asan PUBLIC ts-runtime-asan)

add_executable(tree-sitter-asan harness.cpp)
target_compile_features(tree-sitter-asan PRIVATE cxx_std_20)
target_link_libraries(tree-sitter-asan PRIVATE ts-c-asan ts-cpp-asan)
