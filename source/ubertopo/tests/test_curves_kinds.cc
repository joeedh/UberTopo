/* The other P4a kinds (plan P4a W4, §6.1 test_curves_kinds): offset2,
 * alignment3, catenary3, polyline3 and bspline against independent
 * references (the cesaro2 quad oracle, long double formulas, Cox-de Boor in
 * long double), their special parameters, and derivative bounds against
 * sampled derivatives for every kind. */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "support/cesaro_fixtures.h"
#include "support/splitmix64.h"
#include "ubertopo/curves/alignment.h"
#include "ubertopo/curves/bspline.h"
#include "ubertopo/curves/catenary.h"
#include "ubertopo/curves/cesaro.h"
#include "ubertopo/curves/offset.h"
#include "ubertopo/curves/polyline.h"
#include "ut_test.h"

#include "data/curve_oracle.inc"

using namespace ubertopo::curves;
using ubertopo::test::CesaroFixture;
using LD = long double;

namespace {

attr::PoolRef add_cesaro(attr::PoolStore &s, const CesaroFixture &f)
{
  util::Vector<CesaroSegment, 4> sg;
  for (auto &x : f.segs) {
    sg.append({x[0], Poly::exact({x[1], x[2], x[3], x[4]})});
  }
  Cesaro2 c;
  UT_CHECK(Cesaro2::make(f.x0, f.y0, f.th0, sg, c) == nullptr);
  return s.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(c));
}

const CesaroFixture &fixture(const std::vector<CesaroFixture> &fx, const char *name, int *index)
{
  for (size_t i = 0; i < fx.size(); i++) {
    if (std::string(fx[i].name) == name) {
      *index = int(i);
      return fx[i];
    }
  }
  abort();
}

/* kappa(s) of a fixture in long double */
LD fixture_kappa(const CesaroFixture &f, LD s)
{
  LD acc = 0;
  for (size_t i = 0; i < f.segs.size(); i++) {
    const auto &g = f.segs[i];
    if (s <= acc + g[0] || i + 1 == f.segs.size()) {
      const LD u = s - acc;
      return g[1] + u * (g[2] + u * (g[3] + u * g[4]));
    }
    acc += g[0];
  }
  return 0;
}

/* an Fn1D's value and first derivative in long double */
void fn_ld(const Fn1D &w, LD s, LD &v, LD &d)
{
  const int i = w.piece_at(double(s));
  const LD u = s - w.breaks[i];
  v = d = 0;
  const Poly &p = w.pieces[i];
  for (int j = p.size(); j-- > 0;) {
    d = d * u + v;
    v = v * u + p.c[j];
  }
}

template <int D> double vlen(const Vec<D> &v)
{
  double s = 0;
  for (int i = 0; i < D; i++) {
    s += v[i] * v[i];
  }
  return std::sqrt(s);
}

/* deriv_bound contains sampled |r^(k)| on ranges between special
 * parameters, and is within 1000x of the sampled maximum */
template <int D> void check_deriv_bounds(const std::string &name, const CurveEval<D> &c, int kmax = 6)
{
  const Interval dom = c.domain();
  util::Vector<SpecialParam> sp;
  c.special_params(dom.lo, dom.hi, sp);
  util::Vector<double> cuts;
  cuts.append(dom.lo);
  for (const SpecialParam &p : sp) {
    if (p.continuity != kSmooth && p.t > cuts.last() && p.t < dom.hi) {
      cuts.append(p.t);
    }
  }
  cuts.append(dom.hi);
  int bad = 0, loose = 0;
  for (int ci = 0; ci + 1 < cuts.size(); ci++) {
    const double a0 = cuts[ci], b0 = cuts[ci + 1];
    for (int part = 0; part < 4; part++) {
      const double a = a0 + (b0 - a0) * part / 4, b = part == 3 ? b0 : a0 + (b0 - a0) * (part + 1) / 4;
      for (int k = 1; k <= kmax; k++) {
        const double bound = c.deriv_bound(a, b, k);
        double mx = 0;
        for (int s = 0; s <= 40; s++) {
          Vec<D> d[7];
          c.eval(s == 40 ? b : a + (b - a) * s / 40, 6, d, s == 40 ? Side::Below : Side::Above);
          mx = std::max(mx, vlen(d[k]));
        }
        if (!(mx <= bound * (1 + 1e-12) + 1e-300)) {
          if (bad++ < 3) {
            fprintf(stderr, "%s k=%d [%g, %g]: sampled %.17g > bound %.17g\n", name.c_str(), k, a, b, mx, bound);
          }
        }
        if (mx > 1e-200 && !(bound <= 1000 * mx)) {
          if (loose++ < 3) {
            fprintf(stderr, "%s k=%d [%g, %g]: bound %g vs sampled %g\n", name.c_str(), k, a, b, bound, mx);
          }
        }
      }
    }
  }
  UT_CHECK(bad == 0);
  UT_CHECK(loose == 0);
}

