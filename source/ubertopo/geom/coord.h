#pragma once

/* Coordinate types of the geometry core (design 02 §2-3, DEC-007).
 *
 * The coordinate scalar is a template parameter, constrained by CoordType.
 * CoordTraits<T> is the customisation point: point types, exact primitive
 * predicates (sign of a determinant: -1, 0, +1) and conversions.
 *
 *   double  — adaptive exact predicates on stored values (no tolerance).
 *   float   — promoted to double, then adaptive (tests, small fixtures).
 *   Lattice — int64 fixed point, value = raw * 2^quantum_log2 in the tile
 *             frame; exact integer determinants; |raw| <= 2^26.
 *
 * Predicate conventions are litestl's (Shewchuk): orient2d > 0 iff a, b, c
 * are counter-clockwise; orient3d(a,b,c,d) > 0 iff d is below the plane of
 * counter-clockwise a, b, c. vol3() gives the right-handed volume sign
 * sign(det[b-a, c-a, d-a]) = -orient3d(a,b,c,d). */

#include <cmath>
#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>

#include "litestl/math/predicates.h"
#include "litestl/math/vector.h"

namespace ubertopo::geom {

namespace lmath = litestl::math;

/** int64 fixed-point coordinate. A strong type: no implicit conversions and no
 * multiplication of two lattice values (products exist only inside predicates). */
struct Lattice {
  int64_t raw = 0;

  constexpr Lattice() = default;
  constexpr explicit Lattice(int64_t r) : raw(r) {}

