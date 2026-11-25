/* Random cuts through holes (plan 2026-11-24-connect-loops, §5): plates with
 * holes, windows and walls; connect_loops and disconnect_loops mixed with
 * splits, joins, inclusions, undo and redo. After every edit that succeeds:
 *
 *  - the complex is valid, the editor's index, grid and hint counts hold;
 *  - the faces' directed edges are the ones before, with the chords added or
 *    removed;
 *  - the loops and radial cycles equal those of a fresh build of the faces.
 *
 * A refused edit changes nothing. Every validation mode that checks, and
 * every coordinate type, ends with the same loops. */

#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

#include "litestl/util/alloc.h"
#include "support/cut_fixture.h"
#include "support/splitmix64.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
using litestl::test::SplitMix64;
using test::Fixture;
using test::I3;
using test::pid;
using test::pt;

namespace {

using Directed = std::multiset<std::pair<PersistentId, PersistentId>>;

/* loops by face, each from its smallest vertex, and radial cycles by edge */
struct Shape {
  std::map<PersistentId, std::vector<std::vector<PersistentId>>> faces;
  std::map<std::pair<PersistentId, PersistentId>, std::vector<std::pair<PersistentId, bool>>> radial;
  bool operator==(const Shape &b) const
  {
    return faces == b.faces && radial == b.radial;
  }
};

template <typename T> void canonical(std::vector<T> &v)
{
  std::rotate(v.begin(), std::min_element(v.begin(), v.end()), v.end());
}

template <geom::CoordType C> Shape shape_of(const Complex<C> &k)
{
  Shape out;
  k.faces.for_each_live([&](uint32_t f) {
    auto &loops = out.faces[k.face_id(FaceRef(f))];
    k.for_each_loop(FaceRef(f), [&](LoopRef l) {
      std::vector<PersistentId> v;
      k.for_each_coedge(l, [&](CoedgeRef c) { v.push_back(k.vert_id(k.start_vert(c))); });
      canonical(v);
      loops.push_back(v);
    });
    std::sort(loops.begin(), loops.end());
  });
  k.edges.for_each_live([&](uint32_t e) {
    const auto vs = k.edge_verts(EdgeRef(e));
    const PersistentId a = k.vert_id(vs[0]), b = k.vert_id(vs[1]);
    const PersistentId lo = a < b ? a : b, hi = a < b ? b : a;
    std::vector<std::pair<PersistentId, bool>> cyc;
    k.for_each_radial(EdgeRef(e), [&](CoedgeRef c) {
      cyc.push_back({k.face_id(k.face_of(c)), k.vert_id(k.start_vert(c)) == lo});
    });
    if (cyc.empty()) {
      return; /* a wire */
    }
    /* the order around lo -> hi from the edge's first coedge, whichever way
     * the edge is stored */
    if (!(a == lo)) {
      std::reverse(cyc.begin() + 1, cyc.end());
    }
    out.radial[{lo, hi}] = cyc;
  });
  return out;
}

template <geom::CoordType C> Shape fresh_shape(const Complex<C> &k, bool &built)
{
  Builder<C> b;
  std::map<uint32_t, uint32_t> key;
  k.faces.for_each_live([&](uint32_t f) {
    std::vector<uint32_t> keys, sizes;
    k.for_each_loop(FaceRef(f), [&](LoopRef l) {
      uint32_t n = 0;
      k.for_each_coedge(l, [&](CoedgeRef c) {
        const VertRef v = k.start_vert(c);
        if (!key.count(v.i)) {
          key[v.i] = b.vertex(k.vert_id(v), k.pos(v));
        }
        keys.push_back(key[v.i]);
        n++;
      });
      sizes.push_back(n);
    });
    b.face(k.face_id(FaceRef(f)), keys, sizes);
  });
  Complex<C> k2;
  BuildReport br;
  built = b.build(k2, br);
  return shape_of(k2);
}

template <geom::CoordType C> Directed directed(const Complex<C> &k)
{
  Directed out;
  k.coedges.for_each_live([&](uint32_t c) {
    out.insert({k.vert_id(k.start_vert(CoedgeRef(c))), k.vert_id(k.end_vert(CoedgeRef(c)))});
  });
  return out;
}

/* a plate of 3 x 2 cells with holes in some of them, some filled, some with
 * a wall on one edge; loops and their first vertices in random order */
template <geom::CoordType C> bool plate(Fixture<C> &fx, SplitMix64 &r)
{
  test::GridBuilder<C> g;
  std::vector<std::vector<I3>> loops = {
      {{0, 0, 0}, {60, 0, 0}, {120, 0, 0}, {120, 40, 0}, {120, 80, 0}, {60, 80, 0}, {0, 80, 0}, {0, 40, 0}}};
  if (r.below(2)) { /* concave */
    loops[0].insert(loops[0].begin() + 5, I3{70, 76, 0});
  }
  std::vector<std::vector<I3>> fills, walls;
  const uint32_t skip = uint32_t(r.below(6)), n = 1 + uint32_t(r.below(6));
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t cell = (i + skip) % 6;
    const int64_t x = 40 * int64_t(cell % 3) + 8 + int64_t(r.below(6));
    const int64_t y = 40 * int64_t(cell / 3) + 8 + int64_t(r.below(6));
    const int64_t w = 6 + 2 * int64_t(r.below(6)), h = 6 + 2 * int64_t(r.below(6));
    std::vector<I3> hole = {{x, y, 0}, {x, y + h, 0}, {x + w, y + h, 0}, {x + w, y, 0}};
    const std::vector<I3> wall = {hole[0], hole[3], {x + w, y, 20}, {x, y, 20}};
    if (r.below(2)) {
      hole.insert(hole.begin() + 3, I3{x + w, y + h / 2, 0});
    }
    if (r.below(2)) {
      hole.insert(hole.begin() + 1, I3{x + int64_t(r.below(3)), y + h / 2, 0}); /* collinear or pushed in */
    }
    if (r.below(2)) {
      fills.push_back(std::vector<I3>(hole.rbegin(), hole.rend()));
    }
    if (r.below(3) == 0) {
      walls.push_back(wall);
    }
    std::rotate(hole.begin(), hole.begin() + int64_t(r.below(hole.size())), hole.end());
    loops.insert(loops.begin() + int64_t(r.below(loops.size() + 1)), hole);
  }
  if (r.below(2)) {
    walls.push_back({{60, 0, 0}, {0, 0, 0}, {0, 0, -20}, {60, 0, -20}});
  }
  g.face(loops);
  for (const auto &l : fills) {
    g.face({l});
  }
  for (const auto &l : walls) {
    g.face({l});
  }
  BuildReport br;
  RegionReport rr;
  return g.b.build(fx.k, br) && build_regions(fx.k, rr);
}

