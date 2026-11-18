/* geom/cdt: canonical exact CDT (plan P2 amendment A1 §12.2, W2 test row).
 *
 * Inputs are integer (u, w) polygons embedded in the plane that drops axis k,
 * with random heights along k (so faces are non-planar but projectable). The
 * oracles work on the integers, independently of the code under test. */

#include "ubertopo/geom/cdt/cdt.h"

#include "support/bigint_oracle.h"
#include "support/splitmix64.h"
#include "ut_test.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <vector>

using namespace ubertopo;
using namespace ubertopo::geom;
using litestl::test::BigInt;
using litestl::test::SplitMix64;

using UV = std::array<int64_t, 2>;
using Loops = std::vector<std::vector<UV>>;

struct Case {
  Loops loops;
  int axis = 2;
  std::vector<int64_t> heights; /* per corner */
};

template <CoordType C> Point<C> lift(const UV &q, int axis, int64_t h)
{
  int64_t v[3];
  v[axis] = h;
  v[(axis + 1) % 3] = q[0];
  v[(axis + 2) % 3] = q[1];
  if constexpr (std::is_same_v<C, Lattice>) {
    return Point<C>(Lattice(v[0]), Lattice(v[1]), Lattice(v[2]));
  }
  else {
    return Point<C>(std::ldexp(double(v[0]), -8), std::ldexp(double(v[1]), -8), std::ldexp(double(v[2]), -8));
  }
}

template <CoordType C> struct Built {
  std::vector<Point<C>> pts;
  std::vector<uint32_t> off{0};
  std::vector<UV> uv;
  PolyView<C> view() const
  {
    return {std::span<const Point<C>>(pts), std::span<const uint32_t>(off)};
  }
};

template <CoordType C> Built<C> build(const Case &c)
{
  Built<C> b;
  size_t k = 0;
  for (auto &l : c.loops) {
    for (auto &q : l) {
      b.pts.push_back(lift<C>(q, c.axis, c.heights.empty() ? 0 : c.heights[k]));
      b.uv.push_back(q);
      k++;
    }
    b.off.push_back(uint32_t(b.pts.size()));
  }
  return b;
}

static int64_t cross(const UV &a, const UV &b, const UV &c)
{
  return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}
static int sgn(int64_t x)
{
  return x > 0 ? 1 : (x < 0 ? -1 : 0);
}

/* proper or touching intersection of closed segments (small integers) */
static bool seg_meet(const UV &a, const UV &b, const UV &c, const UV &d)
{
  const int d1 = sgn(cross(a, b, c)), d2 = sgn(cross(a, b, d)), d3 = sgn(cross(c, d, a)), d4 = sgn(cross(c, d, b));
  if (d1 * d2 < 0 && d3 * d4 < 0) {
    return true;
  }
  auto on = [](const UV &p, const UV &q, const UV &r) {
    return cross(p, q, r) == 0 && std::min(p[0], q[0]) <= r[0] && r[0] <= std::max(p[0], q[0]) &&
           std::min(p[1], q[1]) <= r[1] && r[1] <= std::max(p[1], q[1]);
  };
  return on(a, b, c) || on(a, b, d) || on(c, d, a) || on(c, d, b);
}

/* rank-perturbed incircle oracle: 4x4 lifted determinant and cofactors */
static int oracle_incircle(const UV &a, const UV &b, const UV &c, const UV &d, const uint32_t (&rk)[4])
{
  const UV p[4] = {a, b, c, d};
  std::vector<std::vector<BigInt>> m;
  for (auto &q : p) {
    const BigInt x = BigInt::from_i64(q[0]), y = BigInt::from_i64(q[1]);
    m.push_back({x, y, x * x + y * y, BigInt::from_i64(1)});
  }
  const int d0 = BigInt::det(m).sign();
  if (d0) {
    return d0;
  }
  int order[4] = {0, 1, 2, 3};
  std::sort(order, order + 4, [&](int x, int y) { return rk[x] < rk[y]; });
  for (int k = 0; k < 4; k++) {
    std::vector<std::vector<BigInt>> mi;
    for (int r = 0; r < 4; r++) {
      if (r != order[k]) {
        mi.push_back({m[r][0], m[r][1], m[r][3]});
      }
    }
    const int s = (order[k] % 2 == 0 ? 1 : -1) * BigInt::det(mi).sign();
    if (s) {
      return s;
    }
  }
  return 0;
}

