cmake_minimum_required(VERSION 3.28)

# liquify is C++26 on the same coroutine stack as Zetta (Capy/Corosio/Beast2), which
# needs Clang 17+ — the same pre-project() auto-selection Zetta and liquid use.
if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT LIQUIFY_USE_SYSTEM_COMPILER)
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin"
     AND EXISTS "/opt/homebrew/opt/llvm/bin/clang++")
    set(CMAKE_C_COMPILER   "/opt/homebrew/opt/llvm/bin/clang"   CACHE FILEPATH "")
    set(CMAKE_CXX_COMPILER "/opt/homebrew/opt/llvm/bin/clang++" CACHE FILEPATH "")
    if(NOT CMAKE_OSX_SYSROOT)
      execute_process(COMMAND xcrun --show-sdk-path
                      OUTPUT_VARIABLE LIQUIFY_SDKROOT OUTPUT_STRIP_TRAILING_WHITESPACE)
      set(CMAKE_OSX_SYSROOT "${LIQUIFY_SDKROOT}" CACHE PATH "")
    endif()
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    foreach(LIQUIFY_VER 22 21 20 19 18 17)
      find_program(LIQUIFY_CLANGXX_${LIQUIFY_VER} "clang++-${LIQUIFY_VER}")
      if(LIQUIFY_CLANGXX_${LIQUIFY_VER})
        set(CMAKE_CXX_COMPILER "${LIQUIFY_CLANGXX_${LIQUIFY_VER}}" CACHE FILEPATH "")
        find_program(LIQUIFY_CLANGC_${LIQUIFY_VER} "clang-${LIQUIFY_VER}")
        if(LIQUIFY_CLANGC_${LIQUIFY_VER})
          set(CMAKE_C_COMPILER "${LIQUIFY_CLANGC_${LIQUIFY_VER}}" CACHE FILEPATH "")
        endif()
        break()
      endif()
    endforeach()
  endif()
endif()

project(liquify VERSION 0.0.1 LANGUAGES C CXX
        DESCRIPTION "liquify — the cross-platform IDE for liquid")

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LIQUIFY_BUILD_TESTS "Build the test suite" ON)
option(LIQUIFY_NATIVE_WINDOW_TESTS "Test real windows (requires a desktop session)" ${APPLE})

find_program(LIQUIFY_NODE_EXECUTABLE NAMES node nodejs)
if(LIQUIFY_NODE_EXECUTABLE)
  add_custom_target(liquify-token-gate
    COMMAND ${LIQUIFY_NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/design/test-token-gates.mjs
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking the stamped Orama design-system snapshot")
elseif(LIQUIFY_BUILD_TESTS)
  message(FATAL_ERROR "Node.js is required when LIQUIFY_BUILD_TESTS is ON")
endif()

if(APPLE)
  enable_language(OBJCXX)
  # SDL3 + ImGui are fine at macOS 12+. Native architecture by default.
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wno-deprecated-declarations)  # SDL3 on macOS; not our code
endif()

# -----------------------------------------------------------------------------
# Fetched dependencies — the same window/UI stack Zetta's headed mode uses
# (SDL3 static + Dear ImGui with the SDL_Renderer backend), so liquify starts
# from a proven cross-platform base. No Zetta code is used; only its choices.
# -----------------------------------------------------------------------------
include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        release-3.2.0
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(SDL3)

# Dear ImGui ships no CMakeLists; build our own static target from its sources.
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.5
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC SDL3::SDL3-static)
target_compile_options(imgui PRIVATE -w)

