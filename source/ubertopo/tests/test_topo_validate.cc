/* topo/validate: combinatorial layer (plan P2 W8, P2a part).
 *
 * Valid builder outputs give empty reports; one hand corruption per code is
 * detected. Regions are assigned by hand from geometry (outward-facing box
 * fronts face the outside) so invariant W also checks that the exact radial
 * order and the wedge rule agree on real models. */

#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

#include "gen/buildings.h"

#include "litestl/util/alloc.h"
#include "support/topo_fixtures.h"
#include "ut_test.h"

#include <functional>

using namespace ubertopo;
using namespace ubertopo::topo;
using test::I3;
using test::pid;
using test::pt;
using VC = ValidateCode;

/* One shell per region; region_of(face, side) -> region index; region 0 is
 * the unbounded one. */
template <geom::CoordType C>
static void assign_regions(Complex<C> &k, uint32_t nregions, const std::function<uint32_t(uint32_t, uint32_t)> &region_of)
{
  k.shells.alloc_n(nregions);
  k.regions.alloc_n(nregions);
  for (uint32_t r = 0; r < nregions; r++) {
    k.regions.template get<".id">().set(r, r == 0 ? ident::unbounded_region_id() : pid(9000 + r));
  }
  std::vector<uint32_t> first(nregions, NONE);
  auto &sn = k.faces.sides().template get<".fs.shell_next">();
  for (uint32_t f = k.faces.capacity(); f-- > 0;) {
    for (uint32_t s = 2; s-- > 0;) {
      const uint32_t r = region_of(f, s), fs = 2 * f + s;
      auto reg = k.faces.template get<".f.region">()[f];
      auto sh = k.faces.template get<".f.shell">()[f];
      reg[s] = r;
      sh[s] = r;
      k.faces.template get<".f.region">().set(f, reg);
      k.faces.template get<".f.shell">().set(f, sh);
      sn.set(fs, first[r]);
      first[r] = fs;
    }
  }
  for (uint32_t r = 0; r < nregions; r++) {
    k.shells.template get<".s.region">().set(r, r);
    k.shells.template get<".s.first">().set(r, first[r]);
    k.regions.template get<".r.shell">().set(r, r);
  }
  k.regions.template get<".r.flags">().set(0, region_flags::Unbounded);
  k.regions_dirty = false;
}

/* Region of a face side of a set of axis-aligned boxes: probe a point just
 * off the face on that side and look it up. */
static uint32_t box_region(const std::vector<std::pair<I3, I3>> &boxes, const double c[3], const double n[3], uint32_t side)
{
  const double sgn = side == FRONT ? 1.0 : -1.0;
  double p[3];
  for (int i = 0; i < 3; i++) {
    p[i] = c[i] + sgn * 0.01 * n[i];
  }
  for (size_t b = 0; b < boxes.size(); b++) {
    bool in = true;
    for (int i = 0; i < 3; i++) {
      in = in && p[i] > double(boxes[b].first[i]) && p[i] < double(boxes[b].second[i]);
    }
    if (in) {
      return uint32_t(b + 1);
    }
  }
  return 0;
}

template <geom::CoordType C>
static void assign_box_regions(Complex<C> &k, const std::vector<std::pair<I3, I3>> &boxes)
{
  assign_regions<C>(k, uint32_t(boxes.size() + 1), [&](uint32_t f, uint32_t side) {
    /* face centroid and (unnormalised) Newell normal, in grid units */
    double c[3] = {0, 0, 0}, n[3] = {0, 0, 0};
    uint32_t m = 0;
    std::vector<litestl::math::double3> pts;
    k.for_each_loop(FaceRef(f), [&](LoopRef l) {
      if (l != k.first_loop(FaceRef(f))) {
        return;
      }
      k.for_each_coedge(l, [&](CoedgeRef e) { pts.push_back(geom::to_double3<C>(k.pos(k.start_vert(e)))); });
    });
    for (size_t i = 0; i < pts.size(); i++) {
      const auto &a = pts[i], &b = pts[(i + 1) % pts.size()];
      for (int j = 0; j < 3; j++) {
        c[j] += a[j];
      }
      n[0] += (a[1] - b[1]) * (a[2] + b[2]);
      n[1] += (a[2] - b[2]) * (a[0] + b[0]);
      n[2] += (a[0] - b[0]) * (a[1] + b[1]);
      m++;
    }
    const double scale = std::is_same_v<C, geom::Lattice> ? 1.0 : 256.0; /* back to grid units */
    double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (int j = 0; j < 3; j++) {
      c[j] = c[j] / m * scale;
      n[j] /= len;
    }
    return box_region(boxes, c, n, side);
  });
}

