#pragma once

/* alignment3: a horizontal curve times a vertical profile (plan P4a §3.5).
 *
 * r(s) = (h(s), z(s)), with h a cesaro2 or offset2 and z an Fn1D over the
 * horizontal's parameter (the station). The 3D carrier of road and rail
 * centrelines and lane edges. Derivative bounds combine as
 * sqrt(bound_h^2 + bound_z^2); special parameters are the union, at the
 * lower continuity. */

#include <cstdint>

#include "ubertopo/curves/curve.h"

namespace ubertopo::curves {

class Alignment3 {
public:
  static constexpr bool has_refs = true;

  attr::PoolRef horizontal; /* a cesaro2 or offset2 */
  Fn1D z;

  /** Validates against the store: the horizontal is a live cesaro2 or
   * offset2, z's domain lies inside its domain and z has no provable jump. */
  static const char *make(const attr::PoolStore &s, attr::PoolRef horizontal, const Fn1D &z, Alignment3 &out);

  void hash_into(hash::Hasher128 &h) const;
  bool equal(const Alignment3 &b) const;
  void write(io::ByteWriter &w) const;
  static bool read(io::ByteReader &r, Alignment3 &v, const attr::PoolReadLimits &);
  template <typename F> void for_each_ref(F &&f) const
  {
    f(horizontal);
  }
  template <typename F> void remap_refs(F &&f)
  {
    horizontal = f(horizontal);
  }
};

using Alignment3Pool = attr::InternPool<Alignment3>;

const KindOps &alignment3_kind();

} // namespace ubertopo::curves
