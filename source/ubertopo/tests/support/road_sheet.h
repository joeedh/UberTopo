#pragma once

/* The road sheet of the surface tests (test_validate_surfaces,
 * test_edit_surfaces; plan P4b §7).
 *
 * The superelevation fixture's reference (clothoid, arc, clothoid; 224 m),
 * three links (two right, one left, the left one widening), cut at five
 * stations into 4 x 3 projected lane faces. Border edges carry the builder's
 * alignment3 carriers and graph2 pcurves; cross edges are straight with
 * segment2 pcurves. build_sheet writes surfaces and pcurves straight into the
 * columns (or leaves them for the editor). */

#include <cstdio>
#include <vector>

#include "edit_fixture.h"
#include "road_fixtures.h"
#include "ubertopo/curves/alignment.h"
#include "ubertopo/curves/offset.h"
#include "ubertopo/curves/pcurve.h"
#include "ubertopo/curves/road.h"
#include "ubertopo/curves/road_build.h"

namespace ubertopo::test {

using litestl::attr::PoolRef;
namespace sheet_detail {
namespace util = litestl::util;
namespace attr = litestl::attr;
} // namespace sheet_detail
using namespace sheet_detail;

inline constexpr int kStations = 5;
inline constexpr double kStation[kStations] = {0, 56, 112, 168, 224};

struct Sheet {
  test::Fixture<double> fx;
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
inline bool make_road(attr::PoolStore &st, curves::RoadBuild &rb)
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

/* @p surfaces false leaves the surface and pcurve columns NONE */
inline bool build_sheet(Sheet &sh, bool surfaces = true)
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
  if (!surfaces) {
    return true;
  }
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

} // namespace ubertopo::test
