/* Editor unit tests, P3b: compaction, region IDs and container hints,
 * imprint / holes / weld, OnCommit validation (plan P3b). */

#include "ubertopo/topo/compact.h"
#include "ubertopo/topo/freeze.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

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

/* ---- W6: imprint / unimprint / add_hole / remove_hole (plan P3b §3.6) ---- */

template <geom::CoordType C> struct Wires {
  Fixture<C> &fx;
  Editor<C> &ed;
  uint32_t n = 0;
  VertRef vert(FaceRef f, const I3 &p)
  {
    VertRef v;
    EXPECT_EDIT(ed, ed.make_vertex(pt<C>(p), ident::derive(ident::tag::TestOnly, std::string_view("wv"), n++), &v),
                EditCode::Ok);
    if (f) {
      EXPECT_EDIT(ed, ed.embed_vertex(v, EmbedTarget::face(f)), EditCode::Ok);
    }
    return v;
  }
  EdgeRef edge(FaceRef f, VertRef a, VertRef b)
  {
    EdgeRef e;
    EXPECT_EDIT(ed, ed.make_edge(a, b, ident::kNoId, &e, f ? EmbedTarget::face(f) : EmbedTarget{}), EditCode::Ok);
    return e;
  }
  /* a closed square of embedded wires on the plane y = 0 */
  std::vector<EdgeRef> square(FaceRef f, int64_t lo, int64_t hi, std::vector<VertRef> *vs = nullptr)
  {
    const VertRef a = vert(f, {lo, 0, lo}), b = vert(f, {hi, 0, lo}), c = vert(f, {hi, 0, hi}), d = vert(f, {lo, 0, hi});
    if (vs) {
      *vs = {a, b, c, d};
    }
    return {edge(f, a, b), edge(f, b, c), edge(f, c, d), edge(f, d, a)};
  }
};

template <geom::CoordType C> uint32_t loop_count(const Complex<C> &k, FaceRef f)
{
  uint32_t n = 0;
  k.for_each_loop(f, [&](LoopRef) { n++; });
  return n;
}

template <geom::CoordType C> FaceRef wall_y0(Fixture<C> &fx)
{
  return fx.face_with({{0, 0, 0}, {4, 0, 0}, {4, 0, 4}, {0, 0, 4}});
}

