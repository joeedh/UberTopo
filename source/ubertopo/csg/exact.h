#pragma once

/* Exact numbers of the Boolean engine (plan 2026-11-25-exact-booleans §3).
 *
 * Everything is an integer. Input points have |coordinate| <= 2^26. A plane
 * comes from input points, a point from three planes (homogeneous, w > 0), and
 * nothing from constructed points but the origin of a ray (Mid). So the widths
 * below are bounds, and no result is ever rounded. WideInt wraps, so only
 * final values must fit, not partial sums.
 *
 *   plane normal          2^55 per component, length 2^55.6    int64
 *   plane offset          2^82.6                               Int128
 *   point: w; X, Y, Z     2^166.8; 2^192.8 (inside the box)    Int256
 *   classify(point)       2^250.2                              Int256
 *   cross of two normals  2^112 per component                  Int128
 *   triple of normals     2^166.8                              Int256
 *   compare(point, point) 2^360                                Int512
 *   classify(Mid)         2^418                                Int512
 *   orient2d(points)      2^555                                Int1024 */

#include <cstdint>
#include <numeric>

#include "litestl/math/predicates.h"
#include "litestl/math/vector.h"
#include "litestl/math/wideint.h"

namespace ubertopo::csg {

namespace lmath = litestl::math;
using Int128 = lmath::WideInt<2>;
using Int256 = lmath::WideInt<4>;
using Int512 = lmath::WideInt<8>;
using Int1024 = lmath::WideInt<16>;
using I3 = lmath::int64_3;

/** Bound of input coordinates. */
inline constexpr int64_t kCoordLimit = int64_t(1) << 26;

inline bool in_range(const I3 &p)
{
  for (int i = 0; i < 3; i++) {
    if (p[i] < -kCoordLimit || p[i] > kCoordLimit) {
      return false;
    }
  }
  return true;
}

/** Cross product; the caller's components must be below 2^30. */
inline I3 cross_small(const I3 &a, const I3 &b)
{
  return I3(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]);
}

