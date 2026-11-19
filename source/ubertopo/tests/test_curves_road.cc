/* road3, the ruled road surface (plan P4b §3.2, §3.4, §7):
 *  - S and its partials against the quad oracle (tools/curve_oracle/road_oracle),
 *    within the certified eval_error and deriv_eval_error;
 *  - the per-term white-box check: xy against the oracle, w and z against
 *    long double from the same data, each within its own term;
 *  - borders: S(s, b_k(s)) equals alignment3(offset2(reference, b_k), Z_k)
 *    bit for bit (up to the sign of zero) at every station and break;
 *  - derivative bounds against dense samples; domain_v; special_u;
 *  - refusals: unordered or crossing borders, folds, jumps, domains;
 *  - the road builder.
 * Finiteness is checked before any comparison (a NaN passes every <=). */

#include <bit>
#include <cmath>
#include <cstdio>
#include <vector>

#include "support/road_fixtures.h"
#include "ubertopo/curves/alignment.h"
#include "ubertopo/curves/cesaro.h"
#include "ubertopo/curves/offset.h"
#include "ubertopo/curves/road.h"
#include "ubertopo/curves/road_build.h"
#include "ut_test.h"

#include "data/road_oracle.inc"

using namespace ubertopo;
using namespace ubertopo::curves;
using test::FnData;
using test::RoadFixture;
using LD = long double;

namespace {

Fn1D to_fn(const FnData &d)
{
  util::Vector<double, 2> br;
  for (double b : d.breaks) {
    br.append(b);
  }
  util::Vector<Poly, 1> ps;
  for (const auto &p : d.pieces) {
    ps.append(Poly::exact(p.data(), int(p.size())));
  }
  Fn1D f;
  UT_CHECK(Fn1D::make(br, ps, f) == nullptr);
  return f;
}

/* f's k-th derivative at s in long double, from the same data */
LD fn_ld(const FnData &f, double s, int k)
{
  size_t i = 0;
  while (i + 2 < f.breaks.size() && s >= f.breaks[i + 1]) {
    i++;
  }
  const LD sig = LD(s) - LD(f.breaks[i]);
  const auto &p = f.pieces[i];
  LD v = 0;
  for (int j = int(p.size()) - 1; j >= k; j--) {
    LD c = p[size_t(j)];
    for (int m = 0; m < k; m++) {
      c *= LD(j - m);
    }
    v = v * sig + c;
  }
  return v;
}

struct Built {
  attr::PoolStore st;
  attr::PoolRef ref, road;
  util::Vector<Fn1D, 2> b, z;
};

bool build(const RoadFixture &f, Built &out)
{
  register_pools(out.st);
  util::Vector<CesaroSegment, 4> segs;
  for (auto &s : f.ref.segs) {
    segs.append({s[0], Poly::exact({s[1], s[2], s[3], s[4]})});
  }
  Cesaro2 c;
  if (Cesaro2::make(f.ref.x0, f.ref.y0, f.ref.th0, segs, c)) {
    return false;
  }
  out.ref = out.st.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(c));
  for (size_t i = 0; i < f.borders.size(); i++) {
    out.b.append(to_fn(f.borders[i]));
    out.z.append(to_fn(f.heights[i]));
  }
  Road3 d;
  const char *e = Road3::make(out.st, out.ref, out.b, out.z, d);
  if (e) {
    fprintf(stderr, "%s: %s\n", f.name, e);
    return false;
  }
  out.road = out.st.intern<Road3Pool>(pool_id(road3_kind()), std::move(d));
  return true;
}

