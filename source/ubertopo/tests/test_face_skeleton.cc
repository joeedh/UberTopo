/* The refined skeleton (plan P4b §4.3, §12, W2): FacePoints over a Skeleton,
 * points and segments shared between faces, opposite-sense coedges, numbering
 * independent of insertion order, radial order about a curved edge, and
 * SkeletonDegenerate. */

#include <vector>

#include "support/curve_fixture.h"
#include "support/edit_fixture.h"
#include "ubertopo/topo/face_skeleton.h"
#include "ubertopo/topo/radial.h"
#include "ubertopo/topo/validate.h"
#include "ut_test.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using litestl::attr::PoolRef;
using test::Fixture;
using test::fpt;

namespace {

template <geom::CoordType C> VertRef vert(Editor<C> &ed, double x, double y, double z, uint64_t id)
{
  VertRef v;
  EXPECT_EDIT(ed, ed.make_vertex(fpt<C>(x, y, z), test::pid(id), &v), EditCode::Ok);
  return v;
}

/* an arc carrier turning by 2 alpha (test::two_point_arc) on edge e, in
 * vs[0] -> vs[1] order */
template <geom::CoordType C> EditCode set_arc(Editor<C> &ed, EdgeRef e, double alpha)
{
  const auto vs = ed.complex().edge_verts(e);
  PoolRef arc;
  double s = 0;
  const EditCode c = test::arc_carrier(ed, vs[0], vs[1], alpha, &arc, &s);
  return c != EditCode::Ok ? c : ed.set_edge_curve(e, arc, 0, s);
}

bool has_issue(const ValidationReport &r, ValidateCode c, ident::ElemKind kind)
{
  for (const auto &x : r.issues) {
    if (x.code == c && x.kind == kind) {
      return true;
    }
  }
  return false;
}

/* the face after face f in edge e's radial cycle */
template <geom::CoordType C> FaceRef next_face(const Complex<C> &k, EdgeRef e, FaceRef f)
{
  FaceRef out;
  k.for_each_radial(e, [&](CoedgeRef c) {
    if (k.face_of(c) == f) {
      out = k.face_of(k.radial_next(c));
    }
  });
  return out;
}

/* Three triangular fins on the edge a -> b (horizontal, along +x), off the
 * box: F1 = (b, a, q1) below a, F2 = (a, b, q2) up, along +x and slightly
 * +y, F3 = (a, b, q3) straight up from a, higher than q2. F1 runs the edge
 * against its direction.
 *
 * With an arc on a -> b bulging +y, each fin is a fan from its apex to the
 * arc. At height z, F2's section is the arc scaled by (1 - t) about q2 and
 * F3's the arc scaled by (1 - 0.3 t) about a's vertical: F2's lies strictly
 * between F3's and its chord, so the fans meet only on the arc. (Equal apex
 * heights would make F2's section a translate of F3's, which crosses it
 * whenever the radial order swaps.) */
template <geom::CoordType C> struct Fins {
  VertRef a, b, q[3];
  EdgeRef e;
  FaceRef f[3];

  bool make(Fixture<C> &fx, Editor<C> &ed)
  {
    a = vert(ed, 600, 0, 500, 7000);
    b = vert(ed, 900, 0, 500, 7001);
    q[0] = vert(ed, 600, 0, 350, 7002);
    q[1] = vert(ed, 700, 10, 560, 7003);
    q[2] = vert(ed, 600, 0, 700, 7004);
    ed.begin("fins");
    EXPECT_EDIT(ed, ed.make_edge(a, b, ident::kNoId, &e), EditCode::Ok);
    for (int i = 0; i < 3; i++) {
      EXPECT_EDIT(ed, ed.make_edge(b, q[i]), EditCode::Ok);
      EXPECT_EDIT(ed, ed.make_edge(q[i], a), EditCode::Ok);
    }
    const uint32_t sizes[1] = {3};
    for (int i = 0; i < 3; i++) {
      const VertRef loop[3] = {i == 0 ? b : a, i == 0 ? a : b, q[i]};
      EXPECT_EDIT(ed, ed.make_face(loop, sizes, test::pid(7100 + i), 0, ident::kNoId, ident::kNoId, &f[i]),
                  EditCode::Ok);
    }
    if (ed.commit() != EditCode::Ok || ed.rebuild_regions() != EditCode::Ok) {
      return false;
    }
    return fx.valid() && fx.k.edge_verts(e)[0] == a;
  }
};

/* no curved edges: the skeleton is the stored vertices */
template <geom::CoordType C> void test_no_curves()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  Skeleton<C> sk(fx.k);
  fx.k.faces.for_each_live([&](uint32_t f) {
    FacePoints<C> a, b;
    UT_CHECK(a.gather(fx.k, FaceRef(f)) && b.gather(fx.k, FaceRef(f), sk));
    UT_CHECK(a.pts.size() == b.pts.size() && a.off.size() == b.off.size() && a.ids.size() == b.ids.size());
    for (int i = 0; i < a.pts.size() && i < b.pts.size(); i++) {
      UT_CHECK(geom::lex_compare<C>(a.pts[i], b.pts[i]) == 0 && a.ids[i] == b.ids[i]);
    }
    for (int i = 0; i < a.off.size() && i < b.off.size(); i++) {
      UT_CHECK(a.off[i] == b.off[i]);
    }
  });
  UT_CHECK(sk.extra_count() == 0 && !sk.failed());
}

