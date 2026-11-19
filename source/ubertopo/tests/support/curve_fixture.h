#pragma once

/* Curved-edge helpers of the editor tests (plan P4a §6.3): two-point arc
 * carriers between existing vertices, interned through the editor. Only
 * detmath and basic arithmetic, so the results are the same bits under every
 * compiler (test_edit_random pins hashes of them). */

#include <cmath>

#include "litestl/math/detmath.h"
#include "ubertopo/curves/alignment.h"
#include "ubertopo/curves/cesaro.h"
#include "ubertopo/topo/curve_geom.h"
#include "ubertopo/topo/edit/editor.h"

namespace ubertopo::test {

/** atan2 without libm: Newton on x sin(t) - y cos(t) = 0 from the nearest
 * multiple of pi/4 (so |t - phi| <= pi/8 and the derivative stays positive),
 * a fixed number of steps. */
inline double det_atan2(double y, double x)
{
  namespace dm = litestl::math::detmath;
  constexpr double kPi = 3.141592653589793;
  if (x == 0 && y == 0) {
    return 0;
  }
  /* the octant direction closest to (x, y) */
  double best = 0, best_dot = -INFINITY;
  const double r = std::sqrt(x * x + y * y);
  for (int k = -3; k <= 4; k++) {
    const double t = k * (kPi / 4);
    double s, c;
    dm::sincos(t, &s, &c);
    const double d = (x * c + y * s) / r;
    if (d > best_dot) {
      best_dot = d;
      best = t;
    }
  }
  double t = best;
  for (int i = 0; i < 8; i++) {
    double s, c;
    dm::sincos(t, &s, &c);
    const double f = x * s - y * c, df = x * c + y * s;
    t -= f / df;
  }
  return t;
}

/** The plane arc from (ax, ay) to (bx, by) turning by 2 alpha (left for
 * alpha > 0; a line for alpha = 0). */
inline const char *two_point_arc(double ax, double ay, double bx, double by, double alpha, curves::Cesaro2 &out)
{
  namespace dm = litestl::math::detmath;
  const double dx = bx - ax, dy = by - ay;
  const double lc = std::sqrt(dx * dx + dy * dy);
  const double phi = det_atan2(dy, dx);
  if (alpha == 0) {
    return curves::Cesaro2::line(ax, ay, phi, lc, out);
  }
  const double sa = dm::sin(alpha);
  const double kappa = 2 * sa / lc;
  return curves::Cesaro2::arc(ax, ay, phi - alpha, kappa, alpha * lc / sa, out);
}

/** An alignment3 carrier from vertex @p a to @p b: the two-point arc in plan,
 * z linear in the station. *s gets the carrier's length (its range is
 * (0, *s) from a to b). */
template <geom::CoordType C>
topo::EditCode arc_carrier(topo::Editor<C> &ed, topo::VertRef a, topo::VertRef b, double alpha,
                           litestl::attr::PoolRef *ref, double *s)
{
  using namespace curves;
  auto &k = ed.complex();
  const auto pa = geom::to_double3<C>(k.pos(a)), pb = geom::to_double3<C>(k.pos(b));
  Cesaro2 h;
  if (two_point_arc(pa[0], pa[1], pb[0], pb[1], alpha, h)) {
    return topo::EditCode::BadArgument;
  }
  const double len = h.length();
  litestl::attr::PoolRef hr;
  topo::EditCode c = ed.intern_curve(cesaro2_kind(), h, &hr);
  if (c != topo::EditCode::Ok) {
    return c;
  }
  Alignment3 al;
  if (Alignment3::make(k.geom, hr, Fn1D::linear(pa[2], pb[2], 0, len), al)) {
    return topo::EditCode::BadArgument;
  }
  *s = len;
  return ed.intern_curve(alignment3_kind(), al, ref);
}

/** A frame-local point (frame units: metres for float types, quanta for
 * Lattice), rounded. */
template <geom::CoordType C> geom::Point<C> fpt(double x, double y, double z)
{
  geom::Point<C> p;
  topo::round_point<C>(litestl::math::double3(x, y, z), p);
  return p;
}

} // namespace ubertopo::test
