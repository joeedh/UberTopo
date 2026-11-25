#pragma once

/* Results as triangles in floating point, for the tests of their repair:
 * distances, volumes, winding numbers. */

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "csg_oracle.h"
#include "ubertopo/csg/output.h"
#include "ubertopo/csg/triangulate.h"

namespace ubertopo::test {

struct Tri {
  D3 p[3];
  uint32_t solid[2]; /* front, back */
};

inline D3 at(const csg::I3 &p)
{
  return {double(p[0]), double(p[1]), double(p[2])};
}

/* from a point to a triangle */
inline double distance(const D3 &p, const Tri &t)
{
  const D3 n = cross(sub(t.p[1], t.p[0]), sub(t.p[2], t.p[0]));
  const double l = len(n);
  bool inside = l > 0;
  double best = 1e300;
  for (int i = 0; i < 3; i++) {
    const D3 &a = t.p[i], &b = t.p[(i + 1) % 3];
    const D3 d = sub(b, a), w = sub(p, a);
    inside = inside && dot(cross(d, w), n) >= 0;
    const double dd = dot(d, d), s = dd > 0 ? std::clamp(dot(w, d) / dd, 0.0, 1.0) : 0.0;
    best = std::min(best, len({w[0] - s * d[0], w[1] - s * d[1], w[2] - s * d[2]}));
  }
  return inside ? std::abs(dot(sub(p, t.p[0]), n)) / l : best;
}

inline double distance(const D3 &p, const std::vector<Tri> &s)
{
  double best = 1e300;
  for (const Tri &t : s) {
    best = std::min(best, distance(p, t));
  }
  return best;
}

inline double area(const std::vector<Tri> &s)
{
  double sum = 0;
  for (const Tri &t : s) {
    sum += len(cross(sub(t.p[1], t.p[0]), sub(t.p[2], t.p[0]))) / 2;
  }
  return sum;
}

/* by label: six times it, about the first point */
inline std::map<uint32_t, double> volumes(const std::vector<Tri> &s)
{
  std::map<uint32_t, double> v;
  for (const Tri &t : s) {
    const D3 o = s[0].p[0];
    const double x = dot(sub(t.p[0], o), cross(sub(t.p[1], o), sub(t.p[2], o)));
    v[t.solid[1]] += x;
    v[t.solid[0]] -= x;
  }
  v.erase(csg::kVoid);
  return v;
}

/* the exact result, in operand coordinates */
inline std::vector<Tri> exact_triangles(const csg::Arrangement &a, const csg::Result &res)
{
  std::vector<Tri> out;
  litestl::util::Vector<csg::PointTriangle> tris;
  for (const csg::ResultFace &f : res.faces) {
    UT_CHECK(csg::triangulate_face(a, f, tris) == csg::CsgCode::Ok);
    for (const csg::PointTriangle &t : tris) {
      Tri x;
      for (int i = 0; i < 3; i++) {
        const D3 p = pos(a, t[i]);
        for (int c = 0; c < 3; c++) {
          x.p[i][c] = p[c] - double(a.in.shift[c]);
        }
      }
      x.solid[0] = f.solid[0];
      x.solid[1] = f.solid[1];
      out.push_back(x);
    }
  }
  return out;
}

/* the complex as an operand, for its polygons; moved back by its shift */
inline std::vector<Tri> output_triangles(const LComplex &k, const csg::OutputReport &rep, csg::Arrangement &b)
{
  std::vector<Tri> out;
  const csg::Operand op{&k, {}};
  UT_CHECK(csg::read_operands({&op, 1}, b.in) == csg::CsgCode::Ok);
  for (const csg::Polygon &g : b.in.polygons) {
    /* (a fan has the right sums) */
    for (int l = 0; l + 1 < int(g.off.size()); l++) {
      for (uint32_t i = g.off[l]; i < g.off[l + 1]; i++) {
        const uint32_t q[3] = {g.pts[0], g.pts[int(i)], g.pts[int(i + 1 < g.off[l + 1] ? i + 1 : g.off[l])]};
        Tri x;
        for (int j = 0; j < 3; j++) {
          x.p[j] = sub(at(b.in.ipos[int(q[j])]), at(b.in.shift));
        }
        x.solid[0] = rep.regions[int(g.region[0])].solid;
        x.solid[1] = rep.regions[int(g.region[1])].solid;
        out.push_back(x);
      }
    }
  }
  return out;
}

/* by rays in floating point; empty: no luck */
inline std::map<uint32_t, int> winding(const csg::Input &in, const D3 &o, SplitMix64 &r)
{
  for (int attempt = 0; attempt < 100; attempt++) {
    const D3 dir = {real(r) - 0.5, real(r) - 0.5, real(r) - 0.5};
    std::map<uint32_t, int> w = {{csg::kVoid, 1}};
    bool bad = false;
    for (const csg::Polygon &g : in.polygons) {
      const csg::I3 &pn = in.planes[g.plane].n;
      const D3 n = at(pn);
      const double l = len(n), nd = dot(n, dir) / l;
      if (std::abs(nd) < 1e-3) {
        bad = true;
        break;
      }
      const double t = dot(n, sub(at(in.ipos[int(g.pts[0])]), o)) / l / nd;
      if (t <= 0) {
        continue;
      }
      const D3 x = {o[0] + t * dir[0], o[1] + t * dir[1], o[2] + t * dir[2]};
      std::vector<Seg> segs;
      g.for_each_edge([&](uint32_t i, uint32_t j) {
        segs.push_back({at(in.ipos[int(g.pts[int(i)])]), at(in.ipos[int(g.pts[int(j)])])});
      });
      const int hit = in_segments(segs, x, g.axis, 1e-7);
      if (hit < 0) {
        bad = true;
        break;
      }
      if (hit > 0) {
        const uint32_t near = (nd < 0) == (g.sign > 0) ? 0 : 1;
        w[g.region[near]]++;
        w[g.region[1 - near]]--;
      }
    }
    if (!bad) {
      return w;
    }
  }
  return {};
}

} // namespace ubertopo::test
