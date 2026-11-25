#pragma once

/* Face and shell geometry of the region builder: signed volumes, boxes, and
 * a face's triangles. */

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "litestl/math/wideint.h"
#include "litestl/util/vector.h"
#include "ubertopo/geom/cdt/cdt.h"
#include "ubertopo/geom/predicates.h"
#include "ubertopo/topo/complex.h"
#include "ubertopo/topo/face_skeleton.h"
#include "ubertopo/topo/radial.h"

namespace ubertopo::topo::region_detail {

/** Signed volume under triangles: a filtered floating sum.
 *
 * A face has a subtotal, with its triangles in front winding, and a shell
 * adds its sides' subtotals. The error bound of sign() holds for any
 * summation order in which no term passes through more than @p terms
 * additions, and a sum of subtotals is one (plan 2026-11-24 §4.2). */
template <geom::CoordType C> struct Volume {
  double approx = 0.0, abs_sum = 0.0, perm_sum = 0.0;
  uint64_t terms = 0;

  void add(const geom::Point<C> &a, const geom::Point<C> &b, const geom::Point<C> &c)
  {
    const auto A = geom::to_double3<C>(a), B = geom::to_double3<C>(b), Cc = geom::to_double3<C>(c);
    const double x = B[1] * Cc[2] - B[2] * Cc[1], y = B[2] * Cc[0] - B[0] * Cc[2], z = B[0] * Cc[1] - B[1] * Cc[0];
    const double d = A[0] * x + A[1] * y + A[2] * z;
    approx += d;
    abs_sum += std::fabs(d);
    perm_sum += std::fabs(A[0]) * (std::fabs(B[1] * Cc[2]) + std::fabs(B[2] * Cc[1])) +
                std::fabs(A[1]) * (std::fabs(B[2] * Cc[0]) + std::fabs(B[0] * Cc[2])) +
                std::fabs(A[2]) * (std::fabs(B[0] * Cc[1]) + std::fabs(B[1] * Cc[0]));
    terms++;
  }
  /** Adds a subtotal, negated if @p sign < 0. */
  void add(const Volume &o, int sign)
  {
    approx += sign > 0 ? o.approx : -o.approx;
    abs_sum += o.abs_sum;
    perm_sum += o.perm_sum;
    terms += o.terms;
  }
  /** +1 / -1, or 0 if the sign can't be certified (or the volume is 0). */
  int sign() const
  {
    const double u = std::ldexp(1.0, -53);
    const double bound = 4.0 * u * perm_sum + 2.0 * double(terms) * u * abs_sum + 1e-300;
    return approx > bound ? 1 : (approx < -bound ? -1 : 0);
  }
  /** Magnitude, for ordering nested shells. */
  bool smaller_than(const Volume &o) const
  {
    return std::fabs(approx) < std::fabs(o.approx);
  }
};

/** Exact signed volume (Lattice). */
template <geom::CoordType C>
  requires geom::ExactCoord<C>
struct Volume<C> {
  litestl::math::Int128 exact = litestl::math::Int128::from(0);

  void add(const geom::Point<C> &a, const geom::Point<C> &b, const geom::Point<C> &c)
  {
    /* a . (b x c): |b x c| <= 2^53, products <= 2^79, sum < 2^81 */
    const int64_t bx = b[0].raw, by = b[1].raw, bz = b[2].raw, cx = c[0].raw, cy = c[1].raw, cz = c[2].raw;
    const int64_t x = by * cz - bz * cy, y = bz * cx - bx * cz, z = bx * cy - by * cx;
    exact = exact + (litestl::math::mul_wide(a[0].raw, x) + litestl::math::mul_wide(a[1].raw, y) +
                     litestl::math::mul_wide(a[2].raw, z));
  }
  void add(const Volume &o, int sign)
  {
    exact = sign > 0 ? exact + o.exact : exact - o.exact;
  }
  int sign() const
  {
    return exact.sign();
  }
  bool smaller_than(const Volume &o) const
  {
    const litestl::math::Int128 a = exact.sign() < 0 ? -exact : exact, b = o.exact.sign() < 0 ? -o.exact : o.exact;
    return a < b;
  }
};

struct BBox {
  double lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
  void add(const litestl::math::double3 &p)
  {
    for (int i = 0; i < 3; i++) {
      lo[i] = std::min(lo[i], p[i]);
      hi[i] = std::max(hi[i], p[i]);
    }
  }
  void add(const BBox &o)
  {
    for (int i = 0; i < 3; i++) {
      lo[i] = std::min(lo[i], o.lo[i]);
      hi[i] = std::max(hi[i], o.hi[i]);
    }
  }
  bool contains(const BBox &o) const
  {
    for (int i = 0; i < 3; i++) {
      if (o.lo[i] < lo[i] || o.hi[i] > hi[i]) {
        return false;
      }
    }
    return true;
  }
};

/** What a build keeps of one live face. */
template <geom::CoordType C> struct FaceSummary {
  Volume<C> vol; /* triangles in front winding */
  BBox box;      /* vertices, and the triangles (curved edges' skeletons) */
  bool ok = true; /* it triangulated */
};

/** Scratch of face_triangles; points of the last face gathered. */
template <geom::CoordType C> struct FaceTriangles {
  FacePoints<C> fp;
  litestl::util::Vector<geom::Triangle> cdt;
  litestl::util::Vector<geom::Point<C>> tris; /* three per triangle, front winding */

  /** The triangles of face @p f: loop fans (planar), T(f) (projected).
   * False, with no triangles, if a projected face doesn't triangulate. */
  bool gather(const Complex<C> &k, FaceRef f, Skeleton<C> &sk)
  {
    tris.clear();
    const int axis = k.face_proj_axis(f);
    if (axis >= 0) {
      cdt.clear();
      if (!fp.gather(k, f, sk) || geom::cdt_proj<C>(fp.view(), axis, cdt) != geom::CdtStatus::Ok) {
        return false;
      }
      for (const geom::Triangle &t : cdt) {
        tris.append(fp.pts[t[0]]);
        tris.append(fp.pts[t[1]]);
        tris.append(fp.pts[t[2]]);
      }
      return true;
    }
    fp.gather(k, f, sk);
    const auto v = fp.view();
    for (uint32_t l = 0; l < v.loop_count(); l++) {
      const auto lp = v.loop(l);
      for (size_t i = 1; i + 1 < lp.size(); i++) {
        tris.append(lp[0]);
        tris.append(lp[i]);
        tris.append(lp[i + 1]);
      }
    }
    return true;
  }
};

} // namespace ubertopo::topo::region_detail
