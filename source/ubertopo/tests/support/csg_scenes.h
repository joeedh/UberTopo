#pragma once

/* Random operands for the Boolean engine's tests: small integer coordinates,
 * so that they touch, overlap and share planes all the time. */

#include <algorithm>
#include <memory>
#include <vector>

#include "bigint_oracle.h"
#include "splitmix64.h"
#include "topo_fixtures.h"
#include "ubertopo/csg/arrangement.h"
#include "ubertopo/topo/regions.h"

namespace ubertopo::test {

using L = geom::Lattice;
using LComplex = topo::Complex<L>;
using litestl::test::SplitMix64;

struct Operands {
  std::vector<std::unique_ptr<LComplex>> k;
  std::vector<csg::Operand> ops;
  void add(const GridBuilder<L> &g, const topo::BuildPolicy &policy = {})
  {
    auto c = std::make_unique<LComplex>();
    topo::BuildReport br;
    topo::RegionReport rr;
    const bool built = g.b.build(*c, br, policy) && topo::build_regions(*c, rr);
    UT_CHECK(built);
    ops.push_back({c.get(), {}});
    k.push_back(std::move(c));
  }
};

inline I3 vertex(SplitMix64 &r, int64_t range)
{
  auto c = [&] { return int64_t(r.below(uint64_t(2 * range + 1))) - range; };
  return {c(), c(), c()};
}

inline int orientation(const I3 &a, const I3 &b, const I3 &c, const I3 &d)
{
  using litestl::test::BigInt;
  std::vector<std::vector<BigInt>> m;
  for (const I3 *p : {&b, &c, &d}) {
    m.push_back({});
    for (int i = 0; i < 3; i++) {
      m.back().push_back(BigInt::from_i64((*p)[i] - a[i]));
    }
  }
  return BigInt::det(m).sign();
}

/* a tetrahedron (outward faces), a box, a tilted plate with a hole, an
 * L-shaped sheet, or two rooms with a wall between them */
inline void add_random(SplitMix64 &r, int64_t range, Operands &out)
{
  GridBuilder<L> g;
  const uint64_t kind = r.below(range > 1000 ? 2 : 5);
  if (kind == 0) {
    I3 v[4];
    int d = 0;
    while (d == 0) {
      for (I3 &x : v) {
        x = vertex(r, range);
      }
      d = orientation(v[0], v[1], v[2], v[3]);
    }
    if (d < 0) {
      std::swap(v[0], v[1]);
    }
    g.face({{v[0], v[2], v[1]}});
    g.face({{v[0], v[1], v[3]}});
    g.face({{v[1], v[2], v[3]}});
    g.face({{v[0], v[3], v[2]}});
  }
  else if (kind == 1) {
    I3 a, b;
    do {
      a = vertex(r, range);
      b = vertex(r, range);
    } while (a[0] == b[0] || a[1] == b[1] || a[2] == b[2]);
    g.box({std::min(a[0], b[0]), std::min(a[1], b[1]), std::min(a[2], b[2])},
          {std::max(a[0], b[0]), std::max(a[1], b[1]), std::max(a[2], b[2])});
  }
  else if (kind == 2) {
    /* a one-face sheet: a square with a triangular hole, in a tilted plane */
    const I3 o = vertex(r, 2);
    const I3 s = {int64_t(r.below(3)) - 1, int64_t(r.below(3)) - 1, 1};
    auto p = [&](int64_t x, int64_t y) -> I3 { return {o[0] + x, o[1] + y, o[2] + s[0] * x + s[1] * y}; };
    const int64_t w = 3 + int64_t(r.below(3));
    g.face({{p(-w, -w), p(w, -w), p(w, w), p(-w, w)}, {p(-1, -1), p(-1, 1), p(2, 0)}});
  }
  else if (kind == 3) {
    /* an L-shaped sheet */
    const I3 o = vertex(r, 2);
    const int64_t w = 2 + int64_t(r.below(3));
    const int ax = int(r.below(3));
    auto p = [&](int64_t x, int64_t y) {
      I3 q = o;
      q[(ax + 1) % 3] += x;
      q[(ax + 2) % 3] += y;
      return q;
    };
    std::vector<I3> loop = {p(0, 0), p(w, 0), p(2 * w, 0), p(2 * w, w), p(w, w), p(w, 2 * w), p(0, 2 * w)};
    std::rotate(loop.begin(), loop.begin() + std::ptrdiff_t(r.below(loop.size())), loop.end());
    g.face({loop});
  }
  else {
    I3 o = vertex(r, 2);
    const int ax = int(r.below(3));
    const I3 w = {1 + int64_t(r.below(3)), 1 + int64_t(r.below(3)), 1 + int64_t(r.below(3))};
    I3 m = o;
    m[ax] += w[ax];
    g.box(o, {o[0] + w[0], o[1] + w[1], o[2] + w[2]});
    g.box(m, {m[0] + w[0], m[1] + w[1], m[2] + w[2]});
  }
  out.add(g);
}

inline csg::GridPlacement random_placement(SplitMix64 &r)
{
  csg::GridPlacement p;
  int8_t ax[3] = {0, 1, 2};
  for (int i = 2; i > 0; i--) {
    std::swap(ax[i], ax[r.below(uint64_t(i) + 1)]);
  }
  for (int i = 0; i < 3; i++) {
    p.axis[i] = ax[i];
    p.sign[i] = r.below(2) ? 1 : -1;
    p.offset[i] = int64_t(r.below(2001)) - 1000;
  }
  return p;
}

} // namespace ubertopo::test
