#pragma once

/* Rays against the polygons of an evaluation (plan 2026-11-25-exact-booleans
 * §4.4). */

#include "ubertopo/csg/input.h"

namespace ubertopo::csg {

/** Whether the ray from @p o along @p dir hits @p g; @p side: where o is of
 * g's plane. -1: the direction doesn't do, 0: missed, 1: hit. A polygon whose
 * plane o is in isn't hit. */
inline int ray_hits(const Input &in, const Mid &o, const I3 &dir, const Polygon &g, int side)
{
  const int toward = dot(in.planes[g.plane].n, dir).sign();
  if (side == 0) {
    /* the polygon doesn't contain the origin, or starts there. A ray in its
     * plane leaves it at an edge, where the next polygons rule the direction
     * out */
    return 0;
  }
  if (side * toward >= 0) {
    return 0;
  }
  /* the winding number of the boundary, as a fan about its first point */
  const I3 &p0 = in.ipos[int(g.pts[0])];
  int winding = 0;
  bool bad = false;
  g.for_each_edge([&](uint32_t i, uint32_t j) {
    if (bad) {
      return;
    }
    const I3 &p = in.ipos[int(g.pts[int(i)])], &q = in.ipos[int(g.pts[int(j)])];
    const int s = ray_side(o, dir, p, q);
    bad = s == 0;
    if (bad || i == 0 || j == 0) {
      return;
    }
    const int s1 = ray_side(o, dir, p0, p), s2 = ray_side(o, dir, q, p0);
    bad = s1 == 0 || s2 == 0;
    winding += s1 == s && s2 == s ? s : 0;
  });
  return bad ? -1 : winding != 0;
}

} // namespace ubertopo::csg