bool finite3(const Vec<3> &v)
{
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

double dist3(const Vec<3> &a, const double *b)
{
  const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/* bitwise equal after +0.0 (which turns -0.0 into +0.0 and nothing else) */
bool same_bits(double a, double b)
{
  return std::bit_cast<uint64_t>(a + 0.0) == std::bit_cast<uint64_t>(b + 0.0);
}

/* the oracle's own error, relative to the fixture's scale */
constexpr double kOracleTol = 1e-24;

void test_oracle()
{
  const auto fx = test::road_fixtures();
  UT_CHECK(fx.size() == sizeof(kRoadOracle) / sizeof(kRoadOracle[0]));
  for (size_t fi = 0; fi < fx.size(); fi++) {
    Built b;
    UT_CHECK(build(fx[fi], b));
    const auto ev = surface(b.st, b.road);
    UT_CHECK(bool(ev));
    if (!ev) {
      continue;
    }
    const double e0 = ev->eval_error(), e1 = ev->deriv_eval_error(1), e2 = ev->deriv_eval_error(2);
    UT_CHECK(std::isfinite(e0) && std::isfinite(e1) && std::isfinite(e2) && e0 > 0 && e1 > 0 && e2 > 0);
    /* useful bounds: well below a micrometre at these scales */
    UT_CHECK(e0 < 1e-9 && e1 < 1e-9 && e2 < 1e-9);
    const auto &set = kRoadOracle[fi];
    double worst = 0;
    for (int i = 0; i < set.n; i++) {
      const auto &o = set.samples[i];
      Vec<3> out[6];
      ev->eval(o.s, o.t, 2, out);
      bool fin = true;
      for (const Vec<3> &v : out) {
        fin = fin && finite3(v);
      }
      UT_CHECK(fin);
      if (!fin) {
        continue;
      }
      const double tol = kOracleTol * 1e4;
      const double d0 = dist3(out[0], o.v);
      UT_CHECK(d0 <= e0 + tol);
      worst = std::max(worst, d0 / e0);
      if (o.link < 0) {
        continue; /* on a border: the partials jump */
      }
      UT_CHECK(dist3(out[1], o.v + 3) <= e1 + tol);
      UT_CHECK(dist3(out[2], o.v + 6) <= e1 + tol);
      UT_CHECK(dist3(out[3], o.v + 9) <= e2 + tol);
      UT_CHECK(dist3(out[4], o.v + 12) <= e2 + tol);
      UT_CHECK(out[5][0] == 0 && out[5][1] == 0 && out[5][2] == 0);
    }
    printf("road oracle %s: %d samples, eval_error %.3g, worst error / bound %.3g\n", set.name, set.n, e0, worst);
  }
}

/* each error term against its own source */
void test_terms()
{
  if (sizeof(LD) == sizeof(double)) {
    printf("road terms: skipped (long double is double here)\n");
    return;
  }
  const auto fx = test::road_fixtures();
  int checked = 0;
  for (size_t fi = 0; fi < fx.size(); fi++) {
    const RoadFixture &f = fx[fi];
    Built b;
    UT_CHECK(build(f, b));
    const auto ev = surface(b.st, b.road);
    const auto &set = kRoadOracle[fi];
    for (int i = 0; i < set.n && ev; i++) {
      const auto &o = set.samples[i];
      detail::Road3Probe pr;
      UT_CHECK(detail::road3_probe(b.st, b.road, o.s, o.t, Side::Above, Side::Above, pr));
      const detail::Road3Terms &t = pr.terms;
      UT_CHECK(t.xy > 0 && t.w > 0 && t.z > 0 && std::isfinite(t.e0));
      UT_CHECK(o.link < 0 || pr.link == o.link);
      /* xy: the horizontal part against the oracle */
      Vec<3> out[1];
      ev->eval(o.s, o.t, 0, out);
      const double dxy = std::hypot(out[0][0] - o.v[0], out[0][1] - o.v[1]);
      UT_CHECK(dxy <= t.xy + kOracleTol * 1e4);
      /* w and z: long double from the same borders and heights */
      const int k = pr.link;
      const LD bk = fn_ld(f.borders[size_t(k)], o.s, 0), bk1 = fn_ld(f.borders[size_t(k) + 1], o.s, 0);
      const LD zk = fn_ld(f.heights[size_t(k)], o.s, 0), zk1 = fn_ld(f.heights[size_t(k) + 1], o.s, 0);
      const LD w = (LD(o.t) - bk) / (bk1 - bk);
      const LD z = (1 - w) * zk + w * zk1;
      UT_CHECK(std::isfinite(pr.w) && std::isfinite(pr.z));
      UT_CHECK(std::fabs(LD(pr.w) - w) <= LD(t.w));
      UT_CHECK(std::fabs(LD(pr.z) - z) <= LD(t.z));
      UT_CHECK(pr.z == out[0][2]);
      checked++;
    }
  }
  printf("road terms: %d samples\n", checked);
}

/* S(s, b_k(s)) and the border carrier, bit for bit */
int border_bits(Built &b, const char *name)
{
  const auto ev = surface(b.st, b.road);
  UT_CHECK(bool(ev));
  if (!ev) {
    return 0;
  }
  const Interval dom = ev->domain_u();
  util::Vector<SpecialParam> sp;
  ev->special_u(dom.lo, dom.hi, sp);
  std::vector<double> stations;
  for (int i = 0; i <= 64; i++) {
    stations.push_back(dom.lo + (dom.hi - dom.lo) * i / 64);
  }
  for (const SpecialParam &p : sp) {
    stations.push_back(p.t);
  }
  int n = 0, bad = 0;
  for (int k = 0; k < b.b.size(); k++) {
    const attr::PoolRef horiz = intern_offset(b.st, b.ref, b.b[k]);
    UT_CHECK(!horiz.none());
    Alignment3 al;
    UT_CHECK(Alignment3::make(b.st, horiz, b.z[k], al) == nullptr);
    const attr::PoolRef car = b.st.intern<Alignment3Pool>(pool_id(alignment3_kind()), std::move(al));
    const auto c3 = curve3(b.st, car);
    const auto ev2 = surface(b.st, b.road); /* interning may move entries */
    UT_CHECK(bool(c3) && bool(ev2));
    if (!c3 || !ev2) {
      continue;
    }
    for (double s : stations) {
      for (const Side su : {Side::Below, Side::Above}) {
        const double t = b.b[k].eval(s, 0, su);
        Vec<3> cv[1];
        c3->eval(s, 0, cv, su);
        for (const Side sv : {Side::Below, Side::Above}) {
          Vec<3> sv3[1];
          ev2->eval(s, t, 0, sv3, su, sv);
          const bool ok = same_bits(sv3[0][0], cv[0][0]) && same_bits(sv3[0][1], cv[0][1]) &&
                          same_bits(sv3[0][2], cv[0][2]);
          if (!ok && bad++ < 4) {
            fprintf(stderr, "%s border %d at s = %a: (%a %a %a) vs (%a %a %a)\n", name, k, s, sv3[0][0], sv3[0][1],
                    sv3[0][2], cv[0][0], cv[0][1], cv[0][2]);
          }
          UT_CHECK(ok);
          n++;
        }
      }
    }
  }
  return n;
}

void test_border_bits()
{
  int n = 0;
  for (const RoadFixture &f : test::road_fixtures()) {
    Built b;
    UT_CHECK(build(f, b));
    n += border_bits(b, f.name);
  }
  printf("road borders: %d bitwise comparisons\n", n);
}

/* derivative bounds hold on dense samples of each link between special
 * stations; S_tt is 0; domain_v is (b_0, b_m) */
void test_deriv_bounds()
{
  for (const RoadFixture &f : test::road_fixtures()) {
    Built b;
    UT_CHECK(build(f, b));
    const auto ev = surface(b.st, b.road);
    if (!ev) {
      UT_CHECK(false);
      continue;
    }
    const Interval dom = ev->domain_u();
    util::Vector<SpecialParam> sp;
    ev->special_u(dom.lo, dom.hi, sp);
    std::vector<double> cuts = {dom.lo};
    for (const SpecialParam &p : sp) {
      if (p.t > cuts.back() && p.t < dom.hi) {
        cuts.push_back(p.t);
      }
    }
    cuts.push_back(dom.hi);
    for (size_t c = 0; c + 1 < cuts.size(); c++) {
      for (int k = 0; k + 1 < b.b.size(); k++) {
        const double u0 = cuts[c], u1 = cuts[c + 1];
        /* the link's t range over the box */
        double v0 = INFINITY, v1 = -INFINITY;
        for (int i = 0; i <= 16; i++) {
          const double s = u0 + (u1 - u0) * i / 16;
          v0 = std::min(v0, b.b[k].eval(s));
          v1 = std::max(v1, b.b[k + 1].eval(s));
        }
        const ParamBox box{{u0, u1}, {v0, v1}};
        const int ij[6][2] = {{0, 0}, {1, 0}, {0, 1}, {2, 0}, {1, 1}, {0, 2}};
        double bound[6];
        for (int q = 0; q < 6; q++) {
          bound[q] = ev->deriv_bound(box, ij[q][0], ij[q][1]);
          UT_CHECK(std::isfinite(bound[q]));
        }
        UT_CHECK(bound[5] == 0);
        for (int i = 1; i < 32; i++) {
          const double s = u0 + (u1 - u0) * i / 32;
          const double bk = b.b[k].eval(s), bk1 = b.b[k + 1].eval(s);
          for (int j = 1; j < 8; j++) {
            const double t = bk + (bk1 - bk) * j / 8;
            Vec<3> out[6];
            ev->eval(s, t, 2, out);
            for (int q = 0; q < 6; q++) {
              const double m = std::sqrt(out[q][0] * out[q][0] + out[q][1] * out[q][1] + out[q][2] * out[q][2]);
              UT_CHECK(std::isfinite(m) && m <= bound[q]);
            }
          }
          const Interval dv = ev->domain_v(s);
          UT_CHECK(dv.lo == b.b[0].eval(s) && dv.hi == b.b[b.b.size() - 1].eval(s));
        }
      }
    }
  }
}

/* make refuses unordered or crossing borders, folds, jumps and domains */
void test_refusals()
{
  attr::PoolStore st;
  register_pools(st);
  Cesaro2 h;
  UT_CHECK(Cesaro2::arc(0, 0, 0.0, -1.0 / 128, 160, h) == nullptr); /* a right turn of radius 128 */
  const attr::PoolRef hr = st.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(h));
  auto make = [&](std::initializer_list<Fn1D> bs, std::initializer_list<Fn1D> zs) {
    util::Vector<Fn1D, 2> b, z;
    for (const Fn1D &f : bs) {
      b.append(f);
    }
    for (const Fn1D &f : zs) {
      z.append(f);
    }
    Road3 d;
    return Road3::make(st, hr, b, z, d);
  };
  const Fn1D z0 = Fn1D::constant(5, 0, 160);
  UT_CHECK(make({Fn1D::constant(-3, 0, 160), Fn1D::constant(3, 0, 160)}, {z0, z0}) == nullptr);
  /* unordered, equal, crossing */
  UT_CHECK(make({Fn1D::constant(3, 0, 160), Fn1D::constant(-3, 0, 160)}, {z0, z0}) != nullptr);
  UT_CHECK(make({Fn1D::constant(3, 0, 160), Fn1D::constant(3, 0, 160)}, {z0, z0}) != nullptr);
  UT_CHECK(make({Fn1D::linear(-3, 3, 0, 160), Fn1D::linear(3, -3, 0, 160)}, {z0, z0}) != nullptr);
  /* folds: the right border beyond the centre of curvature (t = -128) */
  UT_CHECK(make({Fn1D::constant(-130, 0, 160), Fn1D::constant(3, 0, 160)}, {z0, z0}) != nullptr);
  UT_CHECK(make({Fn1D::linear(-3, -129, 0, 160), Fn1D::constant(3, 0, 160)}, {z0, z0}) != nullptr);
  UT_CHECK(make({Fn1D::constant(-127, 0, 160), Fn1D::constant(3, 0, 160)}, {z0, z0}) == nullptr);
  /* a height that jumps */
  {
    util::Vector<double, 2> br;
    for (double x : {0.0, 80.0, 160.0}) {
      br.append(x);
    }
    util::Vector<Poly, 1> ps;
    ps.append(Poly::exact({5}));
    ps.append(Poly::exact({6}));
    Fn1D jump;
    UT_CHECK(Fn1D::make(br, ps, jump) == nullptr);
    UT_CHECK(make({Fn1D::constant(-3, 0, 160), Fn1D::constant(3, 0, 160)}, {z0, jump}) != nullptr);
  }
  /* domains: heights on another range, a range beyond the reference */
  UT_CHECK(make({Fn1D::constant(-3, 0, 160), Fn1D::constant(3, 0, 160)}, {z0, Fn1D::constant(5, 0, 150)}) != nullptr);
  UT_CHECK(make({Fn1D::constant(-3, 0, 170), Fn1D::constant(3, 0, 170)}, {Fn1D::constant(5, 0, 170),
                                                                          Fn1D::constant(5, 0, 170)}) != nullptr);
}

/* the builder: a crowned road with a superelevation transition on a spiral
 * and arc, two links on the right, one on the left */
void test_build()
{
  attr::PoolStore st;
  register_pools(st);
  const RoadFixture fx = test::road_fixtures()[1];
  util::Vector<CesaroSegment, 4> segs;
  for (auto &s : fx.ref.segs) {
    segs.append({s[0], Poly::exact({s[1], s[2], s[3], s[4]})});
  }
  Cesaro2 c;
  UT_CHECK(Cesaro2::make(fx.ref.x0, fx.ref.y0, fx.ref.th0, segs, c) == nullptr);
  const attr::PoolRef ref = st.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(c));
  const Fn1D z = to_fn(test::road_fx::linear({0, 64, 160, 224}, {20, 22, 25, 27}));
  RoadSection sec;
  sec.right.append({Fn1D::constant(3.75, 0, 224), to_fn(test::road_fx::linear({0, 64, 160, 224},
                                                                               {-1.0 / 32, 1.0 / 16, 1.0 / 16, -1.0 / 32}))});
  sec.right.append({Fn1D::constant(1.5, 0, 224), Fn1D::constant(-1.0 / 16, 0, 224)});
  sec.left.append({Fn1D::linear(3.5, 4.0, 0, 224), Fn1D::constant(-1.0 / 32, 0, 224)});
  RoadBuild rb;
  const char *e = build_road(st, store_interner(st), ref, z, sec, rb);
  if (e) {
    fprintf(stderr, "build_road: %s\n", e);
  }
  UT_CHECK(e == nullptr);
  UT_CHECK(rb.borders.size() == 4 && rb.pivot == 2 && rb.carriers.size() == 4 && rb.pcurves.size() == 4);
  if (rb.borders.size() != 4) {
    return;
  }
  UT_CHECK(rb.borders[2].is_zero() && rb.heights[2].equal(z));
  UT_CHECK(rb.offsets[2] == ref); /* b = 0 over the whole reference */
  for (int i = 0; i <= 32; i++) {
    const double s = 224.0 * i / 32;
    UT_CHECK(rb.borders[0].eval(s) < rb.borders[1].eval(s) && rb.borders[1].eval(s) < 0 &&
             rb.borders[3].eval(s) > 0);
    /* the left height: z - (3.5 + s / 448) / 32 */
    const double zl = z.eval(s) - (3.5 + 0.5 * s / 224) / 32;
    UT_CHECK(std::fabs(rb.heights[3].eval(s) - zl) <= 1e-12);
    /* pcurves: (s, b_k(s)) */
    for (int k = 0; k < 4; k++) {
      const auto g = curve2(st, rb.pcurves[k]);
      UT_CHECK(bool(g));
      if (g) {
        Vec<2> p[1];
        g->eval(s, 0, p);
        UT_CHECK(p[0][0] == s && same_bits(p[0][1], rb.borders[k].eval(s)));
      }
    }
  }
  /* the carriers lie on the surface bit for bit */
  const auto ev = surface(st, rb.road);
  UT_CHECK(bool(ev));
  int n = 0;
  for (int k = 0; k < 4 && ev; k++) {
    const auto c3 = curve3(st, rb.carriers[k]);
    UT_CHECK(bool(c3));
    for (int i = 0; i <= 64 && c3; i++) {
      const double s = 224.0 * i / 64;
      for (const Side su : {Side::Below, Side::Above}) {
        Vec<3> a[1], bb[1];
        c3->eval(s, 0, a, su);
        ev->eval(s, rb.borders[k].eval(s, 0, su), 0, bb, su);
        UT_CHECK(same_bits(a[0][0], bb[0][0]) && same_bits(a[0][1], bb[0][1]) && same_bits(a[0][2], bb[0][2]));
        n++;
      }
    }
  }
  UT_CHECK(n == 4 * 65 * 2);
  /* refusals: a domain that differs, a fold on the inside of the curve */
  RoadSection bad = sec;
  bad.left[0].width = Fn1D::constant(3.5, 0, 200);
  UT_CHECK(build_road(st, store_interner(st), ref, z, bad, rb) != nullptr);
  RoadSection fold = sec;
  fold.left[0].width = Fn1D::constant(300.0, 0, 224); /* radius 256 on the left */
  UT_CHECK(build_road(st, store_interner(st), ref, z, fold, rb) != nullptr);
}