/* ---- offset2 ---- */

void test_offset()
{
  attr::PoolStore s;
  register_pools(s);
  const auto fx = ubertopo::test::cesaro_fixtures();
  for (const char *name : {"clothoid", "s_curve", "arc", "chain_10km", "cubic"}) {
    int idx;
    const CesaroFixture &f = fixture(fx, name, &idx);
    const attr::PoolRef base = add_cesaro(s, f);
    const double L = ubertopo::test::fixture_length(f);
    for (int wi = 0; wi < 2; wi++) {
      const Fn1D w = wi == 0 ? Fn1D::linear(2.0, 3.5, 0, L) : Fn1D::constant(-3.6, 0, L);
      const char *err = nullptr;
      const attr::PoolRef o = intern_offset(s, base, w, &err);
      UT_CHECK(!o.none() && curve_dim(s, o) == 2);
      const auto ev = curve2(s, o);
      UT_CHECK(ev->param_kind() == ParamKind::Station);
      const auto &set = kCesaroOracle[idx];
      double worst = 0, worst_t = 0;
      for (int k = 0; k < set.n; k++) {
        const auto &q = set.samples[k];
        LD wv, wd;
        fn_ld(w, q.s, wv, wd);
        const LD kap = fixture_kappa(f, q.s);
        const LD ex = LD(q.x) - wv * q.ty, ey = LD(q.y) + wv * q.tx;
        /* r_w' = (1 - w kappa) T + w' N */
        const LD tx = (1 - wv * kap) * q.tx - wd * q.ty, ty = (1 - wv * kap) * q.ty + wd * q.tx;
        Vec<2> d[2];
        ev->eval(q.s, 1, d);
        const double ep = double(std::hypot(LD(d[0][0]) - ex, LD(d[0][1]) - ey));
        const double et = double(std::hypot(LD(d[1][0]) - tx, LD(d[1][1]) - ty));
        /* the oracle doubles carry half an ulp each, scaled by |w| for N */
        const double slack = 0.6 * (std::fabs(q.x) + std::fabs(q.y)) * 0x1p-52 + 4 * double(std::fabs(wv)) * 0x1p-53;
        worst = std::max(worst, ep / (ev->eval_error() + slack));
        worst_t = std::max(worst_t, et / (ev->deriv_eval_error(1) + 4 * 0x1p-53 * (1 + double(std::fabs(wv)))));
      }
      if (!(worst <= 1 && worst_t <= 1)) {
        fprintf(stderr, "offset %s w%d: position err/bound %g, tangent %g\n", name, wi, worst, worst_t);
      }
      UT_CHECK(worst <= 1 && worst_t <= 1);
      if (std::string(name) != "chain_10km") {
        check_deriv_bounds(std::string("offset_") + name, *ev);
      }
    }
    /* widths compose by adding: r_(w1+w2) - r_w1 = w2 N */
    {
      const Fn1D w1 = Fn1D::linear(1.0, 2.0, 0, L), w2 = Fn1D::constant(1.5, 0, L);
      Fn1D w12;
      UT_CHECK(Fn1D::add(w1, w2, w12) == nullptr);
      const auto e1 = curve2(s, intern_offset(s, base, w1)), e12 = curve2(s, intern_offset(s, base, w12));
      const auto eb = curve2(s, base);
      for (int i = 0; i <= 20; i++) {
        const double t = L * i / 20;
        Vec<2> a, b, bt[2];
        e1->eval(t, 0, &a);
        e12->eval(t, 0, &b);
        eb->eval(t, 1, bt);
        const double dx = b[0] - a[0] + 1.5 * bt[1][1], dy = b[1] - a[1] - 1.5 * bt[1][0];
        UT_CHECK(std::hypot(dx, dy) <= e1->eval_error() + e12->eval_error() + 1.5 * eb->deriv_eval_error(1));
      }
    }
    /* a zero w collapses to the base */
    UT_CHECK(intern_offset(s, base, Fn1D::constant(0.0, 0, L)) == base);
    UT_CHECK(intern_offset(s, base, Fn1D::constant(0.0, 0, L / 2)) != base);
    /* refusals */
    const char *err = nullptr;
    UT_CHECK(intern_offset(s, base, Fn1D::constant(1.0, 0, 2 * L), &err).none() && err);
  }
  /* a fold where w kappa = 1: kappa = s / 90000, w = 1000 -> s = 90 */
  {
    int idx;
    const attr::PoolRef base = add_cesaro(s, fixture(fx, "clothoid", &idx)); /* the same entry as above */
    const attr::PoolRef o = intern_offset(s, base, Fn1D::constant(1000.0, 0, 150));
    const auto ev = curve2(s, o);
    const auto *view = static_cast<const Offset2View *>(ev.get());
    util::Vector<SpecialParam> sp;
    ev->special_params(0, 150, sp);
    bool found = false;
    for (const SpecialParam &p : sp) {
      found |= std::fabs(p.t - 90) < 1e-6;
    }
    UT_CHECK(found);
    UT_CHECK(view->offset_valid(0, 80));
    UT_CHECK(!view->offset_valid(0, 100));
    UT_CHECK(!view->offset_valid(95, 150));
    UT_CHECK(view->offset_valid(0, 89.99));
  }
  /* the base must be a cesaro2 */
  {
    util::Vector<double, 12> pts;
    for (double x : {0.0, 0.0, 0.0, 1.0, 0.0, 0.0}) {
      pts.append(x);
    }
    Polyline3 p;
    UT_CHECK(Polyline3::make(pts, p) == nullptr);
    const attr::PoolRef pl = s.intern<Polyline3Pool>(pool_id(polyline3_kind()), std::move(p));
    const char *err = nullptr;
    UT_CHECK(intern_offset(s, pl, Fn1D::constant(1.0, 0, 1), &err).none() && err);
  }
}

