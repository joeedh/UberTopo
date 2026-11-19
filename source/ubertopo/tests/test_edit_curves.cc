/* Curved edges in the editor (plan P4a W6, §5.2, §6.3): set_edge_curve and
 * the endpoint rule, split_edge_at / join_edge, weld, the journal rooting
 * refs across collects and barriers, and freeze / write / read. */

#include <cstring>
#include <vector>

#include "support/curve_fixture.h"
#include "support/edit_fixture.h"
#include "ubertopo/topo/freeze.h"
#include "ubertopo/topo/io.h"
#include "ut_test.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
using litestl::attr::PoolRef;
using test::Fixture;
using test::fpt;
using test::h;

namespace {

/* the first validation issue's code of the last Invalid result */
template <geom::CoordType C> ValidateCode first_issue(const Editor<C> &ed)
{
  const auto &v = ed.report().validation;
  return v.issues.size() ? v.issues[0].code : ValidateCode::DeadReference;
}

template <geom::CoordType C> struct Air {
  VertRef a, b;
  EdgeRef e;
};

/* a wire in region 0 from @p pa to @p pb (frame units) */
template <geom::CoordType C>
Air<C> wire(Editor<C> &ed, const double *pa, const double *pb, uint64_t id)
{
  Air<C> w;
  EXPECT_EDIT(ed, ed.make_vertex(fpt<C>(pa[0], pa[1], pa[2]), test::pid(id), &w.a), EditCode::Ok);
  EXPECT_EDIT(ed, ed.make_vertex(fpt<C>(pb[0], pb[1], pb[2]), test::pid(id + 1), &w.b), EditCode::Ok);
  EXPECT_EDIT(ed, ed.make_edge(w.a, w.b, ident::kNoId, &w.e, EmbedTarget::region(RegionRef(0))), EditCode::Ok);
  return w;
}

template <geom::CoordType C> void test_set_and_split()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  Editor<C> ed(fx.k, &fx.j);
  const double A[3] = {600, 0, 300}, B[3] = {900, 120, 340};
  const Air<C> w = wire(ed, A, B, 5001);
  const auto ecurve = [&](EdgeRef e) { return fx.k.edges.template get<".e.curve">()[e.i]; };
  const auto erange = [&](EdgeRef e) { return fx.k.edges.template get<".e.range">()[e.i]; };

  /* a valid arc carrier */
  PoolRef arc;
  double s = 0;
  EXPECT_EDIT(ed, test::arc_carrier(ed, w.a, w.b, 0.3, &arc, &s), EditCode::Ok);
  const PersistentId h_straight = h(fx.k);
  EXPECT_EDIT(ed, ed.set_edge_curve(w.e, arc, 0, s), EditCode::Ok);
  UT_CHECK(ecurve(w.e) == arc && erange(w.e)[1] == s && fx.valid());
  UT_CHECK(!(h(fx.k) == h_straight));

  /* reversed, the carrier misses both ends: CurveEndpoint; a carrier that
   * misses b; after a failure the hash is unchanged, interning included */
  const PersistentId h1 = h(fx.k);
  EXPECT_EDIT(ed, ed.set_edge_curve(w.e, arc, s, 0), EditCode::Invalid);
  UT_CHECK(first_issue(ed) == ValidateCode::CurveEndpoint);
  PoolRef other;
  double s2 = 0;
  EXPECT_EDIT(ed, test::arc_carrier(ed, w.a, w.b, 0.45, &other, &s2), EditCode::Ok);
  EXPECT_EDIT(ed, ed.set_edge_curve(w.e, other, 0, s2 * 0.9), EditCode::Invalid);
  UT_CHECK(first_issue(ed) == ValidateCode::CurveEndpoint);
  UT_CHECK(h(fx.k) == h1);

