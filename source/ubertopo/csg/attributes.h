#pragma once

/* Copies the operands' attributes to a result (plan 2026-11-25-exact-booleans
 * §5). */

#include "ubertopo/csg/names.h"
#include "ubertopo/topo/edit/propagate.h"

namespace ubertopo::csg {

/** Where the elements of a result are in its complex, and come from. */
struct ElementMap {
  std::span<const uint32_t> vert_of; /* by point */
  std::span<const uint32_t> edge_of; /* by result edge */
  std::span<const uint32_t> face_of; /* by result face */
  std::span<const RegionSides> regions;
};

/** Faces, face sides and edges: from the source face or edge, as parts that it
 * is split into. Vertices: from the operand's own vertex. Regions: from the
 * first region with their label. Columns are created for the leaves that
 * something comes from. BadOperand: one name with two types (*bad_leaf: the
 * later one). */
CsgCode copy_attributes(const Arrangement &a, const Result &result, const Names &names, const NameSource &source,
                        const ElementMap &map, const topo::PropagationRules *rules,
                        topo::Complex<geom::Lattice> &k, uint32_t *bad_leaf = nullptr);

} // namespace ubertopo::csg
