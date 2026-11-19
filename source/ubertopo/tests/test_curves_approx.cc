/* Generic curve algorithms (plan P4a W3, §6.1 test_curves_approx): bounds
 * contain samples; flattening stays within tol and nests; approximations
 * stay within max_error, which is within 1000x of the measured error; knot
 * multiplicities follow the source's continuity; the approximation cache
 * lives exactly as long as its source. */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "support/cesaro_fixtures.h"
#include "support/splitmix64.h"
#include "ubertopo/curves/approx.h"
#include "ubertopo/curves/cesaro.h"
#include "ut_test.h"

using namespace ubertopo::curves;
using ubertopo::test::CesaroFixture;

namespace {

struct Named {
  std::string name;
  attr::PoolRef ref;
};

attr::PoolRef add_cesaro(attr::PoolStore &s, double x0, double y0, double th0,
                         const std::vector<std::array<double, 5>> &segs)
{
  util::Vector<CesaroSegment, 4> sg;
  for (auto &x : segs) {
    sg.append({x[0], Poly::exact({x[1], x[2], x[3], x[4]})});
  }
  Cesaro2 c;
  UT_CHECK(Cesaro2::make(x0, y0, th0, sg, c) == nullptr);
  return s.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(c));
}

attr::PoolRef add_bspline(attr::PoolStore &s, int dim, int degree, const std::vector<double> &knots,
                          const std::vector<double> &ctrl)
{
  util::Vector<double, 16> k;
  util::Vector<double, 48> c;
  for (double x : knots) {
    k.append(x);
  }
  for (double x : ctrl) {
    c.append(x);
  }
  BSpline b;
  const char *err = BSpline::make(dim, degree, k, c, attr::kNoRef, 0, 0, 0, b);
  if (err) {
    fprintf(stderr, "bspline: %s\n", err);
  }
  UT_CHECK(err == nullptr);
  return s.intern<BSplinePool>(pool_id(bspline_kind()), std::move(b));
}

util::Vector<Named> make_curves(attr::PoolStore &s)
{
  util::Vector<Named> v;
  for (const CesaroFixture &f : ubertopo::test::cesaro_fixtures()) {
    v.append({f.name, add_cesaro(s, f.x0, f.y0, f.th0, f.segs)});
  }
  /* a curvature jump (C1): line then arc */
  v.append({"line_arc", add_cesaro(s, 0, 0, 0.4, {{30.0, 0, 0, 0, 0}, {40.0, 1.0 / 25, 0, 0, 0}})});
  /* 3D cubic with a double interior knot (C1 there) */
  {
    litestl::test::SplitMix64 r(11);
    std::vector<double> ctrl;
    for (int i = 0; i < 8 * 3; i++) {
      ctrl.push_back(100 * r.unit() - 50);
    }
    v.append({"bspline3_c1", add_bspline(s, 3, 3, {0, 0, 0, 0, 1, 2, 2, 3.5, 5, 5, 5, 5}, ctrl)});
  }
  /* 3D polyline as a degree-1 spline: corners (C0) */
  v.append({"bspline3_poly", add_bspline(s, 3, 1, {0, 0, 1, 2.5, 3, 4, 4},
                                         {0, 0, 0, 10, 0, 1, 10, 10, 2, 0, 10, 3, 0, 0, 4})});
  /* 2D quintic, uniform knots */
  v.append({"bspline2_quintic", add_bspline(s, 2, 5, {0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3},
                                            {0, 0, 1, 2, 3, 1, 4, 4, 5, 2, 7, 1, 8, 3, 9, 0})});
  return v;
}

template <int D> double dist(const Vec<D> &a, const Vec<D> &b)
{
  double s = 0;
  for (int i = 0; i < D; i++) {
    s += (a[i] - b[i]) * (a[i] - b[i]);
  }
  return std::sqrt(s);
}