  /* the editor's own checks: range outside the domain, equal ends, a
   * straight edge with a range, a 2D kind */
  EXPECT_EDIT(ed, ed.set_edge_curve(w.e, arc, 0, s * 2), EditCode::BadArgument);
  EXPECT_EDIT(ed, ed.set_edge_curve(w.e, arc, 1, 1), EditCode::BadArgument);
  EXPECT_EDIT(ed, ed.set_edge_curve(w.e, PoolRef{}, 0, 1), EditCode::BadArgument);
  {
    curves::Cesaro2 c2;
    UT_CHECK(test::two_point_arc(0, 0, 10, 0, 0.2, c2) == nullptr);
    PoolRef r2;
    EXPECT_EDIT(ed, ed.intern_curve(curves::cesaro2_kind(), c2, &r2), EditCode::Ok);
    EXPECT_EDIT(ed, ed.set_edge_curve(w.e, r2, 0, c2.length()), EditCode::BadArgument);
    /* a definition that is not canonical: refused by intern_curve */
    curves::Cesaro2 bad = c2;
    bad.theta0 += 8;
    EXPECT_EDIT(ed, ed.intern_curve(curves::cesaro2_kind(), bad, &r2), EditCode::BadArgument);
    /* an alignment whose z outruns its horizontal: read accepts it (the
     * check needs the store), the evaluator does not build */
    PoolRef hz;
    EXPECT_EDIT(ed, ed.intern_curve(curves::cesaro2_kind(), c2, &hz), EditCode::Ok);
    curves::Alignment3 al;
    al.horizontal = hz;
    al.z = curves::Fn1D::constant(1.0, 0, 2 * c2.length());
    EXPECT_EDIT(ed, ed.intern_curve(curves::alignment3_kind(), al, &r2), EditCode::BadArgument);
    /* a definition of another kind's type */
    EXPECT_EDIT(ed, ed.intern_curve(curves::alignment3_kind(), c2, &r2), EditCode::BadArgument);
  }
  UT_CHECK(h(fx.k) == h1);

  /* split_edge refuses a curved edge */
  EXPECT_EDIT(ed, ed.split_edge(w.e, fx.k.pos(w.a)), EditCode::CurvedEdge);

  /* split_edge_at: the vertex at round_C(c(t)), ranges (0, t) and (t, s) */
  const double t = s * 0.375;
  VertRef v;
  EdgeRef e2;
  const PersistentId h_before = h(fx.k);
  EXPECT_EDIT(ed, ed.split_edge_at(w.e, t, &v, &e2), EditCode::Ok);
  UT_CHECK(fx.valid());
  {
    const auto c = curves::curve3(fx.k.geom, arc);
    curves::Vec<3> x;
    c->eval(t, 0, &x);
    UT_CHECK(geom::lex_compare<C>(fx.k.pos(v), fpt<C>(x[0], x[1], x[2])) == 0);
  }
  UT_CHECK(ecurve(e2) == arc && ecurve(w.e) == arc);
  UT_CHECK(erange(w.e)[0] == 0 && erange(w.e)[1] == t && erange(e2)[0] == t && erange(e2)[1] == s);
  const PersistentId h_split = h(fx.k);
  /* undo restores the hash bit for bit; redo re-executes */
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  UT_CHECK(h(fx.k) == h_before);
  EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  UT_CHECK(h(fx.k) == h_split);
  /* outside the range, or on a straight edge */
  EXPECT_EDIT(ed, ed.split_edge_at(w.e, t, nullptr, nullptr), EditCode::NotOnEdge);
  EXPECT_EDIT(ed, ed.split_edge_at(w.e, -1, nullptr, nullptr), EditCode::NotOnEdge);