/* a road on reference ref from Fn1D lists, interned (kNoRef on refusal) */
attr::PoolRef make_road(attr::PoolStore &st, attr::PoolRef ref, std::initializer_list<Fn1D> bs,
                        std::initializer_list<Fn1D> zs)
{
  util::Vector<Fn1D, 2> b, z;
  for (const Fn1D &f : bs) {
    b.append(f);
  }
  for (const Fn1D &f : zs) {
    z.append(f);
  }
  Road3 d;
  const char *e = Road3::make(st, ref, b, z, d);
  if (e) {
    fprintf(stderr, "make_road: %s\n", e);
    return attr::kNoRef;
  }
  return st.intern<Road3Pool>(pool_id(road3_kind()), std::move(d));
}

attr::PoolRef intern_cesaro(attr::PoolStore &st, Cesaro2 &&c)
{
  return st.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(c));
}

double fn_err(const Fn1D &f, int k)
{
  return f.eval_error(k) + (k == 0 ? f.max_jump() : 0.0);
}

/* Each term covers the error source it is for, on roads where that source
 * dominates (the oracle check can't see a missing term while the bound has
 * slack): necessary lower bounds from the inputs' own certified errors. */
void test_term_requirements()
{
  attr::PoolStore st;
  register_pools(st);
  detail::Road3Probe pr;
  {
    /* far borders (|t| ~ 5000: the tangent error times t outweighs the
     * rounding of the reference, and border eval errors are ~ 1e-12), a
     * 100 m height step */
    Cesaro2 c;
    UT_CHECK(Cesaro2::line(0, 0, 0.3, 100, c) == nullptr);
    const attr::PoolRef ref = intern_cesaro(st, std::move(c));
    const Fn1D b0 = Fn1D::linear(5000, 5000 + 100.0 / 1024, 0, 100),
               b1 = Fn1D::linear(5001, 5001 + 100.0 / 1024, 0, 100);
    const attr::PoolRef road = make_road(st, ref, {b0, b1}, {Fn1D::constant(0, 0, 100), Fn1D::constant(100, 0, 100)});
    UT_CHECK(!road.none() && detail::road3_probe(st, road, 50, 5000.5 + 50.0 / 1024, Side::Above, Side::Above, pr));
    const auto r = curve2(st, ref);
    const double be0 = r->eval_error(), be1 = r->deriv_eval_error(1);
    const double tmax = 5001 + 100.0 / 1024, Be = std::max(fn_err(b0, 0), fn_err(b1, 0));
    const detail::Road3Terms &t = pr.terms;
    printf("road terms, far borders: xy %.3g (be0 %.3g, t be1 %.3g), w %.3g (Be %.3g), z %.3g\n", t.xy, be0,
           tmax * be1, t.w, Be, t.z);
    UT_CHECK(Be > 0);
    UT_CHECK(t.xy >= be0 + tmax * be1);
    UT_CHECK(t.w >= Be);       /* the link is 1 wide */
    UT_CHECK(t.z >= 100 * Be); /* a w error moves z by D = 100 times it */
    UT_CHECK(t.e0 >= t.xy && t.e0 >= t.z);
  }
  {
    /* steep, low heights on a short road: Z_k' errors dominate S_s */
    Cesaro2 c;
    UT_CHECK(Cesaro2::line(0, 0, 0.0, 100, c) == nullptr);
    const attr::PoolRef ref = intern_cesaro(st, std::move(c));
    util::Vector<double, 2> br;
    br.append(0);
    br.append(0.01);
    util::Vector<Poly, 1> ps;
    ps.append(Poly::exact({0, 1e4, 1e3}));
    Fn1D zh;
    UT_CHECK(Fn1D::make(br, ps, zh) == nullptr);
    const attr::PoolRef road =
        make_road(st, ref, {Fn1D::constant(-1.75, 0, 0.01), Fn1D::constant(1.75, 0, 0.01)}, {zh, zh});
    UT_CHECK(!road.none() && detail::road3_probe(st, road, 0.005, 0, Side::Above, Side::Above, pr));
    const double ze1 = fn_err(zh, 1);
    printf("road terms, steep heights: e1 %.3g (Z' error %.3g)\n", pr.terms.e1, ze1);
    UT_CHECK(ze1 > 0 && pr.terms.e1 >= ze1);
  }
  {
    /* borders 4 km out on a right arc: errors of t r^(3) dominate S_ss */
    Cesaro2 c;
    UT_CHECK(Cesaro2::arc(0, 0, 0.0, -1.0 / 256, 200, c) == nullptr);
    const attr::PoolRef ref = intern_cesaro(st, std::move(c));
    const attr::PoolRef road = make_road(st, ref, {Fn1D::constant(4000, 0, 200), Fn1D::constant(4001, 0, 200)},
                                         {Fn1D::constant(0, 0, 200), Fn1D::constant(1, 0, 200)});
    UT_CHECK(!road.none() && detail::road3_probe(st, road, 100, 4000.5, Side::Above, Side::Above, pr));
    const double be2 = curve2(st, ref)->deriv_eval_error(2);
    printf("road terms, far arc: e2 %.3g (be2 %.3g, t r3 %.3g)\n", pr.terms.e2, be2, 4001 * pr.terms.r3);
    UT_CHECK(pr.terms.r3 > 0 && pr.terms.e2 >= be2 + 4001 * pr.terms.r3);
  }
}

