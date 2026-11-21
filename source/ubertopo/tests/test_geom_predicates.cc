/* geom/: Lattice, TileFrame and the derived exact predicates (plan P2 W2).
 *
 * Inputs are integer grids; the double instantiation uses the same values
 * times 2^-8 (exact), so one big-integer oracle covers both. */

#include "ubertopo/geom/coord.h"
#include "ubertopo/geom/predicates.h"

#include "support/bigint_oracle.h"
#include "support/splitmix64.h"
#include "ut_test.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace ubertopo;
using namespace ubertopo::geom;
using litestl::test::BigInt;
using litestl::test::SplitMix64;

using I3 = std::array<int64_t, 3>;

template <CoordType C> Point<C> mk(const I3 &v)
{
  if constexpr (std::is_same_v<C, Lattice>) {
    return Point<C>(Lattice(v[0]), Lattice(v[1]), Lattice(v[2]));
  }
  else {
    return Point<C>(C(std::ldexp(double(v[0]), -8)), C(std::ldexp(double(v[1]), -8)), C(std::ldexp(double(v[2]), -8)));
  }
}

static BigInt B(int64_t v)
{
  return BigInt::from_i64(v);
}

/* sign det[b-a, c-a, d-a] */
static int oracle_vol3(const I3 &a, const I3 &b, const I3 &c, const I3 &d)
{
  std::vector<std::vector<BigInt>> m(3, std::vector<BigInt>(3));
  for (int i = 0; i < 3; i++) {
    m[0][i] = B(b[i]) - B(a[i]);
    m[1][i] = B(c[i]) - B(a[i]);
    m[2][i] = B(d[i]) - B(a[i]);
  }
  return BigInt::det(m).sign();
}

/* sign of the axis-th component of (b-a) x (c-a) */
static int oracle_cross_k(const I3 &a, const I3 &b, const I3 &c, int k)
{
  const int u = (k + 1) % 3, v = (k + 2) % 3;
  const BigInt x = (B(b[u]) - B(a[u])) * (B(c[v]) - B(a[v])) - (B(b[v]) - B(a[v])) * (B(c[u]) - B(a[u]));
  return x.sign();
}

static I3 rand_i3(SplitMix64 &r, int64_t lim)
{
  return {r.range(-lim, lim), r.range(-lim, lim), r.range(-lim, lim)};
}

/* ---------------------------------------------------------------------- */

static void test_lattice_and_frame()
{
  static_assert(!std::is_convertible_v<int64_t, Lattice>);
  static_assert(!std::is_convertible_v<Lattice, int64_t>);
  Lattice a(5), b(7);
  UT_CHECK((a + b).raw == 12 && (a - b).raw == -2 && (-a).raw == -5 && a < b && a != b);

  TileFrame f;
  f.quantum_log2 = -10;
  UT_CHECK(f.quantum() == 1.0 / 1024);
  const int64_t lim = TileFrame::kLatticeLimit;
  Point<Lattice> p;
  UT_CHECK(f.to_lattice(litestl::math::double3(1.5, -2.25, 0.0), p) && p[0].raw == 1536 && p[1].raw == -2304);
  UT_CHECK(f.to_local(p)[0] == 1.5 && f.to_local(p)[1] == -2.25);
  /* range: +-2^26 quanta inclusive */
  const double edge = std::ldexp(double(lim), f.quantum_log2);
  UT_CHECK(f.to_lattice(litestl::math::double3(edge, -edge, 0), p) && p[0].raw == lim && p[1].raw == -lim);
  UT_CHECK(!f.to_lattice(litestl::math::double3(edge + f.quantum(), 0, 0), p));
  UT_CHECK(!f.to_lattice(litestl::math::double3(NAN, 0, 0), p));
  UT_CHECK(!f.to_lattice(litestl::math::double3(0, INFINITY, 0), p));
  /* round trip of random in-range lattice points */
  SplitMix64 r(1);
  for (int i = 0; i < 10000; i++) {
    const I3 v = rand_i3(r, lim);
    const Point<Lattice> q = mk<Lattice>(v);
    Point<Lattice> back;
    UT_CHECK(f.to_lattice(f.to_local(q), back) && lex_compare<Lattice>(back, q) == 0 && TileFrame::in_range(q));
  }
}