template <geom::CoordType C> static Complex<C> build_boxes(const std::vector<std::pair<I3, I3>> &boxes)
{
  test::GridBuilder<C> g;
  for (auto &b : boxes) {
    g.box(b.first, b.second);
  }
  Complex<C> k;
  BuildReport rep;
  UT_CHECK(g.b.build(k, rep) && rep.ok());
  return k;
}

template <geom::CoordType C> static void expect_clean(const Complex<C> &k, const char *what)
{
  const ValidationReport rep = validate(k);
  if (!rep.ok()) {
    fprintf(stderr, "%s: ", what);
    rep.print(stderr);
  }
  UT_CHECK(rep.ok());
}

template <geom::CoordType C> static void test_valid_models()
{
  const std::vector<std::pair<I3, I3>> one = {{{0, 0, 0}, {10, 10, 10}}};
  const std::vector<std::pair<I3, I3>> two = {{{0, 0, 0}, {10, 10, 10}}, {{10, 0, 0}, {20, 10, 10}}};
  const std::vector<std::pair<I3, I3>> four = {{{0, 0, 0}, {10, 10, 10}},
                                               {{10, 0, 0}, {20, 10, 10}},
                                               {{0, 10, 0}, {10, 20, 10}},
                                               {{10, 10, 0}, {20, 20, 10}}};
  for (const auto *set : {&one, &two, &four}) {
    Complex<C> k = build_boxes<C>(*set);
    expect_clean(k, "boxes (no regions)");
    assign_box_regions<C>(k, *set);
    expect_clean(k, "boxes with regions"); /* invariant W from exact radial order */
  }
  /* fence, wire and a sensor included in the fence face */
  test::GridBuilder<C> g;
  g.face({{{0, 0, 0}, {10, 0, 0}, {10, 0, 10}, {0, 0, 10}}});
  const uint32_t a = g.b.vertex(pid(100), pt<C>({50, 50, 0})), b = g.b.vertex(pid(101), pt<C>({50, 50, 30}));
  g.b.wire(pid(200), a, b);
  g.b.vertex(pid(102), pt<C>({5, 0, 5}), ContainerHint::in_face(0));
  Complex<C> k;
  BuildReport rep;
  UT_CHECK(g.b.build(k, rep));
  expect_clean(k, "fence");
  /* a sheet: both sides of the fence face the outside */
  assign_regions<C>(k, 1, [](uint32_t, uint32_t) { return 0u; });
  expect_clean(k, "fence with regions");
}

template <geom::CoordType C> using Corrupt = std::function<void(Complex<C> &)>;

template <geom::CoordType C> static void expect_code(const Complex<C> &base, VC code, const Corrupt<C> &corrupt)
{
  Complex<C> k = base; /* a snapshot: base stays valid */
  corrupt(k);
  const ValidationReport rep = validate(k);
  if (rep.count(code) == 0) {
    fprintf(stderr, "corruption for %s not detected; got: ", validate_code_name(code));
    rep.print(stderr);
  }
  UT_CHECK(rep.count(code) > 0);
}

