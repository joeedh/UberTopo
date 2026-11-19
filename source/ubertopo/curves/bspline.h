#pragma once

/* bspline: non-rational clamped B-splines in 2D or 3D (plan P4a §3.6).
 *
 * Either a primary definition (imported data) or an approximation cache of
 * another curve: then `source` names it (an inner ref), [src_t0, src_t1] is
 * the source range, the spline's parameter is the source parameter, and
 * max_error bounds the distance to the exact source.
 *
 * Canonical form (make): -0.0 -> +0.0 everywhere; without a source, the
 * source range and max_error are 0. Refused: non-finite values, degree
 * outside 1..7, unclamped or decreasing knots, interior multiplicity > p,
 * fewer than p + 1 control points. */

#include <cstdint>

#include "ubertopo/curves/curve.h"

namespace ubertopo::curves {

class BSpline {
public:
  static constexpr int kMaxDegree = 7;
  static constexpr uint32_t kMaxCount = 1u << 22;
  static constexpr bool has_refs = true;

  uint8_t dim = 3, degree = 3;
  util::Vector<double, 16> knots; /* count + degree + 1 */
  util::Vector<double, 48> ctrl;  /* count * dim */
  attr::PoolRef source;
  double src_t0 = 0, src_t1 = 0, max_error = 0;

  /* derived (make): derivative control points per order k = 1..min(p, 6),
   * same layout as ctrl (count - k points) */
  util::Vector<util::Vector<double, 48>, 6> dctrl;
  double max_abs[7] = {0, 0, 0, 0, 0, 0, 0}; /* max |coordinate| of order-k control points */
  double qerr[7] = {0, 0, 0, 0, 0, 0, 0};    /* rounding bound of order-k control points */

  int count() const
  {
    return ctrl.size() / int(dim);
  }
  double lo() const
  {
    return knots[degree];
  }
  double hi() const
  {
    return knots[count()];
  }

  static const char *make(int dim, int degree, const util::Vector<double, 16> &knots,
                          const util::Vector<double, 48> &ctrl, attr::PoolRef source, double src_t0, double src_t1,
                          double max_error, BSpline &out);

  /** Inserts knot u once (Boehm), strictly inside the domain and below
   * multiplicity p. The result is a primary spline (no source): the same
   * curve up to the rounding of the new control points. */
  static const char *insert_knot(const BSpline &b, double u, BSpline &out);

  /* pool member protocol */
  void hash_into(hash::Hasher128 &h) const;
  bool equal(const BSpline &b) const;
  void write(io::ByteWriter &w) const;
  static bool read(io::ByteReader &r, BSpline &v, const attr::PoolReadLimits &);
  template <typename F> void for_each_ref(F &&f) const
  {
    f(source);
  }
  template <typename F> void remap_refs(F &&f)
  {
    source = f(source);
  }

  util::UniquePtr<Curve2> evaluator2() const;
  util::UniquePtr<Curve3> evaluator3() const;
};

using BSplinePool = attr::InternPool<BSpline>;

const KindOps &bspline_kind();

} // namespace ubertopo::curves
