#pragma once

/* The curve kind interface (plan P4a §3.2).
 *
 * A curve kind is a pool value type (the definition, interned in a
 * PoolStore) plus a KindOps object that builds an evaluator for an entry.
 * Kinds are an open set: each registers its KindOps in kinds() (curve.cc),
 * appended in a fixed order, and pool ids follow that order (pool id =
 * index + 1) in every store set up by register_pools(). Dispatch is virtual,
 * per kind; nothing switches over the kinds.
 *
 * Every evaluator reports certified bounds:
 *  - eval_error(): |computed r(t) - exact r(t)| over the whole domain;
 *  - deriv_eval_error(k): the same for r^(k), k <= 2;
 *  - deriv_bound(t0, t1, k): max |r^(k)| over [t0, t1], for a range with no
 *    special parameter strictly inside (callers split there first); +inf
 *    where r is not C^k.
 * "Certified" = proved for exact arithmetic plus explicit rounding
 * allowances (poly.h), and checked against the quad oracle in the tests.
 *
 * An evaluator may point into its store's entries: it is valid until the
 * next intern, collect, canonicalize or assignment of that store. */

#include <cstdint>

#include "litestl/attr/pool_store.h"
#include "litestl/math/vector.h"
#include "litestl/util/memory.h"
#include "ubertopo/curves/fn1d.h"

namespace ubertopo::curves {

namespace attr = litestl::attr;

template <int D> using Vec = litestl::math::Vec<double, D>;

struct Interval {
  double lo, hi;
};

enum class ParamKind : uint8_t { ArcLength, Station, General };

/** Continuity at a special parameter: the curve is C^continuity there and
 * no smoother (a break: algorithms cut there). kSmooth marks a feature where
 * the curve is C-infinity (an inflection, a curvature extremum): a place,
 * not a break. */
inline constexpr uint8_t kSmooth = 255;
struct SpecialParam {
  double t;
  uint8_t continuity;
};

inline constexpr int kMaxEvalDeriv = 6;

template <int D> class CurveEval {
public:
  virtual ~CurveEval() = default;
  virtual Interval domain() const = 0;
  virtual ParamKind param_kind() const = 0;
  /** r, r', ..., r^(n) at t into out[0..n], n <= kMaxEvalDeriv; Side picks the
   * one-sided limit at a special parameter. */
  virtual void eval(double t, int n, Vec<D> *out, Side side = Side::Above) const = 0;
  virtual double eval_error() const = 0;
  virtual double deriv_eval_error(int k) const = 0;
  virtual double deriv_bound(double t0, double t1, int k) const = 0;
  /** Special parameters in [t0, t1] (ends included), sorted. */
  virtual void special_params(double t0, double t1, util::Vector<SpecialParam> &out) const = 0;
};

using Curve2 = CurveEval<2>;
using Curve3 = CurveEval<3>;

class KindOps {
public:
  virtual ~KindOps() = default;
  virtual const char *key() const = 0;
  /** Registers this kind's pool in s (in kinds() order). */
  virtual void add_pool(attr::PoolStore &s) const = 0;
  /** Dimension of entry r (2 or 3), 0 if r is not a live curve of this kind. */
  virtual int dim(const attr::PoolStore &s, attr::PoolRef r) const = 0;
  virtual util::UniquePtr<Curve2> make2(const attr::PoolStore &, attr::PoolRef) const
  {
    return {};
  }
  virtual util::UniquePtr<Curve3> make3(const attr::PoolStore &, attr::PoolRef) const
  {
    return {};
  }
};

/** The registered kinds, in pool-id order. */
util::span<const KindOps *const> kinds();
/** Registers every kind's pool in a fresh store, in order. */
void register_pools(attr::PoolStore &s);
/** The pool id of a registered kind in a store set up by register_pools. */
uint32_t pool_id(const KindOps &k);
/** The kind of r in a store set up by register_pools (nullptr for none). */
const KindOps *kind_of(attr::PoolRef r);
/** Dimension of the live curve r (0 for none, dead or not a curve). */
int curve_dim(const attr::PoolStore &s, attr::PoolRef r);
/** Evaluators for a live curve of the right dimension (null otherwise). */
util::UniquePtr<Curve2> curve2(const attr::PoolStore &s, attr::PoolRef r);
util::UniquePtr<Curve3> curve3(const attr::PoolStore &s, attr::PoolRef r);

} // namespace ubertopo::curves
