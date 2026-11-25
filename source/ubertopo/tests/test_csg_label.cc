/* The regions on both sides of every facet (plan 2026-11-25-exact-booleans
 * §4.4), against rays in floating point from points just off the facets, and
 * the radial order of facets against angles. Scenes are small, so a margin
 * makes the floating point safe. */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "litestl/util/alloc.h"
#include "ut_test.h"

#include "support/csg_scenes.h"

using namespace ubertopo;
using namespace ubertopo::test;
using csg::Arrangement;
using csg::Facet;
using csg::kNone;
using csg::Polygon;
using D3 = std::array<double, 3>;

namespace {

double real(SplitMix64 &r)
{
  return double(r.next() >> 11) / double(uint64_t(1) << 53);
}

D3 pos(const Arrangement &a, uint32_t p)
{
  const csg::HPoint &h = a.in.points[p];
  return {h.x[0].to_double() / h.w.to_double(), h.x[1].to_double() / h.w.to_double(),
          h.x[2].to_double() / h.w.to_double()};
}

D3 sub(const D3 &a, const D3 &b)
{
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
D3 cross(const D3 &a, const D3 &b)
{
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const D3 &a, const D3 &b)
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
double len(const D3 &a)
{
  return std::sqrt(dot(a, a));
}

D3 unit_normal(const Arrangement &a, uint32_t plane)
{
  const csg::I3 &n = a.in.planes[plane].n;
  D3 d = {double(n[0]), double(n[1]), double(n[2])};
  const double l = len(d);
  return {d[0] / l, d[1] / l, d[2] / l};
}

struct Seg {
  D3 a, b;
};

/* 1 inside, 0 outside, -1 too close to an edge; p is in the plane */
int in_segments(const std::vector<Seg> &segs, const D3 &p, int axis, double margin)
{
  const int u = (axis + 1) % 3, v = (axis + 2) % 3;
  bool inside = false;
  for (const Seg &s : segs) {
    const D3 d = sub(s.b, s.a), w = sub(p, s.a);
    const double t = std::clamp(dot(w, d) / dot(d, d), 0.0, 1.0);
    if (len({w[0] - t * d[0], w[1] - t * d[1], w[2] - t * d[2]}) < margin) {
      return -1;
    }
    if ((s.a[v] <= p[v]) != (s.b[v] <= p[v])) {
      const double x = s.a[u] + (p[v] - s.a[v]) / (s.b[v] - s.a[v]) * (s.b[u] - s.a[u]);
      inside = inside != (x > p[u]);
    }
  }
  return inside;
}

/* the leaf's region at a point off its faces */
uint32_t oracle(const Arrangement &a, uint32_t leaf, const D3 &o, SplitMix64 &r)
{
  for (int attempt = 0; attempt < 100; attempt++) {
    const D3 dir = {real(r) - 0.5, real(r) - 0.5, real(r) - 0.5};
    double best = 1e9;
    uint32_t region = 0;
    bool bad = false;
    for (const Polygon &g : a.in.polygons) {
      if (g.leaf != leaf) {
        continue;
      }
      const D3 n = unit_normal(a, g.plane);
      const auto &q0 = a.in.ipos[int(g.pts[0])];
      const D3 p0 = {double(q0[0]), double(q0[1]), double(q0[2])};
      const double nd = dot(n, dir), h = dot(n, sub(p0, o));
      if (std::abs(nd) < 1e-3) {
        bad = true;
        break;
      }
      const double t = h / nd;
      if (t <= 0) {
        continue;
      }
      const D3 x = {o[0] + t * dir[0], o[1] + t * dir[1], o[2] + t * dir[2]};
      std::vector<Seg> segs;
      g.for_each_edge([&](uint32_t i, uint32_t j) {
        const auto &s = a.in.ipos[int(g.pts[int(i)])], &e = a.in.ipos[int(g.pts[int(j)])];
        segs.push_back({{double(s[0]), double(s[1]), double(s[2])}, {double(e[0]), double(e[1]), double(e[2])}});
      });
      const int in = in_segments(segs, x, g.axis, 1e-6);
      if (in < 0) {
        bad = true;
        break;
      }
      if (in > 0 && t < best) {
        best = t;
        region = g.region[(nd < 0) == (g.sign > 0) ? 0 : 1];
      }
    }
    if (!bad) {
      return region;
    }
  }
  return kNone;
}

struct Counts {
  uint32_t samples = 0, nonzero = 0, inner = 0, rays = 0, retries = 0, radial = 0, merged = 0;
};

void check(const Arrangement &a, Counts &n, SplitMix64 &r)
{
  n.rays += a.stats.rays;
  n.retries += a.stats.retries;
  for (uint32_t f = 0; f < a.facets.size(); f++) {
    const Facet &fc = a.facets[int(f)];
    n.merged += fc.sources.size() > 1;
    const D3 nrm = unit_normal(a, fc.plane);
    const int axis = a.in.polygons[int(fc.sources[0])].axis;
    std::vector<Seg> segs;
    std::vector<D3> pts;
    for (const csg::FacetEdge &e : fc.edges) {
      const csg::ArrEdge &x = a.edges[int(e.edge)];
      segs.push_back({pos(a, x.a), pos(a, x.b)});
      pts.push_back(pos(a, x.a));
      pts.push_back(pos(a, x.b));
    }
    int found = 0;
    for (int t = 0; t < 40 && found < 2; t++) {
      const D3 &p = pts[r.below(pts.size())], &q = pts[r.below(pts.size())], &s = pts[r.below(pts.size())];
      const double wa = real(r), wb = (1 - wa) * real(r), wc = 1 - wa - wb;
      D3 x;
      for (int i = 0; i < 3; i++) {
        x[i] = wa * p[i] + wb * q[i] + wc * s[i];
      }
      if (in_segments(segs, x, axis, 1e-4) != 1) {
        continue;
      }
      found++;
      for (uint32_t side = 0; side < 2; side++) {
        const double eps = side ? -1e-5 : 1e-5;
        const D3 o = {x[0] + eps * nrm[0], x[1] + eps * nrm[1], x[2] + eps * nrm[2]};
        for (uint32_t l = 0; l < a.leaf_count; l++) {
          const uint32_t want = oracle(a, l, o, r);
          UT_CHECK(want != kNone && a.region(f, l, side) == want);
          n.samples++;
          n.nonzero += want > 0;
          n.inner += want > 1;
        }
      }
    }
  }
  for (const csg::ArrEdge &e : a.edges) {
    if (e.uses.size() < 3) {
      continue;
    }
    n.radial++;
    /* counter-clockwise about a -> b: the angles add up to one turn */
    const D3 d = sub(pos(a, e.b), pos(a, e.a));
    double total = 0;
    for (size_t i = 0; i < e.uses.size(); i++) {
      const csg::EdgeUse &p = e.uses[int(i)], &q = e.uses[int((i + 1) % e.uses.size())];
      auto into = [&](const csg::EdgeUse &u) {
        D3 m = unit_normal(a, a.facets[int(u.facet)].plane);
        for (double &v : m) {
          v *= u.dir;
        }
        return cross(m, d);
      };
      const D3 x = into(p), y = into(q);
      double ang = std::atan2(dot(cross(x, y), d) / len(d), dot(x, y));
      total += ang < 1e-9 ? ang + 2 * 3.14159265358979323846 : ang;
    }
    UT_CHECK(std::abs(total - 2 * 3.14159265358979323846) < 1e-6);
  }
}

void test_random()
{
  Counts n;
  SplitMix64 r(21);
  for (int s = 0; s < 300; s++) {
    Operands o;
    const int count = 2 + int(r.below(3));
    for (int i = 0; i < count; i++) {
      add_random(r, 3, o);
    }
    if (s % 3 == 0) {
      GridBuilder<L> g;
      g.box({-8, -8, -8}, {8, 8, 8});
      o.add(g);
    }
    if (s % 4 == 3) {
      for (csg::Operand &x : o.ops) {
        x.place = random_placement(r);
        for (int i = 0; i < 3; i++) {
          x.place.offset[i] = int64_t(r.below(5)) - 2;
        }
      }
    }
    Arrangement a;
    UT_CHECK(csg::arrange(o.ops, a) == csg::CsgCode::Ok);
    check(a, n, r);
  }
  printf("samples %u, not void %u, second regions %u, rays %u (%u retries), radial %u, merged %u\n", n.samples,
         n.nonzero, n.inner, n.rays, n.retries, n.radial, n.merged);
  UT_CHECK(n.samples > 30000 && n.nonzero > 5000 && n.inner > 500 && n.rays > 100 && n.rays < 250 && n.retries > 20 &&
           n.radial > 2000 && n.merged > 100);
}

/* boxes, each inside the one before, and a ray that has to miss a hole */
void test_nested()
{
  Operands o;
  for (int64_t i = 0; i < 4; i++) {
    GridBuilder<L> g;
    g.box({-20 + 2 * i, -20 + 2 * i, -20 + 2 * i}, {20 - 2 * i, 20 - 2 * i, 20 - 2 * i});
    o.add(g);
  }
  {
    /* around nothing: a ring */
    GridBuilder<L> g;
    const int64_t z[2] = {-30, 30};
    for (int i = 0; i < 2; i++) {
      std::vector<I3> out = {{-28, -28, z[i]}, {28, -28, z[i]}, {28, 28, z[i]}, {-28, 28, z[i]}};
      std::vector<I3> in = {{-24, -24, z[i]}, {-24, 24, z[i]}, {24, 24, z[i]}, {24, -24, z[i]}};
      if (i == 0) {
        std::reverse(out.begin(), out.end());
        std::reverse(in.begin(), in.end());
      }
      g.face({out, in});
    }
    const I3 c[4] = {{-28, -28, 0}, {28, -28, 0}, {28, 28, 0}, {-28, 28, 0}};
    const I3 d[4] = {{-24, -24, 0}, {24, -24, 0}, {24, 24, 0}, {-24, 24, 0}};
    for (int i = 0; i < 4; i++) {
      const I3 &p = c[i], &q = c[(i + 1) % 4], &s = d[i], &t = d[(i + 1) % 4];
      g.face({{{p[0], p[1], z[0]}, {q[0], q[1], z[0]}, {q[0], q[1], z[1]}, {p[0], p[1], z[1]}}});
      g.face({{{t[0], t[1], z[0]}, {s[0], s[1], z[0]}, {s[0], s[1], z[1]}, {t[0], t[1], z[1]}}});
    }
    o.add(g);
  }
  Arrangement a;
  UT_CHECK(csg::arrange(o.ops, a) == csg::CsgCode::Ok);
  UT_CHECK(a.facets.size() == 24 + 10 && a.leaf_count == 5);
  for (uint32_t f = 0; f < a.facets.size(); f++) {
    const uint32_t own = a.in.polygons[int(a.facets[int(f)].sources[0])].leaf;
    for (uint32_t l = 0; l < 5; l++) {
      for (uint32_t side = 0; side < 2; side++) {
        const uint32_t got = a.region(f, l, side);
        if (l != own) {
          UT_CHECK(got == (own < 4 && l < own ? 1u : 0u));
        }
      }
      if (l == own) {
        UT_CHECK(a.region(f, l, 0) + a.region(f, l, 1) == 1);
      }
    }
  }
}

/* a triangle in a box such that the ray of t = 1, (1, 1, 1) from the middle of
 * one of its edges, hits a corner, an edge or the diagonal of a face */
void test_degenerate_rays()
{
  const I3 tri[3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 2}};
  std::vector<std::array<I3, 2>> boxes = {{{{-5, -5, -5}, {5, 5, 5}}},
                                          {{{-3, -2, -9}, {11, 10, 4}}},
                                          {{{-9, -2, -3}, {4, 10, 11}}}};
  for (int64_t k = 5; k < 25; k++) {
    /* (loops start at the smallest ID, which depends on the position) */
    boxes.push_back({{{-k, -5, -5}, {5, 9, 5}}});
  }
  for (int e = 0; e < 3; e++) {
    for (const auto &b : boxes) {
      for (int first = 0; first < 2; first++) {
        Operands o;
        GridBuilder<L> g, h;
        g.face({{tri[0], tri[1], tri[2]}});
        h.box(b[0], b[1]);
        o.add(first ? g : h);
        o.add(first ? h : g);
        csg::Operand &t = o.ops[first ? 0 : 1];
        for (int i = 0; i < 3; i++) {
          t.place.offset[i] = -(tri[e][i] + tri[(e + 1) % 3][i]) / 2;
        }
        Arrangement a;
        UT_CHECK(csg::arrange(o.ops, a) == csg::CsgCode::Ok);
        const uint32_t tl = first ? 0 : 1;
        for (uint32_t f = 0; f < a.facets.size(); f++) {
          const bool sheet = a.in.polygons[int(a.facets[int(f)].sources[0])].leaf == tl;
          for (uint32_t side = 0; side < 2; side++) {
            UT_CHECK(a.region(f, tl, side) == 0);
            UT_CHECK(!sheet || a.region(f, 1 - tl, side) == 1);
          }
        }
        UT_CHECK(a.facets.size() == 7);
      }
    }
  }
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_nested();
  test_degenerate_rays();
  test_random();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
