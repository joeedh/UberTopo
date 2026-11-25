/* build_regions() against generator ground truth (plan P2 W7, W9), and its
 * ordering, tie and carry-over rules (plan 2026-11-23 Stage A). */

#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

#include "gen/buildings.h"
#include "litestl/util/alloc.h"
#include "support/topo_fixtures.h"
#include "ut_test.h"

#include <array>
#include <map>
#include <vector>

using namespace ubertopo;
using namespace ubertopo::topo;
using test::gen::GridOpts;
using test::gen::I3;
using test::gen::Model;
using ident::PersistentId;

struct Outcome {
  bool built = false, regions = false, partition = false, validated = false;
};

/* Builds, computes regions, and checks the face-side partition against the
 * generator's labels. */
template <geom::CoordType C> static Outcome check_model(Model<C> &m, Complex<C> &k, RegionReport &rr, const char *what)
{
  Outcome o;
  BuildReport br;
  o.built = m.b.build(k, br, m.policy);
  if (!o.built) {
    fprintf(stderr, "%s: build failed:", what);
    for (auto &i : br.issues) {
      fprintf(stderr, " %s", build_code_name(i.code));
    }
    fprintf(stderr, "\n");
    return o;
  }
  o.regions = build_regions(k, rr);
  if (!o.regions || rr.warnings) {
    fprintf(stderr, "%s: regions ok=%d warnings=%u:", what, int(o.regions), rr.warnings);
    for (auto &i : rr.issues) {
      fprintf(stderr, " %s(%u)", region_code_name(i.code), i.index);
    }
    fprintf(stderr, "\n");
    o.regions = o.regions && rr.warnings == 0;
    if (!o.regions) {
      return o;
    }
  }
  /* partition: region <-> label, both ways */
  std::map<uint32_t, PersistentId> r2l;
  std::map<PersistentId, uint32_t> l2r;
  bool ok = true;
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    for (uint32_t s = 0; s < 2; s++) {
      const uint32_t r = k.region(FaceRef(f), s).i;
      const PersistentId l = k.side_hint(FaceRef(f), s);
      auto a = r2l.find(r);
      if (a != r2l.end() && !(a->second == l)) {
        ok = false;
      }
      r2l[r] = l;
      auto b = l2r.find(l);
      if (b != l2r.end() && b->second != r) {
        ok = false;
      }
      l2r[l] = r;
    }
  }
  /* region 0 is the outside; bounded regions match the label set */
  ok = ok && r2l.count(0) && r2l[0] == ident::unbounded_region_id();
  uint32_t bounded = 0;
  for (auto &[l, r] : l2r) {
    bounded += !(l == ident::unbounded_region_id());
    ok = ok && (r == 0) == (l == ident::unbounded_region_id()) && (r == 0 || m.truth.labels.count(l));
  }
  ok = ok && bounded == m.truth.labels.size() && k.regions.live_count() == bounded + 1;
  /* contained vertices land in the labelled region */
  for (size_t i = 0; i < m.truth.contained_vertex_ids.size(); i++) {
    for (uint32_t v = 0; v < k.verts.capacity(); v++) {
      if (k.vert_id(VertRef(v)) == m.truth.contained_vertex_ids[i]) {
        const RegionRef r = k.vert_container(VertRef(v)).as_region();
        ok = ok && r && l2r.count(m.truth.vertex_containers[i]) && l2r[m.truth.vertex_containers[i]] == r.i;
      }
    }
  }
  o.partition = ok;
  if (!ok) {
    fprintf(stderr, "%s: region partition differs from ground truth (regions %u, labels %zu)\n", what,
            k.regions.live_count(), m.truth.labels.size());
  }
  const ValidationReport vr = validate(k, ValidateLevel::Global);
  o.validated = vr.ok();
  if (!vr.ok()) {
    fprintf(stderr, "%s: ", what);
    vr.print(stderr);
  }
  return o;
}

