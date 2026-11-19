/* Pcurve kinds and road3's definition (plan P4b W1, §3.3, §7):
 *  - graph2 and segment2 against long-double evaluation, with their certified
 *    error and derivative bounds checked on dense samples;
 *  - refusals and canonical reads (-0.0, equal endpoints, empty f);
 *  - write/read round trips, including road3 (its evaluator lands in W3). */

#include <bit>
#include <cmath>
#include <cstdio>
#include <vector>

#include "ubertopo/curves/cesaro.h"
#include "ubertopo/curves/pcurve.h"
#include "ubertopo/curves/road.h"
#include "ut_test.h"

using namespace ubertopo;
using namespace ubertopo::curves;
using LD = long double;

namespace {

template <typename T> bool round_trip(const T &v)
{
  io::ByteWriter w;
  v.write(w);
  io::ByteReader r(w.data(), w.size());
  T back;
  return T::read(r, back, {}) && back.equal(v) && r.remaining() == 0;
}

/* f = 1.5 - 0.02 s + 3e-4 s^2 on [0, 40], then linear to 60: a C1 break at 40 */
Fn1D sample_fn()
{
  util::Vector<double, 2> br;
  for (double b : {0.0, 40.0, 60.0}) {
    br.append(b);
  }
  const double f40 = 1.5 - 0.02 * 40 + 3e-4 * 1600, d40 = -0.02 + 2 * 3e-4 * 40;
  util::Vector<Poly, 1> pc;
  pc.append(Poly::exact({1.5, -0.02, 3e-4}));
  pc.append(Poly::exact({f40, d40}));
  Fn1D f;
  UT_CHECK(Fn1D::make(br, pc, f) == nullptr);
  return f;
}

LD exact_f(LD s, int k)
{
  if (s <= 40) {
    return k == 0 ? 1.5L - 0.02L * s + 3e-4L * s * s : k == 1 ? -0.02L + 6e-4L * s : k == 2 ? 6e-4L : 0;
  }
  const LD f40 = LD(1.5 - 0.02 * 40 + 3e-4 * 1600), d40 = LD(-0.02 + 2 * 3e-4 * 40);
  return k == 0 ? f40 + d40 * (s - 40) : k == 1 ? d40 : 0;
}

void test_graph2()
{
  Graph2 g;
  UT_CHECK(Graph2::make(Fn1D(), g) != nullptr);
  UT_CHECK(Graph2::make(sample_fn(), g) == nullptr);
  UT_CHECK(round_trip(g));
  auto c = g.evaluator();
  UT_CHECK(c->domain().lo == 0 && c->domain().hi == 60 && c->param_kind() == ParamKind::Station);
  util::Vector<SpecialParam> sp;
  c->special_params(0, 60, sp);
  UT_CHECK(sp.size() == 1 && sp[0].t == 40 && sp[0].continuity == 1);
  const double e0 = c->eval_error(), e1 = c->deriv_eval_error(1);
  UT_CHECK(std::isfinite(e0) && std::isfinite(e1) && e0 > 0);
  /* deriv bounds over [0, 40] and [40, 60] (no break inside) */
  const double b[2][3] = {{c->deriv_bound(0, 40, 0), c->deriv_bound(0, 40, 1), c->deriv_bound(0, 40, 2)},
                          {c->deriv_bound(40, 60, 0), c->deriv_bound(40, 60, 1), c->deriv_bound(40, 60, 2)}};
  UT_CHECK(std::isinf(c->deriv_bound(30, 50, 2))); /* C1 at 40: no second-derivative bound across it */
  UT_CHECK(std::isfinite(c->deriv_bound(30, 50, 1)));
  int bad = 0;
  for (int i = 0; i <= 6000; i++) {
    const double s = 0.01 * i;
    const Side side = s == 40 && (i % 2) ? Side::Below : Side::Above;
    Vec<2> out[3];
    c->eval(s, 2, out, side);
    const int piece = s < 40 || (s == 40 && side == Side::Below) ? 0 : 1;
    const LD fx = exact_f(LD(s), 0);
    bad += out[0][0] != s ? 1 : 0;
    bad += std::fabs(LD(out[0][1]) - fx) > LD(e0) ? 1 : 0;
    bad += out[1][0] != 1 || std::fabs(LD(out[1][1]) - exact_f(s, 1)) > LD(e1) ? 1 : 0;
    const LD n0 = std::sqrt(LD(s) * s + fx * fx), n1 = std::sqrt(1 + exact_f(s, 1) * exact_f(s, 1));
    bad += n0 > LD(b[piece][0]) || n1 > LD(b[piece][1]) ? 1 : 0;
    bad += std::fabs(LD(out[2][1])) > LD(b[piece][2]) || out[2][0] != 0 ? 1 : 0;
  }
  UT_CHECK(bad == 0);
}

void test_segment2()
{
  Segment2 s;
  UT_CHECK(Segment2::make(1, 2, 1, 2, s) != nullptr);
  UT_CHECK(Segment2::make(NAN, 2, 1, 2, s) != nullptr);
  UT_CHECK(Segment2::make(-0.0, 2.5, 7.25, -3.0, s) == nullptr);
  UT_CHECK(std::bit_cast<uint64_t>(s.p[0]) == 0); /* -0.0 canonicalised */
  UT_CHECK(round_trip(s));
  {
    /* a stream holding -0.0 is not canonical */
    io::ByteWriter w;
    for (double x : {-0.0, 2.5, 7.25, -3.0}) {
      w.u64(std::bit_cast<uint64_t>(x));
    }
    io::ByteReader r(w.data(), w.size());
    Segment2 back;
    UT_CHECK(!Segment2::read(r, back, {}));
  }
  auto c = s.evaluator();
  UT_CHECK(c->domain().lo == 0 && c->domain().hi == 1);
  const double e0 = c->eval_error(), b0 = c->deriv_bound(0, 1, 0), b1 = c->deriv_bound(0, 1, 1);
  UT_CHECK(c->deriv_bound(0, 1, 2) == 0);
  int bad = 0;
  for (int i = 0; i <= 1000; i++) {
    const double t = i / 1000.0;
    Vec<2> out[3];
    c->eval(t, 2, out, Side::Above);
    const LD x = LD(t) * 7.25L, y = 2.5L + LD(t) * (-5.5L);
    bad += std::hypot(LD(out[0][0]) - x, LD(out[0][1]) - y) > LD(e0) ? 1 : 0;
    bad += std::hypot(x, y) > LD(b0) || std::hypot(7.25L, 5.5L) > LD(b1) ? 1 : 0;
    bad += out[1][0] != 7.25 || out[1][1] != -5.5 || out[2][0] != 0 || out[2][1] != 0 ? 1 : 0;
  }
  UT_CHECK(bad == 0);
}

void test_road3_definition()
{
  attr::PoolStore st;
  register_pools(st);
  Cesaro2 h;
  UT_CHECK(Cesaro2::arc(0, 0, 0.2, 0.004, 120, h) == nullptr);
  const attr::PoolRef hr = st.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(h));
  util::Vector<Fn1D, 2> b, z;
  for (double t : {-3.5, 0.0, 3.5}) {
    b.append(Fn1D::linear(t, t * 1.1, 0, 100));
    z.append(Fn1D::linear(10 - 0.02 * std::fabs(t), 12, 0, 100));
  }
  Road3 d;
  UT_CHECK(Road3::make(st, hr, b, z, d) == nullptr);
  UT_CHECK(round_trip(d));
  const attr::PoolRef dr = st.intern<Road3Pool>(pool_id(road3_kind()), Road3(d));
  UT_CHECK(surface_kind_of(st, dr) == road3_kind().as_surface() && curve_dim(st, dr) == 0);
  /* refusals: too few borders, mismatched heights, domains that differ or
   * leave the reference, a reference that isn't a cesaro2 */
  util::Vector<Fn1D, 2> one;
  one.append(b[0]);
  UT_CHECK(Road3::make(st, hr, one, one, d) != nullptr);
  util::Vector<Fn1D, 2> z2 = z;
  z2.pop_back();
  UT_CHECK(Road3::make(st, hr, b, z2, d) != nullptr);
  util::Vector<Fn1D, 2> b2 = b;
  b2[1] = Fn1D::constant(0.0, 0, 90);
  UT_CHECK(Road3::make(st, hr, b2, z, d) != nullptr);
  util::Vector<Fn1D, 2> b3, z3;
  for (int i = 0; i < 3; i++) {
    b3.append(Fn1D::constant(double(i), 0, 130));
    z3.append(Fn1D::constant(1.0, 0, 130));
  }
  UT_CHECK(Road3::make(st, hr, b3, z3, d) != nullptr); /* 130 > the reference's 120 */
  Graph2 g;
  UT_CHECK(Graph2::make(Fn1D::constant(1.0, 0, 100), g) == nullptr);
  const attr::PoolRef gr = st.intern<Graph2Pool>(pool_id(graph2_kind()), std::move(g));
  UT_CHECK(Road3::make(st, gr, b, z, d) != nullptr);
  UT_CHECK(curve_dim(st, gr) == 2 && !surface_kind_of(st, gr));
}

