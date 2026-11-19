/* cesaro2 (plan P4a W2, §6.1): evaluation against the quad oracle within the
 * certified bounds; derivative bounds against sampled derivatives (and
 * tight to 1000x); invariances; canonical form and strict reads. */

#include <cmath>
#include <cstdio>

#include "litestl/math/detmath.h"
#include "support/cesaro_fixtures.h"
#include "support/splitmix64.h"
#include "ubertopo/curves/cesaro.h"
#include "ut_test.h"

#include "data/curve_oracle.inc"

using namespace ubertopo::curves;
using ubertopo::test::CesaroFixture;
namespace dm = litestl::math::detmath;

namespace {

Cesaro2 build(const CesaroFixture &f)
{
  util::Vector<CesaroSegment, 4> segs;
  for (auto &s : f.segs) {
    segs.append({s[0], Poly::exact({s[1], s[2], s[3], s[4]})});
  }
  Cesaro2 c;
  const char *err = Cesaro2::make(f.x0, f.y0, f.th0, segs, c);
  if (err) {
    fprintf(stderr, "%s: %s\n", f.name, err);
  }
  UT_CHECK(err == nullptr);
  return c;
}

double half_ulp(double x)
{
  return 0.5 * dm::ulp(x);
}

void test_oracle()
{
  const auto fx = ubertopo::test::cesaro_fixtures();
  UT_CHECK(int(fx.size()) == int(sizeof(kCesaroOracle) / sizeof(kCesaroOracle[0])));
  for (size_t i = 0; i < fx.size(); i++) {
    const Cesaro2 c = build(fx[i]);
    const auto ev = c.evaluator();
    const auto &set = kCesaroOracle[i];
    UT_CHECK(set.n == int(fx[i].stations.size()));
    double worst = 0, worst_t = 0;
    int bad = 0;
    for (int k = 0; k < set.n; k++) {
      const auto &o = set.samples[k];
      Vec<2> d[2];
      ev->eval(o.s, 1, d);
      const double ep = std::hypot(d[0][0] - o.x, d[0][1] - o.y);
      const double et = std::hypot(d[1][0] - o.tx, d[1][1] - o.ty);
      const double bp = ev->eval_error() + half_ulp(o.x) + half_ulp(o.y);
      const double bt = ev->deriv_eval_error(1) + 2 * 0x1p-53;
      bad += ep > bp;
      bad += et > bt;
      worst = std::max(worst, ep / bp);
      worst_t = std::max(worst_t, et / bt);
    }
    printf("%-15s pieces %5d  eval_error %.3g  worst position err/bound %.3g  tangent %.3g\n", fx[i].name,
           int(c.pieces.size()), ev->eval_error(), worst, worst_t);
    UT_CHECK(bad == 0);
    UT_CHECK(ev->eval_error() > 0);
    if (std::string_view(fx[i].name) == "chain_10km") {
      UT_CHECK(ev->eval_error() <= 1e-9);
    }
  }
}

/* deriv_bound contains sampled |r^(k)| between special parameters, and is
 * within 1000x of the sampled maximum; r'' agrees with a central difference
 * of r' */
void test_deriv_bounds()
{
  const auto fx = ubertopo::test::cesaro_fixtures();
  for (const CesaroFixture &f : fx) {
    const Cesaro2 c = build(f);
    const auto ev = c.evaluator();
    const Interval dom = ev->domain();
    util::Vector<SpecialParam> sp;
    ev->special_params(dom.lo, dom.hi, sp);
    util::Vector<double> cuts;
    cuts.append(dom.lo);
    for (const SpecialParam &p : sp) {
      if (p.t > cuts.last() && p.t < dom.hi) {
        cuts.append(p.t);
      }
    }
    cuts.append(dom.hi);
    for (int ci = 0; ci + 1 < cuts.size(); ci++) {
      /* sub-ranges of up to 1/8 of the span, to exercise local bounds */
      const double a0 = cuts[ci], b0 = cuts[ci + 1];
      for (int part = 0; part < 8; part++) {
        const double a = a0 + (b0 - a0) * part / 8, b = a0 + (b0 - a0) * (part + 1) / 8;
        for (int k = 2; k <= 6; k++) {
          const double bound = ev->deriv_bound(a, b, k);
          double mx = 0;
          for (int s = 0; s <= 40; s++) {
            Vec<2> d[7];
            /* one-sided at the range ends: a range ending at a join covers the
             * segment before it */
            ev->eval(s == 40 ? b : a + (b - a) * s / 40, 6, d, s == 40 ? Side::Below : Side::Above);
            mx = std::max(mx, std::hypot(d[k][0], d[k][1]));
          }
          if (!(mx <= bound * (1 + 1e-12) + 1e-300)) {
            fprintf(stderr, "%s k=%d [%.17g, %.17g]: sampled %.17g > bound %.17g\n", f.name, k, a, b, mx, bound);
          }
          UT_CHECK(mx <= bound * (1 + 1e-12) + 1e-300);
          if (mx > 1e-200 && !(bound <= 1000 * mx)) {
            fprintf(stderr, "%s k=%d [%g, %g]: bound %g vs sampled %g\n", f.name, k, a, b, bound, mx);
            UT_CHECK(bound <= 1000 * mx);
          }
        }
        /* central difference of the tangent */
        const double t = 0.5 * (a + b), h = std::max(1e-3, 1e-6 * dom.hi);
        if (t - h > a0 && t + h < b0) {
          Vec<2> d0[3], dm1[2], dp1[2];
          ev->eval(t, 2, d0);
          ev->eval(t - h, 1, dm1);
          ev->eval(t + h, 1, dp1);
          const double fdx = (dp1[1][0] - dm1[1][0]) / (2 * h), fdy = (dp1[1][1] - dm1[1][1]) / (2 * h);
          const double k3 = ev->deriv_bound(a0, b0, 4); /* |r'''' | bounds the difference error */
          UT_CHECK(std::hypot(fdx - d0[2][0], fdy - d0[2][1]) <= h * h * k3 + 1e-10 / h);
        }
      }
    }
  }
}

void test_invariance()
{
  const auto fx = ubertopo::test::cesaro_fixtures();
  litestl::test::SplitMix64 r(3);
  for (const CesaroFixture &f : fx) {
    const Cesaro2 c = build(f);
    const auto ev = c.evaluator();
    const double L = c.length(), e = ev->eval_error();
    /* arc length: a chord never exceeds the parameter difference */
    for (int i = 0; i < 200; i++) {
      const double s1 = L * r.unit(), s2 = L * r.unit();
      Vec<2> p1, p2;
      ev->eval(s1, 0, &p1);
      ev->eval(s2, 0, &p2);
      UT_CHECK(std::hypot(p1[0] - p2[0], p1[1] - p2[1]) <= std::fabs(s1 - s2) + 2 * e);
    }
    /* reversal: start at the end pose facing back, kappa(sigma) -> -kappa(L - sigma) */
    {
      Vec<2> end[2];
      ev->eval(L, 1, end);
      util::Vector<CesaroSegment, 4> rs;
      for (int i = c.segs.size(); i-- > 0;) {
        const Poly &k = c.segs[i].kappa;
        rs.append({c.segs[i].length, k.compose_affine(c.segs[i].length, -1.0).scaled(-1.0)});
      }
      for (CesaroSegment &s : rs) {
        for (double &x : s.kappa.e) {
          x = 0; /* compose rounds: the reversed definition is the rounded one */
        }
      }
      Cesaro2 rev;
      UT_CHECK(Cesaro2::make(end[0][0], end[0][1], std::atan2(-end[1][1], -end[1][0]), rs, rev) == nullptr);
      const auto er = rev.evaluator();
      /* the reversed definition differs from the exact reversal by the end
       * pose's error and the rounding of the reversed kappa coefficients */
      const double tol = 2 * e + er->eval_error() + ev->deriv_eval_error(1) * L + 1e-12 * L;
      for (int i = 0; i <= 16; i++) {
        const double s = L * i / 16;
        Vec<2> a, b;
        ev->eval(s, 0, &a);
        er->eval(L - s, 0, &b);
        UT_CHECK(std::hypot(a[0] - b[0], a[1] - b[1]) <= tol);
      }
    }
    /* rigid motion commutes with evaluation */
    {
      const double phi = 0.75, tx = 321.5, ty = -77.25;
      double sp, cp;
      dm::sincos(phi, &sp, &cp);
      util::Vector<CesaroSegment, 4> segs = c.segs;
      Cesaro2 m;
      UT_CHECK(Cesaro2::make(cp * c.x0 - sp * c.y0 + tx, sp * c.x0 + cp * c.y0 + ty, c.theta0 + phi, segs, m) ==
               nullptr);
      const auto em = m.evaluator();
      for (int i = 0; i <= 16; i++) {
        const double s = L * i / 16;
        Vec<2> a, b;
        ev->eval(s, 0, &a);
        em->eval(s, 0, &b);
        const double x = cp * a[0] - sp * a[1] + tx, y = sp * a[0] + cp * a[1] + ty;
        const double tol = e + em->eval_error() + 1e-15 * (std::fabs(x) + std::fabs(y) + L) * 8;
        UT_CHECK(std::hypot(x - b[0], y - b[1]) <= tol);
      }
    }
  }
}

void test_canonical()
{
  attr::PoolStore store;
  const uint32_t pid = store.add_pool<Cesaro2Pool>("curves.cesaro2");
  util::Vector<CesaroSegment, 4> a, b;
  a.append({50.0, Poly::exact({0.01, 1e-4})});
  b.append({0.0, Poly::exact({5.0})});               /* dropped */
  b.append({50.0, Poly::exact({0.01, 1e-4, 0.0, -0.0})}); /* trailing zeros */
  Cesaro2 ca, cb;
  UT_CHECK(Cesaro2::make(0.0, 1.0, 0.5, a, ca) == nullptr);
  UT_CHECK(Cesaro2::make(-0.0, 1.0, 0.5, b, cb) == nullptr);
  UT_CHECK(ca.equal(cb));
  /* the heading is reduced to (-pi, pi] (0.5 + 2 pi is a different double,
   * so it reduces to a neighbour of 0.5, not to 0.5 itself) */
  {
    Cesaro2 cr;
    UT_CHECK(Cesaro2::make(0.0, 1.0, 0.5 + 2 * 3.141592653589793, a, cr) == nullptr);
    UT_CHECK(std::fabs(cr.theta0 - 0.5) < 1e-14 && cr.theta0 > -3.2 && cr.theta0 <= 3.1415926535897931);
  }
  const attr::PoolRef ra = store.intern<Cesaro2Pool>(pid, std::move(ca));
  const attr::PoolRef rb = store.intern<Cesaro2Pool>(pid, std::move(cb));
  UT_CHECK(ra == rb);
  /* refusals */
  Cesaro2 x;
  util::Vector<CesaroSegment, 4> bad;
  bad.append({10.0, Poly::exact({NAN})});
  UT_CHECK(Cesaro2::make(0, 0, 0, bad, x) != nullptr);
  bad[0] = {-1.0, Poly::exact({0.0})};
  UT_CHECK(Cesaro2::make(0, 0, 0, bad, x) != nullptr);
  bad[0] = {10.0, Poly::exact({0.0, 0.0, 0.0, 0.0, 1e-9})};
  UT_CHECK(Cesaro2::make(0, 0, 0, bad, x) != nullptr);
  UT_CHECK(Cesaro2::make(0, 0, INFINITY, a, x) != nullptr);
  /* write / read; a non-canonical stream (unreduced heading) is refused */
  const Cesaro2 &e = store.get<Cesaro2Pool>(ra);
  litestl::io::ByteWriter w;
  e.write(w);
  litestl::io::ByteReader rd(w.data(), w.size());
  Cesaro2 back;
  UT_CHECK(Cesaro2::read(rd, back, {}) && back.equal(e) && back.pieces.size() == e.pieces.size());
  Cesaro2 raw = e;
  raw.theta0 = e.theta0 + 6.5;
  litestl::io::ByteWriter w2;
  raw.write(w2);
  litestl::io::ByteReader rd2(w2.data(), w2.size());
  UT_CHECK(!Cesaro2::read(rd2, back, {}));
  /* the kind through the registry */
  attr::PoolStore s2;
  register_pools(s2);
  Cesaro2 c3;
  UT_CHECK(Cesaro2::arc(0, 0, 0, 0.02, 100, c3) == nullptr);
  const attr::PoolRef r3 = s2.intern<Cesaro2Pool>(1, std::move(c3));
  UT_CHECK(curve_dim(s2, r3) == 2 && bool(curve2(s2, r3)) && !curve3(s2, r3));
}

} // namespace

int main()
{
  test_oracle();
  test_deriv_bounds();
  test_invariance();
  test_canonical();
  return ut_test_end();
}
