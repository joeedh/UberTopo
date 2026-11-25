#pragma once

/* Where two polygons meet (plan 2026-11-25-exact-booleans §4.2). */

#include "ubertopo/csg/polygon.h"

namespace ubertopo::csg {

/** A segment on a polygon: its own edge, or where another one meets it. The
 * line is the polygon's plane and @p cut. */
struct Cut {
  uint32_t a, b; /* points */
  uint32_t cut;  /* plane */
};

/** What lies on one polygon. */
struct PolygonCuts {
  litestl::util::Vector<Cut, 8> segments;
  litestl::util::Vector<uint32_t, 2> dots; /* single points of contact */
};

/** A plane through edge (@p a, @p b) of polygon @p g (or lying in its plane),
 * across its plane. */
inline uint32_t cut_plane(Input &in, const Polygon &g, uint32_t a, uint32_t b)
{
  Plane p;
  edge_plane(in.ipos[int(a)], in.ipos[int(b)], g.axis, p);
  return in.planes.add(p);
}

/** Adds where @p a and @p b, polygons of different leaves, meet. */
void intersect_polygons(Input &in, uint32_t a, uint32_t b, PolygonCuts &on_a, PolygonCuts &on_b);

/** Sorts distinct points along the line of two planes. */
void sort_along(const Input &in, uint32_t plane, uint32_t cut, litestl::util::Vector<uint32_t, 16> &points);

} // namespace ubertopo::csg
