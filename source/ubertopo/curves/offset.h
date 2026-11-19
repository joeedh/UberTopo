#pragma once

/* offset2: a lateral offset of a cesaro2 reference line (plan P4a §3.4).
 *
 * r_w(s) = r(s) + w(s) N(s), N = i e^{i theta}: the base point moved by w(s)
 * along the base's normal (road semantics: every lane edge is measured from
 * the reference line, so widths compose by adding w). The parameter is the
 * base station, over w's domain. Evaluated from the exact base.
 *
 * Derivatives: r_w^(k) = Q_(k-1) e^{i theta}, Q_0 = 1 - w kappa + i w',
 * Q_(j+1) = Q_j' + i kappa Q_j. The view builds the Q_j as polynomials on
 * each interval where both w and kappa are single polynomials.
 *
 * Continuity: at a w break, w's; at a base join of continuity C^k, C^(k-1)
 * (a kappa jump changes |r_w'|). Folds, the roots of 1 - w kappa, are
 * special parameters; offset_valid() reports whether 1 - w kappa > 0. */

#include <cstdint>

#include "ubertopo/curves/cesaro.h"
#include "ubertopo/curves/curve.h"

namespace ubertopo::curves {

class Offset2 {
public:
  static constexpr bool has_refs = true;

  attr::PoolRef base; /* a cesaro2 */
  Fn1D w;

  /** Validates against the store: base must be a live cesaro2 and w's
   * domain inside the base's; w must be continuous (no provable jump). */
  static const char *make(const attr::PoolStore &s, attr::PoolRef base, const Fn1D &w, Offset2 &out);

  void hash_into(hash::Hasher128 &h) const;
  bool equal(const Offset2 &b) const;
  void write(io::ByteWriter &w) const;
  static bool read(io::ByteReader &r, Offset2 &v, const attr::PoolReadLimits &);
  template <typename F> void for_each_ref(F &&f) const
  {
    f(base);
  }
  template <typename F> void remap_refs(F &&f)
  {
    base = f(base);
  }
};

using Offset2Pool = attr::InternPool<Offset2>;

/** The offset view, with offset_valid. Points into the store's entries. */
class Offset2View final : public Curve2 {
public:
  Offset2View(const Cesaro2 *base, const Offset2 *off);

  Interval domain() const override;
  ParamKind param_kind() const override
  {
    return ParamKind::Station;
  }
  void eval(double t, int n, Vec<2> *out, Side side = Side::Above) const override;
  double eval_error() const override
  {
    return e_[0];
  }
  double deriv_eval_error(int k) const override
  {
    return k <= 2 ? e_[k] : INFINITY;
  }
  double deriv_bound(double t0, double t1, int k) const override;
  void special_params(double t0, double t1, util::Vector<SpecialParam> &out) const override;

  /** 1 - w kappa > 0 throughout [t0, t1] (no fold, no reversal). */
  bool offset_valid(double t0, double t1) const;

  static constexpr int kQ = 6; /* Q_0..Q_5: r_w' .. r_w^(6) */

private:
  struct Span {
    double s0, s1;
    uint8_t cont0; /* continuity at s0 (a break between spans) */
    Poly A[kQ], B[kQ]; /* Q_j = A_j + i B_j in u = s - s0 */
  };
  int span_at(double t, Side side) const;

  const Cesaro2 &b_;
  const Offset2 &o_;
  util::UniquePtr<Curve2> base_;
  util::Vector<Span> spans_;
  double e_[3] = {0, 0, 0};
};

/** Interns the offset (base, w); returns base itself when w is zero over the
 * base's whole domain. kNoRef and *err on failure. */
attr::PoolRef intern_offset(attr::PoolStore &s, attr::PoolRef base, const Fn1D &w, const char **err = nullptr);

const KindOps &offset2_kind();

} // namespace ubertopo::curves
