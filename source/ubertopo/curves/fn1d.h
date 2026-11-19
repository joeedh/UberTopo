#pragma once

/* Piecewise polynomials in station (plan P4a §3.1): lane widths w(s),
 * vertical profiles z(s).
 *
 * Piece i covers [breaks[i], breaks[i+1]] in the local variable
 * sigma = s - breaks[i]. Coefficients are exact data (Poly::e = 0).
 *
 * Canonical form (make()): trailing zero coefficients trimmed, -0.0 -> +0.0,
 * zero-length pieces dropped, at most kMaxDegree. Pieces are never merged
 * (merging would round shifted coefficients), so two functions built the
 * same way are bitwise equal, and the same function split differently is
 * not. Non-finite values and decreasing breaks are refused. */

#include <cstdint>

#include "litestl/io/bytes.h"
#include "litestl/util/hash128.h"
#include "ubertopo/curves/poly.h"

namespace ubertopo::curves {

namespace io = litestl::io;
namespace hash = litestl::hash;

/** One-sided limits at breakpoints and other special parameters. */
enum class Side : uint8_t { Below, Above };

class Fn1D {
public:
  static constexpr int kMaxDegree = 7;
  static constexpr uint32_t kMaxPieces = 1u << 20;

  util::Vector<double, 2> breaks; /* n + 1, strictly increasing */
  util::Vector<Poly, 1> pieces;   /* n */

  /** Validates and canonicalises; nullptr on success, else a reason. */
  static const char *make(const util::Vector<double, 2> &breaks, const util::Vector<Poly, 1> &pieces, Fn1D &out);
  static Fn1D constant(double v, double s0, double s1);
  static Fn1D linear(double v0, double v1, double s0, double s1);

  bool empty() const
  {
    return pieces.size() == 0;
  }
  double lo() const
  {
    return breaks[0];
  }
  double hi() const
  {
    return breaks[breaks.size() - 1];
  }
  /** True if every piece is the zero polynomial. */
  bool is_zero() const;

  /** Piece containing s; at a breakpoint, Side picks the piece. s is clamped
   * to the domain. */
  int piece_at(double s, Side side = Side::Above) const;
  /** The k-th derivative at s (k <= kMaxDegree + 1); *err bounds the rounding. */
  double eval(double s, int k = 0, Side side = Side::Above, double *err = nullptr) const;
  /** The k-th derivative's polynomial on piece i, in sigma. */
  Poly piece_deriv(int i, int k) const;
  /** Enclosure of the k-th derivative over [s0, s1] (within the domain). */
  Range range(double s0, double s1, int k = 0) const;

  /** Sum (breakpoints merged; coefficients shifted with rounding). Both must
   * cover the same domain. */
  static const char *add(const Fn1D &a, const Fn1D &b, Fn1D &out);

  /* pool member protocol helpers (bitwise) */
  void hash_into(hash::Hasher128 &h) const;
  bool equal(const Fn1D &b) const;
  void write(io::ByteWriter &w) const;
  /** Reads, then checks the result is canonical (make() reproduces it). */
  static bool read(io::ByteReader &r, Fn1D &out);
};

} // namespace ubertopo::curves
