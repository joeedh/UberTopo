/* topo/builder: link tables, welding, rejection paths, permutation invariance
 * (plan P2 W6). */

#include "ubertopo/topo/builder.h"

#include "litestl/util/alloc.h"
#include "litestl/util/task.h"
#include "support/splitmix64.h"
#include "support/topo_fixtures.h"
#include "ut_test.h"

#include <algorithm>
#include <map>
#include <vector>

using namespace ubertopo;
using namespace ubertopo::topo;
using test::I3;
using test::pid;
using test::pt;

static uint32_t radial_size(const ComplexBase &k, EdgeRef e)
{
  uint32_t n = 0;
  k.for_each_radial(e, [&](CoedgeRef) { n++; });
  return n;
}

/* Tetrahedron with IDs 1..4 (so the vertex order is known) and outward faces. */
template <geom::CoordType C> static void test_tetrahedron()
{
  Builder<C> b;
  const uint32_t v0 = b.vertex(pid(1), pt<C>({0, 0, 0}));
  const uint32_t v1 = b.vertex(pid(2), pt<C>({10, 0, 0}));
  const uint32_t v2 = b.vertex(pid(3), pt<C>({0, 10, 0}));
  const uint32_t v3 = b.vertex(pid(4), pt<C>({0, 0, 10}));
  b.face(pid(10), {{v0, v2, v1}});
  b.face(pid(11), {{v0, v1, v3}});
  b.face(pid(12), {{v0, v3, v2}});
  b.face(pid(13), {{v1, v2, v3}});
  Complex<C> k;
  BuildReport rep;
  UT_CHECK(b.build(k, rep) && rep.ok() && rep.warnings == 0);
  UT_CHECK(k.verts.live_count() == 4 && k.edges.live_count() == 6 && k.coedges.live_count() == 12 &&
           k.loops.live_count() == 4 && k.faces.live_count() == 4 && k.faces.sides().size() == 8);
  UT_CHECK(k.regions_dirty);
  /* edges sorted by vertex pair: (0,1) (0,2) (0,3) (1,2) (1,3) (2,3) */
  const uint32_t pairs[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
  for (uint32_t e = 0; e < 6; e++) {
    const auto vs = k.edge_verts(EdgeRef(e));
    UT_CHECK(vs[0].i == pairs[e][0] && vs[1].i == pairs[e][1]);
    UT_CHECK(k.edge_id(EdgeRef(e)) == ident::edge_id(pid(pairs[e][0] + 1), pid(pairs[e][1] + 1)));
    UT_CHECK(radial_size(k, EdgeRef(e)) == 2);
  }
  /* coedges in (face, position) order; c_e and sense per the hand table */
  const uint32_t ce[12] = {1, 3, 0, 0, 4, 2, 2, 5, 1, 3, 5, 4};
  const uint8_t cs[12] = {1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0};
  for (uint32_t c = 0; c < 12; c++) {
    UT_CHECK(k.edge_of(CoedgeRef(c)).i == ce[c] && k.sense(CoedgeRef(c)) == bool(cs[c]));
    UT_CHECK(k.face_of(CoedgeRef(c)).i == c / 3 && k.loop_of(CoedgeRef(c)).i == c / 3);
    UT_CHECK(k.next(CoedgeRef(c)).i == (c / 3) * 3 + (c % 3 + 1) % 3);
    UT_CHECK(k.prev(k.next(CoedgeRef(c))) == CoedgeRef(c));
    UT_CHECK(k.radial_next(k.radial_prev(CoedgeRef(c))) == CoedgeRef(c));
    /* a manifold edge's two coedges run in opposite directions */
    UT_CHECK(k.sense(k.radial_next(CoedgeRef(c))) != k.sense(CoedgeRef(c)));
  }
  /* .e.c is the coedge of the smallest face ID: edge 0 is used by faces 0 and 1 */
  UT_CHECK(k.edge_coedge(EdgeRef(0)) == CoedgeRef(2));
  /* disk cycles in edge-index order */
  for (uint32_t v = 0; v < 4; v++) {
    std::vector<uint32_t> es;
    UT_CHECK(k.for_each_edge_at(VertRef(v), [&](EdgeRef e) { es.push_back(e.i); }));
    UT_CHECK(es.size() == 3 && std::is_sorted(es.begin(), es.end()));
  }
  UT_CHECK(k.vert_edge(VertRef(3)) == EdgeRef(2));
  UT_CHECK(k.loops.template get<".id">()[2] == ident::loop_id(pid(12), 0) && k.loop_size(LoopRef(1)) == 3);
}

/* Edge at x=10, y=0 between two boxes [0,10]^3 and [10,20]x[0,10]^2 sharing
 * the wall x=10: three faces around it, two of them coplanar and opposite. */
template <geom::CoordType C> static void test_shared_wall()
{
  test::GridBuilder<C> g;
  g.box({0, 0, 0}, {10, 10, 10});
  g.box({10, 0, 0}, {20, 10, 10});
  Complex<C> k;
  BuildReport rep;
  UT_CHECK(g.b.build(k, rep) && rep.ok() && rep.warnings == 0);
  UT_CHECK(k.faces.live_count() == 11 && k.edges.live_count() == 20 && k.verts.live_count() == 12);
  uint32_t n3 = 0;
  for (uint32_t e = 0; e < 20; e++) {
    const uint32_t r = radial_size(k, EdgeRef(e));
    UT_CHECK(r == 2 || r == 3);
    n3 += r == 3;
  }
  UT_CHECK(n3 == 4);
  /* every radial cycle alternates consistently: invariant W needs regions,
   * but the wedge pairing must be orientable — each wedge is bounded by one
   * coedge's toward-next side and the next coedge's other side. Check that a
   * size-3 cycle visits the three faces once. */
  for (uint32_t e = 0; e < 20; e++) {
    std::vector<uint32_t> fs;
    k.for_each_radial(EdgeRef(e), [&](CoedgeRef c) { fs.push_back(k.face_of(c).i); });
    std::sort(fs.begin(), fs.end());
    UT_CHECK(std::adjacent_find(fs.begin(), fs.end()) == fs.end());
  }
}

/* Box with a window hole in its +y wall and an aperture face filling it. */
template <geom::CoordType C> static void test_window_aperture()
{
  test::GridBuilder<C> g;
  const int64_t x0 = 0, x1 = 40, y0 = 0, y1 = 40, z0 = 0, z1 = 40;
  g.face({{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}});
  g.face({{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}});
  g.face({{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}});
  /* +y wall with a CW hole (seen from outside, +y) */
  g.face({{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}},
          {{10, y1, 10}, {30, y1, 10}, {30, y1, 30}, {10, y1, 30}}});
  g.face({{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}});
  g.face({{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}});
  /* glazing: fills the hole (its own CCW loop over the hole vertices) */
  g.face({{{10, y1, 10}, {10, y1, 30}, {30, y1, 30}, {30, y1, 10}}}, {}, {}, face_flags::Aperture);
  Complex<C> k;
  BuildReport rep;
  UT_CHECK(g.b.build(k, rep) && rep.ok() && rep.warnings == 0);
  UT_CHECK(k.faces.live_count() == 7 && k.loops.live_count() == 8 && k.edges.live_count() == 16);
  for (uint32_t e = 0; e < 16; e++) {
    UT_CHECK(radial_size(k, EdgeRef(e)) == 2);
  }
  /* the wall face has two loops */
  uint32_t two = 0;
  for (uint32_t f = 0; f < 7; f++) {
    uint32_t n = 0;
    k.for_each_loop(FaceRef(f), [&](LoopRef) { n++; });
    two += n == 2;
  }
  UT_CHECK(two == 1);
}

