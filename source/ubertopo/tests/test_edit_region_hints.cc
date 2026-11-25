/* Embeds into a region: which hint they store and when they are refused
 * (P3b C4), and the editor's derived state behind them, the cached winning
 * hints (editor_region_hints.h) and the index's face inclusions across region
 * rebuilds (plan 2026-11-24). */

#include <map>
#include <vector>

#include "ubertopo/topo/compact.h"

#include "litestl/util/alloc.h"
#include "support/edit_fixture.h"
#include "support/splitmix64.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
using test::Fixture;
using test::I3;
using test::pt;

namespace {

PersistentId hid(uint32_t i)
{
  return ident::derive(ident::tag::TestOnly, i);
}

/* A box whose sides have their own hints: in[i] inward, out[i] outward.
 * Face order: -z, +z, -y, +y, -x, +x. */
template <geom::CoordType C>
void hbox(test::GridBuilder<C> &g, const I3 &lo, const I3 &hi, const PersistentId in[6], const PersistentId out[6])
{
  const int64_t x0 = lo[0], y0 = lo[1], z0 = lo[2], x1 = hi[0], y1 = hi[1], z1 = hi[2];
  g.face({{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}}, out[0], in[0]);
  g.face({{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, out[1], in[1]);
  g.face({{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, out[2], in[2]);
  g.face({{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}}, out[3], in[3]);
  g.face({{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}, out[4], in[4]);
  g.face({{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}}, out[5], in[5]);
}

/* every region's winning hint, by brute force over the columns: most sides,
 * ties to the smaller hint, none if the unbounded hint wins */
template <geom::CoordType C> std::vector<PersistentId> brute_winners(const Complex<C> &k)
{
  std::vector<std::map<PersistentId, uint32_t>> votes(k.regions.capacity());
  k.faces.for_each_live([&](uint32_t f) {
    for (uint32_t s = 0; s < 2; s++) {
      const uint32_t r = k.region(FaceRef(f), s).i;
      const PersistentId h = k.side_hint(FaceRef(f), s);
      if (r < votes.size() && !(h == ident::kNoId)) {
        votes[r][h]++;
      }
    }
  });
  std::vector<PersistentId> out(votes.size());
  for (size_t r = 1; r < votes.size(); r++) {
    uint32_t best = 0;
    for (const auto &[h, n] : votes[r]) {
      if (n > best) {
        best = n;
        out[r] = h;
      }
    }
    if (out[r] == ident::unbounded_region_id()) {
      out[r] = PersistentId{};
    }
  }
  return out;
}

struct Seen {
  uint32_t ok = 0, unhinted = 0, shared = 0;
};

/* embeds @p v into every region in turn, rolling each back: the stored hint
 * is the region's winner, refused if it has none or another region shares
 * it. Leaves the complex and the journal as they were. @p built: the regions
 * come straight from build_regions, so their IDs are a second oracle. */
template <geom::CoordType C> void check_embeds(Editor<C> &ed, VertRef v, bool built, Seen *seen = nullptr)
{
  Complex<C> &k = ed.complex();
  UT_CHECK(!k.regions_dirty);
  const PersistentId h0 = content_hash(k);
  const std::vector<PersistentId> win = brute_winners(k);
  for (uint32_t r = 1; r < win.size(); r++) {
    uint32_t n = 0;
    for (uint32_t q = 1; q < win.size(); q++) {
      n += win[q] == win[r];
    }
    const bool none = win[r] == ident::kNoId, ok = !none && n == 1;
    if (built && !none) {
      UT_CHECK(ok == (k.regions.template get<".id">()[r] == ident::region_id_from_hint(win[r])));
    }
    UT_CHECK(ed.begin("probe") == EditCode::Ok);
    EXPECT_EDIT(ed, ed.embed_vertex(v, EmbedTarget::region(RegionRef(r))), ok ? EditCode::Ok : EditCode::UnhintedRegion);
    if (ok) {
      UT_CHECK(k.verts.template get<".v.container_hint">()[v.i] == win[r]);
      UT_CHECK(k.verts.template get<".v.container">()[v.i].as_region().i == r);
    }
    UT_CHECK(ed.rollback() == EditCode::Ok);
    if (seen) {
      seen->ok += ok;
      seen->unhinted += none;
      seen->shared += !none && !ok;
    }
  }
  UT_CHECK(content_hash(k) == h0);
  UT_CHECK(ed.index_consistent() && ed.region_hints_consistent());
}

/* ---- the vote alone, with few and many distinct hints ---- */

void test_votes()
{
  litestl::test::SplitMix64 r(5);
  HintVotes votes; /* reused */
  for (int c = 0; c < 400; c++) {
    const uint32_t distinct = uint32_t(1 + r.below(c % 2 ? 30 : 5));
    const uint32_t n = uint32_t(r.below(80));
    std::map<PersistentId, uint32_t> ref;
    std::vector<PersistentId> order;
    votes.clear();
    for (uint32_t i = 0; i < n; i++) {
      const PersistentId h = hid(uint32_t(r.below(distinct)));
      const uint32_t m = uint32_t(1 + r.below(3));
      if (m == 1) {
        votes.add(h);
      }
      else {
        votes.add(h, m);
      }
      uint32_t &have = ref[h];
      if (have == 0) {
        order.push_back(h);
      }
      have += m;
    }
    PersistentId best{};
    uint32_t most = 0;
    for (const auto &[h, m] : ref) {
      if (m > most) {
        most = m;
        best = h;
      }
    }
    UT_CHECK(votes.winner() == best);
    bool same = votes.votes().size() == order.size();
    for (size_t i = 0; same && i < order.size(); i++) {
      const HintVotes::Vote &v = votes.votes()[int(i)];
      same = v.h == order[i] && v.n == ref[order[i]];
    }
    UT_CHECK(same);
  }
}

/* ---- random hints against brute force, through rebuilds and undo ---- */

template <geom::CoordType C> void test_random_hints()
{
  litestl::test::SplitMix64 r(11);
  Seen seen;
  uint32_t merged = 0;
  for (int round = 0; round < 12; round++) {
    /* a small pool, so that ties, shared winners and wins by the unbounded
     * hint are common; kNoId leaves a side unhinted */
    const PersistentId pool[7] = {ident::unbounded_region_id(), ident::unbounded_region_id(), PersistentId{}, hid(1),
                                  hid(2), hid(3), hid(4)};
    auto draw = [&]() { return pool[r.below(round < 2 ? 4 : 7)]; };
    test::GridBuilder<C> g;
    for (int b = 0; b < 6; b++) {
      PersistentId in[6], out[6];
      /* outside sides carry rooms' hints too: region 0 must not count */
      const PersistentId main = hid(uint32_t(10 + r.below(8)));
      for (int i = 0; i < 6; i++) {
        in[i] = b % 2 ? draw() : (i < 4 ? main : draw());
        out[i] = r.below(2) ? main : draw();
      }
      hbox<C>(g, {4 * b, 0, 0}, {4 * b + 4, 4, 4}, in, out); /* a row: neighbours share a wall */
    }
    Fixture<C> fx;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(fx.k, br) && build_regions(fx.k, rr));
    EditorOptions opt;
    opt.validate = ValidateMode::None; /* the probe is embedded anywhere */
    Editor<C> ed(fx.k, &fx.j, nullptr, opt);
    VertRef v;
    EXPECT_EDIT(ed, ed.make_vertex(pt<C>({1, 1, 1}), hid(1000), &v), EditCode::Ok);
    check_embeds(ed, v, true, &seen);
    /* a hint change or a wall removed (regions merge and shift), a rebuild
     * while the cache is warm, then both undone and redone */
    for (int n = 0; n < 6; n++) {
      const bool wall = n >= 4; /* the rooms are in a row: five walls at first */
      FaceRef f;
      do {
        f = FaceRef(uint32_t(r.below(fx.k.faces.capacity())));
      } while (!fx.k.faces.live(f.i) || (wall && (fx.k.region(f, 0).i == 0 || fx.k.region(f, 1).i == 0)));
      const EditCode c = wall ? ed.kill_face(f) : ed.set_side_hint(f, uint32_t(r.below(2)), draw());
      UT_CHECK(c == EditCode::Ok);
      merged += wall;
      UT_CHECK(ed.region_hints_consistent());
      EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
      check_embeds(ed, v, true, &seen);
      if (n % 2) {
        EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
        EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
        check_embeds(ed, v, true);
        EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
        EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
        check_embeds(ed, v, true);
      }
    }
    while (fx.j.undo_count() > 1) { /* all but the probe */
      EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
      UT_CHECK(ed.region_hints_consistent());
      if (!fx.k.regions_dirty) {
        check_embeds(ed, v, true);
      }
    }
  }
  const bool enough = seen.ok > 100 && seen.unhinted > 50 && seen.shared > 50 && merged == 24;
  UT_CHECK(enough);
  if (!enough) {
    fprintf(stderr, "  embeds: %u ok, %u unhinted, %u shared; %u merges\n", seen.ok, seen.unhinted, seen.shared, merged);
  }
}

/* ---- ties and the unbounded hint, spelled out ---- */

template <geom::CoordType C> void test_rules()
{
  const PersistentId unb = ident::unbounded_region_id();
  const PersistentId below = test::pid(1001), above = test::pid(1001, ~0ull); /* IDs order hi first */
  UT_CHECK(below < unb && unb < above);
  const PersistentId out[6] = {below, below, below, below, below, below}; /* region 0 carries a room's hint */
  auto embed = [&](const PersistentId (&in)[6], PersistentId &stored) {
    test::GridBuilder<C> g;
    hbox<C>(g, {0, 0, 0}, {4, 4, 4}, in, out);
    Fixture<C> fx;
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(fx.k, br) && build_regions(fx.k, rr));
    EditorOptions opt;
    opt.validate = ValidateMode::None;
    Editor<C> ed(fx.k, &fx.j, nullptr, opt);
    VertRef v;
    EXPECT_EDIT(ed, ed.make_vertex(pt<C>({1, 1, 1}), hid(1000), &v), EditCode::Ok);
    const EditCode c = ed.embed_vertex(v, EmbedTarget::region(RegionRef(1)));
    stored = fx.k.verts.template get<".v.container_hint">()[v.i];
    return c;
  };
  PersistentId h;
  UT_CHECK(embed({above, below, above, below, above, below}, h) == EditCode::Ok && h == below); /* tie: smaller */
  UT_CHECK(embed({below, unb, below, unb, below, unb}, h) == EditCode::Ok && h == below); /* beats a tied unb */
  UT_CHECK(embed({above, unb, above, unb, above, unb}, h) == EditCode::UnhintedRegion); /* tied unb wins */
  UT_CHECK(embed({below, unb, unb, unb, unb, below}, h) == EditCode::UnhintedRegion); /* unb's majority */
  UT_CHECK(embed({above, above, above, below, below, unb}, h) == EditCode::Ok && h == above); /* majority */
  UT_CHECK(embed({{}, {}, {}, {}, {}, above}, h) == EditCode::Ok && h == above); /* unhinted sides don't vote */
  UT_CHECK(embed({{}, {}, {}, {}, {}, {}}, h) == EditCode::UnhintedRegion);
}

/* ---- the cache's lifetime ---- */

template <geom::CoordType C> void test_lifetime()
{
  PersistentId ha = hid(21), hb = hid(22);
  if (ha < hb) {
    std::swap(ha, hb); /* B's hint is the smaller: a tie goes to B */
  }
  const PersistentId unb = ident::unbounded_region_id();
  Fixture<C> fx;
  test::GridBuilder<C> g;
  const PersistentId ina[6] = {ha, ha, ha, ha, ha, ha}, inb[6] = {hb, hb, hb, hb, hb, hb};
  const PersistentId outa[6] = {unb, unb, unb, unb, unb, hb}, outb[6] = {unb, unb, unb, unb, unb, unb};
  hbox<C>(g, {0, 0, 0}, {4, 4, 4}, ina, outa);
  hbox<C>(g, {4, 0, 0}, {8, 4, 4}, inb, outb);
  BuildReport br;
  RegionReport rr;
  UT_CHECK(g.b.build(fx.k, br) && build_regions(fx.k, rr));
  EditorOptions opt;
  opt.validate = ValidateMode::None;
  Editor<C> ed(fx.k, &fx.j, nullptr, opt);
  VertRef v;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({1, 1, 1}), hid(1000), &v), EditCode::Ok);
  check_embeds(ed, v, true);
  const FaceRef floor_a = fx.face_with({{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}});
  const FaceRef wall = fx.face_with({{4, 0, 0}, {4, 4, 0}, {4, 4, 4}, {4, 0, 4}});
  UT_CHECK(floor_a && wall);

  /* a transaction that changes hints, rolled back */
  EXPECT_EDIT(ed, ed.begin("t"), EditCode::Ok);
  EXPECT_EDIT(ed, ed.set_side_hint(floor_a, 1, hb), EditCode::Ok);
  UT_CHECK(ed.region_hints_consistent());
  EXPECT_EDIT(ed, ed.embed_vertex(v, EmbedTarget::region(RegionRef(1))), EditCode::RegionsDirty);
  EXPECT_EDIT(ed, ed.rollback(), EditCode::Ok);
  check_embeds(ed, v, true);

  /* a barrier that fails under a warm cache: the rooms come to share a hint
   * that a container uses */
  VertRef w;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({2, 2, 2}), hid(1001), &w), EditCode::Ok);
  const uint32_t ra = fx.k.region(floor_a, 1).i;
  EXPECT_EDIT(ed, ed.embed_vertex(w, EmbedTarget::region(RegionRef(ra))), EditCode::Ok);
  EXPECT_EDIT(ed, ed.begin("t"), EditCode::Ok);
  std::vector<uint32_t> sides_b;
  fx.k.faces.for_each_live([&](uint32_t f) {
    for (uint32_t s = 0; s < 2; s++) {
      if (fx.k.side_hint(FaceRef(f), s) == hb) {
        sides_b.push_back(2 * f + s);
      }
    }
  });
  UT_CHECK(sides_b.size() == 6);
  for (uint32_t s : sides_b) {
    EXPECT_EDIT(ed, ed.set_side_hint(FaceRef(s / 2), s & 1, ha), EditCode::Ok);
  }
  EXPECT_EDIT(ed, ed.commit(), EditCode::Ok);
  UT_CHECK(ed.region_hints_consistent());
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::RegionsFailed);
  UT_CHECK(fx.k.regions_dirty && ed.index_consistent() && ed.region_hints_consistent());
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok); /* the hints */
  check_embeds(ed, v, true);
  EXPECT_EDIT(ed, ed.undo(), EditCode::Ok); /* w's embedding */

  /* a merge: regions are renumbered, and the index of every step of undo and
   * redo stays consistent */
  EXPECT_EDIT(ed, ed.kill_face(wall), EditCode::Ok);
  UT_CHECK(ed.region_hints_consistent());
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  check_embeds(ed, v, true); /* one room now: the tie goes to hb */
  UT_CHECK(fx.k.regions.live_count() == 2);
  EXPECT_EDIT(ed, ed.embed_vertex(w, EmbedTarget::region(RegionRef(1))), EditCode::Ok);
  const PersistentId h_embedded = content_hash(fx.k);
  while (fx.j.undo_count()) {
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
    UT_CHECK(ed.index_consistent() && ed.region_hints_consistent());
  }
  while (fx.j.redo_count()) {
    EXPECT_EDIT(ed, ed.redo(), EditCode::Ok);
    UT_CHECK(ed.index_consistent() && ed.region_hints_consistent());
  }
  UT_CHECK(content_hash(fx.k) == h_embedded);

  /* hinted vertices appended after a barrier and indexed, then undone across
   * the barrier with no query between: the index holds rows past the end of
   * the columns when the barrier's undo refreshes it */
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  const uint32_t base = fx.j.undo_count();
  const uint32_t rows = fx.k.verts.capacity();
  for (uint32_t i = 0; i < 3000; i++) {
    VertRef y;
    EXPECT_EDIT(ed, ed.make_vertex(pt<C>({3, 3, 3}), hid(2000 + i), &y), EditCode::Ok);
    EXPECT_EDIT(ed, ed.embed_vertex(y, EmbedTarget::region(RegionRef(1))), EditCode::Ok);
  }
  UT_CHECK(ed.index_consistent() && fx.k.verts.capacity() == rows + 3000);
  while (fx.j.undo_count() >= base) {
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
  }
  UT_CHECK(fx.k.verts.capacity() == rows && ed.index_consistent());
  UT_CHECK(content_hash(fx.k) == h_embedded);

  /* compaction renumbers faces under a warm cache */
  UT_CHECK(has_tombstones(fx.k));
  EXPECT_EDIT(ed, ed.compact(), EditCode::Ok);
  VertRef v2;
  fx.k.verts.for_each_live([&](uint32_t i) {
    if (fx.k.vert_id(VertRef(i)) == hid(1000)) {
      v2 = VertRef(i);
    }
  });
  UT_CHECK(v2);
  check_embeds(ed, v2, true);

  /* outside writes while the regions are clean, under a warm cache: the hint
   * of every side in the room, then one face's regions (one out of range,
   * which must not vote or be used as an index) */
  fx.k.faces.for_each_live([&](uint32_t f) {
    for (uint32_t s = 0; s < 2; s++) {
      if (fx.k.region(FaceRef(f), s).i == 1) {
        fx.k.faces.sides().template get<".fs.hint">().set(2 * f + s, hid(23));
      }
    }
  });
  check_embeds(ed, v2, false);
  uint32_t n = 0;
  fx.k.faces.for_each_live([&](uint32_t f) {
    if (n++ < 2) {
      fx.k.faces.template get<".f.region">().set(f, U32x2{n == 1 ? 1000000u : 0u, 1u});
    }
  });
  fx.k.faces.sides().template get<".fs.hint">().set(0, hid(24));
  check_embeds(ed, v2, false);
  /* every other side leaves the room: only the regions column changes */
  fx.k.faces.for_each_live([&](uint32_t f) { fx.k.faces.template get<".f.region">().set(f, U32x2{0u, 0u}); });
  check_embeds(ed, v2, false);
}

