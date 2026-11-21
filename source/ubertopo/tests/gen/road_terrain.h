#pragma once

/* The road-on-terrain acceptance generator (plan P4b §6).
 *
 * A 2 km x 2 km terrain sheet of 40 x 40 projected quads, and a 1.2 km road
 * (line, clothoid, 300 m arc, clothoid, line; crest and sag vertical curves;
 * two lanes and two shoulders with a crown rotating to superelevation and a
 * widening outer lane) placed into it in one editor transaction: the cells
 * under the road are killed, the road's faces carry the road3 surface and
 * pcurves, and a fill face joins the hole's boundary to the road's outline.
 *
 * Coordinates. Everything the generator decides (stations, the kill set) is
 * decided once, in double metres, from the definition (plan_road). Each
 * coordinate type then builds the same definition in its own frame units,
 * scaled by u: 1 for double (metres), 2^10 for Lattice (quanta of 2^-10 m).
 * Scaling by a power of two is exact, so every definition (and everything
 * build_road derives from it) is the metre one times u, bit for bit; only the
 * rounded vertices differ. Terrain points (multiples of 50 m in x and y, of
 * 2^-4 m in z) are exact in both. */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../support/splitmix64.h"
#include "ubertopo/curves/alignment.h"
#include "ubertopo/curves/approx.h"
#include "ubertopo/curves/cesaro.h"
#include "ubertopo/curves/offset.h"
#include "ubertopo/curves/pcurve.h"
#include "ubertopo/curves/road.h"
#include "ubertopo/curves/road_build.h"
#include "ubertopo/ident/id.h"
#include "ubertopo/topo/builder.h"
#include "ubertopo/topo/curve_geom.h"
#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/regions.h"