template <geom::CoordType C> static void test_fence_and_wire()
{
  test::GridBuilder<C> g;
  g.face({{{0, 0, 0}, {10, 0, 0}, {10, 0, 10}, {0, 0, 10}}});
  const uint32_t a = g.b.vertex(pid(100), pt<C>({50, 50, 0}));
  const uint32_t c = g.b.vertex(pid(101), pt<C>({50, 50, 30}), ContainerHint::in_region(pid(7)));
  const uint32_t w = g.b.wire(pid(200), a, c, ContainerHint::in_region(pid(7)));
  (void)w;
  const uint32_t s = g.b.vertex(pid(102), pt<C>({5, 0, 5}), ContainerHint::in_face(0)); /* a sensor on the fence */
  (void)s;
  Complex<C> k;
  BuildReport rep;
  UT_CHECK(g.b.build(k, rep) && rep.ok());
  UT_CHECK(k.edges.live_count() == 5);
  uint32_t wires = 0, boundary = 0;
  for (uint32_t e = 0; e < 5; e++) {
    const uint32_t r = radial_size(k, EdgeRef(e));
    wires += r == 0;
    boundary += r == 1;
    if (r == 0) {
      UT_CHECK(k.edge_id(EdgeRef(e)) == pid(200) && k.edges.template get<".e.container_hint">()[e] == pid(7));
    }
  }
  UT_CHECK(wires == 1 && boundary == 4);
  bool sensor = false;
  for (uint32_t v = 0; v < k.verts.capacity(); v++) {
    if (k.vert_id(VertRef(v)) == pid(102)) {
      sensor = k.vert_container(VertRef(v)).as_face() == FaceRef(0) && !k.vert_edge(VertRef(v));
    }
  }
  UT_CHECK(sensor);
}