  /* join_edge with the ranges not meeting bit for bit: Incompatible (the
   * vertex is still within tolerance of the nudged parameter) */
  EXPECT_EDIT(ed, ed.set_edge_curve(e2, arc, std::nextafter(t, s), s), EditCode::Ok);
  EXPECT_EDIT(ed, ed.join_edge(v), EditCode::Incompatible);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  /* different carriers: Incompatible */
  PoolRef arc2;
  double s3 = 0;
  EXPECT_EDIT(ed, test::arc_carrier(ed, v, w.b, 0.2, &arc2, &s3), EditCode::Ok);
  EXPECT_EDIT(ed, ed.set_edge_curve(e2, arc2, 0, s3), EditCode::Ok);
  UT_CHECK(fx.valid());
  EXPECT_EDIT(ed, ed.join_edge(v), EditCode::Incompatible);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  UT_CHECK(h(fx.k) == h_split);

  /* join_edge gives back the carrier, the range and the positions */
  EdgeRef surv;
  EXPECT_EDIT(ed, ed.join_edge(v, &surv), EditCode::Ok);
  UT_CHECK(ecurve(surv) == arc && erange(surv)[0] == 0 && erange(surv)[1] == s);
  const auto vs = fx.k.edge_verts(surv);
  UT_CHECK(vs[0] == w.a && vs[1] == w.b && fx.valid());

  /* clearing the carrier */
  EXPECT_EDIT(ed, ed.set_edge_curve(surv, PoolRef{}, 0, 0), EditCode::Ok);
  UT_CHECK(ecurve(surv).none() && fx.valid());
}

/* a face edge with a carrier off one of its faces' planes: setting it is
 * fine (planarity reads the stored vertices; the in-plane check is OI-P4b-7),
 * though it changes the faces' skeletons and so dirties the regions (P4b
 * §4.3); splitting it is not fine */
template <geom::CoordType C> void test_face_edge()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  Editor<C> ed(fx.k, &fx.j);
  const EdgeRef e = fx.edge_between({0, 0, 0}, {256, 0, 0});
  const auto vs = fx.k.edge_verts(e);
  PoolRef arc;
  double s = 0;
  EXPECT_EDIT(ed, test::arc_carrier(ed, vs[0], vs[1], 0.4, &arc, &s), EditCode::Ok);
  EXPECT_EDIT(ed, ed.set_edge_curve(e, arc, 0, s), EditCode::Ok);
  UT_CHECK(fx.valid());
  UT_CHECK(fx.k.regions_dirty);
  const PersistentId h0 = h(fx.k);
  EXPECT_EDIT(ed, ed.split_edge_at(e, s / 2), EditCode::NonPlanarFace);
  UT_CHECK(h(fx.k) == h0);
}

/* a lone square face in the air with an in-plane arc on one edge: the split
 * keeps the face planar, adds a corner and makes the regions dirty */
template <geom::CoordType C> void test_face_split_ok()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  Editor<C> ed(fx.k, &fx.j);
  const double xy[4][2] = {{600, 0}, {900, 0}, {900, 300}, {600, 300}};
  VertRef v[4];
  for (int i = 0; i < 4; i++) {
    EXPECT_EDIT(ed, ed.make_vertex(fpt<C>(xy[i][0], xy[i][1], 500), test::pid(9500 + i), &v[i]), EditCode::Ok);
  }
  ed.begin("square");
  for (int i = 0; i < 4; i++) {
    EXPECT_EDIT(ed, ed.make_edge(v[i], v[(i + 1) % 4]), EditCode::Ok);
  }
  const uint32_t sizes[1] = {4};
  EXPECT_EDIT(ed, ed.make_face(v, sizes, test::pid(9600)), EditCode::Ok);
  UT_CHECK(ed.commit() == EditCode::Ok);
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  UT_CHECK(!fx.k.regions_dirty && fx.valid());
  /* the edge v0 -> v1 along +x: a right turn bulges to -y, out of the square */
  const EdgeRef e = ed.find_edge(v[0], v[1]);
  const bool fwd = fx.k.edge_verts(e)[0] == v[0];
  PoolRef arc;
  double s = 0;
  EXPECT_EDIT(ed, test::arc_carrier(ed, v[0], v[1], -0.3, &arc, &s), EditCode::Ok);
  EXPECT_EDIT(ed, ed.set_edge_curve(e, arc, fwd ? 0 : s, fwd ? s : 0), EditCode::Ok);
  UT_CHECK(fx.k.regions_dirty); /* the face's skeleton changed (P4b §4.3) */
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  UT_CHECK(!fx.k.regions_dirty && fx.valid());
  VertRef m;
  EXPECT_EDIT(ed, ed.split_edge_at(e, s / 2, &m), EditCode::Ok);
  UT_CHECK(fx.k.regions_dirty && fx.valid());
  /* the corner leaves again on the join */
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  EXPECT_EDIT(ed, ed.join_edge(m), EditCode::Ok);
  UT_CHECK(fx.k.regions_dirty && fx.valid());
}

