/* bench_curves (plan P4a §6.4): reported, not gated. Not in ctest: run by
 * hand.
 *  - cesaro2 evaluation throughput (positions, and up to r'');
 *  - flattening and quintic Hermite approximation of a highway clothoid and a
 *    15 m junction corner at 0.1 mm: piece counts and times;
 *  - the approximation of a lane edge (offset2 of the clothoid);
 *  - bound-to-measured ratios: the approximation's max_error against the
 *    sampled deviation, and deriv_bound(2) against the sampled max |r''|;
 *  - content_hash on a ~10^5-face city with and without 10^4 curved edges. */

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "gen/buildings.h"
#include "support/curve_fixture.h"
#include "ubertopo/curves/approx.h"
#include "ubertopo/curves/bspline.h"
#include "ubertopo/curves/offset.h"
#include "ubertopo/topo/io.h"
#include "ubertopo/topo/regions.h"

using namespace ubertopo;
using namespace ubertopo::curves;
using test::gen::GridOpts;
using test::gen::Model;

namespace {

using Clock = std::chrono::steady_clock;

double us_since(Clock::time_point t0)
{
  return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

template <int D> double dist(const Vec<D> &a, const Vec<D> &b)
{
  double s = 0;
  for (int i = 0; i < D; i++) {
    s += (a[i] - b[i]) * (a[i] - b[i]);
  }
  return std::sqrt(s);
}

void throughput(const Curve2 &c)
{
  const Interval d = c.domain();
  const int n = 1000000;
  Vec<2> out[3];
  double sink = 0;
  for (int k : {0, 2}) {
    const auto t0 = Clock::now();
    for (int i = 0; i < n; i++) {
      c.eval(d.lo + (d.hi - d.lo) * (double(i) / n), k, out);
      sink += out[0][0];
    }
    const double us = us_since(t0);
    printf("  cesaro2 eval (n = %d): %.1f Mevals/s\n", k, n / us);
  }
  if (sink == 12345) {
    printf("%g\n", sink); /* keeps the loop */
  }
}

/* flatten + approximate src over its domain at tol; sampled ratios */
void approx_case(const char *name, attr::PoolStore &s, attr::PoolRef src, double tol)
{
  const auto c = curve2(s, src);
  const Interval d = c->domain();
  util::Vector<double> pts;
  auto t0 = Clock::now();
  const bool fok = flatten<2>(*c, d.lo, d.hi, tol, pts);
  const double fus = us_since(t0);
  const char *err = nullptr;
  t0 = Clock::now();
  const attr::PoolRef sp = approximate(s, src, d.lo, d.hi, tol, &err);
  const double aus = us_since(t0);
  if (!fok || sp.none()) {
    printf("  %s: failed (%s)\n", name, err ? err : "flatten");
    return;
  }
  const BSpline &b = s.get<BSplinePool>(sp);
  const auto e = curve2(s, sp);
  double measured = 0, r2max = 0;
  const int n = 20000;
  for (int i = 0; i <= n; i++) {
    const double t = d.lo + (d.hi - d.lo) * (double(i) / n);
    Vec<2> a[3], bb;
    c->eval(t, 2, a);
    e->eval(t, 0, &bb);
    measured = std::fmax(measured, dist<2>(a[0], bb));
    r2max = std::fmax(r2max, std::sqrt(a[2][0] * a[2][0] + a[2][1] * a[2][1]));
  }
  const int pieces = b.count() - b.degree;
  printf("  %s: length %.2f; flatten %d points %.0f us; approx %d control points (~%d spans) %.0f us\n", name,
         d.hi - d.lo, int(pts.size()), fus, b.count(), pieces, aus);
  printf("    max_error %.3g, measured %.3g (bound/measured %.1f); deriv_bound(2) %.4g, sampled |r''| %.4g (%.2f)\n",
         b.max_error, measured, measured > 0 ? b.max_error / measured : 0.0, c->deriv_bound(d.lo, d.hi, 2), r2max,
         r2max > 0 ? c->deriv_bound(d.lo, d.hi, 2) / r2max : 0.0);
}

void library()
{
  attr::PoolStore s;
  register_pools(s);
  /* a highway clothoid: kappa 0 -> 1/1000 over 200 m; a 15 m radius corner */
  Cesaro2 hw, corner;
  if (Cesaro2::clothoid(0, 0, 0.3, 0, 1.0 / 1000, 200, hw) ||
      Cesaro2::arc(0, 0, 0, 1.0 / 15, 15 * 1.5707963267948966, corner))
  {
    printf("make failed\n");
    return;
  }
  const attr::PoolRef rhw = s.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(hw));
  const attr::PoolRef rc = s.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(corner));
  printf("library:\n");
  throughput(*curve2(s, rhw));
  approx_case("highway clothoid, 0.1 mm", s, rhw, 1e-4);
  approx_case("15 m corner, 0.1 mm", s, rc, 1e-4);
  Offset2 lane;
  if (Offset2::make(s, rhw, Fn1D::constant(3.5, 0, 200), lane)) {
    printf("offset make failed\n");
    return;
  }
  const attr::PoolRef rl = s.intern<Offset2Pool>(pool_id(offset2_kind()), std::move(lane));
  approx_case("lane edge (offset 3.5 m), 0.1 mm", s, rl, 1e-4);
}

