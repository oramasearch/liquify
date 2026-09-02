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

if(APPLE)
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
# liquify
# -----------------------------------------------------------------------------
add_library(liquify-core STATIC src/version.cpp)
target_include_directories(liquify-core PUBLIC include)

add_library(liquify-app STATIC src/app.cpp)
target_include_directories(liquify-app PUBLIC include)
target_link_libraries(liquify-app PUBLIC liquify-core PRIVATE imgui)

add_executable(liquify src/main.cpp)
target_link_libraries(liquify PRIVATE liquify-app)

if(LIQUIFY_BUILD_TESTS)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()
  add_subdirectory(tests)
endif()