template <geom::CoordType C> void test_weld()
{
  for (const bool same : {true, false}) {
    Fixture<C> fx;
    UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
    Editor<C> ed(fx.k, &fx.j);
    const double A[3] = {600, 0, 300}, B[3] = {900, 120, 300};
    const Air<C> w1 = wire(ed, A, B, 6001);
    PoolRef arc;
    double s = 0;
    EXPECT_EDIT(ed, test::arc_carrier(ed, w1.a, w1.b, 0.3, &arc, &s), EditCode::Ok);
    EXPECT_EDIT(ed, ed.set_edge_curve(w1.e, arc, 0, s), EditCode::Ok);
    /* the same carrier the other way round, between coincident vertices
     * (invalid until welded) */
    ed.options().validate = ValidateMode::None;
    const Air<C> w2 = wire(ed, B, A, 6101);
    if (same) {
      EXPECT_EDIT(ed, ed.set_edge_curve(w2.e, arc, s, 0), EditCode::Ok);
    }
    else {
      PoolRef other;
      double s2 = 0;
      EXPECT_EDIT(ed, test::arc_carrier(ed, w2.a, w2.b, -0.3, &other, &s2), EditCode::Ok);
      EXPECT_EDIT(ed, ed.set_edge_curve(w2.e, other, 0, s2), EditCode::Ok);
    }
    ed.options().validate = ValidateMode::Full;
    const std::pair<VertRef, VertRef> pairs[2] = {{w1.a, w2.b}, {w1.b, w2.a}};
    const EditCode c = ed.weld(pairs);
    UT_CHECK(c == (same ? EditCode::Ok : EditCode::Incompatible));
    if (same) {
      UT_CHECK(fx.k.edges.live(w1.e.i) != fx.k.edges.live(w2.e.i) && fx.valid());
    }
  }
}

/* the journal roots every ref it holds: nothing undo or redo needs is
 * collected (with freed slots never reused, a dangling ref would show) */
template <geom::CoordType C> void test_journal_roots()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  fx.k.geom.set_reuse_freed(false);
  Editor<C> ed(fx.k, &fx.j);
  const double A[3] = {600, 0, 300}, B[3] = {900, 120, 340};
  const Air<C> w = wire(ed, A, B, 7001);
  PoolRef ra, rb;
  double sa = 0, sb = 0;
  EXPECT_EDIT(ed, test::arc_carrier(ed, w.a, w.b, 0.3, &ra, &sa), EditCode::Ok);
  EXPECT_EDIT(ed, test::arc_carrier(ed, w.a, w.b, -0.2, &rb, &sb), EditCode::Ok);
  EXPECT_EDIT(ed, ed.set_edge_curve(w.e, ra, 0, sa), EditCode::Ok);
  EXPECT_EDIT(ed, ed.set_edge_curve(w.e, rb, 0, sb), EditCode::Ok);
  const PersistentId h_b = h(fx.k);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok); /* A back; B only in the redo payload */
  EXPECT_EDIT(ed, ed.collect_geometry(), EditCode::Ok);
  UT_CHECK(fx.k.geom.live(ra) && fx.k.geom.live(rb));
  EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  UT_CHECK(h(fx.k) == h_b && fx.valid());
  /* collect refused inside a transaction */
  UT_CHECK(ed.begin() == EditCode::Ok);
  EXPECT_EDIT(ed, ed.collect_geometry(), EditCode::OpenTransaction);
  UT_CHECK(ed.commit() == EditCode::Ok);
  /* without the journal, the replaced carrier goes (with its horizontal) */
  fx.j.clear();
  const uint32_t live0 = fx.k.geom.live_count();
  EXPECT_EDIT(ed, ed.collect_geometry(), EditCode::Ok);
  UT_CHECK(!fx.k.geom.live(ra) && fx.k.geom.live(rb) && fx.k.geom.live_count() == live0 - 2);
  UT_CHECK(fx.valid());
}