/* every face on a curved edge reads the same ids, reversed for an
 * opposite-sense coedge */
template <geom::CoordType C> void test_shared()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  Editor<C> ed(fx.k, &fx.j);
  Fins<C> fin;
  UT_CHECK(fin.make(fx, ed));
  EXPECT_EDIT(ed, set_arc(ed, fin.e, -0.3), EditCode::Ok);
  UT_CHECK(fx.valid());
  Skeleton<C> sk(fx.k);
  const auto in = sk.interior(fin.e);
  UT_CHECK(in.size() > 2);
  const std::vector<uint32_t> fwd(in.begin(), in.end());
  int reversed = 0;
  for (int i = 0; i < 3; i++) {
    FacePoints<C> fp;
    UT_CHECK(fp.gather(fx.k, fin.f[i], sk));
    /* the coedge on e, and the ids that follow its start vertex */
    CoedgeRef c;
    fx.k.for_each_radial(fin.e, [&](CoedgeRef x) {
      if (fx.k.face_of(x) == fin.f[i]) {
        c = x;
      }
    });
    const uint32_t start = fx.k.start_vert(c).i;
    int at = -1;
    for (int j = 0; j < fp.ids.size(); j++) {
      if (fp.ids[j] == start) {
        at = j;
      }
    }
    UT_CHECK(at >= 0 && at + int(fwd.size()) < fp.ids.size());
    if (at < 0 || at + int(fwd.size()) >= fp.ids.size()) {
      continue;
    }
    const bool sense = fx.k.sense(c);
    reversed += !sense;
    for (size_t j = 0; j < fwd.size(); j++) {
      const uint32_t want = sense ? fwd[j] : fwd[fwd.size() - 1 - j];
      UT_CHECK(fp.ids[at + 1 + int(j)] == want);
      UT_CHECK(geom::lex_compare<C>(fp.pts[at + 1 + int(j)], sk.pos(want)) == 0);
    }
  }
  UT_CHECK(reversed == 1); /* F1 */
  UT_CHECK(!sk.failed());
}