namespace ubertopo::test::gen {

namespace rt {

namespace util = litestl::util;
namespace attr = litestl::attr;
using attr::PoolRef;
using ident::PersistentId;

inline constexpr int kCells = 40;          /* per side */
inline constexpr double kCell = 50;        /* m */
inline constexpr double kBuffer = 10;      /* m beyond the outer borders, and past the ends */
inline constexpr double kBufferTol = 0.1;  /* m: the buffer's flattening */
inline constexpr double kStationStep = 50; /* m */
inline constexpr double kStationSnap = 1;  /* m: a break this close replaces a step station */
inline constexpr int kLatticeQuantumLog2 = -10;
inline constexpr int kSkeletonTolLog2 = -6; /* m */

/** Frame units per metre: 1 for float types, 2^10 for Lattice. */
template <geom::CoordType C> constexpr int scale_log2()
{
  return std::is_same_v<C, geom::Lattice> ? -kLatticeQuantumLog2 : 0;
}

/* ---- the definition, in metres ---------------------------------------- */

/** A piecewise polynomial in metres: piece i in sigma = s - breaks[i]. */
struct MFn {
  std::vector<double> breaks;
  std::vector<std::vector<double>> pieces; /* low degree first */
};

/** Piecewise linear through (breaks[i], values[i]), as road_fixtures' linear. */
inline MFn mlinear(const std::vector<double> &breaks, const std::vector<double> &values)
{
  MFn f;
  f.breaks = breaks;
  for (size_t i = 0; i + 1 < breaks.size(); i++) {
    f.pieces.push_back({values[i], (values[i + 1] - values[i]) / (breaks[i + 1] - breaks[i])});
  }
  return f;
}

/** c on [s0, s1]. */
inline MFn mconstant(double c, double s0, double s1)
{
  return {{s0, s1}, {{c}}};
}

/** In frame units: coefficient k of a quantity of length dimension dim
 * (1: lengths, 0: slopes, -1: curvatures) scales by u^(dim - k), the breaks by
 * u. Exact (a power of two). */
inline curves::Poly frame_poly(const std::vector<double> &cs, int ul2, int dim)
{
  double c[8] = {};
  for (size_t k = 0; k < cs.size(); k++) {
    c[k] = std::ldexp(cs[k], ul2 * (dim - int(k)));
  }
  return curves::Poly::exact(c, int(cs.size()));
}

inline const char *frame_fn(const MFn &f, int ul2, int dim, curves::Fn1D &out)
{
  util::Vector<double, 2> br;
  util::Vector<curves::Poly, 1> ps;
  for (double b : f.breaks) {
    br.append(std::ldexp(b, ul2));
  }
  for (const auto &p : f.pieces) {
    ps.append(frame_poly(p, ul2, dim));
  }
  return curves::Fn1D::make(br, ps, out);
}

struct RoadDef {
  double x0 = 300, y0 = 500, th0 = 0.2;
  std::vector<std::pair<double, std::vector<double>>> segs; /* length, kappa(sigma) */
  double length = 0;
  MFn z;
  struct Link {
    MFn width, crossfall;
  };
  std::vector<Link> left, right; /* outward from the reference */
  std::vector<double> breaks;    /* every function's breaks, sorted, the ends included */
};

/** The road (plan §6): it turns left, so the right side is the outer one. */
inline RoadDef road_def()
{
  RoadDef d;
  const double k = 0x1p-9;     /* 1/512 m: the arc's curvature */
  const double lc = 128;       /* each clothoid */
  const double l1 = 322, l2 = 322, la = 300;
  d.segs = {{l1, {0.0}}, {lc, {0.0, k / lc}}, {la, {k}}, {lc, {k, -k / lc}}, {l2, {0.0}}};
  d.length = l1 + lc + la + lc + l2; /* 1200 */
  const double a0 = l1, a1 = l1 + lc, b0 = a1 + la, b1 = b0 + lc, e = d.length;
  /* the profile: grades of +-2 % joined by a crest (200..400) and a sag
   * (700..900) vertical curve of 200 m; each piece starts at the previous
   * piece's end value, so the profile is continuous to rounding */
  {
    const double g = 0.02, c = 2 * g / 200 / 2; /* the vertical curves' quadratic term */
    const std::vector<double> br = {0, 200, 400, 700, 900, e};
    const std::vector<std::vector<double>> shape = {{g}, {g, -c}, {-g}, {-g, c}, {g}};
    double v = -1;
    d.z.breaks = br;
    for (size_t i = 0; i < shape.size(); i++) {
      std::vector<double> p = {v};
      p.insert(p.end(), shape[i].begin(), shape[i].end());
      const double L = br[i + 1] - br[i];
      v = p[0] + L * (p[1] + (p.size() > 2 ? L * p[2] : 0.0));
      d.z.pieces.push_back(p);
    }
  }
  /* crossfall: a -2 % crown on the straights, rotating over the clothoids to
   * +6 % on the outer (right) side and -6 % on the inner (left) side */
  const MFn outer_fall = mlinear({0, a0, a1, b0, b1, e}, {-0.02, -0.02, 0.06, 0.06, -0.02, -0.02});
  const MFn inner_fall = mlinear({0, a0, a1, b0, b1, e}, {-0.02, -0.02, -0.06, -0.06, -0.02, -0.02});
  /* the outer lane widens from 3.5 m to 4.5 m over 30 m inside the first
   * transition, and back inside the second (symmetric about the middle) */
  const double w0 = a0 + 28, w1 = w0 + 30, w3 = e - w0, w2 = w3 - 30;
  const MFn outer_lane = mlinear({0, w0, w1, w2, w3, e}, {3.5, 3.5, 4.5, 4.5, 3.5, 3.5});
  d.right = {{outer_lane, outer_fall}, {mconstant(2, 0, e), outer_fall}};
  d.left = {{mconstant(3.5, 0, e), inner_fall}, {mconstant(2, 0, e), inner_fall}};
  /* every break */
  std::vector<double> all = {0, e, a0, a1, b0, b1};
  auto take = [&](const MFn &f) { all.insert(all.end(), f.breaks.begin(), f.breaks.end()); };
  take(d.z);
  for (const auto *side : {&d.left, &d.right}) {
    for (const RoadDef::Link &l : *side) {
      take(l.width);
      take(l.crossfall);
    }
  }
  std::sort(all.begin(), all.end());
  all.erase(std::unique(all.begin(), all.end()), all.end());
  d.breaks = all;
  return d;
}

/** Builds the reference and the road in frame units (ul2 = log2 u) through
 * @p intern. nullptr on success. */
template <typename Intern>
const char *build_road_def(const attr::PoolStore &s, Intern &&intern, const RoadDef &d, int ul2, PoolRef &ref,
                           curves::RoadBuild &out)
{
  using namespace curves;
  util::Vector<CesaroSegment, 4> segs;
  for (const auto &[len, kappa] : d.segs) {
    segs.append({std::ldexp(len, ul2), frame_poly(kappa, ul2, -1)});
  }
  Cesaro2 c;
  if (const char *e = Cesaro2::make(std::ldexp(d.x0, ul2), std::ldexp(d.y0, ul2), d.th0, segs, c)) {
    return e;
  }
  ref = intern(cesaro2_kind(), c);
  if (ref.none()) {
    return "interning the reference failed";
  }
  Fn1D z;
  if (const char *e = frame_fn(d.z, ul2, 1, z)) {
    return e;
  }
  RoadSection sec;
  for (int side = 0; side < 2; side++) {
    for (const RoadDef::Link &l : side == 0 ? d.left : d.right) {
      RoadLink rl;
      if (const char *e = frame_fn(l.width, ul2, 1, rl.width)) {
        return e;
      }
      if (const char *e = frame_fn(l.crossfall, ul2, 0, rl.crossfall)) {
        return e;
      }
      (side == 0 ? sec.left : sec.right).append(std::move(rl));
    }
  }
  return build_road(s, intern, ref, z, sec, out);
}

/* ---- the terrain -------------------------------------------------------- */

/** Height of grid point (i, j) in sixteenths of a metre, in [-240, 240]. */
inline int64_t terrain_h16(int i, int j)
{
  litestl::test::SplitMix64 r(0x7e77a1 + uint64_t(i) * 1009 + uint64_t(j) * 7919);
  return int64_t(r.below(481)) - 240;
}

inline PersistentId root_id()
{
  return ident::derive_generator(PersistentId{}, "road_terrain");
}

/* ---- the plan: stations and kill set, in double metres ----------------- */

struct Plan {
  RoadDef def;
  std::vector<double> stations;             /* metres */
  std::vector<uint8_t> killed;              /* [j * kCells + i] */
  std::vector<std::array<double, 2>> buffer; /* the buffer polygon, counter-clockwise */
  int n_killed = 0;
};

namespace plan_detail {

using P2 = litestl::math::double2;

inline int orient(const P2 &a, const P2 &b, const P2 &c)
{
  return geom::CoordTraits<double>::orient2d(a, b, c);
}

/* p on segment ab, given orient(a, b, p) == 0 */
inline bool on_seg(const P2 &a, const P2 &b, const P2 &p)
{
  return std::min(a[0], b[0]) <= p[0] && p[0] <= std::max(a[0], b[0]) && std::min(a[1], b[1]) <= p[1] &&
         p[1] <= std::max(a[1], b[1]);
}

/* closed segments ab and cd meet */
inline bool segs_meet(const P2 &a, const P2 &b, const P2 &c, const P2 &d)
{
  const int o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
  if (o1 * o2 < 0 && o3 * o4 < 0) {
    return true;
  }
  return (o1 == 0 && on_seg(a, b, c)) || (o2 == 0 && on_seg(a, b, d)) || (o3 == 0 && on_seg(c, d, a)) ||
         (o4 == 0 && on_seg(c, d, b));
}

/* p strictly inside the counter-clockwise polygon (winding number) */
inline bool inside(const std::vector<P2> &poly, const P2 &p)
{
  int wn = 0;
  for (size_t i = 0; i < poly.size(); i++) {
    const P2 &a = poly[i], &b = poly[(i + 1) % poly.size()];
    if (a[1] <= p[1]) {
      if (b[1] > p[1] && orient(a, b, p) > 0) {
        wn++;
      }
    }
    else if (b[1] <= p[1] && orient(a, b, p) < 0) {
      wn--;
    }
  }
  return wn != 0;
}

/* the closed cell square meets the closed polygon */
inline bool cell_meets(const std::vector<P2> &poly, int i, int j)
{
  const double x0 = i * kCell, y0 = j * kCell, x1 = x0 + kCell, y1 = y0 + kCell;
  const P2 sq[4] = {P2(x0, y0), P2(x1, y0), P2(x1, y1), P2(x0, y1)};
  for (const P2 &q : sq) {
    if (inside(poly, q)) {
      return true;
    }
  }
  for (size_t k = 0; k < poly.size(); k++) {
    const P2 &a = poly[k], &b = poly[(k + 1) % poly.size()];
    if (a[0] >= x0 && a[0] <= x1 && a[1] >= y0 && a[1] <= y1) {
      return true;
    }
    for (int e = 0; e < 4; e++) {
      if (segs_meet(a, b, sq[e], sq[(e + 1) % 4])) {
        return true;
      }
    }
  }
  return false;
}

/* flattened points of the offset of the reference by w (metres) */
inline const char *offset_points(attr::PoolStore &st, PoolRef ref, const curves::Fn1D &w, std::vector<P2> &out)
{
  using namespace curves;
  Offset2 o;
  if (const char *e = Offset2::make(st, ref, w, o)) {
    return e;
  }
  const PoolRef r = st.intern<attr::InternPool<Offset2>>(pool_id(offset2_kind()), std::move(o));
  const auto c = curve2(st, r);
  if (!c) {
    return "no evaluator for the buffer offset";
  }
  util::Vector<double> ts;
  const char *err = nullptr;
  if (!flatten<2>(*c, c->domain().lo, c->domain().hi, kBufferTol, ts, &err)) {
    return err ? err : "flattening the buffer failed";
  }
  for (double t : ts) {
    Vec<2> p;
    c->eval(t, 0, &p);
    out.push_back(P2(p[0], p[1]));
  }
  return nullptr;
}

} // namespace plan_detail

/** Decides the stations and the kill set from the definition alone (plan
 * §6), in double metres. nullptr on success, else a reason. */
inline const char *plan_road(Plan &plan)
{
  using namespace curves;
  using namespace plan_detail;
  plan.def = road_def();
  const RoadDef &d = plan.def;
  /* stations: the 50 m steps, and every break; a break within 1 m of a step
   * station replaces it */
  {
    std::vector<double> st;
    for (double s = 0; s <= d.length; s += kStationStep) {
      bool replaced = false;
      for (double b : d.breaks) {
        replaced = replaced || (std::fabs(b - s) < kStationSnap && b != s);
      }
      if (!replaced) {
        st.push_back(s);
      }
    }
    st.insert(st.end(), d.breaks.begin(), d.breaks.end());
    std::sort(st.begin(), st.end());
    st.erase(std::unique(st.begin(), st.end()), st.end());
    for (size_t i = 1; i < st.size(); i++) {
      if (st[i] - st[i - 1] < kStationSnap) {
        return "two stations closer than 1 m";
      }
    }
    plan.stations = st;
  }
  /* the buffer polygon: the outer borders pushed out by 10 m, flattened at
   * 0.1 m, closed by end caps extended 10 m along the end tangents */
  attr::PoolStore st;
  register_pools(st);
  PoolRef ref;
  RoadBuild rb;
  if (const char *e = build_road_def(st, store_interner(st), d, 0, ref, rb)) {
    return e;
  }
  Fn1D right, left;
  if (const char *e = Fn1D::add(rb.borders[0], Fn1D::constant(-kBuffer, 0, d.length), right)) {
    return e;
  }
  if (const char *e = Fn1D::add(rb.borders[rb.borders.size() - 1], Fn1D::constant(kBuffer, 0, d.length), left)) {
    return e;
  }
  std::vector<P2> rp, lp;
  if (const char *e = offset_points(st, ref, right, rp)) {
    return e;
  }
  if (const char *e = offset_points(st, ref, left, lp)) {
    return e;
  }
  const auto rc = curve2(st, ref);
  Vec<2> t0[2], t1[2];
  rc->eval(0, 1, t0);
  rc->eval(d.length, 1, t1);
  std::vector<P2> poly;
  poly.push_back(P2(rp.front()[0] - kBuffer * t0[1][0], rp.front()[1] - kBuffer * t0[1][1]));
  poly.insert(poly.end(), rp.begin(), rp.end());
  poly.push_back(P2(rp.back()[0] + kBuffer * t1[1][0], rp.back()[1] + kBuffer * t1[1][1]));
  poly.push_back(P2(lp.back()[0] + kBuffer * t1[1][0], lp.back()[1] + kBuffer * t1[1][1]));
  poly.insert(poly.end(), lp.rbegin(), lp.rend());
  poly.push_back(P2(lp.front()[0] - kBuffer * t0[1][0], lp.front()[1] - kBuffer * t0[1][1]));
  plan.buffer.clear();
  for (const P2 &p : poly) {
    plan.buffer.push_back({p[0], p[1]});
  }
  /* the cells meeting it */
  auto &K = plan.killed;
  K.assign(kCells * kCells, 0);
  for (int j = 0; j < kCells; j++) {
    for (int i = 0; i < kCells; i++) {
      K[j * kCells + i] = cell_meets(poly, i, j);
    }
  }
  auto at = [&](int i, int j) -> bool { return i >= 0 && j >= 0 && i < kCells && j < kCells && K[j * kCells + i]; };
  /* close to a disc: enclosed kept cells, and diagonal pinches */
  for (bool changed = true; changed;) {
    changed = false;
    std::vector<uint8_t> reach(kCells * kCells, 0);
    std::vector<std::array<int, 2>> stack;
    for (int a = 0; a < kCells; a++) {
      for (const auto &[i, j] : {std::array<int, 2>{a, 0}, {a, kCells - 1}, {0, a}, {kCells - 1, a}}) {
        if (!at(i, j) && !reach[j * kCells + i]) {
          reach[j * kCells + i] = 1;
          stack.push_back({i, j});
        }
      }
    }
    while (!stack.empty()) {
      const auto [i, j] = stack.back();
      stack.pop_back();
      for (const auto &[di, dj] : {std::array<int, 2>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
        const int a = i + di, b = j + dj;
        if (a >= 0 && b >= 0 && a < kCells && b < kCells && !at(a, b) && !reach[b * kCells + a]) {
          reach[b * kCells + a] = 1;
          stack.push_back({a, b});
        }
      }
    }
    for (int c = 0; c < kCells * kCells; c++) {
      if (!K[c] && !reach[c]) {
        K[c] = 1;
        changed = true;
      }
    }
    /* the 2 x 2 cells around each interior grid point: killed only on a diagonal */
    for (int j = 1; j < kCells; j++) {
      for (int i = 1; i < kCells; i++) {
        const bool a = at(i - 1, j - 1), b = at(i, j - 1), c = at(i - 1, j), e = at(i, j);
        if ((a && e && !b && !c) || (b && c && !a && !e)) {
          K[(j - 1) * kCells + (a ? i : i - 1)] = 1; /* the lower kept cell */
          changed = true;
        }
      }
    }
  }
  /* assert: 4-connected, away from the tile border (simply connected by the
   * closure above) */
  plan.n_killed = 0;
  int first = -1;
  for (int c = 0; c < kCells * kCells; c++) {
    if (K[c]) {
      plan.n_killed++;
      first = first < 0 ? c : first;
      const int i = c % kCells, j = c / kCells;
      if (i == 0 || j == 0 || i == kCells - 1 || j == kCells - 1) {
        return "the kill set touches the tile border";
      }
    }
  }
  if (first < 0) {
    return "the kill set is empty";
  }
  {
    std::vector<uint8_t> seen(kCells * kCells, 0);
    std::vector<int> stack = {first};
    seen[first] = 1;
    int n = 0;
    while (!stack.empty()) {
      const int c = stack.back();
      stack.pop_back();
      n++;
      const int i = c % kCells, j = c / kCells;
      for (const auto &[di, dj] : {std::array<int, 2>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
        if (at(i + di, j + dj) && !seen[(j + dj) * kCells + i + di]) {
          seen[(j + dj) * kCells + i + di] = 1;
          stack.push_back((j + dj) * kCells + i + di);
        }
      }
    }
    if (n != plan.n_killed) {
      return "the kill set is not 4-connected";
    }
  }
  return nullptr;
}

/* ---- the model ----------------------------------------------------------- */

template <geom::CoordType C> struct Model {
  topo::Complex<C> k;
  topo::Journal<C> j;
  std::vector<topo::VertRef> tv; /* terrain grid point (i, j): [j * (kCells + 1) + i] */
  std::vector<topo::FaceRef> tf; /* terrain cell (i, j): [j * kCells + i] */
  /* set by place_road */
  PoolRef ref;
  curves::RoadBuild rb;
  std::vector<double> s;                      /* stations, frame units */
  std::vector<std::vector<topo::VertRef>> rv; /* [station][border] */
  std::vector<std::vector<topo::EdgeRef>> along;  /* [interval][border] */
  std::vector<std::vector<topo::EdgeRef>> across; /* [station][link] */
  std::vector<std::vector<topo::FaceRef>> rf;     /* [interval][link] */
  topo::FaceRef fill;

  static constexpr int ul2 = scale_log2<C>();
  topo::VertRef grid(int i, int j) const
  {
    return tv[j * (kCells + 1) + i];
  }
};

/** The terrain sheet: built, regions built, the frame and skeleton_tol set. */
template <geom::CoordType C> bool build_terrain(Model<C> &m)
{
  using namespace topo;
  if constexpr (std::is_same_v<C, geom::Lattice>) {
    m.k.frame.quantum_log2 = kLatticeQuantumLog2;
  }
  Builder<C> b;
  const PersistentId root = root_id();
  const int ul2 = Model<C>::ul2;
  std::vector<uint32_t> key((kCells + 1) * (kCells + 1));
  for (int j = 0; j <= kCells; j++) {
    for (int i = 0; i <= kCells; i++) {
      geom::Point<C> p;
      if (!round_point<C>(litestl::math::double3(std::ldexp(i * kCell, ul2), std::ldexp(j * kCell, ul2),
                                                 std::ldexp(double(terrain_h16(i, j)), ul2 - 4)),
                          p))
      {
        return false;
      }
      key[j * (kCells + 1) + i] = b.vertex(ident::derive_generator(root, "tv", i, j), p);
    }
  }
  for (int j = 0; j < kCells; j++) {
    for (int i = 0; i < kCells; i++) {
      const auto g = [&](int a, int c) { return key[c * (kCells + 1) + a]; };
      b.face(ident::derive_generator(root, "tf", i, j), {{g(i, j), g(i + 1, j), g(i + 1, j + 1), g(i, j + 1)}});
    }
  }
  BuildReport rep;
  BuildPolicy pol;
  pol.allow_projectable_faces = true;
  RegionReport rr;
  if (!b.build(m.k, rep, pol) || !build_regions(m.k, rr)) {
    return false;
  }
  m.k.global.template get<".g.skeleton_tol">().set(0, std::ldexp(1.0, kSkeletonTolLog2 + ul2));
  /* find the elements by ID */
  m.tv.assign((kCells + 1) * (kCells + 1), VertRef());
  m.tf.assign(kCells * kCells, FaceRef());
  m.k.verts.for_each_live([&](uint32_t x) {
    const PersistentId id = m.k.verts.template get<".id">()[x];
    for (int j = 0; j <= kCells; j++) {
      for (int i = 0; i <= kCells; i++) {
        if (id == ident::derive_generator(root, "tv", i, j)) {
          m.tv[j * (kCells + 1) + i] = VertRef(x);
        }
      }
    }
  });
  m.k.faces.for_each_live([&](uint32_t x) {
    const PersistentId id = m.k.face_id(FaceRef(x));
    for (int j = 0; j < kCells; j++) {
      for (int i = 0; i < kCells; i++) {
        if (id == ident::derive_generator(root, "tf", i, j)) {
          m.tf[j * kCells + i] = FaceRef(x);
        }
      }
    }
  });
  return true;
}

struct PlaceOptions {
  /* the crossing variant (plan §6, Skeleton): the cell under the centre line
   * at the middle station stays, flattened to the road's height there, so the
   * road crosses it; there is no fill face (the outline leaves the hole) */
  bool cross = false;
};

/** The road, placed in one editor transaction (plan §6). The editor's
 * report says why on failure. */
template <geom::CoordType C>
topo::EditCode place_road(topo::Editor<C> &ed, Model<C> &m, const Plan &plan, PlaceOptions po = {})
{
  using namespace topo;
  using namespace curves;
  const int ul2 = Model<C>::ul2;
  const PersistentId root = root_id();
  auto ok = [](EditCode c) { return c == EditCode::Ok; };
  EditCode c = ed.begin("place_road");
  if (!ok(c)) {
    return c;
  }
  auto fail = [&](EditCode code) {
    ed.rollback();
    return code;
  };
  /* 1. the definitions, interned through the journal */
  EditCode ic = EditCode::Ok;
  auto intern = [&](const KindOps &kind, const auto &def) -> PoolRef {
    PoolRef r;
    const EditCode e = ed.intern_geom(kind, def, &r);
    if (e != EditCode::Ok) {
      ic = e;
      return PoolRef{};
    }
    return r;
  };
  if (build_road_def(m.k.geom, intern, plan.def, ul2, m.ref, m.rb) != nullptr) {
    return fail(ic == EditCode::Ok ? EditCode::BadArgument : ic);
  }
  const int nb = int(m.rb.borders.size()), nl = nb - 1;
  m.s.clear();
  for (double s : plan.stations) {
    m.s.push_back(std::ldexp(s, ul2));
  }
  const int ns = int(m.s.size());
  /* 2. kill the cells, then the edges and vertices only they used */
  std::vector<uint8_t> K = plan.killed;
  int kept = -1; /* the crossing variant's cell */
  geom::Point<C> centre;
  if (po.cross) {
    const auto car = curve3(m.k.geom, m.rb.carriers[m.rb.pivot]);
    Vec<3> p;
    car->eval(m.s[ns / 2], 0, &p);
    if (!round_point<C>(litestl::math::double3(p[0], p[1], p[2]), centre)) {
      return fail(EditCode::BadArgument);
    }
    const double cell = std::ldexp(kCell, ul2);
    kept = int(std::floor(p[1] / cell)) * kCells + int(std::floor(p[0] / cell));
    if (kept < 0 || kept >= kCells * kCells || !K[kept]) {
      return fail(EditCode::BadArgument);
    }
    K[kept] = 0;
  }
  auto killed = [&](int i, int j) { return i >= 0 && j >= 0 && i < kCells && j < kCells && K[j * kCells + i]; };
  if (kept >= 0) {
    const int i = kept % kCells, j = kept / kCells;
    for (const auto &[di, dj] : {std::array<int, 2>{0, 0}, {1, 0}, {1, 1}, {0, 1}}) {
      geom::Point<C> q = m.k.pos(m.grid(i + di, j + dj));
      q[2] = centre[2];
      if (!ok(c = ed.set_position(m.grid(i + di, j + dj), q))) {
        return fail(c);
      }
    }
  }
  for (int x = 0; x < kCells * kCells; x++) {
    if (K[x] && !ok(c = ed.kill_face(m.tf[x]))) {
      return fail(c);
    }
  }
  for (int j = 0; j <= kCells; j++) {
    for (int i = 0; i <= kCells; i++) {
      /* the edge to (i + 1, j) lies between cells (i, j - 1) and (i, j) */
      if (i < kCells && killed(i, j - 1) && killed(i, j)) {
        if (!ok(c = ed.kill_edge(ed.find_edge(m.grid(i, j), m.grid(i + 1, j))))) {
          return fail(c);
        }
      }
      if (j < kCells && killed(i - 1, j) && killed(i, j)) {
        if (!ok(c = ed.kill_edge(ed.find_edge(m.grid(i, j), m.grid(i, j + 1))))) {
          return fail(c);
        }
      }
    }
  }
  for (int j = 0; j <= kCells; j++) {
    for (int i = 0; i <= kCells; i++) {
      if (killed(i - 1, j - 1) && killed(i, j - 1) && killed(i - 1, j) && killed(i, j)) {
        if (!ok(c = ed.kill_vertex(m.grid(i, j)))) {
          return fail(c);
        }
      }
    }
  }
  /* 3. the road: vertices round_C(S(s_i, b_k(s_i))), from the border carriers */
  m.rv.assign(ns, std::vector<VertRef>(nb));
  for (int k = 0; k < nb; k++) {
    const auto car = curve3(m.k.geom, m.rb.carriers[k]);
    for (int i = 0; i < ns; i++) {
      Vec<3> p;
      car->eval(m.s[i], 0, &p);
      geom::Point<C> q;
      if (!round_point<C>(litestl::math::double3(p[0], p[1], p[2]), q)) {
        return fail(EditCode::BadArgument);
      }
      if (!ok(c = ed.make_vertex(q, ident::derive_generator(root, "rv", i, k), &m.rv[i][k]))) {
        return fail(c);
      }
    }
  }
  /* curved border edges, straight cross edges */
  m.along.assign(ns - 1, std::vector<EdgeRef>(nb));
  m.across.assign(ns, std::vector<EdgeRef>(nl));
  for (int i = 0; i < ns; i++) {
    for (int k = 0; k < nb; k++) {
      if (i + 1 < ns) {
        EdgeRef e;
        if (!ok(c = ed.make_edge(m.rv[i][k], m.rv[i + 1][k], ident::kNoId, &e))) {
          return fail(c);
        }
        const bool fwd = m.k.edge_verts(e)[0] == m.rv[i][k];
        if (!ok(c = ed.set_edge_curve(e, m.rb.carriers[k], fwd ? m.s[i] : m.s[i + 1], fwd ? m.s[i + 1] : m.s[i]))) {
          return fail(c);
        }
        m.along[i][k] = e;
      }
      if (k + 1 < nb && !ok(c = ed.make_edge(m.rv[i][k], m.rv[i][k + 1], ident::kNoId, &m.across[i][k]))) {
        return fail(c);
      }
    }
  }
  /* one face per link and station interval, with its surface and pcurves */
  m.rf.assign(ns - 1, std::vector<FaceRef>(nl));
  auto st_of = [&](VertRef v, double &s, double &t) {
    for (int i = 0; i < ns; i++) {
      for (int k = 0; k < nb; k++) {
        if (m.rv[i][k] == v) {
          s = m.s[i];
          t = m.rb.borders[k].eval(m.s[i]);
        }
      }
    }
  };
  util::Vector<PoolRef> cross_pc; /* [station * nl + link] */
  for (int i = 0; i < ns; i++) {
    for (int k = 0; k < nl; k++) {
      const auto vs = m.k.edge_verts(m.across[i][k]);
      double s0 = 0, t0 = 0, s1 = 0, t1 = 0;
      st_of(vs[0], s0, t0);
      st_of(vs[1], s1, t1);
      Segment2 sg;
      if (Segment2::make(s0, t0, s1, t1, sg) != nullptr) {
        return fail(EditCode::BadArgument);
      }
      cross_pc.append(intern(segment2_kind(), sg));
      if (cross_pc[cross_pc.size() - 1].none()) {
        return fail(ic);
      }
    }
  }
  auto coedge_in = [&](EdgeRef e, FaceRef f) {
    CoedgeRef out;
    m.k.for_each_radial(e, [&](CoedgeRef x) {
      if (m.k.face_of(x) == f) {
        out = x;
      }
    });
    return out;
  };
  for (int i = 0; i + 1 < ns; i++) {
    for (int k = 0; k < nl; k++) {
      /* s forward on border k, then left to border k + 1: counter-clockwise from above */
      const VertRef loop[4] = {m.rv[i][k], m.rv[i + 1][k], m.rv[i + 1][k + 1], m.rv[i][k + 1]};
      const uint32_t sizes[1] = {4};
      FaceRef f;
      if (!ok(c = ed.make_face(loop, sizes, ident::derive_generator(root, "rf", i, k), 0, ident::kNoId, ident::kNoId,
                               &f)))
      {
        return fail(c);
      }
      m.rf[i][k] = f;
      const std::pair<CoedgeRef, PoolRef> pcs[4] = {
          {coedge_in(m.along[i][k], f), m.rb.pcurves[k]},
          {coedge_in(m.along[i][k + 1], f), m.rb.pcurves[k + 1]},
          {coedge_in(m.across[i][k], f), cross_pc[i * nl + k]},
          {coedge_in(m.across[i + 1][k], f), cross_pc[(i + 1) * nl + k]},
      };
      if (!ok(c = ed.set_face_surface(f, m.rb.road, false, pcs))) {
        return fail(c);
      }
    }
  }
  /* 4. the fill face: the hole's boundary (counter-clockwise from above),
   * then the road's outline (clockwise) */
  if (!po.cross) {
    std::vector<VertRef> loops;
    {
      /* directed boundary edges of the killed region, region on the left:
       * next[(i, j)] = the grid point after (i, j) */
      std::vector<int> next((kCells + 1) * (kCells + 1), -1);
      int start = -1, n_edges = 0;
      auto gid = [](int i, int j) { return j * (kCells + 1) + i; };
      for (int j = 0; j < kCells; j++) {
        for (int i = 0; i < kCells; i++) {
          if (!killed(i, j)) {
            continue;
          }
          /* the cell's edges counter-clockwise; a boundary edge when the neighbour is kept */
          const int corner[4] = {gid(i, j), gid(i + 1, j), gid(i + 1, j + 1), gid(i, j + 1)};
          const bool nb_kept[4] = {!killed(i, j - 1), !killed(i + 1, j), !killed(i, j + 1), !killed(i - 1, j)};
          for (int e = 0; e < 4; e++) {
            if (nb_kept[e]) {
              next[corner[e]] = corner[(e + 1) % 4];
              start = start < 0 || corner[e] < start ? corner[e] : start;
              n_edges++;
            }
          }
        }
      }
      for (int g = start, n = 0;; n++) {
        if (g < 0 || n > n_edges) {
          return fail(EditCode::BadArgument);
        }
        loops.push_back(m.tv[g]);
        g = next[g];
        if (g == start) {
          break;
        }
      }
    }
    const uint32_t n_outer = uint32_t(loops.size());
    {
      /* the outline counter-clockwise: border 0 forward, across the end,
       * border m backward, across the start; then reversed */
      std::vector<VertRef> o;
      for (int i = 0; i < ns; i++) {
        o.push_back(m.rv[i][0]);
      }
      for (int k = 1; k < nb; k++) {
        o.push_back(m.rv[ns - 1][k]);
      }
      for (int i = ns - 2; i >= 0; i--) {
        o.push_back(m.rv[i][nb - 1]);
      }
      for (int k = nb - 2; k >= 1; k--) {
        o.push_back(m.rv[0][k]);
      }
      loops.insert(loops.end(), o.rbegin(), o.rend());
    }
    const uint32_t sizes[2] = {n_outer, uint32_t(loops.size()) - n_outer};
    if (!ok(c = ed.make_face(loops, sizes, ident::derive_generator(root, "fill"), 0, ident::kNoId, ident::kNoId,
                             &m.fill)))
    {
      return fail(c);
    }
  }
  /* 5. commit: validates the touched rows */
  if (!ok(c = ed.commit())) {
    return c;
  }
  return EditCode::Ok;
}

} // namespace rt

} // namespace ubertopo::test::gen
