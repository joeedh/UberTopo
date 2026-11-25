/* build_regions(): nesting test points, and the errors of degenerate and
 * touching shells (plan 2026-11-24-incremental-regions W1). */

#include "ubertopo/topo/regions.h"

#include "litestl/util/alloc.h"
#include "support/splitmix64.h"
#include "support/topo_fixtures.h"
#include "ut_test.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
using test::I3;

namespace {

/* A builder with its own face IDs, so that faces may repeat a vertex set. */
template <geom::CoordType C> struct Fx {
  using P = geom::Point<C>;
  Builder<C> b;
  uint32_t n = 0;
  std::vector<PersistentId> fid;

  uint32_t vert(const P &p)
  {
    return b.vertex(test::pid(1000 + n++), p);
  }
  uint32_t grid(const I3 &p)
  {
    return vert(test::pt<C>(p));
  }
  /* returns the face's number */
  uint32_t face(std::vector<uint32_t> keys)
  {
    const uint32_t size = uint32_t(keys.size());
    fid.push_back(test::pid(7, 100 + fid.size()));
    b.face(fid.back(), std::span<const uint32_t>(keys), std::span<const uint32_t>(&size, 1));
    return uint32_t(fid.size()) - 1;
  }
  /* A box with front sides out. Wall @p w (0: +x, 1: +y, 2: +z) of @p centre
   * is fanned around the given vertex; NONE keeps it whole. */
  void box(const I3 &lo, const I3 &hi, std::array<uint32_t, 3> centre = {NONE, NONE, NONE})
  {
    uint32_t c[2][2][2];
    for (int x = 0; x < 2; x++) {
      for (int y = 0; y < 2; y++) {
        for (int z = 0; z < 2; z++) {
          c[x][y][z] = grid({x ? hi[0] : lo[0], y ? hi[1] : lo[1], z ? hi[2] : lo[2]});
        }
      }
    }
    const auto wall = [&](std::array<uint32_t, 4> q, uint32_t m) {
      if (m == NONE) {
        face({q[0], q[1], q[2], q[3]});
        return;
      }
      for (int i = 0; i < 4; i++) {
        face({q[i], q[(i + 1) % 4], m});
      }
    };
    wall({c[0][0][0], c[0][1][0], c[1][1][0], c[1][0][0]}, NONE);
    wall({c[0][0][1], c[1][0][1], c[1][1][1], c[0][1][1]}, centre[2]);
    wall({c[0][0][0], c[1][0][0], c[1][0][1], c[0][0][1]}, NONE);
    wall({c[0][1][0], c[0][1][1], c[1][1][1], c[1][1][0]}, centre[1]);
    wall({c[0][0][0], c[0][0][1], c[0][1][1], c[0][1][0]}, NONE);
    wall({c[1][0][0], c[1][1][0], c[1][1][1], c[1][0][1]}, centre[0]);
  }
  PersistentId side(uint32_t f, uint32_t s) const
  {
    return ident::face_side_id(fid[f], s);
  }
  /* the shell index of a set of sides: the rank of its smallest side ID among
   * the shells, given as lists of (face, side) */
  static uint32_t rank(const Fx &fx, const std::vector<std::vector<std::pair<uint32_t, uint32_t>>> &shells, size_t which)
  {
    std::vector<PersistentId> label;
    for (const auto &sh : shells) {
      PersistentId m = fx.side(sh[0].first, sh[0].second);
      for (const auto &[f, s] : sh) {
        m = std::min(m, fx.side(f, s));
      }
      label.push_back(m);
    }
    uint32_t r = 0;
    for (const PersistentId &l : label) {
      r += l < label[which];
    }
    return r;
  }
};

/* the face with the given ID */
template <geom::CoordType C> FaceRef face_by_id(const Complex<C> &k, PersistentId id)
{
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    if (k.faces.live(f) && k.face_id(FaceRef(f)) == id) {
      return FaceRef(f);
    }
  }
  return FaceRef();
}

std::vector<std::pair<uint32_t, uint32_t>> sides(uint32_t first, uint32_t n, uint32_t s)
{
  std::vector<std::pair<uint32_t, uint32_t>> out;
  for (uint32_t i = 0; i < n; i++) {
    out.push_back({first + i, s});
  }
  return out;
}

/* A sheet whose vertices are all vertices of the room around it: the test
 * point is an interior point of the sheet. */
