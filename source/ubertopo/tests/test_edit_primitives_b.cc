/* Editor unit tests, P3b: compaction, region IDs and container hints,
 * imprint / holes / weld, OnCommit validation (plan P3b). */

#include "ubertopo/topo/compact.h"
#include "ubertopo/topo/freeze.h"

#include "litestl/util/alloc.h"
#include "support/edit_fixture.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
using test::Fixture;
using test::I3;
using test::pt;

namespace {

template <geom::CoordType C> PersistentId hk(const Complex<C> &k)
{
  return content_hash(k);
}

/* ---- W2: compaction as a barrier (plan P3b §3.2, C1) ---- */
template <geom::CoordType C> void test_compact()
{
  Fixture<C> fx;
  UT_CHECK(fx.box());
  Editor<C> ed(fx.k, &fx.j);
  const PersistentId h0 = hk(fx.k);
  /* tombstones: a vertex made and killed, an edge split and joined again */
  VertRef v;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({9, 9, 9}), ident::derive(ident::tag::TestOnly, 1u), &v), EditCode::Ok);
  VertRef last;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({9, 9, 10}), ident::derive(ident::tag::TestOnly, 2u), &last), EditCode::Ok);
  EXPECT_EDIT(ed, ed.kill_vertex(v), EditCode::Ok);
  const EdgeRef e = fx.edge_between({0, 0, 0}, {4, 0, 0});
  VertRef mid;
  EXPECT_EDIT(ed, ed.split_edge(e, pt<C>({2, 0, 0}), &mid), EditCode::Ok);
  EXPECT_EDIT(ed, ed.join_edge(mid), EditCode::Ok);
  UT_CHECK(has_tombstones(fx.k));
  const auto h_last = fx.k.verts.handle(last);
  const PersistentId last_id = fx.k.vert_id(last);
  const PersistentId h_before = hk(fx.k);
  const uint32_t undo_before = fx.j.undo_count();

  /* refused inside a transaction (C1) */
  EXPECT_EDIT(ed, ed.begin("t"), EditCode::Ok);
  EXPECT_EDIT(ed, ed.compact(), EditCode::OpenTransaction);
  EXPECT_EDIT(ed, ed.rollback(), EditCode::Ok);
  UT_CHECK(hk(fx.k) == h_before);

  EXPECT_EDIT(ed, ed.compact(), EditCode::Ok);
  UT_CHECK(!has_tombstones(fx.k) && fx.valid());
  UT_CHECK(fx.j.undo_count() == undo_before + 1);
  const PersistentId h_compact = hk(fx.k);
  /* the moved vertex: its old handle doesn't resolve, its ID does */
  UT_CHECK(!fx.k.verts.resolve(h_last));
  bool found = false;
  fx.k.verts.for_each_live([&](uint32_t i) { found = found || fx.k.vert_id(VertRef(i)) == last_id; });
  UT_CHECK(found);
  /* nothing to drop: no transaction */
  EXPECT_EDIT(ed, ed.compact(), EditCode::Ok);
  UT_CHECK(fx.j.undo_count() == undo_before + 1 && hk(fx.k) == h_compact);

  /* edits after the barrier, then undo through it and redo everything */
  VertRef w;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({8, 8, 8}), ident::derive(ident::tag::TestOnly, 3u), &w), EditCode::Ok);
  const PersistentId h_end = hk(fx.k);
  while (fx.j.undo_count() > undo_before) {
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  }
  UT_CHECK(hk(fx.k) == h_before);
  while (fx.j.undo_count() > 0) {
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  }
  UT_CHECK(hk(fx.k) == h0);
  while (fx.j.redo_count() > 0) {
    EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  }
  UT_CHECK(hk(fx.k) == h_end);

  /* a frozen complex is refused */
  {
    Fixture<C> fz;
    UT_CHECK(fz.box());
    UT_CHECK(freeze(fz.k) == FreezeResult::Ok);
    Editor<C> ez(fz.k, &fz.j);
    EXPECT_EDIT(ez, ez.compact(), EditCode::Frozen);
  }
}

/* ---- W3: region identity (DEC-017) and container hints (P3b C4) ---- */

PersistentId hid(const char *name)
{
  return ident::derive(ident::tag::TestOnly, std::string_view(name));
}

/* A box with per-face hints: fronts face outward (out[i]), backs inward (in).
 * Face order: -z, +z, -y, +y, -x, +x. */
