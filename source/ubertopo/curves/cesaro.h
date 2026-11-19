#pragma once

/* cesaro2: planar curves defined by curvature (plan P4a §3.3).
 *
 * A start pose (x0, y0, theta0) and segments {L_i, kappa_i(sigma)}, kappa_i a
 * polynomial of degree <= 3 in the local arc length sigma in [0, L_i]. The
 * segments chain (each starts at the previous end pose); the heading is
 * continuous and kappa may jump at a join. Parameter: arc length s in
 * [0, sum L_i]. Lines, arcs, clothoids, cubic spirals and Bloss transitions
 * are all cesaro2 curves.
 *
 * Definition and derived data:
 *  - hash_into, equal and write cover the definition only (bitwise). read
 *    re-runs make() and refuses a definition that isn't canonical.
 *  - make() canonicalises (-0.0 -> +0.0, theta0 reduced to (-pi, pi],
 *    zero-length segments dropped, kappa trimmed) and builds the pieces.
 *
 * Pieces: each segment is bisected into pieces whose phase variation
 * Q(u) = theta(m + u) - theta(m), |u| <= r, is small. On each piece the
 * position is a degree-31 polynomial in u, from the Taylor series of
 * e^{iQ(u)} integrated term by term. Each piece carries a certified bound on
 * its position error (Taylor tail by the majorant series and a Cauchy
 * estimate, coefficient and recurrence rounding, Horner rounding, rotation).
 * Bisection continues until the terms it reduces faster than h (tail,
 * coefficient uncertainty, recurrence rounding) are at most kPieceTol * h;
 * the terms proportional to h (heading uncertainty, Horner and rotation
 * rounding) stay in the bound. Piece start points are chained in
 * double-double. */

#include <cstdint>

#include "ubertopo/curves/curve.h"

namespace ubertopo::curves {

struct CesaroSegment {
  double length;
  Poly kappa; /* exact, degree <= 3, in sigma */
};

struct CesaroPiece {
  double sa, sb;     /* local sigma range within the segment */
  double m, r;       /* midpoint and half-length */
  double sx, sy;     /* start point (double-double hi parts) */
  double cos_m, sin_m; /* rotation e^{i theta(m)} */
  double err;        /* certified bound on the position error inside the piece, start included */
  util::Vector<double, 32> px, py; /* position polynomial in u (low degree first), before rotation */
};

class Cesaro2 {
public:
  static constexpr int kMaxKappaDegree = 3;
  static constexpr int kTaylorOrder = 30;
  static constexpr double kPieceTol = 0x1p-47; /* per-piece error target, relative to its length */
  static constexpr double kMaxBend = 0.5;      /* majorant Q^(r) per piece */
  static constexpr uint32_t kMaxPieces = 1u << 20;
  static constexpr bool has_refs = false;

  /* definition */
  double x0 = 0, y0 = 0, theta0 = 0;
  util::Vector<CesaroSegment, 4> segs;

  /* derived (make) */
  util::Vector<double, 4> seg_start;  /* s at each segment start (n + 1 entries) */
  util::Vector<Poly, 4> theta;        /* theta_i(sigma), with error bounds */
  util::Vector<uint32_t, 4> seg_piece; /* first piece of each segment (n + 1 entries) */
  util::Vector<CesaroPiece, 8> pieces;
  /* per segment, P_k = A_k + i B_k for k = 0..5, r^(k+1) = P_k e^{i theta} */
  util::Vector<Poly, 24> pa, pb;
  util::Vector<uint8_t, 4> join_cont; /* continuity at the joins (n - 1 entries) */
  double eval_err = 0;                /* whole curve */
  double deriv_err[3] = {0, 0, 0};    /* r, r', r'' (r'' bound also covers kappa uncertainty) */

  /** Validates, canonicalises and builds the derived data. nullptr on
   * success, else a reason (refuses non-finite values, negative lengths,
   * kappa of degree > 3, turning beyond the trig domain, too many pieces). */
  static const char *make(double x0, double y0, double theta0, const util::Vector<CesaroSegment, 4> &segs,
                          Cesaro2 &out);
  /* convenience constructors */
  static const char *line(double x0, double y0, double theta0, double length, Cesaro2 &out);
  static const char *arc(double x0, double y0, double theta0, double kappa, double length, Cesaro2 &out);
  static const char *clothoid(double x0, double y0, double theta0, double k0, double k1, double length, Cesaro2 &out);

  double length() const
  {
    return seg_start[seg_start.size() - 1];
  }

  /* pool member protocol (definition only) */
  void hash_into(hash::Hasher128 &h) const;
  bool equal(const Cesaro2 &b) const;
  void write(io::ByteWriter &w) const;
  static bool read(io::ByteReader &r, Cesaro2 &v, const attr::PoolReadLimits &);

  /** The evaluator (valid while this entry is). */
  util::UniquePtr<Curve2> evaluator() const;
};

using Cesaro2Pool = attr::InternPool<Cesaro2>;

/** The cesaro2 kind's registry entry. */
const KindOps &cesaro2_kind();

} // namespace ubertopo::curves