/* on a border, sv picks the link: S_t's slope is the one on that side */
void test_link_sides()
{
  const RoadFixture f = test::road_fixtures()[1]; /* superelevation */
  Built b;
  UT_CHECK(build(f, b));
  const auto ev = surface(b.st, b.road);
  UT_CHECK(bool(ev));
  int differ = 0;
  for (double s : f.stations) {
    if (!ev) {
      break;
    }
    const double t = b.b[1].eval(s);
    Vec<3> lo[3], hi[3];
    ev->eval(s, t, 1, lo, Side::Above, Side::Below);
    ev->eval(s, t, 1, hi, Side::Above, Side::Above);
    const double below = (b.z[1].eval(s) - b.z[0].eval(s)) / (b.b[1].eval(s) - b.b[0].eval(s));
    const double above = (b.z[2].eval(s) - b.z[1].eval(s)) / (b.b[2].eval(s) - b.b[1].eval(s));
    UT_CHECK(lo[2][2] == below && hi[2][2] == above);
    UT_CHECK(same_bits(lo[0][2], hi[0][2])); /* S itself is continuous */
    differ += below != above;
  }
  UT_CHECK(differ > 0);
}

/* deriv_bound's Z_s covers w D' (constant borders and Z_0: the only slope in
 * s is the height difference's) */