template <geom::CoordType C> static void expect_truth(Model<C> &m, const char *what, RegionReport *out = nullptr)
{
  Complex<C> k;
  RegionReport rr;
  const Outcome o = check_model(m, k, rr, what);
  UT_CHECK(o.built && o.regions && o.partition && o.validated);
  UT_CHECK(!k.regions_dirty);
  if (out) {
    *out = rr;
  }
}

static PersistentId root(const char *what)
{
  return ident::derive(ident::tag::TestOnly, std::string_view(what));
}

template <geom::CoordType C> static void test_families()
{
  {
    Model<C> m(root("single"));
    GridOpts g;
    g.nx = g.ny = 1;
    test::gen::room_grid(m, {0, 0, 0}, g);
    RegionReport rr;
    expect_truth(m, "single thin room (touching shells)", &rr);
    UT_CHECK(rr.outer_shells == 1 && rr.cavity_shells == 1 && rr.sheet_shells == 0);
  }
  {
    Model<C> m(root("grid"));
    GridOpts g;
    g.nx = 3;
    g.ny = 2;
    g.floors = 2;
    test::gen::room_grid(m, {0, 0, 0}, g);
    expect_truth(m, "thin room grid");
  }
  {
    Model<C> m(root("thick"));
    GridOpts g;
    g.nx = 2;
    g.ny = 2;
    g.floors = 2;
    g.thick = true;
    test::gen::room_grid(m, {0, 0, 0}, g);
    RegionReport rr;
    expect_truth(m, "thick walls", &rr);
    UT_CHECK(rr.outer_shells == 1 + 8 && rr.cavity_shells == 1 + 8); /* wall + rooms; outside + room cavities */
  }
  {
    Model<C> m(root("glazed"));
    GridOpts g;
    g.nx = 3;
    g.ny = 1;
    g.windows = 2;
    g.glazing = true;
    test::gen::room_grid(m, {0, 0, 0}, g);
    expect_truth(m, "glazed windows");
  }
  {
    Model<C> m(root("open"));
    GridOpts g;
    g.nx = 3;
    g.ny = 1;
    g.windows = 1;
    g.glazing = false;
    test::gen::room_grid(m, {0, 0, 0}, g);
    expect_truth(m, "open window joins the outside");
  }
  {
    Model<C> m(root("court"));
    test::gen::courtyard(m, {0, 0, 0});
    expect_truth(m, "courtyard");
  }
  {
    Model<C> m(root("pillar"));
    test::gen::pillar_room(m, {0, 0, 0});
    expect_truth(m, "pillar in a room");
  }
  {
    Model<C> m(root("cavity"));
    test::gen::sealed_cavity(m, {0, 0, 0});
    expect_truth(m, "sealed cavity");
  }
  {
    Model<C> m(root("nested"));
    test::gen::nested_solids(m, {0, 0, 0});
    expect_truth(m, "room > pillar > void (innermost containment)");
  }
  {
    Model<C> m(root("fence"));
    test::gen::fence_in_air(m, {0, 0, 0}, 3);
    RegionReport rr;
    expect_truth(m, "fence in air", &rr);
    UT_CHECK(rr.sheet_shells == 1 && rr.outer_shells == 0);
  }
  {
    Model<C> m(root("sensor"));
    test::gen::sensor_and_wire(m, {0, 0, 0});
    expect_truth(m, "sensor and wire");
  }
  {
    Model<C> m(root("roof"));
    test::gen::sloped_roof(m, {0, 0, 0}, 1);
    expect_truth(m, "sloped (projected) roof");
  }
  {
    Model<C> m(root("city"));
    test::gen::random_city(m, 7, 25);
    expect_truth(m, "random city (25)");
  }
}