struct Stats {
  uint32_t ok[10] = {}, refused[10] = {};
  uint32_t holes[4] = {}; /* connects by holes cut */
};

template <geom::CoordType C> FaceRef pick_face(const Complex<C> &k, SplitMix64 &r)
{
  std::vector<uint32_t> fs;
  k.faces.for_each_live([&](uint32_t f) { fs.push_back(f); });
  return FaceRef(fs[r.below(fs.size())]);
}

template <geom::CoordType C> EdgeRef pick_edge(const Complex<C> &k, SplitMix64 &r)
{
  std::vector<uint32_t> es;
  k.edges.for_each_live([&](uint32_t e) { es.push_back(e); });
  return EdgeRef(es[r.below(es.size())]);
}

template <geom::CoordType C> VertRef pick_vert(const Complex<C> &k, LoopRef l, SplitMix64 &r)
{
  uint32_t n = uint32_t(r.below(k.loop_size(l)));
  VertRef out;
  k.for_each_coedge(l, [&](CoedgeRef c) {
    if (n-- == 0) {
      out = k.start_vert(c);
    }
  });
  return out;
}

/* a vertex's position in the plate's integer units */
template <geom::CoordType C> I3 grid(const geom::Point<C> &p)
{
  const auto a = geom::to_double3<C>(p), u = geom::to_double3<C>(pt<C>({1, 0, 0}));
  I3 out;
  for (int i = 0; i < 3; i++) {
    out[i] = int64_t(std::llround(a[i] / u[0]));
  }
  return out;
}