template <geom::CoordType C> static void expect_error(Builder<C> &b, BuildCode code, const char *what)
{
  Complex<C> k;
  BuildReport rep;
  const bool ok = b.build(k, rep);
  if (ok || rep.count(code) == 0) {
    fprintf(stderr, "%s: expected %s, got ok=%d issues:", what, build_code_name(code), int(ok));
    for (const BuildIssue &i : rep.issues) {
      fprintf(stderr, " %s", build_code_name(i.code));
    }
    fprintf(stderr, "\n");
  }
  UT_CHECK(!ok && rep.count(code) > 0 && k.verts.capacity() == 0 && k.faces.capacity() == 0);
}

template <geom::CoordType C> static void test_rejections()
{
  auto quad = [](Builder<C> &b, int64_t z = 0) {
    std::array<uint32_t, 4> v;
    const I3 p[4] = {{0, 0, z}, {10, 0, z}, {10, 10, z}, {0, 10, z}};
    for (int i = 0; i < 4; i++) {
      v[i] = b.vertex(pid(uint64_t(i + 1 + 10 * z)), pt<C>(p[i]));
    }
    return v;
  };
  {
    Builder<C> b;
    b.vertex(pid(1), pt<C>({0, 0, 0}));
    b.vertex(pid(1), pt<C>({0, 0, 1}));
    expect_error(b, BuildCode::VertexPositionMismatch, "position mismatch");
  }
  {
    Builder<C> b;
    b.vertex(pid(1), pt<C>({0, 0, 0}));
    b.vertex(pid(1), pt<C>({0, 0, 0}), ContainerHint::in_region(pid(3)));
    expect_error(b, BuildCode::VertexContainerMismatch, "container mismatch");
  }
  {
    Builder<C> b;
    auto v = quad(b);
    b.face(pid(50), {{v[0], v[1], 99}});
    expect_error(b, BuildCode::BadVertexKey, "bad key");
  }
  {
    Builder<C> b;
    auto v = quad(b);
    b.face(pid(50), {{v[0], v[1], v[2]}});
    b.face(pid(50), {{v[0], v[2], v[3]}});
    expect_error(b, BuildCode::DuplicateFaceId, "duplicate face");
  }
  {
    Builder<C> b;
    auto v = quad(b);
    b.face(pid(50), {{v[0], v[1]}});
    expect_error(b, BuildCode::LoopTooShort, "short loop");
  }
  {
    Builder<C> b;
    b.face(pid(50), std::span<const uint32_t>(), std::span<const uint32_t>());
    expect_error(b, BuildCode::LooplessFace, "loopless");
  }
  {
    Builder<C> b;
    auto v = quad(b);
    b.face(pid(50), {{v[0], v[1], v[2], v[1]}});
    expect_error(b, BuildCode::RepeatedVertexInLoop, "repeated vertex");
  }
  {
    Builder<C> b;
    auto v = quad(b);
    const uint32_t h = b.vertex(pid(90), pt<C>({5, 5, 0}));
    b.face(pid(50), {{v[0], v[1], v[2], v[3]}, {v[0], h, b.vertex(pid(91), pt<C>({6, 4, 0}))}});
    expect_error(b, BuildCode::VertexSharedBetweenLoops, "shared vertex");
  }
  {
    Builder<C> b;
    const uint32_t a = b.vertex(pid(1), pt<C>({0, 0, 0})), c = b.vertex(pid(2), pt<C>({5, 0, 0})),
                   d = b.vertex(pid(3), pt<C>({10, 0, 0}));
    b.face(pid(50), {{a, c, d}});
    expect_error(b, BuildCode::DegenerateLoop, "collinear loop");
  }
  {
    Builder<C> b;
    auto v = quad(b);
    const uint32_t up = b.vertex(pid(9), pt<C>({0, 10, 1}));
    b.face(pid(50), {{v[0], v[1], v[2], up}});
    expect_error(b, BuildCode::NonPlanarFace, "non-planar");
  }
  {
    /* hole wound like the outer loop (the review's counterexample) */
    Builder<C> b;
    auto v = quad(b);
    const uint32_t h0 = b.vertex(pid(90), pt<C>({2, 2, 0})), h1 = b.vertex(pid(91), pt<C>({4, 2, 0})),
                   h2 = b.vertex(pid(92), pt<C>({4, 4, 0}));
    b.face(pid(50), {{v[0], v[1], v[2], v[3]}, {h0, h1, h2}});
    expect_error(b, BuildCode::HoleOrientation, "hole orientation");
  }
  {
    Builder<C> b;
    const uint32_t a = b.vertex(pid(1), pt<C>({0, 0, 0}));
    b.wire(pid(60), a, a);
    expect_error(b, BuildCode::DegenerateWire, "degenerate wire");
  }
  {
    Builder<C> b;
    auto v = quad(b);
    b.face(pid(50), {{v[0], v[1], v[2], v[3]}});
    b.wire(pid(60), v[1], v[0]);
    expect_error(b, BuildCode::WireOnFaceEdge, "wire on face edge");
  }
  {
    Builder<C> b;
    const uint32_t a = b.vertex(pid(1), pt<C>({0, 0, 0})), c = b.vertex(pid(2), pt<C>({0, 0, 5}));
    b.wire(pid(60), a, c);
    b.wire(pid(61), c, a);
    expect_error(b, BuildCode::DuplicateWire, "duplicate wire");
  }
  {
    Builder<C> b;
    const uint32_t a = b.vertex(pid(1), pt<C>({0, 0, 0})), c = b.vertex(pid(2), pt<C>({0, 0, 5})),
                   d = b.vertex(pid(3), pt<C>({0, 5, 5}));
    b.wire(pid(60), a, c);
    b.wire(pid(60), c, d);
    expect_error(b, BuildCode::DuplicateWireId, "duplicate wire id");
  }
  {
    Builder<C> b;
    auto v = quad(b);
    b.face(pid(50), {{v[0], v[1], v[2], v[3]}});
    b.vertex(pid(2), pt<C>({10, 0, 0}), ContainerHint::in_region(pid(5))); /* welds onto a face vertex */
    expect_error(b, BuildCode::VertexContainerMismatch, "container on face vertex (mismatch)");
  }
  {
    Builder<C> b;
    const uint32_t a = b.vertex(pid(1), pt<C>({0, 0, 0}), ContainerHint::in_region(pid(5)));
    const uint32_t c = b.vertex(pid(2), pt<C>({10, 0, 0})), d = b.vertex(pid(3), pt<C>({0, 10, 0}));
    b.face(pid(50), {{a, c, d}});
    expect_error(b, BuildCode::ContainerOnFaceVertex, "container on face vertex");
  }
  {
    Builder<C> b;
    b.vertex(pid(1), pt<C>({0, 0, 0}), ContainerHint::in_face(3));
    expect_error(b, BuildCode::BadFaceKey, "bad face key");
  }
}

