/* Editor primitives (plan P3a W4/W5/W8): effects, preconditions, exact
 * rollback, undo/redo through the journal, propagation, IDs and lineage. */

#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/io.h"
#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

#include "litestl/util/alloc.h"
#include "gen/buildings.h"
#include "litestl/attr/pool_store.h"
#include "support/edit_fixture.h"
#include "support/topo_fixtures.h"
#include "ut_test.h"

#include <cstring>
#include <vector>

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
using test::I3;
using test::pt;

namespace {

using test::Fixture;
using test::h;

/* ---- every primitive once, then undo all / redo all ---- */
template <geom::CoordType C> void test_roundtrip()
{
  Fixture<C> fx;
  UT_CHECK(fx.box());
  Editor<C> ed(fx.k, &fx.j);
  const PersistentId h0 = h(fx.k);
  const uint32_t V0 = fx.k.verts.live_count(), E0 = fx.k.edges.live_count(), F0 = fx.k.faces.live_count();
  const uint32_t C0 = fx.k.coedges.live_count();

  /* split_edge at the midpoint of a face edge (two coedges) */
  const EdgeRef e = fx.edge_between({0, 0, 0}, {4, 0, 0});
  VertRef mid;
  EdgeRef e2;
  EXPECT_EDIT(ed, ed.split_edge(e, pt<C>({2, 0, 0}), &mid, &e2), EditCode::Ok);
  UT_CHECK(fx.k.verts.live_count() == V0 + 1 && fx.k.edges.live_count() == E0 + 1);
  UT_CHECK(fx.k.coedges.live_count() == C0 + 2);
  UT_CHECK(!fx.k.regions_dirty); /* planar faces keep their regions */
  UT_CHECK(fx.valid());
  const PersistentId h_split = h(fx.k);

  /* join_edge restores the topology (with the survivor's new ID) */
  EdgeRef surv;
  EXPECT_EDIT(ed, ed.join_edge(mid, &surv), EditCode::Ok);
  UT_CHECK(fx.k.verts.live_count() == V0 && fx.k.edges.live_count() == E0 && fx.k.coedges.live_count() == C0);
  UT_CHECK(fx.valid());

  /* split_face along a diagonal of the -z face, join it again */
  const FaceRef f = fx.face_with({{0, 0, 0}, {4, 4, 0}});
  UT_CHECK(bool(f));
  FaceRef fr;
  EdgeRef chord;
  EXPECT_EDIT(ed, ed.split_face(f, fx.vert_at({0, 0, 0}), fx.vert_at({4, 4, 0}), &fr, &chord), EditCode::Ok);
  UT_CHECK(fx.k.faces.live_count() == F0 + 1 && fx.k.regions_dirty);
  UT_CHECK(fx.k.loop_size(fx.k.first_loop(f)) == 3 && fx.k.loop_size(fx.k.first_loop(fr)) == 3);
  UT_CHECK(fx.valid());
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  UT_CHECK(!fx.k.regions_dirty && fx.valid());
  FaceRef fs;
  EXPECT_EDIT(ed, ed.join_faces(chord, &fs), EditCode::Ok);
  UT_CHECK(fx.k.faces.live_count() == F0 && fx.valid());

  /* flip a face twice: the second flip restores its columns but not the
   * journal; both are valid */
  UT_CHECK(ed.flip_face(fs) == EditCode::Ok && fx.valid());
  UT_CHECK(ed.flip_face(fs) == EditCode::Ok && fx.valid());

  /* a wire in the air, embedded in the unbounded region; then removed */
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  VertRef a, b;
  EdgeRef w;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({10, 0, 0}), test::pid(1001), &a), EditCode::Ok);
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({10, 4, 0}), test::pid(1002), &b), EditCode::Ok);
  EXPECT_EDIT(ed, ed.make_edge(a, b, ident::kNoId, &w), EditCode::Ok);
  EXPECT_EDIT(ed, ed.embed_edge(w, EmbedTarget::region(RegionRef(0))), EditCode::Ok);
  UT_CHECK(fx.valid());
  /* a wire split carries its container to both halves and the new vertex (C1) */
  VertRef wm;
  EdgeRef w2;
  EXPECT_EDIT(ed, ed.split_edge(w, pt<C>({10, 2, 0}), &wm, &w2), EditCode::Ok);
  UT_CHECK(fx.k.vert_container(wm) == fx.k.edges.template get<".e.container">()[w.i]);
  UT_CHECK(fx.k.edges.template get<".e.container">()[w2.i] == fx.k.edges.template get<".e.container">()[w.i]);
  UT_CHECK(fx.valid());
  EXPECT_EDIT(ed, ed.join_edge(wm, &w), EditCode::Ok);
  EXPECT_EDIT(ed, ed.embed_edge(w, EmbedTarget{}), EditCode::Ok);
  EXPECT_EDIT(ed, ed.kill_edge(w), EditCode::Ok);
  UT_CHECK(ed.kill_vertex(a) == EditCode::Ok && ed.kill_vertex(b) == EditCode::Ok);
  UT_CHECK(fx.valid());

  /* set_position on a vertex without faces (a box corner can't move without
   * bending its three faces) */
  VertRef lone;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({20, 0, 0}), test::pid(1003), &lone), EditCode::Ok);
  EXPECT_EDIT(ed, ed.set_position(lone, pt<C>({22, 2, 2})), EditCode::Ok);
  UT_CHECK(geom::lex_compare<C>(fx.k.pos(lone), pt<C>({22, 2, 2})) == 0);

  const PersistentId h_end = h(fx.k);
  const uint32_t n = fx.j.undo_count();
  UT_CHECK(n == 20);
  for (uint32_t i = 0; i < n; i++) {
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
    if (i == n - 2) {
      UT_CHECK(h(fx.k) == h_split);
    }
  }
  UT_CHECK(h(fx.k) == h0);
  EXPECT_EDIT(ed, ed.undo(), EditCode::NothingToUndo);
  for (uint32_t i = 0; i < n; i++) {
    EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  }
  UT_CHECK(h(fx.k) == h_end);
  EXPECT_EDIT(ed, ed.redo(), EditCode::NothingToRedo);
  UT_CHECK(fx.valid());
}

