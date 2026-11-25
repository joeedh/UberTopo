/* What decides where a cut's holes and inclusions go, and which of a face's
 * loops is the outer one (plan 2026-11-24-connect-loops). */

#include "litestl/util/alloc.h"
#include "support/cut_fixture.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
using test::contained_in;
using test::Fixture;
using test::I3;
using test::Loops;
using test::pid;
using test::pt;
using test::sheet;

namespace {

/* ---- W0: wires and the cut's direction ---- */

template <geom::CoordType C> void test_wires()
{
  const Loops plate = {{{0, 0, 0}, {20, 0, 0}, {40, 0, 0}, {40, 40, 0}, {20, 40, 0}, {0, 40, 0}},
                       {{2, 30, 0}, {2, 34, 0}, {6, 34, 0}, {6, 30, 0}},
                       {{30, 20, 0}, {30, 24, 0}, {34, 24, 0}, {34, 20, 0}}};
  struct Case {
    I3 p, q;
    EditCode want;
    bool on_right; /* x > 20 */
  };
  const EditCode ok = EditCode::Ok, no = EditCode::InclusionOnChord;
  const std::vector<Case> cases = {
      {{40, 0, 0}, {30, 10, 0}, ok, true},   /* from an outer corner */
      {{0, 0, 0}, {10, 10, 0}, ok, false},   /* on the other side */
      {{20, 0, 0}, {30, 10, 0}, ok, true},   /* from an end of the cut */
      {{20, 40, 0}, {10, 30, 0}, ok, false}, /* from the other one */
      {{40, 40, 0}, {34, 24, 0}, ok, true},  /* outer corner to a hole */
      {{0, 40, 0}, {2, 34, 0}, ok, false},   /* the same, on the other side */
      {{30, 20, 0}, {25, 15, 0}, ok, true},  /* from a hole */
      {{6, 30, 0}, {12, 20, 0}, ok, false},  /* from a hole, on the other side */
      {{20, 0, 0}, {30, 20, 0}, ok, true},   /* end of the cut to a hole */
      {{24, 5, 0}, {28, 5, 0}, ok, true},    /* free at both ends */
      {{16, 5, 0}, {12, 5, 0}, ok, false},   /* the same, on the other side */
      {{16, 5, 0}, {24, 5, 0}, no, false},   /* across */
      {{20, 5, 0}, {24, 5, 0}, no, false},   /* from a point on the cut */
      {{6, 30, 0}, {30, 20, 0}, no, false},  /* hole to hole, across */
      {{0, 0, 0}, {24, 5, 0}, no, false},    /* corner to a point, across */
      {{0, 0, 0}, {40, 40, 0}, no, false},   /* corner to corner, across */
  };
  for (const Case &c : cases) {
    for (const bool reversed : {false, true}) {
      Fixture<C> fx;
      UT_CHECK(sheet<C>(fx, plate));
      Editor<C> ed(fx.k, &fx.j);
      const FaceRef f = fx.face_with({{0, 0, 0}, {40, 0, 0}});
      EdgeRef w;
      VertRef v[2];
      const I3 at[2] = {c.p, c.q};
      ed.options().validate = ValidateMode::OnCommit;
      ed.begin("wire");
      for (int i = 0; i < 2; i++) {
        v[i] = fx.vert_at(at[i]);
        if (!v[i]) {
          EXPECT_EDIT(ed, ed.make_vertex(pt<C>(at[i]), pid(700 + i), &v[i]), EditCode::Ok);
          EXPECT_EDIT(ed, ed.embed_vertex(v[i], EmbedTarget::face(f)), EditCode::Ok);
        }
      }
      EXPECT_EDIT(ed, ed.make_edge(v[0], v[1], ident::kNoId, &w, EmbedTarget::face(f)), EditCode::Ok);
      UT_CHECK(ed.commit() == EditCode::Ok && fx.valid());
      ed.options().validate = ValidateMode::Full;
      const VertRef a = fx.vert_at({20, 0, 0}), b = fx.vert_at({20, 40, 0});
      const PersistentId h = test::h(fx.k);
      EXPECT_EDIT(ed, reversed ? ed.split_face(f, b, a) : ed.split_face(f, a, b), c.want);
      if (c.want == EditCode::Ok) {
        const FaceRef side = c.on_right ? fx.face_with({{40, 0, 0}, {40, 40, 0}}) : fx.face_with({{0, 0, 0}, {0, 40, 0}});
        UT_CHECK(fx.valid() && side && contained_in(fx.k, w, side));
        UT_CHECK(ed.undo() == EditCode::Ok && test::h(fx.k) == h);
        UT_CHECK(ed.redo() == EditCode::Ok && contained_in(fx.k, w, side));
      }
      else {
        UT_CHECK(test::h(fx.k) == h);
      }
    }
  }
}

/* a wire between the cut's two ends, beside a bent cut: its middle decides */
template <geom::CoordType C> void test_wire_between_ends()
{
  for (const int64_t bend : {10, 30}) {
    for (const bool reversed : {false, true}) {
      Fixture<C> fx;
      UT_CHECK(sheet<C>(fx, {{{0, 0, 0}, {20, 0, 0}, {40, 0, 0}, {40, 41, 0}, {20, 41, 0}, {0, 41, 0}}}));
      Editor<C> ed(fx.k, &fx.j);
      const FaceRef f = fx.face_with({{0, 0, 0}, {40, 0, 0}});
      const VertRef a = fx.vert_at({20, 0, 0}), b = fx.vert_at({20, 41, 0});
      VertRef m;
      EdgeRef w, e0, e1;
      ed.options().validate = ValidateMode::OnCommit;
      ed.begin("wires");
      EXPECT_EDIT(ed, ed.make_vertex(pt<C>({bend, 20, 0}), pid(700), &m), EditCode::Ok);
      EXPECT_EDIT(ed, ed.embed_vertex(m, EmbedTarget::face(f)), EditCode::Ok);
      EXPECT_EDIT(ed, ed.make_edge(a, b, ident::kNoId, &w, EmbedTarget::face(f)), EditCode::Ok);
      EXPECT_EDIT(ed, ed.make_edge(a, m, ident::kNoId, &e0, EmbedTarget::face(f)), EditCode::Ok);
      EXPECT_EDIT(ed, ed.make_edge(m, b, ident::kNoId, &e1, EmbedTarget::face(f)), EditCode::Ok);
      UT_CHECK(ed.commit() == EditCode::Ok && fx.valid());
      ed.options().validate = ValidateMode::Full;
      const EdgeRef chain[2] = {reversed ? e1 : e0, reversed ? e0 : e1};
      EXPECT_EDIT(ed, ed.imprint(f, chain), EditCode::Ok);
      const FaceRef side = bend < 20 ? fx.face_with({{40, 0, 0}, {40, 41, 0}}) : fx.face_with({{0, 0, 0}, {0, 41, 0}});
      UT_CHECK(fx.valid() && side && contained_in(fx.k, w, side));
    }
  }
}

/* ---- W0: unimprint of an island whose host has a hole first ---- */

template <geom::CoordType C> void test_unimprint_hole_first()
{
  const std::vector<I3> outer = {{0, 0, 0}, {40, 0, 0}, {40, 40, 0}, {0, 40, 0}};
  const std::vector<I3> hole = {{10, 10, 0}, {10, 20, 0}, {20, 20, 0}, {20, 10, 0}};
  const std::vector<I3> fill = {{10, 10, 0}, {20, 10, 0}, {20, 20, 0}, {10, 20, 0}};
  for (const bool hole_first : {false, true}) {
    Fixture<C> fx;
    UT_CHECK(sheet<C>(fx, hole_first ? Loops{hole, outer} : Loops{outer, hole}, {fill}));
    Editor<C> ed(fx.k, &fx.j);
    const FaceRef f = fx.face_with({{0, 0, 0}});
    bool first_is_outer = false;
    fx.k.for_each_coedge(fx.k.first_loop(f), [&](CoedgeRef c) {
      first_is_outer = first_is_outer || fx.k.start_vert(c) == fx.vert_at({0, 0, 0});
    });
    UT_CHECK(first_is_outer != hole_first);
    EXPECT_EDIT(ed, ed.unimprint(fx.edge_between({10, 10, 0}, {20, 10, 0})), EditCode::Ok);
    UT_CHECK(fx.valid() && fx.k.faces.live_count() == 1);
  }
  /* two faces over one loop: it is the outer loop of both */
  {
    Fixture<C> fx;
    Builder<C> b;
    uint32_t v[4];
    const I3 at[4] = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
    for (int i = 0; i < 4; i++) {
      v[i] = b.vertex(pid(1 + i), pt<C>(at[i]));
    }
    const uint32_t up[4] = {v[0], v[1], v[2], v[3]}, down[4] = {v[3], v[2], v[1], v[0]}, size = 4;
    b.face(pid(10), up, std::span<const uint32_t>(&size, 1));
    b.face(pid(11), down, std::span<const uint32_t>(&size, 1));
    BuildReport br;
    UT_CHECK(b.build(fx.k, br));
    EditorOptions opt;
    opt.validate = ValidateMode::None;
    Editor<C> ed(fx.k, &fx.j, nullptr, opt);
    const PersistentId h = test::h(fx.k);
    EXPECT_EDIT(ed, ed.unimprint(fx.edge_between(at[0], at[1])), EditCode::NotJoinable);
    UT_CHECK(test::h(fx.k) == h);
  }
  /* remove_hole of a first loop */
  for (const bool hole_first : {false, true}) {
    Fixture<C> fx;
    UT_CHECK(sheet<C>(fx, hole_first ? Loops{hole, outer} : Loops{outer, hole}));
    Editor<C> ed(fx.k, &fx.j);
    const FaceRef f = fx.face_with({{0, 0, 0}});
    LoopRef first = fx.k.first_loop(f), second = fx.k.next_loop(first);
    const PersistentId h = test::h(fx.k);
    EXPECT_EDIT(ed, ed.remove_hole(f, hole_first ? second : first), EditCode::NotOuterLoop);
    EXPECT_EDIT(ed, ed.remove_hole(f, hole_first ? first : second), EditCode::Ok);
    UT_CHECK(fx.valid() && fx.k.next_loop(fx.k.first_loop(f)).i == NONE);
    UT_CHECK(ed.undo() == EditCode::Ok && test::h(fx.k) == h && ed.redo() == EditCode::Ok);
  }
}

template <geom::CoordType C> void run_all()
{
  test_wires<C>();
  test_wire_between_ends<C>();
  test_unimprint_hole_first<C>();
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  run_all<geom::Lattice>();
  run_all<double>();
  run_all<float>();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
