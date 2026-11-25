/* Faces that the complex can't hold, cut along diagonals (plan
 * 2026-11-25-exact-booleans §4.5): facets with slits, pinches and isolated
 * points out of random arrangements, and some made by hand.
 *
 *  - every piece is legal, and together they have the area;
 *  - diagonals lie in the face and cross nothing;
 *  - none of them can be left out;
 *  - the pieces don't depend on how points are numbered. */

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>

#include "litestl/util/alloc.h"
#include "ut_test.h"

#include "support/csg_oracle.h"
#include "ubertopo/csg/legal.h"

using namespace ubertopo;
using namespace ubertopo::test;
using csg::Arrangement;
using csg::BoundarySegment;
using csg::CsgCode;
using csg::Diagonal;
using csg::kNone;
using csg::PlanarGraph;
using csg::PolygonFacet;

namespace {

struct Face {
  std::vector<BoundarySegment> boundary;
  std::vector<uint32_t> dots;
  uint32_t plane;
  int axis;
};

/* twice the area along the axis, seen from the normal */
double area(const Arrangement &a, const Face &f, const PolygonFacet &c)
{
  const int u = (f.axis + 1) % 3, v = (f.axis + 2) % 3;
  const double s = a.in.planes[f.plane].n[f.axis] > 0 ? 1 : -1;
  double sum = 0;
  for (const csg::HalfEdge &h : c.edges) {
    const D3 p = pos(a, h.from), q = pos(a, h.to);
    sum += s * (p[u] * q[v] - p[v] * q[u]);
  }
  return sum;
}

struct Counts {
  uint32_t legal = 0, illegal = 0, diagonals = 0, pieces = 0, dots = 0, slits = 0;
};

/* the corners of every piece, as coordinates */
std::multiset<std::set<std::string>> shape(const Arrangement &a, const litestl::util::Vector<PolygonFacet> &cells)
{
  std::multiset<std::set<std::string>> out;
  for (const PolygonFacet &c : cells) {
    std::set<std::string> s;
    for (const csg::HalfEdge &h : c.edges) {
      const D3 p = pos(a, h.from);
      s.insert(std::to_string(p[0]) + "," + std::to_string(p[1]) + "," + std::to_string(p[2]));
    }
    out.insert(s);
  }
  return out;
}

void check(const Arrangement &a, const Face &f, Counts &n, litestl::util::Vector<PolygonFacet> &cells)
{
  litestl::util::Vector<Diagonal> diagonals;
  PlanarGraph g(a.in, f.plane, f.axis);
  auto rebuild = [&](int skip) {
    g.clear();
    for (const BoundarySegment &s : f.boundary) {
      g.add(s.p, s.q, s.cut, s.out);
    }
    for (int i = 0; i < int(diagonals.size()); i++) {
      if (i != skip) {
        g.add(diagonals[i].p, diagonals[i].q, kNone, 0);
      }
    }
    litestl::util::Vector<PolygonFacet> out;
    UT_CHECK(g.cells(f.dots, out) == CsgCode::Ok);
    return out;
  };
  auto all_legal = [](const litestl::util::Vector<PolygonFacet> &v) {
    return std::all_of(v.begin(), v.end(), [](const PolygonFacet &c) { return csg::is_legal(c); });
  };
  const auto before = rebuild(-1);
  UT_CHECK(before.size() == 1);
  UT_CHECK(csg::make_legal(a.in, f.plane, f.axis, f.boundary, f.dots, cells, diagonals) == CsgCode::Ok);
  if (all_legal(before)) {
    n.legal++;
    UT_CHECK(diagonals.size() == 0 && cells.size() == 1);
    return;
  }
  n.illegal++;
  n.diagonals += uint32_t(diagonals.size());
  n.pieces += uint32_t(cells.size());
  n.dots += uint32_t(before[0].dots.size());
  UT_CHECK(diagonals.size() > 0 && all_legal(cells));
  double sum = 0;
  std::set<uint32_t> corners;
  for (const PolygonFacet &c : cells) {
    /* (written out again: every point once, no segment both ways) */
    std::set<uint32_t> seen;
    UT_CHECK(c.dots.size() == 0);
    for (const csg::HalfEdge &h : c.edges) {
      UT_CHECK(seen.insert(h.from).second);
      corners.insert(h.from);
    }
    for (int i = 0; i + 1 < int(c.off.size()); i++) {
      UT_CHECK(c.off[i + 1] - c.off[i] >= 3);
    }
    const double x = area(a, f, c);
    UT_CHECK(x > 0);
    sum += x;
  }
  const double want = area(a, f, before[0]);
  UT_CHECK(std::abs(sum - want) < 1e-9 * want + 1e-9);
  std::set<uint32_t> points(f.dots.begin(), f.dots.end());
  std::vector<Seg> segs;
  for (const BoundarySegment &s : f.boundary) {
    points.insert(s.p);
    points.insert(s.q);
    if (s.out) {
      segs.push_back({pos(a, s.p), pos(a, s.q)});
    }
  }
  UT_CHECK(corners == points);
  for (int i = 0; i < int(diagonals.size()); i++) {
    const Diagonal &d = diagonals[i];
    const D3 p = pos(a, d.p), q = pos(a, d.q);
    UT_CHECK(d.p != d.q && points.count(d.p) && points.count(d.q));
    UT_CHECK(in_segments(segs, {(p[0] + q[0]) / 2, (p[1] + q[1]) / 2, (p[2] + q[2]) / 2}, f.axis, 0) == 1);
    auto crossing = [&](uint32_t r, uint32_t s) {
      return d.p != r && d.p != s && d.q != r && d.q != s && g.turn(d.p, d.q, r) * g.turn(d.p, d.q, s) < 0 &&
             g.turn(r, s, d.p) * g.turn(r, s, d.q) < 0;
    };
    for (const BoundarySegment &s : f.boundary) {
      UT_CHECK(!crossing(s.p, s.q));
      UT_CHECK(std::minmax(s.p, s.q) != std::minmax(d.p, d.q));
    }
    for (int j = 0; j < i; j++) {
      UT_CHECK(!crossing(diagonals[j].p, diagonals[j].q));
    }
    for (uint32_t x : points) {
      UT_CHECK(x == d.p || x == d.q || g.turn(d.p, d.q, x) != 0 || !g.between(d.p, d.q, x));
    }
    UT_CHECK(!all_legal(rebuild(i)));
  }
}

Face face_of(const Arrangement &a, uint32_t f)
{
  const csg::Facet &fc = a.facets[int(f)];
  Face out;
  out.plane = fc.plane;
  out.axis = a.in.polygons[int(fc.sources[0])].axis;
  out.dots.assign(fc.dots.begin(), fc.dots.end());
  std::map<uint32_t, std::pair<uint8_t, uint32_t>> sides; /* edge: sides that are in, cut */
  for (const csg::FacetEdge &e : fc.edges) {
    sides[e.edge].first |= e.dir > 0 ? 1 : 2;
    sides[e.edge].second = e.cut;
  }
  for (const auto &[e, s] : sides) {
    out.boundary.push_back({a.edges[int(e)].a, a.edges[int(e)].b, s.second,
                            uint8_t((s.first & 1 ? 0 : csg::kLeftOut) | (s.first & 2 ? 0 : csg::kRightOut))});
  }
  return out;
}

void test_random()
{
  SplitMix64 r(51);
  Counts n;
  for (int s = 0; s < 250; s++) {
    Operands o;
    GridBuilder<L> g;
    /* a tilted plate, and things that stand on it, cross it or touch it */
    const int64_t tx = int64_t(r.below(5)) - 2, ty = int64_t(r.below(5)) - 2;
    auto p = [&](int64_t x, int64_t y, int64_t z) -> I3 { return {x, y, tx * x + ty * y + z}; };
    g.face({{p(-9, -9, 0), p(9, -9, 0), p(9, 9, 0), p(-9, 9, 0)}});
    o.add(g);
    const int count = 1 + int(r.below(6));
    for (int i = 0; i < count; i++) {
      GridBuilder<L> h;
      const I3 a = vertex(r, 5), b = vertex(r, 5);
      const uint64_t kind = r.below(4);
      if (kind == 0) {
        /* a spike */
        h.face({{p(a[0], a[1], 0), p(a[0] + 1, a[1], 7), p(a[0], a[1] + 1, 7)}});
      }
      else if (kind == 1 && (a[0] != b[0] || a[1] != b[1])) {
        /* a wall on it */
        h.face({{p(a[0], a[1], 0), p(b[0], b[1], 0), p(b[0], b[1], 5), p(a[0], a[1], 5)}});
      }
      else if (kind == 2) {
        /* a triangle in its plane */
        const I3 c = vertex(r, 5);
        if ((b[0] - a[0]) * (c[1] - a[1]) == (b[1] - a[1]) * (c[0] - a[0])) {
          continue;
        }
        h.face({{p(a[0], a[1], 0), p(b[0], b[1], 0), p(c[0], c[1], 0)}});
      }
      else {
        /* a wall through it */
        h.face({{p(a[0], a[1], -3), p(a[0] + 3, a[1] + 1, -3), p(a[0] + 3, a[1] + 1, 3), p(a[0], a[1], 3)}});
      }
      o.add(h);
    }
    std::vector<uint32_t> order;
    litestl::util::Vector<PolygonFacet> cells;
    std::multiset<std::set<std::string>> got[2];
    for (int turn = 0; turn < 2; turn++) {
      Arrangement a;
      std::vector<csg::Operand> ops = o.ops;
      if (turn) {
        std::reverse(ops.begin(), ops.end());
      }
      UT_CHECK(csg::arrange(ops, a) == CsgCode::Ok);
      for (uint32_t f = 0; f < a.facets.size(); f++) {
        check(a, face_of(a, f), n, cells);
        got[turn].merge(shape(a, cells));
      }
    }
    UT_CHECK(got[0] == got[1]);
  }
  printf("legal %u, illegal %u: %u diagonals, %u pieces; %u dots\n", n.legal, n.illegal, n.diagonals, n.pieces, n.dots);
  UT_CHECK(n.legal > 1500 && n.illegal > 250 && n.diagonals > 600 && n.dots > 100);
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_random();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