/* ---- failures are bit-identical no-ops ---- */
template <geom::CoordType C> void test_failures()
{
  Fixture<C> fx;
  UT_CHECK(fx.box());
  Editor<C> ed(fx.k, &fx.j);
  const PersistentId h0 = h(fx.k);
  auto same = [&](EditCode got, EditCode want) {
    UT_CHECK(got == want);
    if (got != want) {
      fprintf(stderr, "  got %s, want %s\n", edit_code_name(got), edit_code_name(want));
    }
    UT_CHECK(h(fx.k) == h0);
  };
  const VertRef v000 = fx.vert_at({0, 0, 0}), v400 = fx.vert_at({4, 0, 0}), v440 = fx.vert_at({4, 4, 0});
  const EdgeRef e = fx.edge_between({0, 0, 0}, {4, 0, 0});
  const FaceRef fz = fx.face_with({{0, 0, 0}, {4, 4, 0}});
  same(ed.make_vertex(pt<C>({9, 9, 9}), fx.k.vert_id(v000)), EditCode::DuplicateId);
  same(ed.make_vertex(pt<C>({9, 9, 9}), ident::kNoId), EditCode::BadArgument);
  same(ed.kill_vertex(v000), EditCode::NotIsolated);
  same(ed.kill_edge(e), EditCode::NotWire);
  same(ed.make_edge(v000, v400), EditCode::EdgeExists);
  same(ed.make_edge(v000, v000), EditCode::SameVertex);
  same(ed.split_edge(e, pt<C>({4, 0, 0})), EditCode::NotOnEdge); /* an endpoint */
  same(ed.split_edge(e, pt<C>({2, 1, 0})), EditCode::NotOnEdge); /* off the line */
  same(ed.split_edge(e, pt<C>({6, 0, 0})), EditCode::NotOnEdge); /* beyond */
  same(ed.join_edge(v000), EditCode::NotJoinable);
  same(ed.split_face(fz, v000, v400), EditCode::Adjacent);
  same(ed.split_face(fz, v000, fx.vert_at({4, 4, 4})), EditCode::NotInLoop);
  same(ed.join_faces(e), EditCode::NotJoinable); /* perpendicular faces: not coplanar */
  same(ed.kill_face(FaceRef(999)), EditCode::DeadElement);
  same(ed.set_side_hint(fz, 2, test::pid(5)), EditCode::BadArgument);
  same(ed.embed_vertex(v000, EmbedTarget::face(fz)), EditCode::NotIsolated);
  {
    const VertRef vs[3] = {v000, v400, v440};
    const uint32_t sz[1] = {3};
    /* v000-v440 has no edge */
    same(ed.make_face(vs, sz, test::pid(77)), EditCode::BadLoop);
  }
  /* a vertex moved off its faces' plane (C5: no promotion to projected) */
  same(ed.set_position(v000, pt<C>({-1, 0, 0})), EditCode::NonPlanarFace);
  /* a primitive that would leave an invalid complex rolls back with the
   * validator's report: a wire through a face is fine (a known gap), but a
   * vertex embedded in a face it isn't on is not */
  {
    VertRef lone;
    EXPECT_EDIT(ed, ed.make_vertex(pt<C>({2, 2, 9}), test::pid(4242), &lone), EditCode::Ok);
    const PersistentId h1 = h(fx.k);
    EXPECT_EDIT(ed, ed.embed_vertex(lone, EmbedTarget::face(fz)), EditCode::Invalid);
    UT_CHECK(ed.report().validation.count(ValidateCode::ContainerGeometry) > 0);
    UT_CHECK(h(fx.k) == h1);
    EXPECT_EDIT(ed, ed.kill_vertex(lone), EditCode::Ok);
    UT_CHECK(ed.undo() == EditCode::Ok && ed.undo() == EditCode::Ok && h(fx.k) == h0);
    fx.j.clear();
  }
  /* rollback of a user transaction after successful primitives */
  EXPECT_EDIT(ed, ed.begin("tool"), EditCode::Ok);
  EXPECT_EDIT(ed, ed.split_edge(e, pt<C>({2, 0, 0})), EditCode::Ok);
  EXPECT_EDIT(ed, ed.split_face(fz, v000, v440), EditCode::Ok);
  EXPECT_EDIT(ed, ed.rollback(), EditCode::Ok);
  UT_CHECK(h(fx.k) == h0 && fx.j.undo_count() == 0);
  /* savepoints: an inner rollback keeps the outer work */
  EXPECT_EDIT(ed, ed.begin("outer"), EditCode::Ok);
  VertRef nv;
  EXPECT_EDIT(ed, ed.split_edge(e, pt<C>({2, 0, 0}), &nv), EditCode::Ok);
  const PersistentId h1 = h(fx.k);
  EXPECT_EDIT(ed, ed.begin("inner"), EditCode::Ok);
  EXPECT_EDIT(ed, ed.join_edge(nv), EditCode::Ok);
  EXPECT_EDIT(ed, ed.rollback(), EditCode::Ok);
  UT_CHECK(h(fx.k) == h1);
  EXPECT_EDIT(ed, ed.commit(), EditCode::Ok);
  UT_CHECK(fx.j.undo_count() == 1 && fx.valid());
  UT_CHECK(ed.undo() == EditCode::Ok && h(fx.k) == h0);
  UT_CHECK(ed.redo() == EditCode::Ok && h(fx.k) == h1);
  /* writes outside the editor truncate the journal */
  fx.k.verts.template get<".v.container_hint">().set(0, ident::kNoId);
  UT_CHECK(ed.undo() == EditCode::JournalStale && fx.j.undo_count() == 0);
}