template <geom::CoordType C> void test_interior_point()
{
  Fx<C> fx;
  const uint32_t cx = fx.grid({40, 20, 20}), cy = fx.grid({20, 40, 20}), cz = fx.grid({20, 20, 40});
  fx.box({0, 0, 0}, {40, 40, 40}, {cx, cy, cz});
  const uint32_t t = fx.face({cx, cy, cz});
  Complex<C> k;
  BuildReport br;
  RegionReport rr;
  UT_CHECK(fx.b.build(k, br) && build_regions(k, rr));
  UT_CHECK(rr.sheet_shells == 1 && rr.outer_shells == 1 && rr.cavity_shells == 1 && rr.regions == 2);
  const FaceRef f = face_by_id(k, fx.fid[t]);
  UT_CHECK(f && k.region(f, 0).i == 1 && k.region(f, 1).i == 1);
}

/* Inside another shell's box but not inside it: a block in a courtyard's
 * hole, and the same block in the ring. */
template <geom::CoordType C> void test_concave()
{
  /* flipped: the outer walls' fronts face into the ring, so the ring's shell
   * has front and back sides */
  for (int mode = 0; mode < 4; mode++) {
    const bool in_ring = mode & 1, flipped = mode & 2;
    test::GridBuilder<C> g;
    const PersistentId ring = test::pid(1), out = ident::unbounded_region_id();
    /* a square ring: floor and roof with a hole, outer and inner walls */
    g.face({{{0, 0, 0}, {0, 90, 0}, {90, 90, 0}, {90, 0, 0}}, {{30, 30, 0}, {60, 30, 0}, {60, 60, 0}, {30, 60, 0}}}, out,
           ring);
    g.face({{{0, 0, 30}, {90, 0, 30}, {90, 90, 30}, {0, 90, 30}},
            {{30, 30, 30}, {30, 60, 30}, {60, 60, 30}, {60, 30, 30}}},
           out, ring);
    const auto wall = [&](std::vector<I3> l) {
      if (flipped) {
        std::reverse(l.begin(), l.end());
      }
      g.face({l}, flipped ? ring : out, flipped ? out : ring);
    };
    wall({{0, 0, 0}, {90, 0, 0}, {90, 0, 30}, {0, 0, 30}});
    wall({{0, 90, 0}, {0, 90, 30}, {90, 90, 30}, {90, 90, 0}});
    wall({{0, 0, 0}, {0, 0, 30}, {0, 90, 30}, {0, 90, 0}});
    wall({{90, 0, 0}, {90, 90, 0}, {90, 90, 30}, {90, 0, 30}});
    g.face({{{30, 30, 0}, {30, 30, 30}, {60, 30, 30}, {60, 30, 0}}}, out, ring);
    g.face({{{30, 60, 0}, {60, 60, 0}, {60, 60, 30}, {30, 60, 30}}}, out, ring);
    g.face({{{30, 30, 0}, {30, 60, 0}, {30, 60, 30}, {30, 30, 30}}}, out, ring);
    g.face({{{60, 30, 0}, {60, 30, 30}, {60, 60, 30}, {60, 60, 0}}}, out, ring);
    const int64_t o = in_ring ? 5 : 40;
    g.box({o, o, 10}, {o + 10, o + 10, 20});
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(k, br) && build_regions(k, rr) && rr.regions == 3);
    const PersistentId ring_id = ident::region_id_from_hint(ring);
    uint32_t found = 0;
    for (uint32_t f = 0; f < k.faces.capacity(); f++) {
      /* the block's faces are the unhinted ones; their fronts face out */
      if (k.side_hint(FaceRef(f), 0) == ident::kNoId) {
        const uint32_t r = k.region(FaceRef(f), 0).i;
        UT_CHECK((r != 0) == in_ring && (!in_ring || k.regions.template get<".id">()[r] == ring_id));
        found++;
      }
    }
    UT_CHECK(found == 6);
  }
}

/* Sheets with vertices on a room's walls that aren't recorded contacts. The
 * build fails and names the sheets, in shell order; the complex is untouched. */
