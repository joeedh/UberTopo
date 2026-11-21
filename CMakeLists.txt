cmake_minimum_required(VERSION 3.20)
project(UberTopo C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands")

# Determinism contract (docs/design/core-data-model/04 §8): no FMA contraction
# anywhere, never fast-math.
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  add_compile_options(/fp:precise)
  # The CRT marks portable C functions (getenv, fopen) deprecated in favour of
  # its own _s variants.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-ffp-contract=off -fno-fast-math)
endif()

# One sanitizer for every target, litestl included (TSan needs every object
# instrumented). See docs/building.md for the presets that set it.
set(UT_SANITIZE "" CACHE STRING "Sanitizer for every target: address, undefined or thread")
if (UT_SANITIZE)
  if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    if (NOT UT_SANITIZE STREQUAL "address")
      message(FATAL_ERROR "UT_SANITIZE=${UT_SANITIZE}: MSVC-style drivers support only address")
    endif()
    add_compile_options(/fsanitize=address)
  else()
    set(_ut_san -fsanitize=${UT_SANITIZE})
    if (UT_SANITIZE STREQUAL "address")
      list(APPEND _ut_san -fno-omit-frame-pointer)
    elseif (UT_SANITIZE STREQUAL "undefined")
      list(APPEND _ut_san -fno-sanitize-recover=undefined)
    endif()
    add_compile_options(${_ut_san})
    add_link_options(${_ut_san})
  endif()
endif()

include(CTest)
enable_testing()

# litestl (git submodule, DEC-003). Its own tests are built only on request
# (CI does, so litestl changes get the same configurations as UberTopo).
option(UT_LITESTL_TESTS "Also build and register litestl's own tests" OFF)
set(LITESTL_BUILD_TESTS ${UT_LITESTL_TESTS} CACHE BOOL "" FORCE)
add_subdirectory(source/litestl)

add_subdirectory(source/ubertopo)