/* the points of each edge don't depend on the order edges enter the table */
template <geom::CoordType C> void test_insertion_order()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  Editor<C> ed(fx.k, &fx.j);
  Fins<C> fin;
  UT_CHECK(fin.make(fx, ed));
  /* the wire first: embedding in region 0 needs clean regions */
  const VertRef w0 = vert(ed, 600, 900, 300, 7200), w1 = vert(ed, 950, 1000, 340, 7201);
  EdgeRef w;
  EXPECT_EDIT(ed, ed.make_edge(w0, w1, ident::kNoId, &w, EmbedTarget::region(RegionRef(0))), EditCode::Ok);
  EXPECT_EDIT(ed, set_arc(ed, w, 0.4), EditCode::Ok);
  EXPECT_EDIT(ed, set_arc(ed, fin.e, -0.3), EditCode::Ok);
  UT_CHECK(fx.valid());
  Skeleton<C> s1(fx.k), s2(fx.k);
  const EdgeRef order1[2] = {fin.e, w}, order2[2] = {w, fin.e};
  for (int i = 0; i < 2; i++) {
    (void)s1.interior(order1[i]);
    (void)s2.interior(order2[i]);
  }
  bool ids_differ = false;
  for (const EdgeRef e : {fin.e, w}) {
    const auto a = s1.interior(e), b = s2.interior(e);
    UT_CHECK(a.size() == b.size() && a.size() > 0);
    for (size_t j = 0; j < a.size() && j < b.size(); j++) {
      UT_CHECK(geom::lex_compare<C>(s1.pos(a[j]), s2.pos(b[j])) == 0);
      ids_differ = ids_differ || a[j] != b[j];
    }
  }
  UT_CHECK(ids_differ); /* the numbering did follow the insertion order */
  UT_CHECK(s1.extra_count() == s2.extra_count());
}

/* Radial order about a curved edge of degree 3 sorts about the first
 * skeleton segment from vs[0]. About the chord (+x), F2's apex is at about
 * 80 degrees from +y toward +z and F3's at 90. The arc leaves a heading
 * about 0.29 rad into +y, which turns the sort axis about z; F2's apex, 100
 * along x and 10 along y, then lies past F3's (once the axis slope exceeds
 * 10 / 100), so the cyclic order reverses. Clearing the carrier restores
 * it. */
template <geom::CoordType C> void test_radial_curved()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  Editor<C> ed(fx.k, &fx.j);
  Fins<C> fin;
  UT_CHECK(fin.make(fx, ed));
  const FaceRef straight_next = next_face(fx.k, fin.e, fin.f[0]);
  UT_CHECK(straight_next);
  EXPECT_EDIT(ed, set_arc(ed, fin.e, -0.3), EditCode::Ok);
  UT_CHECK(fx.valid());
  {
    Skeleton<C> sk(fx.k);
    const auto in = sk.interior(fin.e);
    UT_CHECK(in.size() > 0);
    if (in.size() > 0) {
      const auto p = geom::to_double3<C>(sk.pos(in[0])), a = geom::to_double3<C>(fx.k.pos(fin.a));
      /* the turn of the axis is enough to move F2: dy/dx > 10 / 100 */
      UT_CHECK(p[1] - a[1] > (p[0] - a[0]) / 10);
    }
  }
  const FaceRef curved_next = next_face(fx.k, fin.e, fin.f[0]);
  UT_CHECK(curved_next && curved_next != straight_next);
  EXPECT_EDIT(ed, ed.set_edge_curve(fin.e, PoolRef{}, 0, 0), EditCode::Ok);
  UT_CHECK(next_face(fx.k, fin.e, fin.f[0]) == straight_next);
  UT_CHECK(fx.valid());
}

/* SkeletonDegenerate: an arc that bulges across the far side of a thin face
 * is refused; the same arc bulging outward is fine; an edge that doesn't
 * flatten at the complex's tolerance is reported */