  friend constexpr Lattice operator+(Lattice a, Lattice b)
  {
    return Lattice(a.raw + b.raw);
  }
  friend constexpr Lattice operator-(Lattice a, Lattice b)
  {
    return Lattice(a.raw - b.raw);
  }
  friend constexpr Lattice operator-(Lattice a)
  {
    return Lattice(-a.raw);
  }
  constexpr Lattice &operator+=(Lattice b)
  {
    raw += b.raw;
    return *this;
  }
  constexpr Lattice &operator-=(Lattice b)
  {
    raw -= b.raw;
    return *this;
  }
  friend constexpr bool operator==(Lattice a, Lattice b) = default;
  friend constexpr std::strong_ordering operator<=>(Lattice a, Lattice b) = default;
};
static_assert(sizeof(Lattice) == 8 && std::has_unique_object_representations_v<Lattice>);

template <typename T> struct CoordTraits; /* primary undefined: unsupported types fail to compile */

template <typename T>
concept CoordScalar = requires(T a, T b) {
  { a + b } -> std::convertible_to<T>;
  { a - b } -> std::convertible_to<T>;
  { -a } -> std::convertible_to<T>;
  { a < b } -> std::convertible_to<bool>;
  { a == b } -> std::convertible_to<bool>;
};

template <typename T>
concept CoordType = CoordScalar<T> && requires {
  typename CoordTraits<T>::Point;
  typename CoordTraits<T>::Point2;
  { CoordTraits<T>::is_exact } -> std::convertible_to<bool>;
  { CoordTraits<T>::to_double(std::declval<T>()) } -> std::same_as<double>;
  { CoordTraits<T>::from_int(int64_t{}) } -> std::same_as<T>;
} && requires(const typename CoordTraits<T>::Point &p, const typename CoordTraits<T>::Point2 &q) {
  { CoordTraits<T>::orient2d(q, q, q) } -> std::same_as<int>;
  { CoordTraits<T>::orient3d(p, p, p, p) } -> std::same_as<int>;
  { CoordTraits<T>::orient3d_ext(p, p, p, p) } -> std::same_as<int>;
  { CoordTraits<T>::incircle(q, q, q, q) } -> std::same_as<int>;
  { CoordTraits<T>::incircle_sos(q, q, q, q, uint64_t{}, uint64_t{}, uint64_t{}, uint64_t{}) } -> std::same_as<int>;
  { CoordTraits<T>::compare(p, p, int{}) } -> std::same_as<int>;
  { CoordTraits<T>::ext_ok(p) } -> std::same_as<bool>;
} && requires(T a, T &out) {
  { CoordTraits<T>::from_int_pow2(int64_t{}, int{}, out) } -> std::same_as<bool>;
  { CoordTraits<T>::add_exact(a, a, out) } -> std::same_as<bool>;
};

/** Coordinates whose additions are exact (constructions need no rounding). */
template <typename T>
concept ExactCoord = CoordType<T> && CoordTraits<T>::is_exact;

namespace detail {
template <typename T> constexpr int cmp(T a, T b)
{
  return a < b ? -1 : (b < a ? 1 : 0);
}
} // namespace detail

template <> struct CoordTraits<double> {
  using Point = lmath::double3;
  using Point2 = lmath::double2;
  static constexpr bool is_exact = false;
  static constexpr const char *key = "f64";

  static double to_double(double v)
  {
    return v;
  }
  /** Exact for |v| < 2^53. */
  static double from_int(int64_t v)
  {
    return double(v);
  }
  static int orient2d(const Point2 &a, const Point2 &b, const Point2 &c)
  {
    return lmath::pred::orient2d(a, b, c);
  }
  static int orient3d(const Point &a, const Point &b, const Point &c, const Point &d)
  {
    return lmath::pred::orient3d(a, b, c, d);
  }
  /** orient3d for points constructed by derived predicates (far ray points,
   * scaled test points); the adaptive predicate has no tighter range. */
  static int orient3d_ext(const Point &a, const Point &b, const Point &c, const Point &d)
  {
    return lmath::pred::orient3d(a, b, c, d);
  }
  static int incircle(const Point2 &a, const Point2 &b, const Point2 &c, const Point2 &d)
  {
    return lmath::pred::incircle(a, b, c, d);
  }
  static int incircle_sos(const Point2 &a,
                          const Point2 &b,
                          const Point2 &c,
                          const Point2 &d,
                          uint64_t ra,
                          uint64_t rb,
                          uint64_t rc,
                          uint64_t rd)
  {
    return lmath::pred::incircle_sos(a, b, c, d, ra, rb, rc, rd);
  }
  static int compare(const Point &a, const Point &b, int axis)
  {
    return detail::cmp(a[axis], b[axis]);
  }
  /** d * 2^k (correctly rounded). */
  static bool from_int_pow2(int64_t d, int k, double &out)
  {
    out = double(std::ldexp(double(d), k));
    return std::isfinite(out);
  }
  static bool ext_ok(const Point &p)
  {
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
  }
  /** a + b, false unless the sum is exact (TwoSum error term is zero). */
  static bool add_exact(double a, double b, double &out)
  {
    const double s = a + b;
    const double bb = s - a;
    const double err = (a - (s - bb)) + (b - bb);
    out = s;
    return err == double(0) && std::isfinite(s);
  }
};

template <> struct CoordTraits<float> {
  using Point = lmath::float3;
  using Point2 = lmath::float2;
  static constexpr bool is_exact = false;
  static constexpr const char *key = "f32";

  static double to_double(float v)
  {
    return double(v);
  }
  /** Exact for |v| < 2^24. */
  static float from_int(int64_t v)
  {
    return float(v);
  }
  static int orient2d(const Point2 &a, const Point2 &b, const Point2 &c)
  {
    return lmath::pred::orient2d(a, b, c);
  }
  static int orient3d(const Point &a, const Point &b, const Point &c, const Point &d)
  {
    return lmath::pred::orient3d(a, b, c, d);
  }
  static int orient3d_ext(const Point &a, const Point &b, const Point &c, const Point &d)
  {
    return lmath::pred::orient3d(a, b, c, d);
  }
  static int incircle(const Point2 &a, const Point2 &b, const Point2 &c, const Point2 &d)
  {
    return lmath::pred::incircle(a, b, c, d);
  }
  static int incircle_sos(const Point2 &a,
                          const Point2 &b,
                          const Point2 &c,
                          const Point2 &d,
                          uint64_t ra,
                          uint64_t rb,
                          uint64_t rc,
                          uint64_t rd)
  {
    return lmath::pred::incircle_sos(a, b, c, d, ra, rb, rc, rd);
  }
  static int compare(const Point &a, const Point &b, int axis)
  {
    return detail::cmp(a[axis], b[axis]);
  }
  /** d * 2^k (correctly rounded). */
  static bool from_int_pow2(int64_t d, int k, float &out)
  {
    out = float(std::ldexp(double(d), k));
    return std::isfinite(out);
  }
  static bool ext_ok(const Point &p)
  {
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
  }
  /** a + b, false unless the sum is exact (TwoSum error term is zero). */
  static bool add_exact(float a, float b, float &out)
  {
    const float s = a + b;
    const float bb = s - a;
    const float err = (a - (s - bb)) + (b - bb);
    out = s;
    return err == float(0) && std::isfinite(s);
  }
};

template <> struct CoordTraits<Lattice> {
  using Point = lmath::Vec<Lattice, 3>;
  using Point2 = lmath::Vec<Lattice, 2>;
  static constexpr bool is_exact = true;
  static constexpr const char *key = "lat";
  /** Stored coordinates: |raw| <= 2^input_bits (design 02 §3.1). */
  static constexpr int input_bits = 26;
  /** Range of orient3d_ext: stored points scaled by up to 4 (interior test
   * points) plus a far ray point stay within 2^30 (plan P2 W2). */
  static constexpr int ext_bits = 30;
  static_assert(2 * ext_bits + 3 <= 63 && 3 * ext_bits + 7 <= 127, "ext_bits exceeds litestl orient3d");

  static double to_double(Lattice v)
  {
    return double(v.raw);
  }
  static Lattice from_int(int64_t v)
  {
    return Lattice(v);
  }
  static lmath::int64_2 raw(const Point2 &p)
  {
    return lmath::int64_2(p[0].raw, p[1].raw);
  }
  static lmath::int64_3 raw(const Point &p)
  {
    return lmath::int64_3(p[0].raw, p[1].raw, p[2].raw);
  }
  static int orient2d(const Point2 &a, const Point2 &b, const Point2 &c)
  {
    return lmath::pred::orient2d<input_bits>(raw(a), raw(b), raw(c));
  }
  static int orient3d(const Point &a, const Point &b, const Point &c, const Point &d)
  {
    return lmath::pred::orient3d<input_bits>(raw(a), raw(b), raw(c), raw(d));
  }
  static int orient3d_ext(const Point &a, const Point &b, const Point &c, const Point &d)
  {
    return lmath::pred::orient3d<ext_bits>(raw(a), raw(b), raw(c), raw(d));
  }
  static int incircle(const Point2 &a, const Point2 &b, const Point2 &c, const Point2 &d)
  {
    return lmath::pred::incircle<input_bits>(raw(a), raw(b), raw(c), raw(d));
  }
  static int incircle_sos(const Point2 &a,
                          const Point2 &b,
                          const Point2 &c,
                          const Point2 &d,
                          uint64_t ra,
                          uint64_t rb,
                          uint64_t rc,
                          uint64_t rd)
  {
    return lmath::pred::incircle_sos<input_bits>(raw(a), raw(b), raw(c), raw(d), ra, rb, rc, rd);
  }
  static int compare(const Point &a, const Point &b, int axis)
  {
    return detail::cmp(a[axis].raw, b[axis].raw);
  }
  /** d * 2^k as a coordinate; false if it would leave the orient3d_ext range. */
  static bool from_int_pow2(int64_t d, int k, Lattice &out)
  {
    const int64_t lim = int64_t(1) << ext_bits;
    if (k < 0 || k > ext_bits || (d > (lim >> k)) || (d < -(lim >> k))) {
      return false;
    }
    out = Lattice(d * (int64_t(1) << k));
    return true;
  }
  static bool ext_ok(const Point &p)
  {
    const int64_t lim = int64_t(1) << ext_bits;
    for (int i = 0; i < 3; i++) {
      if (p[i].raw > lim || p[i].raw < -lim) {
        return false;
      }
    }
    return true;
  }
  /** a + b; Lattice addition is exact (callers keep values in range). */
  static bool add_exact(Lattice a, Lattice b, Lattice &out)
  {
    out = a + b;
    return true;
  }
};

static_assert(CoordType<double> && CoordType<float> && CoordType<Lattice>);
static_assert(ExactCoord<Lattice> && !ExactCoord<double>);

template <CoordType C> using Point = typename CoordTraits<C>::Point;
template <CoordType C> using Point2 = typename CoordTraits<C>::Point2;

/** Lexicographic (x, y, z) comparison: -1, 0, +1. */
template <CoordType C> int lex_compare(const Point<C> &a, const Point<C> &b)
{
  for (int i = 0; i < 3; i++) {
    if (const int c = CoordTraits<C>::compare(a, b, i)) {
      return c;
    }
  }
  return 0;
}

template <CoordType C> lmath::double3 to_double3(const Point<C> &p)
{
  using T = CoordTraits<C>;
  return lmath::double3(T::to_double(p[0]), T::to_double(p[1]), T::to_double(p[2]));
}

/* ---------------------------------------------------------------------- */

/** One tile frame per complex (design 02 §6): positions are tile-local.
 * Lattice values are raw * 2^quantum_log2 metres; the quantum is a power of
 * two so Lattice <-> double is exact for in-range values. */
struct TileFrame {
  lmath::double3 origin;  /* site-frame metres; a multiple of the site grid step */
  int quantum_log2 = -13; /* 2^-13 m ~ 0.12 mm */
  uint64_t frame_id = 0;

