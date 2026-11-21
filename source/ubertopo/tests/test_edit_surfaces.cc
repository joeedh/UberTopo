/* Editing surfaced faces (plan P4b §5, §7): intern_geom for surface kinds,
 * set_face_surface and set_coedge_pcurve with undo, redo and the journal's
 * roots, the pcurve rules of split_edge_at and join_edge, the SurfacedFace
 * refusals, and barriers.
 *
 * The sheet is support/road_sheet.h, built without surfaces; the editor
 * surfaces it. */

#include <cstdio>
#include <span>
#include <string_view>
#include <utility>

#include "support/road_sheet.h"
#include "ut_test.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using litestl::attr::PoolRef;
using test::build_sheet;
using test::kStation;
using test::kStations;
using test::Sheet;
namespace util = litestl::util;
namespace attr = litestl::attr;

namespace {

using Pcs = util::Vector<std::pair<CoedgeRef, PoolRef>, 4>;

std::span<const std::pair<CoedgeRef, PoolRef>> span_of(const Pcs &p)
{
  return std::span<const std::pair<CoedgeRef, PoolRef>>(p.data(), p.size());
}

/* the pcurves of face f[i][j]: borders j and j + 1, cross edges i and i + 1 */
Pcs face_pcurves(Sheet &sh, int i, int j)
{
  const FaceRef f = sh.f[i][j];
  Pcs out;
  out.append({sh.coedge(sh.along[i][j], f), sh.rb.pcurves[j]});
  out.append({sh.coedge(sh.along[i][j + 1], f), sh.rb.pcurves[j + 1]});
  out.append({sh.coedge(sh.across[i][j], f), sh.cross_pcurve(sh.across[i][j])});
  out.append({sh.coedge(sh.across[i + 1][j], f), sh.cross_pcurve(sh.across[i + 1][j])});
  return out;
}

/* the last refusal's detail names this reason: several argument refusals share
 * BadArgument, so the code alone can't tell which check fired */
bool detail_has(const Editor<double> &ed, const char *text)
{
  return ed.report().detail && std::string_view(ed.report().detail).find(text) != std::string_view::npos;
}

EditCode surface_face(Editor<double> &ed, Sheet &sh, int i, int j, bool reversed = false)
{
  const Pcs pcs = face_pcurves(sh, i, j);
  return ed.set_face_surface(sh.f[i][j], sh.rb.road, reversed, span_of(pcs));
}

bool surface_all(Editor<double> &ed, Sheet &sh)
{
  bool ok = true;
  for (int i = 0; i + 1 < kStations; i++) {
    for (int j = 0; j + 1 < sh.nb; j++) {
      const EditCode c = surface_face(ed, sh, i, j);
      EXPECT_EDIT(ed, c, EditCode::Ok);
      ok = ok && c == EditCode::Ok;
    }
  }
  return ok;
}

int surfaced_faces(const Complex<double> &k)
{
  int n = 0;
  k.faces.for_each_live([&](uint32_t f) { n += !k.faces.template get<".f.surface">()[f].none(); });
  return n;
}

/* intern_geom takes surface kinds: their evaluator is a SurfaceEval */
void test_intern_geom()
{
  Sheet sh;
  UT_CHECK(build_sheet(sh, false));
  Complex<double> &k = sh.k();
  Editor<double> ed(k, &sh.fx.j);
  const auto *road = k.geom.try_get<curves::Road3Pool>(sh.rb.road);
  UT_CHECK(road != nullptr);
  if (!road) {
    return;
  }
  /* the road's own definitions again: the same entries */
  PoolRef r;
  EXPECT_EDIT(ed, ed.intern_geom(curves::road3_kind(), *road, &r), EditCode::Ok);
  UT_CHECK(r == sh.rb.road);
  const auto *g = k.geom.try_get<curves::Graph2Pool>(sh.rb.pcurves[1]);
  UT_CHECK(g != nullptr);
  if (g) {
    EXPECT_EDIT(ed, ed.intern_curve(curves::graph2_kind(), *g, &r), EditCode::Ok);
    UT_CHECK(r == sh.rb.pcurves[1]);
  }
  curves::Segment2 sg;
  UT_CHECK(curves::Segment2::make(1, 0, 2, 1, sg) == nullptr);
  EXPECT_EDIT(ed, ed.intern_geom(curves::segment2_kind(), sg, &r), EditCode::Ok);
  UT_CHECK(k.geom.live(r));
  /* refusals: the wrong type for the kind; an inner ref of a refused kind;
   * a road on a reference too short for its borders (no evaluator) */
  EXPECT_EDIT(ed, ed.intern_geom(curves::road3_kind(), sg, &r), EditCode::BadArgument);
  curves::Road3 bad = *road;
  bad.reference = sh.rb.offsets[0].none() ? sh.rb.pcurves[0] : sh.rb.offsets[0];
  EXPECT_EDIT(ed, ed.intern_geom(curves::road3_kind(), bad, &r), EditCode::BadArgument);
  curves::Cesaro2 shortref;
  UT_CHECK(curves::Cesaro2::line(0, 0, 0, 100, shortref) == nullptr);
  PoolRef sref;
  EXPECT_EDIT(ed, ed.intern_geom(curves::cesaro2_kind(), shortref, &sref), EditCode::Ok);
  bad.reference = sref;
  EXPECT_EDIT(ed, ed.intern_geom(curves::road3_kind(), bad, &r), EditCode::BadArgument);
  UT_CHECK(detail_has(ed, "evaluator"));
}

/* set_face_surface: whole faces in Full validation, undo and redo, clearing,
 * and the argument and validation refusals */
void test_set_surface()
{
  Sheet sh;
  UT_CHECK(build_sheet(sh, false));
  Complex<double> &k = sh.k();
  Editor<double> ed(k, &sh.fx.j);
  const attr::PoolStore &st = k.geom;
  (void)st;
  const ident::PersistentId h0 = test::h(k);
  /* refusals first: each leaves the content hash */
  const FaceRef f = sh.f[1][1];
  Pcs pcs = face_pcurves(sh, 1, 1);
  {
    Pcs missing = pcs; /* a coedge without a pcurve on a surfaced face */
    missing.pop_back();
    EXPECT_EDIT(ed, ed.set_face_surface(f, sh.rb.road, false, span_of(missing)), EditCode::Invalid);
    UT_CHECK(ed.report().validation.issues.size() && ed.report().validation.issues[0].code == ValidateCode::PcurveKind);
    Pcs twice = pcs;
    twice.append(pcs[0]);
    EXPECT_EDIT(ed, ed.set_face_surface(f, sh.rb.road, false, span_of(twice)), EditCode::BadArgument);
    UT_CHECK(detail_has(ed, "listed twice"));
    Pcs foreign = pcs; /* a coedge of the neighbouring face */
    foreign[0].first = sh.coedge(sh.along[1][1], sh.f[1][0]);
    EXPECT_EDIT(ed, ed.set_face_surface(f, sh.rb.road, false, span_of(foreign)), EditCode::BadArgument);
    UT_CHECK(detail_has(ed, "not a coedge of the face"));
    Pcs dead = pcs;
    dead[2].second = PoolRef{dead[2].second.pool, 100000};
    EXPECT_EDIT(ed, ed.set_face_surface(f, sh.rb.road, false, span_of(dead)), EditCode::BadArgument);
    UT_CHECK(detail_has(ed, "pcurve is not a live"));
    EXPECT_EDIT(ed, ed.set_face_surface(f, PoolRef{sh.rb.road.pool, 100000}, false, span_of(pcs)), EditCode::BadArgument);
    UT_CHECK(detail_has(ed, "surface is not a live"));
    EXPECT_EDIT(ed, ed.set_face_surface(f, PoolRef{}, false, span_of(pcs)), EditCode::BadArgument);
    EXPECT_EDIT(ed, ed.set_face_surface(f, PoolRef{}, true), EditCode::BadArgument);
    /* the lane's normal is up, as the face's front: Reversed is wrong */
    EXPECT_EDIT(ed, ed.set_face_surface(f, sh.rb.road, true, span_of(pcs)), EditCode::Invalid);
    UT_CHECK(ed.report().validation.issues.size() &&
             ed.report().validation.issues[0].code == ValidateCode::SurfaceOrientation);
    EXPECT_EDIT(ed, ed.set_face_surface(FaceRef(k.faces.capacity()), sh.rb.road, false), EditCode::DeadElement);
    UT_CHECK(test::h(k) == h0);
  }
  /* the whole sheet, one face per primitive */
  UT_CHECK(surface_all(ed, sh));
  UT_CHECK(surfaced_faces(k) == 4 * (sh.nb - 1) && sh.fx.valid());
  const ident::PersistentId h1 = test::h(k);
  /* set_coedge_pcurve: the same pcurve again; a wrong one is caught */
  const CoedgeRef cl = sh.coedge(sh.along[1][1], f);
  EXPECT_EDIT(ed, ed.set_coedge_pcurve(cl, sh.rb.pcurves[1]), EditCode::Ok);
  UT_CHECK(test::h(k) == h1);
  EXPECT_EDIT(ed, ed.set_coedge_pcurve(cl, sh.rb.pcurves[0]), EditCode::Invalid);
  EXPECT_EDIT(ed, ed.set_coedge_pcurve(cl, PoolRef{sh.rb.road.pool, 100000}), EditCode::BadArgument);
  EXPECT_EDIT(ed, ed.set_coedge_pcurve(CoedgeRef(k.coedges.capacity()), PoolRef{}), EditCode::DeadElement);
  /* flipped, then cleared: the pcurves go and so does Reversed; surfaced
   * again with Reversed, as the flipped front now needs */
  EXPECT_EDIT(ed, ed.flip_face(f), EditCode::Ok);
  UT_CHECK((k.face_flags(f) & face_flags::Reversed) != 0 && sh.fx.valid());
  EXPECT_EDIT(ed, ed.set_face_surface(f, PoolRef{}, false), EditCode::Ok);
  UT_CHECK(surfaced_faces(k) == 4 * (sh.nb - 1) - 1 && sh.fx.valid());
  k.for_each_loop(f, [&](LoopRef l) {
    k.for_each_coedge(l, [&](CoedgeRef c) { UT_CHECK(k.coedges.template get<".c.pcurve">()[c.i].none()); });
  });
  UT_CHECK((k.face_flags(f) & face_flags::Reversed) == 0);
  EXPECT_EDIT(ed, ed.set_face_surface(f, sh.rb.road, false, span_of(pcs)), EditCode::Invalid);
  EXPECT_EDIT(ed, ed.set_face_surface(f, sh.rb.road, true, span_of(pcs)), EditCode::Ok);
  UT_CHECK(surfaced_faces(k) == 4 * (sh.nb - 1) && sh.fx.valid());
  const ident::PersistentId h2 = test::h(k);
  /* undo back to the start, redo to the end */
  const int n_ops = 4 * (sh.nb - 1) + 4; /* the faces, the same pcurve, flip, clear, set */
  for (int n = 0; n < n_ops; n++) {
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
    if (n == 2) {
      UT_CHECK(test::h(k) == h1);
    }
  }
  UT_CHECK(test::h(k) == h0 && surfaced_faces(k) == 0);
  for (int n = 0; n < n_ops; n++) {
    EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  }
  UT_CHECK(test::h(k) == h2 && sh.fx.valid());
}

/* the journal roots both payloads: an undone set survives a collect and
 * redoes (no validation, so the definitions need not fit) */
void test_journal_roots()
{
  Sheet sh;
  UT_CHECK(build_sheet(sh, false));
  Complex<double> &k = sh.k();
  k.geom.set_reuse_freed(false);
  Editor<double> ed(k, &sh.fx.j);
  ed.options().validate = ValidateMode::None;
  const FaceRef f = sh.f[2][1];
  const Pcs pcs = face_pcurves(sh, 2, 1);
  /* SetFaceSurface: the road3 and the cross pcurves are reached by nothing else */
  EXPECT_EDIT(ed, ed.set_face_surface(f, sh.rb.road, false, span_of(pcs)), EditCode::Ok);
  const ident::PersistentId h1 = test::h(k);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  EXPECT_EDIT(ed, ed.collect_geometry(), EditCode::Ok);
  UT_CHECK(k.geom.live(sh.rb.road) && k.geom.live(pcs[2].second) && k.geom.live(pcs[3].second));
  EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  UT_CHECK(test::h(k) == h1);
  /* SetCoedgePcurve: a fresh segment2 on one coedge */
  PoolRef fresh;
  curves::Segment2 sg;
  UT_CHECK(curves::Segment2::make(3, 1, 4, 2, sg) == nullptr);
  EXPECT_EDIT(ed, ed.intern_geom(curves::segment2_kind(), sg, &fresh), EditCode::Ok);
  EXPECT_EDIT(ed, ed.set_coedge_pcurve(pcs[2].first, fresh), EditCode::Ok);
  const ident::PersistentId h2 = test::h(k);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  EXPECT_EDIT(ed, ed.collect_geometry(), EditCode::Ok);
  UT_CHECK(k.geom.live(fresh));
  EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  UT_CHECK(test::h(k) == h2);
  /* without the journal, the replaced cross pcurve goes */
  sh.fx.j.clear();
  EXPECT_EDIT(ed, ed.collect_geometry(), EditCode::Ok);
  UT_CHECK(!k.geom.live(pcs[2].second) && k.geom.live(fresh) && k.geom.live(sh.rb.road));
}

/* split_edge_at copies the pcurves; join_edge needs them equal */
void test_split_join()
{
  Sheet sh;
  UT_CHECK(build_sheet(sh, false));
  Complex<double> &k = sh.k();
  Editor<double> ed(k, &sh.fx.j);
  UT_CHECK(surface_all(ed, sh));
  const ident::PersistentId h0 = test::h(k);
  /* the border between links 0 and 1, stations 56..112: two road faces */
  const EdgeRef e = sh.along[1][1];
  const auto rg = k.edges.template get<".e.range">()[e.i];
  VertRef v;
  EdgeRef e2;
  EXPECT_EDIT(ed, ed.split_edge_at(e, 0.5 * (rg[0] + rg[1]), &v, &e2), EditCode::Ok);
  int n = 0;
  for (const EdgeRef x : {e, e2}) {
    k.for_each_radial(x, [&](CoedgeRef c) {
      UT_CHECK(k.coedges.template get<".c.pcurve">()[c.i] == sh.rb.pcurves[1]);
      n++;
    });
  }
  UT_CHECK(n == 4 && sh.fx.valid());
  EdgeRef surv;
  EXPECT_EDIT(ed, ed.join_edge(v, &surv), EditCode::Ok);
  UT_CHECK(sh.fx.valid());
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  UT_CHECK(test::h(k) == h0);
  /* different pcurves on matching coedges: no join (unvalidated, so the
   * mismatch can be built) */
  EXPECT_EDIT(ed, ed.split_edge_at(e, 0.5 * (rg[0] + rg[1]), &v, &e2), EditCode::Ok);
  ed.options().validate = ValidateMode::None;
  CoedgeRef c2;
  k.for_each_radial(e2, [&](CoedgeRef c) { c2 = c; });
  EXPECT_EDIT(ed, ed.set_coedge_pcurve(c2, sh.rb.pcurves[0]), EditCode::Ok);
  EXPECT_EDIT(ed, ed.join_edge(v), EditCode::Incompatible);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  ed.options().validate = ValidateMode::Full;
  EXPECT_EDIT(ed, ed.join_edge(v), EditCode::Ok);
  UT_CHECK(sh.fx.valid());
}

/* the edits P4b refuses on surfaced faces (plan P4b §5.2, OI-P4b-2), and
 * the ones it allows */
void test_refusals()
{
  Sheet sh;
  UT_CHECK(build_sheet(sh, false));
  Complex<double> &k = sh.k();
  Editor<double> ed(k, &sh.fx.j);
  UT_CHECK(surface_all(ed, sh));
  UT_CHECK(surfaced_faces(k) == 4 * (sh.nb - 1) && sh.fx.valid());
  const ident::PersistentId h0 = test::h(k);
  const FaceRef f = sh.f[1][1];
  const auto corner = [&](int i, int j) { return sh.v[i][j]; };
  /* refused */
  EXPECT_EDIT(ed, ed.split_edge(sh.across[1][1], k.pos(corner(1, 1)), nullptr, nullptr), EditCode::SurfacedFace);
  EXPECT_EDIT(ed, ed.split_face(f, corner(1, 1), corner(2, 2)), EditCode::SurfacedFace);
  EXPECT_EDIT(ed, ed.join_faces(sh.across[2][1]), EditCode::SurfacedFace);
  EXPECT_EDIT(ed, ed.unimprint(sh.across[2][1]), EditCode::SurfacedFace);
  EXPECT_EDIT(ed, ed.imprint(f, {}), EditCode::SurfacedFace);
  EXPECT_EDIT(ed, ed.add_hole(f, {}), EditCode::SurfacedFace);
  LoopRef outer;
  k.for_each_loop(f, [&](LoopRef l) { outer = outer ? outer : l; });
  EXPECT_EDIT(ed, ed.remove_hole(f, outer), EditCode::SurfacedFace);
  {
    /* weld a coincident loose vertex onto a road vertex */
    ed.options().validate = ValidateMode::None;
    VertRef w;
    EXPECT_EDIT(ed, ed.make_vertex(k.pos(corner(2, 1)), ident::derive_generator(ident::PersistentId{}, "w"), &w),
                EditCode::Ok);
    const std::pair<VertRef, VertRef> pr[1] = {{corner(2, 1), w}};
    EXPECT_EDIT(ed, ed.weld(pr), EditCode::SurfacedFace);
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
    ed.options().validate = ValidateMode::Full;
  }
  UT_CHECK(test::h(k) == h0);
  /* allowed: flip_face (Reversed follows), kill_face, and set_position
   * (validation catches a vertex off the surface; here the border carriers'
   * CurveEndpoint first) */
  EXPECT_EDIT(ed, ed.flip_face(f), EditCode::Ok);
  UT_CHECK((k.face_flags(f) & face_flags::Reversed) != 0 && sh.fx.valid());
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  EXPECT_EDIT(ed, ed.kill_face(sh.f[3][2]), EditCode::Ok);
  UT_CHECK(sh.fx.valid());
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  const auto p = k.pos(corner(2, 2));
  EXPECT_EDIT(ed, ed.set_position(corner(2, 2), geom::Point<double>(p[0], p[1], p[2] + 0.25)), EditCode::Invalid);
  UT_CHECK(test::h(k) == h0);
}

/* join_edge and split_edge on straight edges of a surfaced face: a box,
 * whose grid points make an exact midpoint, under a road3 it doesn't fit
 * (no validation; the refusal comes first) */
void test_refusals_straight()
{
  test::Fixture<double> fx;
  UT_CHECK(fx.box());
  Editor<double> ed(fx.k, &fx.j);
  ed.options().validate = ValidateMode::None;
  curves::Cesaro2 c;
  UT_CHECK(curves::Cesaro2::line(0, 0, 0, 100, c) == nullptr);
  PoolRef ref, road;
  EXPECT_EDIT(ed, ed.intern_geom(curves::cesaro2_kind(), c, &ref), EditCode::Ok);
  util::Vector<curves::Fn1D, 2> bs, zs;
  bs.append(curves::Fn1D::constant(-1, 0, 100));
  bs.append(curves::Fn1D::constant(1, 0, 100));
  zs.append(curves::Fn1D::constant(0, 0, 100));
  zs.append(curves::Fn1D::constant(0, 0, 100));
  curves::Road3 d;
  UT_CHECK(curves::Road3::make(fx.k.geom, ref, bs, zs, d) == nullptr);
  EXPECT_EDIT(ed, ed.intern_geom(curves::road3_kind(), d, &road), EditCode::Ok);
  const EdgeRef e = fx.edge_between({0, 0, 0}, {4, 0, 0});
  VertRef m;
  EXPECT_EDIT(ed, ed.split_edge(e, test::pt<double>({2, 0, 0}), &m), EditCode::Ok);
  const FaceRef f = fx.face_with({{0, 0, 0}, {4, 0, 0}, {0, 4, 0}});
  UT_CHECK(bool(f));
  EXPECT_EDIT(ed, ed.set_face_surface(f, road, false), EditCode::Ok);
  EXPECT_EDIT(ed, ed.join_edge(m), EditCode::SurfacedFace);
  const EdgeRef e2 = fx.edge_between({0, 0, 0}, {0, 4, 0});
  EXPECT_EDIT(ed, ed.split_edge(e2, test::pt<double>({0, 2, 0})), EditCode::SurfacedFace);
  /* without the surface both go through */
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  EXPECT_EDIT(ed, ed.join_edge(m), EditCode::Ok);
  EXPECT_EDIT(ed, ed.split_edge(e2, test::pt<double>({0, 2, 0})), EditCode::Ok);
}

/* barriers keep surfaces and pcurves; undoing them restores the groups */
void test_barriers()
{
  Sheet sh;
  UT_CHECK(build_sheet(sh, false));
  Complex<double> &k = sh.k();
  Editor<double> ed(k, &sh.fx.j);
  UT_CHECK(surface_all(ed, sh));
  EXPECT_EDIT(ed, ed.kill_face(sh.f[0][0]), EditCode::Ok);
  const ident::PersistentId h0 = test::h(k);
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  EXPECT_EDIT(ed, ed.compact(), EditCode::Ok);
  UT_CHECK(surfaced_faces(k) == 4 * (sh.nb - 1) - 1 && sh.fx.valid());
  UT_CHECK(k.geom.live(sh.rb.road));
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  UT_CHECK(test::h(k) == h0);
  EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  UT_CHECK(surfaced_faces(k) == 4 * (sh.nb - 1) - 1 && sh.fx.valid());
}

} // namespace

int main()
{
  test_intern_geom();
  test_set_surface();
  test_journal_roots();
  test_split_join();
  test_refusals();
  test_refusals_straight();
  test_barriers();
  return ut_test_end();
}