template <geom::CoordType C> void test_degenerate()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  Editor<C> ed(fx.k, &fx.j);
  VertRef v[4];
  const double xy[4][2] = {{600, 0}, {900, 0}, {900, 40}, {600, 40}};
  for (int i = 0; i < 4; i++) {
    v[i] = vert(ed, xy[i][0], xy[i][1], 500, 7300 + i);
  }
  ed.begin("strip");
  EdgeRef e;
  EXPECT_EDIT(ed, ed.make_edge(v[0], v[1], ident::kNoId, &e), EditCode::Ok);
  for (int i = 1; i < 4; i++) {
    EXPECT_EDIT(ed, ed.make_edge(v[i], v[(i + 1) % 4]), EditCode::Ok);
  }
  const uint32_t sizes[1] = {4};
  EXPECT_EDIT(ed, ed.make_face(v, sizes, test::pid(7400)), EditCode::Ok);
  UT_CHECK(ed.commit() == EditCode::Ok);
  UT_CHECK(fx.k.edge_verts(e)[0] == v[0] && fx.valid());
  const ident::PersistentId h0 = test::h(fx.k);

  /* heading +1.2 rad from v0: a sagitta of about 100 crosses the side y = 40 */
  EXPECT_EDIT(ed, set_arc(ed, e, -1.2), EditCode::Invalid);
  UT_CHECK(has_issue(ed.report().validation, ValidateCode::SkeletonDegenerate, ident::ElemKind::Face));
  UT_CHECK(test::h(fx.k) == h0);

  /* bulging to -y, away from the face */
  EXPECT_EDIT(ed, set_arc(ed, e, 1.2), EditCode::Ok);
  UT_CHECK(fx.valid());

  /* a tolerance below the carrier's evaluation error: no flattening */
  auto &tol = fx.k.global.template get<".g.skeleton_tol">();
  const double t0 = tol[0];
  tol.set(0, 0x1p-200);
  UT_CHECK(has_issue(validate(fx.k, ValidateLevel::Global), ValidateCode::SkeletonDegenerate, ident::ElemKind::Edge));
  tol.set(0, t0);
  UT_CHECK(fx.valid());
}

/* a reversed range (the carrier runs vs[1] -> vs[0]): the points still come
 * in edge order */
template <geom::CoordType C> void test_reversed_range()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  Editor<C> ed(fx.k, &fx.j);
  const VertRef a = vert(ed, 600, 0, 300, 7500), b = vert(ed, 900, 120, 300, 7501);
  EdgeRef e;
  EXPECT_EDIT(ed, ed.make_edge(a, b, ident::kNoId, &e, EmbedTarget::region(RegionRef(0))), EditCode::Ok);
  UT_CHECK(fx.k.edge_verts(e)[0] == a);
  PoolRef arc;
  double s = 0;
  EXPECT_EDIT(ed, test::arc_carrier(ed, b, a, 0.4, &arc, &s), EditCode::Ok);
  EXPECT_EDIT(ed, ed.set_edge_curve(e, arc, s, 0), EditCode::Ok);
  Skeleton<C> sk(fx.k);
  const auto in = sk.interior(e);
  UT_CHECK(in.size() > 2);
  if (in.size() > 2) {
    const auto pa = geom::to_double3<C>(fx.k.pos(a)), pb = geom::to_double3<C>(fx.k.pos(b));
    auto d2 = [](const litestl::math::double3 &p, const litestl::math::double3 &q) {
      return (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]);
    };
    const auto first = geom::to_double3<C>(sk.pos(in[0])), last = geom::to_double3<C>(sk.pos(in[in.size() - 1]));
    UT_CHECK(d2(first, pa) < d2(first, pb) && d2(last, pb) < d2(last, pa));
  }
}

/* a curved wire is checked one skeleton segment at a time: its chord misses
 * a vertical face, its arc crosses it (local and full validation) */