template <geom::CoordType C> void test_imprint()
{
  /* split case: a 3-edge chain across the wall y = 0, then unimprint */
  {
    Fixture<C> fx;
    UT_CHECK(fx.box());
    Editor<C> ed(fx.k, &fx.j);
    Wires<C> w{fx, ed};
    const FaceRef wall = wall_y0(fx);
    const VertRef a = fx.vert_at({0, 0, 0}), b = fx.vert_at({4, 0, 4});
    const VertRef p = w.vert(wall, {2, 0, 1}), q = w.vert(wall, {3, 0, 2});
    const EdgeRef e1 = w.edge(wall, a, p), e2 = w.edge(wall, p, q), e3 = w.edge(wall, q, b);
    UT_CHECK(fx.valid());
    const PersistentId h_pre = hk(fx.k);
    const uint32_t faces0 = fx.k.faces.live_count();
    /* refusals are bit-identical no-ops */
    {
      const EdgeRef bad[2] = {e1, e3}; /* not connected */
      EXPECT_EDIT(ed, ed.imprint(wall, std::span<const EdgeRef>(bad, 2)), EditCode::ChainNotSimple);
      const EdgeRef dup[2] = {e1, e1};
      EXPECT_EDIT(ed, ed.imprint(wall, std::span<const EdgeRef>(dup, 2)), EditCode::ChainNotSimple);
      const FaceRef other = fx.face_with({{0, 0, 0}, {0, 4, 0}, {4, 4, 0}, {4, 0, 0}});
      const EdgeRef one[1] = {e1};
      EXPECT_EDIT(ed, ed.imprint(other, std::span<const EdgeRef>(one, 1)), EditCode::NotEmbedded);
      UT_CHECK(hk(fx.k) == h_pre);
    }
    const EdgeRef chain[3] = {e1, e2, e3};
    FaceRef child;
    EXPECT_EDIT(ed, ed.imprint(wall, std::span<const EdgeRef>(chain, 3), &child), EditCode::Ok);
    UT_CHECK(fx.valid() && fx.k.faces.live_count() == faces0 + 1 && bool(child));
    for (const EdgeRef e : chain) {
      UT_CHECK(fx.k.edges.template get<".e.container">()[e.i].is_none());
      uint32_t m = 0;
      fx.k.for_each_radial(e, [&](CoedgeRef) { m++; });
      UT_CHECK(m == 2);
    }
    UT_CHECK(fx.k.verts.template get<".v.container">()[p.i].is_none());
    const PersistentId h_imp = hk(fx.k);
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
    UT_CHECK(hk(fx.k) == h_pre);
    EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
    UT_CHECK(hk(fx.k) == h_imp);
    /* unimprint from the middle edge: the faces merge, the chain is wires again */
    FaceRef surv;
    EXPECT_EDIT(ed, ed.unimprint(e2, &surv), EditCode::Ok);
    UT_CHECK(fx.valid() && fx.k.faces.live_count() == faces0);
    ident::ContainerRef cs;
    ident::ContainerRef::face(surv, cs);
    for (const EdgeRef e : chain) {
      UT_CHECK(fx.k.edges.template get<".e.container">()[e.i] == cs);
      UT_CHECK(fx.k.edges.template get<".e.c">()[e.i] == NONE);
    }
    UT_CHECK(fx.k.verts.template get<".v.container">()[q.i] == cs);
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
    UT_CHECK(hk(fx.k) == h_imp);
  }
  /* island case: a square on the wall with a sensor inside */
  {
    Fixture<C> fx;
    UT_CHECK(fx.box());
    Editor<C> ed(fx.k, &fx.j);
    Wires<C> w{fx, ed};
    const FaceRef wall = wall_y0(fx);
    const std::vector<EdgeRef> sq = w.square(wall, 1, 3);
    const VertRef s = w.vert(wall, {2, 0, 2});
    const PersistentId h_pre = hk(fx.k);
    FaceRef island;
    EXPECT_EDIT(ed, ed.imprint(wall, std::span<const EdgeRef>(sq.data(), sq.size()), &island), EditCode::Ok);
    UT_CHECK(fx.valid() && bool(island) && loop_count(fx.k, wall) == 2 && loop_count(fx.k, island) == 1);
    ident::ContainerRef ci;
    ident::ContainerRef::face(island, ci);
    UT_CHECK(fx.k.verts.template get<".v.container">()[s.i] == ci);
    EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok); /* the room is still closed */
    UT_CHECK(fx.valid());
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
    FaceRef surv;
    EXPECT_EDIT(ed, ed.unimprint(sq[1], &surv), EditCode::Ok);
    UT_CHECK(fx.valid() && surv == wall && loop_count(fx.k, wall) == 1);
    ident::ContainerRef cw;
    ident::ContainerRef::face(wall, cw);
    UT_CHECK(fx.k.verts.template get<".v.container">()[s.i] == cw);
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
    UT_CHECK(hk(fx.k) == h_pre);
  }
}