  double quantum() const
  {
    return std::ldexp(1.0, quantum_log2);
  }

  static constexpr int64_t kLatticeLimit = int64_t(1) << CoordTraits<Lattice>::input_bits;

  /** Tile-local metres -> lattice, rounding to the nearest quantum (ties away
   * from zero). False if a coordinate is non-finite or outside +-2^26 quanta. */
  bool to_lattice(const lmath::double3 &local, Point<Lattice> &out) const
  {
    for (int i = 0; i < 3; i++) {
      const double q = std::round(std::ldexp(local[i], -quantum_log2));
      if (!std::isfinite(q) || std::fabs(q) > double(kLatticeLimit)) {
        return false;
      }
      out[i] = Lattice(int64_t(q));
    }
    return true;
  }

  /** Lattice -> tile-local metres (exact: power-of-two quantum). */
  lmath::double3 to_local(const Point<Lattice> &p) const
  {
    return lmath::double3(std::ldexp(double(p[0].raw), quantum_log2),
                          std::ldexp(double(p[1].raw), quantum_log2),
                          std::ldexp(double(p[2].raw), quantum_log2));
  }

  /** True if every coordinate is within the stored lattice range. */
  static bool in_range(const Point<Lattice> &p)
  {
    for (int i = 0; i < 3; i++) {
      if (p[i].raw > kLatticeLimit || p[i].raw < -kLatticeLimit) {
        return false;
      }
    }
    return true;
  }
};

} // namespace ubertopo::geom