template <geom::CoordType C> double left_of(const Complex<C> &k, LoopRef l)
{
  double x = 1e300;
  k.for_each_coedge(l, [&](CoedgeRef c) { x = std::min(x, geom::to_double3<C>(k.pos(k.start_vert(c)))[0]); });
  return x;
}

/* a path through one to three holes of a face, left to right or back; false
 * if the face has none */
template <geom::CoordType C> std::vector<VertRef> pick_path(const Complex<C> &k, FaceRef f, SplitMix64 &r, uint32_t &cut_holes)
{
  std::vector<LoopRef> ls;
  k.for_each_loop(f, [&](LoopRef l) { ls.push_back(l); });
  /* the outer loop has the leftmost vertex */
  const LoopRef outer =
      *std::min_element(ls.begin(), ls.end(), [&](LoopRef a, LoopRef b) { return left_of(k, a) < left_of(k, b); });
  std::vector<LoopRef> cut;
  for (const LoopRef l : ls) {
    if (l != outer && cut.size() < 3 && (cut.empty() || r.below(2))) {
      cut.push_back(l);
    }
  }
  std::sort(cut.begin(), cut.end(), [&](LoopRef a, LoopRef b) { return left_of(k, a) < left_of(k, b); });
  std::vector<VertRef> path = {pick_vert(k, outer, r)};
  for (const LoopRef l : cut) {
    path.push_back(pick_vert(k, l, r));
    path.push_back(pick_vert(k, l, r));
  }
  path.push_back(pick_vert(k, outer, r));
  if (r.below(2)) {
    std::reverse(path.begin(), path.end());
  }
  cut_holes = uint32_t(cut.size());
  return path;
}

/* the edges shared by the two faces of e */
template <geom::CoordType C>
std::vector<std::pair<PersistentId, PersistentId>> shared_edges(const Complex<C> &k, EdgeRef e)
{
  std::vector<FaceRef> fs;
  std::vector<std::pair<PersistentId, PersistentId>> out;
  k.for_each_radial(e, [&](CoedgeRef c) { fs.push_back(k.face_of(c)); });
  if (fs.size() != 2 || fs[0] == fs[1]) {
    return out;
  }
  k.edges.for_each_live([&](uint32_t x) {
    bool a = false, b = false;
    k.for_each_radial(EdgeRef(x), [&](CoedgeRef c) {
      a = a || k.face_of(c) == fs[0];
      b = b || k.face_of(c) == fs[1];
    });
    if (a && b) {
      const auto vs = k.edge_verts(EdgeRef(x));
      out.push_back({k.vert_id(vs[0]), k.vert_id(vs[1])});
    }
  });
  return out;
}

