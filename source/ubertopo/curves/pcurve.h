#pragma once

/* Pcurves: curves in a surface's (u, v) parameter space (plan P4b §3.3).
 *
 * A coedge's pcurve shares its edge's parameter ("same parameter"):
 *  - graph2 {f}: (lambda, f(lambda)), lambda over f's domain. On a curved
 *    edge whose carrier is parameterised by station (road borders);
 *  - segment2 {p0, p1}: p0 + lambda (p1 - p0), lambda in [0, 1]. On a straight
 *    edge, lambda running from vs[0] to vs[1].
 *
 * Both are Curve2 kinds with certified bounds; they are accepted only in
 * .c.pcurve, never as edge carriers. */

#include <cstdint>

#include "ubertopo/curves/curve.h"

namespace ubertopo::curves {

class Graph2 {
public:
  static constexpr bool has_refs = false;

  Fn1D f;

  /** Refuses an empty f. */
  static const char *make(const Fn1D &f, Graph2 &out);

  void hash_into(hash::Hasher128 &h) const;
  bool equal(const Graph2 &b) const;
  void write(io::ByteWriter &w) const;
  static bool read(io::ByteReader &r, Graph2 &v, const attr::PoolReadLimits &);
  util::UniquePtr<Curve2> evaluator() const;
};

class Segment2 {
public:
  static constexpr bool has_refs = false;

  double p[4] = {0, 0, 0, 0}; /* p0.u, p0.v, p1.u, p1.v */

  /** Canonical form: -0.0 -> +0.0. Refuses non-finite or equal endpoints. */
  static const char *make(double u0, double v0, double u1, double v1, Segment2 &out);

  void hash_into(hash::Hasher128 &h) const;
  bool equal(const Segment2 &b) const;
  void write(io::ByteWriter &w) const;
  static bool read(io::ByteReader &r, Segment2 &v, const attr::PoolReadLimits &);
  util::UniquePtr<Curve2> evaluator() const;
};

using Graph2Pool = attr::InternPool<Graph2>;
using Segment2Pool = attr::InternPool<Segment2>;

const KindOps &graph2_kind();
const KindOps &segment2_kind();

} // namespace ubertopo::curves
