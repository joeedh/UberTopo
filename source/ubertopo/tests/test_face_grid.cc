/* FaceGrid (plan P3b W5, C7): records, queries, large faces by triangles,
 * wires along their segment, and same_as detecting a stale grid. */

#include <cstdio>
#include <set>

#include "support/edit_fixture.h"
#include "ubertopo/topo/face_grid.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using test::Fixture;
using test::pt;

namespace {

/* one grid unit of pt<C>() in world (double) coordinates */
template <geom::CoordType C> double unit()
{
  return geom::CoordTraits<C>::to_double(pt<C>({1, 0, 0})[0]);
}

template <geom::CoordType C> void test_records_and_queries()
{
  const double u = unit<C>();
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {4, 4, 4}));
  FaceGrid g;
  g.build(fx.k);
  /* the smallest power of two >= the median face extent (4 units) */
  UT_CHECK(g.valid() && g.cell() >= 4 * u && g.cell() < 8 * u);
  uint32_t faces = 0;
  fx.k.faces.for_each_live([&](uint32_t f) {
    faces++;
    const FaceGrid::Rec *r = g.face_rec(f);
    UT_CHECK(r && r->cells.size() > 0 && r->tris.size() == 0);
  });
  UT_CHECK(faces == 6);
  /* a query around one corner meets the three faces there */
  const FaceGrid::Box b{{-0.5 * u, -0.5 * u, -0.5 * u}, {0.5 * u, 0.5 * u, 0.5 * u}};
  std::set<uint32_t> met;
  g.query(b, [&](const FaceGrid::Entry &x) {
    if (x.kind == FaceGrid::Kind::Face) {
      met.insert(x.elem);
    }
  });
  UT_CHECK(met.size() >= 3);
  /* same_as: a fresh build agrees */
  FaceGrid h;
  h.build_with_cell(fx.k, g.cell());
  UT_CHECK(g.same_as(h) && h.same_as(g));
  /* a different box with the same cell does not */
  {
    Fixture<C> other;
    UT_CHECK(other.box({0, 0, 0}, {4, 4, 6}));
    FaceGrid o;
    o.build_with_cell(other.k, g.cell());
    UT_CHECK(!g.same_as(o) && !o.same_as(g));
  }
  /* a wire moved without telling the grid makes it stale; updating the wire
   * brings it back in step */
  Editor<C> ed(fx.k, &fx.j);
  ed.options().validate = ValidateMode::None;
  VertRef a, c;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({9, 9, 9}), ident::derive(ident::tag::TestOnly, 201u), &a), EditCode::Ok);
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({12, 9, 9}), ident::derive(ident::tag::TestOnly, 202u), &c), EditCode::Ok);
  EdgeRef w;
  EXPECT_EDIT(ed, ed.make_edge(a, c, ident::kNoId, &w), EditCode::Ok);
  g.update_wire(fx.k, w.i);
  h.build_with_cell(fx.k, g.cell());
  UT_CHECK(g.same_as(h));
  EXPECT_EDIT(ed, ed.set_position(c, pt<C>({30, 9, 9})), EditCode::Ok);
  FaceGrid moved;
  moved.build_with_cell(fx.k, g.cell());
  UT_CHECK(!g.same_as(moved) && !moved.same_as(g));
  g.update_wire(fx.k, w.i);
  UT_CHECK(g.same_as(moved));
}

template <geom::CoordType C> void test_large_faces_and_wires()
{
  const double u = unit<C>();
  /* an 8-unit box on a 1-unit grid: each face spans 81 cells, so it is
   * entered by its triangles */
  Fixture<C> fx;
  UT_CHECK(fx.box({0, 0, 0}, {8, 8, 8}));
  FaceGrid g;
  g.build_with_cell(fx.k, u);
  uint32_t large = 0;
  fx.k.faces.for_each_live([&](uint32_t f) {
    const FaceGrid::Rec *r = g.face_rec(f);
    UT_CHECK(r != nullptr);
    if (r && r->tris.size()) {
      large++;
      UT_CHECK(r->tris.size() == r->tri_boxes.size());
    }
  });
  UT_CHECK(large == 6);
  /* a diagonal wire is entered along its segment, not its box */
  Editor<C> ed(fx.k, &fx.j);
  ed.options().validate = ValidateMode::None;
  VertRef a, b;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({20, 20, 20}), ident::derive(ident::tag::TestOnly, 101u), &a), EditCode::Ok);
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({36, 36, 36}), ident::derive(ident::tag::TestOnly, 102u), &b), EditCode::Ok);
  EdgeRef e;
  EXPECT_EDIT(ed, ed.make_edge(a, b, ident::kNoId, &e), EditCode::Ok);
  g.update_wire(fx.k, e.i);
  const FaceGrid::Rec *w = g.wire_rec(e.i);
  UT_CHECK(w && w->cells.size() > 16 && w->cells.size() < 1000); /* its box would be ~19^3 cells */
  FaceGrid fresh;
  fresh.build_with_cell(fx.k, u);
  UT_CHECK(g.same_as(fresh));
  EXPECT_EDIT(ed, ed.kill_edge(e), EditCode::Ok);
  g.update_wire(fx.k, e.i);
  UT_CHECK(g.wire_rec(e.i) == nullptr);
}

} // namespace

int main()
{
  topo::register_column_types();
  test_records_and_queries<double>();
  test_records_and_queries<geom::Lattice>();
  test_large_faces_and_wires<double>();
  test_large_faces_and_wires<geom::Lattice>();
  return ut_test_end();
}