template <geom::CoordType C> void test_touching()
{
  Fx<C> fx;
  fx.box({0, 0, 0}, {40, 40, 40});
  fx.box({100, 0, 0}, {140, 40, 40}); /* shells on both sides of the sheets' */
  const auto sheet = [&](int64_t d) {
    const uint32_t a = fx.grid({40, 20 + d, 20}), b = fx.grid({20 + d, 40, 20}), c = fx.grid({20 + d, 20, 40});
    return fx.face({a, b, c});
  };
  const uint32_t t0 = sheet(0), t1 = sheet(-9);
  Complex<C> k;
  BuildReport br;
  RegionReport rr;
  UT_CHECK(fx.b.build(k, br));
  const PersistentId h = test::complex_hash(k);
  UT_CHECK(!build_regions(k, rr) && test::complex_hash(k) == h && k.regions_dirty);
  UT_CHECK(rr.errors == 2 && rr.issues.size() == 2 && rr.count(RegionCode::TouchingShell) == 2);
  const std::vector<std::vector<std::pair<uint32_t, uint32_t>>> shells = {
      sides(0, 6, 0), sides(0, 6, 1), sides(6, 6, 0), sides(6, 6, 1), {{t0, 0}, {t0, 1}}, {{t1, 0}, {t1, 1}}};
  uint32_t want[2] = {Fx<C>::rank(fx, shells, 4), Fx<C>::rank(fx, shells, 5)};
  std::sort(want, want + 2);
  UT_CHECK(rr.issues.size() == 2 && rr.issues[0].index == want[0] && rr.issues[1].index == want[1]);
}

/* Sheets outside a room with their test point on one of its walls: not in
 * the room's box, so they aren't tested against it and the contact isn't
 * seen. One per direction: each bound of the box counts. */
template <geom::CoordType C> void test_outside_box()
{
  for (int axis = 0; axis < 3; axis++) {
    for (const bool high : {false, true}) {
      Fx<C> fx;
      I3 p[3] = {{20, 20, 20}, {15, 15, 15}, {25, 25, 25}};
      p[0][axis] = high ? 40 : 0; /* the first vertex of the sheet, on the wall */
      for (int i = 1; i < 3; i++) {
        p[i][axis] = high ? 60 : -20;
        p[i][(axis + 1) % 3] += 5 * i;
      }
      const uint32_t a = fx.grid(p[0]), b = fx.grid(p[1]), c = fx.grid(p[2]);
      fx.box({0, 0, 0}, {40, 40, 40});
      const uint32_t t = fx.face({a, b, c});
      Complex<C> k;
      BuildReport br;
      RegionReport rr;
      UT_CHECK(fx.b.build(k, br) && build_regions(k, rr) && rr.regions == 2 && rr.ray_tests == 0);
      const FaceRef f = face_by_id(k, fx.fid[t]);
      UT_CHECK(f && k.region(f, 0).i == 0 && k.region(f, 1).i == 0);
    }
  }
}

/* A shell with paired and unpaired sides isn't a sheet, whichever come last:
 * a room with a fin on an edge of a subdivided wall. */
template <geom::CoordType C> void test_fin()
{
  for (const bool last : {false, true}) {
    Fx<C> fx;
    const uint32_t cx = fx.grid({40, 20, 20}), corner = fx.grid({40, 0, 0}), tip = fx.grid({20, 18, 13});
    if (!last) {
      fx.face({cx, corner, tip});
    }
    /* a box whose +x wall is fanned around cx, with the given corner */
    const uint32_t v[7] = {fx.grid({0, 0, 0}), fx.grid({0, 40, 0}), fx.grid({40, 40, 0}), fx.grid({0, 0, 40}),
                           fx.grid({40, 0, 40}), fx.grid({40, 40, 40}), fx.grid({0, 40, 40})};
    fx.face({v[0], v[1], v[2], corner});
    fx.face({v[3], v[4], v[5], v[6]});
    fx.face({v[0], corner, v[4], v[3]});
    fx.face({v[1], v[6], v[5], v[2]});
    fx.face({v[0], v[3], v[6], v[1]});
    const uint32_t q[4] = {corner, v[2], v[5], v[4]};
    for (int i = 0; i < 4; i++) {
      fx.face({q[i], q[(i + 1) % 4], cx});
    }
    if (last) {
      fx.face({cx, corner, tip});
    }
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(fx.b.build(k, br) && build_regions(k, rr));
    UT_CHECK(rr.sheet_shells == 0 && rr.outer_shells == 1 && rr.cavity_shells == 1 && rr.regions == 2);
  }
}

/* A projected face that doesn't triangulate (a vertical wall marked as
 * projected along z): its shells are degenerate. */