/* ---- alignment3 ---- */

void test_alignment()
{
  attr::PoolStore s;
  register_pools(s);
  const auto fx = ubertopo::test::cesaro_fixtures();
  int idx;
  const CesaroFixture &f = fixture(fx, "s_curve", &idx);
  const attr::PoolRef base = add_cesaro(s, f);
  const double L = ubertopo::test::fixture_length(f), h = L / 2;
  /* z: a parabola then its tangent line (C1 at h) */
  util::Vector<double, 2> br;
  br.append(0);
  br.append(h);
  br.append(L);
  util::Vector<Poly, 1> pcs;
  pcs.append(Poly::exact({10.0, 0.02, 1e-4}));
  pcs.append(Poly::exact({10.0 + h * (0.02 + h * 1e-4), 0.02 + 2e-4 * h}));
  Fn1D z;
  UT_CHECK(Fn1D::make(br, pcs, z) == nullptr);
  UT_CHECK(z.continuity_at(1) == 1);
  for (const bool on_offset : {false, true}) {
    const attr::PoolRef hz = on_offset ? intern_offset(s, base, Fn1D::constant(-1.75, 0, L)) : base;
    Alignment3 a;
    UT_CHECK(Alignment3::make(s, hz, z, a) == nullptr);
    const attr::PoolRef ar = s.intern<Alignment3Pool>(pool_id(alignment3_kind()), std::move(a));
    UT_CHECK(curve_dim(s, ar) == 3);
    const auto ev = curve3(s, ar);
    const auto eh = curve2(s, hz);
    const auto &set = kCesaroOracle[idx];
    int bad = 0;
    for (int k = 0; k < set.n; k++) {
      const auto &q = set.samples[k];
      Vec<3> p;
      ev->eval(q.s, 0, &p);
      Vec<2> ph;
      eh->eval(q.s, 0, &ph);
      LD zv, zd;
      fn_ld(z, q.s, zv, zd);
      const double ez = double(std::fabs(LD(p[2]) - zv));
      bad += !(p[0] == ph[0] && p[1] == ph[1]);
      bad += !(ez <= ev->eval_error());
      if (!on_offset) {
        bad += !(std::hypot(p[0] - q.x, p[1] - q.y) <= ev->eval_error() + 1e-15 * (std::fabs(q.x) + std::fabs(q.y)));
      }
    }
    UT_CHECK(bad == 0);
    util::Vector<SpecialParam> sp;
    ev->special_params(0, L, sp);
    bool at_h = false;
    for (const SpecialParam &p : sp) {
      at_h |= p.t == h && p.continuity == 1;
    }
    UT_CHECK(at_h);
    check_deriv_bounds(on_offset ? "alignment_offset" : "alignment", *ev);
  }
  /* refusals: z outside the horizontal's domain; a 3D horizontal */
  Alignment3 bad;
  UT_CHECK(Alignment3::make(s, base, Fn1D::constant(1.0, 0, 2 * L), bad) != nullptr);
  UT_CHECK(Alignment3::make(s, attr::kNoRef, z, bad) != nullptr);
}

