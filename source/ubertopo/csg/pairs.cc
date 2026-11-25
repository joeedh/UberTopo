#include "ubertopo/csg/pairs.h"

#include <algorithm>

namespace ubertopo::csg {

namespace {

using Points = litestl::util::Vector<uint32_t, 16>;

int axis_of(const Dir &d)
{
  for (int i = 0; i < 3; i++) {
    if (!d.v[i].is_zero()) {
      return i;
    }
  }
  return 0;
}

void add_unique(Points &v, uint32_t p)
{
  if (std::find(v.begin(), v.end(), p) == v.end()) {
    v.append(p);
  }
}

/* the points of g's boundary on plane q (which isn't g's); false if g lies
 * strictly on one side */
bool boundary_points(Input &in, const Polygon &g, uint32_t q, Points &out)
{
  litestl::util::Vector<int8_t, 16> side;
  bool neg = false, pos = false, on = false;
  for (uint32_t p : g.pts) {
    const int s = classify(in.ipos[int(p)], in.planes[q]);
    side.append(int8_t(s));
    neg = neg || s < 0;
    pos = pos || s > 0;
    on = on || s == 0;
  }
  if (!on && !(neg && pos)) {
    return false;
  }
  g.for_each_edge([&](uint32_t i, uint32_t j) {
    if (side[int(i)] == 0) {
      add_unique(out, g.pts[int(i)]);
    }
    if (side[int(i)] * side[int(j)] < 0) {
      const uint32_t e = cut_plane(in, g, g.pts[int(i)], g.pts[int(j)]);
      HPoint x;
      meet(in.planes[g.plane], in.planes[e], in.planes[q], x);
      add_unique(out, in.points.add(x));
    }
  });
  return true;
}

/* Splits a line at its points; emits the intervals inside both polygons, and
 * the points inside both that aren't on one of these. */
template <typename InA, typename InB, typename Emit, typename Dot>
void intervals(const Input &in, const Points &pts, InA &&in_a, InB &&in_b, Emit &&emit, Dot &&dot)
{
  const int n = int(pts.size());
  bool before = false;
  for (int i = 0; i < n; i++) {
    bool after = false;
    if (i + 1 < n) {
      const Mid m(in.points[pts[i]], in.points[pts[i + 1]]);
      after = in_a(m) && in_b(m);
      if (after) {
        emit(pts[i], pts[i + 1]);
      }
    }
    if (!before && !after) {
      const HPoint &p = in.points[pts[i]];
      if (in_a(p) && in_b(p)) {
        dot(pts[i]);
      }
    }
    before = after;
  }
}

/* the edges of g on the coplanar polygon h */
void coplanar_edges(Input &in, const Polygon &g, const Polygon &h, PolygonCuts &on_h)
{
  g.for_each_edge([&](uint32_t i, uint32_t j) {
    const uint32_t pa = g.pts[int(i)], pb = g.pts[int(j)];
    const I3 a = in.ipos[int(pa)], b = in.ipos[int(pb)];
    for (int x = 0; x < 3; x++) {
      if (std::max(a[x], b[x]) < h.lo[x] || std::min(a[x], b[x]) > h.hi[x]) {
        return;
      }
    }
    const uint32_t cut = cut_plane(in, h, pa, pb);
    Points pts;
    pts.append(pa);
    pts.append(pb);
    h.for_each_edge([&](uint32_t k, uint32_t l) {
      const uint32_t qa = h.pts[int(k)], qb = h.pts[int(l)];
      const int sa = classify(in.ipos[int(qa)], in.planes[cut]), sb = classify(in.ipos[int(qb)], in.planes[cut]);
      if (sa == 0) {
        add_unique(pts, qa);
      }
      if (sa * sb < 0) {
        const uint32_t e = cut_plane(in, h, qa, qb);
        HPoint x;
        meet(in.planes[h.plane], in.planes[e], in.planes[cut], x);
        add_unique(pts, in.points.add(x));
      }
    });
    sort_along(in, h.plane, cut, pts);
    /* only between the edge's ends */
    Points own;
    bool inside = false;
    for (uint32_t p : pts) {
      const bool end = p == pa || p == pb;
      if (inside || end) {
        own.append(p);
      }
      inside = inside != end;
    }
    intervals(
        in, own, [&](const auto &p) { return point_in_polygon(in, h, p) >= 0; }, [](const auto &) { return true; },
        [&](uint32_t p, uint32_t q) { on_h.segments.append({p, q, cut}); }, [&](uint32_t p) { on_h.dots.append(p); });
  });
}

} // namespace

void sort_along(const Input &in, uint32_t plane, uint32_t cut, litestl::util::Vector<uint32_t, 16> &points)
{
  const int axis = axis_of(cross(in.planes[plane].n, in.planes[cut].n));
  std::sort(points.begin(), points.end(),
            [&](uint32_t a, uint32_t b) { return compare(in.points[a], in.points[b], axis) < 0; });
}

void intersect_polygons(Input &in, uint32_t ia, uint32_t ib, PolygonCuts &on_a, PolygonCuts &on_b)
{
  const Polygon &a = in.polygons[int(ia)], &b = in.polygons[int(ib)];
  for (int i = 0; i < 3; i++) {
    if (a.lo[i] > b.hi[i] || b.lo[i] > a.hi[i]) {
      return;
    }
  }
  if (a.plane == b.plane) {
    coplanar_edges(in, a, b, on_b);
    coplanar_edges(in, b, a, on_a);
    return;
  }
  Points pts;
  /* (parallel planes: each polygon is on one side of the other) */
  if (!boundary_points(in, a, b.plane, pts) || !boundary_points(in, b, a.plane, pts)) {
    return;
  }
  sort_along(in, a.plane, b.plane, pts);
  intervals(
      in, pts, [&](const auto &p) { return point_in_polygon(in, a, p) >= 0; },
      [&](const auto &p) { return point_in_polygon(in, b, p) >= 0; },
      [&](uint32_t p, uint32_t q) {
        on_a.segments.append({p, q, b.plane});
        on_b.segments.append({p, q, a.plane});
      },
      [&](uint32_t p) {
        on_a.dots.append(p);
        on_b.dots.append(p);
      });
}

} // namespace ubertopo::csg
