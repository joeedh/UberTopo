#include "ubertopo/csg/triangulate.h"

#include <algorithm>
#include <cmath>

#include "ubertopo/csg/planar.h"

namespace ubertopo::csg {

namespace {

using litestl::util::Vector;

struct Triangulator {
  const Arrangement &a;
  const ResultFace &f;
  PlanarGraph graph;

  struct Segment {
    uint32_t p, q; /* points */
    double lo[3], hi[3];
  };
  struct Corner {
    uint32_t p, prev, next; /* points; the face on the left, seen from the normal */
  };
  Vector<Segment> segments; /* the boundary, then the diagonals */
  Vector<Corner> corners;   /* in lexicographic order */

  Triangulator(const Arrangement &a_, const ResultFace &f_)
      : a(a_), f(f_), graph(a_.in, a_.in.polygons[int(f_.source)].plane, a_.in.polygons[int(f_.source)].axis)
  {
  }

  /* a box that contains the segment, for culling */
  Segment segment(uint32_t p, uint32_t q) const
  {
    Segment s;
    s.p = p;
    s.q = q;
    for (int i = 0; i < 3; i++) {
      const double x = double(floor_at(a.in.points[p], i, 0)), y = double(floor_at(a.in.points[q], i, 0));
      s.lo[i] = std::min(x, y);
      s.hi[i] = std::max(x, y) + 1;
    }
    return s;
  }
  static bool apart(const Segment &s, const Segment &t)
  {
    for (int i = 0; i < 3; i++) {
      if (s.hi[i] < t.lo[i] || t.hi[i] < s.lo[i]) {
        return true;
      }
    }
    return false;
  }

  void gather()
  {
    for (int l = 0; l + 1 < int(f.off.size()); l++) {
      const uint32_t b = f.off[l], n = f.off[l + 1] - b;
      for (uint32_t i = 0; i < n; i++) {
        const uint32_t p = f.pts[int(b + i)], next = f.pts[int(b + (i + 1) % n)], prev = f.pts[int(b + (i + n - 1) % n)];
        segments.append(segment(p, next));
        corners.append(f.front > 0 ? Corner{p, prev, next} : Corner{p, next, prev});
      }
    }
    std::sort(corners.begin(), corners.end(), [&](const Corner &x, const Corner &y) {
      return lex_compare(a.in.points[x.p], a.in.points[y.p]) < 0;
    });
  }

  /* the direction to q starts into the face at corner c */
  bool starts_inside(const Corner &c, uint32_t q) const
  {
    const bool l1 = graph.turn(c.p, c.next, q) > 0, l2 = graph.turn(c.prev, c.p, q) > 0;
    return graph.turn(c.prev, c.p, c.next) > 0 ? l1 && l2 : l1 || l2;
  }

  bool free(const Corner &c, const Corner &d) const
  {
    const uint32_t p = c.p, q = d.p;
    if (!starts_inside(c, q) || !starts_inside(d, p)) {
      return false;
    }
    const Segment s = segment(p, q);
    for (const Segment &t : segments) {
      if (apart(s, t)) {
        continue;
      }
      if ((t.p == p && t.q == q) || (t.p == q && t.q == p)) {
        return false;
      }
      for (const uint32_t x : {t.p, t.q}) {
        if (x != p && x != q && graph.turn(p, q, x) == 0 && graph.between(p, q, x)) {
          return false;
        }
      }
      if (t.p != p && t.p != q && t.q != p && t.q != q && graph.turn(p, q, t.p) * graph.turn(p, q, t.q) < 0 &&
          graph.turn(t.p, t.q, p) * graph.turn(t.p, t.q, q) < 0)
      {
        return false;
      }
    }
    return true;
  }

  CsgCode run(Vector<PointTriangle> &out)
  {
    gather();
    const int boundary = int(segments.size());
    /* (a polygon with h holes takes n - 3 + 3 h diagonals) */
    const int want = boundary - 3 + 3 * (int(f.off.size()) - 2);
    for (int i = 0; i < int(corners.size()) && int(segments.size()) < boundary + want; i++) {
      for (int j = i + 1; j < int(corners.size()) && int(segments.size()) < boundary + want; j++) {
        if (free(corners[i], corners[j])) {
          segments.append(segment(corners[i].p, corners[j].p));
        }
      }
    }
    for (int i = 0; i < int(segments.size()); i++) {
      graph.add(segments[i].p, segments[i].q, kNone, i >= boundary ? 0 : f.front > 0 ? kRightOut : kLeftOut);
    }
    Vector<PolygonFacet> cells;
    if (graph.cells({}, cells) != CsgCode::Ok) {
      return CsgCode::Inconsistent;
    }
    for (const PolygonFacet &c : cells) {
      if (c.edges.size() != 3) {
        return CsgCode::Inconsistent;
      }
      const uint32_t x = c.edges[0].from, y = c.edges[1].from, z = c.edges[2].from;
      out.append(f.front > 0 ? PointTriangle{x, y, z} : PointTriangle{x, z, y});
    }
    return CsgCode::Ok;
  }
};

} // namespace

CsgCode triangulate_face(const Arrangement &a, const ResultFace &f, Vector<PointTriangle> &out)
{
  out.clear();
  if (!f.whole) {
    Triangulator t(a, f);
    return t.run(out);
  }
  const Outline &o = a.in.outlines[int(a.in.polygons[int(f.source)].outline)];
  for (uint32_t i = o.first; i < o.first + o.count; i++) {
    const auto &p = a.in.polygons[int(i)].pts;
    out.append(f.reversed ? PointTriangle{p[0], p[2], p[1]} : PointTriangle{p[0], p[1], p[2]});
  }
  return CsgCode::Ok;
}

} // namespace ubertopo::csg