/* distance from p to the segment [a, b] */
template <int D> double seg_dist(const Vec<D> &p, const Vec<D> &a, const Vec<D> &b)
{
  double ab2 = 0, t = 0;
  for (int i = 0; i < D; i++) {
    ab2 += (b[i] - a[i]) * (b[i] - a[i]);
    t += (p[i] - a[i]) * (b[i] - a[i]);
  }
  t = ab2 > 0 ? std::clamp(t / ab2, 0.0, 1.0) : 0.0;
  Vec<D> q;
  for (int i = 0; i < D; i++) {
    q[i] = a[i] + t * (b[i] - a[i]);
  }
  return dist(p, q);
}

template <int D> void check_bounds(const std::string &name, const CurveEval<D> &c)
{
  const Interval dom = c.domain();
  litestl::test::SplitMix64 r(5);
  const double e = c.eval_error();
  for (int range = 0; range < 4; range++) {
    double a = dom.lo, b = dom.hi;
    if (range > 0) {
      a = dom.lo + (dom.hi - dom.lo) * r.unit();
      b = dom.lo + (dom.hi - dom.lo) * r.unit();
      if (a > b) {
        std::swap(a, b);
      }
    }
    const Box<D> box = bounds(c, a, b);
    int bad = 0;
    for (int i = 0; i <= 2500; i++) {
      const double t = i == 2500 ? b : a + (b - a) * r.unit();
      Vec<D> p;
      c.eval(t, 0, &p);
      for (int k = 0; k < D; k++) {
        bad += p[k] < box.lo[k] - e || p[k] > box.hi[k] + e;
      }
    }
    if (bad) {
      fprintf(stderr, "%s: %d samples outside the box over [%g, %g]\n", name.c_str(), bad, a, b);
    }
    UT_CHECK(bad == 0);
  }
}

template <int D> void check_flatten(const std::string &name, const CurveEval<D> &c)
{
  const Interval dom = c.domain();
  const double e = c.eval_error();
  for (const double tol : {1e-1, 1e-3}) {
    util::Vector<double> p1, p4;
    UT_CHECK(flatten(c, dom.lo, dom.hi, tol, p1));
    UT_CHECK(flatten(c, dom.lo, dom.hi, tol / 4, p4));
    UT_CHECK(p1.size() >= 2 && p1[0] == dom.lo && p1.last() == dom.hi);
    /* nesting: every tol parameter is a tol / 4 parameter */
    int j = 0, missing = 0;
    for (double t : p1) {
      while (j < p4.size() && p4[j] < t) {
        j++;
      }
      missing += !(j < p4.size() && p4[j] == t);
    }
    UT_CHECK(missing == 0);
    double worst = 0;
    for (int i = 0; i + 1 < p1.size(); i++) {
      Vec<D> a, b;
      c.eval(p1[i], 0, &a, Side::Above);
      c.eval(p1[i + 1], 0, &b, Side::Below);
      for (int s = 1; s < 16; s++) {
        Vec<D> q;
        c.eval(p1[i] + (p1[i + 1] - p1[i]) * s / 16, 0, &q);
        worst = std::max(worst, seg_dist(q, a, b));
      }
    }
    UT_CHECK(worst <= tol + e);
    if (tol == 1e-3) {
      printf("%-16s flatten tol %.0e: %5d points, worst deviation / tol %.3f\n", name.c_str(), tol,
             int(p1.size()), worst / tol);
    }
  }
}

