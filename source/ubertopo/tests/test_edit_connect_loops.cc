/* connect_loops and disconnect_loops (plan 2026-11-24-connect-loops): cutting
 * faces through their holes, and taking the cut back. */

#include <map>

#include "litestl/util/alloc.h"
#include "support/cut_fixture.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
using test::as_loops;
using test::contained_in;
using test::cut_cases;
using test::CutCase;
using test::Fixture;
using test::I3;
using test::Loop;
using test::loops_of;
using test::Loops;
using test::maintained;
using test::pid;
using test::pt;
using test::sheet;
using test::sides_of;
using test::verts_at;

namespace {

template <geom::CoordType C> void test_connect()
{
  for (const CutCase &c : cut_cases()) {
    for (const ValidateMode mode : {ValidateMode::Full, ValidateMode::Local, ValidateMode::OnCommit, ValidateMode::None}) {
      for (const bool flip : {false, true}) {
        Fixture<C> fx;
        UT_CHECK(sheet<C>(fx, c.face, c.fill));
        EditorOptions opt;
        opt.validate = mode;
        opt.verify_redo = true;
        Editor<C> ed(fx.k, &fx.j, nullptr, opt);
        const FaceRef f = fx.face_with({c.path.front(), c.path.back()});
        if (flip) {
          EXPECT_EDIT(ed, ed.flip_face(f), EditCode::Ok);
        }
        EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
        /* after a flip the loops run the other way, so left and right swap */
        Loops want_f = flip ? c.right : c.left, want_o = flip ? c.left : c.right;
        if (flip) {
          for (Loops *ls : {&want_f, &want_o}) {
            for (Loop &l : *ls) {
              std::reverse(l.begin(), l.end());
            }
          }
        }
        const auto parent = loops_of(fx.k, f);
        const auto regions = sides_of(fx.k, f);
        const PersistentId h0 = test::h(fx.k), fid = fx.k.face_id(f);
        const uint32_t edges = fx.k.edges.live_count(), loops = fx.k.loops.live_count();
        const auto path = verts_at(fx, c.path);
        FaceRef other;
        EdgeRef chord;
        EXPECT_EDIT(ed, ed.connect_loops(f, path, &other, &chord), EditCode::Ok);
        const bool good = fx.valid() && loops_of(fx.k, f) == as_loops(fx, want_f) && other &&
                          loops_of(fx.k, other) == as_loops(fx, want_o) && maintained(ed);
        if (!good) {
          fprintf(stderr, "connect_loops: %s, flip %d\n", c.what, int(flip));
        }
        UT_CHECK(good);
        const uint32_t chords = uint32_t(c.path.size() / 2);
        UT_CHECK(fx.k.edges.live_count() == edges + chords);
        UT_CHECK(fx.k.loops.live_count() == loops + 1 - (chords - 1));
        UT_CHECK(chord == fx.edge_between(c.path[0], c.path[1]) && fx.k.face_id(f) != fx.k.face_id(other));
        /* IDs and lineage */
        UT_CHECK(fx.k.face_id(f) == ident::derive(ident::tag::OpDerived, fid, std::string_view("connect_loops"), 0u));
        UT_CHECK(fx.k.face_id(other) == ident::derive(ident::tag::OpDerived, fid, std::string_view("connect_loops"), 1u));
        UT_CHECK(fx.j.forward(fid).size() == 2);
        const PersistentId h1 = test::h(fx.k);
        UT_CHECK(ed.undo() == EditCode::Ok && test::h(fx.k) == h0 && maintained(ed));
        UT_CHECK(ed.redo() == EditCode::Ok && test::h(fx.k) == h1 && maintained(ed));
        EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
        UT_CHECK(fx.valid() && sides_of(fx.k, f) == regions && sides_of(fx.k, other) == regions);
        /* and back, by any of the chords: the same loops, the outer one first */
        PersistentId joined{};
        for (uint32_t i = 0; i < chords; i++) {
          const EdgeRef e = fx.edge_between(c.path[2 * i], c.path[2 * i + 1]);
          EXPECT_EDIT(ed, ed.join_faces(e), EditCode::NotJoinable);
          EXPECT_EDIT(ed, ed.unimprint(e), EditCode::NotJoinable);
          const PersistentId h2 = test::h(fx.k);
          FaceRef s;
          const PersistentId ida = fx.k.face_id(f), idb = fx.k.face_id(other);
          EXPECT_EDIT(ed, ed.disconnect_loops(e, &s), EditCode::Ok);
          UT_CHECK(fx.k.face_id(s) == (ida < idb ? ida : idb) && fx.j.forward(ida < idb ? idb : ida).size() == 1);
          UT_CHECK(fx.valid() && loops_of(fx.k, s) == parent && maintained(ed));
          UT_CHECK(fx.k.edges.live_count() == edges && fx.k.loops.live_count() == loops);
          bool outer_first = false;
          fx.k.for_each_coedge(fx.k.first_loop(s), [&](CoedgeRef x) {
            outer_first = outer_first || fx.k.start_vert(x) == path.front();
          });
          UT_CHECK(outer_first);
          const PersistentId h3 = test::h(fx.k);
          UT_CHECK(i == 0 || h3 == joined);
          joined = h3;
          UT_CHECK(ed.undo() == EditCode::Ok && test::h(fx.k) == h2);
          UT_CHECK(ed.redo() == EditCode::Ok && test::h(fx.k) == h3);
          UT_CHECK(ed.undo() == EditCode::Ok && test::h(fx.k) == h2);
        }
        EXPECT_EDIT(ed, ed.disconnect_loops(chord), EditCode::Ok);
        EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
        UT_CHECK(fx.valid());
      }
    }
  }
}

/* wires and points beside a cut, from its loops too */
template <geom::CoordType C> void test_connect_inclusions()
{
  const CutCase c = cut_cases()[0];
  struct Case {
    I3 p, q;
    EditCode want;
    bool above;
  };
  const EditCode ok = EditCode::Ok, no = EditCode::InclusionOnChord;
  const std::vector<Case> cases = {
      {{0, 40, 0}, {10, 30, 0}, ok, true},   /* outer corner to the cut hole */
      {{40, 0, 0}, {30, 10, 0}, ok, false},  /* the same, below */
      {{10, 30, 0}, {10, 35, 0}, ok, true},  /* from the hole's upper arc */
      {{10, 10, 0}, {10, 5, 0}, ok, false},  /* from its lower arc */
      {{0, 20, 0}, {10, 30, 0}, ok, true},   /* from an end of the cut */
      {{0, 20, 0}, {10, 10, 0}, ok, false},  /* the same, below */
      {{10, 20, 0}, {5, 25, 0}, ok, true},   /* from where it enters the hole */
      {{30, 20, 0}, {35, 15, 0}, ok, false}, /* from where it leaves */
      {{10, 20, 0}, {5, 15, 0}, ok, false},  /* the same two, on the other sides */
      {{30, 20, 0}, {35, 25, 0}, ok, true},
      {{0, 0, 0}, {10, 30, 0}, no, false},   /* across the first chord */
      {{0, 40, 0}, {10, 10, 0}, no, false},  /* the same */
      {{3, 18, 0}, {3, 22, 0}, no, false},   /* across, free at both ends */
      {{5, 20, 0}, {5, 25, 0}, no, false},   /* from a point on the cut */
  };
  for (const Case &w : cases) {
    for (const bool reversed : {false, true}) {
      Fixture<C> fx;
      UT_CHECK(sheet<C>(fx, c.face));
      Editor<C> ed(fx.k, &fx.j);
      const FaceRef f = fx.face_with({{0, 0, 0}});
      EdgeRef e;
      VertRef v[2];
      const I3 at[2] = {w.p, w.q};
      ed.options().validate = ValidateMode::OnCommit;
      ed.begin("wire");
      for (int i = 0; i < 2; i++) {
        v[i] = fx.vert_at(at[i]);
        if (!v[i]) {
          EXPECT_EDIT(ed, ed.make_vertex(pt<C>(at[i]), pid(700 + i), &v[i]), EditCode::Ok);
          EXPECT_EDIT(ed, ed.embed_vertex(v[i], EmbedTarget::face(f)), EditCode::Ok);
        }
      }
      EXPECT_EDIT(ed, ed.make_edge(v[0], v[1], ident::kNoId, &e, EmbedTarget::face(f)), EditCode::Ok);
      UT_CHECK(ed.commit() == EditCode::Ok && fx.valid());
      ed.options().validate = ValidateMode::Full;
      Loop p = c.path;
      if (reversed) {
        std::reverse(p.begin(), p.end());
      }
      const PersistentId h = test::h(fx.k);
      EXPECT_EDIT(ed, ed.connect_loops(f, verts_at(fx, p)), w.want);
      if (w.want == EditCode::Ok) {
        const FaceRef side = w.above ? fx.face_with({{0, 40, 0}}) : fx.face_with({{0, 0, 0}});
        UT_CHECK(fx.valid() && side && contained_in(fx.k, e, side) && maintained(ed));
        FaceRef s;
        EXPECT_EDIT(ed, ed.disconnect_loops(fx.edge_between(p[0], p[1]), &s), EditCode::Ok);
        UT_CHECK(fx.valid() && contained_in(fx.k, e, s) && maintained(ed));
      }
      else {
        UT_CHECK(test::h(fx.k) == h);
      }
    }
  }
  /* a point on a chord */
  Fixture<C> fx;
  UT_CHECK(sheet<C>(fx, c.face));
  Editor<C> ed(fx.k, &fx.j);
  const FaceRef f = fx.face_with({{0, 0, 0}});
  VertRef v;
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({5, 20, 0}), pid(700), &v), EditCode::Ok);
  EXPECT_EDIT(ed, ed.embed_vertex(v, EmbedTarget::face(f)), EditCode::Ok);
  EXPECT_EDIT(ed, ed.connect_loops(f, verts_at(fx, c.path)), EditCode::InclusionOnChord);
}