template <geom::CoordType C> Shape run(uint64_t seed, uint32_t steps, ValidateMode mode, Stats &st)
{
  SplitMix64 r(seed);
  Fixture<C> fx;
  if (!plate<C>(fx, r)) {
    fprintf(stderr, "seed %llu: no plate\n", (unsigned long long)seed);
    UT_CHECK(false);
    return {};
  }
  EditorOptions opt;
  opt.validate = mode;
  opt.verify_redo = true;
  Editor<C> ed(fx.k, &fx.j, nullptr, opt);
  const PersistentId h_start = test::h(fx.k);
  for (uint32_t n = 0; n < steps; n++) {
    const uint32_t op = uint32_t(r.below(20));
    const PersistentId h0 = test::h(fx.k);
    const Directed d0 = directed(fx.k);
    uint32_t kind = 0;
    EditCode got = EditCode::BadArgument;
    Directed added, removed;
    if (op < 8) {
      FaceRef f = pick_face(fx.k, r);
      for (int t = 0; t < 4 && fx.k.next_loop(fx.k.first_loop(f)).i == NONE; t++) {
        f = pick_face(fx.k, r);
      }
      uint32_t cut = 0;
      std::vector<VertRef> path = pick_path(fx.k, f, r, cut);
      for (int t = 0; t < 12 && (got = ed.connect_loops(f, path)) != EditCode::Ok; t++) {
        st.refused[0]++;
        UT_CHECK(test::h(fx.k) == h0);
        path = pick_path(fx.k, f, r, cut);
      }
      if (got == EditCode::Ok) {
        st.holes[cut]++;
        for (size_t i = 0; i < path.size(); i += 2) {
          const PersistentId a = fx.k.vert_id(path[i]), b = fx.k.vert_id(path[i + 1]);
          added.insert({a, b});
          added.insert({b, a});
        }
      }
    }
    else if (op < 12) {
      kind = 1;
      EdgeRef e = pick_edge(fx.k, r);
      for (int t = 0; t < 6 && shared_edges(fx.k, e).size() < 2; t++) {
        e = pick_edge(fx.k, r);
      }
      const auto chords = shared_edges(fx.k, e);
      got = ed.disconnect_loops(e);
      UT_CHECK(got != EditCode::Invalid); /* refused before anything is written */
      if (got == EditCode::Ok) {
        UT_CHECK(chords.size() > 1);
        for (const auto &[a, b] : chords) {
          removed.insert({a, b});
          removed.insert({b, a});
        }
      }
    }
    else if (op == 12) {
      kind = 2;
      const FaceRef f = pick_face(fx.k, r);
      const LoopRef l = fx.k.first_loop(f);
      const VertRef a = pick_vert(fx.k, l, r), b = pick_vert(fx.k, l, r);
      got = ed.split_face(f, a, b);
      if (got == EditCode::Ok) {
        added.insert({fx.k.vert_id(a), fx.k.vert_id(b)});
        added.insert({fx.k.vert_id(b), fx.k.vert_id(a)});
      }
    }
    else if (op == 13) {
      kind = 3;
      const EdgeRef e = pick_edge(fx.k, r);
      const auto vs = fx.k.edge_verts(e);
      const PersistentId a = fx.k.vert_id(vs[0]), b = fx.k.vert_id(vs[1]);
      got = ed.join_faces(e);
      if (got == EditCode::Ok) {
        removed.insert({a, b});
        removed.insert({b, a});
      }
    }
    else if (op == 14) {
      kind = 4;
      const EdgeRef e = pick_edge(fx.k, r);
      const auto vs = fx.k.edge_verts(e);
      const I3 a = grid<C>(fx.k.pos(vs[0])), b = grid<C>(fx.k.pos(vs[1]));
      if ((a[0] + b[0]) % 2 == 0 && (a[1] + b[1]) % 2 == 0 && (a[2] + b[2]) % 2 == 0) {
        got = ed.split_edge(e, pt<C>({(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2}));
      }
    }
    else if (op == 15) {
      kind = 5;
      const auto vs = fx.k.edge_verts(pick_edge(fx.k, r));
      got = ed.join_edge(vs[r.below(2)]);
    }
    else if (op == 16) {
      kind = 6;
      /* a point in a face, or a wire from one of its corners to it */
      const FaceRef f = pick_face(fx.k, r);
      const VertRef corner = pick_vert(fx.k, fx.k.first_loop(f), r);
      const I3 p = {1 + int64_t(r.below(118)), 1 + int64_t(r.below(78)), 0};
      const bool wire = r.below(2) == 0;
      VertRef v;
      ed.begin("inclusion");
      got = ed.make_vertex(pt<C>(p), pid(100000 + n), &v);
      if (got == EditCode::Ok) {
        got = ed.embed_vertex(v, EmbedTarget::face(f));
      }
      if (got == EditCode::Ok && wire) {
        got = ed.make_edge(corner, v, ident::kNoId, nullptr, EmbedTarget::face(f));
      }
      if (got == EditCode::Ok) {
        got = ed.commit();
      }
      else {
        ed.rollback();
      }
    }
    else if (op == 17) {
      kind = 7;
      got = fx.j.undo_count() ? ed.undo() : EditCode::BadArgument;
    }
    else if (op == 18) {
      kind = 8;
      got = fx.j.redo_count() ? ed.redo() : EditCode::BadArgument;
    }
    else {
      kind = 9;
      got = ed.rebuild_regions();
    }
    if (got != EditCode::Ok) {
      st.refused[kind]++;
      UT_CHECK(test::h(fx.k) == h0);
      continue;
    }
    st.ok[kind]++;
    bool built = false;
    const bool same = fresh_shape(fx.k, built) == shape_of(fx.k);
    const bool good = fx.valid() && ed.index_consistent() && ed.grid_consistent() && ed.region_hints_consistent() &&
                      built && same;
    if (!good) {
      fprintf(stderr, "seed %llu step %u kind %u: built %d same %d\n", (unsigned long long)seed, n, kind, int(built),
              int(same));
    }
    UT_CHECK(good);
    if (kind < 4 || kind == 9) {
      Directed want = d0;
      for (const auto &x : removed) {
        UT_CHECK(want.count(x) == 1);
        want.erase(x);
      }
      want.insert(added.begin(), added.end());
      UT_CHECK(directed(fx.k) == want);
    }
  }
  EXPECT_EDIT(ed, ed.rebuild_regions(), EditCode::Ok);
  UT_CHECK(fx.valid());
  const PersistentId h_end = test::h(fx.k);
  const Shape out = shape_of(fx.k);
  while (fx.j.undo_count() > 0) {
    UT_CHECK(ed.undo() == EditCode::Ok);
  }
  UT_CHECK(test::h(fx.k) == h_start);
  while (fx.j.redo_count() > 0) {
    UT_CHECK(ed.redo() == EditCode::Ok);
  }
  UT_CHECK(test::h(fx.k) == h_end);
  return out;
}

} // namespace