/* ---- catenary3 ---- */

struct CatRef {
  LD A[3], ux, uy, d, a, c;
};

CatRef cat_ref(const Catenary3 &k)
{
  CatRef r;
  for (int i = 0; i < 3; i++) {
    r.A[i] = k.A[i];
  }
  const LD dx = LD(k.B[0]) - k.A[0], dy = LD(k.B[1]) - k.A[1], dz = LD(k.B[2]) - k.A[2];
  r.d = std::sqrt(dx * dx + dy * dy);
  r.ux = dx / r.d;
  r.uy = dy / r.d;
  r.a = k.a;
  r.c = r.d / 2 - r.a * std::asinh(dz / (2 * r.a * std::sinh(r.d / (2 * r.a))));
  return r;
}

void test_catenary()
{
  struct Case {
    const char *name;
    double A[3], B[3], a;
  };
  const Case cases[] = {
      {"level", {0, 0, 10}, {100, 0, 10}, 200},
      {"inclined", {5, -3, 20}, {305, 197, -40}, 150},
      {"taut", {0, 0, 0}, {100, 0, 1}, 1e6},
      {"far", {5e5, -3e5, 120}, {5e5 + 80, -3e5 + 60, 140}, 500},
      {"slack", {0, 0, 50}, {60, 0, 45}, 12},
  };
  attr::PoolStore s;
  register_pools(s);
  for (const Case &cs : cases) {
    Catenary3 k;
    UT_CHECK(Catenary3::make(cs.A, cs.B, cs.a, k) == nullptr);
    const CatRef R = cat_ref(k);
    const attr::PoolRef r = s.intern<Catenary3Pool>(pool_id(catenary3_kind()), Catenary3(k));
    const auto ev = curve3(s, r);
    UT_CHECK(ev && ev->domain().hi == k.d);
    double worst = 0, worst1 = 0, worst2 = 0;
    for (int i = 0; i <= 400; i++) {
      const LD x = LD(k.d) * i / 400;
      const double xd = double(x);
      Vec<3> d[3];
      ev->eval(xd, 2, d);
      const LD X = xd;
      const LD g = 2 * R.a * std::sinh((X - 2 * R.c) / (2 * R.a)) * std::sinh(X / (2 * R.a));
      const LD ex[3] = {R.A[0] + X * R.ux, R.A[1] + X * R.uy, R.A[2] + g};
      const LD sh = std::sinh((X - R.c) / R.a), ch = std::cosh((X - R.c) / R.a);
      LD e0 = 0, e1 = 0;
      for (int c = 0; c < 3; c++) {
        e0 += (d[0][c] - ex[c]) * (d[0][c] - ex[c]);
      }
      const LD t1[3] = {R.ux, R.uy, sh};
      for (int c = 0; c < 3; c++) {
        e1 += (d[1][c] - t1[c]) * (d[1][c] - t1[c]);
      }
      const double e2 = double(std::fabs(d[2][2] - ch / R.a));
      worst = std::max(worst, double(std::sqrt(e0)) / ev->eval_error());
      worst1 = std::max(worst1, double(std::sqrt(e1)) / ev->deriv_eval_error(1));
      worst2 = std::max(worst2, e2 / ev->deriv_eval_error(2));
    }
    printf("catenary %-9s eval_error %.3g  err/bound %.3g  r' %.3g  r'' %.3g\n", cs.name, ev->eval_error(), worst,
           worst1, worst2);
    UT_CHECK(worst <= 1 && worst1 <= 1 && worst2 <= 1);
    check_deriv_bounds(std::string("catenary_") + cs.name, *ev);
    /* the curve meets both anchors */
    Vec<3> pa, pb;
    ev->eval(0, 0, &pa);
    ev->eval(k.d, 0, &pb);
    for (int c = 0; c < 3; c++) {
      UT_CHECK(std::fabs(pa[c] - cs.A[c]) <= ev->eval_error() && std::fabs(pb[c] - cs.B[c]) <= ev->eval_error());
    }
    if (std::string(cs.name) == "taut") {
      UT_CHECK(ev->eval_error() <= 1e-10); /* absolute, not a * ulp of the cosh form */
    }
  }
  /* from_sag */
  for (const double sag : {0.05, 2.0, 15.0}) {
    const double A[3] = {0, 0, 10}, B[3] = {120, 30, 4};
    Catenary3 k;
    UT_CHECK(Catenary3::from_sag(A, B, sag, k) == nullptr);
    UT_CHECK(std::fabs(k.sag() - sag) <= 1e-9 * (1 + sag));
    const auto ev = k.evaluator();
    Vec<3> m;
    ev->eval(0.5 * k.d, 0, &m);
    UT_CHECK(std::fabs(0.5 * (A[2] + B[2]) - m[2] - sag) <= 1e-9 * (1 + sag) + ev->eval_error());
  }
  /* the lowest point of a level span is mid-span */
  {
    const double A[3] = {0, 0, 10}, B[3] = {100, 0, 10};
    Catenary3 k;
    UT_CHECK(Catenary3::make(A, B, 80, k) == nullptr);
    util::Vector<SpecialParam> sp;
    k.evaluator()->special_params(0, 100, sp);
    UT_CHECK(sp.size() == 1 && std::fabs(sp[0].t - 50) < 1e-9 && sp[0].continuity == kSmooth);
  }
  /* refusals */
  {
    const double A[3] = {0, 0, 0}, B[3] = {0, 0, 5}, C[3] = {100, 0, 0};
    Catenary3 k;
    UT_CHECK(Catenary3::make(A, B, 10, k) != nullptr);  /* vertical */
    UT_CHECK(Catenary3::make(A, C, 0, k) != nullptr);   /* a <= 0 */
    UT_CHECK(Catenary3::make(A, C, 0.01, k) != nullptr); /* overflow */
    UT_CHECK(Catenary3::from_sag(A, C, -1, k) != nullptr);
  }
}

