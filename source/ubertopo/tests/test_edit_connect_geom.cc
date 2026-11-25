/* connect_loops and disconnect_loops on projected faces and curved edges
 * (plan 2026-11-24-connect-loops). */

#include <map>

#include "litestl/util/alloc.h"
#include "support/curve_fixture.h"
#include "support/cut_fixture.h"
#include "support/splitmix64.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
using test::as_loops;
using test::contained_in;
using test::cut_cases;
using test::CutCase;
using test::Fixture;
using test::I3;
using test::Loop;
using test::loops_of;
using test::Loops;
using test::maintained;
using test::pid;
using test::pt;
using test::sheet;
using test::sides_of;
using test::verts_at;

namespace {

/* a projected terrain face with a hole: cut and joined back, and refused
 * where a child became planar */
template <geom::CoordType C> void test_connect_projected()
{
  for (const bool flat_half : {false, true}) {
    Fixture<C> fx;
    test::GridBuilder<C> g;
    const auto z = [&](int64_t x, int64_t y) { return flat_half && y >= 20 ? int64_t(0) : (x * 7 + y * 3) % 5 + (x * y) % 3; };
    Loops ls = cut_cases()[0].face;
    Loop path = cut_cases()[0].path;
    for (Loop &l : ls) {
      for (I3 &p : l) {
        p[2] = z(p[0], p[1]);
      }
    }
    for (I3 &p : path) {
      p[2] = z(p[0], p[1]);
    }
    g.face(ls);
    BuildPolicy pol;
    pol.allow_projectable_faces = true;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(fx.k, br, pol) && br.projected_faces == 1 && build_regions(fx.k, rr));
    EditorOptions opt;
    Editor<C> ed(fx.k, &fx.j, nullptr, opt);
    const FaceRef f = fx.face_with({ls[0][0]});
    const auto parent = loops_of(fx.k, f);
    FaceRef other, s;
    EdgeRef chord;
    EXPECT_EDIT(ed, ed.connect_loops(f, verts_at(fx, path), &other, &chord), EditCode::Ok);
    UT_CHECK(fx.valid() && maintained(ed));
    UT_CHECK((fx.k.face_proj_axis(f) < 0) == flat_half && fx.k.face_proj_axis(other) == 2);
    EXPECT_EDIT(ed, ed.disconnect_loops(chord, &s), flat_half ? EditCode::Incompatible : EditCode::Ok);
    UT_CHECK(fx.valid() && (flat_half || loops_of(fx.k, s) == parent));
  }
}

/* walls on the edges of a projected face, some inside the wedge that the cut
 * or the join moves: the radial order follows T(f). Not validated Global, as
 * such a wall crosses the face before or after. */
template <geom::CoordType C> void test_connect_radial()
{
  uint32_t moved = 0;
  for (uint64_t seed = 1; seed <= 24; seed++) {
    litestl::test::SplitMix64 r(seed);
    Fixture<C> fx;
    test::GridBuilder<C> g;
    Loops ls = cut_cases()[0].face;
    Loop path = {{0, 0, 0}, {10, 20, 0}, {30, 20, 0}, {40, 40, 0}}; /* not Delaunay edges */
    std::map<std::pair<int64_t, int64_t>, int64_t> z;
    const auto lift = [&](I3 &p) {
      const std::pair<int64_t, int64_t> at(p[0], p[1]);
      if (!z.count(at)) {
        z[at] = int64_t(r.below(9));
      }
      p[2] = z[at];
    };
    for (Loop &l : ls) {
      for (I3 &p : l) {
        lift(p);
      }
    }
    for (I3 &p : path) {
      lift(p);
    }
    g.face(ls);
    for (const Loop &l : ls) {
      for (size_t i = 0; i < l.size(); i++) {
        const I3 a = l[i], b = l[(i + 1) % l.size()];
        /* two walls toward the face's inside, about as steep as the face */
        for (int n = 0; n < 2; n++) {
          const I3 mid = {(a[0] + b[0]) / 2 - (b[1] - a[1]) / 2, (a[1] + b[1]) / 2 + (b[0] - a[0]) / 2,
                          (a[2] + b[2]) / 2 + int64_t(r.below(13)) - 6};
          g.face({{a, b, mid}});
        }
      }
    }
    BuildPolicy pol;
    pol.allow_projectable_faces = true;
    BuildReport br;
    EditorOptions none;
    none.validate = ValidateMode::None;
    if (!g.b.build(fx.k, br, pol) || br.projected_faces != 1) {
      continue;
    }
    const auto order_ok = [&] {
      uint32_t bad = 0;
      for (const auto &x : validate(fx.k, ValidateLevel::Embedding).issues) {
        bad += x.code == ValidateCode::RadialOrder;
      }
      return bad == 0;
    };
    UT_CHECK(order_ok());
    Editor<C> ed(fx.k, &fx.j, nullptr, none);
    const FaceRef f = fx.face_with({ls[0][0], ls[0][1], ls[0][3]});
    const auto orders = [&] {
      std::map<PersistentId, std::vector<uint64_t>> out;
      fx.k.edges.for_each_live([&](uint32_t e) {
        std::vector<uint64_t> cyc;
        fx.k.for_each_radial(EdgeRef(e), [&](CoedgeRef c) {
          const FaceRef x = fx.k.face_of(c);
          cyc.push_back(fx.k.face_proj_axis(x) >= 0 || x == f ? 0 : uint64_t(x.i) + 1);
        });
        std::rotate(cyc.begin(), std::min_element(cyc.begin(), cyc.end()), cyc.end());
        out[fx.k.edge_id(EdgeRef(e))] = cyc;
      });
      return out;
    };
    const auto before = orders();
    EdgeRef chord;
    FaceRef other;
    if (ed.connect_loops(f, verts_at(fx, path), &other, &chord) != EditCode::Ok) {
      continue;
    }
    UT_CHECK(order_ok());
    for (const auto &[id, cyc] : orders()) {
      moved += before.count(id) && before.at(id) != cyc;
    }
    if (ed.disconnect_loops(chord) == EditCode::Ok) {
      UT_CHECK(order_ok());
    }
  }
  UT_CHECK(moved > 0);
}

