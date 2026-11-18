/* build_regions() against generator ground truth (plan P2 W7, W9). */

#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

#include "gen/buildings.h"
#include "litestl/util/alloc.h"
#include "support/topo_fixtures.h"
#include "ut_test.h"

#include <map>

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
  const ValidationReport vr = validate(k);
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
}

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  run_all<geom::Lattice>();
  run_all<double>();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