/* ---- propagation ---- */
template <geom::CoordType C> void test_propagation()
{
  using litestl::attr::Column;
  Fixture<C> fx;
  UT_CHECK(fx.box());
  /* vertex temperature (float: interpolated), edge mass (conserved), a face
   * side colour (swapped by flip), an int face tag (copied), a Temp cache */
  auto *temp = fx.k.verts.group().template ensure<Column<double>>("temp");
  auto *mass = fx.k.edges.group().template ensure<Column<double>>("mass");
  auto *paint = fx.k.faces.sides().group().template ensure<Column<int32_t>>("paint");
  auto *tag = fx.k.faces.group().template ensure<Column<int32_t>>("tag");
  auto *area = fx.k.faces.group().template ensure<Column<double>>("area_w");
  auto *cache = fx.k.faces.group().template ensure<Column<int32_t>>("cache", 0, litestl::attr::ColumnFlags::Temp);
  auto *uv = fx.k.coedges.group().template ensure<Column<double>>("uv");
  UT_CHECK(temp && mass && paint && tag && area && cache && uv);
  const VertRef v000 = fx.vert_at({0, 0, 0}), v400 = fx.vert_at({4, 0, 0}), v440 = fx.vert_at({4, 4, 0});
  temp->set(v000.i, 10.0);
  temp->set(v400.i, 30.0);
  const EdgeRef e = fx.edge_between({0, 0, 0}, {4, 0, 0});
  mass->set(e.i, 8.0);
  const FaceRef fz = fx.face_with({{0, 0, 0}, {4, 4, 0}});
  paint->set(2 * fz.i, 1);
  paint->set(2 * fz.i + 1, 2);
  tag->set(fz.i, 42);
  area->set(fz.i, 16.0);
  cache->set(fz.i, 7);
  fx.k.coedges.for_each_live([&](uint32_t c) { uv->set(c, double(fx.k.start_vert(CoedgeRef(c)).i)); });

  PropagationRules rules;
  rules.conserved(Grp::Edges, "mass");
  rules.conserved(Grp::Faces, "area_w");
  Editor<C> ed(fx.k, &fx.j, &rules);

  VertRef m;
  EdgeRef e2;
  EXPECT_EDIT(ed, ed.split_edge(e, pt<C>({1, 0, 0}), &m, &e2), EditCode::Ok);
  UT_CHECK((*temp)[m.i] == 15.0);                          /* lerp at t = 1/4 */
  UT_CHECK((*mass)[e.i] + (*mass)[e2.i] == 8.0); /* conserved */
  /* by length: e keeps the half at its vs[0] */
  const bool e_at_origin = fx.k.edge_verts(e)[0] == v000 || fx.k.edge_verts(e)[1] == v000;
  UT_CHECK((*mass)[e.i] == (e_at_origin ? 2.0 : 6.0) && (*mass)[e2.i] == (e_at_origin ? 6.0 : 2.0));
  UT_CHECK(geom::lex_compare<C>(fx.k.pos(m), pt<C>({1, 0, 0})) == 0); /* pos is never mixed (C2) */
  /* corners: the new corner at m interpolates the corners at its neighbours */
  fx.k.coedges.for_each_live([&](uint32_t c) {
    if (fx.k.start_vert(CoedgeRef(c)) == m) {
      UT_CHECK((*uv)[c] == 0.75 * double(v000.i) + 0.25 * double(v400.i));
    }
  });
  EdgeRef joined;
  EXPECT_EDIT(ed, ed.join_edge(m, &joined), EditCode::Ok);
  UT_CHECK((*mass)[joined.i] == 8.0); /* summed back */

  FaceRef fr;
  EXPECT_EDIT(ed, ed.split_face(fz, v000, v440, &fr), EditCode::Ok);
  UT_CHECK((*tag)[fz.i] == 42 && (*tag)[fr.i] == 42);              /* copied */
  UT_CHECK((*area)[fz.i] == 8.0 && (*area)[fr.i] == 8.0);          /* split by measure */
  UT_CHECK((*cache)[fz.i] == 0 && (*cache)[fr.i] == 0);            /* Temp: reset */
  UT_CHECK((*paint)[2 * fr.i] == 1 && (*paint)[2 * fr.i + 1] == 2); /* sides follow the parent */

  EXPECT_EDIT(ed, ed.flip_face(fr), EditCode::Ok);
  UT_CHECK((*paint)[2 * fr.i] == 2 && (*paint)[2 * fr.i + 1] == 1); /* swapped */
  EXPECT_EDIT(ed, ed.flip_face(fr), EditCode::Ok);

  /* join: equal ints copy, conserved floats sum */
  const EdgeRef chord = ed.find_edge(v000, v440);
  FaceRef fs;
  EXPECT_EDIT(ed, ed.join_faces(chord, &fs), EditCode::Ok);
  UT_CHECK((*tag)[fs.i] == 42 && (*area)[fs.i] == 16.0);
  UT_CHECK(fx.valid());

  /* a split-by-measure rule on an int column is rejected (C10) */
  PropagationRules bad;
  bad.conserved(Grp::Faces, "tag");
  Editor<C> ed2(fx.k, nullptr, &bad);
  const PersistentId hb = h(fx.k);
  EXPECT_EDIT(ed2, ed2.split_face(fs, v000, v440), EditCode::PropagationFailed);
  UT_CHECK(h(fx.k) == hb);
}