/* barrier undo keeps later entries: compact, then a transaction interns X
 * and sets it; undo both, redo both: X is the same live entry */
template <geom::CoordType C> void test_barrier()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  fx.k.geom.set_reuse_freed(false);
  Editor<C> ed(fx.k, &fx.j);
  const double A[3] = {600, 0, 300}, B[3] = {900, 120, 340};
  const Air<C> w = wire(ed, A, B, 8001);
  VertRef lone;
  EXPECT_EDIT(ed, ed.make_vertex(fpt<C>(0, 900, 0), test::pid(8100), &lone), EditCode::Ok);
  EXPECT_EDIT(ed, ed.kill_vertex(lone), EditCode::Ok); /* a tombstone */
  PoolRef junk;
  double sj = 0;
  EXPECT_EDIT(ed, test::arc_carrier(ed, w.a, w.b, 0.1, &junk, &sj), EditCode::Ok); /* nothing references it */
  EXPECT_EDIT(ed, ed.compact(), EditCode::Ok);
  UT_CHECK(!fx.k.geom.live(junk)); /* barriers collect after they commit */
  /* the wire's edge after compact: the one live edge with no coedges */
  EdgeRef we;
  fx.k.edges.for_each_live([&](uint32_t i) {
    if (!fx.k.edge_coedge(EdgeRef(i))) {
      we = EdgeRef(i);
    }
  });
  const auto vs = fx.k.edge_verts(we);
  UT_CHECK(ed.begin("curve") == EditCode::Ok);
  PoolRef x;
  double s = 0;
  EXPECT_EDIT(ed, test::arc_carrier(ed, vs[0], vs[1], 0.3, &x, &s), EditCode::Ok);
  EXPECT_EDIT(ed, ed.set_edge_curve(we, x, 0, s), EditCode::Ok);
  UT_CHECK(ed.commit() == EditCode::Ok);
  const PersistentId h_end = h(fx.k);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok); /* the compact: the groups restored, the store kept */
  UT_CHECK(fx.k.geom.live(x));
  EXPECT_EDIT(ed, ed.redo(), EditCode::Ok); /* the compact again: it collects */
  UT_CHECK(fx.k.geom.live(x));
  EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  UT_CHECK(h(fx.k) == h_end && fx.k.edges.template get<".e.curve">()[we.i] == x && fx.valid());
  (void)w;
}

