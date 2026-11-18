# UberTopo sources. Modules are added here as they land (docs/design/core-data-model/06).
# "source/" is the include root: #include "litestl/..." and "ubertopo/...".

# geom/: coordinate types, tile frames, derived exact predicates (header-only).
add_library(ubertopo_geom INTERFACE)
target_include_directories(ubertopo_geom INTERFACE ${PROJECT_SOURCE_DIR}/source)
target_link_libraries(ubertopo_geom INTERFACE predicates math util platform)

if (BUILD_TESTING)
  add_subdirectory(tests)
endif()