template <geom::CoordType C> void test_holes()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {8, 8, 8}));
  Editor<C> ed(fx.k, &fx.j);
  Wires<C> w{fx, ed};
  const FaceRef wall = fx.face_with({{0, 0, 0}, {8, 0, 0}, {8, 0, 8}, {0, 0, 8}});
  std::vector<VertRef> corners;
  const std::vector<EdgeRef> sq = w.square(wall, 3, 5, &corners);
  /* an inclusion inside the opening is refused */
  {
    const VertRef s = w.vert(wall, {4, 0, 4});
    const PersistentId h = hk(fx.k);
    EXPECT_EDIT(ed, ed.add_hole(wall, std::span<const EdgeRef>(sq.data(), sq.size())), EditCode::ContainedElement);
    UT_CHECK(hk(fx.k) == h);
    EXPECT_EDIT(ed, ed.embed_vertex(s, EmbedTarget{}), EditCode::Ok);
    EXPECT_EDIT(ed, ed.kill_vertex(s), EditCode::Ok);
  }
  /* an open chain is refused */
  {
    const EdgeRef open[3] = {sq[0], sq[1], sq[2]};
    EXPECT_EDIT(ed, ed.add_hole(wall, std::span<const EdgeRef>(open, 3)), EditCode::ChainNotSimple);
  }
  EXPECT_EDIT(ed, ed.add_hole(wall, std::span<const EdgeRef>(sq.data(), sq.size())), EditCode::Ok);
  UT_CHECK(fx.valid() && loop_count(fx.k, wall) == 2);
  LoopRef hole;
  fx.k.for_each_loop(wall, [&](LoopRef l) {
    if (l != fx.k.first_loop(wall)) {
      hole = l;
    }
  });
  /* a bigger cycle around the hole is refused (nested holes), as hole and
   * as island */
  {
    const std::vector<EdgeRef> big = w.square(wall, 1, 7);
    const PersistentId h = hk(fx.k);
    EXPECT_EDIT(ed, ed.add_hole(wall, std::span<const EdgeRef>(big.data(), big.size())), EditCode::HoleInside);
    EXPECT_EDIT(ed, ed.imprint(wall, std::span<const EdgeRef>(big.data(), big.size())), EditCode::HoleInside);
    UT_CHECK(hk(fx.k) == h);
  }
  /* a window filling the hole, then remove_hole refuses the shared edges */
  {
    const VertRef wv[4] = {corners[0], corners[3], corners[2], corners[1]};
    const uint32_t sizes[1] = {4};
    FaceRef win;
    EditCode c = ed.make_face(std::span<const VertRef>(wv, 4), std::span<const uint32_t>(sizes, 1),
                              ident::derive(ident::tag::TestOnly, std::string_view("window")), 0, ident::kNoId,
                              ident::kNoId, &win);
    if (c != EditCode::Ok) {
      /* the other orientation */
      const VertRef wv2[4] = {corners[0], corners[1], corners[2], corners[3]};
      c = ed.make_face(std::span<const VertRef>(wv2, 4), std::span<const uint32_t>(sizes, 1),
                       ident::derive(ident::tag::TestOnly, std::string_view("window")), 0, ident::kNoId, ident::kNoId,
                       &win);
    }
    EXPECT_EDIT(ed, c, EditCode::Ok);
    UT_CHECK(fx.valid());
    EXPECT_EDIT(ed, ed.remove_hole(wall, hole), EditCode::EdgeShared);
    EXPECT_EDIT(ed, ed.kill_face(win), EditCode::Ok);
  }
  EXPECT_EDIT(ed, ed.remove_hole(wall, fx.k.first_loop(wall)), EditCode::NotOuterLoop);
  EXPECT_EDIT(ed, ed.remove_hole(wall, hole), EditCode::Ok);
  UT_CHECK(fx.valid() && loop_count(fx.k, wall) == 1);
  ident::ContainerRef cw;
  ident::ContainerRef::face(wall, cw);
  for (const EdgeRef e : sq) {
    UT_CHECK(fx.k.edges.template get<".e.container">()[e.i] == cw);
  }
  UT_CHECK(fx.k.verts.template get<".v.container">()[corners[0].i] == cw);
  /* everything undoes */
  const PersistentId h_end = hk(fx.k);
  const uint32_t applied = fx.j.undo_count();
  for (uint32_t i = 0; i < applied; i++) {
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  }
  for (uint32_t i = 0; i < applied; i++) {
    EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  }
  UT_CHECK(hk(fx.k) == h_end && ed.index_consistent());
}

/* ---- W7: weld (plan P3b §3.6, C2, C3, C14) ---- */

/* The faces of an axis-aligned box as grid-point loops (GridBuilder::box's
 * order and orientation): -z, +z, -y, +y, -x, +x. */
inline std::vector<std::vector<I3>> box_loops(const I3 &lo, const I3 &hi)
{
  const int64_t x0 = lo[0], y0 = lo[1], z0 = lo[2], x1 = hi[0], y1 = hi[1], z1 = hi[2];
  return {{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}},
          {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}},
          {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}},
          {{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}},
          {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}},
          {{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}};
}

/* Structure by position (C2): vertices, edges with their coedge counts,
 * faces as oriented position cycles, and the region/shell counts. Neither
 * IDs nor row order enter it. */
