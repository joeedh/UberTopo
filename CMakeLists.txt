cmake_minimum_required(VERSION 3.20)
project(UberTopo C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands")

# Determinism contract (docs/design/core-data-model/04 §8): no FMA contraction
# anywhere, never fast-math.
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  add_compile_options(/fp:precise)
else()
  add_compile_options(-ffp-contract=off -fno-fast-math)
endif()

include(CTest)
enable_testing()

# litestl (git submodule, DEC-003). Its own tests are not built here.
set(LITESTL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(source/litestl)

add_subdirectory(source/ubertopo)
