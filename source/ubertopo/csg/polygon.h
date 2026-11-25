#pragma once

/* Exact point-in-polygon in the plane of an input polygon, for points and
 * midpoints (plan 2026-11-25-exact-booleans §3, §4.2). */

#include "ubertopo/csg/input.h"

namespace ubertopo::csg {

/** Sign of p - v on one axis; P is HPoint or Mid. */
template <typename P> int compare_int(const P &p, int axis, int64_t v)
{
  using W = std::remove_cvref_t<decltype(p.w)>;
  return (p.x[axis] - W::from(v) * p.w).sign();
}

/** Orientation of (a, b, p) projected onto axes (u, v). */
template <typename P> int edge_side(const I3 &a, const I3 &b, const P &p, int u, int v)
{
  using W = std::remove_cvref_t<decltype(p.w)>;
  const W du = W::from(b[u] - a[u]), dv = W::from(b[v] - a[v]);
  const W pu = p.x[u] - W::from(a[u]) * p.w, pv = p.x[v] - W::from(a[v]) * p.w;
  return (du * pv - dv * pu).sign();
}

/** +1 inside, 0 on the boundary, -1 outside; p is in the polygon's plane. */
template <typename P> int point_in_polygon(const Input &in, const Polygon &g, const P &p)
{
  const int u = (g.axis + 1) % 3, v = (g.axis + 2) % 3;
  bool inside = false, boundary = false;
  g.for_each_edge([&](uint32_t i, uint32_t j) {
    if (boundary) {
      return;
    }
    const I3 &a = in.ipos[int(g.pts[int(i)])], &b = in.ipos[int(g.pts[int(j)])];
    const int av = compare_int(p, v, a[v]), bv = compare_int(p, v, b[v]); /* p - a, p - b */
    if (av * bv > 0) {
      return; /* both above, or both below */
    }
    const int s = edge_side(a, b, p, u, v);
    if (s == 0) {
      /* on the line, and (or the line is horizontal) within its v range */
      const int au = compare_int(p, u, a[u]), bu = compare_int(p, u, b[u]);
      boundary = au * bu <= 0;
      return;
    }
    /* half-open in v: a ray toward +u */
    if ((av < 0) != (bv < 0)) {
      inside = inside != ((s > 0) == (bv < 0));
    }
  });
  return boundary ? 0 : inside ? 1 : -1;
}

} // namespace ubertopo::csg