template <geom::CoordType C> std::string structure(const Complex<C> &k)
{
  auto key = [&](VertRef v) {
    const auto &p = k.pos(v);
    char buf[96];
    snprintf(buf, sizeof(buf), "(%.6g,%.6g,%.6g)", double(geom::CoordTraits<C>::to_double(p[0])),
             double(geom::CoordTraits<C>::to_double(p[1])), double(geom::CoordTraits<C>::to_double(p[2])));
    return std::string(buf);
  };
  std::vector<std::string> vs, es, fs;
  k.verts.for_each_live([&](uint32_t v) { vs.push_back(key(VertRef(v))); });
  k.edges.for_each_live([&](uint32_t e) {
    const auto ev = k.edge_verts(EdgeRef(e));
    std::string a = key(ev[0]), b = key(ev[1]);
    if (b < a) {
      std::swap(a, b);
    }
    uint32_t m = 0;
    k.for_each_radial(EdgeRef(e), [&](CoedgeRef) { m++; });
    es.push_back(a + b + "#" + std::to_string(m));
  });
  k.faces.for_each_live([&](uint32_t f) {
    std::string face;
    k.for_each_loop(FaceRef(f), [&](LoopRef l) {
      std::vector<std::string> cyc;
      k.for_each_coedge(l, [&](CoedgeRef c) { cyc.push_back(key(k.start_vert(c))); });
      std::rotate(cyc.begin(), std::min_element(cyc.begin(), cyc.end()), cyc.end());
      for (auto &s : cyc) {
        face += s;
      }
      face += "|";
    });
    fs.push_back(face);
  });
  std::sort(vs.begin(), vs.end());
  std::sort(es.begin(), es.end());
  std::sort(fs.begin(), fs.end());
  std::string out = "R" + std::to_string(k.regions.live_count()) + "S" + std::to_string(k.shells.live_count()) + "\n";
  for (auto *list : {&vs, &es, &fs}) {
    for (auto &s : *list) {
      out += s + "\n";
    }
  }
  return out;
}

