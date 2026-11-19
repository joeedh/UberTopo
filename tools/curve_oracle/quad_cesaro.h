#pragma once

/* Quad-precision cesaro2 evaluation shared by the oracle tools (plan P4a
 * §3.8): composite Gauss-Legendre directly on e^{i theta(s)}, nodes computed
 * in quad. Offline only (GCC with libquadmath). */

#include <quadmath.h>

#include <vector>

namespace quad_oracle {

using Q = __float128;

constexpr int kNodes = 24;
inline Q gl_x[kNodes], gl_w[kNodes];

inline void gl_init()
{
  for (int i = 0; i < kNodes; i++) {
    Q x = cosq(M_PIq * (Q(i) + 0.75Q) / (Q(kNodes) + 0.5Q));
    for (int it = 0; it < 100; it++) {
      Q p0 = 1, p1 = x;
      for (int k = 2; k <= kNodes; k++) {
        const Q p2 = ((2 * k - 1) * x * p1 - (k - 1) * p0) / k;
        p0 = p1;
        p1 = p2;
      }
      const Q dp = kNodes * (x * p1 - p0) / (x * x - 1);
      const Q dx = p1 / dp;
      x -= dx;
      if (fabsq(dx) < 1e-33Q) {
        break;
      }
    }
    Q p0 = 1, p1 = x;
    for (int k = 2; k <= kNodes; k++) {
      const Q p2 = ((2 * k - 1) * x * p1 - (k - 1) * p0) / k;
      p0 = p1;
      p1 = p2;
    }
    const Q dp = kNodes * (x * p1 - p0) / (x * x - 1);
    gl_x[i] = x;
    gl_w[i] = 2 / ((1 - x * x) * dp * dp);
  }
}

struct QSeg {
  Q L, th0, k[4];
  Q theta(Q s) const
  {
    return th0 + s * (k[0] + s * (k[1] / 2 + s * (k[2] / 3 + s * (k[3] / 4))));
  }
  Q kappa_max() const
  {
    Q m = 0;
    for (int i = 0; i <= 64; i++) {
      const Q s = L * i / 64;
      const Q v = fabsq(k[0] + s * (k[1] + s * (k[2] + s * k[3])));
      m = v > m ? v : m;
    }
    return m;
  }
};

/* int_{a}^{b} e^{i theta} over a segment, `panels` per unit of bend scale */
inline void integrate(const QSeg &g, Q a, Q b, int refine, Q &ix, Q &iy)
{
  ix = iy = 0;
  if (b <= a) {
    return;
  }
  const Q km = g.kappa_max();
  Q h = 2;
  if (km > 0 && 0.02Q / km < h) {
    h = 0.02Q / km;
  }
  long n = long((b - a) / h) + 1;
  n *= refine;
  const Q w = (b - a) / n;
  for (long p = 0; p < n; p++) {
    const Q c = a + w * (p + 0.5Q);
    for (int i = 0; i < kNodes; i++) {
      const Q th = g.theta(c + 0.5Q * w * gl_x[i]);
      ix += gl_w[i] * cosq(th) * 0.5Q * w;
      iy += gl_w[i] * sinq(th) * 0.5Q * w;
    }
  }
}

inline void eval(const std::vector<QSeg> &segs, Q x0, Q y0, Q s, int refine, Q &x, Q &y, Q &tx, Q &ty)
{
  x = x0;
  y = y0;
  Q acc = 0;
  for (size_t i = 0; i < segs.size(); i++) {
    const QSeg &g = segs[i];
    const bool last = i + 1 == segs.size();
    const Q take = (s - acc < g.L && !last) ? s - acc : (last ? s - acc : g.L);
    Q ix, iy;
    integrate(g, 0, take, refine, ix, iy);
    x += ix;
    y += iy;
    if (s - acc <= g.L || last) {
      const Q th = g.theta(take);
      tx = cosq(th);
      ty = sinq(th);
      return;
    }
    acc += g.L;
  }
}


} // namespace quad_oracle