/* ---- make_face over wires, kill_face ---- */
template <geom::CoordType C> void test_make_kill_face()
{
  Fixture<C> fx;
  UT_CHECK(fx.box());
  Editor<C> ed(fx.k, &fx.j);
  const PersistentId h0 = h(fx.k);
  /* a fence panel beside the box: four vertices, four wires, one face */
  VertRef v[4];
  const I3 ps[4] = {{8, 0, 0}, {12, 0, 0}, {12, 0, 4}, {8, 0, 4}};
  for (int i = 0; i < 4; i++) {
    EXPECT_EDIT(ed, ed.make_vertex(pt<C>(ps[i]), test::pid(2000 + i), &v[i]), EditCode::Ok);
  }
  for (int i = 0; i < 4; i++) {
    EXPECT_EDIT(ed, ed.make_edge(v[i], v[(i + 1) % 4]), EditCode::Ok);
  }
  const uint32_t sz[1] = {4};
  FaceRef f;
  EXPECT_EDIT(ed, ed.make_face(v, sz, test::pid(3000), 0, ident::kNoId, ident::kNoId, &f), EditCode::Ok);
  UT_CHECK(fx.valid());
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  UT_CHECK(fx.k.region(f, 0).i == 0 && fx.k.region(f, 1).i == 0); /* a sheet in the air */
  UT_CHECK(fx.valid());
  /* a second face on the same edges (a coincident panel) is a radial tie */
  EXPECT_EDIT(ed, ed.make_face(v, sz, test::pid(3001)), EditCode::RadialTie);
  /* a vertex embedded in the panel blocks kill_face */
  VertRef in;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({9, 0, 3}), test::pid(2100), &in), EditCode::Ok);
  EXPECT_EDIT(ed, ed.embed_vertex(in, EmbedTarget::face(f)), EditCode::Ok);
  UT_CHECK(fx.valid());
  EXPECT_EDIT(ed, ed.kill_face(f), EditCode::FaceHasInclusions);
  /* split the panel: the inclusion follows the child containing it */
  FaceRef other;
  EXPECT_EDIT(ed, ed.split_face(f, v[0], v[2], &other), EditCode::Ok);
  const FaceRef holder = fx.k.vert_container(in).as_face();
  UT_CHECK(holder == f || holder == other);
  UT_CHECK(fx.valid());
  EXPECT_EDIT(ed, ed.embed_vertex(in, EmbedTarget{}), EditCode::Ok);
  EXPECT_EDIT(ed, ed.kill_vertex(in), EditCode::Ok);
  EXPECT_EDIT(ed, ed.kill_face(other), EditCode::Ok);
  EXPECT_EDIT(ed, ed.kill_face(f), EditCode::Ok);
  UT_CHECK(fx.valid());
  while (fx.j.undo_count()) {
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  }
  UT_CHECK(h(fx.k) == h0);
}

