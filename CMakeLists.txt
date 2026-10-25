cmake_minimum_required(VERSION 3.25)
project(chart_grabber LANGUAGES CXX C VERSION 2026.10.11)  # C: third_party/geodesic/geodesic.c (vendored, T-0016)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(CG_ENABLE_ASAN "Build with Address/UB sanitizers" OFF)
option(CG_ENABLE_TSAN "Build with ThreadSanitizer" OFF)
option(CG_BUILD_TESTS "Build unit tests" ON)

# T-0090: every compile goes through ccache (C and C++). CCACHE_BASEDIR / CCACHE_NOHASHDIR come from the presets'
# "environment" (CMakePresets.json) or tools/gate_env.sh, so worktrees at different paths share entries.
# CCACHE_DISABLE=1 in the environment (tools/validate.sh --no-cache, the control run) means no launcher at all.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND NOT "$ENV{CCACHE_DISABLE}" STREQUAL "1")
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
  set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()
# Path independence of the objects: __FILE__ (gtest macros) and debug-info directories no longer contain the
# worktree path, so a cache hit from another worktree is byte-identical to a local compile.
add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)

add_library(cg_warnings INTERFACE)
target_compile_options(cg_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror -Wshadow -Wnon-virtual-dtor -Wold-style-cast)
if(CG_ENABLE_ASAN)
  target_compile_options(cg_warnings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(cg_warnings INTERFACE -fsanitize=address,undefined)
endif()
if(CG_ENABLE_TSAN)
  target_compile_options(cg_warnings INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(cg_warnings INTERFACE -fsanitize=thread)
endif()

# Qt floor 6.4 (ADR-0018)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Widgets Qml)

# cg::core carries the build id in version.h.
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  OUTPUT_VARIABLE CG_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
endif()
if(NOT CG_GIT_HASH)
  set(CG_GIT_HASH "unknown")
endif()

# cg::Result<T> is std::expected. libstdc++ only exposes <expected> in C++23 mode, which is why
# CMAKE_CXX_STANDARD is 23 above (T-0002A open question: CHARTER section 5 still says C++20).
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-std=c++23")
check_cxx_source_compiles("#include <expected>\nint main(){ return __cpp_lib_expected > 0 ? 0 : 1; }"
                          CG_HAVE_STD_EXPECTED)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT CG_HAVE_STD_EXPECTED)
  message(FATAL_ERROR "cg::core requires <expected> (GCC 13+ / libstdc++ 13+)")
endif()

# Vendored third-party sources (CityHash). Must come before src/.
add_subdirectory(third_party)

# Modules are added in dependency order; each is a static library `cg_<name>` with namespace cg::<name>.
# Core modules must NOT link Qt6::Widgets (enforced by tools/validate.sh).
add_subdirectory(src)

if(CG_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  add_subdirectory(tests)
endif()

# --- cli (T-0193) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cli/CMakeLists.txt")
  add_subdirectory(cli)  # T-0193
endif()
if(NOT WIN32)
  include(cmake/cli_package.cmake)  # T-0204: the .deb; Windows ships a zip made by tools/vm/package_win.sh (T-0335)
endif()
