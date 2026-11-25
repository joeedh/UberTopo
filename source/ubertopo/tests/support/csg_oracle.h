#pragma once

/* Points off the facets of an arrangement, and the regions there by rays in
 * floating point. Scenes are small, so a margin makes it safe. */

#include <array>
#include <cmath>

#include "csg_scenes.h"

namespace ubertopo::test {

using D3 = std::array<double, 3>;

inline double real(SplitMix64 &r)
{
  return double(r.next() >> 11) / double(uint64_t(1) << 53);
}

inline D3 pos(const csg::Arrangement &a, uint32_t p)
{
  const csg::HPoint &h = a.in.points[p];
  return {h.x[0].to_double() / h.w.to_double(), h.x[1].to_double() / h.w.to_double(),
          h.x[2].to_double() / h.w.to_double()};
}

inline D3 sub(const D3 &a, const D3 &b)
{
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
inline D3 cross(const D3 &a, const D3 &b)
{
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline double dot(const D3 &a, const D3 &b)
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline double len(const D3 &a)
{
  return std::sqrt(dot(a, a));
}

inline D3 unit_normal(const csg::Arrangement &a, uint32_t plane)
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
inline int in_segments(const std::vector<Seg> &segs, const D3 &p, int axis, double margin)
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
inline uint32_t oracle(const csg::Arrangement &a, uint32_t leaf, const D3 &o, SplitMix64 &r)
{
  for (int attempt = 0; attempt < 100; attempt++) {
    const D3 dir = {real(r) - 0.5, real(r) - 0.5, real(r) - 0.5};
    double best = 1e9;
    uint32_t region = 0;
    bool bad = false;
    for (const csg::Polygon &g : a.in.polygons) {
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
  return csg::kNone;
}

/** Up to @p count points inside a facet, off its boundary. */
inline std::vector<D3> inner_points(const csg::Arrangement &a, uint32_t f, int count, SplitMix64 &r)
{
  const csg::Facet &fc = a.facets[int(f)];
  const int axis = a.in.polygons[int(fc.sources[0])].axis;
  std::vector<Seg> segs;
  std::vector<D3> pts, out;
  for (const csg::FacetEdge &e : fc.edges) {
    const csg::ArrEdge &x = a.edges[int(e.edge)];
    segs.push_back({pos(a, x.a), pos(a, x.b)});
    pts.push_back(pos(a, x.a));
    pts.push_back(pos(a, x.b));
  }
  for (int t = 0; t < 40 && int(out.size()) < count; t++) {
    const D3 &p = pts[r.below(pts.size())], &q = pts[r.below(pts.size())], &s = pts[r.below(pts.size())];
    const double wa = real(r), wb = (1 - wa) * real(r), wc = 1 - wa - wb;
    D3 x;
    for (int i = 0; i < 3; i++) {
      x[i] = wa * p[i] + wb * q[i] + wc * s[i];
    }
    if (in_segments(segs, x, axis, 1e-4) == 1) {
      out.push_back(x);
    }
  }
  return out;
}

/** Just off a plane, on one side of it. */
inline D3 off_plane(const D3 &x, const D3 &normal, uint32_t side)
{
  const double eps = side ? -1e-5 : 1e-5;
  return {x[0] + eps * normal[0], x[1] + eps * normal[1], x[2] + eps * normal[2]};
}

} // namespace ubertopo::test