/* Full independent check of a CDT result. Returns the number of problems. */
template <CoordType C> static int check_cdt(const Case &c, const Built<C> &b, const litestl::util::Vector<Triangle> &tris)
{
  int bad = 0;
  const auto &uv = b.uv;
  const uint32_t n = uint32_t(uv.size());
  /* outer orientation (in the drop_axis frame) */
  const int so = loop_orientation_proj<C>(b.view().loop(outer_loop_proj<C>(b.view(), c.axis)), c.axis);
  /* validity: front winding, non-degenerate */
  std::map<std::pair<uint32_t, uint32_t>, int> dir; /* directed edge -> count */
  BigInt area2;
  for (const Triangle &t : tris) {
    const int64_t cr = cross(uv[t[0]], uv[t[1]], uv[t[2]]);
    if (sgn(cr) != so) {
      bad++;
    }
    area2 = area2 + BigInt::from_i64(cr) * BigInt::from_i64(so);
    for (int i = 0; i < 3; i++) {
      dir[{t[i], t[(i + 1) % 3]}]++;
    }
  }
  /* loop edges: in exactly one triangle, in loop direction; interior edges paired */
  auto count = [&](uint32_t x, uint32_t y) {
    auto it = dir.find({x, y});
    return it == dir.end() ? 0 : it->second;
  };
  std::set<std::pair<uint32_t, uint32_t>> loop_edges;
  BigInt poly2;
  for (uint32_t l = 0; l + 1 < b.off.size(); l++) {
    const uint32_t s = b.off[l], m = b.off[l + 1] - s;
    for (uint32_t j = 0; j < m; j++) {
      const uint32_t x = s + j, y = s + (j + 1) % m;
      loop_edges.insert({x, y});
      if (count(x, y) != 1 || count(y, x) != 0) {
        bad++;
      }
      poly2 = poly2 + BigInt::from_i64(uv[x][0] * uv[y][1] - uv[y][0] * uv[x][1]) * BigInt::from_i64(so);
    }
  }
  for (auto &[e, k] : dir) {
    if (k != 1) {
      bad++;
    }
    if (!loop_edges.count(e) && count(e.second, e.first) == 0) {
      bad++; /* an interior edge without its twin */
    }
  }
  if (!(area2 == poly2)) {
    bad++;
  }
  /* global constrained Delaunay: no vertex strictly inside a triangle's
   * perturbed circumcircle is visible from its centroid */
  std::vector<uint32_t> lexo(n);
  for (uint32_t i = 0; i < n; i++) {
    lexo[i] = i;
  }
  std::sort(lexo.begin(), lexo.end(), [&](uint32_t x, uint32_t y) { return uv[x] < uv[y]; });
  std::vector<uint32_t> rk(n);
  for (uint32_t i = 0; i < n; i++) {
    rk[lexo[i]] = i;
  }
  for (const Triangle &t : tris) {
    /* ccw in (u,w) for the incircle convention */
    UV a = uv[t[0]], bb = uv[t[1]], cc = uv[t[2]];
    uint32_t ra = rk[t[0]], rb = rk[t[1]], rc = rk[t[2]];
    if (cross(a, bb, cc) < 0) {
      std::swap(bb, cc);
      std::swap(rb, rc);
    }
    const UV cen3 = {a[0] + bb[0] + cc[0], a[1] + bb[1] + cc[1]};
    for (uint32_t v = 0; v < n; v++) {
      if (v == t[0] || v == t[1] || v == t[2]) {
        continue;
      }
      const uint32_t r4[4] = {ra, rb, rc, rk[v]};
      if (oracle_incircle(a, bb, cc, uv[v], r4) <= 0) {
        continue;
      }
      /* visible: the segment 3v -> 3*centroid meets no loop edge (scaled by 3) */
      const UV v3 = {3 * uv[v][0], 3 * uv[v][1]};
      bool blocked = false;
      for (auto &e : loop_edges) {
        const UV p3 = {3 * uv[e.first][0], 3 * uv[e.first][1]}, q3 = {3 * uv[e.second][0], 3 * uv[e.second][1]};
        if ((e.first == v || e.second == v)) {
          continue;
        }
        blocked = blocked || seg_meet(v3, cen3, p3, q3);
      }
      if (!blocked) {
        bad++;
      }
    }
  }
  return bad;
}

