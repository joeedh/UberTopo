/* topo/: domains, allocator, FaceSide sizing, snapshots, accessors (plan P2 W4). */

#include "ubertopo/topo/complex.h"

#include "litestl/io/bytes.h"
#include "litestl/util/alloc.h"
#include "ut_test.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::ElemKind;

static void test_alloc_free()
{
  Domain<EdgeSchema> d;
  UT_CHECK(d.alloc_n(5) == 0 && d.capacity() == 5 && d.live_count() == 5);
  for (uint32_t i = 0; i < 5; i++) {
    UT_CHECK(d.live(i) && d.gen(i) == 0);
    d.get<".e.vs">().set(i, U32x2{i, i + 1});
    d.get<".id">().set(i, ident::derive(ident::tag::TestOnly, i));
  }
  const auto h2 = d.handle(EdgeRef(2));
  const auto h3 = d.handle(EdgeRef(3));
  UT_CHECK(d.resolve(h2) && d.resolve(h3));

  d.free(2);
  d.free(3);
  UT_CHECK(!d.live(2) && !d.live(3) && d.live_count() == 3 && d.free_count() == 2);
  UT_CHECK(d.gen(2) == 1 && d.gen(3) == 1);
  /* every other column is reset to its default */
  UT_CHECK(d.get<".e.vs">()[2] == kNone2 && d.get<".id">()[2] == ident::kNoId);
  UT_CHECK(d.get<".e.vs">()[4] == (U32x2{4, 5}));
  UT_CHECK(!d.resolve(h2) && !d.resolve(h3));

  /* lowest free slot first (P3 C15); generations never go back */
  UT_CHECK(d.peek_alloc() == 2 && d.alloc() == 2 && d.alloc() == 3 && d.peek_alloc() == 5 && d.alloc() == 5);
  UT_CHECK(d.gen(3) == 1 && d.gen(2) == 1 && d.gen(5) == 0);
  UT_CHECK(!d.resolve(h3)); /* stale handle does not match the reused slot */
  d.free(3);
  UT_CHECK(d.gen(3) == 2 && d.alloc() == 3 && d.gen(3) == 2);

  /* iteration over live slots */
  d.free(1);
  uint32_t seen = 0, sum = 0;
  d.for_each_live([&](uint32_t i) {
    seen++;
    sum += i;
  });
  UT_CHECK(seen == d.live_count() && seen == 5 && sum == 0 + 2 + 3 + 4 + 5);

  /* rebuild_allocator: lowest free slot first */
  d.free(4);
  d.rebuild_allocator();
  UT_CHECK(d.live_count() == 4 && d.free_count() == 2 && d.alloc() == 1 && d.alloc() == 4);

  /* the order is a function of the free set: free in any order, alloc ascending */
  for (uint32_t i : {5u, 1u, 3u}) {
    d.free(i);
  }
  UT_CHECK(d.alloc() == 1 && d.alloc() == 3 && d.alloc() == 5 && d.alloc() == 6);

  /* undo hooks reverse the bookkeeping */
  d.free(2);
  const uint32_t cnt = d.live_count();
  const uint32_t i = d.alloc();
  d.undo_alloc(i, true);
  UT_CHECK(i == 2 && d.peek_alloc() == 2 && d.live_count() == cnt);
  d.free(0);
  d.undo_free(0);
  UT_CHECK(d.peek_alloc() == 2 && d.live_count() == cnt && d.free_count() == 1);
}

static void test_snapshot()
{
  Domain<LoopSchema> a;
  a.alloc_n(10);
  a.get<".l.size">().set(4, 7u);
  Domain<LoopSchema> b = a;
  a.get<".l.size">().set(4, 9u);
  a.free(5);
  UT_CHECK(b.get<".l.size">()[4] == 7u && b.live(5) && b.live_count() == 10);
  UT_CHECK(a.get<".l.size">()[4] == 9u && !a.live(5));
  UT_CHECK(a.group().version() != b.group().version());
}

static void test_face_sides()
{
  FaceDomain f;
  f.alloc_n(3);
  UT_CHECK(f.sides().size() == 6);
  f.sides().get<".fs.shell_next">().set(face_side_index(FaceRef(1), BACK), 42u);
  f.sides().get<".fs.hint">().set(face_side_index(FaceRef(1), FRONT), ident::derive(ident::tag::TestOnly, 1));
  UT_CHECK(f.alloc() == 3 && f.sides().size() == 8);
  f.free(1);
  UT_CHECK(f.sides().get<".fs.shell_next">()[3] == NONE && f.sides().get<".fs.hint">()[2] == ident::kNoId);
  UT_CHECK(f.alloc() == 1 && f.sides().size() == 8 && f.capacity() == 4);
}

/* One triangle, built by hand: v0 v1 v2, edges e0=(0,1) e1=(1,2) e2=(0,2),
 * loop v0->v1->v2 (c0 uses e0 forward, c1 e1 forward, c2 e2 backward). */