template <geom::CoordType C> static void test_corruptions()
{
  const std::vector<std::pair<I3, I3>> boxes = {{{0, 0, 0}, {10, 10, 10}}, {{10, 0, 0}, {20, 10, 10}}};
  Complex<C> base = build_boxes<C>(boxes);
  assign_box_regions<C>(base, boxes);
  expect_clean(base, "corruption base");
  ident::ContainerRef cr;

  expect_code<C>(base, VC::DeadReference, [](Complex<C> &k) { k.coedges.template get<".c.next">().set(0, 9999u); });
  expect_code<C>(base, VC::EdgeEndpoints, [](Complex<C> &k) {
    auto vs = k.edges.template get<".e.vs">()[3];
    vs[0] = vs[1];
    k.edges.template get<".e.vs">().set(3, vs);
  });
  expect_code<C>(base, VC::CoedgeNextPrev, [](Complex<C> &k) { k.coedges.template get<".c.prev">().set(0, 2u); });
  expect_code<C>(base, VC::CoedgeStartVertex, [](Complex<C> &k) {
    k.coedges.template get<".c.flags">().set(1, uint8_t(k.coedges.template get<".c.flags">()[1] ^ coedge_flags::Sense));
  });
  expect_code<C>(base, VC::CoedgeChain, [](Complex<C> &k) {
    /* c0 -> c2 -> c1 -> c3 -> c0 in a 4-loop: links stay inverse, the chain breaks */
    auto &n = k.coedges.template get<".c.next">();
    auto &p = k.coedges.template get<".c.prev">();
    n.set(0, 2u), n.set(2, 1u), n.set(1, 3u), n.set(3, 0u);
    p.set(2, 0u), p.set(1, 2u), p.set(3, 1u), p.set(0, 3u);
  });
  expect_code<C>(base, VC::LoopNotClosed, [](Complex<C> &k) { k.coedges.template get<".c.l">().set(2, 1u); });
  expect_code<C>(base, VC::LoopSize, [](Complex<C> &k) { k.loops.template get<".l.size">().set(0, 5u); });
  expect_code<C>(base, VC::LoopFace, [](Complex<C> &k) { k.loops.template get<".l.f">().set(0, 1u); });
  expect_code<C>(base, VC::LooplessFace, [](Complex<C> &k) { k.faces.template get<".f.l">().set(0, NONE); });
  expect_code<C>(base, VC::FaceEdgeTwice, [](Complex<C> &k) {
    k.coedges.template get<".c.e">().set(1, k.coedges.template get<".c.e">()[0]);
  });
  expect_code<C>(base, VC::RadialNotClosed, [](Complex<C> &k) {
    k.coedges.template get<".c.radial_next">().set(0, 5u);
  });
  expect_code<C>(base, VC::RadialMembership, [](Complex<C> &k) {
    /* a coedge dropped from its edge's cycle (self-loop), still using the edge */
    const CoedgeRef c(0);
    const CoedgeRef pv = k.radial_prev(c), nx = k.radial_next(c);
    k.coedges.template get<".c.radial_next">().set(pv.i, nx.i);
    k.coedges.template get<".c.radial_prev">().set(nx.i, pv.i);
    k.coedges.template get<".c.radial_next">().set(0, 0u);
    k.coedges.template get<".c.radial_prev">().set(0, 0u);
    k.edges.template get<".e.c">().set(k.edge_of(c).i, nx.i);
  });
  expect_code<C>(base, VC::EdgeCoedge, [](Complex<C> &k) { k.edges.template get<".e.c">().set(0, NONE); });
  expect_code<C>(base, VC::DiskNotClosed, [](Complex<C> &k) {
    /* .v.e of vertex 0 -> an edge not at vertex 0 */
    for (uint32_t e = 0; e < k.edges.capacity(); e++) {
      const auto vs = k.edge_verts(EdgeRef(e));
      if (vs[0] != VertRef(0) && vs[1] != VertRef(0)) {
        k.verts.template get<".v.e">().set(0, e);
        return;
      }
    }
  });
  expect_code<C>(base, VC::DiskInverse, [](Complex<C> &k) {
    auto d = k.edges.template get<".e.disk">()[0];
    d[1] = d[0] == d[1] ? disk_pack(5, 1) : d[0];
    k.edges.template get<".e.disk">().set(0, d);
  });
  expect_code<C>(base, VC::DiskMembership, [](Complex<C> &k) {
    /* skip one edge of vertex 0's disk cycle (degree 3): a -> c directly */
    std::vector<uint32_t> es;
    k.for_each_edge_at(VertRef(0), [&](EdgeRef e) { es.push_back(e.i); });
    auto slot = [&](uint32_t e) { return k.edge_verts(EdgeRef(e))[0] == VertRef(0) ? 0u : 1u; };
    auto da = k.edges.template get<".e.disk">()[es[0]];
    da[2 * slot(es[0]) + 1] = disk_pack(es[2], slot(es[2]));
    k.edges.template get<".e.disk">().set(es[0], da);
    auto dc = k.edges.template get<".e.disk">()[es[2]];
    dc[2 * slot(es[2])] = disk_pack(es[0], slot(es[0]));
    k.edges.template get<".e.disk">().set(es[2], dc);
  });
  expect_code<C>(base, VC::VertexEdge, [](Complex<C> &k) { k.verts.template get<".v.e">().set(0, NONE); });
  ident::ContainerRef::face(FaceRef(999), cr);
  expect_code<C>(base, VC::ContainerInvalid, [cr](Complex<C> &k) {
    const uint32_t v = k.verts.alloc();
    k.verts.template get<".v.container">().set(v, cr);
    k.verts.template get<".id">().set(v, pid(77777));
  });
  ident::ContainerRef cr0;
  ident::ContainerRef::face(FaceRef(0), cr0);
  expect_code<C>(base, VC::ContainerOnFaceVertex, [cr0](Complex<C> &k) {
    k.verts.template get<".v.container">().set(0, cr0);
  });
  expect_code<C>(base, VC::DuplicateId, [](Complex<C> &k) {
    k.faces.template get<".id">().set(1, k.faces.template get<".id">()[0]);
  });
  expect_code<C>(base, VC::WedgeInvariant, [](Complex<C> &k) {
    /* move one face side to the other box's region (and shell bookkeeping
     * stays consistent, so only W can catch it) */
    auto reg = k.faces.template get<".f.region">()[0];
    std::swap(reg[0], reg[1]);
    k.faces.template get<".f.region">().set(0, reg);
  });
  expect_code<C>(base, VC::ShellPartition, [](Complex<C> &k) {
    auto sh = k.faces.template get<".f.shell">()[0];
    sh[0] = sh[0] == 0 ? 1 : 0;
    k.faces.template get<".f.shell">().set(0, sh);
  });
  expect_code<C>(base, VC::ShellRegion, [](Complex<C> &k) { k.shells.template get<".s.region">().set(1, 2u); });
  expect_code<C>(base, VC::UnboundedRegion, [](Complex<C> &k) { k.regions.template get<".r.flags">().set(0, uint8_t(0)); });
}