/* canonical comparison by projected position (indices change with loop heads) */
template <CoordType C>
static std::vector<std::array<UV, 3>> by_position(const Built<C> &b, const litestl::util::Vector<Triangle> &tris)
{
  std::vector<std::array<UV, 3>> out;
  for (const Triangle &t : tris) {
    std::array<UV, 3> x = {b.uv[t[0]], b.uv[t[1]], b.uv[t[2]]};
    while (x[0] > x[1] || x[0] > x[2]) {
      x = {x[1], x[2], x[0]};
    }
    out.push_back(x);
  }
  std::sort(out.begin(), out.end());
  return out;
}

static std::vector<UV> star_polygon(SplitMix64 &r, int n, int64_t rmin, int64_t rmax, int64_t cx = 0, int64_t cy = 0)
{
  for (int attempt = 0; attempt < 1000; attempt++) {
    std::vector<UV> p;
    for (int i = 0; i < n; i++) {
      const double ang = 2 * M_PI * (i + 0.15 + 0.7 * r.unit()) / n;
      const double rad = double(r.range(rmin, rmax));
      p.push_back({cx + int64_t(std::lround(rad * std::cos(ang))), cy + int64_t(std::lround(rad * std::sin(ang)))});
    }
    bool ok = true;
    for (int i = 0; i < n && ok; i++) {
      const UV a = {p[i][0] - cx, p[i][1] - cy}, b = {p[(i + 1) % n][0] - cx, p[(i + 1) % n][1] - cy};
      ok = a[0] * b[1] - a[1] * b[0] > 0; /* strictly star-shaped about the centre: simple, CCW */
    }
    if (ok) {
      return p;
    }
  }
  return {};
}

/* brute-force validity of a polygon with holes (small integers): no two
 * edges meet except adjacent ones at their shared vertex, and every hole
 * vertex is strictly inside the outer loop (loop 0) */
static bool valid_polygon(const Loops &loops)
{
  struct E {
    UV a, b;
    size_t loop, i, m;
  };
  std::vector<E> es;
  for (size_t l = 0; l < loops.size(); l++) {
    const size_t m = loops[l].size();
    for (size_t i = 0; i < m; i++) {
      es.push_back({loops[l][i], loops[l][(i + 1) % m], l, i, m});
    }
  }
  for (size_t x = 0; x < es.size(); x++) {
    for (size_t y = x + 1; y < es.size(); y++) {
      const bool adjacent = es[x].loop == es[y].loop &&
                            ((es[x].i + 1) % es[x].m == es[y].i || (es[y].i + 1) % es[y].m == es[x].i);
      if (!adjacent && seg_meet(es[x].a, es[x].b, es[y].a, es[y].b)) {
        return false;
      }
    }
  }
  for (size_t l = 1; l < loops.size(); l++) {
    for (const UV &p : loops[l]) {
      int w = 0;
      const auto &o = loops[0];
      for (size_t i = 0; i < o.size(); i++) {
        const UV a = o[i], b = o[(i + 1) % o.size()];
        const int64_t cr = cross(a, b, p);
        if (a[1] <= p[1] && b[1] > p[1] && cr > 0) {
          w++;
        }
        if (a[1] > p[1] && b[1] <= p[1] && cr < 0) {
          w--;
        }
      }
      if (w == 0) {
        return false;
      }
    }
  }
  return true;
}