template <geom::CoordType C> void test_untriangulated()
{
  for (uint32_t which = 0; which < 6; which++) {
    Fx<C> fx;
    fx.box({0, 0, 0}, {40, 40, 40});
    fx.box({100, 0, 0}, {140, 40, 40});
    Complex<C> k;
    BuildReport br;
    UT_CHECK(fx.b.build(k, br));
    const FaceRef f = face_by_id(k, fx.fid[which]);
    auto &flags = k.faces.template get<".f.flags">();
    flags.set(f.i, with_proj_axis(flags[f.i], which < 2 ? 0 : 2));
    const PersistentId h = test::complex_hash(k);
    RegionReport rr;
    UT_CHECK(!build_regions(k, rr) && rr.errors == 2 && rr.count(RegionCode::DegenerateShell) == 2);
    UT_CHECK(test::complex_hash(k) == h);
  }
}

/* Two faces on one vertex set: both shells enclose nothing. */
template <geom::CoordType C> void test_degenerate()
{
  Fx<C> fx;
  fx.box({0, 0, 0}, {40, 40, 40});
  const uint32_t a = fx.grid({100, 0, 0}), b = fx.grid({140, 0, 0}), c = fx.grid({140, 40, 10}), d = fx.grid({100, 40, 10});
  const uint32_t f0 = fx.face({a, b, c, d}), f1 = fx.face({b, c, d, a});
  Complex<C> k;
  BuildReport br;
  RegionReport rr;
  UT_CHECK(fx.b.build(k, br) && br.count(BuildCode::RadialTie) > 0);
  const PersistentId h = test::complex_hash(k);
  UT_CHECK(!build_regions(k, rr) && test::complex_hash(k) == h);
  UT_CHECK(rr.errors == 2 && rr.count(RegionCode::DegenerateShell) == 2);
  const std::vector<std::vector<std::pair<uint32_t, uint32_t>>> shells = {
      sides(0, 6, 0), sides(0, 6, 1), {{f0, 0}, {f1, 1}}, {{f0, 1}, {f1, 0}}};
  uint32_t want[2] = {Fx<C>::rank(fx, shells, 2), Fx<C>::rank(fx, shells, 3)};
  std::sort(want, want + 2);
  UT_CHECK(rr.issues.size() == 2 && rr.issues[0].index == want[0] && rr.issues[1].index == want[1]);
}

/* The same in double, with coordinates whose products round: the summed
 * volume is noise, and the filter must not certify a sign. */
void test_rounded_volume()
{
  litestl::test::SplitMix64 r(17);
  const auto coord = [&] { return 1.0 + std::ldexp(double(r.below(uint64_t(1) << 50)), -50); };
  uint32_t failed = 0;
  for (int c = 0; c < 200; c++) {
    Fx<double> fx;
    const double z = coord(), x0 = coord(), y0 = coord(), x1 = x0 + coord(), y1 = y0 + coord();
    const uint32_t a = fx.vert({x0, y0, z}), b = fx.vert({x1, y0, z}), d = fx.vert({x1, y1, z}), e = fx.vert({x0, y1, z});
    fx.face({a, b, d, e});
    fx.face({b, d, e, a});
    Complex<double> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(fx.b.build(k, br));
    UT_CHECK(!build_regions(k, rr) && rr.count(RegionCode::DegenerateShell) == 2);
    failed += rr.errors == 2;
  }
  UT_CHECK(failed == 200);
}

/* Small areas far from the origin: the terms are tiny next to the products
 * they are differences of, so only the products bound the error. */
void test_cancelled_volume()
{
  litestl::test::SplitMix64 r(23);
  const auto bits = [&] { return std::ldexp(double(r.below(uint64_t(1) << 50)), -50); };
  for (int c = 0; c < 200; c++) {
    Fx<double> fx;
    const double z = 1000 + bits(), x0 = 1000 + bits(), y0 = 1000 + bits();
    const double x1 = x0 + std::ldexp(1 + bits(), -30), y1 = y0 + std::ldexp(1 + bits(), -30);
    const uint32_t a = fx.vert({x0, y0, z}), b = fx.vert({x1, y0, z}), d = fx.vert({x1, y1, z}), e = fx.vert({x0, y1, z});
    fx.face({a, b, d, e});
    fx.face({b, d, e, a});
    Complex<double> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(fx.b.build(k, br));
    UT_CHECK(!build_regions(k, rr) && rr.count(RegionCode::DegenerateShell) == 2);
  }
}

/* As test_interior_point, but no interior point is representable (a tiny
 * coordinate among large ones). */
