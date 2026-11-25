#pragma once

/* Cuts a face that the complex can't hold along diagonals (plan
 * 2026-11-25-exact-booleans §4.5). */

#include "ubertopo/csg/planar.h"

namespace ubertopo::csg {

/** A segment of a face's boundary. */
struct BoundarySegment {
  uint32_t p, q, cut;
  uint8_t out; /* of PlanarGraph::add */
};

struct Diagonal {
  uint32_t p, q;
};

/** Whether a face can be this cell: simple, disjoint loops and nothing inside. */
bool is_legal(const PolygonFacet &cell);

/** The face with the given boundary and isolated points, as one or more legal
 * cells. Diagonals run between its points. */
CsgCode make_legal(const Input &in, uint32_t plane, int axis, std::span<const BoundarySegment> boundary,
                   std::span<const uint32_t> dots, litestl::util::Vector<PolygonFacet> &cells,
                   litestl::util::Vector<Diagonal> &diagonals);

} // namespace ubertopo::csg
