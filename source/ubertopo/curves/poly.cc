/* Polynomials with certified rounding bounds (poly.h). */

#include "ubertopo/curves/poly.h"

#include <algorithm>
#include <cmath>

namespace ubertopo::curves {

namespace {

/* majorant Horner: sum |c_i| |x|^i (rounded up by the callers' up()) */
double abs_horner(const util::Vector<double, 8> &c, double ax)
{
  double r = 0;
  for (int i = c.size(); i-- > 0;) {
    r = r * ax + std::fabs(c[i]);
  }
  return r;
}

void resize_like(Poly &p, int n)
{
  p.c.resize(n);
  p.e.resize(n);
  for (int i = 0; i < n; i++) {
    p.c[i] = 0;
    p.e[i] = 0;
  }
}

/* Taylor shift t_k = sum_i C(i, k) a^(i - k) c_i (nested Horner scheme) */
void taylor_shift(util::Vector<double, 8> &t, double a)
{
  const int n = t.size() - 1;
  for (int i = 0; i < n; i++) {
    for (int j = n - 1; j >= i; j--) {
      t[j] = t[j] + a * t[j + 1];
    }
  }
}

} // namespace

Poly Poly::exact(std::initializer_list<double> cs)
{
  Poly p;
  for (double v : cs) {
    p.c.append(v);
    p.e.append(0);
  }
  return p;
}

Poly Poly::exact(const double *cs, int n)
{
  Poly p;
  for (int i = 0; i < n; i++) {
    p.c.append(cs[i]);
    p.e.append(0);
  }
  return p;
}

Poly Poly::constant(double v, double err)
{
  Poly p;
  p.c.append(v);
  p.e.append(err);
  return p;
}

bool Poly::exact_data() const
{
  for (double x : e) {
    if (x != 0) {
      return false;
    }
  }
  return true;
}

double Poly::eval(double x, double *err) const
{
  const int n = c.size();
  if (n == 0) {
    if (err) {
      *err = 0;
    }
    return 0;
  }
  double r = c[n - 1];
  for (int i = n - 1; i-- > 0;) {
    r = r * x + c[i];
  }
  if (err) {
    const double ax = std::fabs(x);
    double ee = 0;
    for (int i = n; i-- > 0;) {
      ee = ee * ax + e[i];
    }
    *err = up(gamma_n(2 * n) * abs_horner(c, ax) + ee);
  }
  return r;
}

Poly Poly::deriv() const
{
  Poly d;
  const int n = c.size();
  if (n <= 1) {
    return d;
  }
  resize_like(d, n - 1);
  for (int k = 0; k < n - 1; k++) {
    const double m = double(k + 1);
    d.c[k] = m * c[k + 1];
    d.e[k] = up(m * e[k + 1] + std::fabs(d.c[k]) * kUnit);
  }
  return d;
}

Poly Poly::integral(double k) const
{
  Poly r;
  const int n = c.size();
  resize_like(r, n + 1);
  r.c[0] = k;
  for (int i = 0; i < n; i++) {
    const double m = double(i + 1);
    r.c[i + 1] = c[i] / m;
    r.e[i + 1] = up(e[i] / m + std::fabs(r.c[i + 1]) * kUnit);
  }
  return r;
}

Poly Poly::operator+(const Poly &b) const
{
  Poly s;
  const int n = std::max(c.size(), b.c.size());
  resize_like(s, n);
  for (int i = 0; i < n; i++) {
    const double x = i < c.size() ? c[i] : 0, y = i < b.c.size() ? b.c[i] : 0;
    const double ex = i < e.size() ? e[i] : 0, ey = i < b.e.size() ? b.e[i] : 0;
    s.c[i] = x + y;
    s.e[i] = up(ex + ey + std::fabs(s.c[i]) * kUnit);
  }
  return s;
}

Poly Poly::operator-(const Poly &b) const
{
  return *this + b.scaled(-1.0);
}

Poly Poly::scaled(double s) const
{
  Poly r = *this;
  const double as = std::fabs(s);
  for (int i = 0; i < r.c.size(); i++) {
    r.c[i] = c[i] * s;
    r.e[i] = s == 1.0 || s == -1.0 ? e[i] : up(e[i] * as + std::fabs(r.c[i]) * kUnit);
  }
  return r;
}

Poly Poly::operator*(const Poly &b) const
{
  Poly p;
  const int na = c.size(), nb = b.c.size();
  if (na == 0 || nb == 0) {
    return p;
  }
  const int n = na + nb - 1;
  resize_like(p, n);
  util::Vector<double, 16> mag;
  mag.resize(n);
  for (int k = 0; k < n; k++) {
    mag[k] = 0;
  }
  for (int i = 0; i < na; i++) {
    for (int j = 0; j < nb; j++) {
      p.c[i + j] = p.c[i + j] + c[i] * b.c[j];
      mag[i + j] = mag[i + j] + std::fabs(c[i] * b.c[j]);
      p.e[i + j] = p.e[i + j] + (std::fabs(c[i]) * b.e[j] + e[i] * std::fabs(b.c[j]) + e[i] * b.e[j]);
    }
  }
  const int m = std::min(na, nb) + 1;
  for (int k = 0; k < n; k++) {
    p.e[k] = up(p.e[k] + gamma_n(m + 1) * mag[k]);
  }
  return p;
}

Poly Poly::compose_affine(double a, double h) const
{
  Poly q;
  const int n = c.size();
  if (n == 0) {
    return q;
  }
  q.c = c;
  taylor_shift(q.c, a);
  util::Vector<double, 8> mag, err;
  mag.resize(n);
  err.resize(n);
  for (int i = 0; i < n; i++) {
    mag[i] = std::fabs(c[i]);
    err[i] = e[i];
  }
  taylor_shift(mag, std::fabs(a));
  taylor_shift(err, std::fabs(a));
  q.e.resize(n);
  const double ah = std::fabs(h);
  double hp = 1.0, ahp = 1.0;
  for (int k = 0; k < n; k++) {
    const double shifted_err = up(err[k] + gamma_n(3 * n + 2) * mag[k]);
    const double v = q.c[k] * hp;
    q.e[k] = up(shifted_err * ahp * (1 + gamma_n(k + 1)) + gamma_n(k + 1) * std::fabs(v));
    q.c[k] = v;
    hp *= h;
    ahp *= ah;
  }
  return q;
}

void Poly::canonicalize()
{
  while (c.size() > 0 && c.last() == 0 && e.last() == 0) {
    c.resize(c.size() - 1);
    e.resize(e.size() - 1);
  }
  for (double &x : c) {
    if (x == 0) {
      x = 0.0; /* -0.0 -> +0.0 */
    }
  }
}

Range Poly::range(double a, double b, int subdiv) const
{
  const int n = c.size();
  if (n == 0) {
    return {0, 0};
  }
  Range out{INFINITY, -INFINITY};
  const int parts = 1 << subdiv;
  const double w = (b - a) / double(parts);
  for (int part = 0; part < parts; part++) {
    const double x0 = part == 0 ? a : a + w * double(part);
    const double x1 = part == parts - 1 ? b : a + w * double(part + 1);
    const Poly q = compose_affine(x0, x1 - x0);
    const int deg = n - 1;
    double sum_abs = 0, sum_err = 0;
    for (int i = 0; i < n; i++) {
      sum_abs += std::fabs(q.c[i]);
      sum_err += q.e[i];
    }
    const double E = up(gamma_n(2 * deg + 4) * sum_abs + sum_err);
    /* Bernstein coefficients b_k = sum_{i <= k} C(k, i) / C(deg, i) q_i */
    double lo = INFINITY, hi = -INFINITY;
    for (int k = 0; k <= deg; k++) {
      double bk = 0;
      double cki = 1.0;   /* C(k, i) */
      double cni = 1.0;   /* C(deg, i) */
      for (int i = 0; i <= k; i++) {
        bk += (cki / cni) * q.c[i];
        cki = cki * double(k - i) / double(i + 1);
        cni = cni * double(deg - i) / double(i + 1);
      }
      lo = std::min(lo, bk);
      hi = std::max(hi, bk);
    }
    out.lo = std::min(out.lo, lo - E);
    out.hi = std::max(out.hi, hi + E);
  }
  return out;
}

namespace {

void isolate(const Poly &p, const Poly &dp, double a, double b, double tol, int depth, util::Vector<Root> &out)
{
  const Range r = p.range(a, b);
  if (r.lo > 0 || r.hi < 0) {
    return;
  }
  if (b - a <= tol || depth >= 80) {
    out.append({0.5 * (a + b), 2});
    return;
  }
  const Range d = dp.range(a, b);
  if (d.lo > 0 || d.hi < 0) {
    double ea, eb;
    const double fa = p.eval(a, &ea), fb = p.eval(b, &eb);
    const bool a_pos = fa - ea > 0, a_neg = fa + ea < 0, b_pos = fb - eb > 0, b_neg = fb + eb < 0;
    if ((a_pos && b_neg) || (a_neg && b_pos)) {
      double lo = a, hi = b;
      for (int it = 0; it < 200; it++) {
        const double m = 0.5 * (lo + hi);
        if (!(m > lo && m < hi)) {
          break;
        }
        double em;
        const double fm = p.eval(m, &em);
        if (std::fabs(fm) <= em) {
          lo = hi = m;
          break;
        }
        if ((fm > 0) == a_pos) {
          lo = m;
        }
        else {
          hi = m;
        }
      }
      out.append({0.5 * (lo + hi), 1});
      return;
    }
    const bool a_zero = !a_pos && !a_neg, b_zero = !b_pos && !b_neg;
    if (a_zero && b_zero) {
      out.append({0.5 * (a + b), 2});
    }
    else if (a_zero) {
      out.append({a, 1});
    }
    else if (b_zero) {
      out.append({b, 1});
    }
    return;
  }
  const double m = 0.5 * (a + b);
  isolate(p, dp, a, m, tol, depth + 1, out);
  isolate(p, dp, m, b, tol, depth + 1, out);
}

} // namespace

void Poly::roots(double a, double b, util::Vector<Root> &out, double tol) const
{
  if (c.size() == 0) {
    return; /* the zero polynomial: callers treat it separately */
  }
  if (tol <= 0) {
    /* simple roots are bisected to full precision regardless; tol only stops
     * intervals where p and p' may both vanish, whose width near a double
     * root is ~sqrt(eps) */
    tol = 0x1p-26 * std::max({1.0, std::fabs(a), std::fabs(b)});
  }
  util::Vector<Root> found;
  isolate(*this, deriv(), a, b, tol, 0, found);
  std::sort(found.begin(), found.end(), [](const Root &x, const Root &y) { return x.t < y.t; });
  /* merge: duplicates found at a shared interval end, and clusters of
   * unresolved intervals around one multiple root (reported at the centre) */
  const int first = out.size();
  double cluster_lo = 0;
  for (const Root &x : found) {
    if (out.size() > first) {
      Root &last = out.last();
      const double reach = (last.mult == 2 && x.mult == 2) ? 4 * tol : tol;
      if (x.t - last.t <= reach) {
        last.mult = std::max(last.mult, x.mult);
        if (x.mult == 2) {
          last.t = 0.5 * (cluster_lo + x.t);
        }
        continue;
      }
    }
    cluster_lo = x.t;
    out.append(x);
  }
}

} // namespace ubertopo::curves