/* ---- polyline3 ---- */

void test_polyline()
{
  util::Vector<double, 12> pts;
  for (double x : {0.0, 0.0, 0.0, 10.0, 0.0, 1.0, 10.0, 10.0, 2.0, -0.0, 10.0, 3.0}) {
    pts.append(x);
  }
  Polyline3 p;
  UT_CHECK(Polyline3::make(pts, p) == nullptr);
  UT_CHECK(std::signbit(p.pts[9]) == false);
  const auto ev = p.evaluator();
  UT_CHECK(ev->domain().lo == 0 && ev->domain().hi == 3);
  util::Vector<SpecialParam> sp;
  ev->special_params(0, 3, sp);
  UT_CHECK(sp.size() == 2 && sp[0].t == 1 && sp[1].t == 2 && sp[0].continuity == 0);
  Vec<3> d[3];
  ev->eval(1.0, 1, d, Side::Below);
  UT_CHECK(d[1][0] == 10 && d[1][1] == 0);
  ev->eval(1.0, 1, d, Side::Above);
  UT_CHECK(d[1][0] == 0 && d[1][1] == 10);
  ev->eval(2.5, 2, d);
  UT_CHECK(std::fabs(d[0][0] - 5) < 1e-12 && d[0][2] == 2.5 && d[2][0] == 0);
  UT_CHECK(ev->deriv_bound(1.2, 1.8, 2) == 0 && std::isinf(ev->deriv_bound(0.5, 1.5, 1)));
  check_deriv_bounds("polyline", *ev);
  /* refusals */
  util::Vector<double, 12> one, dup;
  for (double x : {1.0, 2.0, 3.0}) {
    one.append(x);
  }
  for (double x : {1.0, 2.0, 3.0, 1.0, 2.0, 3.0}) {
    dup.append(x);
  }
  UT_CHECK(Polyline3::make(one, p) != nullptr && Polyline3::make(dup, p) != nullptr);
}

