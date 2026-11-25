#pragma once

/* The arrangement of the polygons of all leaves, and the region of every leaf
 * on both sides of every facet (plan 2026-11-25-exact-booleans §4.2-4.4). */

#include "ubertopo/csg/subdivide.h"

namespace ubertopo::csg {

struct FacetEdge {
  uint32_t edge;
  int8_t dir; /* +1: from the edge's a to b */
  uint32_t cut; /* the edge is on the facet's plane and this one */
};

/** A facet, once for all the polygons it lies on. Its boundary runs with the
 * facet on the left, seen from the plane's positive side. Side 0 is the
 * positive side of the plane, side 1 the negative one. */
struct Facet {
  uint32_t plane;
  litestl::util::Vector<uint32_t, 2> sources; /* polygons, one per leaf at most */
  litestl::util::Vector<FacetEdge, 8> edges;
  litestl::util::Vector<uint32_t, 4> off; /* cycles */
  litestl::util::Vector<uint32_t, 1> dots;
};

/** A facet at an edge. */
struct EdgeUse {
  uint32_t facet;
  int8_t dir;

  /** The side of the facet that follows it, turning counter-clockwise about
   * a -> b; the other one precedes it. */
  uint32_t side_after() const
  {
    return dir > 0 ? 0 : 1;
  }
};

/** A piece of a line between two points, a < b. */
struct ArrEdge {
  uint32_t a, b;
  litestl::util::Vector<EdgeUse, 4> uses; /* counter-clockwise about a -> b */
};

struct ArrangementStats {
  uint32_t pairs = 0, rays = 0, retries = 0;
};

struct Arrangement {
  Input in;
  uint32_t leaf_count = 0;
  litestl::util::Vector<Facet> facets;
  litestl::util::Vector<ArrEdge> edges;
  litestl::util::Vector<uint32_t> regions; /* by facet, leaf and side */
  ArrangementStats stats;

  uint32_t region(uint32_t facet, uint32_t leaf, uint32_t side) const
  {
    return regions[int(2 * (facet * leaf_count + leaf) + side)];
  }
  /** The polygon of @p leaf that facet @p f lies on, or kNone. */
  uint32_t source_of(uint32_t f, uint32_t leaf) const
  {
    for (uint32_t s : facets[int(f)].sources) {
      if (in.polygons[int(s)].leaf == leaf) {
        return s;
      }
    }
    return kNone;
  }
};

/** Cuts the polygons of a.in at each other, and orders facets about edges. */
CsgCode build_arrangement(Arrangement &a);

/** Finds the regions (label.cc). */
CsgCode label_arrangement(Arrangement &a);

/** Reads the operands, and both of the above. */
CsgCode arrange(std::span<const Operand> leaves, Arrangement &a, uint32_t *bad_leaf = nullptr);

} // namespace ubertopo::csg
