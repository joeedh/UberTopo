#pragma once

/* Edge carriers against the vertex skeleton (plan P4a §2, §5.1, §5.3).
 *
 * Curves evaluate in frame-local doubles: the values geom::to_double3
 * returns (metres for double and float, quanta for Lattice). round_point is
 * round_C, the conversion back; rounding_of is rounding_C, the per-axis bound
 * on |round_C(x) - x| at a stored point.
 *
 * The endpoint rule: an edge's carrier meets its end vertex p at parameter t
 * when |c(t) - p| <= eval_error + sqrt(3) rounding_C(p) + curve_tol, with a
 * relative allowance for the distance computation's own rounding. */

#include <cmath>
#include <limits>

#include "litestl/attr/pool_store.h"
#include "ubertopo/curves/curve.h"
#include "ubertopo/geom/coord.h"

namespace ubertopo::topo {

/** round_C: frame-local doubles to the nearest stored point. False if a
 * coordinate is not finite or out of the coordinate type's range. */
template <geom::CoordType C> bool round_point(const litestl::math::double3 &x, geom::Point<C> &out)
{
  for (int i = 0; i < 3; i++) {
    if (!std::isfinite(x[i])) {
      return false;
    }
  }
  if constexpr (std::is_same_v<C, geom::Lattice>) {
    constexpr double lim = double(geom::TileFrame::kLatticeLimit);
    for (int i = 0; i < 3; i++) {
      const double q = std::round(x[i]); /* ties away from zero, as TileFrame */
      if (std::fabs(q) > lim) {
        return false;
      }
      out[i] = geom::Lattice(int64_t(q));
    }
  }
  else if constexpr (std::is_same_v<C, float>) {
    for (int i = 0; i < 3; i++) {
      if (std::fabs(x[i]) > double(std::numeric_limits<float>::max())) {
        return false;
      }
      out[i] = float(x[i]);
    }
  }
  else {
    for (int i = 0; i < 3; i++) {
      out[i] = x[i];
    }
  }
  return true;
}

/** rounding_C(p): a bound, per axis, on how far the exact point stored as p
 * may lie from it: half a quantum for Lattice; one ulp of the largest
 * coordinate for double and float (one ulp, not half: the exact value may
 * lie in the binade above). */
template <geom::CoordType C> double rounding_of(const geom::Point<C> &p)
{
  if constexpr (std::is_same_v<C, geom::Lattice>) {
    return 0.5;
  }
  else {
    constexpr int mant = std::numeric_limits<C>::digits - 1; /* 52 or 23 */
    double m = 0;
    for (int i = 0; i < 3; i++) {
      m = std::fmax(m, std::fabs(double(p[i])));
    }
    if (m == 0) {
      return double(std::numeric_limits<C>::denorm_min());
    }
    return std::ldexp(1.0, std::ilogb(m) - mant);
  }
}

/** The carrier of an edge, checked: @p r live and of a kind .e.curve accepts
 * (3D), the range inside the domain with distinct ends, and no offset fold
 * inside it. Null with *why on failure; *range_bad tells a range failure
 * (CurveRange) from a ref failure (CurveKind). */
litestl::util::UniquePtr<curves::Curve3> edge_carrier(const litestl::attr::PoolStore &s,
                                                      litestl::attr::PoolRef r,
                                                      double t0,
                                                      double t1,
                                                      bool *range_bad = nullptr,
                                                      const char **why = nullptr);

/** The kind check alone (what the store's accept rule says for .e.curve). */
bool edge_curve_kind_ok(const litestl::attr::PoolStore &s, litestl::attr::PoolRef r);

/** |c(t) - p| within the endpoint tolerance (header comment). */
template <geom::CoordType C>
bool on_carrier(const curves::Curve3 &c, double t, const geom::Point<C> &p, double curve_tol, curves::Side side)
{
  curves::Vec<3> x;
  c.eval(t, 0, &x, side);
  const auto q = geom::to_double3<C>(p);
  const double dx = x[0] - q[0], dy = x[1] - q[1], dz = x[2] - q[2];
  const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
  constexpr double kSqrt3Up = 1.7320508075688774; /* > sqrt(3) */
  const double tol = c.eval_error() + kSqrt3Up * rounding_of<C>(p) + curve_tol;
  /* the distance (about six roundings) and the tolerance sum (three) */
  return std::isfinite(d) && d * (1 - 0x1p-49) <= tol * (1 + 0x1p-49);
}

} // namespace ubertopo::topo
