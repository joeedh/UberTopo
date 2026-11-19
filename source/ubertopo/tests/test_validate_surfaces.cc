/* Surface validation (plan P4b §4.2, §7): a valid road sheet, then one
 * corruption per new code, each restored before the next.
 *
 * The sheet: the superelevation fixture's reference (clothoid, arc,
 * clothoid; 224 m), three links (two right, one left, the left one widening),
 * cut at five stations into 4 x 3 projected lane faces. Border edges carry
 * the builder's alignment3 carriers and graph2 pcurves; cross edges are
 * straight with segment2 pcurves. Surfaces and pcurves are written straight
 * into the columns, as the editor gains its primitives only in W5. */

#include <cstdio>
#include <vector>

#include "support/edit_fixture.h"
#include "support/road_fixtures.h"
#include "ubertopo/curves/alignment.h"
#include "ubertopo/curves/offset.h"
#include "ubertopo/curves/pcurve.h"
#include "ubertopo/curves/road.h"
#include "ubertopo/curves/road_build.h"
#include "ut_test.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using litestl::attr::PoolRef;
using test::Fixture;
namespace util = litestl::util;
namespace attr = litestl::attr;

namespace {

constexpr int kStations = 5;
constexpr double kStation[kStations] = {0, 56, 112, 168, 224};

struct Sheet {
  Fixture<double> fx;
  curves::RoadBuild rb;
  int nb = 0;                                 /* borders */
  std::vector<std::vector<VertRef>> v;        /* v[i][j]: station i, border j */
  std::vector<std::vector<FaceRef>> f;        /* f[i][k]: stations i..i+1, link k */
  std::vector<std::vector<EdgeRef>> along;    /* along[i][j]: border j, stations i..i+1 */
  std::vector<std::vector<EdgeRef>> across;   /* across[i][k]: station i, link k */

  Complex<double> &k()
  {
    return fx.k;
  }
  PoolRef segment(double u0, double v0, double u1, double v1)
  {
    curves::Segment2 sg;
    UT_CHECK(curves::Segment2::make(u0, v0, u1, v1, sg) == nullptr);
    return k().geom.intern<curves::Segment2Pool>(curves::pool_id(curves::segment2_kind()), std::move(sg));
  }
  PoolRef graph(const curves::Fn1D &fn)
  {
    curves::Graph2 g;
    UT_CHECK(curves::Graph2::make(fn, g) == nullptr);
    return k().geom.intern<curves::Graph2Pool>(curves::pool_id(curves::graph2_kind()), std::move(g));
  }
  /* the (s, t) of a vertex on the sheet */
  void param(VertRef x, double &s, double &t) const
  {
    for (int i = 0; i < kStations; i++) {
      for (int j = 0; j < nb; j++) {
        if (v[i][j] == x) {
          s = kStation[i];
          t = rb.borders[j].eval(s);
        }
      }
    }
  }
  /* the segment2 of a straight edge, vs[0] to vs[1] */
  PoolRef cross_pcurve(EdgeRef e)
  {
    const auto vs = k().edge_verts(e);
    double s0 = 0, t0 = 0, s1 = 0, t1 = 0;
    param(vs[0], s0, t0);
    param(vs[1], s1, t1);
    return segment(s0, t0, s1, t1);
  }
  /* the coedge of edge e in face fa */
  CoedgeRef coedge(EdgeRef e, FaceRef fa) const
  {
    CoedgeRef out;
    fx.k.for_each_radial(e, [&](CoedgeRef c) {
      if (fx.k.face_of(c) == fa) {
        out = c;
      }
    });
    return out;
  }
};

/* the road's definition into @p st */
bool make_road(attr::PoolStore &st, curves::RoadBuild &rb)
{
  using namespace curves;
  const test::RoadFixture rf = test::road_fixtures()[1];
  util::Vector<CesaroSegment, 4> segs;
  for (auto &s : rf.ref.segs) {
    segs.append({s[0], Poly::exact({s[1], s[2], s[3], s[4]})});
  }
  Cesaro2 c;
  if (Cesaro2::make(rf.ref.x0, rf.ref.y0, rf.ref.th0, segs, c)) {
    return false;
  }
  const PoolRef ref = st.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(c));
  util::Vector<double, 2> br;
  util::Vector<Poly, 1> ps;
  for (double b : {0.0, 64.0, 160.0, 224.0}) {
    br.append(b);
  }
  ps.append(Poly::exact({20, 2.0 / 64}));
  ps.append(Poly::exact({22, 3.0 / 96}));
  ps.append(Poly::exact({25, 2.0 / 64}));
  Fn1D z;
  if (Fn1D::make(br, ps, z)) {
    return false;
  }
  RoadSection sec;
  sec.right.append({Fn1D::constant(3.75, 0, 224), Fn1D::constant(-1.0 / 32, 0, 224)});
  sec.right.append({Fn1D::constant(1.5, 0, 224), Fn1D::constant(-1.0 / 16, 0, 224)});
  sec.left.append({Fn1D::linear(3.5, 4.0, 0, 224), Fn1D::constant(-1.0 / 32, 0, 224)});
  if (const char *e = build_road(st, store_interner(st), ref, z, sec, rb)) {
    fprintf(stderr, "build_road: %s\n", e);
    return false;
  }
  return true;
}

