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

# curves/: exact curves with certified bounds (plan P4a); evaluated in double,
# not templated on the coordinate type. detmath: deterministic transcendentals.
add_library(ubertopo_curves STATIC
  curves/poly.cc
  curves/fn1d.cc
  curves/curve.cc
  curves/cesaro.cc
  curves/bspline.cc
  curves/offset.cc
  curves/alignment.cc
  curves/catenary.cc
  curves/pcurve.cc
  curves/polyline.cc
  curves/road.cc
  curves/approx.cc
)
target_include_directories(ubertopo_curves PUBLIC ${PROJECT_SOURCE_DIR}/source)
target_link_libraries(ubertopo_curves PUBLIC detmath attr math util)

# topo/: domains, schemas, Complex<C>, builder, regions, validator.
add_library(ubertopo_topo STATIC
  topo/schema.cc
  topo/builder.cc
  topo/validate.cc
  topo/validate_regions.cc
  topo/regions.cc
  topo/validate_geom.cc
  topo/face_grid.cc
  topo/validate_grid.cc
  topo/validate_local.cc
  topo/compact.cc
  topo/freeze.cc
  topo/io.cc
  topo/io_legacy.cc
  topo/geom_store.cc
  topo/curve_geom.cc
  topo/validate_curves.cc
  topo/validate_surfaces.cc
  topo/edit/editor.cc
)
target_link_libraries(ubertopo_topo PUBLIC ubertopo_curves ubertopo_geom ubertopo_ident attr util)

# csg/: exact Booleans from a CSG tree.
add_library(ubertopo_csg STATIC
  csg/arrangement.cc
  csg/input.cc
  csg/label.cc
  csg/pairs.cc
  csg/subdivide.cc
)
target_link_libraries(ubertopo_csg PUBLIC ubertopo_topo)

if (BUILD_TESTING)
  add_subdirectory(tests)
endif()
