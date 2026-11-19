#pragma once

/* road3: a ruled road surface in road coordinates (s, t) (plan P4b §3.2).
 *
 * S(s, t) = r(s) + t N(s) + Z(s, t) e_z, with r and N the reference cesaro2's
 * point and left normal, t a horizontal offset, and Z linear in t between
 * border heights: on link k (b_k(s) <= t <= b_(k+1)(s)),
 *   w = (t - b_k) / (b_(k+1) - b_k),  Z = (1 - w) Z_k(s) + w Z_(k+1)(s).
 * A border t = b_k(s) is the curve alignment3(offset2(reference, b_k), Z_k)
 * bit for bit (§3.2's three rules). The surface is C0 across borders (creases
 * in t) and stores no slopes: crossfall and superelevation live in the Z_k. */

#include <cstdint>

#include "ubertopo/curves/surface.h"

namespace ubertopo::curves {

class Road3 {
public:
  static constexpr bool has_refs = true;
  static constexpr int kMaxBorders = 64;

  attr::PoolRef reference;         /* a cesaro2 */
  util::Vector<Fn1D, 2> borders;   /* b_0 < ... < b_m, common domain */
  util::Vector<Fn1D, 2> heights;   /* Z_0 ... Z_m, same domain */

  int links() const
  {
    return borders.size() - 1;
  }

  /** Validates against the store: a live cesaro2 reference; 2..kMaxBorders
   * borders with as many heights; one common domain inside the reference's.
   * CLAUDENOTE: W3 adds the border-order and fold checks (plan §3.2). */
  static const char *make(const attr::PoolStore &s,
                          attr::PoolRef reference,
                          const util::Vector<Fn1D, 2> &borders,
                          const util::Vector<Fn1D, 2> &heights,
                          Road3 &out);

  void hash_into(hash::Hasher128 &h) const;
  bool equal(const Road3 &b) const;
  void write(io::ByteWriter &w) const;
  static bool read(io::ByteReader &r, Road3 &v, const attr::PoolReadLimits &);
  template <typename F> void for_each_ref(F &&f) const
  {
    f(reference);
  }
  template <typename F> void remap_refs(F &&f)
  {
    reference = f(reference);
  }
};

using Road3Pool = attr::InternPool<Road3>;

const KindOps &road3_kind();

} // namespace ubertopo::curves