template <geom::CoordType C> void test_weld()
{
  /* reference: the two rooms built directly, sharing the wall x = 4 */
  Complex<C> ref;
  {
    test::GridBuilder<C> g;
    g.box({0, 0, 0}, {4, 4, 4});
    g.box({4, 0, 0}, {8, 4, 4});
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(ref, br) && build_regions(ref, rr));
  }
  /* A closed; B open (no x = 4 wall) with its own rim vertices */
  Fixture<C> fx;
  {
    test::GridBuilder<C> g;
    g.box({0, 0, 0}, {4, 4, 4});
    std::map<I3, uint32_t> bkeys;
    auto vb = [&](const I3 &p) {
      if (p[0] != 4) {
        return g.v(p); /* only the rim at x = 4 is duplicated */
      }
      auto it = bkeys.find(p);
      if (it != bkeys.end()) {
        return it->second;
      }
      const uint32_t key = g.b.vertex(ident::derive_generator(hid("bodyB"), "v", p[0], p[1], p[2]), pt<C>(p));
      bkeys[p] = key;
      return key;
    };
    const auto loops = box_loops({4, 0, 0}, {8, 4, 4});
    for (int i = 0; i < 6; i++) {
      if (i == 4) {
        continue; /* the -x wall is A's */
      }
      std::vector<uint32_t> keys;
      for (const I3 &p : loops[i]) {
        keys.push_back(vb(p));
      }
      const uint32_t sizes[1] = {4};
      g.b.face(ident::derive(ident::tag::TestOnly, std::string_view("bodyB.face"), uint32_t(i)),
               std::span<const uint32_t>(keys), std::span<const uint32_t>(sizes, 1));
    }
    BuildReport br;
    UT_CHECK(g.b.build(fx.k, br));
  }
  Editor<C> ed(fx.k, &fx.j);
  /* the pairs: A's wall corners and B's rim vertices, by position */
  std::vector<std::pair<VertRef, VertRef>> pairs;
  for (const I3 &p : std::vector<I3>{{4, 0, 0}, {4, 4, 0}, {4, 4, 4}, {4, 0, 4}}) {
    VertRef a, b;
    fx.k.verts.for_each_live([&](uint32_t v) {
      if (geom::lex_compare<C>(fx.k.pos(VertRef(v)), pt<C>(p)) == 0) {
        (a ? b : a) = VertRef(v);
      }
    });
    UT_CHECK(a && b);
    pairs.push_back({a, b});
  }
  const PersistentId h_pre = hk(fx.k);
  /* refusals */
  {
    const std::pair<VertRef, VertRef> far[1] = {{pairs[0].first, pairs[1].second}};
    EXPECT_EDIT(ed, ed.weld(std::span<const std::pair<VertRef, VertRef>>(far, 1)), EditCode::Coincident);
    const std::pair<VertRef, VertRef> twice[2] = {pairs[0], {pairs[0].second, pairs[0].first}};
    EXPECT_EDIT(ed, ed.weld(std::span<const std::pair<VertRef, VertRef>>(twice, 2)), EditCode::BadArgument);
    /* one pair alone leaves the other corners coincident: invalid */
    const std::pair<VertRef, VertRef> one[1] = {pairs[0]};
    EXPECT_EDIT(ed, ed.weld(std::span<const std::pair<VertRef, VertRef>>(one, 1)), EditCode::Invalid);
    UT_CHECK(hk(fx.k) == h_pre);
  }
  EXPECT_EDIT(ed, ed.weld(std::span<const std::pair<VertRef, VertRef>>(pairs.data(), pairs.size())), EditCode::Ok);
  UT_CHECK(fx.valid() && fx.k.regions_dirty);
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  UT_CHECK(fx.valid());
  const std::string got = structure(fx.k), want = structure(ref);
  UT_CHECK(got == want);
  if (got != want) {
    fprintf(stderr, "welded:\n%s\nbuilt:\n%s\n", got.c_str(), want.c_str());
  }
  const PersistentId h_weld = hk(fx.k);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  UT_CHECK(hk(fx.k) == h_pre);
  EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
  UT_CHECK(hk(fx.k) == h_weld && ed.index_consistent());

  /* a wire snapped onto a box corner: one pair; the end loses its container */
  {
    Fixture<C> fw;
    UT_CHECK(fw.box());
    Editor<C> ew(fw.k, &fw.j);
    const VertRef corner = fw.vert_at({0, 0, 4}); /* before a second vertex sits there */
    VertRef far, end;
    EXPECT_EDIT(ew, ew.make_vertex(pt<C>({0, 0, 9}), hid("pole_top"), &far), EditCode::Ok);
    ew.options().validate = ValidateMode::None; /* the end coincides with a corner until welded */
    EXPECT_EDIT(ew, ew.make_vertex(pt<C>({0, 0, 4}), hid("wire_end"), &end), EditCode::Ok);
    EXPECT_EDIT(ew, ew.embed_vertex(end, EmbedTarget::region(RegionRef(0))), EditCode::Ok);
    EdgeRef wire;
    EXPECT_EDIT(ew, ew.make_edge(far, end, ident::kNoId, &wire), EditCode::Ok);
    ew.options().validate = ValidateMode::Full;
    const std::pair<VertRef, VertRef> snap[1] = {{corner, end}};
    EXPECT_EDIT(ew, ew.weld(std::span<const std::pair<VertRef, VertRef>>(snap, 1)), EditCode::Ok);
    UT_CHECK(fw.valid());
    const VertRef s = fw.vert_at({0, 0, 4});
    UT_CHECK(s && fw.k.verts.template get<".v.container">()[s.i].is_none());
    const auto wv = fw.k.edge_verts(wire);
    UT_CHECK(wv[0] == s || wv[1] == s);
  }
}

/* ---- W8: wires against faces (plan P3b §3.7, C5) ---- */

template <geom::CoordType C> bool reports(const Editor<C> &ed, ValidateCode code)
{
  for (const auto &i : ed.report().validation.issues) {
    if (i.code == code) {
      return true;
    }
  }
  return false;
}

