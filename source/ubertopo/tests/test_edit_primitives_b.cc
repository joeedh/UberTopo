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

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_compact<double>();
  test_compact<geom::Lattice>();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