/* freeze, write and read with a curved edge */
template <geom::CoordType C> void test_freeze_io()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  {
    Editor<C> ed(fx.k, &fx.j);
    const double A[3] = {600, 0, 300}, B[3] = {900, 120, 340};
    const Air<C> w = wire(ed, A, B, 9001);
    PoolRef arc, junk;
    double s = 0, sj = 0;
    EXPECT_EDIT(ed, test::arc_carrier(ed, w.a, w.b, 0.3, &arc, &s), EditCode::Ok);
    EXPECT_EDIT(ed, test::arc_carrier(ed, w.a, w.b, 0.1, &junk, &sj), EditCode::Ok); /* unreferenced */
    EXPECT_EDIT(ed, ed.set_edge_curve(w.e, arc, 0, s), EditCode::Ok);
    EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  }
  const PersistentId h0 = h(fx.k);
  UT_CHECK(freeze(fx.k) == FreezeResult::Ok && fx.k.frozen() && fx.k.geom.live_count() == 2);
  UT_CHECK(fx.valid());
  litestl::io::ByteWriter wr;
  write(fx.k, wr);
  Complex<C> back;
  litestl::io::ByteReader r(wr.data(), wr.size());
  const char *err = nullptr;
  UT_CHECK(read(back, r, &err));
  UT_CHECK(content_hash(back) == content_hash(fx.k) && back.frozen());
  UT_CHECK(validate(back, ValidateLevel::Global).ok());
  (void)h0;
  /* an editor on a frozen complex refuses interning too */
  Editor<C> ed(back, nullptr);
  curves::Cesaro2 c2;
  UT_CHECK(test::two_point_arc(0, 0, 10, 0, 0.2, c2) == nullptr);
  PoolRef r2;
  EXPECT_EDIT(ed, ed.intern_curve(curves::cesaro2_kind(), c2, &r2), EditCode::Frozen);
}

/* the validator's curve codes on states the editor never writes */
template <geom::CoordType C> void test_validate_direct()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  PoolRef plane;
  {
    Editor<C> ed(fx.k, nullptr);
    curves::Cesaro2 c2;
    UT_CHECK(test::two_point_arc(0, 0, 10, 0, 0.2, c2) == nullptr);
    EXPECT_EDIT(ed, ed.intern_curve(curves::cesaro2_kind(), c2, &plane), EditCode::Ok);
  }
  const EdgeRef e = fx.edge_between({0, 0, 0}, {256, 0, 0});
  const FaceRef f = fx.face_with({{0, 0, 0}, {256, 256, 0}});
  auto issue = [&]() {
    const ValidationReport r = validate(fx.k, ValidateLevel::Global);
    return r.issues.size() ? r.issues[0].code : ValidateCode::DeadReference;
  };
  auto &curve = fx.k.edges.template get<".e.curve">();
  auto &range = fx.k.edges.template get<".e.range">();
  UT_CHECK(fx.valid());
  fx.k.edges.template get<".e.geom_flags">().set(e.i, 1);
  UT_CHECK(issue() == ValidateCode::GeometryUnsupported);
  fx.k.edges.template get<".e.geom_flags">().set(e.i, 0);
  range.set(e.i, litestl::math::double2(1, 2)); /* a range without a carrier */
  UT_CHECK(issue() == ValidateCode::CurveRange);
  curve.set(e.i, plane); /* a 2D kind */
  UT_CHECK(issue() == ValidateCode::CurveKind);
  curve.set(e.i, PoolRef{});
  range.set(e.i, litestl::math::double2(0, 0));
  UT_CHECK(fx.valid());
  fx.k.faces.template get<".f.surface">().set(f.i, plane); /* not a surface kind (P4b) */
  UT_CHECK(issue() == ValidateCode::SurfaceKind);
  fx.k.faces.template get<".f.surface">().set(f.i, PoolRef{});
  const CoedgeRef c = fx.k.edge_coedge(e);
  fx.k.coedges.template get<".c.pcurve">().set(c.i, plane); /* on an unsurfaced face */
  UT_CHECK(issue() == ValidateCode::PcurveKind);
  fx.k.coedges.template get<".c.pcurve">().set(c.i, PoolRef{});
  UT_CHECK(fx.valid());
}

template <geom::CoordType C> void all()
{
  test_validate_direct<C>();
  test_set_and_split<C>();
  test_face_edge<C>();
  test_face_split_ok<C>();
  test_weld<C>();
  test_journal_roots<C>();
  test_barrier<C>();
  test_freeze_io<C>();
}

} // namespace

int main()
{
  register_column_types();
  all<double>();
  all<geom::Lattice>();
  return ut_test_end();
}
