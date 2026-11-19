#pragma once

/* The surface kind interface (plan P4b §3.2).
 *
 * A surface kind is a pool value type plus a SurfaceKindOps (a KindOps whose
 * as_surface() returns itself) that builds an evaluator for an entry. The
 * parameters are (u, v); for road3 they are road coordinates (s, t).
 *
 * Every evaluator reports certified bounds, as curves do (curve.h):
 *  - eval_error(): |computed S - exact S| anywhere in the domain;
 *  - deriv_eval_error(k): the same for every partial of order k <= 2;
 *  - deriv_bound(box, i, j): max |d^(i+j) S / du^i dv^j| over the box. A
 *    kind with creases in v (road3's borders) extends one smooth piece over
 *    the whole box, so a box that touches a crease is still covered for the
 *    piece the caller names (side_v).
 *
 * An evaluator may point into its store's entries: it is valid until the
 * next intern, collect, canonicalize or assignment of that store. */

#include "ubertopo/curves/curve.h"

namespace ubertopo::curves {

struct ParamBox {
  Interval u, v;
};

class SurfaceEval {
public:
  virtual ~SurfaceEval() = default;
  /** The u range (station range for road3). */
  virtual Interval domain_u() const = 0;
  /** The v range at u (one-sided at u breaks). */
  virtual Interval domain_v(double u, Side su = Side::Above) const = 0;
  /** S and its partials at (u, v): out[0] = S; n >= 1 adds S_u, S_v; n >= 2
   * adds S_uu, S_uv, S_vv (n <= 2). The sides pick one-sided limits at
   * breaks in u and creases in v. */
  virtual void eval(double u, double v, int n, Vec<3> *out, Side su = Side::Above, Side sv = Side::Above) const = 0;
  virtual double eval_error() const = 0;
  virtual double deriv_eval_error(int k) const = 0;
  virtual double deriv_bound(const ParamBox &box, int i, int j, Side sv = Side::Above) const = 0;
  /** Breaks in u within [u0, u1], sorted (as CurveEval::special_params). */
  virtual void special_u(double u0, double u1, util::Vector<SpecialParam> &out) const = 0;
};

class SurfaceKindOps : public KindOps {
public:
  int dim(const attr::PoolStore &, attr::PoolRef) const override
  {
    return 0; /* not a curve */
  }
  const SurfaceKindOps *as_surface() const override
  {
    return this;
  }
  /** True if r is a live, well-formed entry of this kind. */
  virtual bool valid(const attr::PoolStore &s, attr::PoolRef r) const = 0;
  /** The evaluator (null if r isn't valid or the kind has none yet). */
  virtual util::UniquePtr<SurfaceEval> make_surface(const attr::PoolStore &s, attr::PoolRef r) const = 0;
};

/** The kind of a live, valid surface entry (null otherwise). */
const SurfaceKindOps *surface_kind_of(const attr::PoolStore &s, attr::PoolRef r);
/** An evaluator for a live, valid surface (null otherwise). */
util::UniquePtr<SurfaceEval> surface(const attr::PoolStore &s, attr::PoolRef r);

} // namespace ubertopo::curves