template <geom::CoordType C> void test_connect_refusals(ValidateMode mode)
{
  const CutCase two = cut_cases()[5];
  Fixture<C> fx;
  UT_CHECK(sheet<C>(fx, two.face));
  EditorOptions opt;
  opt.validate = mode;
  Editor<C> ed(fx.k, &fx.j, nullptr, opt);
  const FaceRef f = fx.face_with({{0, 0, 0}});
  const PersistentId h = test::h(fx.k);
  const auto refuse = [&](const Loop &p, EditCode want) {
    EXPECT_EDIT(ed, ed.connect_loops(f, verts_at(fx, p)), want);
    UT_CHECK(test::h(fx.k) == h && maintained(ed));
  };
  EXPECT_EDIT(ed, ed.connect_loops(FaceRef(999), verts_at(fx, two.path)), EditCode::DeadElement);
  refuse({}, EditCode::BadArgument);
  refuse({{0, 20, 0}, {80, 20, 0}}, EditCode::BadArgument);
  refuse({{0, 20, 0}, {10, 24, 0}, {20, 16, 0}}, EditCode::BadArgument);
  refuse({{0, 20, 0}, {10, 24, 0}, {20, 16, 0}, {40, 24, 0}, {80, 20, 0}}, EditCode::BadArgument);
  refuse({{0, 20, 0}, {10, 24, 0}, {10, 16, 0}, {0, 20, 0}}, EditCode::BadArgument);
  {
    auto p = verts_at(fx, two.path);
    p[2] = VertRef(9999);
    EXPECT_EDIT(ed, ed.connect_loops(f, p), EditCode::BadArgument);
  }
  /* a vertex that isn't on the face */
  VertRef loose;
  EXPECT_EDIT(ed, ed.begin("t"), EditCode::Ok);
  EXPECT_EDIT(ed, ed.make_vertex(pt<C>({0, 0, 50}), pid(700), &loose), EditCode::Ok);
  for (size_t i = 0; i < two.path.size(); i++) {
    auto p = verts_at(fx, two.path);
    p[i] = loose;
    EXPECT_EDIT(ed, ed.connect_loops(f, p), EditCode::NotInLoop);
    UT_CHECK(ed.report().index == loose.i);
  }
  EXPECT_EDIT(ed, ed.rollback(), EditCode::Ok);
  /* ends on holes, middles on the outer loop */
  refuse({{10, 16, 0}, {10, 24, 0}, {20, 16, 0}, {80, 20, 0}}, EditCode::NotOuterLoop);
  refuse({{0, 20, 0}, {10, 24, 0}, {20, 16, 0}, {40, 16, 0}}, EditCode::NotOuterLoop);
  refuse({{0, 20, 0}, {0, 40, 0}, {20, 16, 0}, {80, 20, 0}}, EditCode::NotOuterLoop);
  refuse({{0, 20, 0}, {10, 24, 0}, {80, 40, 0}, {80, 20, 0}}, EditCode::NotOuterLoop);
  /* in on one hole and out of another; in and out at one vertex; a hole twice */
  refuse({{0, 20, 0}, {10, 24, 0}, {40, 24, 0}, {80, 20, 0}}, EditCode::ChainNotSimple);
  refuse({{0, 20, 0}, {10, 24, 0}, {10, 24, 0}, {80, 20, 0}}, EditCode::ChainNotSimple);
  refuse({{0, 20, 0}, {10, 24, 0}, {20, 24, 0}, {20, 16, 0}, {10, 16, 0}, {0, 0, 0}}, EditCode::ChainNotSimple);
  /* chords that cross a hole, the outer loop or each other */
  refuse({{0, 20, 0}, {20, 24, 0}, {20, 16, 0}, {80, 20, 0}}, EditCode::Invalid);
  refuse({{0, 20, 0}, {40, 24, 0}, {50, 16, 0}, {80, 20, 0}}, EditCode::Invalid);
  refuse({{0, 0, 0}, {10, 24, 0}, {10, 16, 0}, {0, 40, 0}}, EditCode::Invalid);
  {
    Fixture<C> cx;
    const CutCase u = cut_cases()[6];
    UT_CHECK(sheet<C>(cx, u.face));
    Editor<C> ec(cx.k, &cx.j, nullptr, opt);
    const PersistentId hc = test::h(cx.k);
    const FaceRef g = cx.face_with({{0, 0, 0}});
    EXPECT_EDIT(ec, ec.connect_loops(g, verts_at(cx, {{20, 50, 0}, {26, 14, 0}, {34, 14, 0}, {40, 20, 0}})), EditCode::Invalid);
    UT_CHECK(test::h(cx.k) == hc);
    /* a hole cut off at the side: no refusal */
    Fixture<C> fd;
    UT_CHECK(sheet<C>(fd, {{{0, 0, 0}, {10, 0, 0}, {20, 0, 0}, {20, 20, 0}, {0, 20, 0}},
                           {{4, 10, 0}, {6, 12, 0}, {8, 10, 0}}}));
    Editor<C> e2(fd.k, &fd.j);
    EXPECT_EDIT(e2, e2.connect_loops(fd.face_with({{0, 0, 0}}), verts_at(fd, {{0, 0, 0}, {4, 10, 0}, {8, 10, 0}, {10, 0, 0}})),
                EditCode::Ok);
  }
  /* an edge elsewhere with the ID of the second chord */
  {
    Fixture<C> fe;
    UT_CHECK(sheet<C>(fe, two.face));
    Editor<C> ee(fe.k, &fe.j);
    const PersistentId id =
        ident::edge_id(fe.k.vert_id(fe.vert_at({50, 16, 0})), fe.k.vert_id(fe.vert_at({80, 20, 0})));
    VertRef v[2];
    EXPECT_EDIT(ee, ee.make_vertex(pt<C>({0, 0, 50}), pid(700), &v[0]), EditCode::Ok);
    EXPECT_EDIT(ee, ee.make_vertex(pt<C>({9, 0, 50}), pid(701), &v[1]), EditCode::Ok);
    EXPECT_EDIT(ee, ee.make_edge(v[0], v[1], id), EditCode::Ok);
    EXPECT_EDIT(ee, ee.connect_loops(fe.face_with({{0, 0, 0}}), verts_at(fe, two.path)), EditCode::DuplicateId);
  }
  /* an edge along a chord, and another hole touched by one */
  {
    Fixture<C> fe;
    UT_CHECK(sheet<C>(fe, two.face));
    Editor<C> ee(fe.k, &fe.j);
    const FaceRef g = fe.face_with({{0, 0, 0}});
    EdgeRef w;
    EXPECT_EDIT(ee, ee.make_edge(fe.vert_at({50, 16, 0}), fe.vert_at({80, 20, 0}), ident::kNoId, &w, EmbedTarget::face(g)),
                EditCode::Ok);
    const PersistentId he = test::h(fe.k);
    EXPECT_EDIT(ee, ee.connect_loops(g, verts_at(fe, two.path)), EditCode::EdgeExists);
    UT_CHECK(test::h(fe.k) == he && ee.report().index == w.i);
  }
  {
    Fixture<C> ft;
    /* the first corner of a hole's loop on the first chord */
    UT_CHECK(sheet<C>(ft, {{{0, 0, 0}, {40, 0, 0}, {40, 20, 0}, {40, 40, 0}, {0, 40, 0}, {0, 20, 0}},
                           {{20, 10, 0}, {20, 20, 0}, {20, 30, 0}, {30, 30, 0}, {30, 20, 0}, {30, 10, 0}},
                           {{10, 20, 0}, {8, 24, 0}, {12, 24, 0}}}));
    EditorOptions none;
    none.validate = ValidateMode::None;
    Editor<C> et(ft.k, &ft.j, nullptr, none);
    const PersistentId ht = test::h(ft.k);
    EXPECT_EDIT(et, et.connect_loops(ft.face_with({{0, 0, 0}}), verts_at(ft, {{0, 20, 0}, {20, 20, 0}, {30, 20, 0}, {40, 20, 0}})),
                EditCode::HoleOnChord);
    UT_CHECK(test::h(ft.k) == ht);
  }
}