void test_no_test_point()
{
  Fx<double> fx;
  /* grid coordinates are scaled by 2^-8 */
  const double u = std::ldexp(1.0, -8), tiny = std::ldexp(1.0, -70);
  const uint32_t cx = fx.vert({40 * u, 20 * u, 20 * u});
  const uint32_t cy = fx.vert({tiny, 40 * u, 20 * u});
  const uint32_t cz = fx.vert({20 * u, 20 * u, 40 * u});
  fx.box({0, 0, 0}, {40, 40, 40}, {cx, cy, cz});
  fx.face({cx, cy, cz});
  Complex<double> k;
  BuildReport br;
  RegionReport rr;
  UT_CHECK(fx.b.build(k, br));
  UT_CHECK(!build_regions(k, rr) && rr.errors == 1 && rr.count(RegionCode::NoTestPoint) == 1);
}

/* Solids in solids in a room, several to each outer shell: every shell's
 * region is the innermost one around it. */
template <geom::CoordType C> void test_innermost()
{
  test::GridBuilder<C> g;
  g.box({0, 0, 0}, {400, 400, 400});
  for (int x = 0; x < 3; x++) {
    for (int y = 0; y < 3; y++) {
      const int64_t a = 10 + 130 * x, b = 10 + 130 * y;
      g.box({a, b, 10}, {a + 110, b + 110, 300});
      for (int z = 0; z < 2; z++) {
        g.box({a + 10, b + 10, 20 + 100 * z}, {a + 90, b + 90, 100 + 100 * z});
        g.box({a + 20, b + 20, 30 + 100 * z}, {a + 40, b + 40, 60 + 100 * z});
      }
    }
  }
  Complex<C> k;
  BuildReport br;
  RegionReport rr;
  UT_CHECK(g.b.build(k, br) && build_regions(k, rr));
  UT_CHECK(rr.outer_shells == 1 + 9 * 5 && rr.cavity_shells == 1 + 9 * 5 && rr.regions == 2 + 9 * 5);
  /* boxes by size: the region on a face's front is the next larger box's
   * inside. A region's depth is its nesting level */
  const auto level = [&](FaceRef f) {
    double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
    k.for_each_loop(f, [&](LoopRef l) {
      k.for_each_coedge(l, [&](CoedgeRef c) {
        const auto p = geom::to_double3<C>(k.pos(k.start_vert(c)));
        for (int i = 0; i < 3; i++) {
          lo[i] = std::min(lo[i], p[i]);
          hi[i] = std::max(hi[i], p[i]);
        }
      });
    });
    const double s = std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]}) * (std::is_same_v<C, geom::Lattice> ? 1 : 256);
    return s > 350 ? 0 : s > 200 ? 1 : s > 75 ? 2 : 3;
  };
  std::vector<int> depth(k.regions.capacity(), -1); /* by region: its box's level */
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    int &d = depth[k.region(FaceRef(f), 1).i];
    /* a face of a box of the same size class may be small: take the largest */
    d = d < 0 ? level(FaceRef(f)) : std::min(d, level(FaceRef(f)));
  }
  /* without hints a region is named by its smallest side, over all of its
   * shells */
  std::vector<PersistentId> smallest(k.regions.capacity(), PersistentId{~0ull, ~0ull});
  for (uint32_t s = 0; s < 2 * k.faces.capacity(); s++) {
    PersistentId &m = smallest[k.region(FaceRef(s / 2), s & 1).i];
    m = std::min(m, ident::face_side_id(k.face_id(FaceRef(s / 2)), s & 1));
  }
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    const uint32_t in = k.region(FaceRef(f), 1).i, out = k.region(FaceRef(f), 0).i;
    UT_CHECK(in != 0 && depth[in] >= 0);
    UT_CHECK(k.regions.template get<".id">()[in] == ident::region_id_from_sides(smallest[in]));
    UT_CHECK(depth[in] == 0 ? out == 0 : (out != 0 && depth[out] == depth[in] - 1));
  }
}

template <geom::CoordType C> void run_all()
{
  test_interior_point<C>();
  test_concave<C>();
  test_touching<C>();
  test_outside_box<C>();
  test_fin<C>();
  test_untriangulated<C>();
  test_degenerate<C>();
  test_innermost<C>();
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  run_all<geom::Lattice>();
  run_all<double>();
  run_all<float>();
  test_rounded_volume();
  test_cancelled_volume();
  test_no_test_point();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