template <CoordType C> static void test_primitives_vs_oracle()
{
  SplitMix64 r(2);
  const int64_t lim = TileFrame::kLatticeLimit;
  int bad = 0;
  for (int i = 0; i < 20000; i++) {
    /* mix full-range and tiny grids (the latter hit exact zeros often) */
    const int64_t l = (i % 3 == 0) ? 3 : lim;
    const I3 a = rand_i3(r, l), b = rand_i3(r, l), c = rand_i3(r, l), d = rand_i3(r, l);
    const int axis = int(r.below(3));
    bad += vol3<C>(mk<C>(a), mk<C>(b), mk<C>(c), mk<C>(d)) != oracle_vol3(a, b, c, d);
    bad += orient2d_proj<C>(mk<C>(a), mk<C>(b), mk<C>(c), axis) != oracle_cross_k(a, b, c, axis);
  }
  UT_CHECK(bad == 0);
}

/* A polygon in a tilted integer plane: origin + i*e1 + j*e2. */
struct PlaneMap {
  I3 o, e1, e2;
  I3 at(int64_t i, int64_t j) const
  {
    return {o[0] + i * e1[0] + j * e2[0], o[1] + i * e1[1] + j * e2[1], o[2] + i * e1[2] + j * e2[2]};
  }
  I3 normal() const
  {
    return {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
  }
};

template <CoordType C> struct Poly {
  std::vector<Point<C>> pts;
  std::vector<uint32_t> off{0};
  void add_loop(const PlaneMap &m, const std::vector<std::array<int64_t, 2>> &ij)
  {
    for (auto &q : ij) {
      pts.push_back(mk<C>(m.at(q[0], q[1])));
    }
    off.push_back(uint32_t(pts.size()));
  }
  PolyView<C> view() const
  {
    return {std::span<const Point<C>>(pts), std::span<const uint32_t>(off)};
  }
};

template <CoordType C> static void test_face_orientation()
{
  /* concave outer loop (CCW in ij) with a CW hole; hole listed first */
  const std::vector<std::array<int64_t, 2>> outer = {{0, 0}, {10, 0}, {10, 10}, {5, 4}, {0, 10}};
  const std::vector<std::array<int64_t, 2>> hole = {{2, 2}, {2, 3}, {3, 3}, {3, 2}};
  const PlaneMap maps[] = {
      {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}},    /* xy plane, normal +z */
      {{5, -3, 7}, {0, 0, 1}, {1, 0, 0}},   /* normal +y */
      {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}},    /* normal -z */
      {{1, 2, 3}, {2, -1, 1}, {1, 3, -2}},  /* tilted */
      {{-9, 4, 0}, {-3, 1, 5}, {2, 2, -1}}, /* tilted */
  };
  for (const PlaneMap &m : maps) {
    Poly<C> f;
    f.add_loop(m, hole);
    f.add_loop(m, outer);
    const PolyView<C> v = f.view();
    const int axis = project_axis<C>(v);
    const I3 n = m.normal();
    UT_CHECK(axis >= 0 && n[axis] != 0);
    /* the dominant axis really is the largest |normal component| */
    for (int k = 0; k < 3; k++) {
      UT_CHECK(std::llabs(n[k]) <= std::llabs(n[axis]));
    }
    UT_CHECK(outer_loop_proj<C>(v, axis) == 1);
    const int expect = n[axis] > 0 ? 1 : -1; /* outer loop CCW about +normal */
    UT_CHECK(face_front_sign<C>(v, axis) == expect);
    UT_CHECK(loop_orientation_proj<C>(v.loop(1), axis) == expect);
    UT_CHECK(loop_orientation_proj<C>(v.loop(0), axis) == -expect);
    /* point classification */
    UT_CHECK(point_in_poly_proj<C>(mk<C>(m.at(1, 1)), v, axis) == PointClass::Inside);
    UT_CHECK(point_in_poly_proj<C>(mk<C>(m.at(5, 8)), v, axis) == PointClass::Outside); /* in the notch */
    UT_CHECK(point_in_poly_proj<C>(mk<C>(m.at(2, 2)), v, axis) == PointClass::Boundary);
    UT_CHECK(point_in_poly_proj<C>(mk<C>(m.at(5, 0)), v, axis) == PointClass::Boundary);
    UT_CHECK(point_in_poly_proj<C>(mk<C>(m.at(11, 5)), v, axis) == PointClass::Outside);
  }
  /* a collinear loop is degenerate in every projection */
  Poly<C> d;
  d.add_loop(maps[3], {{0, 0}, {1, 1}, {3, 3}});
  UT_CHECK(project_axis<C>(d.view()) == -1);
  for (int k = 0; k < 3; k++) {
    UT_CHECK(loop_orientation_proj<C>(d.view().loop(0), k) == 0);
  }
}