static void reverse_loop(std::vector<UV> &l)
{
  std::reverse(l.begin(), l.end());
}

template <CoordType C> static void test_random_polygons()
{
  SplitMix64 r(21);
  int bad = 0, total = 0, invalid = 0;
  for (int it = 0; it < 300; it++) {
    Case c;
    c.axis = int(r.below(3));
    const int n = 3 + int(r.below(it < 100 ? 8 : 40));
    auto outer = star_polygon(r, n, 20, 60);
    if (outer.empty()) {
      continue;
    }
    c.loops.push_back(outer);
    if (it % 3 != 0) {
      /* one or two CW holes near the centre (well inside: radius < 18) */
      auto h = star_polygon(r, 3 + int(r.below(6)), 3, 7, -9, 0);
      reverse_loop(h);
      c.loops.push_back(h);
      if (it % 3 == 2) {
        auto h2 = star_polygon(r, 3 + int(r.below(5)), 3, 6, 9, 2);
        reverse_loop(h2);
        c.loops.push_back(h2);
      }
    }
    if (!valid_polygon(c.loops)) {
      /* a hole reaching an outer edge: the CDT must reject it */
      const Built<C> bi = build<C>(c);
      litestl::util::Vector<Triangle> ti;
      bad += cdt_proj<C>(bi.view(), c.axis, ti) == CdtStatus::Ok;
      invalid++;
      continue;
    }
    if (it % 2) {
      for (auto &l : c.loops) {
        reverse_loop(l); /* mirrored: outer CW, holes CCW in this frame */
      }
    }
    size_t corners = 0;
    for (auto &l : c.loops) {
      corners += l.size();
    }
    for (size_t k = 0; k < corners; k++) {
      c.heights.push_back(r.range(-1000, 1000)); /* non-planar */
    }
    const Built<C> b = build<C>(c);
    litestl::util::Vector<Triangle> tris;
    if (cdt_proj<C>(b.view(), c.axis, tris) != CdtStatus::Ok) {
      fprintf(stderr, "cdt failed on a valid polygon (it %d)\n", it);
      bad++;
      continue;
    }
    total++;
    bad += check_cdt<C>(c, b, tris);
    /* uniqueness under shuffled vertex and constraint insertion orders */
    for (int sh = 0; sh < 3; sh++) {
      std::vector<uint32_t> vo(b.pts.size()), co;
      for (uint32_t i = 0; i < vo.size(); i++) {
        vo[i] = i;
      }
      for (size_t i = vo.size(); i > 1; i--) {
        std::swap(vo[i - 1], vo[r.below(i)]);
      }
      for (uint32_t i = 0; i < corners; i++) {
        co.push_back(i);
      }
      for (size_t i = co.size(); i > 1; i--) {
        std::swap(co[i - 1], co[r.below(i)]);
      }
      litestl::util::Vector<Triangle> t2;
      const CdtStatus st = cdt::detail::cdt_proj_ordered<C>(b.view(), c.axis, std::span<const uint32_t>(vo),
                                                            std::span<const uint32_t>(co), t2);
      bool same = st == CdtStatus::Ok && t2.size() == tris.size();
      for (uint32_t i = 0; same && i < tris.size(); i++) {
        same = t2[i] == tris[i];
      }
      bad += !same;
    }
    /* loop heads rotated and loops reordered: same triangles by position */
    Case c2 = c;
    for (auto &l : c2.loops) {
      std::rotate(l.begin(), l.begin() + r.below(l.size()), l.end());
    }
    std::reverse(c2.loops.begin(), c2.loops.end());
    c2.heights.clear();
    const Built<C> b2 = build<C>(c2);
    litestl::util::Vector<Triangle> t3;
    bad += cdt_proj<C>(b2.view(), c2.axis, t3) != CdtStatus::Ok || by_position(b2, t3) != by_position(b, tris);
  }
  UT_CHECK(bad == 0 && total > 200 && invalid > 0);
}

/* Grids: every 4 grid points on a square are cocircular; collinear runs and
 * a degenerate (collinear) start are everywhere. */
