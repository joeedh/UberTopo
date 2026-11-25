/* Reading operands, polygon pairs and the subdivision of polygons (plan
 * 2026-11-25-exact-booleans §4.1-4.3), on random operands that touch, overlap
 * and share planes.
 *
 * Checked per scene, without the engine's own traversal:
 *  - an edge of a facet never crosses, and no vertex lies on, a polygon that
 *    doesn't have it as an edge or a point;
 *  - the facets of a polygon have its area, and its boundary;
 *  - placing the operands in another way gives the same arrangement;
 *  - so does the order of the operands. */

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "litestl/util/alloc.h"
#include "ut_test.h"

#include "support/csg_scenes.h"

using namespace ubertopo;
using csg::HPoint;
using csg::Input;
using csg::kNone;
using csg::Mid;
using csg::Operand;
using csg::Polygon;
using csg::PolygonCuts;
using csg::PolygonFacet;
using litestl::test::SplitMix64;
using test::add_random;
using test::I3;
using test::L;
using test::Operands;
using test::random_placement;
using Complex = test::LComplex;

namespace {

I3 at(const litestl::math::int64_3 &p)
{
  return {p[0], p[1], p[2]};
}

struct Arrangement {
  Input in;
  std::vector<PolygonCuts> cuts;
  std::vector<std::vector<PolygonFacet>> facets;
};

void arrange(const std::vector<Operand> &ops, Arrangement &a)
{
  UT_CHECK(csg::read_operands(ops, a.in) == csg::CsgCode::Ok);
  const uint32_t n = uint32_t(a.in.polygons.size());
  a.cuts.resize(n);
  a.facets.resize(n);
  for (uint32_t i = 0; i < n; i++) {
    for (uint32_t j = i + 1; j < n; j++) {
      if (a.in.polygons[int(i)].leaf != a.in.polygons[int(j)].leaf) {
        csg::intersect_polygons(a.in, i, j, a.cuts[i], a.cuts[j]);
      }
    }
  }
  for (uint32_t i = 0; i < n; i++) {
    litestl::util::Vector<PolygonFacet> f;
    UT_CHECK(csg::subdivide(a.in, a.in.polygons[int(i)], a.cuts[i], f) == csg::CsgCode::Ok);
    for (auto &x : f) {
      a.facets[i].push_back(std::move(x));
    }
  }
}

/* ---- the checks ---- */

std::pair<uint32_t, uint32_t> und(uint32_t a, uint32_t b)
{
  return {std::min(a, b), std::max(a, b)};
}

double num(const HPoint &p, int i)
{
  return p.x[i].to_double() / p.w.to_double();
}

double area(const Input &in, const Polygon &g, const PolygonFacet *f);

struct Counts {
  uint32_t facets = 0, dots = 0, slits = 0, holes = 0, crossings = 0, shared = 0;
};

void check_conforming(const Arrangement &a, Counts &n)
{
  const Input &in = a.in;
  const uint32_t np = uint32_t(in.polygons.size());
  std::vector<std::set<std::pair<uint32_t, uint32_t>>> edges(np);
  std::vector<std::set<uint32_t>> pts(np);
  std::map<std::pair<uint32_t, uint32_t>, int> all;
  std::set<uint32_t> used;
  for (uint32_t i = 0; i < np; i++) {
    UT_CHECK(!a.facets[i].empty());
    std::set<std::pair<uint32_t, uint32_t>> directed;
    for (const PolygonFacet &f : a.facets[i]) {
      n.facets++;
      n.dots += uint32_t(f.dots.size());
      n.holes += uint32_t(f.off.size()) - 2;
      std::set<std::pair<uint32_t, uint32_t>> own;
      for (int c = 0; c + 1 < int(f.off.size()); c++) {
        for (uint32_t e = f.off[c]; e < f.off[c + 1]; e++) {
          const csg::HalfEdge &h = f.edges[int(e)];
          UT_CHECK(h.to == f.edges[int(e + 1 < f.off[c + 1] ? e + 1 : f.off[c])].from);
          UT_CHECK(csg::classify(in.points[h.from], in.planes[h.cut]) == 0 &&
                   csg::classify(in.points[h.to], in.planes[h.cut]) == 0);
          n.slits += own.count({h.to, h.from});
          own.insert({h.from, h.to});
          UT_CHECK(directed.insert({h.from, h.to}).second);
          edges[i].insert(und(h.from, h.to));
          pts[i].insert(h.from);
        }
      }
      for (uint32_t d : f.dots) {
        UT_CHECK(pts[i].insert(d).second);
      }
    }
    UT_CHECK(area(in, in.polygons[int(i)], nullptr) > 0);
    /* an edge inside the polygon has a facet on both sides */
    const Polygon &g = in.polygons[int(i)];
    for (const auto &e : edges[i]) {
      const Mid m(in.points[e.first], in.points[e.second]);
      const int where = csg::point_in_polygon(in, g, m);
      const int sides = int(directed.count({e.first, e.second}) + directed.count({e.second, e.first}));
      UT_CHECK(where >= 0 && sides == (where > 0 ? 2 : 1));
      all[e]++;
    }
    for (uint32_t p : pts[i]) {
      UT_CHECK(csg::point_in_polygon(in, g, in.points[p]) >= 0);
      used.insert(p);
    }
  }
  for (uint32_t i = 0; i < np; i++) {
    const Polygon &g = in.polygons[int(i)];
    const csg::Plane &pl = in.planes[g.plane];
    std::map<uint32_t, int> side;
    for (uint32_t p : used) {
      const int s = side[p] = csg::classify(in.points[p], pl);
      if (s == 0) {
        /* on the polygon, or off it */
        UT_CHECK((csg::point_in_polygon(in, g, in.points[p]) >= 0) == (pts[i].count(p) > 0));
      }
    }
    for (const auto &[e, count] : all) {
      const int sa = side[e.first], sb = side[e.second];
      if (sa == 0 && sb == 0) {
        const Mid m(in.points[e.first], in.points[e.second]);
        const bool on = csg::point_in_polygon(in, g, m) >= 0;
        UT_CHECK(on == (edges[i].count(e) > 0));
        n.shared += on;
      }
      else if (sa * sb < 0) {
        /* the point of the edge on the plane: between its ends, so not a
         * vertex; the edge lies on two planes of any polygon that has it */
        n.crossings++;
        uint32_t own = kNone, cut = kNone;
        for (uint32_t j = 0; j < np && own == kNone; j++) {
          for (const PolygonFacet &f : a.facets[j]) {
            for (const csg::HalfEdge &h : f.edges) {
              if (und(h.from, h.to) == e) {
                own = in.polygons[int(j)].plane;
                cut = h.cut;
              }
            }
          }
        }
        HPoint x;
        UT_CHECK(csg::meet(in.planes[own], in.planes[cut], pl, x));
        UT_CHECK(csg::point_in_polygon(in, g, x) < 0);
      }
    }
  }
}

/* the area along the projection axis; slits cancel */
double area(const Input &in, const Polygon &g, const PolygonFacet *f)
{
  const int u = (g.axis + 1) % 3, v = (g.axis + 2) % 3;
  const int s = in.planes[g.plane].n[g.axis] > 0 ? 1 : -1;
  double sum = 0;
  if (!f) {
    g.for_each_edge([&](uint32_t i, uint32_t j) {
      const auto &p = in.ipos[int(g.pts[int(i)])], &q = in.ipos[int(g.pts[int(j)])];
      sum += double(p[u]) * double(q[v]) - double(p[v]) * double(q[u]);
    });
    return sum * s * g.sign;
  }
  for (const csg::HalfEdge &h : f->edges) {
    const HPoint &p = in.points[h.from], &q = in.points[h.to];
    sum += num(p, u) * num(q, v) - num(p, v) * num(q, u);
  }
  return sum * s;
}

void check_areas(const Arrangement &a)
{
  for (uint32_t i = 0; i < a.facets.size(); i++) {
    const Polygon &g = a.in.polygons[int(i)];
    double sum = 0;
    for (const PolygonFacet &f : a.facets[i]) {
      const double x = area(a.in, g, &f);
      UT_CHECK(x > 0);
      sum += x;
    }
    const double want = area(a.in, g, nullptr);
    UT_CHECK(want > 0 && std::abs(sum - want) <= 1e-9 * want + 1e-9);
  }
}

/* An arrangement up to renumbering: per (leaf, face), its facets, as sets of
 * edges and dots, by exact position. */
std::string key(const HPoint &p)
{
  /* a fixed fraction of the coordinates, exact enough for these scenes */
  std::string s;
  for (int i = 0; i < 3; i++) {
    s += std::to_string(csg::floor_at(p, i, 20)) + ",";
  }
  return s;
}

using Canon = std::multiset<std::string>;

Canon canonical(const Arrangement &a, const std::vector<uint32_t> &leaf_name)
{
  Canon out;
  for (uint32_t i = 0; i < a.facets.size(); i++) {
    const Polygon &g = a.in.polygons[int(i)];
    for (const PolygonFacet &f : a.facets[i]) {
      std::vector<std::string> es;
      for (const csg::HalfEdge &h : f.edges) {
        std::string p = key(a.in.points[h.from]), q = key(a.in.points[h.to]);
        es.push_back(std::min(p, q) + "-" + std::max(p, q));
      }
      for (uint32_t d : f.dots) {
        es.push_back("." + key(a.in.points[d]));
      }
      std::sort(es.begin(), es.end());
      std::string s = std::to_string(leaf_name[g.leaf]) + "/" + std::to_string(g.face) + ":";
      for (const std::string &e : es) {
        s += e + " ";
      }
      out.insert(s);
    }
  }
  return out;
}

/* Numbers that don't depend on the frame. */
std::vector<uint32_t> shape(const Arrangement &a)
{
  std::vector<uint32_t> out;
  for (uint32_t i = 0; i < a.facets.size(); i++) {
    std::vector<std::array<uint32_t, 3>> fs;
    for (const PolygonFacet &f : a.facets[i]) {
      fs.push_back({uint32_t(f.edges.size()), uint32_t(f.off.size()), uint32_t(f.dots.size())});
    }
    std::sort(fs.begin(), fs.end());
    out.push_back(a.in.polygons[int(i)].leaf);
    out.push_back(a.in.polygons[int(i)].face);
    for (const auto &f : fs) {
      out.insert(out.end(), f.begin(), f.end());
    }
  }
  return out;
}

void test_random()
{
  Counts n;
  SplitMix64 r(11);
  for (int s = 0; s < 400; s++) {
    const int64_t range = s % 8 == 7 ? (int64_t(1) << 25) : 3;
    Operands o;
    const int count = 2 + int(r.below(3));
    for (int i = 0; i < count; i++) {
      add_random(r, range, o);
    }
    Arrangement a;
    arrange(o.ops, a);
    check_conforming(a, n);
    if (range < 1000) {
      check_areas(a);
    }
    /* another order */
    std::vector<uint32_t> name, rev_name;
    std::vector<Operand> rev(o.ops.rbegin(), o.ops.rend());
    for (uint32_t i = 0; i < o.ops.size(); i++) {
      name.push_back(i);
      rev_name.push_back(uint32_t(o.ops.size()) - 1 - i);
    }
    Arrangement b;
    arrange(rev, b);
    UT_CHECK(canonical(a, name) == canonical(b, rev_name));
    /* another frame */
    const csg::GridPlacement p = random_placement(r);
    std::vector<Operand> moved = o.ops;
    for (Operand &x : moved) {
      x.place = p;
    }
    Arrangement c;
    arrange(moved, c);
    check_conforming(c, n);
    UT_CHECK(shape(a) == shape(c));
  }
  printf("facets %u, dots %u, slits %u, holes %u, transversal %u, shared %u\n", n.facets, n.dots, n.slits, n.holes,
         n.crossings, n.shared);
  UT_CHECK(n.facets > 8000 && n.dots > 50 && n.slits > 200 && n.holes > 200 && n.shared > 5000);
}

void test_polygon()
{
  /* point_in_polygon against winding numbers of integer points, in a plate
   * with two holes */
  SplitMix64 r(12);
  uint32_t counts[3] = {};
  for (int s = 0; s < 60; s++) {
    Operands o;
    test::GridBuilder<L> g;
    const I3 slope = {int64_t(r.below(5)) - 2, int64_t(r.below(5)) - 2, 0};
    auto p = [&](int64_t x, int64_t y) -> I3 { return {x, y, slope[0] * x + slope[1] * y}; };
    const std::vector<std::vector<std::array<int64_t, 2>>> loops = {
        {{0, 0}, {12, 0}, {12, 4}, {8, 4}, {8, 8}, {12, 8}, {12, 12}, {0, 12}},
        {{2, 2}, {2, 5}, {5, 5}, {5, 2}},
        {{5, 6}, {5, 9}, {7, 9}}};
    std::vector<std::vector<I3>> ls;
    for (const auto &l : loops) {
      ls.push_back({});
      for (const auto &q : l) {
        ls.back().push_back(p(q[0], q[1]));
      }
    }
    g.face(ls);
    o.add(g);
    if (s % 2) {
      o.ops[0].place = random_placement(r);
    }
    Input in;
    UT_CHECK(csg::read_operands(o.ops, in) == csg::CsgCode::Ok);
    UT_CHECK(in.polygons.size() == 1);
    const Polygon &pg = in.polygons[0];
    UT_CHECK(pg.whole && pg.loop_count() == 3);
    for (int64_t x = -1; x <= 26; x++) {
      for (int64_t y = -1; y <= 26; y++) {
        /* half-integer points, doubled; as a Mid of two integer points */
        bool on = false, inside = false;
        for (const auto &l : loops) {
          for (size_t i = 0; i < l.size(); i++) {
            const auto &a = l[i], &b = l[(i + 1) % l.size()];
            const int64_t ax = 2 * a[0], ay = 2 * a[1], bx = 2 * b[0], by = 2 * b[1];
            const int64_t cr = (bx - ax) * (y - ay) - (by - ay) * (x - ax);
            if (cr == 0 && x >= std::min(ax, bx) && x <= std::max(ax, bx) && y >= std::min(ay, by) &&
                y <= std::max(ay, by))
            {
              on = true;
            }
            if ((ay <= y) != (by <= y)) {
              const bool right = by > ay ? cr > 0 : cr < 0;
              inside = inside != right;
            }
          }
        }
        const int want = on ? 0 : inside ? 1 : -1;
        counts[want + 1]++;
        const I3 a = p(x / 2, y / 2), b = p(x - x / 2, y - y / 2);
        auto place = [&](const I3 &q) {
          const auto t = o.ops[0].place.apply(csg::I3(q[0], q[1], q[2]));
          return csg::I3(t[0] + in.shift[0], t[1] + in.shift[1], t[2] + in.shift[2]);
        };
        const Mid m(HPoint::from(place(a)), HPoint::from(place(b)));
        UT_CHECK(csg::point_in_polygon(in, pg, m) == want);
        if (x % 2 == 0 && y % 2 == 0) {
          UT_CHECK(csg::point_in_polygon(in, pg, HPoint::from(place(p(x / 2, y / 2)))) == want);
        }
      }
    }
  }
  UT_CHECK(counts[0] > 1000 && counts[1] > 1000 && counts[2] > 1000);
}

/* a plate cut by tubes inside each other, in two groups */
void test_nested()
{
  SplitMix64 r(14);
  for (int s = 0; s < 40; s++) {
    Operands o;
    test::GridBuilder<L> g;
    g.face({{{-40, -40, 0}, {40, -40, 0}, {40, 40, 0}, {-40, 40, 0}}});
    o.add(g);
    std::vector<int64_t> sizes = {1, 2, 3, 4, 5, 6};
    for (size_t i = sizes.size(); i > 1; i--) {
      std::swap(sizes[i - 1], sizes[r.below(i)]);
    }
    for (int64_t w : sizes) {
      test::GridBuilder<L> b;
      b.box({-2 * w, -2 * w, -w}, {2 * w, 2 * w, w});
      b.box({20 - 2 * w, -2 * w, -w - 20}, {20 + 2 * w, 2 * w, -19});
      o.add(b);
      test::GridBuilder<L> c;
      c.box({-30 - w, 20 - w, -w}, {-30 + w, 20 + w, w});
      o.add(c);
    }
    /* points: pyramids that stand on the plate */
    for (const int64_t x : {0, 15, -30}) {
      test::GridBuilder<L> t;
      const I3 a = {x, x < -20 ? 20 : 0, 0};
      const I3 p = {a[0] + 1, a[1], 5}, q = {a[0], a[1] + 1, 5}, n = {a[0] - 1, a[1] - 1, 5};
      t.face({{a, q, p}});
      t.face({{a, n, q}});
      t.face({{a, p, n}});
      t.face({{p, q, n}});
      o.add(t);
    }
    const csg::GridPlacement pl = random_placement(r);
    for (Operand &x : o.ops) {
      x.place = pl;
    }
    Arrangement a;
    arrange(o.ops, a);
    Counts n;
    check_conforming(a, n);
    check_areas(a);
    /* the plate: outside, and a ring or a square per tube */
    UT_CHECK(a.facets[0].size() == 13);
    uint32_t dots = 0;
    for (const PolygonFacet &f : a.facets[0]) {
      const size_t cycles = f.off.size() - 1;
      const bool outside = cycles == 3, ring = cycles == 2, middle = cycles == 1;
      UT_CHECK(outside || ring || middle);
      UT_CHECK(f.dots.size() == (ring ? 0u : 1u));
      dots += uint32_t(f.dots.size());
    }
    UT_CHECK(dots == 3);
  }
}

/* sheets in one plane that touch in points, or in part of an edge */
void test_coplanar_contacts()
{
  const std::vector<std::vector<I3>> other = {
      {{4, 2, 0}, {8, 0, 0}, {8, 4, 0}},             /* a corner on an edge */
      {{4, 4, 0}, {8, 4, 0}, {8, 8, 0}, {4, 8, 0}},  /* a corner on a corner */
      {{4, 1, 0}, {8, 1, 0}, {8, 3, 0}, {4, 3, 0}},  /* along a part of an edge */
      {{4, 3, 0}, {8, 3, 0}, {8, 6, 0}, {4, 6, 0}},  /* the same, past the corner */
      {{5, -3, 0}, {8, 0, 0}, {5, 3, 0}, {4, 0, 0}}, /* its corner on a corner */
      {{6, 0, 0}, {8, 0, 0}, {8, 4, 0}, {0, 6, 0}}}; /* an edge past a corner, off it */
  for (size_t i = 0; i < other.size(); i++) {
    for (int swap = 0; swap < 2; swap++) {
      Operands o;
      test::GridBuilder<L> g, h;
      g.face({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
      h.face({other[i]});
      o.add(swap ? h : g);
      o.add(swap ? g : h);
      Arrangement a;
      arrange(o.ops, a);
      Counts n;
      check_conforming(a, n);
      check_areas(a);
      if (i == 0) {
        const auto &f = a.facets[size_t(swap)];
        UT_CHECK(f.size() == 1 && f[0].edges.size() == 5);
      }
    }
  }
}

/* placements, sides and edges as read */
void test_read()
{
  SplitMix64 r(13);
  for (int s = 0; s < 200; s++) {
    Operands o;
    test::GridBuilder<L> g;
    g.box({0, 0, 0}, {3, 5, 7});
    o.add(g);
    o.ops[0].place = random_placement(r);
    const csg::GridPlacement &pl = o.ops[0].place;
    Input in;
    UT_CHECK(csg::read_operands(o.ops, in) == csg::CsgCode::Ok);
    UT_CHECK(in.polygons.size() == 6 && in.ipos.size() == 8 && in.edges.size() == 12 && in.planes.size() == 6);
    const Complex &k = *o.k[0];
    const I3 c = at(pl.apply(csg::I3(1, 2, 3)));
    for (const Polygon &p : in.polygons) {
      /* the front points out of the box */
      const csg::I3 inner(c[0] + in.shift[0], c[1] + in.shift[1], c[2] + in.shift[2]);
      UT_CHECK(csg::classify(inner, in.planes[p.plane]) * p.sign < 0);
      UT_CHECK(p.region[0] == k.region(topo::FaceRef(p.face), 0).i && p.region[0] == 0 && p.region[1] != 0);
      /* the loop turns left about the front */
      const csg::I3 x = in.ipos[int(p.pts[0])], y = in.ipos[int(p.pts[1])], z = in.ipos[int(p.pts[2])];
      const csg::I3 nrm = csg::cross_small(csg::sub(y, x), csg::sub(z, y));
      UT_CHECK(csg::dot(nrm, in.planes[p.plane].n).sign() == p.sign);
      p.for_each_edge([&](uint32_t i, uint32_t j) {
        const csg::SrcEdge &e = in.edges[int(p.edges[int(i)])];
        UT_CHECK(und(p.pts[int(i)], p.pts[int(j)]) == std::make_pair(e.a, e.b) && e.edge != kNone);
        const auto vs = k.edge_verts(topo::EdgeRef(e.edge));
        std::set<I3> want, got;
        for (int t = 0; t < 2; t++) {
          const auto &q = k.pos(vs[t]);
          const csg::I3 w = pl.apply(csg::I3(q[0].raw, q[1].raw, q[2].raw));
          want.insert({w[0] + in.shift[0], w[1] + in.shift[1], w[2] + in.shift[2]});
        }
        got = {at(in.ipos[int(e.a)]), at(in.ipos[int(e.b)])};
        UT_CHECK(want == got);
      });
    }
  }
  /* a projected face becomes its triangles, with diagonals */
  {
    Operands o;
    test::GridBuilder<L> g;
    g.face({{{0, 0, 0}, {4, 0, 1}, {4, 4, 0}, {0, 4, 1}}});
    topo::BuildPolicy policy;
    policy.allow_projectable_faces = true;
    o.add(g, policy);
    Input in;
    UT_CHECK(csg::read_operands(o.ops, in) == csg::CsgCode::Ok);
    UT_CHECK(in.polygons.size() == 2 && in.edges.size() == 5);
    int diagonals = 0;
    for (const csg::SrcEdge &e : in.edges) {
      diagonals += e.edge == kNone;
    }
    UT_CHECK(diagonals == 1);
    for (const Polygon &t : in.polygons) {
      UT_CHECK(!t.whole && t.sign * in.planes[t.plane].n[2] > 0);
      UT_CHECK(area(in, t, nullptr) > 0);
    }
  }
  /* refusals */
  {
    Operands o;
    test::GridBuilder<L> g;
    g.box({0, 0, 0}, {1, 1, 1});
    o.add(g);
    uint32_t bad = kNone;
    Input a, b, c, d;
    std::vector<Operand> ops = {o.ops[0], o.ops[0]};
    ops[1].place.axis[0] = 1;
    UT_CHECK(csg::read_operands(ops, a, &bad) == csg::CsgCode::BadOperand && bad == 1);
    ops[1].place = {};
    ops[1].place.offset[2] = (int64_t(1) << 27) - 1;
    UT_CHECK(csg::read_operands(ops, b, &bad) == csg::CsgCode::Ok);
    ops[1].place.offset[2] = (int64_t(1) << 27) + 2;
    UT_CHECK(csg::read_operands(ops, c, &bad) == csg::CsgCode::OutOfRange);
    ops[1].place.offset[2] = (int64_t(1) << 40) + 1;
    Input e, f, h, w, v;
    UT_CHECK(csg::read_operands(ops, e, &bad) == csg::CsgCode::OutOfRange && bad == 1);
    /* a permutation with one axis twice, of any axis */
    for (int8_t i = 0; i < 3; i++) {
      csg::GridPlacement p;
      p.axis[i] = int8_t((i + 1) % 3);
      UT_CHECK(p.det() == 0);
      p = {};
      p.sign[i] = 0;
      UT_CHECK(p.det() == 0);
      p.sign[i] = -1;
      UT_CHECK(p.det() == -1);
      std::swap(p.axis[0], p.axis[1]);
      UT_CHECK(p.det() == 1);
    }
    /* each operand has its own placement */
    ops[1].place = {};
    for (int i = 0; i < 3; i++) {
      ops[1].place.offset[i] = 10 + i;
    }
    UT_CHECK(csg::read_operands(ops, f) == csg::CsgCode::Ok && f.polygons.size() == 12);
    for (int i = 0; i < 6; i++) {
      for (int x = 0; x < 3; x++) {
        UT_CHECK(f.polygons[i + 6].lo[x] == f.polygons[i].lo[x] + 10 + x);
      }
    }
    /* other quanta, wires, vertices in regions and faces */
    Operands u;
    u.add(g);
    u.add(g);
    u.k[1]->frame.quantum_log2 = -10;
    UT_CHECK(csg::read_operands(u.ops, h, &bad) == csg::CsgCode::MixedFrames && bad == 1);
    for (int kind = 0; kind < 4; kind++) {
      test::GridBuilder<L> m;
      m.box({0, 0, 0}, {9, 9, 9});
      const uint32_t face = m.face({{{20, 0, 0}, {29, 0, 0}, {29, 9, 0}, {20, 9, 0}}});
      const auto id = [](uint64_t i) { return test::pid(i, 77); };
      const auto p = [](int64_t x, int64_t z) { return test::pt<L>({x, 3, z}); };
      if (kind == 0) {
        m.b.wire(id(1), m.b.vertex(id(2), p(40, 1)), m.b.vertex(id(3), p(44, 1)));
      }
      else if (kind == 1) {
        m.b.vertex(id(2), p(40, 1), topo::ContainerHint::in_region(ident::unbounded_region_id()));
      }
      else if (kind == 2) {
        m.b.vertex(id(2), p(24, 0), topo::ContainerHint::in_face(face));
      }
      else {
        m.b.wire(id(1), m.v({20, 0, 0}), m.v({20, 0, 5}));
      }
      Operands c2;
      c2.add(m);
      Input x;
      UT_CHECK(csg::read_operands(c2.ops, x, &bad) == csg::CsgCode::UnsupportedOperand && bad == 0);
    }
    u.k[0]->faces.get<".f.surface">().set(0, topo::GeomRef{1, 0});
    Input z;
    UT_CHECK(csg::read_operands({u.ops.data(), 1}, z, &bad) == csg::CsgCode::UnsupportedOperand && bad == 0);
    o.k[0]->regions_dirty = true;
    UT_CHECK(csg::read_operands(o.ops, d, &bad) == csg::CsgCode::BadOperand && bad == 0);
  }
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_polygon();
  test_read();
  test_nested();
  test_coplanar_contacts();
  test_random();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