template <geom::CoordType C> static void test_hints_and_rebuild()
{
  /* two disconnected regions with the same hint: a warning, distinct IDs */
  {
    test::GridBuilder<C> g;
    const PersistentId h = test::pid(42), out = ident::unbounded_region_id();
    auto boxh = [&](const I3 &lo, const I3 &hi) {
      const int64_t x0 = lo[0], y0 = lo[1], z0 = lo[2], x1 = hi[0], y1 = hi[1], z1 = hi[2];
      g.face({{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}}, out, h);
      g.face({{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, out, h);
      g.face({{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, out, h);
      g.face({{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}}, out, h);
      g.face({{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}, out, h);
      g.face({{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}}, out, h);
    };
    boxh({0, 0, 0}, {10, 10, 10});
    boxh({20, 0, 0}, {30, 10, 10});
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(k, br) && build_regions(k, rr));
    UT_CHECK(rr.count(RegionCode::DuplicateHint) == 1 && rr.errors == 0);
    UT_CHECK(k.regions.live_count() == 3 && !(k.regions.template get<".id">()[1] == k.regions.template get<".id">()[2]));
    UT_CHECK(validate(k).ok());
  }
  /* conflicting hints on one region: a warning; majority wins */
  {
    test::GridBuilder<C> g;
    const PersistentId out = ident::unbounded_region_id();
    g.face({{{0, 0, 0}, {0, 10, 0}, {10, 10, 0}, {10, 0, 0}}}, out, test::pid(1));
    g.face({{{0, 0, 10}, {10, 0, 10}, {10, 10, 10}, {0, 10, 10}}}, out, test::pid(1));
    g.face({{{0, 0, 0}, {10, 0, 0}, {10, 0, 10}, {0, 0, 10}}}, out, test::pid(1));
    g.face({{{0, 10, 0}, {0, 10, 10}, {10, 10, 10}, {10, 10, 0}}}, out, test::pid(2));
    g.face({{{0, 0, 0}, {0, 0, 10}, {0, 10, 10}, {0, 10, 0}}}, out, test::pid(1));
    g.face({{{10, 0, 0}, {10, 10, 0}, {10, 10, 10}, {10, 0, 10}}}, out, test::pid(1));
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(k, br) && build_regions(k, rr) && rr.count(RegionCode::HintConflict) == 1);
    /* the room region took hint 1 */
    const RegionRef room = k.region(FaceRef(0), BACK);
    UT_CHECK(room.i != 0);
  }
  /* region attributes survive a rebuild, keyed by persistent ID */
  {
    Model<C> m(root("attrs"));
    GridOpts g;
    g.nx = 2;
    test::gen::room_grid(m, {0, 0, 0}, g);
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(m.b.build(k, br) && build_regions(k, rr));
    auto *temp = k.regions.group().template ensure<attr::Column<float>>("temperature", -1.0f);
    UT_CHECK(temp != nullptr);
    for (uint32_t r = 0; r < k.regions.capacity(); r++) {
      temp->set(r, float(10 + r));
    }
    std::map<PersistentId, float> before;
    for (uint32_t r = 0; r < k.regions.capacity(); r++) {
      before[k.regions.template get<".id">()[r]] = (*temp)[r];
    }
    k.regions_dirty = true;
    RegionReport rr2;
    UT_CHECK(build_regions(k, rr2));
    auto *t2 = k.regions.group().template find<attr::Column<float>>("temperature");
    UT_CHECK(t2 != nullptr);
    for (uint32_t r = 0; t2 && r < k.regions.capacity(); r++) {
      UT_CHECK((*t2)[r] == before[k.regions.template get<".id">()[r]]);
    }
  }
  /* unresolved container hint: an error, the complex is left unchanged */
  {
    Builder<C> b;
    b.vertex(test::pid(5), test::pt<C>({1, 2, 3}), ContainerHint::in_region(test::pid(999)));
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(b.build(k, br));
    UT_CHECK(!build_regions(k, rr) && rr.count(RegionCode::UnresolvedContainerHint) == 1 && k.regions_dirty);
  }
}

/* Stage A of plan 2026-11-23 (build_regions' bookkeeping): the ordering and
 * tie rules that a fresh model with no warnings never exercises. */

/* a box's six faces, front sides out (unb) and back sides in, with one inner
 * hint per face */
template <geom::CoordType C>
static void hinted_box(test::GridBuilder<C> &g, const I3 &lo, const I3 &hi, const std::array<PersistentId, 6> &in)
{
  const PersistentId out = ident::unbounded_region_id();
  const int64_t x0 = lo[0], y0 = lo[1], z0 = lo[2], x1 = hi[0], y1 = hi[1], z1 = hi[2];
  g.face({{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}}, out, in[0]);
  g.face({{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, out, in[1]);
  g.face({{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, out, in[2]);
  g.face({{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}}, out, in[3]);
  g.face({{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}, out, in[4]);
  g.face({{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}}, out, in[5]);
}

template <geom::CoordType C> static uint32_t vert_by_id(const Complex<C> &k, PersistentId id)
{
  for (uint32_t v = 0; v < k.verts.capacity(); v++) {
    if (k.verts.live(v) && k.verts.template get<".id">()[v] == id) {
      return v;
    }
  }
  return NONE;
}

template <geom::CoordType C> static uint32_t edge_by_id(const Complex<C> &k, PersistentId id)
{
  for (uint32_t e = 0; e < k.edges.capacity(); e++) {
    if (k.edges.live(e) && k.edges.template get<".id">()[e] == id) {
      return e;
    }
  }
  return NONE;
}

template <geom::CoordType C> static void test_votes_and_order()
{
  const PersistentId unb = ident::unbounded_region_id();
  /* a tie between two hints: the smaller wins, with a HintConflict */
  {
    const PersistentId a = test::pid(1), b = test::pid(2);
    const PersistentId lo = a < b ? a : b;
    test::GridBuilder<C> g;
    hinted_box<C>(g, {0, 0, 0}, {10, 10, 10}, {b, a, b, a, b, a});
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(k, br) && build_regions(k, rr));
    UT_CHECK(rr.count(RegionCode::HintConflict) == 1 && k.regions.live_count() == 2);
    UT_CHECK(k.regions.template get<".id">()[1] == ident::region_id_from_hint(lo));
  }
  /* a tie between unb and a hint: unb votes like any hint. Below unb, the
   * hint wins; above, unb wins, the room gets a side-based ID, and the hint
   * becomes a minority carrier that a container hint resolves through */
  const PersistentId below = test::pid(1001), above = test::pid(1001, ~0ull); /* IDs order hi first */
  UT_CHECK(below < unb && unb < above);
  for (const PersistentId h : {below, above}) {
    test::GridBuilder<C> g;
    hinted_box<C>(g, {0, 0, 0}, {10, 10, 10}, {h, unb, h, unb, h, unb});
    g.b.vertex(test::pid(900), test::pt<C>({5, 5, 5}), ContainerHint::in_region(h));
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(k, br) && build_regions(k, rr));
    UT_CHECK(rr.count(RegionCode::HintConflict) == 1 && k.regions.live_count() == 2);
    const PersistentId id = k.regions.template get<".id">()[1];
    UT_CHECK((h == below) == (id == ident::region_id_from_hint(h)));
    const uint32_t v = vert_by_id(k, test::pid(900));
    UT_CHECK(v != NONE && k.verts.template get<".v.container">()[v].as_region() == RegionRef(1));
  }
  /* more than 8 distinct hints in one region (the vote map's path). One
   * shell, a box whose walls are split 2x2 into 24 faces: its sides vote in
   * face order (the builder orders faces by ID), so twelve faces with hints of their own come first (the
   * switch to the map happens at the ninth), then twelve with hint W, all
   * counted through the map. W wins only if the map counts right, and W
   * isn't the smallest hint. A minority hint is carried by the room. */
  {
    const PersistentId w = test::pid(500), out = ident::unbounded_region_id();
    test::GridBuilder<C> g;
    const int64_t a = 0, b = 20;
    const std::array<std::array<I3, 4>, 6> quads = {{
        {{{a, a, a}, {a, b, a}, {b, b, a}, {b, a, a}}},
        {{{a, a, b}, {b, a, b}, {b, b, b}, {a, b, b}}},
        {{{a, a, a}, {b, a, a}, {b, a, b}, {a, a, b}}},
        {{{a, b, a}, {a, b, b}, {b, b, b}, {b, b, a}}},
        {{{a, a, a}, {a, a, b}, {a, b, b}, {a, b, a}}},
        {{{b, a, a}, {b, b, a}, {b, b, b}, {b, a, b}}},
    }};
    auto mid = [](const I3 &p, const I3 &q) { return I3{(p[0] + q[0]) / 2, (p[1] + q[1]) / 2, (p[2] + q[2]) / 2}; };
    uint32_t n = 0;
    for (const auto &q : quads) {
      const I3 m01 = mid(q[0], q[1]), m12 = mid(q[1], q[2]), m23 = mid(q[2], q[3]), m30 = mid(q[3], q[0]);
      const I3 c = mid(q[0], q[2]);
      for (const auto &sub : {std::array<I3, 4>{q[0], m01, c, m30}, std::array<I3, 4>{m01, q[1], m12, c},
                              std::array<I3, 4>{c, m12, q[2], m23}, std::array<I3, 4>{m30, c, m23, q[3]}})
      {
        /* ascending face IDs keep the faces (and so the sides) in this order */
        const PersistentId in = n < 12 ? test::pid(10 + n) : w;
        const uint32_t ks[4] = {g.v(sub[0]), g.v(sub[1]), g.v(sub[2]), g.v(sub[3])}, sz[1] = {4};
        g.b.face(test::pid(100 + n), std::span<const uint32_t>(ks, 4), std::span<const uint32_t>(sz, 1), out, in);
        n++;
      }
    }
    g.b.vertex(test::pid(905), test::pt<C>({10, 10, 10}), ContainerHint::in_region(test::pid(15)));
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(k, br) && build_regions(k, rr));
    UT_CHECK(rr.count(RegionCode::HintConflict) == 1 && k.regions.live_count() == 2);
    UT_CHECK(k.regions.template get<".id">()[1] == ident::region_id_from_hint(w));
    const uint32_t v = vert_by_id(k, test::pid(905));
    UT_CHECK(v != NONE && k.verts.template get<".v.container">()[v].as_region() == RegionRef(1));
  }
  /* a minority hint carried by two regions: a container hint naming it is
   * ambiguous */
  {
    const PersistentId m = test::pid(30);
    test::GridBuilder<C> g;
    hinted_box<C>(g, {0, 0, 0}, {10, 10, 10}, {test::pid(31), test::pid(31), test::pid(31), test::pid(31), test::pid(31), m});
    hinted_box<C>(g, {20, 0, 0}, {30, 10, 10}, {test::pid(32), test::pid(32), test::pid(32), test::pid(32), test::pid(32), m});
    g.b.vertex(test::pid(906), test::pt<C>({50, 0, 0}), ContainerHint::in_region(m));
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(k, br));
    UT_CHECK(!build_regions(k, rr) && rr.count(RegionCode::AmbiguousContainerHint) == 1);
  }
  /* three regions win one hint: DuplicateHint once, on the second in final
   * order */
  {
    const PersistentId h = test::pid(7);
    test::GridBuilder<C> g;
    for (int i = 0; i < 3; i++) {
      hinted_box<C>(g, {20 * i, 0, 0}, {20 * i + 10, 10, 10}, {h, h, h, h, h, h});
    }
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(k, br) && build_regions(k, rr));
    UT_CHECK(rr.count(RegionCode::DuplicateHint) == 1 && k.regions.live_count() == 4);
    for (const RegionIssue &i : rr.issues) {
      UT_CHECK(i.code != RegionCode::DuplicateHint || i.index == 2);
    }
  }
  /* two hints, each won twice: the two DuplicateHint issues come in final
   * region order, whatever the hints' own order (several pairs, so a
   * hint-ordered report would show) */
  for (uint32_t p = 0; p < 8; p++) {
    const PersistentId ha = test::pid(100 + 2 * p), hb = test::pid(101 + 2 * p);
    test::GridBuilder<C> g;
    int x = 0;
    for (const PersistentId h : {ha, hb, ha, hb}) {
      hinted_box<C>(g, {x, 0, 0}, {x + 10, 10, 10}, {h, h, h, h, h, h});
      x += 20;
    }
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(k, br) && build_regions(k, rr));
    std::vector<uint32_t> at;
    for (const RegionIssue &i : rr.issues) {
      if (i.code == RegionCode::DuplicateHint) {
        at.push_back(i.index);
      }
    }
    UT_CHECK(at.size() == 2 && at[0] < at[1]);
  }
  /* the order of a failed build's issues: warnings in region order, then
   * container errors for vertices, then edges. The barrier reports the
   * first error (editor_impl_face.h), here the vertex's. */
  {
    const PersistentId h = test::pid(7), p1 = test::pid(11), p2 = test::pid(12);
    test::GridBuilder<C> g;
    for (int i = 0; i < 2; i++) {
      hinted_box<C>(g, {20 * i, 0, 0}, {20 * i + 10, 10, 10}, {h, h, h, h, h, h});
    }
    hinted_box<C>(g, {40, 0, 0}, {50, 10, 10}, {p1, p1, p1, p1, p2, p2});
    g.b.vertex(test::pid(901), test::pt<C>({100, 0, 0}), ContainerHint::in_region(test::pid(999)));
    const uint32_t a = g.b.vertex(test::pid(902), test::pt<C>({100, 5, 0}));
    const uint32_t b = g.b.vertex(test::pid(903), test::pt<C>({100, 9, 0}));
    g.b.wire(test::pid(904), a, b, ContainerHint::in_region(h)); /* won twice: ambiguous */
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(k, br));
    UT_CHECK(!build_regions(k, rr));
    const uint32_t v = vert_by_id(k, test::pid(901)), e = edge_by_id(k, test::pid(904));
    UT_CHECK(rr.issues.size() == 4);
    if (rr.issues.size() == 4) {
      UT_CHECK(rr.issues[0].code == RegionCode::HintConflict && rr.issues[0].index >= 1);
      UT_CHECK(rr.issues[1].code == RegionCode::DuplicateHint && rr.issues[1].index == 2);
      UT_CHECK(rr.issues[2].code == RegionCode::UnresolvedContainerHint && rr.issues[2].index == v);
      UT_CHECK(rr.issues[3].code == RegionCode::AmbiguousContainerHint && rr.issues[3].index == e);
      const RegionIssue *first = nullptr;
      for (const RegionIssue &i : rr.issues) {
        if (!first && region_code_is_error(i.code)) {
          first = &i;
        }
      }
      UT_CHECK(first == &rr.issues[2]);
    }
  }
}

/* Region rows carried over by ID through a rebuild that renames a region
 * (its ID leaves the order and a new one enters it), and through an old
 * domain that isn't in ID order or repeats an ID. Generations follow
 * replace_generations' rule. */
template <geom::CoordType C> static void test_carry_over()
{
  Model<C> m(root("carry"));
  GridOpts go;
  go.nx = 3;
  go.ny = 2;
  test::gen::room_grid(m, {0, 0, 0}, go);
  Complex<C> k;
  BuildReport br;
  RegionReport rr;
  UT_CHECK(m.b.build(k, br) && build_regions(k, rr));
  const uint32_t R = k.regions.capacity();
  UT_CHECK(R >= 5);
  auto *temp = k.regions.group().template ensure<attr::Column<float>>("temperature", -1.0f);
  UT_CHECK(temp != nullptr);
  if (!temp || R < 5) {
    return;
  }
  for (uint32_t r = 0; r < R; r++) {
    temp->set(r, float(10 + r));
  }
  auto ids = [&]() {
    std::vector<PersistentId> v;
    for (uint32_t r = 0; r < k.regions.capacity(); r++) {
      v.push_back(k.regions.template get<".id">()[r]);
    }
    return v;
  };
  auto temps = [&]() {
    std::map<PersistentId, float> t;
    auto *c = k.regions.group().template find<attr::Column<float>>("temperature");
    for (uint32_t r = 0; c && r < k.regions.capacity(); r++) {
      t[k.regions.template get<".id">()[r]] = (*c)[r];
    }
    return t;
  };
  auto rebuild = [&]() {
    k.regions_dirty = true;
    RegionReport r2;
    return build_regions(k, r2);
  };

  /* 1. rename region 2: its sides get a new hint */
  {
    const std::vector<PersistentId> old_id = ids();
    const std::map<PersistentId, float> before = temps();
    std::vector<uint32_t> old_gen;
    for (uint32_t r = 0; r < R; r++) {
      old_gen.push_back(k.regions.gen(r));
    }
    const uint32_t old_floor = k.regions.gen_floor();
    const PersistentId nh = test::pid(5000);
    auto &fh = k.faces.sides().template get<".fs.hint">();
    for (uint32_t f = 0; f < k.faces.capacity(); f++) {
      if (!k.faces.live(f)) {
        continue;
      }
      for (uint32_t s = 0; s < 2; s++) {
        if (k.region(FaceRef(f), s) == RegionRef(2)) {
          fh.set(2 * f + s, nh);
        }
      }
    }
    UT_CHECK(rebuild());
    const std::vector<PersistentId> new_id = ids();
    const std::map<PersistentId, float> after = temps();
    UT_CHECK(new_id.size() == R);
    uint32_t fresh = 0;
    for (const PersistentId &id : new_id) {
      const auto it = before.find(id);
      if (it == before.end()) {
        fresh++;
        UT_CHECK(after.at(id) == -1.0f);
      }
      else {
        UT_CHECK(after.at(id) == it->second); /* region 0 included */
      }
    }
    UT_CHECK(fresh == 1);
    bool moved = false;
    for (uint32_t r = 0; r < R && r < new_id.size(); r++) {
      const bool same = old_id[r] == new_id[r];
      moved = moved || !same;
      const uint32_t want = same ? old_gen[r] : std::max(old_gen[r] + 1, old_floor);
      UT_CHECK(k.regions.gen(r) == want);
    }
    UT_CHECK(moved);
  }
  /* 2. the old domain out of ID order: swap two rows' IDs and values */
  {
    auto &id = k.regions.template get<".id">();
    auto *c = k.regions.group().template find<attr::Column<float>>("temperature");
    UT_CHECK(c != nullptr);
    const PersistentId i1 = id[1], i2 = id[2];
    const float t1 = (*c)[1], t2 = (*c)[2];
    id.set(1, i2), id.set(2, i1);
    c->set(1, t2), c->set(2, t1);
    const std::map<PersistentId, float> before = temps();
    UT_CHECK(rebuild());
    const std::map<PersistentId, float> after = temps();
    for (const auto &[pid_, t] : after) {
      UT_CHECK(before.count(pid_) == 1 && before.at(pid_) == t);
    }
  }
  /* 3. an old ID twice: the last row's values are carried */
  {
    auto &id = k.regions.template get<".id">();
    auto *c = k.regions.group().template find<attr::Column<float>>("temperature");
    const PersistentId dup = id[3], lost = id[4];
    id.set(4, dup);
    c->set(3, 333.0f), c->set(4, 444.0f);
    UT_CHECK(rebuild());
    const std::map<PersistentId, float> after = temps();
    UT_CHECK(after.at(dup) == 444.0f);
    UT_CHECK(after.at(lost) == -1.0f);
  }
}

/* The columns a build writes are compared first (plan 2026-11-24 §4.5): a
 * rebuild that changes nothing leaves them alone, and stale values, whatever
 * they are, are replaced. */
template <geom::CoordType C> static void test_compare_first()
{
  Model<C> m(root("compare-first"));
  test::gen::random_city(m, 11, 24);
  Complex<C> k;
  BuildReport br;
  RegionReport rr;
  UT_CHECK(m.b.build(k, br, m.policy) && build_regions(k, rr));
  const PersistentId h = test::complex_hash(k);
  auto &fr = k.faces.template get<".f.region">();
  auto &fs = k.faces.template get<".f.shell">();
  auto &sn = k.faces.sides().template get<".fs.shell_next">();
  auto &vc = k.verts.template get<".v.container">();
  auto &ec = k.edges.template get<".e.container">();
  const auto versions = [&] {
    return std::array<uint64_t, 5>{fr.version(), fs.version(), sn.version(), vc.version(), ec.version()};
  };
  const auto v0 = versions();
  const auto rebuild = [&] {
    RegionReport r1;
    return build_regions(k, r1) && test::complex_hash(k) == h;
  };
  UT_CHECK(rebuild() && versions() == v0);
  /* one row of one column at a time, over all of them, then many at once */
  uint32_t hinted[2] = {0, 0};
  const auto &vh = k.verts.template get<".v.container_hint">();
  const auto &eh = k.edges.template get<".e.container_hint">();
  litestl::test::SplitMix64 r(5);
  const auto spoil = [&](int col, uint32_t i) {
    if (col == 0 || col == 1) {
      auto &c = col == 0 ? fr : fs;
      U32x2 x = c[i % k.faces.capacity()];
      x[i & 1]++;
      c.set(i % k.faces.capacity(), x);
    }
    else if (col == 2) {
      sn.set(i % (2 * k.faces.capacity()), NONE - 1);
    }
    else if (col == 3 || col == 4) {
      const uint32_t n = col == 3 ? k.verts.capacity() : k.edges.capacity();
      for (uint32_t j = 0; j < n; j++) {
        const uint32_t e = (i + j) % n;
        if (!((col == 3 ? vh[e] : eh[e]) == ident::kNoId)) {
          (col == 3 ? vc : ec).set(e, ident::ContainerRef{});
          hinted[col - 3]++;
          return;
        }
      }
    }
  };
  for (int c = 0; c < 200; c++) {
    spoil(c % 5, uint32_t(r.below(1u << 20)));
    UT_CHECK(rebuild());
  }
  for (int c = 0; c < 200; c++) {
    spoil(c % 5, uint32_t(r.below(1u << 20)));
  }
  UT_CHECK(!(test::complex_hash(k) == h));
  UT_CHECK(rebuild());
  UT_CHECK(hinted[0] > 0 && hinted[1] > 0);
}

/* Band counts: identical regions at 1/2/4/12 bands. */
template <geom::CoordType C> static void test_bands()
{
  PersistentId ref{};
  for (int bands : {1, 2, 4, 12}) {
    litestl::task::ScopedBandCount scope(bands);
    Model<C> m(root("bands"));
    test::gen::random_city(m, 11, 16);
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(m.b.build(k, br, m.policy) && build_regions(k, rr));
    const PersistentId h = test::complex_hash(k);
    if (bands == 1) {
      ref = h;
    }
    UT_CHECK(h == ref);
  }
}

template <geom::CoordType C> static void run_all()
{
  test_families<C>();
  test_hints_and_rebuild<C>();
  test_bands<C>();
  test_votes_and_order<C>();
  test_carry_over<C>();
  test_compare_first<C>();
}

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  run_all<geom::Lattice>();
  run_all<double>();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