/* ---- a hinted element with a face container (only a loaded complex can
 * hold one): region rebuilds move it out of the face's inclusions, and their
 * undo puts it back ---- */

template <geom::CoordType C> void test_face_container_hint(bool edge)
{
  const PersistentId ha = hid(31), unb = ident::unbounded_region_id();
  Fixture<C> fx;
  test::GridBuilder<C> g;
  const PersistentId in[6] = {ha, ha, ha, ha, ha, ha}, out[6] = {unb, unb, unb, unb, unb, unb};
  hbox<C>(g, {0, 0, 0}, {4, 4, 4}, in, out);
  BuildReport br;
  RegionReport rr;
  UT_CHECK(g.b.build(fx.k, br) && build_regions(fx.k, rr));
  EditorOptions opt;
  opt.validate = ValidateMode::None;
  Editor<C> ed(fx.k, &fx.j, nullptr, opt);
  VertRef v, w;
  EdgeRef e;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({1, 1, 1}), hid(1000), &v), EditCode::Ok);
  if (edge) {
    EXPECT_EDIT(ed, ed.make_vertex(pt<C>({2, 2, 2}), hid(1001), &w), EditCode::Ok);
    EXPECT_EDIT(ed, ed.make_edge(v, w, ident::kNoId, &e), EditCode::Ok);
    EXPECT_EDIT(ed, ed.embed_edge(e, EmbedTarget::region(RegionRef(1))), EditCode::Ok);
  }
  else {
    EXPECT_EDIT(ed, ed.embed_vertex(v, EmbedTarget::region(RegionRef(1))), EditCode::Ok);
  }
  const FaceRef floor = fx.face_with({{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}});
  ident::ContainerRef cr;
  UT_CHECK(ident::ContainerRef::face(floor, cr));
  if (edge) {
    fx.k.edges.template get<".e.container">().set(e.i, cr);
  }
  else {
    fx.k.verts.template get<".v.container">().set(v.i, cr);
  }
  auto contained = [&]() { return edge ? fx.k.edges.template get<".e.container">()[e.i] : fx.k.vert_container(v); };
  /* a primitive that flushes the index, so it is valid before the barrier */
  VertRef x;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({3, 3, 3}), hid(1002), &x), EditCode::Ok);
  EXPECT_EDIT(ed, ed.kill_face(floor), EditCode::FaceHasInclusions);
  UT_CHECK(ed.index_consistent());
  for (int pass = 0; pass < 2; pass++) {
    EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
    UT_CHECK(contained().as_region().i == 1 && ed.index_consistent());
    EXPECT_EDIT(ed, ed.begin("t"), EditCode::Ok);
    EXPECT_EDIT(ed, ed.kill_face(floor), EditCode::Ok);
    EXPECT_EDIT(ed, ed.rollback(), EditCode::Ok);
    UT_CHECK(ed.index_consistent());
    EXPECT_EDIT(ed, ed.undo(), EditCode::Ok);
    UT_CHECK(contained() == cr && ed.index_consistent());
    EXPECT_EDIT(ed, ed.kill_face(floor), EditCode::FaceHasInclusions);
  }
  /* edits on the re-keyed row: its stored key must match what it added */
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  EXPECT_EDIT(ed, edge ? ed.kill_edge(e) : ed.kill_vertex(v), EditCode::Ok);
  UT_CHECK(ed.index_consistent());
  EXPECT_EDIT(ed, ed.kill_face(floor), EditCode::Ok);
  UT_CHECK(ed.index_consistent());
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_votes();
  test_random_hints<double>();
  test_random_hints<geom::Lattice>();
  test_rules<double>();
  test_rules<geom::Lattice>();
  test_lifetime<double>();
  test_lifetime<geom::Lattice>();
  for (const bool edge : {false, true}) {
    test_face_container_hint<double>(edge);
    test_face_container_hint<geom::Lattice>(edge);
  }
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
