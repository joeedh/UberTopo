#pragma once

/* polyline3: a 3D polyline (plan P4a §3.6). The parameter is the vertex
 * index, t in [0, n - 1]; each span is linear. Interior vertices are special
 * parameters of continuity C0.
 *
 * Canonical form (make): -0.0 -> +0.0. Refused: fewer than 2 points,
 * non-finite coordinates, repeated consecutive points (a zero-length span has
 * no tangent), more than kMaxPoints. */

#include <cstdint>

#include "ubertopo/curves/curve.h"

namespace ubertopo::curves {

class Polyline3 {
public:
  static constexpr uint32_t kMaxPoints = 1u << 22;
  static constexpr bool has_refs = false;

  util::Vector<double, 12> pts; /* x, y, z per point */

  int count() const
  {
    return pts.size() / 3;
  }
  Vec<3> point(int i) const
  {
    return Vec<3>(pts[3 * i], pts[3 * i + 1], pts[3 * i + 2]);
  }

  static const char *make(const util::Vector<double, 12> &pts, Polyline3 &out);

  void hash_into(hash::Hasher128 &h) const;
  bool equal(const Polyline3 &b) const;
  void write(io::ByteWriter &w) const;
  static bool read(io::ByteReader &r, Polyline3 &v, const attr::PoolReadLimits &);

  util::UniquePtr<Curve3> evaluator() const;
};

using Polyline3Pool = attr::InternPool<Polyline3>;

const KindOps &polyline3_kind();

} // namespace ubertopo::curves