/* arcs on the outer loop and on the hole */
template <geom::CoordType C> void test_connect_curved()
{
  Fixture<C> fx;
  Builder<C> b;
  const auto at = [&](double x, double y) { return test::fpt<C>(x, y, 500); };
  const double o[4][2] = {{600, 0}, {1000, 0}, {1000, 400}, {600, 400}};
  const double in[4][2] = {{700, 100}, {700, 300}, {900, 300}, {900, 100}};
  std::vector<uint32_t> keys;
  for (int i = 0; i < 4; i++) {
    keys.push_back(b.vertex(pid(1 + i), at(o[i][0], o[i][1])));
  }
  for (int i = 0; i < 4; i++) {
    keys.push_back(b.vertex(pid(11 + i), at(in[i][0], in[i][1])));
  }
  const uint32_t sizes[2] = {4, 4};
  b.face(pid(50), keys, sizes);
  BuildReport br;
  RegionReport rr;
  UT_CHECK(b.build(fx.k, br) && build_regions(fx.k, rr));
  Editor<C> ed(fx.k, &fx.j);
  const auto vert = [&](uint64_t id) {
    VertRef out;
    fx.k.verts.for_each_live([&](uint32_t v) { out = fx.k.vert_id(VertRef(v)) == pid(id) ? VertRef(v) : out; });
    return out;
  };
  for (const auto &[p, q] : {std::pair<uint64_t, uint64_t>{1, 2}, {3, 4}, {11, 12}, {13, 14}}) {
    const EdgeRef e = ed.find_edge(vert(p), vert(q));
    const auto vs = fx.k.edge_verts(e);
    litestl::attr::PoolRef arc;
    double s = 0;
    UT_CHECK(test::arc_carrier(ed, vs[0], vs[1], p == 1 || p == 11 ? 0.4 : -0.4, &arc, &s) == EditCode::Ok);
    EXPECT_EDIT(ed, ed.set_edge_curve(e, arc, 0, s), EditCode::Ok);
  }
  UT_CHECK(fx.valid());
  const FaceRef f(0);
  const auto parent = loops_of(fx.k, f);
  /* along the diagonal */
  const VertRef path[4] = {vert(1), vert(11), vert(13), vert(3)};
  FaceRef other, s;
  EdgeRef chord;
  EXPECT_EDIT(ed, ed.connect_loops(f, path, &other, &chord), EditCode::Ok);
  UT_CHECK(fx.valid() && maintained(ed));
  EXPECT_EDIT(ed, ed.disconnect_loops(chord, &s), EditCode::Ok);
  UT_CHECK(fx.valid() && loops_of(fx.k, s) == parent);
}

/* a child bounded by two arcs and two chords, its vertices all on one line:
 * only its skeleton has an area */