/* ---- bspline ---- */

LD basis_ld(const util::Vector<double, 16> &U, int i, int p, LD t, bool last)
{
  if (p == 0) {
    if (last) {
      return (U[i] < t && t <= U[i + 1]) ? 1 : 0;
    }
    return (U[i] <= t && t < U[i + 1]) ? 1 : 0;
  }
  LD v = 0;
  if (U[i + p] > U[i]) {
    v += (t - U[i]) / (LD(U[i + p]) - U[i]) * basis_ld(U, i, p - 1, t, last);
  }
  if (U[i + p + 1] > U[i + 1]) {
    v += (LD(U[i + p + 1]) - t) / (LD(U[i + p + 1]) - U[i + 1]) * basis_ld(U, i + 1, p - 1, t, last);
  }
  return v;
}

void test_bspline()
{
  litestl::test::SplitMix64 r(7);
  for (const int p : {1, 2, 3, 5, 7}) {
    const int n = p + 1 + 5;
    util::Vector<double, 16> U;
    for (int i = 0; i <= p; i++) {
      U.append(0);
    }
    double u = 0;
    for (int i = 0; i < n - p - 1; i++) {
      u += 0.25 + r.unit();
      U.append(u);
    }
    u += 0.5;
    for (int i = 0; i <= p; i++) {
      U.append(u);
    }
    util::Vector<double, 48> P;
    for (int i = 0; i < n * 3; i++) {
      P.append(200 * r.unit() - 100);
    }
    BSpline b;
    UT_CHECK(BSpline::make(3, p, U, P, attr::kNoRef, 0, 0, 0, b) == nullptr);
    const auto ev = b.evaluator3();
    double worst = 0;
    for (int k = 0; k <= 500; k++) {
      const double t = b.hi() * k / 500;
      Vec<3> x;
      ev->eval(t, 0, &x);
      LD e = 0;
      for (int c = 0; c < 3; c++) {
        LD v = 0;
        for (int i = 0; i < n; i++) {
          v += basis_ld(U, i, p, t, k == 500) * P[i * 3 + c];
        }
        e += (x[c] - v) * (x[c] - v);
      }
      worst = std::max(worst, double(std::sqrt(e)) / ev->eval_error());
    }
    UT_CHECK(worst <= 1);
    check_deriv_bounds("bspline_p" + std::to_string(p), *ev);
    /* knot insertion keeps the shape */
    BSpline bi;
    const double ins = 0.37 * b.hi();
    UT_CHECK(BSpline::insert_knot(b, ins, bi) == nullptr && bi.count() == b.count() + 1);
    const auto ei = bi.evaluator3();
    for (int k = 0; k <= 200; k++) {
      const double t = b.hi() * k / 200;
      Vec<3> x, y;
      ev->eval(t, 0, &x);
      ei->eval(t, 0, &y);
      UT_CHECK(vlen(Vec<3>(x[0] - y[0], x[1] - y[1], x[2] - y[2])) <= ev->eval_error() + ei->eval_error() + 1e-12);
    }
  }
}

} // namespace

int main()
{
  test_offset();
  test_alignment();
  test_catenary();
  test_polyline();
  test_bspline();
  return ut_test_end();
}