/* ---- persistent IDs and lineage ---- */
template <geom::CoordType C> void test_ids()
{
  Fixture<C> fx;
  UT_CHECK(fx.box());
  Editor<C> ed(fx.k, &fx.j);
  const EdgeRef e = fx.edge_between({0, 0, 0}, {4, 0, 0});
  const PersistentId eid = fx.k.edge_id(e);
  VertRef m;
  EdgeRef e2;
  EXPECT_EDIT(ed, ed.split_edge(e, pt<C>({2, 0, 0}), &m, &e2), EditCode::Ok);
  const auto kids = fx.j.forward(eid);
  UT_CHECK(kids.size() == 3); /* two halves and the vertex */
  UT_CHECK(fx.k.vert_id(m) == ident::derive(ident::tag::OpDerived, eid, std::string_view("split_edge.v")));
  UT_CHECK(fx.j.backward(fx.k.edge_id(e2)).size() == 1 && fx.j.backward(fx.k.edge_id(e2))[0] == eid);
  /* the same split on a copy derives the same IDs (determinism) */
  Fixture<C> fy;
  UT_CHECK(fy.box());
  Editor<C> ed2(fy.k);
  EXPECT_EDIT(ed2, ed2.split_edge(fy.edge_between({0, 0, 0}, {4, 0, 0}), pt<C>({2, 0, 0})), EditCode::Ok);
  UT_CHECK(content_hash(fx.k) == content_hash(fy.k));
}