/* Fn1D::mul (plan P4b §3.1): merged breakpoints; exact products where every
 * value is a short dyadic (a == b exactly); refusals; a general product
 * against long double within its rounding */
void test_fn1d_mul()
{
  auto fn = [](std::initializer_list<double> br, std::initializer_list<std::initializer_list<double>> ps) {
    util::Vector<double, 2> b;
    for (double x : br) {
      b.append(x);
    }
    util::Vector<Poly, 1> p;
    for (auto c : ps) {
      p.append(Poly::exact(c));
    }
    Fn1D f;
    UT_CHECK(Fn1D::make(b, p, f) == nullptr);
    return f;
  };
  /* a: 1 + s/2 on [0, 1], then 1.5 - s'/4 + s'^2/8 on [1, 4]; b: 2 - s/4 on
   * [0, 2], then 1.5 + s'/2 on [2, 4] */
  const Fn1D a = fn({0, 1, 4}, {{1, 0.5}, {1.5, -0.25, 0.125}});
  const Fn1D b = fn({0, 2, 4}, {{2, -0.25}, {1.5, 0.5}});
  Fn1D p;
  UT_CHECK(Fn1D::mul(a, b, p) == nullptr);
  UT_CHECK(p.breaks.size() == 4 && p.breaks[0] == 0 && p.breaks[1] == 1 && p.breaks[2] == 2 && p.breaks[3] == 4);
  UT_CHECK(p.pieces.size() == 3 && p.pieces[1].degree() == 3);
  for (int i = 0; i <= 64; i++) {
    const double s = 4.0 * i / 64;
    for (const Side sd : {Side::Below, Side::Above}) {
      UT_CHECK(p.eval(s, 0, sd) == a.eval(s, 0, sd) * b.eval(s, 0, sd));
    }
  }
  /* refusals: degree 4 x degree 4 > kMaxDegree, different domains */
  const Fn1D q4 = fn({0, 4}, {{1, 0, 0, 0, 0.5}});
  UT_CHECK(Fn1D::mul(q4, q4, p) != nullptr);
  UT_CHECK(Fn1D::mul(a, fn({0, 3}, {{1}}), p) != nullptr);
  /* a general product: the sample function times a linear one */
  const Fn1D f = sample_fn(), g = Fn1D::linear(0.3, -1.7, 0, 60);
  UT_CHECK(Fn1D::mul(f, g, p) == nullptr);
  for (int i = 0; i <= 240; i++) {
    const double s = 60.0 * i / 240;
    const LD want = exact_f(s, 0) * (LD(0.3) + (LD(-1.7) - LD(0.3)) * LD(s) / 60);
    UT_CHECK(std::fabs(LD(p.eval(s)) - want) <= 1e-12L);
  }
}

} // namespace

int main()
{
  test_graph2();
  test_segment2();
  test_road3_definition();
  test_fn1d_mul();
  return ut_test_end();
}
