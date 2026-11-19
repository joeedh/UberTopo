#pragma once

/* catenary3: a hanging cable between two anchors (plan P4a §3.6).
 *
 * Definition: anchors A, B and the catenary parameter a > 0. The curve lies
 * in the vertical plane through A and B:
 *   r(x) = A + x u + (f(x) - f(0)) z,  f(x) = a cosh((x - c) / a),  x in [0, d],
 * with u the horizontal unit vector from A towards B, d the horizontal
 * distance and c (derived) placing the curve through B. It is evaluated as
 *   f(x) - f(0) = 2a sinh((x - 2c) / (2a)) sinh(x / (2a)),
 * which has no cancellation when a >> d.
 *
 * Derived data (make): d, u, c and a bound on c's rounding; every bound
 * charges the rounding of the derived values against the exact definition.
 *
 * Canonical form: -0.0 -> +0.0. Refused: non-finite values, a <= 0, anchors
 * on one vertical line, and spans whose hyperbolic arguments exceed
 * kMaxArg (overflow). */

#include <cstdint>

#include "ubertopo/curves/curve.h"

namespace ubertopo::curves {

class Catenary3 {
public:
  static constexpr double kMaxArg = 600; /* |x - c| / a and d / (2a) */
  static constexpr bool has_refs = false;

  /* definition */
  double A[3] = {0, 0, 0}, B[3] = {0, 0, 0};
  double a = 1;

  /* derived (make) */
  double d = 0, ux = 0, uy = 0, c = 0, c_err = 0;

  static const char *make(const double A[3], const double B[3], double a, Catenary3 &out);
  /** The catenary through A and B whose sag is `sag`: the vertical distance
   * from the chord's midpoint down to the curve, (A.z + B.z) / 2 - z(d / 2).
   * Solved by a fixed number of bisection steps on log a. */
  static const char *from_sag(const double A[3], const double B[3], double sag, Catenary3 &out);

  /** (A.z + B.z) / 2 - z(d / 2), evaluated. */
  double sag() const;

  void hash_into(hash::Hasher128 &h) const;
  bool equal(const Catenary3 &b) const;
  void write(io::ByteWriter &w) const;
  static bool read(io::ByteReader &r, Catenary3 &v, const attr::PoolReadLimits &);

  util::UniquePtr<Curve3> evaluator() const;
};

using Catenary3Pool = attr::InternPool<Catenary3>;

const KindOps &catenary3_kind();

} // namespace ubertopo::curves
