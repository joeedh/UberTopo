/* curves/poly.h and curves/fn1d.h (plan P4a W1): certified ranges and
 * evaluation bounds, roots, piecewise functions and their canonical form. */

#include <cmath>
#include <cstdio>

#include "support/splitmix64.h"
#include "ubertopo/curves/fn1d.h"
#include "ubertopo/curves/poly.h"
#include "ut_test.h"

using namespace ubertopo::curves;
using litestl::test::SplitMix64;

namespace {

double uni(SplitMix64 &r, double a, double b)
{
  return a + (b - a) * r.unit();
}

Poly random_poly(SplitMix64 &r, int deg, double scale)
{
  Poly p;
  for (int i = 0; i <= deg; i++) {
    p.c.append(uni(r, -scale, scale));
    p.e.append(0);
  }
  return p;
}

/* a (very) high-precision reference: long double Horner, compared with a
 * margin far below the bounds under test */
long double ref_eval(const Poly &p, long double x)
{
  long double v = 0;
  for (int i = p.size(); i-- > 0;) {
    v = v * x + (long double)p.c[i];
  }
  return v;
}

void test_eval_and_ops()
{
  SplitMix64 r(11);
  int bad = 0;
  for (int it = 0; it < 2000; it++) {
    const Poly a = random_poly(r, int(r.below(8)), 10), b = random_poly(r, int(r.below(6)), 3);
    const double x = uni(r, -2, 2);
    double ea, eb;
    const double va = a.eval(x, &ea), vb = b.eval(x, &eb);
    bad += std::fabs((long double)va - ref_eval(a, x)) > ea;
    /* derived polynomials enclose the exact derived polynomial's values */
    const Poly prod = a * b, sum = a + b, dif = a - b, der = a.deriv(), integ = a.integral(1.5);
    double e;
    bad += std::fabs((long double)prod.eval(x, &e) - ref_eval(a, x) * ref_eval(b, x)) > e;
    bad += std::fabs((long double)sum.eval(x, &e) - (ref_eval(a, x) + ref_eval(b, x))) > e;
    bad += std::fabs((long double)dif.eval(x, &e) - (ref_eval(a, x) - ref_eval(b, x))) > e;
    /* derivative and integral against central differences in long double
     * would be noisy: check the identity deriv(integral(a)) == a instead */
    const Poly back = integ.deriv();
    for (int i = 0; i < a.size(); i++) {
      bad += std::fabs(back.c[i] - a.c[i]) > back.e[i] + 1e-300;
    }
    (void)der;
    /* compose: q(u) = a(c + h u) */
    const double c0 = uni(r, -1, 1), h = uni(r, 0.01, 2), u = uni(r, 0, 1);
    const Poly q = a.compose_affine(c0, h);
    const long double xr = (long double)c0 + (long double)h * (long double)u;
    bad += std::fabs((long double)q.eval(u, &e) - ref_eval(a, xr)) > e + 1e-15L * std::fabs(ref_eval(a.deriv(), xr));
  }
  if (bad) {
    fprintf(stderr, "eval/ops bound violations: %d\n", bad);
  }
  UT_CHECK(bad == 0);
}

void test_ranges()
{
  SplitMix64 r(12);
  int bad = 0;
  for (int it = 0; it < 3000; it++) {
    const Poly p = random_poly(r, 1 + int(r.below(9)), 5);
    const double a = uni(r, -3, 3), b = a + uni(r, 1e-6, 4);
    const Range rg = p.range(a, b, int(r.below(4)));
    for (int s = 0; s <= 64; s++) {
      const long double x = (long double)a + ((long double)b - a) * s / 64.0L;
      const long double v = ref_eval(p, x);
      bad += v < rg.lo || v > rg.hi;
    }
  }
  if (bad) {
    fprintf(stderr, "range containment violations: %d\n", bad);
  }
  UT_CHECK(bad == 0);
  /* tightness: subdivision converges to the true range of a monotone cubic */
  const Poly m = Poly::exact({0.25, 1.0, -0.5, 1.0}); /* p' = 1 - x + 3x^2 > 0 */
  const double lo = 0.25, hi = 0.25 + 1.0 - 0.5 + 1.0;
  const Range r4 = m.range(0, 1, 4), r0 = m.range(0, 1, 0);
  UT_CHECK(r4.lo <= lo && r4.hi >= hi && r4.lo > lo - 1e-3 && r4.hi < hi + 1e-3);
  UT_CHECK(r0.lo <= r4.lo && r0.hi >= r4.hi);
  /* a polynomial with data error: the enclosure widens by at least the error */
  Poly pe = Poly::exact({1.0, 2.0});
  pe.e[0] = 0.125;
  const Range re = pe.range(0, 1);
  UT_CHECK(re.lo <= 0.875 && re.hi >= 3.125);
}

void test_roots()
{
  /* (x - 0.3)(x - 0.7)(x + 2) */
  const Poly a = Poly::exact({0.21, -1.0, 1.0}) * Poly::exact({2.0, 1.0});
  util::Vector<Root> rs;
  a.roots(-3, 1, rs);
  UT_CHECK(rs.size() == 3);
  if (rs.size() == 3) {
    UT_CHECK(std::fabs(rs[0].t + 2) < 1e-13 && std::fabs(rs[1].t - 0.3) < 1e-13 && std::fabs(rs[2].t - 0.7) < 1e-13);
    UT_CHECK(rs[0].mult == 1 && rs[1].mult == 1 && rs[2].mult == 1);
  }
  /* a double root */
  rs.clear();
  Poly::exact({0.25, -1.0, 1.0}).roots(0, 1, rs);
  UT_CHECK(rs.size() == 1 && std::fabs(rs[0].t - 0.5) < 1e-6 && rs[0].mult == 2);
  /* roots at the interval ends */
  rs.clear();
  Poly::exact({0.0, -1.0, 1.0}).roots(0, 1, rs);
  UT_CHECK(rs.size() == 2 && rs[0].t == 0 && rs[1].t == 1);
  /* none */
  rs.clear();
  Poly::exact({1.0, 0.0, 1.0}).roots(-5, 5, rs);
  UT_CHECK(rs.size() == 0);
  /* random products of known roots */
  SplitMix64 r(13);
  int bad = 0;
  for (int it = 0; it < 300; it++) {
    const int n = 1 + int(r.below(5));
    double t[5];
    Poly p = Poly::exact({1.0});
    for (int i = 0; i < n; i++) {
      t[i] = uni(r, -1, 1);
      p = p * Poly::exact({-t[i], 1.0});
    }
    rs.clear();
    p.roots(-1.5, 1.5, rs);
    for (int i = 0; i < n; i++) {
      bool found = false;
      for (const Root &x : rs) {
        found = found || std::fabs(x.t - t[i]) < 1e-6;
      }
      bad += !found;
    }
  }
  UT_CHECK(bad == 0);
}

void test_fn1d()
{
  util::Vector<double, 2> br;
  util::Vector<Poly, 1> ps;
  for (double x : {0.0, 10.0, 10.0, 25.0}) {
    br.append(x);
  }
  ps.append(Poly::exact({1.0, -0.0, 0.0}));
  ps.append(Poly::exact({7.0}));  /* zero length: dropped */
  ps.append(Poly::exact({1.0, 0.5, 0.0}));
  Fn1D f;
  UT_CHECK(Fn1D::make(br, ps, f) == nullptr);
  UT_CHECK(f.pieces.size() == 2 && f.breaks.size() == 3);
  UT_CHECK(f.pieces[0].size() == 1 && f.pieces[1].size() == 2); /* trailing zeros trimmed */
  /* a second construction is bitwise equal */
  Fn1D g;
  UT_CHECK(Fn1D::make(br, ps, g) == nullptr && f.equal(g));
  /* evaluation and one-sided limits */
  double err;
  UT_CHECK(f.eval(5) == 1.0);
  UT_CHECK(f.eval(10, 0, Side::Below) == 1.0 && f.eval(10, 0, Side::Above) == 1.0);
  UT_CHECK(f.eval(10, 1, Side::Below) == 0.0 && f.eval(10, 1, Side::Above) == 0.5);
  UT_CHECK(std::fabs(f.eval(20, 0, Side::Above, &err) - 6.0) <= err);
  const Range rg = f.range(0, 25, 0);
  UT_CHECK(rg.lo <= 1.0 && rg.hi >= 8.5 && rg.hi < 8.6);
  /* a point range at a break covers both one-sided limits (it was empty) */
  const Range rb = f.range(10, 10, 1);
  UT_CHECK(rb.lo <= 0.0 && rb.hi >= 0.5);
  /* refusals */
  Fn1D h;
  util::Vector<Poly, 1> bad;
  bad.append(Poly::exact({NAN}));
  util::Vector<double, 2> b2;
  b2.append(0);
  b2.append(1);
  UT_CHECK(Fn1D::make(b2, bad, h) != nullptr);
  util::Vector<double, 2> b3;
  b3.append(1);
  b3.append(0);
  util::Vector<Poly, 1> one;
  one.append(Poly::exact({1.0}));
  UT_CHECK(Fn1D::make(b3, one, h) != nullptr);
  /* add: breakpoints merged */
  Fn1D lin = Fn1D::linear(0, 5, 0, 25), sum;
  UT_CHECK(Fn1D::add(f, lin, sum) == nullptr && sum.pieces.size() == 2);
  UT_CHECK(std::fabs(sum.eval(20, 0, Side::Above, &err) - 10.0) <= err + 1e-14);
  /* I/O round trip; a non-canonical stream (trailing zero) is refused */
  litestl::io::ByteWriter w;
  f.write(w);
  litestl::io::ByteReader rd(w.data(), w.size());
  Fn1D back;
  UT_CHECK(Fn1D::read(rd, back) && back.equal(f));
  Fn1D raw = f;
  raw.pieces[0].c.append(0.0);
  raw.pieces[0].e.append(0.0);
  litestl::io::ByteWriter w2;
  raw.write(w2);
  litestl::io::ByteReader rd2(w2.data(), w2.size());
  UT_CHECK(!Fn1D::read(rd2, back));
}

} // namespace

int main()
{
  test_eval_and_ops();
  test_ranges();
  test_roots();
  test_fn1d();
  return ut_test_end();
}