template <geom::CoordType C> static void test_weld_equal_positions()
{
  Builder<C> b;
  const uint32_t a = b.vertex(pid(5), pt<C>({0, 0, 0}));
  const uint32_t a2 = b.vertex(pid(3), pt<C>({0, 0, 0})); /* same place, smaller ID */
  const uint32_t c = b.vertex(pid(6), pt<C>({10, 0, 0})), d = b.vertex(pid(7), pt<C>({0, 10, 0}));
  b.face(pid(50), {{a, c, d}});
  b.wire(pid(60), a2, b.vertex(pid(8), pt<C>({0, 0, -10})));
  Complex<C> k;
  BuildReport rep;
  UT_CHECK(b.build(k, rep) && k.verts.live_count() == 5); /* without welding: two vertices at the origin */
  BuildPolicy weld;
  weld.weld_equal_positions = true;
  UT_CHECK(b.build(k, rep, weld) && k.verts.live_count() == 4 && k.vert_id(VertRef(0)) == pid(3));
  uint32_t deg = 0;
  k.for_each_edge_at(VertRef(0), [&](EdgeRef) { deg++; });
  UT_CHECK(deg == 3); /* two face edges + the wire */
}

/* Emission order must not matter: the same set of vertices and faces,
 * emitted in shuffled orders (with repeated vertex emissions), gives
 * identical columns. */