template <geom::CoordType C> void test_wire_chain()
{
  for (const ValidateMode mode : {ValidateMode::Local, ValidateMode::Full}) {
    Fixture<C> fx;
    UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
    Editor<C> ed(fx.k, &fx.j);
    ed.options().validate = mode;
    VertRef v[4];
    const double xz[4][2] = {{620, 200}, {880, 200}, {880, 400}, {620, 400}};
    for (int i = 0; i < 4; i++) {
      v[i] = vert(ed, xz[i][0], 50, xz[i][1], 7600 + i);
    }
    ed.begin("wall");
    for (int i = 0; i < 4; i++) {
      EXPECT_EDIT(ed, ed.make_edge(v[i], v[(i + 1) % 4]), EditCode::Ok);
    }
    const uint32_t sizes[1] = {4};
    EXPECT_EDIT(ed, ed.make_face(v, sizes, test::pid(7700)), EditCode::Ok);
    UT_CHECK(ed.commit() == EditCode::Ok);
    EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
    const VertRef a = vert(ed, 600, 0, 300, 7610), b = vert(ed, 900, 0, 300, 7611);
    EdgeRef w;
    EXPECT_EDIT(ed, ed.make_edge(a, b, ident::kNoId, &w, EmbedTarget::region(RegionRef(0))), EditCode::Ok);
    UT_CHECK(fx.k.edge_verts(w)[0] == a && fx.valid());
    /* heading +1.2 rad: the arc reaches y = 50 near x = 642 and 858 */
    EXPECT_EDIT(ed, set_arc(ed, w, -1.2), EditCode::Invalid);
    UT_CHECK(has_issue(ed.report().validation, ValidateCode::WireCrossesFace, ident::ElemKind::Edge));
    EXPECT_EDIT(ed, set_arc(ed, w, 1.2), EditCode::Ok); /* away from the wall */
    UT_CHECK(fx.valid());
  }
}

/* a face's grid box covers its skeleton: an arc bulging out of the vertex
 * box, and a wire through the bulge */
template <geom::CoordType C> void test_bulge_box()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  Editor<C> ed(fx.k, &fx.j);
  ed.options().validate = ValidateMode::Local;
  VertRef v[4];
  const double xy[4][2] = {{600, 0}, {900, 0}, {900, 40}, {600, 40}};
  for (int i = 0; i < 4; i++) {
    v[i] = vert(ed, xy[i][0], xy[i][1], 500, 7800 + i);
  }
  ed.begin("strip");
  EdgeRef e;
  EXPECT_EDIT(ed, ed.make_edge(v[0], v[1], ident::kNoId, &e), EditCode::Ok);
  for (int i = 1; i < 4; i++) {
    EXPECT_EDIT(ed, ed.make_edge(v[i], v[(i + 1) % 4]), EditCode::Ok);
  }
  const uint32_t sizes[1] = {4};
  EXPECT_EDIT(ed, ed.make_face(v, sizes, test::pid(7900)), EditCode::Ok);
  UT_CHECK(ed.commit() == EditCode::Ok);
  UT_CHECK(fx.k.edge_verts(e)[0] == v[0]);
  EXPECT_EDIT(ed, set_arc(ed, e, 1.2), EditCode::Ok); /* bulges to y of about -100 */
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  UT_CHECK(fx.valid());
  const VertRef a = vert(ed, 750, -50, 400, 7810), b = vert(ed, 750, -50, 600, 7811);
  EXPECT_EDIT(ed, ed.make_edge(a, b, ident::kNoId, nullptr, EmbedTarget::region(RegionRef(0))), EditCode::Invalid);
  UT_CHECK(has_issue(ed.report().validation, ValidateCode::WireCrossesFace, ident::ElemKind::Edge));
}

/* split_face assigns holes by the children's skeletons: a hole inside the
 * bulge of a curved edge goes to the child that owns that edge, whichever way
 * the chord runs */