bool build_sheet(Sheet &sh)
{
  using namespace curves;
  /* the vertices from a scratch store: building the complex replaces its own */
  attr::PoolStore scratch;
  register_pools(scratch);
  if (!make_road(scratch, sh.rb)) {
    return false;
  }
  const attr::PoolStore &st = scratch;
  sh.nb = sh.rb.borders.size();
  /* the lane faces, straight-edged and projected, as a sheet */
  Builder<double> b;
  std::vector<std::vector<uint32_t>> key(kStations, std::vector<uint32_t>(sh.nb));
  for (int i = 0; i < kStations; i++) {
    for (int j = 0; j < sh.nb; j++) {
      const auto cj = curve3(st, sh.rb.carriers[j]);
      Vec<3> p;
      cj->eval(kStation[i], 0, &p);
      key[i][j] = b.vertex(ident::derive_generator(ident::PersistentId{}, "rv", i, j), geom::Point<double>(p[0], p[1], p[2]));
    }
  }
  for (int i = 0; i + 1 < kStations; i++) {
    for (int j = 0; j + 1 < sh.nb; j++) {
      /* s forward on border j, then left to border j + 1: counter-clockwise from above */
      b.face(ident::derive_generator(ident::PersistentId{}, "rf", i, j),
             {{key[i][j], key[i + 1][j], key[i + 1][j + 1], key[i][j + 1]}});
    }
  }
  BuildReport rep;
  BuildPolicy pol;
  pol.allow_projectable_faces = true;
  RegionReport rr;
  if (!b.build(sh.k(), rep, pol) || !build_regions(sh.k(), rr)) {
    fprintf(stderr, "build failed\n");
    return false;
  }
  /* the road again, in the complex's store */
  if (!make_road(sh.k().geom, sh.rb)) {
    return false;
  }
  /* find the elements */
  sh.v.assign(kStations, std::vector<VertRef>(sh.nb));
  sh.k().verts.for_each_live([&](uint32_t x) {
    for (int i = 0; i < kStations; i++) {
      for (int j = 0; j < sh.nb; j++) {
        if (sh.k().verts.template get<".id">()[x] == ident::derive_generator(ident::PersistentId{}, "rv", i, j)) {
          sh.v[i][j] = VertRef(x);
        }
      }
    }
  });
  sh.f.assign(kStations - 1, std::vector<FaceRef>(sh.nb - 1));
  sh.k().faces.for_each_live([&](uint32_t x) {
    for (int i = 0; i + 1 < kStations; i++) {
      for (int j = 0; j + 1 < sh.nb; j++) {
        if (sh.k().face_id(FaceRef(x)) == ident::derive_generator(ident::PersistentId{}, "rf", i, j)) {
          sh.f[i][j] = FaceRef(x);
        }
      }
    }
  });
  Editor<double> ed(sh.k(), &sh.fx.j);
  sh.along.assign(kStations - 1, std::vector<EdgeRef>(sh.nb));
  sh.across.assign(kStations, std::vector<EdgeRef>(sh.nb - 1));
  for (int i = 0; i < kStations; i++) {
    for (int j = 0; j < sh.nb; j++) {
      if (i + 1 < kStations) {
        sh.along[i][j] = ed.find_edge(sh.v[i][j], sh.v[i + 1][j]);
      }
      if (j + 1 < sh.nb) {
        sh.across[i][j] = ed.find_edge(sh.v[i][j], sh.v[i][j + 1]);
      }
    }
  }
  /* curve the border edges */
  UT_CHECK(ed.begin() == EditCode::Ok);
  for (int i = 0; i + 1 < kStations; i++) {
    for (int j = 0; j < sh.nb; j++) {
      const EdgeRef e = sh.along[i][j];
      const bool fwd = sh.k().edge_verts(e)[0] == sh.v[i][j];
      const double a = kStation[i], c2 = kStation[i + 1];
      EXPECT_EDIT(ed, ed.set_edge_curve(e, sh.rb.carriers[j], fwd ? a : c2, fwd ? c2 : a), EditCode::Ok);
    }
  }
  EXPECT_EDIT(ed, ed.commit(), EditCode::Ok);
  /* surfaces and pcurves */
  auto &surf = sh.k().faces.template get<".f.surface">();
  auto &pc = sh.k().coedges.template get<".c.pcurve">();
  for (int i = 0; i + 1 < kStations; i++) {
    for (int j = 0; j + 1 < sh.nb; j++) {
      surf.set(sh.f[i][j].i, sh.rb.road);
    }
  }
  for (int i = 0; i < kStations; i++) {
    for (int j = 0; j < sh.nb; j++) {
      if (i + 1 < kStations) {
        sh.k().for_each_radial(sh.along[i][j], [&](CoedgeRef c) { pc.set(c.i, sh.rb.pcurves[j]); });
      }
      if (j + 1 < sh.nb) {
        const PoolRef sg = sh.cross_pcurve(sh.across[i][j]);
        sh.k().for_each_radial(sh.across[i][j], [&](CoedgeRef c) { pc.set(c.i, sg); });
      }
    }
  }
  return true;
}