/* what disconnect_loops refuses, and what else it joins */
template <geom::CoordType C> void test_disconnect()
{
  const CutCase ring = cut_cases()[0];
  {
    Fixture<C> fx;
    UT_CHECK(sheet<C>(fx, ring.face));
    Editor<C> ed(fx.k, &fx.j);
    const FaceRef f = fx.face_with({{0, 0, 0}});
    FaceRef other;
    EdgeRef chord;
    EXPECT_EDIT(ed, ed.disconnect_loops(EdgeRef(9999)), EditCode::DeadElement);
    EXPECT_EDIT(ed, ed.disconnect_loops(fx.edge_between({0, 0, 0}, {40, 0, 0})), EditCode::NotJoinable);
    EXPECT_EDIT(ed, ed.connect_loops(f, verts_at(fx, ring.path), &other, &chord), EditCode::Ok);
    const PersistentId h = test::h(fx.k);
    /* a chord in two pieces can't be taken back; joined again, it can */
    VertRef mid;
    EXPECT_EDIT(ed, ed.split_edge(chord, pt<C>({5, 20, 0}), &mid), EditCode::Ok);
    const PersistentId hs = test::h(fx.k);
    EXPECT_EDIT(ed, ed.disconnect_loops(fx.edge_between({30, 20, 0}, {40, 20, 0})), EditCode::NotJoinable);
    EXPECT_EDIT(ed, ed.disconnect_loops(fx.edge_between({0, 20, 0}, {5, 20, 0})), EditCode::NotJoinable);
    UT_CHECK(test::h(fx.k) == hs);
    EXPECT_EDIT(ed, ed.join_edge(mid), EditCode::Ok);
    EXPECT_EDIT(ed, ed.disconnect_loops(fx.edge_between({30, 20, 0}, {40, 20, 0})), EditCode::Ok);
    UT_CHECK(fx.valid());
    while (fx.j.undo_count() > 0 && !(test::h(fx.k) == h)) {
      UT_CHECK(ed.undo() == EditCode::Ok);
    }
    /* different hints, different fronts, a third face on a chord */
    for (uint32_t side = 0; side < 2; side++) {
      EXPECT_EDIT(ed, ed.set_side_hint(other, side, pid(55)), EditCode::Ok);
      EXPECT_EDIT(ed, ed.disconnect_loops(chord), EditCode::Incompatible);
      UT_CHECK(ed.undo() == EditCode::Ok);
    }
    EXPECT_EDIT(ed, ed.flip_face(other), EditCode::Ok);
    EXPECT_EDIT(ed, ed.disconnect_loops(chord), EditCode::Incompatible);
    UT_CHECK(ed.undo() == EditCode::Ok);
    for (const bool on_given : {true, false}) {
      const Loop e = on_given ? Loop{{0, 20, 0}, {10, 20, 0}} : Loop{{30, 20, 0}, {40, 20, 0}};
      ed.begin("fin");
      VertRef tip;
      EXPECT_EDIT(ed, ed.make_vertex(pt<C>({20, 20, 9}), pid(701), &tip), EditCode::Ok);
      EXPECT_EDIT(ed, ed.make_edge(fx.vert_at(e[0]), tip), EditCode::Ok);
      EXPECT_EDIT(ed, ed.make_edge(tip, fx.vert_at(e[1])), EditCode::Ok);
      const VertRef loop[3] = {fx.vert_at(e[0]), fx.vert_at(e[1]), tip};
      const uint32_t size = 3;
      EXPECT_EDIT(ed, ed.make_face(loop, std::span<const uint32_t>(&size, 1), pid(702)), EditCode::Ok);
      const PersistentId hf = test::h(fx.k);
      EXPECT_EDIT(ed, ed.disconnect_loops(chord), EditCode::NotJoinable);
      UT_CHECK(test::h(fx.k) == hf);
      ed.rollback();
    }
    UT_CHECK(test::h(fx.k) == h);
  }
  /* a strip across a hole: the hole becomes two, whichever face survives */
  bool strip_survived = false, plate_survived = false;
  for (uint64_t ids = 0; ids < 2; ids++) {
    Fixture<C> fx;
    Builder<C> b;
    const Loop outer = {{0, 0, 0}, {40, 0, 0}, {40, 40, 0}, {0, 40, 0}};
    const Loop hole = {{10, 5, 0}, {10, 15, 0}, {10, 25, 0}, {10, 35, 0}, {30, 35, 0}, {30, 25, 0}, {30, 15, 0}, {30, 5, 0}};
    const Loop strip = {{10, 15, 0}, {30, 15, 0}, {30, 25, 0}, {10, 25, 0}};
    std::map<I3, uint32_t> key;
    const auto keys = [&](const Loop &l, std::vector<uint32_t> &out) {
      for (const I3 &p : l) {
        if (!key.count(p)) {
          key[p] = b.vertex(pid(100 + key.size()), pt<C>(p));
        }
        out.push_back(key[p]);
      }
    };
    std::vector<uint32_t> plate, st;
    keys(outer, plate), keys(hole, plate), keys(strip, st);
    const uint32_t sizes[2] = {4, 8}, four = 4;
    b.face(pid(500 + ids), plate, sizes);
    b.face(pid(501 - ids), st, std::span<const uint32_t>(&four, 1));
    BuildReport br;
    RegionReport rr;
    UT_CHECK(b.build(fx.k, br) && build_regions(fx.k, rr) && fx.valid());
    Editor<C> ed(fx.k, &fx.j);
    const FaceRef pl = fx.face_with({{0, 0, 0}}), str(1 - pl.i);
    EXPECT_EDIT(ed, ed.join_faces(fx.edge_between({10, 15, 0}, {10, 25, 0})), EditCode::NotJoinable);
    const PersistentId h = test::h(fx.k);
    FaceRef s;
    EXPECT_EDIT(ed, ed.disconnect_loops(fx.edge_between({10, 15, 0}, {10, 25, 0}), &s), EditCode::Ok);
    strip_survived = strip_survived || s == str;
    plate_survived = plate_survived || s == pl;
    UT_CHECK(fx.valid() && maintained(ed) &&
             loops_of(fx.k, s) == as_loops(fx, {outer,
                                                {{10, 5, 0}, {10, 15, 0}, {30, 15, 0}, {30, 5, 0}},
                                                {{10, 25, 0}, {10, 35, 0}, {30, 35, 0}, {30, 25, 0}}}));
    bool outer_first = false;
    fx.k.for_each_coedge(fx.k.first_loop(s), [&](CoedgeRef x) {
      outer_first = outer_first || fx.k.start_vert(x) == fx.vert_at({0, 0, 0});
    });
    UT_CHECK(outer_first);
    const PersistentId h1 = test::h(fx.k);
    UT_CHECK(ed.undo() == EditCode::Ok && test::h(fx.k) == h && ed.redo() == EditCode::Ok && test::h(fx.k) == h1);
  }
  UT_CHECK(strip_survived && plate_survived);
  /* two faces that share two edges of one line, folded along it */
  {
    Fixture<C> fx;
    test::GridBuilder<C> g;
    g.face({{{0, 20, 0}, {10, 20, 0}, {10, 20, 10}, {30, 20, 10}, {30, 20, 0}, {40, 20, 0}, {40, 20, 20}, {0, 20, 20}}});
    g.face({{{0, 20, 0}, {0, 0, 0}, {40, 0, 0}, {40, 20, 0}, {30, 20, 0}, {30, 10, 0}, {10, 10, 0}, {10, 20, 0}}});
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(fx.k, br) && build_regions(fx.k, rr) && fx.valid());
    Editor<C> ed(fx.k, &fx.j);
    EXPECT_EDIT(ed, ed.disconnect_loops(fx.edge_between({0, 20, 0}, {10, 20, 0})), EditCode::NotJoinable);
  }
  /* two faces around a bow-tie hole, which meet at its waist too */
  {
    Fixture<C> fx;
    test::GridBuilder<C> g;
    g.face({{{0, 20, 0}, {5, 20, 0}, {10, 30, 0}, {20, 20, 0}, {30, 30, 0}, {35, 20, 0}, {40, 20, 0}, {40, 40, 0}, {0, 40, 0}}});
    g.face({{{0, 20, 0}, {0, 0, 0}, {40, 0, 0}, {40, 20, 0}, {35, 20, 0}, {30, 10, 0}, {20, 20, 0}, {10, 10, 0}, {5, 20, 0}}});
    BuildReport br;
    RegionReport rr;
    UT_CHECK(g.b.build(fx.k, br) && build_regions(fx.k, rr) && fx.valid());
    Editor<C> ed(fx.k, &fx.j);
    const PersistentId h = test::h(fx.k);
    EXPECT_EDIT(ed, ed.disconnect_loops(fx.edge_between({0, 20, 0}, {5, 20, 0})), EditCode::NotJoinable);
    UT_CHECK(test::h(fx.k) == h);
  }
  /* unions that aren't planar, or meet elsewhere: made unvalidated, coplanar */
  const I3 a1{0, 0, 0}, b1{10, 0, 0}, a2{20, 10, 0}, b2{20, 20, 0}, a3{10, 30, 0}, b3{0, 20, 0};
  const Loop hex = {a1, b1, a2, b2, a3, b3};
  const I3 x{50, 5, 0}, y{50, 25, 0};
  struct Bad {
    const char *what;
    Loops f1, f2;
    EditCode want;
  };
  const std::vector<Bad> bads = {
      {"planar", {hex}, {{b1, a1, b3, a3, b2, a2}}, EditCode::NotJoinable}, /* every vertex of one is on the other,
                                                                              * along edges it doesn't share too */
      {"tripod", {hex}, {{b1, a1, x, b2, a2, y, b3, a3}}, EditCode::NotJoinable},
      {"meets elsewhere", {hex}, {{b1, a1, x, a3, b2, a2, y}}, EditCode::NotJoinable},
      {"on another loop", {hex, {x, y, {60, 15, 0}}}, {{b1, a1, b2, a2, y, x}}, EditCode::NotJoinable},
      {"same direction", {hex}, {{a1, b1, x, a2, b2, y}}, EditCode::NotJoinable},
  };
  for (const Bad &bad : bads) {
    Fixture<C> fx;
    test::GridBuilder<C> g;
    g.face(bad.f1);
    g.face(bad.f2);
    BuildReport br;
    if (!g.b.build(fx.k, br)) {
      fprintf(stderr, "%s: not built\n", bad.what);
      UT_CHECK(false);
      continue;
    }
    EditorOptions none;
    none.validate = ValidateMode::None;
    Editor<C> ed(fx.k, &fx.j, nullptr, none);
    const PersistentId h = test::h(fx.k);
    EXPECT_EDIT(ed, ed.disconnect_loops(fx.edge_between(a1, b1)), bad.want);
    UT_CHECK(test::h(fx.k) == h);
  }
}