template <geom::CoordType C> static void test_permutation_invariance()
{
  /* canonical face set of a 3x2 grid of boxes (shared walls once) */
  test::GridBuilder<C> g0;
  for (int64_t i = 0; i < 3; i++) {
    for (int64_t j = 0; j < 2; j++) {
      g0.box({i * 10, j * 10, 0}, {i * 10 + 10, j * 10 + 10, 10});
    }
  }
  struct FaceRec {
    std::vector<I3> pts;
  };
  std::vector<std::vector<I3>> faces;
  {
    /* recover each face's points (one loop each) from the canonical builder */
    Complex<C> k;
    BuildReport rep;
    UT_CHECK(g0.b.build(k, rep));
    std::map<ident::PersistentId, I3> by_id;
    for (auto &[p, key] : g0.vkeys) {
      by_id[ident::derive_generator(g0.root, "v", p[0], p[1], p[2])] = p;
    }
    for (uint32_t f = 0; f < k.faces.capacity(); f++) {
      std::vector<I3> pts;
      k.for_each_loop(FaceRef(f), [&](LoopRef l) {
        k.for_each_coedge(l, [&](CoedgeRef c) { pts.push_back(by_id[k.vert_id(k.start_vert(c))]); });
      });
      faces.push_back(pts);
    }
  }
  std::vector<I3> points;
  for (auto &[p, key] : g0.vkeys) {
    points.push_back(p);
  }
  ident::PersistentId ref{};
  litestl::test::SplitMix64 r(9);
  for (int trial = 0; trial < 6; trial++) {
    std::vector<I3> vorder = points;
    std::vector<size_t> forder(faces.size());
    for (size_t i = 0; i < forder.size(); i++) {
      forder[i] = i;
    }
    if (trial > 0) {
      for (size_t i = vorder.size(); i > 1; i--) {
        std::swap(vorder[i - 1], vorder[r.below(i)]);
      }
      for (size_t i = forder.size(); i > 1; i--) {
        std::swap(forder[i - 1], forder[r.below(i)]);
      }
    }
    Builder<C> b;
    std::map<I3, uint32_t> key;
    for (const I3 &p : vorder) {
      key[p] = b.vertex(ident::derive_generator(g0.root, "v", p[0], p[1], p[2]), pt<C>(p));
      if (r.below(4) == 0) { /* a repeated emission of the same vertex */
        b.vertex(ident::derive_generator(g0.root, "v", p[0], p[1], p[2]), pt<C>(p));
      }
    }
    for (size_t fi : forder) {
      std::vector<uint32_t> keys;
      ident::Hasher128 h(ident::tag::TestOnly);
      for (const I3 &p : faces[fi]) {
        keys.push_back(key[p]);
        h.i64(p[0]).i64(p[1]).i64(p[2]);
      }
      const uint32_t size = uint32_t(keys.size());
      b.face(h.finish(), std::span<const uint32_t>(keys), std::span<const uint32_t>(&size, 1));
    }
    Complex<C> k;
    BuildReport rep;
    UT_CHECK(b.build(k, rep) && rep.ok() && rep.warnings == 0);
    UT_CHECK(k.faces.live_count() == faces.size());
    const ident::PersistentId hsh = test::complex_hash(k);
    if (trial == 0) {
      ref = hsh;
    }
    UT_CHECK(hsh == ref);
  }
}

/* Bit-identical output at any band count (parallel phases are chunked). */
template <geom::CoordType C> static void test_band_counts()
{
  test::GridBuilder<C> g;
  for (int64_t i = 0; i < 16; i++) {
    for (int64_t j = 0; j < 16; j++) {
      g.box({i * 10, j * 10, 0}, {i * 10 + 10, j * 10 + 10, 10 + 10 * ((i + j) % 2)});
    }
  }
  ident::PersistentId ref{};
  for (int bands : {1, 2, 4, 12}) {
    litestl::task::ScopedBandCount scope(bands);
    Complex<C> k;
    BuildReport rep;
    UT_CHECK(g.b.build(k, rep) && rep.ok());
    UT_CHECK(k.faces.live_count() > 1000 && k.edges.live_count() > 2048);
    const ident::PersistentId h = test::complex_hash(k);
    if (bands == 1) {
      ref = h;
    }
    UT_CHECK(h == ref);
  }
}

template <geom::CoordType C> static void run_all()
{
  test_tetrahedron<C>();
  test_shared_wall<C>();
  test_window_aperture<C>();
  test_fence_and_wire<C>();
  test_rejections<C>();
  test_weld_equal_positions<C>();
  test_permutation_invariance<C>();
  test_band_counts<C>();
}

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  run_all<geom::Lattice>();
  run_all<double>();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