/* the only code reported, at least once */
bool only(const Complex<double> &k, ValidateCode want)
{
  const ValidationReport r = validate(k, ValidateLevel::Global);
  bool ok = r.issues.size() > 0;
  for (const ValidationIssue &i : r.issues) {
    ok = ok && i.code == want;
  }
  if (!ok) {
    fprintf(stderr, "wanted only %s, got:\n", validate_code_name(want));
    r.print(stderr);
  }
  return ok;
}

void test_codes()
{
  Sheet sh;
  UT_CHECK(build_sheet(sh));
  Complex<double> &k = sh.k();
  UT_CHECK(sh.fx.valid());
  auto &surf = k.faces.template get<".f.surface">();
  auto &pc = k.coedges.template get<".c.pcurve">();
  const FaceRef f = sh.f[1][1]; /* stations 56..112, link 1 (the right lane) */
  const EdgeRef lower = sh.along[1][1], upper = sh.along[1][2], start = sh.across[1][1];
  const CoedgeRef cl = sh.coedge(lower, f), cu = sh.coedge(upper, f), cs = sh.coedge(start, f);
  UT_CHECK(bool(cl) && bool(cu) && bool(cs));
  auto restore = [&](CoedgeRef c, PoolRef r) {
    pc.set(c.i, r);
    UT_CHECK(sh.fx.valid());
  };

  /* SurfaceKind: a pcurve entry as the surface */
  surf.set(f.i, sh.rb.pcurves[1]);
  UT_CHECK(only(k, ValidateCode::SurfaceKind));
  surf.set(f.i, sh.rb.road);
  UT_CHECK(sh.fx.valid());

  /* PcurveKind: missing; segment2 on a curved edge; graph2 on a straight one */
  const PoolRef pl = pc[cl.i], ps = pc[cs.i];
  pc.set(cl.i, PoolRef{});
  UT_CHECK(only(k, ValidateCode::PcurveKind));
  pc.set(cl.i, ps);
  UT_CHECK(only(k, ValidateCode::PcurveKind));
  restore(cl, pl);
  pc.set(cs.i, pl);
  UT_CHECK(only(k, ValidateCode::PcurveKind));
  restore(cs, ps);

  /* PcurveRange: a graph2 ending before the edge; a segment2 off the road's u domain */
  pc.set(cl.i, sh.graph(curves::Fn1D::constant(sh.rb.borders[1].eval(0), 0, 100)));
  UT_CHECK(only(k, ValidateCode::PcurveRange));
  restore(cl, pl);
  {
    double s0, t0, s1, t1;
    const auto vs = k.edge_verts(start);
    sh.param(vs[0], s0, t0);
    sh.param(vs[1], s1, t1);
    pc.set(cs.i, sh.segment(-1, t0, -1, t1));
    UT_CHECK(only(k, ValidateCode::PcurveRange));
    restore(cs, ps);

    /* PcurveVertex at each end alone: the other end stays exact, and the
     * moved one moves inward, so the pcurve stays in the link */
    const double in = t1 > t0 ? 0.25 : -0.25;
    pc.set(cs.i, sh.segment(s0, t0, s1, t1 - in));
    UT_CHECK(only(k, ValidateCode::PcurveVertex));
    pc.set(cs.i, sh.segment(s0, t0 + in, s1, t1));
    UT_CHECK(only(k, ValidateCode::PcurveVertex));
    restore(cs, ps);

    /* SurfaceLink: the neighbouring face's cross pcurve at this station (link 2) */
    const auto ws = k.edge_verts(sh.across[1][2]);
    double a0, b0, a1, b1;
    sh.param(ws[0], a0, b0);
    sh.param(ws[1], a1, b1);
    pc.set(cs.i, sh.segment(a0, b0, a1, b1));
    UT_CHECK(only(k, ValidateCode::SurfaceLink));
    restore(cs, ps);
  }
  /* SurfaceLink: the upper border's pcurve two borders off */
  const PoolRef pu = pc[cu.i];
  pc.set(cu.i, sh.rb.pcurves[0]);
  UT_CHECK(only(k, ValidateCode::SurfaceLink));
  restore(cu, pu);

  /* PcurveDeviation: the lower border's f bulged into the face between the
   * stations (exact at 56 and 112): the structural pass refuses an f that is
   * not a border, and the second-order bound finds the bulge */
  {
    curves::Fn1D bump, fb;
    util::Vector<double, 2> br;
    br.append(0);
    br.append(224);
    util::Vector<curves::Poly, 1> q;
    q.append(curves::Poly::exact({-56.0 * 112 / 4096, 168.0 / 4096, -1.0 / 4096})); /* (s - 56)(112 - s) / 4096 */
    UT_CHECK(curves::Fn1D::make(br, q, bump) == nullptr);
    UT_CHECK(curves::Fn1D::add(sh.rb.borders[1], bump, fb) == nullptr);
    pc.set(cl.i, sh.graph(fb));
    UT_CHECK(only(k, ValidateCode::PcurveDeviation));
    restore(cl, pl);

    /* the structural pass checks each of its three conditions: the carrier
     * (on both of the edge's coedges) bulged in plan with the pcurve still the
     * border; bulged with a pcurve that follows it (off the border height,
     * since z stays Z_1); bulged in height only */
    attr::PoolStore &st = k.geom;
    const auto *road = st.try_get<curves::Road3Pool>(sh.rb.road);
    auto carrier = [&](const curves::Fn1D &w, const curves::Fn1D &z) {
      const PoolRef h = curves::intern_offset(st, road->reference, w);
      curves::Alignment3 al;
      UT_CHECK(!h.none() && curves::Alignment3::make(st, h, z, al) == nullptr);
      return st.intern<curves::Alignment3Pool>(curves::pool_id(curves::alignment3_kind()), std::move(al));
    };
    auto &curve = k.edges.template get<".e.curve">();
    const PoolRef was = curve[lower.i];
    const PoolRef bulged = carrier(fb, sh.rb.heights[1]);
    curve.set(lower.i, bulged);
    UT_CHECK(only(k, ValidateCode::PcurveDeviation));
    pc.set(cl.i, sh.graph(fb));
    UT_CHECK(k.edges.template get<".e.curve">()[lower.i] == bulged);
    {
      /* only this coedge: the other face's coedge keeps border 1 */
      const ValidationReport r = validate(k, ValidateLevel::Global);
      UT_CHECK(r.count(ValidateCode::PcurveDeviation) >= 1 && r.count(ValidateCode::PcurveDeviation) == r.total);
      bool mine = false;
      for (const ValidationIssue &i : r.issues) {
        mine = mine || i.index == cl.i;
      }
      UT_CHECK(mine);
    }
    pc.set(cl.i, pl);
    curves::Fn1D zb;
    UT_CHECK(curves::Fn1D::add(sh.rb.heights[1], bump, zb) == nullptr);
    curve.set(lower.i, carrier(sh.rb.borders[1], zb));
    UT_CHECK(only(k, ValidateCode::PcurveDeviation));
    curve.set(lower.i, was);
    UT_CHECK(sh.fx.valid());
  }

  /* SurfaceOrientation: Reversed on a face whose normal is up */
  auto &flags = k.faces.template get<".f.flags">();
  const uint8_t fl = flags[f.i];
  flags.set(f.i, uint8_t(fl | face_flags::Reversed));
  UT_CHECK(only(k, ValidateCode::SurfaceOrientation));
  flags.set(f.i, fl);
  UT_CHECK(sh.fx.valid());

  /* a pcurve on a face that loses its surface */
  surf.set(f.i, PoolRef{});
  UT_CHECK(only(k, ValidateCode::PcurveKind));
  surf.set(f.i, sh.rb.road);
  UT_CHECK(sh.fx.valid());
}