template <geom::CoordType C> void test_connect_arc_ring()
{
  for (const bool reversed : {false, true}) {
    Fixture<C> fx;
    Builder<C> b;
    const double at[6][2] = {{600, 200}, {800, 0}, {1000, 200}, {700, 200}, {900, 200}, {800, 100}};
    std::vector<uint32_t> keys;
    for (int i = 0; i < 6; i++) {
      keys.push_back(b.vertex(pid(1 + i), test::fpt<C>(at[i][0], at[i][1], 500)));
    }
    const uint32_t sizes[2] = {3, 3};
    b.face(pid(50), keys, sizes);
    BuildReport br;
    RegionReport rr;
    UT_CHECK(b.build(fx.k, br) && build_regions(fx.k, rr));
    Editor<C> ed(fx.k, &fx.j);
    VertRef v[6];
    fx.k.verts.for_each_live([&](uint32_t i) {
      for (int n = 0; n < 6; n++) {
        v[n] = fx.k.vert_id(VertRef(i)) == pid(1 + n) ? VertRef(i) : v[n];
      }
    });
    /* the outer arc bulges away from the hole; so does the hole's */
    for (const auto &[p, q] : {std::pair<int, int>{0, 2}, {3, 4}}) {
      const EdgeRef e = ed.find_edge(v[p], v[q]);
      const auto vs = fx.k.edge_verts(e);
      litestl::attr::PoolRef arc;
      double s = 0;
      UT_CHECK(test::arc_carrier(ed, vs[0], vs[1], vs[0] == v[p] ? -0.4 : 0.4, &arc, &s) == EditCode::Ok);
      EXPECT_EDIT(ed, ed.set_edge_curve(e, arc, 0, s), EditCode::Ok);
    }
    const FaceRef f(0);
    const auto parent = loops_of(fx.k, f);
    std::vector<VertRef> path = {v[0], v[3], v[4], v[2]};
    if (reversed) {
      std::reverse(path.begin(), path.end());
    }
    FaceRef other, s;
    EdgeRef chord;
    EXPECT_EDIT(ed, ed.connect_loops(f, path, &other, &chord), EditCode::Ok);
    const FaceRef thin = reversed ? other : f;
    UT_CHECK(fx.valid() && maintained(ed) && fx.k.loop_size(fx.k.first_loop(thin)) == 4);
    EXPECT_EDIT(ed, ed.disconnect_loops(chord, &s), EditCode::Ok);
    UT_CHECK(fx.valid() && loops_of(fx.k, s) == parent);
  }
}

/* an outer arc that bulges past a hole: by their stored vertices alone, the
 * hole would be the outer loop */
template <geom::CoordType C> void test_connect_bulge()
{
  Fixture<C> fx;
  Builder<C> b;
  const double at[6][2] = {{800, 0}, {1000, 100}, {800, 200}, {770, 80}, {770, 120}, {830, 100}};
  std::vector<uint32_t> keys;
  for (int i = 0; i < 3; i++) {
    keys.push_back(b.vertex(pid(1 + i), test::fpt<C>(at[i][0], at[i][1], 500)));
  }
  const uint32_t three = 3;
  b.face(pid(50), keys, std::span<const uint32_t>(&three, 1));
  BuildReport br;
  RegionReport rr;
  UT_CHECK(b.build(fx.k, br) && build_regions(fx.k, rr));
  Editor<C> ed(fx.k, &fx.j);
  VertRef v[6];
  fx.k.verts.for_each_live([&](uint32_t i) {
    for (int n = 0; n < 3; n++) {
      v[n] = fx.k.vert_id(VertRef(i)) == pid(1 + n) ? VertRef(i) : v[n];
    }
  });
  const EdgeRef e = ed.find_edge(v[0], v[2]);
  litestl::attr::PoolRef arc;
  double len = 0;
  UT_CHECK(test::arc_carrier(ed, fx.k.edge_verts(e)[0], fx.k.edge_verts(e)[1],
                             fx.k.edge_verts(e)[0] == v[0] ? -1.2 : 1.2, &arc, &len) == EditCode::Ok);
  EXPECT_EDIT(ed, ed.set_edge_curve(e, arc, 0, len), EditCode::Ok);
  /* the hole, made after the arc */
  const FaceRef f(0);
  EdgeRef h[3];
  ed.begin("hole");
  for (int i = 3; i < 6; i++) {
    EXPECT_EDIT(ed, ed.make_vertex(test::fpt<C>(at[i][0], at[i][1], 500), pid(1 + i), &v[i]), EditCode::Ok);
    EXPECT_EDIT(ed, ed.embed_vertex(v[i], EmbedTarget::face(f)), EditCode::Ok);
  }
  for (int i = 0; i < 3; i++) {
    EXPECT_EDIT(ed, ed.make_edge(v[3 + i], v[3 + (i + 1) % 3], ident::kNoId, &h[i], EmbedTarget::face(f)), EditCode::Ok);
  }
  EXPECT_EDIT(ed, ed.add_hole(f, h), EditCode::Ok);
  UT_CHECK(ed.commit() == EditCode::Ok && fx.valid());
  const auto parent = loops_of(fx.k, f);
  const VertRef path[4] = {v[0], v[3], v[4], v[2]};
  FaceRef s;
  EdgeRef chord;
  EXPECT_EDIT(ed, ed.connect_loops(f, path, nullptr, &chord), EditCode::Ok);
  UT_CHECK(fx.valid());
  EXPECT_EDIT(ed, ed.disconnect_loops(chord, &s), EditCode::Ok);
  UT_CHECK(fx.valid() && loops_of(fx.k, s) == parent);
  bool outer_first = false;
  fx.k.for_each_coedge(fx.k.first_loop(s), [&](CoedgeRef x) { outer_first = outer_first || fx.k.start_vert(x) == v[0]; });
  UT_CHECK(outer_first);
}

template <geom::CoordType C> void run_all()
{
  test_connect_projected<C>();
  test_connect_radial<C>();
  test_connect_curved<C>();
  test_connect_arc_ring<C>();
  test_connect_bulge<C>();
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  run_all<geom::Lattice>();
  run_all<double>();
  run_all<float>();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
