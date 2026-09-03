cmake_minimum_required(VERSION 3.16)
project(evosim VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Floating-point determinism flags.
#
# These are not optional polish. Without -ffp-contract=off the compiler is free
# to fuse a*b+c into a single FMA at -O2 and not at -O0, which changes the last
# bit of the result and makes a debug build and a release build disagree on the
# state hash. Fast-math is likewise forbidden: it licenses reassociation, and
# reassociation is exactly the thing this project spends its time preventing.
# ---------------------------------------------------------------------------
add_library(evosim_fp INTERFACE)
if(MSVC)
  target_compile_options(evosim_fp INTERFACE /fp:precise)
else()
  target_compile_options(evosim_fp INTERFACE -ffp-contract=off -fno-fast-math)
endif()

add_library(evosim_warnings INTERFACE)
if(MSVC)
  target_compile_options(evosim_warnings INTERFACE /W4)
else()
  target_compile_options(evosim_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Core library. Never links or includes anything from renderer/: the simulation
# builds and runs on a box with no graphics library present.
# ---------------------------------------------------------------------------
add_library(evosim_core
  src/rng.cpp
  src/config.cpp
  src/world.cpp
  src/telemetry.cpp
  src/spatial_grid.cpp
  src/thread_pool.cpp
)
target_include_directories(evosim_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(evosim_core PUBLIC Threads::Threads evosim_fp PRIVATE evosim_warnings)

add_executable(evosim src/main.cpp)
target_link_libraries(evosim PRIVATE evosim_core evosim_warnings)

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
add_executable(bench_neighbors bench/bench_neighbors.cpp)
target_link_libraries(bench_neighbors PRIVATE evosim_core evosim_warnings)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

set(EVOSIM_TESTS
  test_rng
  test_config
  test_math
  test_reduction
  test_spatial_grid
  test_determinism
  test_thread_invariance
)

foreach(t IN LISTS EVOSIM_TESTS)
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE evosim_core evosim_warnings)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Both determinism tests run thousands of ticks at several thread counts;
# give them room on a loaded box.
set_tests_properties(test_determinism test_thread_invariance PROPERTIES TIMEOUT 900)

message(STATUS "evosim: build type ${CMAKE_BUILD_TYPE}, C++${CMAKE_CXX_STANDARD}")