/* windows: cut through an imprinted one, taken back, and unimprinted; freed
 * loop rows reused; compact between */
template <geom::CoordType C> void test_connect_sequences()
{
  const CutCase c = cut_cases()[3];
  for (const bool compact : {false, true}) {
    Fixture<C> fx;
    UT_CHECK(sheet<C>(fx, c.face, c.fill));
    Editor<C> ed(fx.k, &fx.j);
    const PersistentId h0 = test::h(fx.k);
    const FaceRef f = fx.face_with({{0, 0, 0}});
    EXPECT_EDIT(ed, ed.connect_loops(f, verts_at(fx, c.path)), EditCode::Ok);
    /* a hole in the left child takes the freed loop's row */
    VertRef v[3];
    EdgeRef e[3];
    const FaceRef up = fx.face_with({{0, 40, 0}});
    const I3 at[3] = {{16, 34, 0}, {20, 37, 0}, {24, 34, 0}};
    ed.begin("hole");
    for (int i = 0; i < 3; i++) {
      EXPECT_EDIT(ed, ed.make_vertex(pt<C>(at[i]), pid(800 + i), &v[i]), EditCode::Ok);
      EXPECT_EDIT(ed, ed.embed_vertex(v[i], EmbedTarget::face(up)), EditCode::Ok);
    }
    for (int i = 0; i < 3; i++) {
      EXPECT_EDIT(ed, ed.make_edge(v[i], v[(i + 1) % 3], ident::kNoId, &e[i], EmbedTarget::face(up)), EditCode::Ok);
    }
    EXPECT_EDIT(ed, ed.add_hole(up, e), EditCode::Ok);
    UT_CHECK(ed.commit() == EditCode::Ok && fx.valid());
    if (compact) {
      EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
      (void)ed.compact();
    }
    FaceRef s;
    EXPECT_EDIT(ed, ed.disconnect_loops(fx.edge_between(c.path[0], c.path[1]), &s), EditCode::Ok);
    UT_CHECK(fx.valid() && maintained(ed));
    EXPECT_EDIT(ed, ed.unimprint(fx.edge_between({10, 10, 0}, {30, 10, 0})), EditCode::Ok);
    UT_CHECK(fx.valid() && fx.k.faces.live_count() == 1 && maintained(ed));
    const PersistentId h1 = test::h(fx.k);
    while (fx.j.undo_count() > 0) {
      UT_CHECK(ed.undo() == EditCode::Ok);
    }
    UT_CHECK(test::h(fx.k) == h0);
    while (fx.j.redo_count() > 0) {
      UT_CHECK(ed.redo() == EditCode::Ok);
    }
    UT_CHECK(test::h(fx.k) == h1 && maintained(ed));
  }
}