template <geom::CoordType C>
static void expect_level(const Complex<C> &base, ValidateLevel lvl, VC code, const Corrupt<C> &corrupt, const char *what)
{
  Complex<C> k = base;
  corrupt(k);
  const ValidationReport rep = validate(k, lvl);
  if (rep.count(code) == 0) {
    fprintf(stderr, "%s: %s not detected; got: ", what, validate_code_name(code));
    rep.print(stderr);
  }
  UT_CHECK(rep.count(code) > 0);
}

template <geom::CoordType C> static void move(Complex<C> &k, uint32_t v, const I3 &p)
{
  k.verts.template get<"pos">().set(v, pt<C>(p));
}

template <geom::CoordType C> static uint32_t vertex_at(const Complex<C> &k, const I3 &p)
{
  for (uint32_t v = 0; v < k.verts.capacity(); v++) {
    if (geom::lex_compare<C>(k.pos(VertRef(v)), pt<C>(p)) == 0) {
      return v;
    }
  }
  return NONE;
}

template <geom::CoordType C> static void test_geometric_layers()
{
  const auto L = ValidateLevel::Embedding, G = ValidateLevel::Global;
  /* clean models pass every layer */
  {
    test::gen::Model<C> m(ident::derive(ident::tag::TestOnly, std::string_view("v")));
    test::gen::GridOpts g;
    g.nx = 2;
    g.windows = 1;
    test::gen::room_grid(m, {0, 0, 0}, g);
    test::gen::sensor_and_wire(m, {2000, 0, 0});
    test::gen::sloped_roof(m, {0, 2000, 0}, 3);
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(m.b.build(k, br, m.policy) && build_regions(k, rr));
    expect_clean(k, "generated (combinatorial)");
    const ValidationReport rep = validate(k, G);
    if (!rep.ok()) {
      rep.print(stderr);
    }
    UT_CHECK(rep.ok());

    expect_level<C>(k, L, VC::ContainerGeometry, [](Complex<C> &x) {
      for (uint32_t v = 0; v < x.verts.capacity(); v++) {
        if (x.vert_container(VertRef(v)).kind() == ident::ContainerKind::Region && x.vert_container(VertRef(v)).index() != 0) {
          move(x, v, {5000, 5000, 5000}); /* the sensor leaves its room */
          return;
        }
      }
    }, "sensor outside its room");
    expect_level<C>(k, L, VC::ProjectionNotAllowed, [](Complex<C> &x) { x.set_allows_projectable(false); },
                    "projected face without the opt-in");
    expect_level<C>(k, L, VC::NotProjectable, [](Complex<C> &x) {
      for (uint32_t f = 0; f < x.faces.capacity(); f++) {
        if (x.face_proj_axis(FaceRef(f)) == 2) {
          x.faces.template get<".f.flags">().set(f, with_proj_axis(x.face_flags(FaceRef(f)), 0));
          return;
        }
      }
    }, "projected face along a wrong axis");
    expect_level<C>(k, L, VC::SpuriousProjection, [](Complex<C> &x) {
      x.faces.template get<".f.flags">().set(0, with_proj_axis(x.face_flags(FaceRef(0)), 2));
    }, "projection flag on a planar face");
    expect_level<C>(k, L, VC::NonPlanarFace, [](Complex<C> &x) { move(x, vertex_at(x, {0, 0, 0}), {0, 0, -7}); },
                    "a vertex moved off its faces' planes");
    expect_level<C>(k, G, VC::RegionsStale, [](Complex<C> &x) {
      x.regions.template get<".id">().set(1, test::pid(31337));
    }, "stale region IDs");
  }
  /* radial order out of step with geometry: two boxes sharing a wall */
  {
    test::GridBuilder<C> g;
    g.box({0, 0, 0}, {10, 10, 10});
    g.box({10, 0, 0}, {20, 10, 10});
    Complex<C> k;
    BuildReport br;
    UT_CHECK(g.b.build(k, br));
    UT_CHECK(validate(k, G).ok());
    expect_level<C>(k, ValidateLevel::Combinatorial, VC::VertexLink, [](Complex<C> &x) {
      for (uint32_t e = 0; e < x.edges.capacity(); e++) {
        std::vector<uint32_t> cyc;
        x.for_each_radial(EdgeRef(e), [&](CoedgeRef c) { cyc.push_back(c.i); });
        if (cyc.size() == 3) { /* reversed: a consistent cycle, but the links lose planarity */
          for (size_t i = 0; i < 3; i++) {
            x.coedges.template get<".c.radial_next">().set(cyc[i], cyc[(i + 2) % 3]);
            x.coedges.template get<".c.radial_prev">().set(cyc[i], cyc[(i + 1) % 3]);
          }
          return;
        }
      }
    }, "reversed radial cycle");
    expect_level<C>(k, L, VC::RadialOrder, [](Complex<C> &x) {
      for (uint32_t e = 0; e < x.edges.capacity(); e++) {
        std::vector<uint32_t> cyc;
        x.for_each_radial(EdgeRef(e), [&](CoedgeRef c) { cyc.push_back(c.i); });
        if (cyc.size() == 3) { /* the cycle no longer starts at the smallest face ID */
          x.edges.template get<".e.c">().set(e, cyc[1]);
          return;
        }
      }
    }, "radial cycle start");
  }
  /* a lone quad: bow tie (in plane), and a second quad crossing it */
  {
    test::GridBuilder<C> g;
    g.face({{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}});
    g.face({{{5, 5, -5}, {5, 5, 5}, {15, 5, 5}, {15, 5, -5}}}); /* pierces the first */
    Complex<C> k;
    BuildReport br;
    UT_CHECK(g.b.build(k, br));
    expect_level<C>(k, G, VC::FaceIntersection, [](Complex<C> &) {}, "piercing faces");
  }
  {
    /* touching, not piercing: a vertical triangle's corner rests on the
     * square's interior. The boxes only meet in the plane z = 0, so the pair
     * test's box filter must use closed boxes */
    test::GridBuilder<C> g;
    g.face({{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}});
    g.face({{{5, 5, 0}, {8, 5, 4}, {2, 5, 4}}});
    Complex<C> k;
    BuildReport br;
    UT_CHECK(g.b.build(k, br));
    expect_level<C>(k, G, VC::FaceIntersection, [](Complex<C> &) {}, "a corner resting on a face");
  }
  {
    test::GridBuilder<C> g;
    g.face({{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}});
    Complex<C> k;
    BuildReport br;
    UT_CHECK(g.b.build(k, br) && validate(k, G).ok());
    expect_level<C>(k, G, VC::LoopIntersection, [](Complex<C> &x) { move(x, vertex_at(x, {10, 10, 0}), {-5, 3, 0}); },
                    "self-intersecting loop");
  }
  /* holes: winding and containment */
  {
    test::GridBuilder<C> g;
    g.face({{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}, {{2, 2, 0}, {2, 4, 0}, {4, 4, 0}}});
    Complex<C> k;
    BuildReport br;
    UT_CHECK(g.b.build(k, br) && validate(k, G).ok());
    expect_level<C>(k, L, VC::HoleWinding, [](Complex<C> &x) {
      const uint32_t a = vertex_at(x, {2, 4, 0}), b = vertex_at(x, {4, 4, 0});
      move(x, a, {4, 4, 0});
      move(x, b, {2, 4, 0}); /* swaps two hole corners: the hole now winds the other way */
    }, "hole wound like the outer loop");
    expect_level<C>(k, G, VC::HoleOutside, [](Complex<C> &x) {
      move(x, vertex_at(x, {2, 2, 0}), {22, 2, 0});
      move(x, vertex_at(x, {2, 4, 0}), {22, 4, 0});
      move(x, vertex_at(x, {4, 4, 0}), {24, 4, 0});
    }, "hole outside its face");
  }
  /* Euler-Poincare at vertices: a rotation inconsistent with any planar link */
  {
    test::GridBuilder<C> g;
    g.box({0, 0, 0}, {10, 10, 10});
    g.box({10, 0, 0}, {20, 10, 10});
    g.box({0, 10, 0}, {10, 20, 10});
    g.box({10, 10, 0}, {20, 20, 10});
    Complex<C> k;
    BuildReport br;
    UT_CHECK(g.b.build(k, br) && validate(k).ok());
    /* at an edge with 4 faces, swap two non-adjacent coedges' places */
    Complex<C> x = k;
    bool done = false;
    for (uint32_t e = 0; e < x.edges.capacity() && !done; e++) {
      std::vector<uint32_t> cyc;
      x.for_each_radial(EdgeRef(e), [&](CoedgeRef c) { cyc.push_back(c.i); });
      if (cyc.size() == 4) {
        const uint32_t order[4] = {cyc[0], cyc[2], cyc[1], cyc[3]};
        for (int i = 0; i < 4; i++) {
          x.coedges.template get<".c.radial_next">().set(order[i], order[(i + 1) % 4]);
          x.coedges.template get<".c.radial_prev">().set(order[i], order[(i + 3) % 4]);
        }
        done = true;
      }
    }
    UT_CHECK(done);
    const ValidationReport rep = validate(x);
    if (rep.count(VC::VertexLink) == 0) {
      rep.print(stderr);
    }
    UT_CHECK(rep.count(VC::VertexLink) > 0);
  }
}

template <geom::CoordType C> static void run_all()
{
  test_valid_models<C>();
  test_corruptions<C>();
  test_geometric_layers<C>();
}

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  run_all<geom::Lattice>();
  run_all<double>();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