/* ---- projected faces (A1-R1, C5) ---- */
template <geom::CoordType C> void test_projected()
{
  using test::gen::gpt;
  test::gen::Model<C> m(ident::derive(ident::tag::TestOnly, std::string_view("roof")));
  test::gen::sloped_roof(m, {0, 0, 0}, 4, 1);
  Complex<C> k;
  BuildReport br;
  RegionReport rr;
  UT_CHECK(m.b.build(k, br, m.policy) && build_regions(k, rr));
  FaceRef roof;
  k.faces.for_each_live([&](uint32_t f) {
    if (k.face_proj_axis(FaceRef(f)) >= 0) {
      roof = FaceRef(f);
    }
  });
  UT_CHECK(bool(roof));
  Journal<C> j;
  Editor<C> ed(k, &j);
  const PersistentId h0 = content_hash(k);
  auto vert_at = [&](const I3 &p) {
    VertRef out;
    k.verts.for_each_live([&](uint32_t v) {
      if (geom::lex_compare<C>(k.pos(VertRef(v)), gpt<C>(p)) == 0) {
        out = VertRef(v);
      }
    });
    return out;
  };
  /* the roof corners: (0,0,300) (500,0,300) (500,500,304) (0,500,300) */
  const VertRef q000 = vert_at({0, 0, 300}), q110 = vert_at({500, 500, 304}), q100 = vert_at({500, 0, 300});
  /* a diagonal split leaves two planar triangles: both drop the axis */
  FaceRef other;
  EXPECT_EDIT(ed, ed.split_face(roof, q000, q110, &other), EditCode::Ok);
  UT_CHECK(k.face_proj_axis(roof) < 0 && k.face_proj_axis(other) < 0);
  UT_CHECK(validate(k, ValidateLevel::Global).ok());
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  UT_CHECK(content_hash(k) == h0 && k.face_proj_axis(roof) >= 0);
  /* splitting a roof edge changes T(f): regions go dirty, cycles are re-derived */
  UT_CHECK(!k.regions_dirty);
  VertRef mid;
  EXPECT_EDIT(ed, ed.split_edge(ed.find_edge(q000, q100), gpt<C>({250, 0, 300}), &mid), EditCode::Ok);
  UT_CHECK(k.regions_dirty && k.face_proj_axis(roof) >= 0);
  UT_CHECK(validate(k, ValidateLevel::Global).ok());
  /* a split between the new vertex and the lifted corner leaves a projected
   * quad (still non-planar) and a planar triangle */
  EXPECT_EDIT(ed, ed.split_face(roof, mid, q110, &other), EditCode::Ok);
  UT_CHECK((k.face_proj_axis(roof) >= 0) != (k.face_proj_axis(other) >= 0));
  UT_CHECK(validate(k, ValidateLevel::Global).ok());
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  /* flipping a projected face keeps T(f) and its radial positions */
  const FaceRef proj = k.face_proj_axis(roof) >= 0 ? roof : other;
  EXPECT_EDIT(ed, ed.flip_face(proj), EditCode::Ok);
  UT_CHECK(validate(k, ValidateLevel::Global).ok());
  while (j.undo_count()) {
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  }
  UT_CHECK(content_hash(k) == h0);
}