/* propagation: areas, sides, corners and loops */
template <geom::CoordType C> void test_connect_propagation()
{
  using litestl::attr::Column;
  const CutCase c = cut_cases()[0];
  Fixture<C> fx;
  UT_CHECK(sheet<C>(fx, c.face));
  auto *area = fx.k.faces.group().template ensure<Column<double>>("area_w");
  auto *paint = fx.k.faces.sides().group().template ensure<Column<int32_t>>("paint");
  auto *tag = fx.k.loops.group().template ensure<Column<int32_t>>("tag");
  auto *uv = fx.k.coedges.group().template ensure<Column<int32_t>>("uv");
  const FaceRef f = fx.face_with({{0, 0, 0}});
  area->set(f.i, 1200.0); /* 40 x 40 less 20 x 20 */
  paint->set(2 * f.i, 7), paint->set(2 * f.i + 1, 8);
  fx.k.for_each_loop(f, [&](LoopRef l) { tag->set(l.i, fx.k.start_vert(fx.k.first_coedge(l)).i + 100); });
  fx.k.coedges.for_each_live([&](uint32_t x) { uv->set(x, int32_t(fx.k.start_vert(CoedgeRef(x)).i) + 1000); });
  int32_t outer_tag = 0;
  fx.k.for_each_loop(f, [&](LoopRef l) {
    fx.k.for_each_coedge(l, [&](CoedgeRef x) {
      if (fx.k.start_vert(x) == fx.vert_at({0, 0, 0})) {
        outer_tag = (*tag)[l.i];
      }
    });
  });
  PropagationRules rules;
  rules.conserved(Grp::Faces, "area_w");
  Editor<C> ed(fx.k, &fx.j, &rules);
  FaceRef other;
  EXPECT_EDIT(ed, ed.connect_loops(f, verts_at(fx, c.path), &other), EditCode::Ok);
  UT_CHECK((*area)[f.i] == 600.0 && (*area)[other.i] == 600.0);
  for (const FaceRef x : {f, other}) {
    UT_CHECK((*paint)[2 * x.i] == 7 && (*paint)[2 * x.i + 1] == 8);
    UT_CHECK((*tag)[fx.k.first_loop(x).i] == outer_tag);
    /* every corner has the value of the parent's corner at its vertex */
    fx.k.for_each_loop(x, [&](LoopRef l) {
      fx.k.for_each_coedge(l, [&](CoedgeRef y) { UT_CHECK((*uv)[y.i] == int32_t(fx.k.start_vert(y).i) + 1000); });
    });
  }
  FaceRef s;
  EXPECT_EDIT(ed, ed.disconnect_loops(fx.edge_between(c.path[0], c.path[1]), &s), EditCode::Ok);
  UT_CHECK((*area)[s.i] == 1200.0 && (*paint)[2 * s.i] == 7 && (*paint)[2 * s.i + 1] == 8);
  fx.k.for_each_loop(s, [&](LoopRef l) {
    UT_CHECK((*tag)[l.i] == outer_tag);
    fx.k.for_each_coedge(l, [&](CoedgeRef y) { UT_CHECK((*uv)[y.i] == int32_t(fx.k.start_vert(y).i) + 1000); });
  });
}

template <geom::CoordType C> void run_all()
{
  test_connect<C>();
  test_connect_inclusions<C>();
  test_connect_refusals<C>(ValidateMode::Full);
  test_connect_refusals<C>(ValidateMode::Local);
  test_disconnect<C>();
  test_connect_sequences<C>();
  test_connect_propagation<C>();
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