# -----------------------------------------------------------------------------
# libliquid — liquify is a client of liquid's library, pinned as a submodule.
# Library only: no CLI, no liquid tests in this build.
# -----------------------------------------------------------------------------
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
set(LIQUID_BUILD_CLI   OFF CACHE BOOL "" FORCE)
set(LIQUID_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIQUID_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(LIQUID_GRAMMARS "c;c++" CACHE STRING "" FORCE)
add_subdirectory(modules/liquid EXCLUDE_FROM_ALL)

# -----------------------------------------------------------------------------
# tree-sitter — pinned parser runtime and generated C/C++ grammars.
# -----------------------------------------------------------------------------
add_library(tree-sitter-runtime STATIC modules/tree-sitter/lib/src/lib.c)
target_include_directories(tree-sitter-runtime PUBLIC modules/tree-sitter/lib/include)
target_include_directories(tree-sitter-runtime PRIVATE modules/tree-sitter/lib/src)

add_library(tree-sitter-c-parser STATIC modules/tree-sitter-c/src/parser.c)
target_include_directories(tree-sitter-c-parser PRIVATE modules/tree-sitter/lib/include)
target_link_libraries(tree-sitter-c-parser PUBLIC tree-sitter-runtime)

add_library(tree-sitter-cpp-parser STATIC
  modules/tree-sitter-cpp/src/parser.c
  modules/tree-sitter-cpp/src/scanner.c)
target_include_directories(tree-sitter-cpp-parser PRIVATE modules/tree-sitter/lib/include)
target_link_libraries(tree-sitter-cpp-parser PUBLIC tree-sitter-runtime)

# -----------------------------------------------------------------------------
# liquify
# -----------------------------------------------------------------------------
add_library(liquify-core STATIC
  modules/liquid/src/encoding/cbor.cpp
  src/ai_config.cpp
  src/ai_provider.cpp
  src/assistant.cpp
  src/app_state.cpp
  src/command.cpp
  src/context_builder.cpp
  src/editor_buffer.cpp
  src/editor_model.cpp
  src/editor_graph.cpp
  src/input.cpp
  src/first_run.cpp
  src/interactive_analysis.cpp
  src/panel.cpp
  src/navigation.cpp
  src/multiplayer.cpp
  src/plugin.cpp
  src/repository_view.cpp
  src/review.cpp
  src/settings.cpp
  src/screenshot.cpp
  src/structural_edit.cpp
  src/text_layout.cpp
  src/stub_tree.cpp
  src/version.cpp)
target_include_directories(liquify-core
  PUBLIC include
  PRIVATE modules/corosio/include modules/http/include modules/liquid/src)
target_link_libraries(liquify-core
  PUBLIC liquid::liquid
  PRIVATE Boost::corosio Boost::corosio_openssl Boost::http Boost::json
          tree-sitter-c-parser tree-sitter-cpp-parser ZLIB::ZLIB)
if(APPLE)
  target_link_libraries(liquify-core PRIVATE "-framework Security")
elseif(WIN32)
  target_link_libraries(liquify-core PRIVATE Advapi32)
endif()
target_link_libraries(liquify-core PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(liquify-core liquid-frontend-tree-sitter)

add_library(liquify-sample-plugin-objects OBJECT plugins/sample/sample_plugin.cpp)
set_target_properties(liquify-sample-plugin-objects PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON
  VISIBILITY_INLINES_HIDDEN ON)
target_include_directories(liquify-sample-plugin-objects PRIVATE
  modules/liquid/include plugins/sample)
add_library(liquify-sample-plugin MODULE $<TARGET_OBJECTS:liquify-sample-plugin-objects>)
set_target_properties(liquify-sample-plugin PROPERTIES
  PREFIX ""
  OUTPUT_NAME "liquify-sample-plugin")
add_library(liquify-sample-plugin-static STATIC
  $<TARGET_OBJECTS:liquify-sample-plugin-objects>)
target_include_directories(liquify-sample-plugin-static PUBLIC plugins/sample)

option(LIQUIFY_BUILD_BENCH "Build stub-tree benchmarks" OFF)
if(LIQUIFY_BUILD_BENCH)
  add_executable(liquify-stub-tree-bench bench/stub_tree_bench.cpp)
  target_link_libraries(liquify-stub-tree-bench PRIVATE liquify-core)
  add_executable(liquify-interactive-analysis-bench
    bench/interactive_analysis_bench.cpp)
  target_link_libraries(liquify-interactive-analysis-bench PRIVATE liquify-core)
  add_executable(liquify-editor-graph-bench bench/editor_graph_bench.cpp)
  target_link_libraries(liquify-editor-graph-bench PRIVATE liquify-core)
  add_executable(liquify-ai-context-bench bench/ai_context_bench.cpp)
  target_link_libraries(liquify-ai-context-bench PRIVATE liquify-core)
  add_executable(liquify-review-bench bench/review_bench.cpp)
  target_link_libraries(liquify-review-bench PRIVATE liquify-core)
  add_executable(liquify-structural-edit-bench bench/structural_edit_bench.cpp)
  target_link_libraries(liquify-structural-edit-bench PRIVATE liquify-core)
endif()

add_library(liquify-app STATIC src/app.cpp)
target_include_directories(liquify-app PUBLIC include)
target_link_libraries(liquify-app PUBLIC liquify-core PRIVATE imgui)
target_compile_definitions(liquify-app PRIVATE
  LIQUIFY_TREE_SITTER_FRONTEND="$<TARGET_FILE:liquid-frontend-tree-sitter>")
if(APPLE)
  set_source_files_properties(src/app.cpp PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(liquify-app PRIVATE "-framework AppKit")
endif()

add_executable(liquify src/main.cpp)
target_link_libraries(liquify PRIVATE liquify-app)
add_custom_command(TARGET liquify POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:liquify>/frontends"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:liquid-frontend-tree-sitter>"
          "$<TARGET_FILE_DIR:liquify>/frontends/$<TARGET_FILE_NAME:liquid-frontend-tree-sitter>")

if(LIQUIFY_BUILD_TESTS)
  if(LIQUIFY_NATIVE_WINDOW_TESTS)
    target_compile_definitions(liquify-app PRIVATE LIQUIFY_NATIVE_WINDOW_TESTS)
  endif()
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()
  add_subdirectory(tests)
  add_test(
    NAME liquify-token-gate
    COMMAND ${LIQUIFY_NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/design/test-token-gates.mjs)
endif()
