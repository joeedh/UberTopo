#pragma once

/* Polynomials with certified rounding bounds (plan P4a §3.1).
 *
 * A Poly holds coefficients c_i of c_0 + c_1 x + ... and, per coefficient, a
 * bound e_i on the distance between the stored c_i and the exact coefficient
 * of the polynomial it stands for. Exact input data has e = 0. Every
 * operation propagates the bounds and adds its own rounding (standard
 * gamma_n = n u / (1 - n u) bounds, u = 2^-53), so ranges and evaluations
 * derived from a Poly are enclosures of the exact polynomial's values.
 *
 * Ranges use the Bernstein form on the interval (convex hull property),
 * optionally after subdividing it. Roots are isolated by ranges of the
 * polynomial and its derivative, and refined by bisection, so they are a
 * deterministic function of the coefficients. */

#include <cstdint>

#include "litestl/util/vector.h"

namespace ubertopo::curves {

namespace util = litestl::util;

inline constexpr double kUnit = 0x1p-53;

/** gamma_n = n u / (1 - n u). */
inline double gamma_n(int n)
{
  const double nu = double(n) * kUnit;
  return nu / (1.0 - nu);
}

/** A non-negative bound rounded outward (covers the rounding of computing it). */
inline double up(double x)
{
  return x + x * 0x1p-50 + 0x1p-1060;
}

struct Range {
  double lo, hi;
  double abs_max() const
  {
    return lo < 0 ? (hi > -lo ? hi : -lo) : hi;
  }
  bool contains_zero() const
  {
    return lo <= 0 && hi >= 0;
  }
};

struct Root {
  double t;
  uint8_t mult; /* 1: simple; 2: multiple or not resolved within the tolerance */
};

class Poly {
public:
  util::Vector<double, 8> c; /* coefficients, low degree first */
  util::Vector<double, 8> e; /* per-coefficient error bounds, same length */

  Poly() = default;
  /** Exact coefficients. */
  static Poly exact(std::initializer_list<double> cs);
  static Poly exact(const double *cs, int n);
  static Poly constant(double v, double err = 0);

  int size() const
  {
    return c.size();
  }
  /** -1 for the zero polynomial. */
  int degree() const
  {
    return c.size() - 1;
  }
  bool exact_data() const;

  /** Horner value; *err (if given) bounds |value - exact value at x|. */
  double eval(double x, double *err = nullptr) const;

  Poly deriv() const;
  /** Antiderivative with constant term k (exact). */
  Poly integral(double k = 0) const;
  Poly operator+(const Poly &b) const;
  Poly operator-(const Poly &b) const;
  Poly operator*(const Poly &b) const;
  Poly scaled(double s) const;
  /** q(u) = p(a + h u). */
  Poly compose_affine(double a, double h) const;

  /** Drops trailing coefficients that are exactly zero with zero error and
   * turns -0.0 into +0.0. */
  void canonicalize();

  /** Certified enclosure of the exact polynomial over [a, b] (a <= b), from
   * the Bernstein form on 2^subdiv equal parts. */
  Range range(double a, double b, int subdiv = 0) const;

  /** Roots of the exact polynomial in [a, b], sorted. An isolating interval
   * that shrinks below tol without resolving reports its midpoint with
   * mult = 2 (a multiple root, or roots closer than tol). Simple roots are
   * bisected to full precision. tol = 0 picks 2^-26 max(1, |a|, |b|), the
   * scale at which a double root stops being resolvable. */
  void roots(double a, double b, util::Vector<Root> &out, double tol = 0) const;
};

} // namespace ubertopo::curves
