#pragma once

/* The subdivision of one polygon by what lies on it (plan
 * 2026-11-25-exact-booleans §4.3). */

#include "ubertopo/csg/pairs.h"

namespace ubertopo::csg {

/** A directed piece of a line between consecutive points. */
struct HalfEdge {
  uint32_t from, to; /* points */
  uint32_t cut;      /* the line is the facet's plane and this one */
};

/** A connected open piece of a polygon. Its boundary runs with the facet on
 * the left, seen from where the plane's normal points: cycles around it or
 * around holes, with slits run twice. */
struct PolygonFacet {
  litestl::util::Vector<HalfEdge, 8> edges;
  litestl::util::Vector<uint32_t, 4> off;  /* cycles, as offsets into edges */
  litestl::util::Vector<uint32_t, 1> dots; /* isolated points */
};

/** Cuts polygon @p g by @p cuts (its own edges are added). */
CsgCode subdivide(Input &in, const Polygon &g, const PolygonCuts &cuts,
                  litestl::util::Vector<PolygonFacet> &out);

} // namespace ubertopo::csg