/* Fan-winding oracle for point-in-polygon on a small integer grid (2D). */
static int fan_winding2(const std::vector<std::vector<std::array<int64_t, 2>>> &loops, int64_t px, int64_t py, bool &boundary)
{
  boundary = false;
  int w = 0;
  for (auto &L : loops) {
    const size_t m = L.size();
    for (size_t i = 0; i < m; i++) {
      const auto a = L[i], b = L[(i + 1) % m];
      const int64_t cr = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
      if (cr == 0 && std::min(a[0], b[0]) <= px && px <= std::max(a[0], b[0]) && std::min(a[1], b[1]) <= py &&
          py <= std::max(a[1], b[1]))
      {
        boundary = true;
      }
      /* winding by upward/downward crossings of the +x ray */
      if (a[1] <= py && b[1] > py && cr > 0) {
        w++;
      }
      if (a[1] > py && b[1] <= py && cr < 0) {
        w--;
      }
    }
  }
  return w;
}

template <CoordType C> static void test_point_in_poly_random()
{
  SplitMix64 r(3);
  int bad = 0;
  for (int it = 0; it < 300; it++) {
    /* random star-shaped CCW polygon about (0,0) on a small grid, plus an
     * axis-aligned CW square hole near the centre */
    std::vector<std::vector<std::array<int64_t, 2>>> loops(1);
    const int n = 3 + int(r.below(9));
    for (int i = 0; i < n; i++) {
      const double ang = 2 * std::numbers::pi * (i + 0.2 + 0.6 * r.unit()) / n;
      const double rad = 6 + double(r.below(8));
      loops[0].push_back({int64_t(std::lround(rad * std::cos(ang))), int64_t(std::lround(rad * std::sin(ang)))});
    }
    bool simple = true;
    for (int i = 0; i < n; i++) {
      const auto a = loops[0][i], b = loops[0][(i + 1) % n];
      if ((a[0] == b[0] && a[1] == b[1]) || a[0] * b[1] - a[1] * b[0] <= 0) {
        simple = false; /* must stay strictly star-shaped */
      }
    }
    if (!simple) {
      continue;
    }
    if (it % 2) {
      loops.push_back({{-1, -1}, {-1, 1}, {1, 1}, {1, -1}});
    }
    const int axis = int(r.below(3));
    PlaneMap m{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    m.e1[(axis + 1) % 3] = 1;
    m.e2[(axis + 2) % 3] = 1;
    Poly<C> f;
    for (auto &L : loops) {
      f.add_loop(m, L);
    }
    for (int64_t x = -15; x <= 15; x++) {
      for (int64_t y = -15; y <= 15; y++) {
        bool on;
        const int w = fan_winding2(loops, x, y, on);
        const PointClass expect = on ? PointClass::Boundary : (w != 0 ? PointClass::Inside : PointClass::Outside);
        bad += point_in_poly_proj<C>(mk<C>(m.at(x, y)), f.view(), axis) != expect;
      }
    }
  }
  UT_CHECK(bad == 0);
}

/* Parametric oracle for segment intersection on small grids (2D). */
static SegIsect oracle_seg(std::array<int64_t, 2> a, std::array<int64_t, 2> b, std::array<int64_t, 2> c, std::array<int64_t, 2> d)
{
  auto cross = [](int64_t x0, int64_t y0, int64_t x1, int64_t y1) { return x0 * y1 - y0 * x1; };
  const int64_t rx = b[0] - a[0], ry = b[1] - a[1], sx = d[0] - c[0], sy = d[1] - c[1];
  const int64_t den = cross(rx, ry, sx, sy);
  const int64_t qpx = c[0] - a[0], qpy = c[1] - a[1];
  if (den == 0) {
    if (cross(qpx, qpy, rx, ry) != 0) {
      return SegIsect::None;
    }
    /* collinear: 1D overlap along the dominant axis */
    const int ax = (std::llabs(rx) + std::llabs(sx) >= std::llabs(ry) + std::llabs(sy)) ? 0 : 1;
    const int64_t s0 = std::min(a[ax], b[ax]), s1 = std::max(a[ax], b[ax]);
    const int64_t t0 = std::min(c[ax], d[ax]), t1 = std::max(c[ax], d[ax]);
    const int64_t lo = std::max(s0, t0), hi = std::min(s1, t1);
    return lo > hi ? SegIsect::None : (lo == hi ? SegIsect::Touch : SegIsect::Overlap);
  }
  int64_t tn = cross(qpx, qpy, sx, sy), un = cross(qpx, qpy, rx, ry), dd = den;
  if (dd < 0) {
    tn = -tn, un = -un, dd = -dd;
  }
  if (tn < 0 || tn > dd || un < 0 || un > dd) {
    return SegIsect::None;
  }
  return (tn == 0 || tn == dd || un == 0 || un == dd) ? SegIsect::Touch : SegIsect::Proper;
}

template <CoordType C> static void test_segments_random()
{
  SplitMix64 r(4);
  int bad = 0;
  for (int it = 0; it < 50000; it++) {
    auto p = [&] { return std::array<int64_t, 2>{r.range(-3, 3), r.range(-3, 3)}; };
    auto a = p(), b = p(), c = p(), d = p();
    if ((a == b) || (c == d)) {
      continue;
    }
    const int axis = int(r.below(3));
    PlaneMap m{{1, 2, 3}, {0, 0, 0}, {0, 0, 0}};
    m.e1[(axis + 1) % 3] = 1;
    m.e2[(axis + 2) % 3] = 1;
    const SegIsect got = segments_intersect_proj<C>(mk<C>(m.at(a[0], a[1])), mk<C>(m.at(b[0], b[1])),
                                                    mk<C>(m.at(c[0], c[1])), mk<C>(m.at(d[0], d[1])), axis);
    bad += got != oracle_seg(a, b, c, d);
  }
  UT_CHECK(bad == 0);
}

/* Exact radial oracle: angle class from exact sin/cos signs of the effective
 * directions perpendicular to the axis u, computed with dot products. */
static int oracle_radial_class(const I3 &v0, const I3 &v1, const I3 &qr, int sr, const I3 &qh, int sh)
{
  BigInt u[3], dr[3], dh[3];
  for (int i = 0; i < 3; i++) {
    u[i] = B(v1[i]) - B(v0[i]);
    dr[i] = (B(qr[i]) - B(v0[i])) * B(sr);
    dh[i] = (B(qh[i]) - B(v0[i])) * B(sh);
  }
  auto dot = [](const BigInt *a, const BigInt *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
  const BigInt cx = dr[1] * dh[2] - dr[2] * dh[1], cy = dr[2] * dh[0] - dr[0] * dh[2], cz = dr[0] * dh[1] - dr[1] * dh[0];
  const int sinv = (u[0] * cx + u[1] * cy + u[2] * cz).sign();
  const int cosv = (dot(dr, dh) * dot(u, u) - dot(dr, u) * dot(dh, u)).sign();
  if (sinv > 0) {
    return 1;
  }
  if (sinv < 0) {
    return 3;
  }
  return cosv > 0 ? 0 : 2;
}

template <CoordType C> static void test_radial_vs_oracle()
{
  SplitMix64 r(5);
  int bad = 0, degenerate = 0;
  for (int it = 0; it < 20000; it++) {
    const I3 v0 = rand_i3(r, 1000);
    I3 v1 = rand_i3(r, 1000);
    if (v1 == v0) {
      continue;
    }
    /* q = v0 + alpha*A + beta*B + gamma*u with tiny coefficients: many exact
     * coplanar and opposite cases */
    const I3 A = rand_i3(r, 50), Bv = rand_i3(r, 50);
    auto q = [&] {
      const int64_t al = r.range(-2, 2), be = r.range(-2, 2), ga = r.range(-2, 2);
      I3 out;
      for (int i = 0; i < 3; i++) {
        out[i] = v0[i] + al * A[i] + be * Bv[i] + ga * (v1[i] - v0[i]);
      }
      return out;
    };
    const I3 qr = q(), qa = q(), qb = q();
    auto collinear = [&](const I3 &x) {
      for (int k = 0; k < 3; k++) {
        if (oracle_cross_k(v0, v1, x, k) != 0) {
          return false;
        }
      }
      return true;
    };
    if (collinear(qr) || collinear(qa) || collinear(qb)) {
      continue;
    }
    const int sr = r.coin() ? 1 : -1, sa = r.coin() ? 1 : -1, sb = r.coin() ? 1 : -1;
    const HalfPlane<C> hr{mk<C>(qr), sr}, ha{mk<C>(qa), sa}, hb{mk<C>(qb), sb};
    const int ca = radial_class<C>(mk<C>(v0), mk<C>(v1), hr, ha);
    const int oa = oracle_radial_class(v0, v1, qr, sr, qa, sa);
    bad += ca != oa;
    /* comparison: oracle via classes, then sin of the difference */
    const int ob = oracle_radial_class(v0, v1, qr, sr, qb, sb);
    int expect;
    if (oa != ob) {
      expect = oa < ob ? -1 : 1;
    }
    else if (oa == 0 || oa == 2) {
      expect = 0;
    }
    else {
      const int s = oracle_radial_class(v0, v1, qa, sa, qb, sb); /* b relative to a */
      expect = s == 1 ? -1 : (s == 3 ? 1 : 0);
      degenerate += expect == 0;
    }
    bad += radial_compare<C>(mk<C>(v0), mk<C>(v1), hr, ha, hb) != expect;
  }
  UT_CHECK(bad == 0);
  UT_CHECK(degenerate > 0); /* the generator does produce exact ties */
}

/* ---------------------------------------------------------------------- */
/* Ray winding on closed prisms built from 2D loops (outer CCW, holes CW). */

template <CoordType C> struct Tris {
  std::vector<Point<C>> t;
  void add(const I3 &a, const I3 &b, const I3 &c)
  {
    t.push_back(mk<C>(a));
    t.push_back(mk<C>(b));
    t.push_back(mk<C>(c));
  }
  void prism(const std::vector<std::vector<std::array<int64_t, 2>>> &loops, int64_t z0, int64_t z1, bool inward = false)
  {
    for (auto &L : loops) {
      const size_t m = L.size();
      for (size_t i = 1; i + 1 < m; i++) { /* fan per loop: top (+z) and bottom (-z) */
        const I3 a0{L[0][0], L[0][1], z0}, b0{L[i][0], L[i][1], z0}, c0{L[i + 1][0], L[i + 1][1], z0};
        const I3 a1{L[0][0], L[0][1], z1}, b1{L[i][0], L[i][1], z1}, c1{L[i + 1][0], L[i + 1][1], z1};
        if (!inward) {
          add(a1, b1, c1);
          add(a0, c0, b0);
        }
        else {
          add(a1, c1, b1);
          add(a0, b0, c0);
        }
      }
      for (size_t i = 0; i < m; i++) {
        const auto a = L[i], b = L[(i + 1) % m];
        const I3 a0{a[0], a[1], z0}, b0{b[0], b[1], z0}, b1{b[0], b[1], z1}, a1{a[0], a[1], z1};
        if (!inward) {
          add(a0, b0, b1);
          add(a0, b1, a1);
        }
        else {
          add(a0, b1, b0);
          add(a0, a1, b1);
        }
      }
    }
  }
  std::span<const Point<C>> view() const
  {
    return std::span<const Point<C>>(t);
  }
};

template <CoordType C> static void check_winding(const Tris<C> &s, const I3 &p, int expect, int &bad)
{
  const RayResult res = ray_winding<C>(mk<C>(p), s.view());
  if (res.status != RayStatus::Ok || res.winding != expect) {
    bad++;
    fprintf(stderr, "winding at (%lld %lld %lld): status %d winding %d expected %d\n", (long long)p[0], (long long)p[1],
            (long long)p[2], int(res.status), res.winding, expect);
  }
}

template <CoordType C> static void test_ray_winding()
{
  int bad = 0;
  using L2 = std::vector<std::vector<std::array<int64_t, 2>>>;
  const std::vector<std::array<int64_t, 2>> sq10 = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
  /* unit cube (side 10), grid-aligned probe points */
  {
    Tris<C> s;
    s.prism(L2{sq10}, 0, 10);
    for (int64_t x = -2; x <= 12; x += 2) {
      for (int64_t y = -2; y <= 12; y += 3) {
        for (int64_t z = -1; z <= 11; z += 3) {
          if (x == 0 || x == 10 || y == 0 || y == 10 || z == 0 || z == 10) {
            continue;
          }
          const bool in = x > 0 && x < 10 && y > 0 && y < 10 && z > 0 && z < 10;
          check_winding<C>(s, {x, y, z}, in ? 1 : 0, bad);
        }
      }
    }
    /* on the surface */
    UT_CHECK(ray_winding<C>(mk<C>({5, 5, 10}), s.view()).status == RayStatus::Boundary); /* on a fan diagonal */
    UT_CHECK(ray_winding<C>(mk<C>({2, 7, 10}), s.view()).status == RayStatus::Boundary);
    UT_CHECK(ray_winding<C>(mk<C>({0, 0, 0}), s.view()).status == RayStatus::Boundary);  /* a vertex */
    UT_CHECK(ray_winding<C>(mk<C>({10, 4, 6}), s.view()).status == RayStatus::Boundary); /* a side */
  }
  /* nested cubes: outer outward, inner inward (a cavity) */
  {
    Tris<C> s;
    s.prism(L2{sq10}, 0, 10);
    s.prism(L2{{{3, 3}, {7, 3}, {7, 7}, {3, 7}}}, 3, 7, true);
    check_winding<C>(s, {1, 1, 1}, 1, bad);
    check_winding<C>(s, {5, 5, 5}, 0, bad);
    check_winding<C>(s, {5, 5, 8}, 1, bad);
    check_winding<C>(s, {15, 5, 5}, 0, bad);
  }
  /* tunnel: a square frame (face with a hole) extruded */
  {
    Tris<C> s;
    s.prism(L2{sq10, {{3, 3}, {3, 7}, {7, 7}, {7, 3}}}, 0, 10);
    check_winding<C>(s, {1, 5, 5}, 1, bad);
    check_winding<C>(s, {5, 5, 5}, 0, bad);   /* in the tunnel */
    check_winding<C>(s, {5, 5, -5}, 0, bad);  /* below the tunnel */
    check_winding<C>(s, {8, 9, 2}, 1, bad);
  }
  /* L-shaped prism: concave caps, so fan triangles overlap with signs */
  {
    Tris<C> s;
    s.prism(L2{{{0, 0}, {10, 0}, {10, 4}, {4, 4}, {4, 10}, {0, 10}}}, 0, 6);
    check_winding<C>(s, {2, 8, 3}, 1, bad);
    check_winding<C>(s, {8, 2, 3}, 1, bad);
    check_winding<C>(s, {8, 8, 3}, 0, bad); /* in the notch */
    check_winding<C>(s, {2, 2, 7}, 0, bad);
  }
  /* forced retry: a triangle whose vertex lies exactly on the first ray */
  {
    Tris<C> s;
    const I3 p{1, 2, 3};
    const auto &d = detail::kRayDirs[0];
    const I3 v{p[0] + d[0], p[1] + d[1], p[2] + d[2]};
    s.add(v, {v[0], v[1] + 1000, v[2] - 1000}, {v[0], v[1] - 1000, v[2] - 1000});
    s.add(v, {v[0], v[1] - 1000, v[2] + 1000}, {v[0], v[1] + 1000, v[2] + 1000});
    const RayResult res = ray_winding<C>(mk<C>(p), s.view());
    UT_CHECK(res.status == RayStatus::Ok && res.retries >= 1 && res.winding == 0);
  }
  /* interior test points scaled by 4 */
  {
    Tris<C> s;
    s.prism(L2{sq10}, 0, 10);
    Point<C> p4;
    UT_CHECK(interior_point4<C>(mk<C>({1, 1, 1}), mk<C>({3, 1, 1}), mk<C>({1, 5, 9}), p4));
    const RayResult res = ray_winding<C>(p4, s.view(), 2);
    UT_CHECK(res.status == RayStatus::Ok && res.winding == 1);
  }
  UT_CHECK(bad == 0);
}

static void test_interior_point_inexact()
{
  Point<double> out;
  UT_CHECK(!interior_point4<double>(Point<double>(1.0, 0, 0), Point<double>(std::ldexp(1.0, -60), 0, 0),
                                    Point<double>(0, 0, 0), out));
  UT_CHECK(interior_point4<double>(Point<double>(0.5, 0, 0), Point<double>(0.25, 0, 0), Point<double>(1, 0, 0), out) &&
           out[0] == 2.75);
  /* Lattice range: scaled interior points beyond 2^30 are refused */
  const int64_t big = int64_t(1) << 29;
  Point<Lattice> o;
  UT_CHECK(!interior_point4<Lattice>(Point<Lattice>(Lattice(big), Lattice(0), Lattice(0)),
                                     Point<Lattice>(Lattice(big), Lattice(0), Lattice(0)),
                                     Point<Lattice>(Lattice(big), Lattice(0), Lattice(0)), o));
}

template <CoordType C> static void run_all()
{
  test_primitives_vs_oracle<C>();
  test_face_orientation<C>();
  test_point_in_poly_random<C>();
  test_segments_random<C>();
  test_radial_vs_oracle<C>();
  test_ray_winding<C>();
}

int main()
{
  test_lattice_and_frame();
  run_all<Lattice>();
  run_all<double>();
  test_interior_point_inexact();
  return ut_test_end();
}
