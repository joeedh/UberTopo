#pragma once

/* Derived exact predicates, written once over CoordType from the traits'
 * primitives (design 02 §4, plan P2 W2). Inputs are spans of points; topo/
 * adapts faces and shells to these views (geom/ never sees topo types).
 *
 * Projections: drop_axis(p, k) keeps coordinates ((k+1)%3, (k+2)%3), in that
 * cyclic order, so orient2d_proj(a, b, c, k) has the sign of the k-th
 * component of (b-a) x (c-a). Every decision is exact; only project_axis
 * uses approximate arithmetic, and only to *choose* a non-degenerate
 * projection. */

#include <cmath>
#include <cstdint>
#include <span>

#include "litestl/util/vector.h"
#include "ubertopo/geom/coord.h"

namespace ubertopo::geom {

template <CoordType C> Point2<C> drop_axis(const Point<C> &p, int axis)
{
  return Point2<C>(p[(axis + 1) % 3], p[(axis + 2) % 3]);
}

template <CoordType C> int orient2d_proj(const Point<C> &a, const Point<C> &b, const Point<C> &c, int axis)
{
  return CoordTraits<C>::orient2d(drop_axis<C>(a, axis), drop_axis<C>(b, axis), drop_axis<C>(c, axis));
}

/** sign(det[b-a, c-a, d-a]): > 0 iff d is on the side of plane (a, b, c) that
 * (b-a) x (c-a) points to. */
template <CoordType C> int vol3(const Point<C> &a, const Point<C> &b, const Point<C> &c, const Point<C> &d)
{
  return -CoordTraits<C>::orient3d(a, b, c, d);
}
/** vol3 for constructed points (scaled test points, far ray points). */
template <CoordType C>
int vol3_ext(const Point<C> &a, const Point<C> &b, const Point<C> &c, const Point<C> &d)
{
  return -CoordTraits<C>::orient3d_ext(a, b, c, d);
}

/** Lexicographic comparison of two points projected along @p axis. */
template <CoordType C> int lex_compare_proj(const Point<C> &a, const Point<C> &b, int axis)
{
  if (const int c = CoordTraits<C>::compare(a, b, (axis + 1) % 3)) {
    return c;
  }
  return CoordTraits<C>::compare(a, b, (axis + 2) % 3);
}

/** A planar polygon with holes: loops concatenated in @p pts, loop i is
 * pts[off[i] .. off[i+1]). */
template <CoordType C> struct PolyView {
  std::span<const Point<C>> pts;
  std::span<const uint32_t> off;

  uint32_t loop_count() const
  {
    return off.empty() ? 0 : uint32_t(off.size() - 1);
  }
  std::span<const Point<C>> loop(uint32_t i) const
  {
    return pts.subspan(off[i], off[i + 1] - off[i]);
  }
};

/** Dominant axis of the approximate (double) Newell normal of all loops:
 * the projection along it is non-degenerate for any non-degenerate planar
 * face. -1 if the approximate normal is zero. Ties pick the smaller axis. */
template <CoordType C> int project_axis(const PolyView<C> &f)
{
  double n[3] = {0, 0, 0};
  for (uint32_t l = 0; l < f.loop_count(); l++) {
    const auto loop = f.loop(l);
    const size_t m = loop.size();
    for (size_t i = 0; i < m; i++) {
      const auto a = to_double3<C>(loop[i]);
      const auto b = to_double3<C>(loop[(i + 1) % m]);
      n[0] += (a[1] - b[1]) * (a[2] + b[2]);
      n[1] += (a[2] - b[2]) * (a[0] + b[0]);
      n[2] += (a[0] - b[0]) * (a[1] + b[1]);
    }
  }
  int best = -1;
  double bv = 0.0;
  for (int k = 0; k < 3; k++) {
    if (std::fabs(n[k]) > bv) {
      bv = std::fabs(n[k]);
      best = k;
    }
  }
  return best;
}

/** Index of the lexicographically minimal vertex of @p loop in projection. */
template <CoordType C> size_t lex_min_proj(std::span<const Point<C>> loop, int axis)
{
  size_t best = 0;
  for (size_t i = 1; i < loop.size(); i++) {
    if (lex_compare_proj<C>(loop[i], loop[best], axis) < 0) {
      best = i;
    }
  }
  return best;
}

/** Exact projected orientation of a simple loop (+1 counter-clockwise): the
 * lexicographically minimal vertex is extreme, hence convex, so the turn there
 * gives the orientation. 0 only for degenerate loops. */
template <CoordType C> int loop_orientation_proj(std::span<const Point<C>> loop, int axis)
{
  const size_t m = loop.size();
  if (m < 3) {
    return 0;
  }
  const size_t i = lex_min_proj<C>(loop, axis);
  return orient2d_proj<C>(loop[(i + m - 1) % m], loop[i], loop[(i + 1) % m], axis);
}

/** The loop holding the face's lexicographically minimal projected vertex.
 * With holes strictly inside, that is the outer loop. */
template <CoordType C> uint32_t outer_loop_proj(const PolyView<C> &f, int axis)
{
  uint32_t best_loop = 0;
  const Point<C> *best = nullptr;
  for (uint32_t l = 0; l < f.loop_count(); l++) {
    const auto loop = f.loop(l);
    if (loop.empty()) {
      continue;
    }
    const Point<C> &p = loop[lex_min_proj<C>(loop, axis)];
    if (!best || lex_compare_proj<C>(p, *best, axis) < 0) {
      best = &p;
      best_loop = l;
    }
  }
  return best_loop;
}

/** +1 if projecting along @p axis preserves the face's front orientation (the
 * outer loop appears counter-clockwise), -1 if it mirrors it, 0 if degenerate. */
template <CoordType C> int face_front_sign(const PolyView<C> &f, int axis)
{
  if (f.loop_count() == 0) {
    return 0;
  }
  return loop_orientation_proj<C>(f.loop(outer_loop_proj<C>(f, axis)), axis);
}

enum class PointClass : uint8_t { Outside, Inside, Boundary };

/** p on the closed segment [a, b], all three collinear in projection. */
template <CoordType C> bool on_segment_collinear_proj(const Point<C> &p, const Point<C> &a, const Point<C> &b, int axis)
{
  const int ca = lex_compare_proj<C>(p, a, axis), cb = lex_compare_proj<C>(p, b, axis);
  return ca == 0 || cb == 0 || ca != cb;
}

/** Exact even-odd classification of a coplanar point against a polygon with
 * holes, in projection along @p axis. */
template <CoordType C> PointClass point_in_poly_proj(const Point<C> &p, const PolyView<C> &f, int axis)
{
  const int v = (axis + 2) % 3;
  using T = CoordTraits<C>;
  bool inside = false;
  for (uint32_t l = 0; l < f.loop_count(); l++) {
    const auto loop = f.loop(l);
    const size_t m = loop.size();
    for (size_t i = 0; i < m; i++) {
      const Point<C> &a = loop[i], &b = loop[(i + 1) % m];
      const int o = orient2d_proj<C>(a, b, p, axis);
      if (o == 0 && on_segment_collinear_proj<C>(p, a, b, axis)) {
        return PointClass::Boundary;
      }
      /* half-open rule on the v coordinate; crossing to the +u side of p */
      const bool a_above = T::compare(a, p, v) > 0, b_above = T::compare(b, p, v) > 0;
      if (a_above != b_above) {
        /* the edge crosses the horizontal line through p; it crosses to the
         * right iff p is on the left of the upward-directed edge */
        const int side = b_above ? o : -o;
        if (side > 0) {
          inside = !inside;
        }
      }
    }
  }
  return inside ? PointClass::Inside : PointClass::Outside;
}

enum class SegIsect : uint8_t {
  None,    /* disjoint */
  Proper,  /* interiors cross at one point */
  Touch,   /* a single shared point that is an endpoint of at least one */
  Overlap, /* collinear with a shared piece of positive length */
};

/** Exact classification of segments [a, b] and [c, d] in projection. */
template <CoordType C>
SegIsect segments_intersect_proj(const Point<C> &a, const Point<C> &b, const Point<C> &c, const Point<C> &d, int axis)
{
  const int o1 = orient2d_proj<C>(a, b, c, axis), o2 = orient2d_proj<C>(a, b, d, axis);
  const int o3 = orient2d_proj<C>(c, d, a, axis), o4 = orient2d_proj<C>(c, d, b, axis);
  if (o1 == 0 && o2 == 0) {
    /* collinear: order the four points along the line */
    auto lo = [&](const Point<C> &x, const Point<C> &y) -> const Point<C> & {
      return lex_compare_proj<C>(x, y, axis) <= 0 ? x : y;
    };
    auto hi = [&](const Point<C> &x, const Point<C> &y) -> const Point<C> & {
      return lex_compare_proj<C>(x, y, axis) <= 0 ? y : x;
    };
    const Point<C> &s0 = lo(a, b), &s1 = hi(a, b), &t0 = lo(c, d), &t1 = hi(c, d);
    const Point<C> &start = lex_compare_proj<C>(s0, t0, axis) >= 0 ? s0 : t0;
    const Point<C> &end = lex_compare_proj<C>(s1, t1, axis) <= 0 ? s1 : t1;
    const int c0 = lex_compare_proj<C>(start, end, axis);
    return c0 > 0 ? SegIsect::None : (c0 == 0 ? SegIsect::Touch : SegIsect::Overlap);
  }
  if (o1 * o2 > 0 || o3 * o4 > 0) {
    return SegIsect::None;
  }
  if (o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0) {
    return SegIsect::Proper;
  }
  /* exactly one endpoint lies on the other segment's line; it is inside the
   * other segment (the sign tests above passed) */
  return SegIsect::Touch;
}

/* ---------------------------------------------------------------------- */
/* Radial order about an edge (plan P2 W5). */

/** The half-plane bounded by the edge line through v0, v1 that contains q
 * (s = +1) or that is opposite q (s = -1); q is not collinear with the edge. */
template <CoordType C> struct HalfPlane {
  Point<C> q;
  int s = 1;
};

/** Angular class of @p h about the axis v0 -> v1 relative to @p ref
 * (counter-clockwise, right-hand rule): 0 at angle 0, 1 in (0, pi), 2 at pi,
 * 3 in (pi, 2pi). */
template <CoordType C>
int radial_class(const Point<C> &v0, const Point<C> &v1, const HalfPlane<C> &ref, const HalfPlane<C> &h)
{
  const int t = ref.s * h.s * vol3<C>(v0, v1, ref.q, h.q);
  if (t > 0) {
    return 1;
  }
  if (t < 0) {
    return 3;
  }
  /* coplanar with the reference half-plane: compare the effective sides of
   * the edge line in a projection where (v0, v1, ref.q) is non-degenerate */
  for (int axis = 0; axis < 3; axis++) {
    const int sr = orient2d_proj<C>(v0, v1, ref.q, axis);
    if (sr != 0) {
      const int sh = orient2d_proj<C>(v0, v1, h.q, axis);
      return sr * ref.s == sh * h.s ? 0 : 2;
    }
  }
  return 0; /* unreachable for non-collinear ref.q */
}

/** -1 if @p a precedes @p b counter-clockwise from @p ref about v0 -> v1, +1 if
 * it follows, 0 if both have exactly the same angle. */
template <CoordType C>
int radial_compare(const Point<C> &v0,
                   const Point<C> &v1,
                   const HalfPlane<C> &ref,
                   const HalfPlane<C> &a,
                   const HalfPlane<C> &b)
{
  const int ca = radial_class<C>(v0, v1, ref, a), cb = radial_class<C>(v0, v1, ref, b);
  if (ca != cb) {
    return ca < cb ? -1 : 1;
  }
  if (ca == 0 || ca == 2) {
    return 0;
  }
  const int t = a.s * b.s * vol3<C>(v0, v1, a.q, b.q);
  return t > 0 ? -1 : (t < 0 ? 1 : 0);
}

/* ---------------------------------------------------------------------- */
/* Ray crossings (plan P2 W2): point-in-shell by signed crossings. */

enum class RayStatus : uint8_t {
  Ok,         /* winding is valid */
  Boundary,   /* the point lies on a triangle */
  Degenerate, /* every direction in the table hit an edge or vertex */
};

struct RayResult {
  RayStatus status = RayStatus::Degenerate;
  int winding = 0;
  int retries = 0;
};

namespace detail {
/* Generic integer directions: the dominant component is ~2^20, the others are
 * <= 2^11 so they stay small when the far point is scaled (bit budget). */
inline constexpr int64_t kRayDirs[][3] = {
    {(1 << 20) + 3, (1 << 10) + 1, 1},
    {-((1 << 20) + 5), 3, (1 << 10) + 7},
    {(1 << 10) + 9, (1 << 20) + 11, -5},
    {-7, -((1 << 10) + 13), (1 << 20) + 15},
    {(1 << 11) - 3, -((1 << 20) + 17), (1 << 9) + 1},
    {-((1 << 9) + 11), (1 << 11) - 7, -((1 << 20) + 19)},
    {(1 << 20) + 21, -((1 << 11) - 9), -((1 << 10) + 23)},
    {29, (1 << 10) + 31, -((1 << 20) + 33)},
};
inline constexpr int kRayDirCount = int(sizeof(kRayDirs) / sizeof(kRayDirs[0]));

template <CoordType C> bool scale_pow2(Point<C> &p, int k)
{
  using T = CoordTraits<C>;
  for (int j = 0; j < k; j++) {
    for (int i = 0; i < 3; i++) {
      if (!T::add_exact(p[i], p[i], p[i])) {
        return false;
      }
    }
  }
  return T::ext_ok(p);
}

/** Far endpoint q = p + D * 2^L beyond [lo, hi] along D's dominant axis. */
template <CoordType C>
bool far_point(const Point<C> &p, const Point<C> &lo, const Point<C> &hi, const int64_t (&d)[3], Point<C> &q)
{
  using T = CoordTraits<C>;
  int m = 0;
  for (int i = 1; i < 3; i++) {
    if ((d[i] < 0 ? -d[i] : d[i]) > (d[m] < 0 ? -d[m] : d[m])) {
      m = i;
    }
  }
  const double span = d[m] > 0 ? T::to_double(hi[m]) - T::to_double(p[m]) : T::to_double(p[m]) - T::to_double(lo[m]);
  const double dm = double(d[m] < 0 ? -d[m] : d[m]);
  int L = span > dm ? int(std::ceil(std::log2(span / dm))) : 0;
  L = L < 0 ? 0 : L;
  for (int attempt = 0; attempt < 4; attempt++, L++) {
    bool ok = true;
    for (int i = 0; i < 3 && ok; i++) {
      C off;
      ok = T::from_int_pow2(d[i], L, off);
      if (ok) {
        q[i] = p[i] + off;
      }
    }
    if (!ok || !T::ext_ok(q)) {
      return false;
    }
    const bool beyond = d[m] > 0 ? T::compare(q, hi, m) > 0 : T::compare(q, lo, m) < 0;
    if (beyond) {
      return true;
    }
  }
  return false;
}
} // namespace detail

/** 4x the interior point (a + b + 2c) / 4 of a non-degenerate triangle;
 * false if not exactly representable (double inputs off a dyadic grid). */
template <CoordType C> bool interior_point4(const Point<C> &a, const Point<C> &b, const Point<C> &c, Point<C> &out)
{
  using T = CoordTraits<C>;
  for (int i = 0; i < 3; i++) {
    C ab, cc, s;
    if (!T::add_exact(a[i], b[i], ab) || !T::add_exact(c[i], c[i], cc) || !T::add_exact(ab, cc, s)) {
      return false;
    }
    out[i] = s;
  }
  return T::ext_ok(out);
}

/** Signed crossing count (winding number) of the oriented triangles @p tris
 * (3 points each) about @p p, along a segment from p to a far point. With
 * @p scale_log2 = k, p is given multiplied by 2^k (e.g. from interior_point4,
 * k = 2) and the triangles are scaled to match, exactly.
 *
 * For a closed shell whose triangles are oriented with outward normals, the
 * winding is 1 inside and 0 outside. Any degenerate contact (the segment
 * through an edge or vertex, or lying in a triangle's plane) moves on to the
 * next direction of a fixed table; the retry count is reported. */
template <CoordType C> RayResult ray_winding(const Point<C> &p, std::span<const Point<C>> tris, int scale_log2 = 0)
{
  using T = CoordTraits<C>;
  RayResult res;
  const size_t nt = tris.size() / 3;
  /* scaled copy of the triangles, and their bounding box with p */
  if (!T::ext_ok(p)) {
    return res;
  }
  litestl::util::Vector<Point<C>> s;
  for (size_t i = 0; i < nt * 3; i++) {
    s.append(tris[i]);
  }
  for (auto &x : s) {
    if (!detail::scale_pow2<C>(x, scale_log2)) {
      return res;
    }
  }
  Point<C> lo = p, hi = p;
  for (const auto &x : s) {
    for (int i = 0; i < 3; i++) {
      if (T::compare(x, lo, i) < 0) {
        lo[i] = x[i];
      }
      if (T::compare(x, hi, i) > 0) {
        hi[i] = x[i];
      }
    }
  }
  for (int k = 0; k < detail::kRayDirCount; k++) {
    Point<C> q;
    if (!detail::far_point<C>(p, lo, hi, detail::kRayDirs[k], q)) {
      res.retries++;
      continue;
    }
    int w = 0;
    bool degenerate = false;
    for (size_t t = 0; t < nt && !degenerate; t++) {
      const Point<C> &a = s[3 * t], &b = s[3 * t + 1], &c = s[3 * t + 2];
      const int sp = vol3_ext<C>(a, b, c, p), sq = vol3_ext<C>(a, b, c, q);
      if (sp == sq && sp != 0) {
        continue; /* both on one side */
      }
      const int e1 = vol3_ext<C>(p, q, a, b), e2 = vol3_ext<C>(p, q, b, c), e3 = vol3_ext<C>(p, q, c, a);
      const bool pos = e1 > 0 || e2 > 0 || e3 > 0, neg = e1 < 0 || e2 < 0 || e3 < 0;
      if (pos && neg) {
        continue; /* the line misses the closed triangle */
      }
      if (sp == 0 && sq != 0) {
        /* the line meets the plane only at p, inside the closed triangle */
        res.status = RayStatus::Boundary;
        res.winding = 0;
        return res;
      }
      if (e1 == 0 || e2 == 0 || e3 == 0 || sq == 0) {
        degenerate = true; /* through an edge or a vertex, or in the plane */
        break;
      }
      w += sq; /* crossing from the back (sp < 0) to the front adds +1 */
    }
    if (!degenerate) {
      res.status = RayStatus::Ok;
      res.winding = w;
      return res;
    }
    res.retries++;
  }
  res.status = RayStatus::Degenerate;
  return res;
}

} // namespace ubertopo::geom
