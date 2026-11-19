#pragma once

/* Generic curve algorithms over the evaluator interface (plan P4a §3.7):
 * certified boxes, flattening and quintic Hermite approximation, plus the
 * weak approximation cache.
 *
 * Each algorithm first splits [t0, t1] at the curve's special parameters
 * of continuity < kSmooth, so deriv_bound is only asked about ranges where
 * the curve is smooth.
 *
 * Interpolation bounds use the vector forms, without a sqrt(D) factor: the
 * Peano kernels of linear and of Hermite interpolation have one sign, so
 *   |r(t) - lerp(t)| <= (t - a)(b - t) / 2 * max|r''|         <= h^2 / 8 * M2
 *   |r(t) - H5(t)|   <= |(t - a)^3 (t - b)^3| / 6! * max|r^(6)| <= h^6 / (2^6 6!) * M6
 * hold for the Euclidean norm, not only per coordinate. */

#include <cstdint>

#include "ubertopo/curves/bspline.h"
#include "ubertopo/curves/curve.h"

namespace ubertopo::curves {

template <int D> struct Box {
  Vec<D> lo, hi;
};

/** A box containing the exact curve over [t0, t1] (rounded outward). */
template <int D> Box<D> bounds(const CurveEval<D> &c, double t0, double t1);

/** Sorted parameters, t0 and t1 included, whose polyline (through the
 * computed points) lies within tol of the exact curve. False, with *err set,
 * if tol is below the curve's evaluation error or the bisection limit is hit.
 * Parameters for tol / 4 are a superset of those for tol (nested LODs). */
template <int D>
bool flatten(const CurveEval<D> &c, double t0, double t1, double tol, util::Vector<double> &out,
             const char **err = nullptr);

/** Interns a degree-5 bspline approximating curve `src` over [t0, t1] within
 * tol (its max_error). The spline's parameter is the source parameter and
 * it names src as an inner ref. The store must be set up by
 * register_pools. kNoRef, with *err set, on failure. */
attr::PoolRef approximate(attr::PoolStore &store, attr::PoolRef src, double t0, double t1, double tol,
                          const char **err = nullptr);

/** The quintic Hermite basis norms Lambda_0..2 (rounded up): the sup on
 * [0, 1] of |H0| + |H5|, |H1| + |H4| and |H2| + |H3|. */
const double *hermite_lambdas();

namespace detail {
template <int D> struct BezierPiece {
  double a, b;
  Vec<D> B[6]; /* quintic Bezier control points on [a, b] */
  double err;  /* bound on |piece - exact source| over [a, b] */
};
/** The degree-5 spline through contiguous Bezier pieces, with interior knot
 * multiplicities mult[j] in 3..5 (P - 1 of them). A control point shared by
 * several pieces is the mean of their blossoms; moved[q] bounds how far that
 * moves piece q (rounding included), exact pieces of a spline of the knots'
 * continuity giving rounding only. Exposed for tests. */
template <int D>
void assemble(const util::Vector<BezierPiece<D>> &pieces, const util::Vector<int> &mult,
              util::Vector<double, 16> &knots, util::Vector<double, 48> &ctrl, util::Vector<double> &moved);
} // namespace detail

/** A weak map (source curve, range, tol) -> approximation. Pass it as an
 * extra root to collect and canonicalize: an approximation lives while its
 * source is reachable otherwise (a weak pair), and its entry is pruned when
 * the source goes. A lookup also checks the cached spline's content (source,
 * range, max_error <= tol), so a cache that missed a collect never returns a
 * reused slot. */
class ApproxCache final : public attr::RefRoot {
public:
  /** The cached approximation, or a new one (kNoRef and *err on failure). */
  attr::PoolRef get(attr::PoolStore &store, attr::PoolRef src, double t0, double t1, double tol,
                    const char **err = nullptr);
  attr::PoolRef find(const attr::PoolStore &store, attr::PoolRef src, double t0, double t1, double tol) const;
  int size() const
  {
    return entries_.size();
  }

  void visit_refs(attr::RefVisitor &) const override {}
  void visit_weak_pairs(attr::WeakPairVisitor &v) const override;
  void remap_refs(const attr::RefRemap &m) override;
  void prune(const attr::MarkQuery &q) override;

private:
  struct Entry {
    attr::PoolRef src, spline;
    double t0, t1, tol;
  };
  util::Vector<Entry> entries_;
};

} // namespace ubertopo::curves
