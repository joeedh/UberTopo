#include "ubertopo/csg/subdivide.h"

#include <algorithm>

namespace ubertopo::csg {

namespace {

using Points = litestl::util::Vector<uint32_t, 16>;

struct Subdivider {
  Input &in;
  const Polygon &g;
  litestl::util::Vector<Cut, 16> segs; /* the first ones are the polygon's own edges */
  Points points;

  void add_point(uint32_t p)
  {
    if (std::find(points.begin(), points.end(), p) == points.end()) {
      points.append(p);
    }
  }

  void gather(const PolygonCuts &cuts)
  {
    g.for_each_edge([&](uint32_t i, uint32_t j) {
      segs.append({g.pts[int(i)], g.pts[int(j)], cut_plane(in, g, g.pts[int(i)], g.pts[int(j)])});
    });
    for (const Cut &c : cuts.segments) {
      segs.append(c);
    }
    for (const Cut &c : segs) {
      add_point(c.a);
      add_point(c.b);
    }
    for (uint32_t p : cuts.dots) {
      add_point(p);
    }
    /* crossings */
    for (int i = 0; i < int(segs.size()); i++) {
      for (int j = i + 1; j < int(segs.size()); j++) {
        const Cut s = segs[i], t = segs[j];
        if (s.cut == t.cut || s.a == t.a || s.a == t.b || s.b == t.a || s.b == t.b) {
          continue;
        }
        const Plane &ps = in.planes[s.cut], &pt = in.planes[t.cut];
        if (classify(in.points[t.a], ps) * classify(in.points[t.b], ps) < 0 &&
            classify(in.points[s.a], pt) * classify(in.points[s.b], pt) < 0)
        {
          HPoint x;
          meet(in.planes[g.plane], ps, pt, x);
          add_point(in.points.add(x));
        }
      }
    }
  }

  /* pieces between consecutive points */
  void split(PlanarGraph &graph)
  {
    for (int i = 0; i < int(segs.size()); i++) {
      const Cut s = segs[i];
      Points on;
      for (uint32_t p : points) {
        if (p == s.a || p == s.b || classify(in.points[p], in.planes[s.cut]) == 0) {
          on.append(p);
        }
      }
      sort_along(in, g.plane, s.cut, on);
      int ia = 0, ib = 0;
      for (int k = 0; k < int(on.size()); k++) {
        ia = on[k] == s.a ? k : ia;
        ib = on[k] == s.b ? k : ib;
      }
      uint8_t out = 0;
      if (i < int(g.pts.size())) {
        /* the loop has the polygon on the left, seen from the front */
        out = (ia < ib) == (g.sign > 0) ? kRightOut : kLeftOut;
      }
      for (int k = std::min(ia, ib); k < std::max(ia, ib); k++) {
        graph.add(on[k], on[k + 1], s.cut, out);
      }
    }
  }
};

} // namespace

CsgCode subdivide(Input &in, const Polygon &g, const PolygonCuts &cuts,
                  litestl::util::Vector<PolygonFacet> &out)
{
  Subdivider s{in, g, {}, {}};
  s.gather(cuts);
  PlanarGraph graph(in, g.plane, g.axis);
  s.split(graph);
  const CsgCode c = graph.cells({cuts.dots.data(), size_t(cuts.dots.size())}, out);
  return c == CsgCode::Ok && out.size() == 0 ? CsgCode::Inconsistent : c;
}

} // namespace ubertopo::csg