int main(int argc, char **argv)
{
  const size_t baseline = litestl::alloc::getMemorySize();
  const uint32_t seeds = argc > 1 ? uint32_t(atoi(argv[1])) : 12;
  {
    Stats st;
    for (uint64_t seed = 1; seed <= seeds; seed++) {
      Stats other;
      const Shape a = run<geom::Lattice>(seed, 120, ValidateMode::Full, st);
      UT_CHECK((a == run<geom::Lattice>(seed, 120, ValidateMode::Local, other)));
      UT_CHECK((a == run<double>(seed, 120, ValidateMode::Local, other)));
      UT_CHECK((a == run<float>(seed, 120, ValidateMode::Full, other)));
    }
    static const char *names[] = {"connect",   "disconnect", "split_face", "join_faces", "split_edge",
                                  "join_edge", "inclusion",  "undo",       "redo",       "regions"};
    for (int i = 0; i < 10; i++) {
      printf("%-12s ok %5u refused %5u\n", names[i], st.ok[i], st.refused[i]);
    }
    printf("connects by holes cut: %u %u %u\n", st.holes[1], st.holes[2], st.holes[3]);
    if (seeds >= 12) {
      /* every kind of edit happened, and cuts through several holes */
      for (int i = 0; i < 10; i++) {
        UT_CHECK(st.ok[i] > 0);
      }
      UT_CHECK(st.holes[1] > 60 && st.holes[2] > 2 && st.holes[3] > 0 && st.ok[1] > 40);
    }
  }
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
