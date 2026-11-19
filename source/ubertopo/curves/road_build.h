#pragma once

/* The road builder (plan P4b §3.4): a helper for generators, not a kind.
 *
 * From a reference cesaro2, a centre profile z(s) and a cross-section of
 * links outward from a pivot border at t = 0, it builds:
 *  - the borders b_k (sums of link widths; t > 0 is left of the reference);
 *  - the heights Z_k = z + sum of c_j w_j over the links between the pivot
 *    and border k (c_j: the link's crossfall, dZ per unit of width moving
 *    outward, so -0.02 drains a crown outward);
 *  - the interned road3, and for each border its offset2 (the reference
 *    itself where b_k is zero over the reference's whole length), its
 *    alignment3 carrier and its graph2 pcurve.
 * A superelevation transition is a crossfall that changes over a station
 * range, e.g. Fn1D pieces running linearly from -0.02 to +0.06.
 *
 * Every Fn1D shares z's domain. Interning goes through a caller-supplied
 * intern(kind, def) -> PoolRef (kNoRef on failure), so the same builder
 * serves a bare PoolStore (store_interner) and the editor's journaled
 * intern_curve. */

#include "ubertopo/curves/alignment.h"
#include "ubertopo/curves/offset.h"
#include "ubertopo/curves/pcurve.h"
#include "ubertopo/curves/road.h"

namespace ubertopo::curves {

struct RoadLink {
  Fn1D width;     /* > 0 */
  Fn1D crossfall; /* dZ / d|t| moving outward */
};

struct RoadSection {
  util::Vector<RoadLink, 4> left;  /* outward from the pivot, t > 0 */
  util::Vector<RoadLink, 4> right; /* outward from the pivot, t < 0 */
};

struct RoadBuild {
  attr::PoolRef road;               /* road3 */
  int pivot = 0;                    /* the border at t = 0 */
  util::Vector<Fn1D, 4> borders;    /* b_0 (rightmost) .. b_m (leftmost) */
  util::Vector<Fn1D, 4> heights;    /* Z_0 .. Z_m */
  util::Vector<attr::PoolRef, 4> offsets, carriers, pcurves; /* per border */
};

/** intern(kind, def) for a bare store set up by register_pools. */
inline auto store_interner(attr::PoolStore &s)
{
  return [&s](const KindOps &kind, const auto &def) -> attr::PoolRef {
    using T = std::decay_t<decltype(def)>;
    return s.intern<attr::InternPool<T>>(pool_id(kind), T(def));
  };
}

namespace road_detail {

inline Fn1D negated(const Fn1D &f)
{
  util::Vector<Poly, 1> ps;
  for (const Poly &p : f.pieces) {
    ps.append(p.scaled(-1.0)); /* exact */
  }
  Fn1D out;
  Fn1D::make(f.breaks, ps, out);
  return out;
}

} // namespace road_detail

/** Builds the road (see the header comment). nullptr on success, else a
 * reason; nothing is interned before the definitions validate, except the
 * offsets the road3 checks need (an orphan is collected later). */
template <typename Intern>
const char *build_road(const attr::PoolStore &s,
                       Intern &&intern,
                       attr::PoolRef reference,
                       const Fn1D &z,
                       const RoadSection &sec,
                       RoadBuild &out)
{
  RoadBuild r;
  const double lo = z.lo(), hi = z.hi();
  /* borders and heights, outward on each side of the pivot */
  util::Vector<Fn1D, 4> lb, lz, rb, rz;
  for (int side = 0; side < 2; side++) {
    const auto &links = side == 0 ? sec.left : sec.right;
    auto &bs = side == 0 ? lb : rb;
    auto &zs = side == 0 ? lz : rz;
    Fn1D b = Fn1D::constant(0.0, lo, hi), h = z;
    for (const RoadLink &l : links) {
      if (l.width.empty() || l.width.lo() != lo || l.width.hi() != hi || l.crossfall.empty() ||
          l.crossfall.lo() != lo || l.crossfall.hi() != hi)
      {
        return "build_road: every width and crossfall must share z's domain";
      }
      Fn1D nb, rise, nh;
      if (const char *e = Fn1D::add(b, side == 0 ? l.width : road_detail::negated(l.width), nb)) {
        return e;
      }
      if (const char *e = Fn1D::mul(l.crossfall, l.width, rise)) {
        return e;
      }
      if (const char *e = Fn1D::add(h, rise, nh)) {
        return e;
      }
      bs.append(nb);
      zs.append(nh);
      b = std::move(nb);
      h = std::move(nh);
    }
  }
  for (int i = rb.size() - 1; i >= 0; i--) {
    r.borders.append(rb[i]);
    r.heights.append(rz[i]);
  }
  r.pivot = r.borders.size();
  r.borders.append(Fn1D::constant(0.0, lo, hi));
  r.heights.append(z);
  for (int i = 0; i < lb.size(); i++) {
    r.borders.append(lb[i]);
    r.heights.append(lz[i]);
  }
  /* the road3 first: its checks (order, folds) decide */
  util::Vector<Fn1D, 2> bv, hv;
  for (int i = 0; i < r.borders.size(); i++) {
    bv.append(r.borders[i]);
    hv.append(r.heights[i]);
  }
  Road3 road;
  if (const char *e = Road3::make(s, reference, bv, hv, road)) {
    return e;
  }
  r.road = intern(road3_kind(), road);
  if (r.road.none()) {
    return "build_road: interning the road3 failed";
  }
  for (int i = 0; i < r.borders.size(); i++) {
    const Fn1D &b = r.borders[i];
    attr::PoolRef horiz = reference;
    const Cesaro2 *c = s.try_get<Cesaro2Pool>(reference);
    if (!(b.is_zero() && c && lo == 0 && hi == c->length())) {
      Offset2 o;
      if (const char *e = Offset2::make(s, reference, b, o)) {
        return e;
      }
      horiz = intern(offset2_kind(), o);
    }
    Alignment3 al;
    if (horiz.none()) {
      return "build_road: interning an offset failed";
    }
    if (const char *e = Alignment3::make(s, horiz, r.heights[i], al)) {
      return e;
    }
    Graph2 g;
    if (const char *e = Graph2::make(b, g)) {
      return e;
    }
    const attr::PoolRef car = intern(alignment3_kind(), al), pc = intern(graph2_kind(), g);
    if (car.none() || pc.none()) {
      return "build_road: interning a carrier or pcurve failed";
    }
    r.offsets.append(horiz);
    r.carriers.append(car);
    r.pcurves.append(pc);
  }
  out = std::move(r);
  return nullptr;
}

} // namespace ubertopo::curves