inline I3 sub(const I3 &a, const I3 &b)
{
  return I3(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

/** a . b, for |sum| < 2^127. */
inline Int128 dot(const I3 &a, const I3 &b)
{
  return lmath::mul_wide(a[0], b[0]) + lmath::mul_wide(a[1], b[1]) + lmath::mul_wide(a[2], b[2]);
}

/* ---- planes ---- */

/** n . p + d = 0. Canonical: gcd(n) = 1 and the first non-zero component of n
 * is positive, so equal planes are equal bitwise. */
struct Plane {
  I3 n{0, 0, 0};
  Int128 d;

  bool operator==(const Plane &b) const
  {
    return n[0] == b.n[0] && n[1] == b.n[1] && n[2] == b.n[2] && d == b.d;
  }
  uint64_t computeHash() const
  {
    uint64_t h = 0;
    for (const uint64_t v : {uint64_t(n[0]), uint64_t(n[1]), uint64_t(n[2]), d.w[0], d.w[1]}) {
      h = (h ^ v) * 0x9e3779b97f4a7c15ull;
      h ^= h >> 29;
    }
    return h;
  }
};

/** The plane through @p p with normal @p n. Returns +1 if the canonical normal
 * points along n, -1 if against it, 0 if n is zero. */
inline int plane_from(const I3 &p, const I3 &n, Plane &out)
{
  const int64_t g = std::gcd(std::gcd(n[0], n[1]), n[2]);
  if (g == 0) {
    return 0;
  }
  const int64_t lead = n[0] ? n[0] : n[1] ? n[1] : n[2];
  const int64_t s = lead < 0 ? -g : g;
  out.n = I3(n[0] / s, n[1] / s, n[2] / s);
  out.d = -dot(out.n, p);
  return lead < 0 ? -1 : 1;
}

/** The plane through three input points; the sign is that of plane_from for
 * (b - a) x (c - a), 0 if they are collinear. */
inline int plane_through(const I3 &a, const I3 &b, const I3 &c, Plane &out)
{
  return plane_from(a, cross_small(sub(b, a), sub(c, a)), out);
}

/** The plane through a and b parallel to axis @p axis; false if the edge is
 * parallel to it. */
inline bool edge_plane(const I3 &a, const I3 &b, int axis, Plane &out)
{
  I3 e(0, 0, 0);
  e[axis] = 1;
  return plane_from(a, cross_small(sub(b, a), e), out) != 0;
}

/** Sign of n . p + d at an input point. */
inline int classify(const I3 &p, const Plane &q)
{
  return (dot(q.n, p) + q.d).sign();
}

/* ---- directions ---- */

/** A line's direction: a cross product of two normals. */
struct Dir {
  Int128 v[3];

  bool is_zero() const
  {
    return v[0].is_zero() && v[1].is_zero() && v[2].is_zero();
  }
};

inline Dir cross(const I3 &a, const I3 &b)
{
  Dir r;
  for (int i = 0; i < 3; i++) {
    const int j = (i + 1) % 3, k = (i + 2) % 3;
    r.v[i] = lmath::mul_wide(a[j], b[k]) - lmath::mul_wide(a[k], b[j]);
  }
  return r;
}

/** Direction of an edge of input points. */
inline Dir direction(const I3 &a, const I3 &b)
{
  Dir r;
  for (int i = 0; i < 3; i++) {
    r.v[i] = Int128::from(b[i] - a[i]);
  }
  return r;
}

/** n . d, exact (below 2^169). */
inline Int256 dot(const I3 &n, const Dir &d)
{
  Int256 r;
  for (int i = 0; i < 3; i++) {
    r += lmath::mul_wide(Int128::from(n[i]), d.v[i]);
  }
  return r;
}

/** a . b, exact (below 2^226). */
inline Int256 dot(const Dir &a, const Dir &b)
{
  Int256 r;
  for (int i = 0; i < 3; i++) {
    r += lmath::mul_wide(a.v[i], b.v[i]);
  }
  return r;
}

/** det[a, b, c]. */
inline Int256 triple(const I3 &a, const I3 &b, const I3 &c)
{
  return dot(c, cross(a, b));
}

/* ---- points ---- */

/** (x / w, y / w, z / w), w > 0. Not reduced: compare with same_point. */
struct HPoint {
  Int256 x[3];
  Int256 w;

  static HPoint from(const I3 &p)
  {
    HPoint r;
    for (int i = 0; i < 3; i++) {
      r.x[i] = Int256::from(p[i]);
    }
    r.w = Int256::from(1);
    return r;
  }
};

/** The common point of three planes; false if there is no single one. */
inline bool meet(const Plane &p, const Plane &q, const Plane &r, HPoint &out)
{
  const Dir qr = cross(q.n, r.n), rp = cross(r.n, p.n), pq = cross(p.n, q.n);
  Int256 w = dot(p.n, qr);
  const int s = w.sign();
  if (s == 0) {
    return false;
  }
  out.w = s < 0 ? -w : w;
  for (int i = 0; i < 3; i++) {
    /* -(p.d (q x r) + q.d (r x p) + r.d (p x q)) */
    const Int256 x = lmath::mul_wide(p.d, qr.v[i]) + lmath::mul_wide(q.d, rp.v[i]) + lmath::mul_wide(r.d, pq.v[i]);
    out.x[i] = s < 0 ? x : -x;
  }
  return true;
}

/** Sign of n . p + d. */
inline int classify(const HPoint &p, const Plane &q)
{
  Int256 s = Int256::from(q.d) * p.w;
  for (int i = 0; i < 3; i++) {
    s += Int256::from(q.n[i]) * p.x[i];
  }
  return s.sign();
}

/** Sign of a - b along one axis. */
inline int compare(const HPoint &a, const HPoint &b, int axis)
{
  return (lmath::mul_wide(a.x[axis], b.w) - lmath::mul_wide(b.x[axis], a.w)).sign();
}

inline int lex_compare(const HPoint &a, const HPoint &b)
{
  for (int i = 0; i < 3; i++) {
    const int c = compare(a, b, i);
    if (c) {
      return c;
    }
  }
  return 0;
}

inline bool same_point(const HPoint &a, const HPoint &b)
{
  return compare(a, b, 0) == 0 && compare(a, b, 1) == 0 && compare(a, b, 2) == 0;
}

/** floor(num / den) for den > 0, if it is below 2^50 in size. The double only
 * guesses; the result is exact. */
template <int N> int64_t floor_div(const lmath::WideInt<N> &num, const lmath::WideInt<N> &den)
{
  int64_t q = int64_t(num.to_double() / den.to_double());
  lmath::WideInt<N> r = num - lmath::WideInt<N>::from(q) * den;
  while (r.is_negative()) {
    r += den;
    q--;
  }
  while (!(r < den)) {
    r -= den;
    q++;
  }
  return q;
}

/** floor(2^shift p) on one axis. Depends on the point only. */
inline int64_t floor_at(const HPoint &p, int axis, int shift)
{
  return floor_div(p.x[axis] << shift, p.w);
}

/** 2^shift p on one axis, rounded to nearest; ties go up. */
inline int64_t round_at(const HPoint &p, int axis, int shift)
{
  return floor_div((p.x[axis] << (shift + 1)) + p.w, p.w << 1);
}

/** Sign of the orientation of three points projected along @p axis (keeping
 * axis + 1 and axis + 2). Slow: for diagonals only. */
inline int orient2d(const HPoint &a, const HPoint &b, const HPoint &c, int axis)
{
  const int u = (axis + 1) % 3, v = (axis + 2) % 3;
  const auto wide = [](const Int256 &x) { return Int1024::from(x); };
  const auto minor = [&](const HPoint &p, const HPoint &q) {
    return lmath::mul_wide(p.x[u], q.x[v]) - lmath::mul_wide(p.x[v], q.x[u]);
  };
  const Int1024 s = wide(a.w) * Int1024::from(minor(b, c)) + wide(b.w) * Int1024::from(minor(c, a)) +
                    wide(c.w) * Int1024::from(minor(a, b));
  return s.sign();
}

/* ---- rays ---- */

/** The midpoint of two points: the origin of a ray. */
struct Mid {
  Int512 x[3];
  Int512 w;

  Mid(const HPoint &a, const HPoint &b)
  {
    for (int i = 0; i < 3; i++) {
      x[i] = lmath::mul_wide(a.x[i], b.w) + lmath::mul_wide(b.x[i], a.w);
    }
    w = lmath::mul_wide(a.w, b.w) << 1;
  }
};

inline int classify(const Mid &p, const Plane &q)
{
  Int512 s = Int512::from(q.d) * p.w;
  for (int i = 0; i < 3; i++) {
    s += Int512::from(q.n[i]) * p.x[i];
  }
  return s.sign();
}

/** The largest t of ray_direction. */
inline constexpr int64_t kMaxRay = int64_t(1) << 12;

/** Directions such that each plane through the origin of a ray, and each line
 * that doesn't pass through it, rules out at most two. */
inline I3 ray_direction(int64_t t)
{
  return I3(1, t, t * t);
}

/** Sign of det[dir, a - o, b - o]: on which side of the ray from @p o the line
 * from a to b passes. */
inline int ray_side(const Mid &o, const I3 &dir, const I3 &a, const I3 &b)
{
  const I3 m = cross_small(sub(a, b), dir); /* below 2^53 */
  const I3 ab = cross_small(a, b);
  Int512 s = Int512::from(dot(dir, ab)) * o.w;
  for (int i = 0; i < 3; i++) {
    s += Int512::from(m[i]) * o.x[i];
  }
  return s.sign();
}

} // namespace ubertopo::csg