void city_hash()
{
  topo::Complex<double> k;
  Model<double> m(ident::derive(ident::tag::TestOnly, std::string_view("bench-city")));
  GridOpts g;
  g.nx = 126;
  g.ny = 126;
  g.floors = 2;
  g.cell = 400;
  g.height = 300;
  test::gen::room_grid(m, {0, 0, 0}, g, 1);
  topo::BuildReport br;
  topo::RegionReport rr;
  if (!m.b.build(k, br, m.policy) || !topo::build_regions(k, rr)) {
    printf("city build failed\n");
    return;
  }
  auto timed_hash = [&]() {
    const auto t0 = Clock::now();
    ident::PersistentId h;
    for (int i = 0; i < 3; i++) {
      h = topo::content_hash(k);
    }
    return us_since(t0) / 3;
  };
  const double straight = timed_hash();
  /* 10^4 curved edges: arcs between the ends of horizontal edges */
  auto &curve = k.edges.get<".e.curve">();
  auto &range = k.edges.get<".e.range">();
  uint32_t curved = 0;
  const auto t0 = Clock::now();
  for (uint32_t e = 0; e < k.edges.capacity() && curved < 10000; e++) {
    if (!k.edges.live(e)) {
      continue;
    }
    const auto vs = k.edge_verts(topo::EdgeRef(e));
    const auto a = geom::to_double3<double>(k.pos(vs[0])), b = geom::to_double3<double>(k.pos(vs[1]));
    if (a[2] != b[2]) {
      continue;
    }
    Cesaro2 h;
    if (test::two_point_arc(a[0], a[1], b[0], b[1], 0.2, h)) {
      continue;
    }
    const double len = h.length();
    const attr::PoolRef hr = k.geom.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(h));
    Alignment3 al;
    if (Alignment3::make(k.geom, hr, Fn1D::constant(a[2], 0, len), al)) {
      continue;
    }
    curve.set(e, k.geom.intern<Alignment3Pool>(pool_id(alignment3_kind()), std::move(al)));
    range.set(e, litestl::math::double2(0, len));
    curved++;
  }
  const double setup = us_since(t0);
  const double with_curves = timed_hash();
  printf("content_hash, %u faces, %u edges: straight %.1f ms; with %u curved edges %.1f ms (%.2fx; interning %.1f ms)\n",
         k.faces.live_count(), k.edges.live_count(), straight / 1000, curved, with_curves / 1000,
         with_curves / straight, setup / 1000);
}

} // namespace

int main()
{
  topo::register_column_types();
  library();
  city_hash();
  return 0;
}
