#pragma once

/* The subdivision of one polygon by what lies on it (plan
 * 2026-11-25-exact-booleans §4.3). */

#include "ubertopo/csg/pairs.h"
#include "ubertopo/csg/planar.h"

namespace ubertopo::csg {

/** Cuts polygon @p g by @p cuts (its own edges are added). */
CsgCode subdivide(Input &in, const Polygon &g, const PolygonCuts &cuts,
                  litestl::util::Vector<PolygonFacet> &out);

} // namespace ubertopo::csg
