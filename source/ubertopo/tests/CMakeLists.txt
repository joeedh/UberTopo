# ut_test(<file.cc> [libs...]): one executable per test file, registered with ctest.
function(ut_test file)
  get_filename_component(name ${file} NAME_WE)
  add_executable(${name} ${file})
  # platform is a litestl OBJECT library: its objects must be linked into the
  # executable itself (static archives listed before util can't supply them).
  target_link_libraries(${name} PRIVATE ${ARGN} platform)
  # "source/" is the include root: #include "litestl/..." and "ubertopo/..."
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/source)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ut_test(test_smoke_litestl.cc predicates math util platform)
ut_test(test_smoke_attr.cc attr util math platform)
ut_test(test_geom_predicates.cc ubertopo_geom)
ut_test(test_ident.cc ubertopo_ident)

# Compile-fail tests (litestl's pattern): each snippet must be rejected with
# the expected diagnostic; the control snippets must compile.
if (NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(UT_CF_FLAGS -std=c++20 -fsyntax-only -I${PROJECT_SOURCE_DIR}/source
    -I${PROJECT_SOURCE_DIR}/source/litestl -I${PROJECT_SOURCE_DIR}/source/litestl/..)
  function(ut_compile_ok name)
    add_test(NAME compile_ok_${name}
      COMMAND ${CMAKE_CXX_COMPILER} ${UT_CF_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/${name}.cc)
  endfunction()
  function(ut_compile_fail name regex)
    add_test(NAME compile_fail_${name}
      COMMAND ${CMAKE_CXX_COMPILER} ${UT_CF_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/${name}.cc)
    set_tests_properties(compile_fail_${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
  endfunction()
  ut_compile_ok(ident_control)
  ut_compile_fail(ref_cross_kind "no matching function|could not convert|cannot convert|no viable conversion")
  ut_compile_fail(ref_implicit_index "conversion from|no viable conversion|cannot convert|copy-initialization")
endif()
ut_test(test_topo_domain.cc ubertopo_topo)
ut_test(test_topo_builder.cc ubertopo_topo)
ut_test(test_topo_radial.cc ubertopo_topo)
ut_test(test_topo_validate.cc ubertopo_topo)
ut_test(test_geom_cdt.cc ubertopo_geom)