template <geom::CoordType C> static void build_triangle(Complex<C> &k)
{
  using P = geom::Point<C>;
  using T = geom::CoordTraits<C>;
  k.verts.alloc_n(3);
  k.verts.template get<"pos">().set(1, P(T::from_int(1), T::from_int(0), T::from_int(0)));
  k.verts.template get<"pos">().set(2, P(T::from_int(0), T::from_int(1), T::from_int(0)));
  k.edges.alloc_n(3);
  const U32x2 vs[3] = {{0, 1}, {1, 2}, {0, 2}};
  for (uint32_t e = 0; e < 3; e++) {
    k.edges.template get<".e.vs">().set(e, vs[e]);
    k.edges.template get<".e.c">().set(e, e);
  }
  /* disk cycles: v0: e0 <-> e2, v1: e0 <-> e1, v2: e1 <-> e2 (each of size 2) */
  k.edges.template get<".e.disk">().set(0, U32x4{disk_pack(2, 0), disk_pack(2, 0), disk_pack(1, 0), disk_pack(1, 0)});
  k.edges.template get<".e.disk">().set(1, U32x4{disk_pack(0, 1), disk_pack(0, 1), disk_pack(2, 1), disk_pack(2, 1)});
  k.edges.template get<".e.disk">().set(2, U32x4{disk_pack(0, 0), disk_pack(0, 0), disk_pack(1, 1), disk_pack(1, 1)});
  k.verts.template get<".v.e">().set(0, 0u);
  k.verts.template get<".v.e">().set(1, 1u);
  k.verts.template get<".v.e">().set(2, 2u);
  k.coedges.alloc_n(3);
  const uint32_t ce[3] = {0, 1, 2}, cv[3] = {0, 1, 2};
  const uint8_t sense[3] = {1, 1, 0};
  for (uint32_t c = 0; c < 3; c++) {
    k.coedges.template get<".c.e">().set(c, ce[c]);
    k.coedges.template get<".c.v">().set(c, cv[c]);
    k.coedges.template get<".c.l">().set(c, 0u);
    k.coedges.template get<".c.next">().set(c, (c + 1) % 3);
    k.coedges.template get<".c.prev">().set(c, (c + 2) % 3);
    k.coedges.template get<".c.radial_next">().set(c, c);
    k.coedges.template get<".c.radial_prev">().set(c, c);
    k.coedges.template get<".c.flags">().set(c, sense[c]);
  }
  k.loops.alloc_n(1);
  k.loops.template get<".l.c">().set(0, 0u);
  k.loops.template get<".l.f">().set(0, 0u);
  k.loops.template get<".l.size">().set(0, 3u);
  k.faces.alloc_n(1);
  k.faces.template get<".f.l">().set(0, 0u);
}

template <geom::CoordType C> static void test_accessors()
{
  Complex<C> k;
  build_triangle(k);
  const LoopRef l(0);
  uint32_t n = 0;
  VertRef prev_end;
  bool chained = true;
  UT_CHECK(k.for_each_coedge(l, [&](CoedgeRef c) {
    if (n > 0) {
      chained = chained && k.start_vert(c) == prev_end;
    }
    prev_end = k.end_vert(c);
    n++;
  }));
  UT_CHECK(n == 3 && chained && prev_end == VertRef(0));
  UT_CHECK(k.face_of(CoedgeRef(1)) == FaceRef(0) && k.next(CoedgeRef(2)) == CoedgeRef(0));
  UT_CHECK(k.side_toward_next(CoedgeRef(0)) == FRONT && k.side_toward_next(CoedgeRef(2)) == BACK);
  uint32_t loops = 0;
  UT_CHECK(k.for_each_loop(FaceRef(0), [&](LoopRef) { loops++; }) && loops == 1);
  uint32_t radial = 0;
  UT_CHECK(k.for_each_radial(EdgeRef(1), [&](CoedgeRef) { radial++; }) && radial == 1);
  for (uint32_t v = 0; v < 3; v++) {
    uint32_t deg = 0;
    UT_CHECK(k.for_each_edge_at(VertRef(v), [&](EdgeRef) { deg++; }) && deg == 2);
  }
  /* a corrupted cycle is detected, not looped over forever */
  k.coedges.template get<".c.next">().set(2, 1u);
  UT_CHECK(!k.for_each_coedge(l, [](CoedgeRef) {}));

  /* snapshot of a whole complex */
  Complex<C> snap = k;
  k.coedges.template get<".c.next">().set(2, 0u);
  UT_CHECK(snap.coedges.template get<".c.next">()[2] == 1u);
}

static void test_group_round_trip()
{
  register_column_types();
  Complex<geom::Lattice> k;
  build_triangle(k);
  k.verts.get<"pos">().set(2, geom::Point<geom::Lattice>(geom::Lattice(3), geom::Lattice(-4), geom::Lattice(5)));
  ident::ContainerRef cr;
  UT_CHECK(ident::ContainerRef::face(FaceRef(0), cr));
  k.verts.get<".v.container">().set(1, cr);
  /* every domain group round-trips with all link columns (they are Topo, not Derived) */
  auto round = [](const attr::AttrGroup &src, attr::AttrGroup &dst) {
    litestl::io::ByteWriter w;
    src.write(w);
    litestl::io::ByteReader r(w.data(), w.size());
    const char *err = nullptr;
    const bool ok = dst.read(r, {}, &err);
    if (!ok) {
      fprintf(stderr, "read failed: %s\n", err ? err : "?");
    }
    return ok && dst.content_hash() == src.content_hash();
  };
  Complex<geom::Lattice> back;
  UT_CHECK(round(k.verts.group(), back.verts.group()));
  UT_CHECK(round(k.edges.group(), back.edges.group()));
  UT_CHECK(round(k.coedges.group(), back.coedges.group()));
  UT_CHECK(round(k.loops.group(), back.loops.group()));
  UT_CHECK(round(k.faces.group(), back.faces.group()));
  back.verts.rebuild_allocator();
  UT_CHECK(back.verts.live_count() == 3 && back.coedges.get<".c.radial_next">()[1] == 1u);
  UT_CHECK(back.vert_container(VertRef(1)) == cr && back.pos(VertRef(2))[1].raw == -4);
  UT_CHECK(back.edges.get<".e.disk">()[2][3] == disk_pack(1, 1));
}

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_alloc_free();
  test_snapshot();
  test_face_sides();
  test_accessors<geom::Lattice>();
  test_accessors<double>();
  test_group_round_trip();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