/* ---- the journal as a pool root (C9) ---- */
void test_pool_roots()
{
  using litestl::attr::Column;
  using litestl::attr::PoolRef;
  using litestl::attr::PoolStore;
  using Names = litestl::attr::InternPool<litestl::util::string>;
  Fixture<double> fx;
  UT_CHECK(fx.box());
  PoolStore store;
  const uint32_t names = store.add_pool<Names>("ut.name");
  auto *col = fx.k.faces.group().ensure<Column<PoolRef>>("name");
  UT_CHECK(col != nullptr);
  const uint32_t ci = uint32_t(fx.k.faces.group().index_of("name"));
  const PoolRef a = store.intern<Names>(names, litestl::util::string("brick"));
  col->set(0, a);
  Editor<double> ed(fx.k, &fx.j);
  auto roots = [&](JournalRoots<double> *jr) {
    static const litestl::attr::AttrGroup *gs[kGroupCount];
    for (uint32_t g = 0; g < kGroupCount; g++) {
      gs[g] = &group_of(fx.k, Grp(g));
    }
    static litestl::attr::RefRoot *ex[1];
    ex[0] = jr;
    return PoolStore::Roots{litestl::util::span<const litestl::attr::AttrGroup *const>(gs, kGroupCount),
                            litestl::util::span<litestl::attr::RefRoot *const>(ex, jr ? 1 : 0),
                            nullptr};
  };
  /* overwrite face 0's name through the editor: only the journal still holds "brick" */
  const PoolRef b = store.intern<Names>(names, litestl::util::string("glass"));
  uint8_t bytes[sizeof(PoolRef)];
  memcpy(bytes, &b, sizeof(b));
  EXPECT_EDIT(ed, ed.set_attr(Grp::Faces, 0, ci, bytes), EditCode::Ok);
  /* a barrier keeps a snapshot that holds "glass" */
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  const PoolRef c = store.intern<Names>(names, litestl::util::string("steel"));
  memcpy(bytes, &c, sizeof(c));
  EXPECT_EDIT(ed, ed.set_attr(Grp::Faces, 0, ci, bytes), EditCode::Ok);
  JournalRoots<double> jr(fx.j, fx.k);
  UT_CHECK(store.collect(roots(&jr)));
  UT_CHECK(store.live(a) && store.live(b) && store.live(c));
  UT_CHECK(store.audit(roots(&jr), true));
  /* undo through the barrier: the restored refs are live (undoing a barrier
   * replaces the groups, so column pointers are fetched again) */
  while (fx.j.undo_count()) {
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  }
  col = fx.k.faces.group().find<Column<PoolRef>>("name");
  UT_CHECK((*col)[0] == a && store.get<Names>((*col)[0]) == litestl::util::string("brick"));
  /* without the journal root, collect frees what only the journal holds */
  fx.j.clear();
  col->set(0, c);
  UT_CHECK(store.collect(roots(nullptr)));
  UT_CHECK(!store.live(a) && !store.live(b) && store.live(c));
}

} // namespace

int main()
{
  topo::register_column_types();
  const size_t baseline = litestl::alloc::getMemorySize();
  test_roundtrip<double>();
  test_roundtrip<geom::Lattice>();
  test_failures<double>();
  test_failures<geom::Lattice>();
  test_propagation<double>();
  test_make_kill_face<double>();
  test_make_kill_face<geom::Lattice>();
  test_ids<double>();
  test_ids<geom::Lattice>();
  test_projected<double>();
  test_projected<geom::Lattice>();
  test_pool_roots();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