void test_bound_dz()
{
  attr::PoolStore st;
  register_pools(st);
  Cesaro2 c;
  UT_CHECK(Cesaro2::line(0, 0, 0.0, 200, c) == nullptr);
  const attr::PoolRef ref = intern_cesaro(st, std::move(c));
  const attr::PoolRef road = make_road(st, ref, {Fn1D::constant(-2, 0, 200), Fn1D::constant(2, 0, 200)},
                                       {Fn1D::constant(3, 0, 200), Fn1D::linear(3, 5, 0, 200)});
  const auto ev = surface(st, road);
  UT_CHECK(bool(ev));
  if (!ev) {
    return;
  }
  const double bound = ev->deriv_bound({{0, 200}, {-2, 2}}, 1, 0);
  for (int i = 0; i <= 16; i++) {
    for (int j = 0; j <= 8; j++) {
      Vec<3> out[3];
      ev->eval(200.0 * i / 16, -2 + 4.0 * j / 8, 1, out);
      UT_CHECK(std::sqrt(out[1][0] * out[1][0] + out[1][1] * out[1][1] + out[1][2] * out[1][2]) <= bound);
    }
  }
}

/* special_u: the borders' and heights' breaks, at their continuity */
void test_special_u()
{
  const RoadFixture f = test::road_fixtures()[2]; /* widening: one arc, breaks at 40 and 120 */
  Built b;
  UT_CHECK(build(f, b));
  const auto ev = surface(b.st, b.road);
  UT_CHECK(bool(ev));
  if (!ev) {
    return;
  }
  util::Vector<SpecialParam> sp;
  ev->special_u(0, 160, sp);
  int c40 = -1, c120 = -1;
  for (const SpecialParam &p : sp) {
    c40 = p.t == 40 ? int(p.continuity) : c40;
    c120 = p.t == 120 ? int(p.continuity) : c120;
  }
  UT_CHECK(c40 == 1 && c120 == 0); /* b_1: C1 at 40 (its slope is 0 both sides), C0 at 120 */
}

} // namespace

int main()
{
  test_term_requirements();
  test_link_sides();
  test_bound_dz();
  test_special_u();
  test_oracle();
  test_terms();
  test_border_bits();
  test_deriv_bounds();
  test_refusals();
  test_build();
  return ut_test_end();
}