/* A twisted road: straight reference, one link 4 m wide, and a height
 * difference across it that grows 1 cm per metre. Its only non-zero second
 * partial is S_st, so a straight diagonal edge over (s, t) = (0, 0)..(20, 4)
 * sags off the surface by D' ds / 4 = 5 cm at its middle while its ends lie
 * on it: the second-order bound must charge the cross term (and the Peano
 * term) to see it. The other two edges (the cross edge at s = 0 and border
 * 1) lie on the surface. */
void test_twisted_diagonal()
{
  using namespace curves;
  auto make = [](attr::PoolStore &st, RoadBuild &rb) {
    Cesaro2 c;
    UT_CHECK(Cesaro2::line(0, 0, 0, 32, c) == nullptr);
    const PoolRef ref = st.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(c));
    util::Vector<Fn1D, 2> b, z;
    b.append(Fn1D::constant(0, 0, 32));
    b.append(Fn1D::constant(4, 0, 32));
    z.append(Fn1D::constant(10, 0, 32));
    z.append(Fn1D::linear(10, 10 + 0.32, 0, 32));
    Road3 d;
    UT_CHECK(Road3::make(st, ref, b, z, d) == nullptr);
    rb.road = st.intern<Road3Pool>(pool_id(road3_kind()), std::move(d));
    rb.borders.clear();
    rb.heights.clear();
    for (int j = 0; j < 2; j++) {
      rb.borders.append(b[j]);
      rb.heights.append(z[j]);
    }
    rb.carriers.clear();
    rb.pcurves.clear();
    for (int j = 0; j < 2; j++) {
      const PoolRef h = intern_offset(st, ref, b[j]);
      Alignment3 al;
      UT_CHECK(Alignment3::make(st, h, z[j], al) == nullptr);
      rb.carriers.append(st.intern<Alignment3Pool>(pool_id(alignment3_kind()), std::move(al)));
      Graph2 g;
      UT_CHECK(Graph2::make(b[j], g) == nullptr);
      rb.pcurves.append(st.intern<Graph2Pool>(pool_id(graph2_kind()), std::move(g)));
    }
  };
  Fixture<double> fx;
  RoadBuild rb;
  /* (s, t) of A, B, C: counter-clockwise from above */
  const double P[3][2] = {{0, 0}, {20, 4}, {0, 4}};
  Builder<double> b;
  {
    attr::PoolStore scratch;
    register_pools(scratch);
    make(scratch, rb);
    const auto ev = surface(scratch, rb.road);
    uint32_t key[3];
    for (int i = 0; i < 3; i++) {
      Vec<3> p;
      ev->eval(P[i][0], P[i][1], 0, &p);
      key[i] = b.vertex(ident::derive_generator(ident::PersistentId{}, "tv", i), geom::Point<double>(p[0], p[1], p[2]));
    }
    b.face(ident::derive_generator(ident::PersistentId{}, "tf"), {{key[0], key[1], key[2]}});
  }
  BuildReport br;
  RegionReport rr;
  UT_CHECK(b.build(fx.k, br) && build_regions(fx.k, rr));
  make(fx.k.geom, rb);
  VertRef v[3];
  fx.k.verts.for_each_live([&](uint32_t x) {
    for (int i = 0; i < 3; i++) {
      if (fx.k.verts.template get<".id">()[x] == ident::derive_generator(ident::PersistentId{}, "tv", i)) {
        v[i] = VertRef(x);
      }
    }
  });
  FaceRef f;
  fx.k.faces.for_each_live([&](uint32_t x) { f = FaceRef(x); });
  Editor<double> ed(fx.k, &fx.j);
  const EdgeRef ab = ed.find_edge(v[0], v[1]), bc = ed.find_edge(v[1], v[2]), ca = ed.find_edge(v[2], v[0]);
  const bool fwd = fx.k.edge_verts(bc)[0] == v[2];
  EXPECT_EDIT(ed, ed.set_edge_curve(bc, rb.carriers[1], fwd ? 0 : 20, fwd ? 20 : 0), EditCode::Ok);
  auto &pc = fx.k.coedges.template get<".c.pcurve">();
  fx.k.faces.template get<".f.surface">().set(f.i, rb.road);
  auto seg = [&](EdgeRef e) {
    const auto vs = fx.k.edge_verts(e);
    double q[4];
    for (int end = 0; end < 2; end++) {
      for (int i = 0; i < 3; i++) {
        if (vs[end] == v[i]) {
          q[2 * end] = P[i][0];
          q[2 * end + 1] = P[i][1];
        }
      }
    }
    Segment2 sg;
    UT_CHECK(Segment2::make(q[0], q[1], q[2], q[3], sg) == nullptr);
    return fx.k.geom.intern<Segment2Pool>(pool_id(segment2_kind()), std::move(sg));
  };
  CoedgeRef cab;
  fx.k.for_each_radial(ab, [&](CoedgeRef c) {
    pc.set(c.i, seg(ab));
    cab = c;
  });
  fx.k.for_each_radial(ca, [&](CoedgeRef c) { pc.set(c.i, seg(ca)); });
  fx.k.for_each_radial(bc, [&](CoedgeRef c) { pc.set(c.i, rb.pcurves[1]); });
  const ValidationReport r = validate(fx.k, ValidateLevel::Global);
  UT_CHECK(r.total == 1 && r.issues.size() == 1);
  if (r.issues.size() == 1) {
    UT_CHECK(r.issues[0].code == ValidateCode::PcurveDeviation && r.issues[0].index == cab.i);
  }
  if (!(r.total == 1)) {
    r.print(stderr);
  }
}

/* the filtered pass: an edge's faces are checked through its edge */
void test_filter()
{
  Sheet sh;
  UT_CHECK(build_sheet(sh));
  Complex<double> &k = sh.k();
  const FaceRef f = sh.f[2][0];
  const CoedgeRef c = sh.coedge(sh.across[2][0], f);
  auto &pc = k.coedges.template get<".c.pcurve">();
  const PoolRef was = pc[c.i];
  pc.set(c.i, PoolRef{});
  ValidateFilter flt;
  flt.edges.append(sh.across[2][0].i);
  ValidationReport r;
  validate_surfaces(k, r, &flt);
  UT_CHECK(r.count(ValidateCode::PcurveKind) == 1);
  ValidateFilter other;
  other.faces.append(sh.f[0][2].i);
  ValidationReport r2;
  validate_surfaces(k, r2, &other);
  UT_CHECK(r2.ok());
  pc.set(c.i, was);
}

} // namespace

int main()
{
  test_codes();
  test_twisted_diagonal();
  test_filter();
  return ut_test_end();
}