template <int D>
void check_approx(attr::PoolStore &s, const std::string &name, attr::PoolRef src, double tol)
{
  const char *err = nullptr;
  Interval dom;
  util::UniquePtr<CurveEval<D>> tmp;
  if constexpr (D == 2) {
    tmp = curve2(s, src);
  }
  else {
    tmp = curve3(s, src);
  }
  dom = tmp->domain();
  tmp.reset();
  const attr::PoolRef ar = approximate(s, src, dom.lo, dom.hi, tol, &err);
  if (ar.none()) {
    fprintf(stderr, "%s: approximate failed: %s\n", name.c_str(), err ? err : "?");
  }
  UT_CHECK(!ar.none());
  if (ar.none()) {
    return;
  }
  const BSpline &b = s.get<BSplinePool>(ar);
  UT_CHECK(b.source == src && b.degree == 5 && b.dim == D && b.max_error <= tol);
  UT_CHECK(b.src_t0 == dom.lo && b.src_t1 == dom.hi && b.lo() == dom.lo && b.hi() == dom.hi);
  util::UniquePtr<CurveEval<D>> ev, sp;
  if constexpr (D == 2) {
    ev = curve2(s, src);
    sp = b.evaluator2();
  }
  else {
    ev = curve3(s, src);
    sp = b.evaluator3();
  }
  const double noise = ev->eval_error() + sp->eval_error();
  /* measured error: samples on every span (between distinct knots) */
  util::Vector<double> spans;
  for (double k : b.knots) {
    if (spans.size() == 0 || k != spans.last()) {
      spans.append(k);
    }
  }
  const int per = std::max(24, 10000 / std::max(1, int(spans.size()) - 1));
  double measured = 0;
  for (int i = 0; i + 1 < spans.size(); i++) {
    for (int k = 0; k <= per; k++) {
      const double t = spans[i] + (spans[i + 1] - spans[i]) * k / per;
      const Side side = k == per ? Side::Below : Side::Above;
      Vec<D> x, y;
      ev->eval(t, 0, &x, side);
      sp->eval(t, 0, &y, side);
      measured = std::max(measured, dist(x, y));
    }
  }
  UT_CHECK(measured <= b.max_error + noise);
  /* tightness, where the measured error is above rounding noise */
  if (measured > 100 * noise) {
    if (!(b.max_error <= 1000 * measured)) {
      fprintf(stderr, "%s tol %g: max_error %g vs measured %g\n", name.c_str(), tol, b.max_error, measured);
    }
    UT_CHECK(b.max_error <= 1000 * measured);
  }
  /* knot multiplicities and continuity across them */
  util::Vector<SpecialParam> spec;
  ev->special_params(dom.lo, dom.hi, spec);
  int m3 = 0, m4 = 0, m5 = 0;
  for (int i = 6; i < b.count();) {
    const double u = b.knots[i];
    int m = 1;
    while (i + m < b.count() && b.knots[i + m] == u) {
      m++;
    }
    i += m;
    Vec<D> l[3], r[3], sl[3], sr[3];
    sp->eval(u, 2, l, Side::Below);
    sp->eval(u, 2, r, Side::Above);
    ev->eval(u, 2, sl, Side::Below);
    ev->eval(u, 2, sr, Side::Above);
    uint8_t cont = kSmooth;
    for (const SpecialParam &p : spec) {
      if (p.t == u && p.continuity < kSmooth) {
        cont = std::min(cont, p.continuity);
      }
    }
    const int want = 5 - std::min<int>(cont, 2);
    if (m != want) {
      fprintf(stderr, "%s: knot %g multiplicity %d, want %d\n", name.c_str(), u, m, want);
    }
    UT_CHECK(m == want);
    double scale[3];
    for (int k = 0; k < 3; k++) {
      scale[k] = 1 + std::max(dist(l[k], Vec<D>()), dist(r[k], Vec<D>()));
    }
    UT_CHECK(dist(l[0], r[0]) <= 1e-12 * scale[0]);
    if (m <= 4) {
      UT_CHECK(dist(l[1], r[1]) <= 1e-9 * scale[1]);
    }
    if (m == 3) {
      m3++;
      UT_CHECK(dist(l[2], r[2]) <= 1e-6 * scale[2]);
    }
    if (m == 4) {
      m4++;
      /* the one-sided r'' of the source, each side */
      const double dl = dist(l[2], sl[2]), dr = dist(r[2], sr[2]);
      if (!(dl <= 1e-3 * scale[2] && dr <= 1e-3 * scale[2])) {
        fprintf(stderr, "%s: r'' at the C1 knot %g: %g / %g (scale %g)\n", name.c_str(), u, dl, dr, scale[2]);
      }
      UT_CHECK(dl <= 1e-3 * scale[2] && dr <= 1e-3 * scale[2]);
      if (name == "line_arc") {
        UT_CHECK(dist(sl[2], sr[2]) > 1e-3 * scale[2]); /* the source visibly jumps */
      }
    }
    if (m == 5) {
      m5++;
      UT_CHECK(dist(l[0], sl[0]) <= b.max_error + noise); /* the corner is kept */
    }
  }
  if (tol == 1e-7) {
    printf("%-16s approx tol %.0e: %5d ctrl, knots m3/m4/m5 %d/%d/%d, max_error %.3g, measured %.3g\n",
           name.c_str(), tol, b.count(), m3, m4, m5, b.max_error, measured);
  }
  if (name == "line_arc") {
    UT_CHECK(m4 == 1);
  }
  if (name == "bspline3_poly") {
    UT_CHECK(m5 == 3);
  }
}