template <geom::CoordType C> void test_wire_face()
{
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {8, 8, 8}));
  Editor<C> ed(fx.k, &fx.j);
  Wires<C> w{fx, ed};
  const PersistentId h0 = hk(fx.k);
  /* a free wire piercing the wall y = 0 */
  {
    const VertRef a = w.vert({}, {4, -2, 4}), b = w.vert({}, {4, 2, 4});
    EdgeRef e;
    EXPECT_EDIT(ed, ed.make_edge(a, b, ident::kNoId, &e), EditCode::Invalid);
    UT_CHECK(reports(ed, ValidateCode::WireCrossesFace));
  }
  /* a free wire through a box corner, mid-length */
  {
    const VertRef a = w.vert({}, {-1, -1, -1}), b = w.vert({}, {1, 1, 1});
    EXPECT_EDIT(ed, ed.make_edge(a, b), EditCode::Invalid);
    UT_CHECK(reports(ed, ValidateCode::WireCrossesFace));
  }
  /* a pole on the roof: its foot is embedded in the face, the wire is free */
  {
    const FaceRef roof = fx.face_with({{0, 0, 8}, {8, 0, 8}, {8, 8, 8}, {0, 8, 8}});
    const VertRef foot = w.vert(roof, {4, 4, 8}), top = w.vert({}, {4, 4, 12});
    EXPECT_EDIT(ed, ed.make_edge(foot, top), EditCode::Ok);
  }
  /* a wire embedded in the wall, across a hole, and through its corners */
  {
    const FaceRef wall = fx.face_with({{0, 0, 0}, {8, 0, 0}, {8, 0, 8}, {0, 0, 8}});
    const std::vector<EdgeRef> sq = w.square(wall, 3, 5);
    EXPECT_EDIT(ed, ed.add_hole(wall, std::span<const EdgeRef>(sq.data(), sq.size())), EditCode::Ok);
    const VertRef a = w.vert(wall, {2, 0, 4}), b = w.vert(wall, {6, 0, 4});
    EXPECT_EDIT(ed, ed.make_edge(a, b, ident::kNoId, nullptr, EmbedTarget::face(wall)), EditCode::Invalid);
    UT_CHECK(reports(ed, ValidateCode::WireCrossesFace));
    const VertRef c = w.vert(wall, {2, 0, 2}), d = w.vert(wall, {6, 0, 6});
    EXPECT_EDIT(ed, ed.make_edge(c, d, ident::kNoId, nullptr, EmbedTarget::face(wall)), EditCode::Invalid);
    UT_CHECK(reports(ed, ValidateCode::WireCrossesFace));
    /* grazing one hole corner (midpoint outside the hole): only the corner rule sees it */
    const VertRef g0 = w.vert(wall, {2, 0, 4}), g1 = w.vert(wall, {5, 0, 1});
    EXPECT_EDIT(ed, ed.make_edge(g0, g1, ident::kNoId, nullptr, EmbedTarget::face(wall)), EditCode::Invalid);
    UT_CHECK(reports(ed, ValidateCode::WireCrossesFace));
    /* corner to corner across the opening: only the midpoint rule sees it */
    EXPECT_EDIT(ed, ed.make_edge(fx.vert_at({3, 0, 3}), fx.vert_at({5, 0, 5}), ident::kNoId, nullptr, EmbedTarget::face(wall)),
                EditCode::Invalid);
    UT_CHECK(reports(ed, ValidateCode::WireCrossesFace));
    /* beside the hole it is fine */
    const VertRef p = w.vert(wall, {1, 0, 1}), q = w.vert(wall, {1, 0, 7});
    EXPECT_EDIT(ed, ed.make_edge(p, q, ident::kNoId, nullptr, EmbedTarget::face(wall)), EditCode::Ok);
  }
  UT_CHECK(fx.valid());
  while (fx.j.undo_count()) {
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  }
  UT_CHECK(hk(fx.k) == h0);
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_compact<double>();
  test_compact<geom::Lattice>();
  test_region_ids<double>();
  test_region_ids<geom::Lattice>();
  test_imprint<double>();
  test_imprint<geom::Lattice>();
  test_holes<double>();
  test_holes<geom::Lattice>();
  test_weld<double>();
  test_weld<geom::Lattice>();
  test_wire_face<double>();
  test_wire_face<geom::Lattice>();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