template <CoordType C> static void test_cocircular_grids()
{
  int bad = 0;
  for (int64_t N : {2, 3, 5, 8}) {
    Case c;
    std::vector<UV> outer;
    for (int64_t i = 0; i < N; i++) {
      outer.push_back({i, 0});
    }
    for (int64_t i = 0; i < N; i++) {
      outer.push_back({N, i});
    }
    for (int64_t i = N; i > 0; i--) {
      outer.push_back({i, N});
    }
    for (int64_t i = N; i > 0; i--) {
      outer.push_back({0, i});
    }
    c.loops.push_back(outer);
    if (N >= 5) {
      c.loops.push_back({{2, 2}, {2, 3}, {3, 3}, {3, 2}}); /* CW square hole */
    }
    const Built<C> b = build<C>(c);
    litestl::util::Vector<Triangle> tris;
    if (cdt_proj<C>(b.view(), c.axis, tris) != CdtStatus::Ok) {
      bad++;
      continue;
    }
    bad += check_cdt<C>(c, b, tris);
    /* cocircular ties: shuffled insertion orders must give the same result */
    SplitMix64 r{uint64_t(N)};
    for (int sh = 0; sh < 8; sh++) {
      std::vector<uint32_t> vo(b.pts.size()), co(b.pts.size());
      for (uint32_t i = 0; i < vo.size(); i++) {
        vo[i] = co[i] = i;
      }
      for (size_t i = vo.size(); i > 1; i--) {
        std::swap(vo[i - 1], vo[r.below(i)]);
        std::swap(co[i - 1], co[r.below(i)]);
      }
      litestl::util::Vector<Triangle> t2;
      const CdtStatus st = cdt::detail::cdt_proj_ordered<C>(b.view(), c.axis, std::span<const uint32_t>(vo),
                                                            std::span<const uint32_t>(co), t2);
      bool same = st == CdtStatus::Ok && t2.size() == tris.size();
      for (uint32_t i = 0; same && i < tris.size(); i++) {
        same = t2[i] == tris[i];
      }
      bad += !same;
    }
  }
  UT_CHECK(bad == 0);
}

template <CoordType C> static void expect_status(const Case &c, CdtStatus want, const char *what)
{
  const Built<C> b = build<C>(c);
  litestl::util::Vector<Triangle> tris;
  const CdtStatus got = cdt_proj<C>(b.view(), c.axis, tris);
  if (got != want) {
    fprintf(stderr, "%s: status %d, expected %d\n", what, int(got), int(want));
  }
  UT_CHECK(got == want && (want != CdtStatus::Ok || tris.size() > 0));
}

template <CoordType C> static void test_rejections()
{
  const std::vector<UV> sq = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
  {
    Case c;
    c.loops = {sq, {{2, 2}, {4, 2}, {4, 4}, {2, 4}}}; /* CCW hole */
    expect_status<C>(c, CdtStatus::HoleOrientation, "hole winding");
  }
  {
    Case c;
    c.loops = {sq, {{2, 2}, {2, 8}, {8, 8}, {8, 2}}, {{4, 4}, {4, 5}, {5, 5}, {5, 4}}};
    expect_status<C>(c, CdtStatus::NotProjectable, "nested hole");
  }
  {
    Case c;
    c.loops = {sq, {{12, 2}, {12, 4}, {14, 4}, {14, 2}}};
    expect_status<C>(c, CdtStatus::NotProjectable, "hole outside");
  }
  {
    Case c;
    c.loops = {sq, {{8, 2}, {8, 4}, {12, 4}, {12, 2}}};
    expect_status<C>(c, CdtStatus::NotProjectable, "hole crossing the outer loop");
  }
  {
    Case c;
    c.loops = {sq, {{5, 0}, {6, 2}, {7, 2}}}; /* CW hole with a corner on the outer edge */
    expect_status<C>(c, CdtStatus::NotProjectable, "hole touching the outer loop");
  }
  {
    Case c;
    c.loops = {{{0, 0}, {10, 0}, {10, 10}, {5, 0}, {0, 10}}}; /* self-touching (a vertex on an edge) */
    expect_status<C>(c, CdtStatus::NotProjectable, "self-touching loop");
  }
  {
    Case c;
    c.loops = {{{0, 0}, {10, 0}, {0, 10}, {10, 10}}}; /* bow tie */
    expect_status<C>(c, CdtStatus::NotProjectable, "self-crossing loop");
  }
  {
    Case c;
    c.loops = {{{0, 0}, {4, 0}, {8, 0}}};
    expect_status<C>(c, CdtStatus::NotProjectable, "collinear loop");
  }
  {
    Case c;
    c.loops = {sq, {{3, 3}, {3, 6}, {6, 6}, {6, 3}}};
    c.loops[1][2] = {10, 10}; /* a hole corner projects onto an outer corner */
    expect_status<C>(c, CdtStatus::NotProjectable, "duplicate projected point");
  }
  {
    /* a valid concave polygon with a collinear run at its lexicographic start */
    Case c;
    c.loops = {{{0, 0}, {5, 0}, {5, 5}, {3, 2}, {0, 5}, {0, 4}, {0, 3}, {0, 2}, {0, 1}}};
    expect_status<C>(c, CdtStatus::Ok, "collinear start");
  }
}