void test_algorithms()
{
  attr::PoolStore s;
  register_pools(s);
  const util::Vector<Named> curves = make_curves(s);
  for (const Named &n : curves) {
    if (curve_dim(s, n.ref) == 2) {
      auto c = curve2(s, n.ref);
      check_bounds(n.name, *c);
      check_flatten(n.name, *c);
    }
    else {
      auto c = curve3(s, n.ref);
      check_bounds(n.name, *c);
      check_flatten(n.name, *c);
    }
  }
  for (const Named &n : curves) {
    for (const double tol : {1e-3, 1e-7}) {
      if (curve_dim(s, n.ref) == 2) {
        check_approx<2>(s, n.name, n.ref, tol);
      }
      else {
        check_approx<3>(s, n.name, n.ref, tol);
      }
    }
  }
  /* refusals */
  const char *err = nullptr;
  UT_CHECK(approximate(s, curves[0].ref, 0, 10, 1e-20, &err).none() && err);
  UT_CHECK(approximate(s, curves[0].ref, 5, 5, 1e-3, &err).none());
  UT_CHECK(approximate(s, attr::kNoRef, 0, 1, 1e-3, &err).none());
}

/* Knot removal on pieces that are only C0 at their join: the averaged
 * control points move the pieces, and `moved` must bound that. */
void test_assemble()
{
  litestl::test::SplitMix64 r(21);
  for (const int m : {3, 4, 5}) {
    for (int trial = 0; trial < 20; trial++) {
      util::Vector<detail::BezierPiece<3>> pcs;
      detail::BezierPiece<3> a, b;
      a.a = 0;
      a.b = 1 + r.unit();
      b.a = a.b;
      b.b = b.a + 0.5 + 2 * r.unit();
      a.err = b.err = 0;
      for (int j = 0; j < 6; j++) {
        for (int c = 0; c < 3; c++) {
          a.B[j][c] = 20 * r.unit() - 10;
          b.B[j][c] = 20 * r.unit() - 10;
        }
      }
      b.B[0] = a.B[5];
      pcs.append(a);
      pcs.append(b);
      util::Vector<int> mult;
      mult.append(m);
      util::Vector<double, 16> knots;
      util::Vector<double, 48> ctrl;
      util::Vector<double> moved;
      detail::assemble<3>(pcs, mult, knots, ctrl, moved);
      BSpline sp;
      UT_CHECK(BSpline::make(3, 5, knots, ctrl, attr::kNoRef, 0, 0, 0, sp) == nullptr);
      const auto ev = sp.evaluator3();
      double worst[2] = {0, 0};
      for (int q = 0; q < 2; q++) {
        const auto &p = pcs[q];
        for (int k = 0; k <= 200; k++) {
          const double s = double(k) / 200, t = p.a + (p.b - p.a) * s;
          Vec<3> w[6];
          for (int j = 0; j < 6; j++) {
            w[j] = p.B[j];
          }
          for (int l = 1; l < 6; l++) {
            for (int j = 0; j < 6 - l; j++) {
              for (int c = 0; c < 3; c++) {
                w[j][c] = (1 - s) * w[j][c] + s * w[j + 1][c];
              }
            }
          }
          Vec<3> x;
          ev->eval(t, 0, &x, k == 200 ? Side::Below : Side::Above);
          worst[q] = std::max(worst[q], dist(x, w[0]));
        }
        UT_CHECK(worst[q] <= moved[q] + 1e-12);
      }
      if (m == 5) {
        UT_CHECK(moved[0] < 1e-12 && moved[1] < 1e-12); /* the Bezier points as they are */
      }
      else {
        UT_CHECK(std::max(worst[0], worst[1]) > 1e-3); /* removal really moves them */
      }
    }
  }
}

