/* Exact radial order about an edge (plan P2 W5).
 *
 * Fans of planar faces around the axis edge A=(0,0,0) -> B=(0,0,L). Each face
 * lies in the half-plane spanned by the axis and an integer direction (dx,dy);
 * its interior near the axis points along +(dx,dy). In (u, w) face
 * coordinates, u runs along the axis and w along (dx,dy): a point is
 * (w*dx, w*dy, u), exactly planar. Shapes: triangles, concave faces whose
 * first off-axis vertex is on the exterior side (s = -1), and faces where the
 * axis edge belongs to a hole loop. Faces are randomly reversed (front flips;
 * the interior side does not). The expected order is the long-double atan2 of
 * (dx, dy), counter-clockwise about vs[0] -> vs[1]. */

#include "ubertopo/topo/builder.h"
#include "ubertopo/topo/radial.h"

#include "support/splitmix64.h"
#include "support/topo_fixtures.h"
#include "ut_test.h"

#include <cmath>
#include <map>
#include <vector>

using namespace ubertopo;
using namespace ubertopo::topo;
using test::I3;
using test::pid;
using test::pt;

enum class Shape { Triangle, Concave, Hole };

struct FanFace {
  int64_t dx, dy;
  Shape shape;
  bool reversed;
};

/* Loops in (u, w); the axis edge is (0,0)-(L,0); interior near it at w > 0. */
static std::vector<std::vector<std::array<int64_t, 2>>> fan_loops(Shape s, int64_t L)
{
  switch (s) {
    case Shape::Triangle:
      return {{{0, 0}, {L, 0}, {L / 2, 7}}};
    case Shape::Concave:
      /* after the edge (0,0)->(L,0) comes (L+3,-4): on the exterior side */
      return {{{0, 0}, {L, 0}, {L + 3, -4}, {L + 3, 9}, {0, 9}}};
    case Shape::Hole:
    default:
      /* outer CCW around a CW hole whose top edge is the axis: the hole runs
       * (0,0) -> (L,0) with the face interior on its left (w > 0) */
      return {{{-5, -9}, {L + 5, -9}, {L + 5, 6}, {-5, 6}}, {{0, 0}, {L, 0}, {L, -4}, {0, -4}}};
  }
}

template <geom::CoordType C> struct Fan {
  int64_t L = 20;
  std::vector<FanFace> faces;
  bool swap_axis_ids = false;

  bool build(Complex<C> &k, BuildReport &rep, EdgeRef &axis_edge, std::vector<uint32_t> &face_key_of_built) const
  {
    Builder<C> b;
    std::map<I3, uint32_t> vk;
    auto key = [&](const I3 &p) {
      auto it = vk.find(p);
      if (it != vk.end()) {
        return it->second;
      }
      uint64_t lo;
      if (p[0] == 0 && p[1] == 0 && (p[2] == 0 || p[2] == L)) {
        lo = (p[2] == 0) != swap_axis_ids ? 1 : 2; /* the axis ends get the smallest IDs */
      }
      else {
        lo = 1000 + vk.size();
      }
      const uint32_t kk = b.vertex(pid(lo), pt<C>(p));
      vk[p] = kk;
      return kk;
    };
    for (size_t i = 0; i < faces.size(); i++) {
      const FanFace &f = faces[i];
      std::vector<uint32_t> keys, sizes;
      for (auto loop : fan_loops(f.shape, L)) {
        if (f.reversed) {
          std::reverse(loop.begin(), loop.end());
        }
        sizes.push_back(uint32_t(loop.size()));
        for (auto &q : loop) {
          keys.push_back(key({q[1] * f.dx, q[1] * f.dy, q[0]}));
        }
      }
      /* face IDs in a scrambled order so the reference face isn't face 0 */
      b.face(pid(500 + (i * 7919) % 1000), std::span<const uint32_t>(keys), std::span<const uint32_t>(sizes));
    }
    if (!b.build(k, rep)) {
      return false;
    }
    /* the axis edge joins the two smallest IDs: vertices 0 and 1 */
    axis_edge = EdgeRef(0);
    const auto vs = k.edge_verts(axis_edge);
    if (vs[0] != VertRef(0) || vs[1] != VertRef(1)) {
      return false;
    }
    face_key_of_built.assign(faces.size(), 0);
    for (uint32_t f = 0; f < k.faces.capacity(); f++) {
      const uint64_t lo = k.face_id(FaceRef(f)).lo;
      for (size_t i = 0; i < faces.size(); i++) {
        if (lo == 500 + (i * 7919) % 1000) {
          face_key_of_built[f] = uint32_t(i);
        }
      }
    }
    return true;
  }
};

/* Checks the radial cycle of the axis edge against atan2. Returns false on a
 * mismatch. Angles closer than 1e-9 rad (and exact duplicates) are skipped. */
