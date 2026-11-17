# ut_test(<file.cc> [libs...]): one executable per test file, registered with ctest.
function(ut_test file)
  get_filename_component(name ${file} NAME_WE)
  add_executable(${name} ${file})
  target_link_libraries(${name} PRIVATE ${ARGN})
  # "source/" is the include root: #include "litestl/..." and "ubertopo/..."
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/source)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ut_test(test_smoke_litestl.cc predicates math util platform)