template <geom::CoordType C> void test_split_bulge()
{
  for (const bool bd : {true, false}) {
    Fixture<C> fx;
    UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
    Editor<C> ed(fx.k, &fx.j);
    VertRef v[4];
    const double xy[4][2] = {{600, 0}, {900, 0}, {900, 300}, {600, 300}};
    for (int i = 0; i < 4; i++) {
      v[i] = vert(ed, xy[i][0], xy[i][1], 500, 8000 + i);
    }
    ed.begin("square");
    EdgeRef e;
    EXPECT_EDIT(ed, ed.make_edge(v[0], v[1], ident::kNoId, &e), EditCode::Ok);
    for (int i = 1; i < 4; i++) {
      EXPECT_EDIT(ed, ed.make_edge(v[i], v[(i + 1) % 4]), EditCode::Ok);
    }
    const uint32_t sizes[1] = {4};
    FaceRef f;
    EXPECT_EDIT(ed, ed.make_face(v, sizes, test::pid(8100), 0, ident::kNoId, ident::kNoId, &f), EditCode::Ok);
    UT_CHECK(ed.commit() == EditCode::Ok);
    UT_CHECK(fx.k.edge_verts(e)[0] == v[0]);
    EXPECT_EDIT(ed, set_arc(ed, e, 1.2), EditCode::Ok); /* bulges to y of about -100 */
    EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
    /* a hole in the bulge, outside the square of the stored vertices */
    const EmbedTarget in = EmbedTarget::face(f);
    const double hxy[4][2] = {{730, -70}, {770, -70}, {770, -30}, {730, -30}};
    VertRef h[4];
    ed.begin("hole");
    for (int i = 0; i < 4; i++) {
      h[i] = vert(ed, hxy[i][0], hxy[i][1], 500, 8010 + i);
      EXPECT_EDIT(ed, ed.embed_vertex(h[i], in), EditCode::Ok);
    }
    EdgeRef he[4];
    for (int i = 0; i < 4; i++) {
      EXPECT_EDIT(ed, ed.make_edge(h[i], h[(i + 1) % 4], ident::kNoId, &he[i], in), EditCode::Ok);
    }
    EXPECT_EDIT(ed, ed.add_hole(f, he), EditCode::Ok);
    UT_CHECK(ed.commit() == EditCode::Ok);
    UT_CHECK(fx.valid());
    /* the chord b -> d or d -> b */
    EXPECT_EDIT(ed, bd ? ed.split_face(f, v[1], v[3]) : ed.split_face(f, v[3], v[1]), EditCode::Ok);
    UT_CHECK(fx.valid());
    const FaceRef owner = fx.k.face_of(fx.k.edge_coedge(e));
    uint32_t loops = 0;
    fx.k.for_each_loop(owner, [&](LoopRef) { loops++; });
    UT_CHECK(loops == 2); /* the outer loop and the hole */
  }
}

/* a "D" face: its stored vertices a, m, b are collinear and the arc b -> a
 * closes it. Planarity falls back to the skeleton, and the half-planes of
 * its straight edges need skeleton points (no stored vertex is off their
 * line). A triangle below shares a -> m, so that edge's radial order is
 * computed. */
template <geom::CoordType C> void test_d_face()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  Editor<C> ed(fx.k, &fx.j);
  const VertRef a = vert(ed, 600, 0, 500, 8200), m = vert(ed, 750, 0, 500, 8201), b = vert(ed, 900, 0, 500, 8202),
                c = vert(ed, 675, -100, 500, 8203);
  ed.begin("d");
  EdgeRef arc;
  EXPECT_EDIT(ed, ed.make_edge(a, m), EditCode::Ok);
  EXPECT_EDIT(ed, ed.make_edge(m, b), EditCode::Ok);
  EXPECT_EDIT(ed, ed.make_edge(b, a, ident::kNoId, &arc), EditCode::Ok);
  EXPECT_EDIT(ed, ed.make_edge(m, c), EditCode::Ok);
  EXPECT_EDIT(ed, ed.make_edge(c, a), EditCode::Ok);
  /* the arc bulges +y, away from the triangle: from a (along +x) it turns
   * right, from b (along -x) left */
  EXPECT_EDIT(ed, set_arc(ed, arc, fx.k.edge_verts(arc)[0] == b ? 0.8 : -0.8), EditCode::Ok);
  const uint32_t three[1] = {3};
  const VertRef d_loop[3] = {a, m, b}, t_loop[3] = {m, a, c};
  EXPECT_EDIT(ed, ed.make_face(d_loop, three, test::pid(8300)), EditCode::Ok);
  EXPECT_EDIT(ed, ed.make_face(t_loop, three, test::pid(8301)), EditCode::Ok);
  UT_CHECK(ed.commit() == EditCode::Ok);
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  UT_CHECK(fx.valid());
}