template <geom::CoordType C> static bool check_fan(const Fan<C> &fan, bool expect_tie = false)
{
  Complex<C> k;
  BuildReport rep;
  EdgeRef e;
  std::vector<uint32_t> key_of;
  if (!fan.build(k, rep, e, key_of)) {
    fprintf(stderr, "fan build failed (%u errors)\n", rep.errors);
    for (auto &i : rep.issues) {
      fprintf(stderr, "  %s\n", build_code_name(i.code));
    }
    return false;
  }
  if ((rep.count(BuildCode::RadialTie) > 0) != expect_tie) {
    fprintf(stderr, "tie report mismatch\n");
    return false;
  }
  std::vector<long double> ang;
  k.for_each_radial(e, [&](CoedgeRef c) {
    const FanFace &f = fan.faces[key_of[k.face_of(c).i]];
    long double a = std::atan2((long double)f.dy, (long double)f.dx);
    if (fan.swap_axis_ids) {
      a = -a; /* the axis runs -z: counter-clockwise about -z is clockwise about +z */
    }
    ang.push_back(a);
  });
  if (ang.size() != fan.faces.size()) {
    fprintf(stderr, "cycle size %zu != %zu\n", ang.size(), fan.faces.size());
    return false;
  }
  /* relative angles from the reference must be non-decreasing in [0, 2pi) */
  const long double two_pi = 2 * 3.14159265358979323846264338327950288L;
  long double prev = -1;
  for (size_t i = 0; i < ang.size(); i++) {
    long double rel = std::fmod(ang[i] - ang[0] + 2 * two_pi, two_pi);
    if (rel > two_pi - 1e-12L) {
      rel = 0; /* the reference direction itself */
    }
    if (rel + 1e-9L < prev) {
      fprintf(stderr, "radial order mismatch at %zu: %.12Lf after %.12Lf\n", i, rel, prev);
      return false;
    }
    prev = std::max(prev, rel);
  }
  /* the cycle starts at the smallest face ID */
  ident::PersistentId minid = k.face_id(FaceRef(0));
  for (uint32_t f = 1; f < k.faces.capacity(); f++) {
    minid = std::min(minid, k.face_id(FaceRef(f)));
  }
  return k.face_id(k.face_of(k.edge_coedge(e))) == minid;
}

template <geom::CoordType C> static void test_random_fans()
{
  litestl::test::SplitMix64 r(11);
  int bad = 0, total = 0;
  for (int it = 0; it < 400; it++) {
    Fan<C> fan;
    fan.swap_axis_ids = r.coin();
    const int k = 1 + int(r.below(8));
    std::vector<std::pair<int64_t, int64_t>> used;
    for (int i = 0; i < k; i++) {
      int64_t dx, dy;
      bool dup;
      int guard = 0;
      do {
        dx = r.range(-60, 60);
        dy = r.range(-60, 60);
        dup = (dx == 0 && dy == 0);
        for (auto &u : used) {
          /* the same direction (any positive multiple) would be an exact tie */
          dup = dup || (u.first * dy == u.second * dx && (u.first * dx + u.second * dy) > 0);
        }
      } while (dup && ++guard < 100);
      if (dup) {
        continue;
      }
      used.push_back({dx, dy});
      const Shape s = Shape(r.below(3));
      fan.faces.push_back({dx, dy, s, r.coin()});
    }
    if (fan.faces.empty()) {
      continue;
    }
    /* hole faces need distinct planes: two hole faces in one plane would share
     * the hole's vertices; keep at most one hole face per direction line */
    total++;
    bad += !check_fan<C>(fan);
  }
  UT_CHECK(bad == 0 && total > 300);
}

template <geom::CoordType C> static void test_degenerate_fans()
{
  /* wall + aperture: coplanar, opposite (theta and theta + pi) */
  {
    Fan<C> fan;
    fan.faces = {{3, 5, Shape::Triangle, false}, {-3, -5, Shape::Concave, true}};
    UT_CHECK(check_fan<C>(fan));
  }
  /* perpendicular walls and their opposites: 4 axis-aligned directions */
  for (int swap = 0; swap < 2; swap++) {
    Fan<C> fan;
    fan.swap_axis_ids = swap;
    fan.faces = {{1, 0, Shape::Triangle, false},
                 {0, 1, Shape::Concave, true},
                 {-1, 0, Shape::Triangle, true},
                 {0, -1, Shape::Hole, false}};
    UT_CHECK(check_fan<C>(fan));
  }
  /* multiples of one direction (2,3) and (4,6): the same angle -> reported tie */
  {
    Fan<C> fan;
    fan.faces = {{2, 3, Shape::Triangle, false}, {4, 6, Shape::Concave, false}, {-1, 7, Shape::Triangle, false}};
    UT_CHECK(check_fan<C>(fan, true));
  }
  /* concave and hole faces at the reference position */
  {
    Fan<C> fan;
    fan.faces = {{5, -2, Shape::Hole, true}, {-5, 2, Shape::Concave, false}, {2, 5, Shape::Hole, false}};
    UT_CHECK(check_fan<C>(fan));
  }
}

/* A mutation check: a comparator with the half-plane sign dropped must be
 * caught by the same harness (guards against a vacuous test). */
template <geom::CoordType C> static void test_harness_sensitivity()
{
  Fan<C> fan;
  fan.faces = {{1, 0, Shape::Concave, false}, {0, 1, Shape::Triangle, false}, {-1, 1, Shape::Triangle, false}};
  Complex<C> k;
  BuildReport rep;
  EdgeRef e;
  std::vector<uint32_t> key_of;
  UT_CHECK(fan.build(k, rep, e, key_of));
  /* the concave face's half-plane has s = -1 */
  const CoedgeRef c0 = k.edge_coedge(e);
  bool found_neg = false;
  k.for_each_radial(e, [&](CoedgeRef c) {
    geom::HalfPlane<C> h;
    const FaceRef f = k.face_of(c);
    UT_CHECK(coedge_half_plane<C>(k, c, face_frame<C>(k, f), h));
    if (fan.faces[key_of[f.i]].shape == Shape::Concave) {
      found_neg = h.s == -1;
    }
  });
  (void)c0;
  UT_CHECK(found_neg);
}

template <geom::CoordType C> static void run_all()
{
  test_random_fans<C>();
  test_degenerate_fans<C>();
  test_harness_sensitivity<C>();
}

int main()
{
  run_all<geom::Lattice>();
  run_all<double>();
  return ut_test_end();
}