template <CoordType C> static void test_instantiations_agree()
{
  SplitMix64 r(33);
  int bad = 0;
  for (int it = 0; it < 100; it++) {
    Case c;
    c.axis = int(r.below(3));
    auto outer = star_polygon(r, 4 + int(r.below(20)), 10, 40);
    if (outer.empty()) {
      continue;
    }
    c.loops.push_back(outer);
    const Built<Lattice> bl = build<Lattice>(c);
    const Built<double> bd = build<double>(c);
    litestl::util::Vector<Triangle> tl, td;
    bad += cdt_proj<Lattice>(bl.view(), c.axis, tl) != CdtStatus::Ok;
    bad += cdt_proj<double>(bd.view(), c.axis, td) != CdtStatus::Ok;
    bool same = tl.size() == td.size();
    for (uint32_t i = 0; same && i < tl.size(); i++) {
      same = tl[i] == td[i];
    }
    bad += !same;
  }
  UT_CHECK(bad == 0);
}

template <CoordType C> static void test_performance()
{
#ifdef NDEBUG
  const int n_star = 10000, n_arc = 4000;
  const double budget_s = 5.0;
#else
  const int n_star = 1000, n_arc = 500;
  const double budget_s = 60.0;
#endif
  SplitMix64 r(44);
  for (int which = 0; which < 2; which++) {
    Case c;
    if (which == 0) {
      c.loops.push_back(star_polygon(r, n_star, 1000000, 2000000));
    }
    else {
      /* a finely tessellated round outline: points on a circle (convex arc) */
      std::vector<UV> arc;
      for (int i = 0; i < n_arc; i++) {
        const double ang = 2 * M_PI * i / n_arc;
        arc.push_back({int64_t(std::lround(3e7 * std::cos(ang))), int64_t(std::lround(3e7 * std::sin(ang)))});
      }
      c.loops.push_back(arc);
    }
    UT_CHECK(!c.loops[0].empty());
    const Built<C> b = build<C>(c);
    litestl::util::Vector<Triangle> tris;
    const auto t0 = std::chrono::steady_clock::now();
    const CdtStatus st = cdt_proj<C>(b.view(), c.axis, tris);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("cdt perf (%s, %s): %zu points, %.3f s\n", which ? "convex arc" : "star", geom::CoordTraits<C>::key,
           b.pts.size(), s);
    UT_CHECK(st == CdtStatus::Ok && tris.size() == b.pts.size() - 2);
    UT_CHECK(s < budget_s);
  }
}

template <CoordType C> static void run_all()
{
  test_random_polygons<C>();
  test_cocircular_grids<C>();
  test_rejections<C>();
  test_performance<C>();
}

int main()
{
  run_all<Lattice>();
  run_all<double>();
  test_instantiations_agree<Lattice>();
  return ut_test_end();
}
