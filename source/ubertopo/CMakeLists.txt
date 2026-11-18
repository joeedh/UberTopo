# UberTopo sources. Modules are added here as they land (docs/design/core-data-model/06).
# "source/" is the include root: #include "litestl/..." and "ubertopo/...".

# geom/: coordinate types, tile frames, derived exact predicates (header-only).
add_library(ubertopo_geom INTERFACE)
target_include_directories(ubertopo_geom INTERFACE ${PROJECT_SOURCE_DIR}/source)
target_link_libraries(ubertopo_geom INTERFACE predicates math util)

# ident/: persistent IDs, typed references (header-only).
add_library(ubertopo_ident INTERFACE)
target_include_directories(ubertopo_ident INTERFACE ${PROJECT_SOURCE_DIR}/source)
target_link_libraries(ubertopo_ident INTERFACE util)

# topo/: domains, schemas, Complex<C>, builder, regions, validator.
add_library(ubertopo_topo STATIC
  topo/schema.cc
  topo/builder.cc
  topo/validate.cc
  topo/regions.cc
  topo/validate_geom.cc
  topo/freeze.cc
  topo/io.cc
  topo/edit/editor.cc
)
target_link_libraries(ubertopo_topo PUBLIC ubertopo_geom ubertopo_ident attr util)

if (BUILD_TESTING)
  add_subdirectory(tests)
endif()