template <geom::CoordType C>
void hbox(test::GridBuilder<C> &g, const I3 &lo, const I3 &hi, PersistentId in, const PersistentId out[6])
{
  const int64_t x0 = lo[0], y0 = lo[1], z0 = lo[2], x1 = hi[0], y1 = hi[1], z1 = hi[2];
  g.face({{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}}, out[0], in);
  g.face({{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, out[1], in);
  g.face({{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, out[2], in);
  g.face({{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}}, out[3], in);
  g.face({{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}, out[4], in);
  g.face({{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}}, out[5], in);
}

/* Rooms A = [0,4]^3 and B = [4,8]x[0,4]^2 sharing the wall x = 4, with a
 * sensor in A hinted @p sensor_hint. */
template <geom::CoordType C>
bool two_rooms(Complex<C> &k, PersistentId ha, PersistentId hb, PersistentId sensor_hint)
{
  const PersistentId unb = ident::unbounded_region_id();
  test::GridBuilder<C> g;
  const PersistentId outa[6] = {unb, unb, unb, unb, unb, hb}; /* +x: the shared wall faces B */
  const PersistentId outb[6] = {unb, unb, unb, unb, ha, unb};
  hbox<C>(g, {0, 0, 0}, {4, 4, 4}, ha, outa);
  hbox<C>(g, {4, 0, 0}, {8, 4, 4}, hb, outb); /* its -x face dedups onto A's +x */
  g.b.vertex(hid("sensor"), pt<C>({1, 1, 1}), ContainerHint::in_region(sensor_hint));
  BuildReport br;
  RegionReport rr;
  return g.b.build(k, br) && build_regions(k, rr);
}

template <geom::CoordType C> uint32_t region_of_id(const Complex<C> &k, PersistentId id)
{
  for (uint32_t r = 0; r < k.regions.capacity(); r++) {
    if (k.regions.live(r) && k.regions.template get<".id">()[r] == id) {
      return r;
    }
  }
  return NONE;
}

template <geom::CoordType C> VertRef vertex_by_id(const Complex<C> &k, PersistentId id)
{
  VertRef out;
  k.verts.for_each_live([&](uint32_t v) {
    if (k.vert_id(VertRef(v)) == id) {
      out = VertRef(v);
    }
  });
  return out;
}

template <geom::CoordType C> void test_region_ids()
{
  /* a hinted room keeps its ID and attributes across a split + rebuild */
  {
    Fixture<C> fx;
    const PersistentId ha = hid("roomA"), hb = hid("roomB");
    UT_CHECK(two_rooms<C>(fx.k, ha, hb, ha));
    const uint32_t ra = region_of_id(fx.k, ident::region_id_from_hint(ha));
    UT_CHECK(ra != NONE);
    auto *medium = fx.k.regions.group().template ensure<litestl::attr::Column<double>>("medium");
    medium->set(ra, 0.25);
    Editor<C> ed(fx.k, &fx.j);
    const FaceRef wall = fx.face_with({{0, 0, 0}, {4, 0, 0}, {4, 0, 4}, {0, 0, 4}});
    UT_CHECK(bool(wall));
    EXPECT_EDIT(ed, ed.split_face(wall, fx.vert_at({0, 0, 0}), fx.vert_at({4, 0, 4})), EditCode::Ok);
    UT_CHECK(fx.k.regions_dirty);
    EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
    const uint32_t ra2 = region_of_id(fx.k, ident::region_id_from_hint(ha));
    UT_CHECK(ra2 != NONE);
    medium = fx.k.regions.group().template find<litestl::attr::Column<double>>("medium"); /* the barrier replaced the group */
    UT_CHECK(medium && (*medium)[ra2] == 0.25);
    const VertRef s = vertex_by_id(fx.k, hid("sensor"));
    UT_CHECK(fx.k.verts.template get<".v.container">()[s.i].as_region().i == ra2);
  }
  /* OI-P3-2: removing the wall merges the rooms; the sensor's hint loses the
   * vote but still resolves, as a minority carrier of the merged room */
  {
    Fixture<C> fx;
    PersistentId ha = hid("roomA"), hb = hid("roomB");
    if (ha < hb) {
      std::swap(ha, hb); /* ties go to the smaller hint: make A's lose */
    }
    UT_CHECK(two_rooms<C>(fx.k, ha, hb, ha));
    Editor<C> ed(fx.k, &fx.j);
    const FaceRef wall = fx.face_with({{4, 0, 0}, {4, 4, 0}, {4, 4, 4}, {4, 0, 4}});
    EXPECT_EDIT(ed, ed.kill_face(wall), EditCode::Ok);
    EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
    const uint32_t merged = region_of_id(fx.k, ident::region_id_from_hint(hb));
    UT_CHECK(merged != NONE && region_of_id(fx.k, ident::region_id_from_hint(ha)) == NONE);
    const VertRef s = vertex_by_id(fx.k, hid("sensor"));
    UT_CHECK(fx.k.verts.template get<".v.container">()[s.i].as_region().i == merged);
    UT_CHECK(fx.valid());
  }
  /* precedence: a hint that wins in one region and is a minority in another
   * resolves to the winner; embedding writes the winning hint */
  {
    Fixture<C> fx;
    const PersistentId ha = hid("roomA"), hb = hid("roomB");
    UT_CHECK(two_rooms<C>(fx.k, ha, hb, ha));
    Editor<C> ed(fx.k, &fx.j);
    /* B's floor carries A's hint on its inner side: A's hint is now a
     * minority in B and still wins in A */
    const FaceRef bfloor = fx.face_with({{4, 0, 0}, {8, 0, 0}, {8, 4, 0}, {4, 4, 0}});
    EXPECT_EDIT(ed, ed.set_side_hint(bfloor, 1, ha), EditCode::Ok);
    EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
    const uint32_t ra = region_of_id(fx.k, ident::region_id_from_hint(ha));
    const VertRef s = vertex_by_id(fx.k, hid("sensor"));
    UT_CHECK(ra != NONE && fx.k.verts.template get<".v.container">()[s.i].as_region().i == ra);
    /* embedding into B writes B's winning hint, not the smaller carried one */
    const uint32_t rb = region_of_id(fx.k, ident::region_id_from_hint(hb));
    VertRef v;
    EXPECT_EDIT(ed, ed.make_vertex(pt<C>({6, 1, 1}), hid("probe"), &v), EditCode::Ok);
    EXPECT_EDIT(ed, ed.embed_vertex(v, EmbedTarget::region(RegionRef(rb))), EditCode::Ok);
    UT_CHECK(fx.k.verts.template get<".v.container_hint">()[v.i] == hb);
  }
  /* two regions winning the same hint: a container naming it is ambiguous,
   * even though a third region carries it as a minority (C4 rule 2) */
  {
    const PersistentId hx = hid("dup"), hc = hid("third"), unb = ident::unbounded_region_id();
    test::GridBuilder<C> g;
    const PersistentId out[6] = {unb, unb, unb, unb, unb, unb};
    hbox<C>(g, {0, 0, 0}, {2, 2, 2}, hx, out);
    hbox<C>(g, {4, 0, 0}, {6, 2, 2}, hx, out);
    test::GridBuilder<C> &gg = g;
    gg.face({{{8, 0, 0}, {8, 2, 0}, {10, 2, 0}, {10, 0, 0}}}, unb, hx); /* C's floor carries hx */
    gg.face({{{8, 0, 2}, {10, 0, 2}, {10, 2, 2}, {8, 2, 2}}}, unb, hc);
    gg.face({{{8, 0, 0}, {10, 0, 0}, {10, 0, 2}, {8, 0, 2}}}, unb, hc);
    gg.face({{{8, 2, 0}, {8, 2, 2}, {10, 2, 2}, {10, 2, 0}}}, unb, hc);
    gg.face({{{8, 0, 0}, {8, 0, 2}, {8, 2, 2}, {8, 2, 0}}}, unb, hc);
    gg.face({{{10, 0, 0}, {10, 2, 0}, {10, 2, 2}, {10, 0, 2}}}, unb, hc);
    g.b.vertex(hid("s2"), pt<C>({1, 1, 1}), ContainerHint::in_region(hx));
    Complex<C> k;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(k, br));
    UT_CHECK(!build_regions(k, rr));
    bool ambiguous = false;
    for (const RegionIssue &i : rr.issues) {
      ambiguous = ambiguous || i.code == RegionCode::AmbiguousContainerHint;
    }
    UT_CHECK(ambiguous);
  }
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_compact<double>();
  test_compact<geom::Lattice>();
  test_region_ids<double>();
  test_region_ids<geom::Lattice>();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