/* an island imprint takes inclusions by the cycle's skeleton: a vertex in
 * the outward bulge of the cycle's curved side, outside its vertex triangle,
 * goes to the island face */
template <geom::CoordType C> void test_island_bulge()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {256, 256, 256}));
  Editor<C> ed(fx.k, &fx.j);
  VertRef v[4];
  const double xy[4][2] = {{600, 0}, {900, 0}, {900, 300}, {600, 300}};
  for (int i = 0; i < 4; i++) {
    v[i] = vert(ed, xy[i][0], xy[i][1], 500, 8400 + i);
  }
  ed.begin("square");
  for (int i = 0; i < 4; i++) {
    EXPECT_EDIT(ed, ed.make_edge(v[i], v[(i + 1) % 4]), EditCode::Ok);
  }
  const uint32_t sizes[1] = {4};
  FaceRef f;
  EXPECT_EDIT(ed, ed.make_face(v, sizes, test::pid(8500), 0, ident::kNoId, ident::kNoId, &f), EditCode::Ok);
  UT_CHECK(ed.commit() == EditCode::Ok);
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  const EmbedTarget in = EmbedTarget::face(f);
  const double cxy[4][2] = {{700, 100}, {800, 100}, {750, 200}, {750, 88}};
  VertRef p[4];
  ed.begin("cycle");
  for (int i = 0; i < 4; i++) {
    p[i] = vert(ed, cxy[i][0], cxy[i][1], 500, 8410 + i);
    EXPECT_EDIT(ed, ed.embed_vertex(p[i], in), EditCode::Ok);
  }
  EdgeRef ce[3];
  for (int i = 0; i < 3; i++) {
    EXPECT_EDIT(ed, ed.make_edge(p[i], p[(i + 1) % 3], ident::kNoId, &ce[i], in), EditCode::Ok);
  }
  UT_CHECK(ed.commit() == EditCode::Ok);
  UT_CHECK(fx.k.edge_verts(ce[0])[0] == p[0]);
  /* heading -1 rad from p0: a sagitta of about 27, down to y = 73 */
  EXPECT_EDIT(ed, set_arc(ed, ce[0], 1.0), EditCode::Ok);
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  UT_CHECK(fx.valid());
  FaceRef island;
  EXPECT_EDIT(ed, ed.imprint(f, ce, &island), EditCode::Ok);
  UT_CHECK(fx.valid());
  const ident::ContainerRef cu = fx.k.vert_container(p[3]);
  UT_CHECK(cu.kind() == ident::ContainerKind::Face && cu.index() != f.i);
}

template <geom::CoordType C> void all()
{
  test_no_curves<C>();
  test_shared<C>();
  test_insertion_order<C>();
  test_radial_curved<C>();
  test_degenerate<C>();
  test_reversed_range<C>();
  test_wire_chain<C>();
  test_bulge_box<C>();
  test_split_bulge<C>();
  test_d_face<C>();
  test_island_bulge<C>();
}

} // namespace

int main()
{
  register_column_types();
  all<double>();
  all<geom::Lattice>();
  return ut_test_end();
}