/* A strong holder of refs (stands in for the owner's columns). */
class Holder final : public attr::RefRoot {
public:
  util::Vector<attr::PoolRef> refs;
  void visit_refs(attr::RefVisitor &v) const override
  {
    for (attr::PoolRef r : refs) {
      v.visit(r, 1);
    }
  }
  void remap_refs(const attr::RefRemap &m) override
  {
    for (attr::PoolRef &r : refs) {
      r = m.map(r);
    }
  }
};

void test_cache()
{
  for (const bool canon : {false, true}) {
    attr::PoolStore s;
    register_pools(s);
    Holder h;
    ApproxCache cache;
    h.refs.append(add_cesaro(s, 0, 0, 0, {{100.0, 0, 1e-4, 0, 0}}));
    const attr::PoolRef other = add_cesaro(s, 0, 0, 0, {{50.0, 0.01, 0, 0, 0}});
    const attr::PoolRef a1 = cache.get(s, h.refs[0], 0, 100, 1e-6);
    const attr::PoolRef a2 = cache.get(s, other, 0, 50, 1e-6);
    UT_CHECK(!a1.none() && !a2.none() && cache.size() == 2);
    UT_CHECK(cache.get(s, h.refs[0], 0, 100, 1e-6) == a1 && cache.size() == 2); /* hit */
    attr::RefRoot *ex[] = {&h, &cache};
    const attr::PoolStore::Roots roots{{}, util::span<attr::RefRoot *const>(ex, 2)};
    const attr::PoolStore::MutRoots mroots{{}, util::span<attr::RefRoot *const>(ex, 2)};
    const auto run = [&] {
      if (canon) {
        UT_CHECK(s.canonicalize(mroots));
      }
      else {
        UT_CHECK(s.collect(roots));
      }
    };
    run();
    /* the held source keeps its approximation; the other pair is gone */
    UT_CHECK(cache.size() == 1 && s.live_count() == 2);
    const attr::PoolRef a1n = cache.find(s, h.refs[0], 0, 100, 1e-6);
    UT_CHECK(!a1n.none() && s.get<BSplinePool>(a1n).source == h.refs[0]);
    UT_CHECK(cache.find(s, h.refs[0], 0, 100, 1e-9).none()); /* another tol is another entry */
    UT_CHECK(s.audit(roots, true));
    /* the approximation alone (held strongly) keeps its source through the inner ref */
    Holder h2;
    h2.refs.append(a1n);
    attr::RefRoot *ex2[] = {&h2, &cache};
    h.refs.clear();
    UT_CHECK(s.collect(attr::PoolStore::Roots{{}, util::span<attr::RefRoot *const>(ex2, 2)}));
    UT_CHECK(s.live_count() == 2 && cache.size() == 1);
    /* nothing holds either: both freed, the entry pruned */
    h2.refs.clear();
    run();
    UT_CHECK(cache.size() == 0 && s.live_count() == 0);
  }
}

} // namespace

int main()
{
  test_algorithms();
  test_assemble();
  test_cache();
  return ut_test_end();
}
