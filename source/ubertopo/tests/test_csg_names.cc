/* Names of the elements of a CSG result (plan 2026-11-25-exact-booleans §4.6):
 * what an edit renames, and what it doesn't. */

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

#include "litestl/util/alloc.h"
#include "ut_test.h"

#include "support/csg_world.h"
#include "support/curve_fixture.h"

using namespace ubertopo;
using namespace ubertopo::test;

namespace {

/* the wall of the fixtures: 400 x 10 x 100, with six windows */
struct Wall : World {
  PersistentId window;

  Wall()
  {
    window = box({0, -2, 0}, {30, 12, 40});
    op(1, 0, Op::Difference);
    put(2, 1, leaf(box({0, 0, 0}, {400, 10, 100})));
    for (uint64_t i = 0; i < 6; i++) {
      put(10 + i, 1, at(i, 0, 0));
    }
  }
  CsgLeaf at(uint64_t i, int64_t dx, int64_t dz) const
  {
    return leaf(window, {10 + 60 * int64_t(i) + dx, 0, 30 + dz});
  }
};

void test_window()
{
  Wall w;
  const Out a = w.run();
  UT_CHECK(a.faces.size() == 6 + 6 * 4 && a.verts.size() == 8 + 6 * 8 && a.regions.size() == 2);
  /* every name is what its sources make it */
  for (uint64_t i = 0; i < 6; i++) {
    Wall x;
    x.set(10 + i, x.at(i, 7, -5));
    const Out b = x.run();
    UT_CHECK(names(b.verts) == names(a.verts) && names(b.faces) == names(a.faces) && b.regions == a.regions);
    UT_CHECK(b.faces == a.faces && moved(a, b) == 8);
    /* another shape: new names for it only */
    x.set(10 + i, x.leaf(x.box({0, -3, 0}, {20, 13, 20}), {10 + 60 * int64_t(i), 0, 30}));
    const Out c = x.run();
    UT_CHECK(c.verts.size() == a.verts.size() && moved(a, c) == 0);
    UT_CHECK(minus(names(a.verts), names(c.verts)).size() <= 8);
    /* gone */
    UT_CHECK(x.t.remove(id(10 + i)) == CsgCode::Ok);
    const Out d = x.run();
    UT_CHECK(d.verts.size() == a.verts.size() - 8 && minus(names(d.verts), names(a.verts)).empty() && moved(a, d) == 0);
    UT_CHECK(minus(names(d.faces), names(a.faces)).empty() && d.regions == a.regions);
    /* and back, in front of the others */
    x.put(10 + i, 1, x.at(i, 0, 0), 1);
    const Out e = x.run();
    UT_CHECK(e.verts == a.verts && e.faces == a.faces && e.regions == a.regions);
    UT_CHECK(topo::content_hash(e.k) == topo::content_hash(a.k));
    /* one more between them */
    x.put(70, 1, x.leaf(x.box({0, -2, 0}, {4, 12, 4}), {50, 0, 90}), 3);
    const Out f = x.run();
    UT_CHECK(minus(names(a.verts), names(f.verts)).empty() && minus(names(a.faces), names(f.faces)).empty());
    UT_CHECK(f.verts.size() == a.verts.size() + 8 && f.faces.size() == a.faces.size() + 4 && moved(a, f) == 0);
  }
}

/* a door from the floor to the ceiling cuts the wall in two */
void test_door()
{
  Wall w;
  const Out a = w.run();
  std::map<int64_t, Out> at;
  const PersistentId door = w.box({0, -2, -2}, {8, 12, 102});
  w.put(50, 1, w.leaf(door, {45, 0, 0}));
  for (const int64_t x : {45, 47, 105, 165, 225, 285, 345}) {
    w.set(50, w.leaf(door, {x, 0, 0}));
    const Out &b = at.emplace(x, w.run()).first->second;
    /* the six faces of the wall: two ends, four in two pieces each */
    UT_CHECK(b.faces.size() == a.faces.size() + 4 + 2 && b.verts.size() == a.verts.size() + 8);
    UT_CHECK(minus(names(a.verts), names(b.verts)).empty() && minus(names(a.faces), names(b.faces)).empty());
    UT_CHECK(moved(a, b) == 0);
    /* (the wall's two parts are two regions of one label; one keeps the name) */
    UT_CHECK(b.regions.size() == 3 && minus({a.regions.begin(), a.regions.end()}, b.regions).empty());
  }
  /* it moves past the windows: all names stay, and the pieces are the ones of
   * the same ends */
  const Out &b = at.at(45);
  for (const auto &[x, c] : at) {
    UT_CHECK(names(c.verts) == names(b.verts) && names(c.faces) == names(b.faces) && c.regions == b.regions);
    for (const auto &[f, loops] : b.faces) {
      std::set<Key> p, q;
      for (const auto &l : loops) {
        p.insert(l.begin(), l.end());
      }
      for (const auto &l : c.faces.at(f)) {
        q.insert(l.begin(), l.end());
      }
      /* (the corners of the wall and of the door are common) */
      std::set<Key> both;
      std::set_intersection(p.begin(), p.end(), q.begin(), q.end(), std::inserter(both, both.begin()));
      UT_CHECK(both.size() >= 4);
    }
  }
  /* a second one: the middle piece has no vertex of the wall's */
  w.set(50, w.leaf(door, {45, 0, 0}));
  w.put(51, 1, w.leaf(door, {225, 0, 0}));
  const Out two = w.run();
  UT_CHECK(two.faces.size() == a.faces.size() + 8 + 4 && two.regions.size() == 4);
  UT_CHECK(minus(names(b.verts), names(two.verts)).empty());
  w.set(51, w.leaf(door, {285, 0, 0}));
  const Out three = w.run();
  UT_CHECK(names(three.verts) == names(two.verts) && names(three.faces) == names(two.faces));
  UT_CHECK(three.regions == two.regions);
}

/* a trimmed corner, a partition that is flush in an inner corner, an extension
 * that joins at a wall's end */
void test_trim()
{
  {
    Wall w;
    const Out a = w.run();
    w.put(60, 1, w.leaf(w.box({0, 0, 0}, {20, 30, 30}), {390, -10, 80}));
    const Out b = w.run();
    /* two vertices of the wall are gone */
    UT_CHECK(minus(names(a.verts), names(b.verts)).size() == 2 && minus(names(a.faces), names(b.faces)).empty());
    UT_CHECK(moved(a, b) == 0 && b.regions == a.regions);
  }
  {
    World w;
    const PersistentId name = id(900);
    w.op(1, 0, Op::Union);
    w.put(2, 1, w.leaf(w.box({0, 0, 0}, {100, 10, 30}), {0, 0, 0}, name));
    w.put(3, 1, w.leaf(w.box({0, 10, 0}, {10, 100, 30}), {0, 0, 0}, name));
    const Out a = w.run();
    UT_CHECK(a.faces.size() == 11 && a.regions.size() == 2);
    /* in the corner, and level with both */
    w.put(4, 1, w.leaf(w.box({0, 0, 0}, {30, 20, 30}), {10, 10, 0}, name));
    const Out b = w.run();
    UT_CHECK(b.regions == a.regions && b.faces.size() == 15 && moved(a, b) == 0);
    UT_CHECK(minus(names(a.faces), names(b.faces)).empty());
    /* wider: no vertex of the others changes */
    w.set(4, w.leaf(w.box({0, 0, 0}, {50, 25, 30}), {10, 10, 0}, name));
    const Out c = w.run();
    UT_CHECK(minus(names(a.faces), names(c.faces)).empty() && moved(a, c) == 0);
    std::set<Key> old = names(a.verts);
    UT_CHECK(minus(old, names(c.verts)) == minus(old, names(b.verts)));
  }
}

/* a wall without thickness: the corners of a door are the cutter's */
void test_thin()
{
  World w;
  GridBuilder<L> g;
  g.face({{{0, 0, 0}, {100, 0, 0}, {100, 0, 30}, {0, 0, 30}}});
  const PersistentId sheet = w.add(g), door = w.box({0, -3, -4}, {9, 3, 20});
  CsgLeaf l = w.leaf(sheet);
  l.thin = true;
  w.op(1, 0, Op::Difference);
  w.put(2, 1, l);
  w.put(3, 1, w.leaf(door, {20, 0, 0}));
  w.put(4, 1, w.leaf(door, {50, 0, 10}));
  const Out a = w.run();
  UT_CHECK(a.faces.size() == 1 && a.verts.size() == 4 + 4 + 4);
  w.set(3, w.leaf(door, {23, 0, -2}));
  const Out b = w.run();
  UT_CHECK(b.faces == a.faces && moved(a, b) == 4);
  UT_CHECK(w.t.remove(id(3)) == CsgCode::Ok);
  const Out c = w.run();
  UT_CHECK(c.verts.size() == 8 && minus(names(c.verts), names(a.verts)).empty() && moved(a, c) == 0);
  /* cutters that share an edge, whose faces meet: the first in the tree */
  w.put(5, 1, w.leaf(door, {29, 0, 0}), 1);
  w.put(6, 1, w.leaf(door, {20, 0, 0}));
  const Out d = w.run();
  UT_CHECK(d.faces.size() == 1 && d.verts.size() == 4 + 4 + 4);
}

/* one name twice: ordinals, by position */
void test_ordinals()
{
  World w;
  /* a rod goes through both arms of a U */
  GridBuilder<L> g;
  const I3 lo[8] = {{0, 0, 0}, {60, 0, 0}, {60, 0, 50}, {40, 0, 50}, {40, 0, 20}, {20, 0, 20}, {20, 0, 50}, {0, 0, 50}};
  std::vector<I3> front(lo, lo + 8), back;
  for (int i = 7; i >= 0; i--) {
    back.push_back({lo[i][0], 20, lo[i][2]});
  }
  g.face({{front[0], front[1], front[2], front[3], front[4], front[5], front[6], front[7]}});
  g.face({{back[0], back[1], back[2], back[3], back[4], back[5], back[6], back[7]}});
  for (int i = 0; i < 8; i++) {
    const int j = (i + 1) % 8;
    const I3 &p = lo[i], &q = lo[j];
    g.face({{q, p, {p[0], 20, p[2]}, {q[0], 20, q[2]}}});
  }
  const PersistentId u = w.add(g), rod = w.box({-10, 0, 0}, {70, 4, 4});
  w.op(1, 0, Op::Union);
  w.put(2, 1, w.leaf(u, {0, 0, 0}, id(900)));
  w.put(3, 1, w.leaf(rod, {0, 8, 30}, id(900)));
  const Out a = w.run();
  UT_CHECK(a.rep.output.names.ordinals == 0); /* (four faces of the U) */
  /* the rod's four long faces are cut into three pieces */
  UT_CHECK(a.faces.size() == 10 + 2 + 4 * 3 && a.verts.size() == 16 + 8 + 16);
  w.set(3, w.leaf(rod, {0, 8, 34}, id(900)));
  const Out b = w.run();
  UT_CHECK(b.faces == a.faces && moved(a, b) == 24);
  /* two windows of one leaf, of one face of it: a plate with a comb */
  World x;
  GridBuilder<L> c;
  const I3 v[9] = {{0, 0, 0}, {8, 0, 40}, {16, 0, 0}, {24, 0, 40}, {32, 0, 0}, {40, 0, 40}, {48, 0, 0}, {48, 0, -9}, {0, 0, -9}};
  std::vector<I3> f2(v, v + 9);
  c.face({{f2[8], f2[7], f2[6], f2[5], f2[4], f2[3], f2[2], f2[1], f2[0]}});
  c.face({{{0, 30, 0}, {8, 30, 40}, {16, 30, 0}, {24, 30, 40}, {32, 30, 0}, {40, 30, 40}, {48, 30, 0}, {48, 30, -9}, {0, 30, -9}}});
  for (int i = 0; i < 9; i++) {
    const int j = (i + 1) % 9;
    c.face({{v[i], v[j], {v[j][0], 30, v[j][2]}, {v[i][0], 30, v[i][2]}}});
  }
  const PersistentId comb = x.add(c);
  x.op(1, 0, Op::Difference);
  x.put(2, 1, x.leaf(x.box({-5, 5, 10}, {60, 25, 50})));
  x.put(3, 1, x.leaf(comb));
  const Out e = x.run();
  printf("comb: %u faces, %u vertices, %u ordinals, %u collisions\n", uint32_t(e.faces.size()), uint32_t(e.verts.size()),
         e.rep.output.names.ordinals, e.rep.output.names.collisions);
  UT_CHECK(e.rep.output.names.collisions == 0);
}

/* a straight edge crosses a folded face twice */
void test_folded()
{
  uint32_t found = 0;
  for (const int64_t z : {250, 216}) {
    World w;
    GridBuilder<L> g;
    const I3 c[4] = {{0, 0, 0}, {200, 0, 0}, {200, 200, 0}, {0, 200, 0}};
    I3 hi[4];
    for (int i = 0; i < 4; i++) {
      hi[i] = {c[i][0], c[i][1], i % 2 ? 200 : 280};
    }
    g.face({{c[3], c[2], c[1], c[0]}});
    g.face({{hi[0], hi[1], hi[2], hi[3]}});
    for (int i = 0; i < 4; i++) {
      const int j = (i + 1) % 4;
      g.face({{c[i], c[j], hi[j], hi[i]}});
    }
    topo::BuildPolicy policy;
    policy.allow_projectable_faces = true;
    const PersistentId rod = w.box({-50, 0, 0}, {250, 4, 4});
    w.op(1, 0, Op::Union);
    w.put(2, 1, w.leaf(w.add(g, policy)));
    w.put(3, 1, w.leaf(rod, {0, 95, z}));
    const Out a = w.run();
    w.set(3, w.leaf(rod, {0, 97, z + 1}));
    const Out b = w.run();
    UT_CHECK(names(a.verts) == names(b.verts) && a.rep.output.names.ordinals == b.rep.output.names.ordinals);
    UT_CHECK(a.rep.output.names.ordinals == 0 || a.rep.output.names.ordinals == 4);
    found += a.rep.output.names.ordinals == 4;
  }
  {
    /* four times: a sawtooth */
    World w;
    GridBuilder<L> g;
    std::vector<I3> f, b, fl;
    for (int i = 0; i < 5; i++) {
      f.push_back({100 * i, 0, i % 2 ? 200 : 100});
      b.push_back({100 * i, 90, i % 2 ? 200 : 100});
    }
    g.face({{f[0], f[1], f[2], f[3], f[4], b[4], b[3], b[2], b[1], b[0]}});
    const I3 c[4] = {{0, 0, 0}, {400, 0, 0}, {400, 90, 0}, {0, 90, 0}};
    g.face({{c[3], c[2], c[1], c[0]}});
    g.face({{c[0], c[1], f[4], f[3], f[2], f[1], f[0]}});
    g.face({{c[2], c[3], b[0], b[1], b[2], b[3], b[4]}});
    g.face({{c[1], c[2], b[4], f[4]}});
    g.face({{c[3], c[0], f[0], b[0]}});
    topo::BuildPolicy policy;
    policy.allow_projectable_faces = true;
    w.op(1, 0, Op::Union);
    w.put(2, 1, w.leaf(w.add(g, policy)));
    w.put(3, 1, w.leaf(w.box({-9, 0, 0}, {409, 4, 4}), {0, 40, 150}));
    const Out a = w.run();
    UT_CHECK(a.rep.output.names.ordinals == 4 * 3);
  }
  UT_CHECK(found == 1); /* (above the fold if it is a ridge, below it if it is a valley) */
}

/* a sheet with a curved edge: the points of its skeleton keep their names when
 * it is cut */
void test_curved()
{
  World w;
  topo::Builder<L> b;
  const int64_t at[4][2] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
  std::vector<uint32_t> keys;
  for (int i = 0; i < 4; i++) {
    keys.push_back(b.vertex(pid(uint64_t(1 + i)), fpt<L>(double(at[i][0]), double(at[i][1]), 0)));
  }
  const uint32_t sizes[1] = {4};
  b.face(pid(50), keys, sizes);
  LComplex k;
  topo::BuildReport br;
  topo::RegionReport rr;
  UT_CHECK(b.build(k, br) && topo::build_regions(k, rr));
  {
    topo::Editor<L> ed(k, nullptr);
    topo::VertRef v[2];
    k.verts.for_each_live([&](uint32_t i) {
      for (int n = 0; n < 2; n++) {
        v[n] = k.vert_id(topo::VertRef(i)) == pid(uint64_t(1 + n)) ? topo::VertRef(i) : v[n];
      }
    });
    const topo::EdgeRef e = ed.find_edge(v[0], v[1]);
    const auto vs = k.edge_verts(e);
    litestl::attr::PoolRef arc;
    double s = 0;
    UT_CHECK(arc_carrier(ed, vs[0], vs[1], vs[0] == v[0] ? 0.4 : -0.4, &arc, &s) == topo::EditCode::Ok);
    UT_CHECK(ed.set_edge_curve(e, arc, 0, s) == topo::EditCode::Ok);
  }
  UT_CHECK(topo::build_regions(k, rr));
  const PersistentId sheet = w.store.add(std::move(k)), box = w.box({0, -5000, -10}, {300, 6000, 10});
  CsgLeaf l = w.leaf(sheet);
  l.thin = true;
  w.op(1, 0, Op::Difference);
  w.put(2, 1, l);
  const Out a = w.run();
  UT_CHECK(a.faces.size() == 1 && a.verts.size() > 12);
  w.put(3, 1, w.leaf(box, {1501, 0, 0}));
  for (const int64_t x : {1501, 700, 2201}) {
    w.set(3, w.leaf(box, {x, 0, 0}));
    const Out c = w.run();
    /* four corners are new; what isn't cut off is what it was */
    UT_CHECK(c.faces.size() == 2 && moved(a, c) == 0 && minus(names(c.verts), names(a.verts)).size() == 4);
    const size_t gone = minus(names(a.verts), names(c.verts)).size();
    UT_CHECK(gone > 0 && gone < a.verts.size() / 2);
    UT_CHECK(minus(names(a.faces), names(c.faces)).empty());
  }
}

/* sheets that stand on a slab cut its top into a grid: the inner pieces have
 * no vertex of the slab's, and share all they have */
struct Grid : World {
  PersistentId across, along;

  Grid(int n)
  {
    GridBuilder<L> g, h;
    g.face({{{0, 0, 0}, {0, 100, 0}, {0, 100, 30}, {0, 0, 30}}});
    h.face({{{0, 0, 0}, {100, 0, 0}, {100, 0, 30}, {0, 0, 30}}});
    across = add(g);
    along = add(h);
    op(1, 0, Op::Union);
    put(2, 1, leaf(box({0, 0, -10}, {100, 100, 0})));
    for (int i = 0; i < n; i++) {
      put(10 + uint64_t(i), 1, sheet(across, {20 * i + 20, 0, 0}));
      put(20 + uint64_t(i), 1, sheet(along, {0, 20 * i + 20, 0}));
    }
  }
  CsgLeaf sheet(PersistentId operand, const I3 &offset) const
  {
    CsgLeaf l = leaf(operand, offset);
    l.thin = true;
    return l;
  }
};

void test_grid()
{
  Grid w(3);
  const Out a = w.run();
  UT_CHECK(a.faces.size() == 5 + 16 + 2 * 3 * 4 && a.rep.output.names.collisions == 0);
  /* a sheet moves: all names stay, and the pieces keep their corners */
  w.set(11, w.sheet(w.across, {47, 0, 0}));
  const Out b = w.run();
  UT_CHECK(b.faces == a.faces && moved(a, b) > 0);
  /* another order of the sheets */
  UT_CHECK(w.t.move(id(22), id(1), 1) == CsgCode::Ok && w.t.move(id(10), id(1)) == CsgCode::Ok);
  const Out c = w.run();
  UT_CHECK(c.faces == b.faces && c.verts == b.verts);
  /* a new one in the last row cuts only that */
  w.put(30, 1, w.sheet(w.across, {90, 0, 0}));
  const Out d = w.run();
  UT_CHECK(minus(names(b.verts), names(d.verts)).empty());
  uint32_t kept = 0;
  for (const auto &[f, loops] : b.faces) {
    kept += d.faces.count(f) && d.faces.at(f) == loops;
  }
  /* (four pieces, the four last parts of the long sheets, and the slab's two
   * sides change) */
  UT_CHECK(kept >= b.faces.size() - 4 - 3 - 2);
}

/* regions: by label; rooms; parts of one label */
void test_regions()
{
  World w;
  const PersistentId wall = id(900), hall = id(901), attic = id(902);
  w.op(1, 0, Op::Difference);
  w.put(2, 1, w.leaf(w.box({0, 0, 0}, {100, 100, 100}), {0, 0, 0}, wall));
  w.put(3, 1, w.leaf(w.box({0, 0, 0}, {40, 80, 80}), {5, 10, 10}));
  w.put(4, 1, w.leaf(w.box({0, 0, 0}, {40, 80, 80}), {55, 10, 10}));
  w.op(5, 0 + 1, Op::Union);
  UT_CHECK(w.t.remove(id(5)) == CsgCode::Ok);
  UT_CHECK(w.t.wrap(id(6), Op::Union, id(1)) == CsgCode::Ok);
  w.put(7, 6, w.leaf(w.box({0, 0, 0}, {40, 80, 80}), {5, 10, 10}, hall, LabelKind::Air));
  auto has = [](const Out &o, PersistentId name) { return o.regions.count(key(ident::region_id_from_hint(name))) == 1; };
  const Out a = w.run();
  UT_CHECK(a.regions.size() == 4 && has(a, wall) && has(a, hall) && !has(a, attic));
  w.put(8, 6, w.leaf(w.box({0, 0, 0}, {40, 80, 80}), {55, 10, 10}, attic, LabelKind::Air));
  const Out b = w.run();
  UT_CHECK(b.regions.size() == 4 && has(b, wall) && has(b, hall) && has(b, attic));
  UT_CHECK(b.verts == a.verts && b.faces == a.faces);
  /* a window to the hall: it is outside now */
  w.put(9, 1, w.leaf(w.box({0, 0, 0}, {30, 5, 5}), {-5, 50, 50}));
  const Out c = w.run();
  UT_CHECK(c.regions.size() == 3 && has(c, wall) && !has(c, hall) && has(c, attic));
  /* two rooms that are joined have the first name in the tree */
  UT_CHECK(w.t.remove(id(9)) == CsgCode::Ok);
  w.put(9, 1, w.leaf(w.box({0, 0, 0}, {30, 5, 5}), {35, 50, 50}));
  const Out d = w.run();
  UT_CHECK(d.regions.size() == 3 && has(d, hall) && !has(d, attic));
  UT_CHECK(w.t.move(id(8), id(6), 0) == CsgCode::Ok);
  const Out e = w.run();
  UT_CHECK(e.regions.size() == 3 && !has(e, hall) && has(e, attic) && e.verts == d.verts);
  {
    /* a leaf with two rooms, one open to the outside: the other has the name */
    World y;
    GridBuilder<L> g;
    g.box({0, 0, 0}, {4, 10, 10});
    g.box({20, 0, 0}, {24, 10, 10});
    const PersistentId rooms = y.add(g);
    CsgLeaf l = y.leaf(rooms, {5, 5, 5}, kNoId, LabelKind::Air);
    y.store.find(rooms)->regions.for_each_live([&](uint32_t q) {
      if (q != 0) {
        l.rules.append({y.store.find(rooms)->regions.get<".id">()[q], LabelKind::Air, hall});
      }
    });
    y.op(1, 0, Op::Union);
    y.op(3, 1, Op::Difference);
    y.put(4, 3, y.leaf(y.box({0, 0, 0}, {40, 20, 20}), {0, 0, 0}, wall));
    y.put(5, 3, y.leaf(rooms, {5, 5, 5}));
    y.put(2, 1, l);
    for (const int64_t x : {-1, 28}) {
      if (x > 0) {
        UT_CHECK(y.t.remove(id(6)) == CsgCode::Ok);
      }
      y.put(6, 3, y.leaf(y.box({0, 0, 0}, {13, 2, 2}), {x, 9, 9}));
      const Out z = y.run();
      UT_CHECK(z.regions.size() == 3 && has(z, hall) && has(z, wall));
    }
  }
  /* the report has them all */
  uint32_t solids = 0, airs = 0;
  for (uint32_t r = 1; r < e.rep.output.regions.size(); r++) {
    const csg::RegionSides &s = e.rep.output.regions[int(r)];
    solids += s.solid != 0 && e.rep.labels[int(s.solid)] == wall;
    airs += s.solid == 0 && s.air != 0 && e.rep.labels[int(s.air)] == attic;
  }
  UT_CHECK(solids == 1 && airs == 1);
  /* parts of one name: the one with the smallest face name has it; the others
   * keep theirs when a third comes */
  World p;
  p.op(1, 0, Op::Union);
  const PersistentId unit = p.box({0, 0, 0}, {10, 10, 10});
  for (uint64_t i = 0; i < 5; i++) {
    p.put(2 + i, 1, p.leaf(unit, {20 * int64_t(i), 0, 0}, wall));
    const Out f = p.run();
    UT_CHECK(f.regions.size() == i + 2 && has(f, wall));
    p.set(2 + i, p.leaf(unit, {20 * int64_t(i), 3, 0}, wall));
    UT_CHECK(p.run().regions == f.regions);
  }
}

/* projected operands: names of diagonals, and pieces of their triangles */
void test_projected()
{
  SplitMix64 r(71);
  uint32_t cut = 0;
  for (int s = 0; s < 20; s++) {
    World w;
    GridBuilder<L> g;
    const int64_t z[4] = {100 + int64_t(r.below(20)), 100, 120 + int64_t(r.below(20)), 100};
    const I3 c[4] = {{0, 0, 0}, {200, 0, 0}, {200, 200, 0}, {0, 200, 0}};
    I3 hi[4];
    for (int i = 0; i < 4; i++) {
      hi[i] = {c[i][0], c[i][1], z[i]};
    }
    g.face({{c[3], c[2], c[1], c[0]}});
    g.face({{hi[0], hi[1], hi[2], hi[3]}});
    for (int i = 0; i < 4; i++) {
      const int j = (i + 1) % 4;
      g.face({{c[i], c[j], hi[j], hi[i]}});
    }
    topo::BuildPolicy policy;
    policy.allow_projectable_faces = true;
    const PersistentId block = w.add(g, policy), shaft = w.box({0, 0, -5}, {31, 17, 300});
    w.op(1, 0, Op::Difference);
    w.put(2, 1, w.leaf(block));
    const Out plain = w.run();
    UT_CHECK(plain.faces.size() == 6 && plain.verts.size() == 8);
    w.put(3, 1, w.leaf(shaft, {20, 30, 0}));
    const Out away = w.run();
    /* through the diagonal (whichever it is) */
    w.set(3, w.leaf(shaft, {int64_t(80 + r.below(8)), int64_t(90 + r.below(8)), 0}));
    const Out on = w.run();
    w.set(3, w.leaf(shaft, {int64_t(80 + r.below(8)), int64_t(90 + r.below(8)), 0}));
    const Out there = w.run();
    UT_CHECK(minus(names(plain.verts), names(on.verts)).empty() && on.faces.size() == 6 + 1 + 4);
    UT_CHECK(names(on.faces) == names(there.faces));
    UT_CHECK(names(away.verts).size() >= 16 && on.rep.output.names.collisions == 0);
    if (names(on.verts) == names(there.verts)) {
      cut++;
    }
  }
  UT_CHECK(cut == 20);
}

/* the names themselves: stored IDs depend on them */
void test_golden()
{
  ident::Hasher128 h(ident::tag::TestOnly);
  auto add = [&](const Out &o) {
    for (const auto &[k, p] : o.verts) {
      h.u64(k.first).u64(k.second).i64(p[0]).i64(p[1]).i64(p[2]);
    }
    for (const auto &[k, loops] : o.faces) {
      h.u64(k.first).u64(k.second);
    }
    for (const Key &k : o.regions) {
      h.u64(k.first).u64(k.second);
    }
  };
  Wall w;
  w.put(50, 1, w.leaf(w.box({0, -2, -2}, {8, 12, 102}), {45, 0, 0}));
  w.put(51, 1, w.leaf(w.box({0, -2, -2}, {8, 12, 102}), {225, 0, 0}));
  add(w.run());
  Grid g(3);
  add(g.run());
  SplitMix64 r(75);
  for (int s = 0; s < 60; s++) {
    World x;
    x.op(1, 0, Op(1 + s % 3));
    for (uint64_t i = 0; i < 4; i++) {
      Operands o;
      add_random(r, 3, o);
      LComplex k = *o.k.back();
      CsgLeaf l;
      l.operand = x.store.add(std::move(k));
      l.thin = i % 2;
      x.put(10 + i, 1, l);
    }
    BoolOptions opt;
    opt.max_quantum_shift = 14;
    add(test::run(x.t, x.store, opt));
  }
  const PersistentId got = h.finish();
  printf("golden: {0x%016llxull, 0x%016llxull}\n", (unsigned long long)got.lo, (unsigned long long)got.hi);
  UT_CHECK(got == (PersistentId{0x0d0dc2f254fe9118ull, 0x9784a26052c8313cull}));
}

/* a leaf far away: without faces it changes nothing; with, only itself */
void test_locality()
{
  SplitMix64 r(72);
  uint32_t runs = 0, verts = 0, ordinals = 0, weak = 0, collisions = 0, faces = 0, pieces = 0;
  for (int s = 0; s < 200; s++) {
    World w;
    w.op(1, 0, Op::Union);
    w.op(2, 1, Op(1 + r.below(3)));
    const int n = 2 + int(r.below(4));
    for (int i = 0; i < n; i++) {
      Operands o;
      add_random(r, 3, o);
      LComplex k = *o.k.back();
      CsgLeaf l;
      l.operand = w.store.add(std::move(k));
      l.thin = r.below(2);
      l.kind = r.below(4) ? LabelKind::Solid : LabelKind::Air;
      w.put(10 + uint64_t(i), 2, l);
    }
    BoolOptions opt;
    opt.max_quantum_shift = 14;
    const Out a = test::run(w.t, w.store, opt);
    if (a.rep.quantum_shift > 0) {
      opt.frame.quantum_log2 = -13;
    }
    const int shift = a.rep.quantum_shift;
    runs++;
    verts += uint32_t(a.verts.size());
    faces += uint32_t(a.faces.size());
    ordinals += a.rep.output.names.ordinals;
    weak += a.rep.output.names.weak;
    collisions += a.rep.output.names.collisions;
    std::set<std::pair<uint32_t, uint32_t>> sources;
    for (const csg::ResultFace &f : a.rep.output.result.faces) {
      sources.insert({f.source, 0});
    }
    pieces += uint32_t(a.faces.size() - sources.size());
    const PersistentId unit = w.box({0, 0, 0}, {5, 5, 5});
    World x;
    x.t = w.t;
    w.put(90, 1, w.leaf(unit, {50, 60, 70}, kNoId, LabelKind::Air), r.below(2) ? 0 : CsgTree::kEnd);
    const Out b = test::run(w.t, w.store, opt);
    UT_CHECK(b.rep.quantum_shift == shift && topo::content_hash(b.k) == topo::content_hash(a.k));
    w.set(90, w.leaf(unit, {50, 60, 70}));
    const Out c = test::run(w.t, w.store, opt);
    UT_CHECK(c.verts.size() == a.verts.size() + 8 && c.faces.size() == a.faces.size() + 6 && moved(a, c) == 0);
    for (const auto &[f, loops] : a.faces) {
      UT_CHECK(c.faces.count(f) && c.faces.at(f) == loops);
    }
    UT_CHECK(minus(a.regions, c.regions).empty() && c.regions.size() == a.regions.size() + 1);
  }
  printf("random: %u runs, %u vertices (%u ordinals, %u weak), %u faces (%u pieces, %u collisions)\n", runs, verts,
         ordinals, weak, faces, pieces, collisions);
  UT_CHECK(verts > 1500 && pieces > 100);
}

/* a signed permutation and a translation of the whole tree; the order of the
 * cutters; how the operands were numbered */
void test_invariance()
{
  SplitMix64 r(73);
  uint32_t runs = 0, same_order = 0, all = 0, ordinals = 0, turned = 0;
  for (int s = 0; s < 200; s++) {
    World w, m, q;
    for (World *x : {&w, &m, &q}) {
      x->op(1, 0, Op::Difference);
    }
    const csg::GridPlacement pl = random_placement(r);
    std::vector<CsgLeaf> leaves;
    const int n = 3 + int(r.below(3));
    for (int i = 0; i < n; i++) {
      Operands o;
      add_random(r, 3, o);
      CsgLeaf l;
      l.thin = r.below(2);
      for (World *x : {&w, &m, &q}) {
        LComplex k = *o.k.back();
        l.operand = x->store.add(std::move(k));
      }
      leaves.push_back(l);
    }
    for (int i = 0; i < n; i++) {
      w.put(10 + uint64_t(i), 1, leaves[size_t(i)]);
      CsgLeaf l = leaves[size_t(i)];
      l.place = pl;
      m.put(10 + uint64_t(i), 1, l);
    }
    q.put(10, 1, leaves[0]);
    for (int i = n - 1; i > 0; i--) {
      q.put(10 + uint64_t(i), 1, leaves[size_t(i)]);
    }
    BoolOptions opt;
    opt.max_quantum_shift = 14;
    const Out a = test::run(w.t, w.store, opt);
    BoolOptions fine = opt;
    const Out b = test::run(m.t, m.store, fine);
    const Out c = test::run(q.t, q.store, opt);
    runs++;
    const bool plain = a.rep.output.names.ordinals == 0 && a.rep.output.names.collisions == 0 &&
                       a.rep.output.result.stats.diagonals == 0;
    ordinals += !plain;
    UT_CHECK(names(b.verts).size() == names(a.verts).size());
    if (plain && b.rep.output.result.stats.diagonals == 0) {
      UT_CHECK(names(b.verts) == names(a.verts));
      /* (pieces of a face are named by directed coedges, which a mirror turns) */
      if (pl.det() > 0) {
        UT_CHECK(b.faces == a.faces && b.regions == a.regions);
        turned++;
      }
    }
    UT_CHECK(a.regions.size() == c.regions.size());
    all++;
    same_order += names(c.verts) == names(a.verts) && names(c.faces) == names(a.faces);
  }
  printf("invariance: %u runs (%u with ordinals or diagonals), %u of %u the same in another order\n", runs, ordinals,
         same_order, all);
  UT_CHECK(ordinals < 60 && same_order > 120 && turned > 60);
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_window();
  test_door();
  test_trim();
  test_thin();
  test_ordinals();
  test_folded();
  test_curved();
  test_regions();
  test_projected();
  test_grid();
  test_golden();
  test_locality();
  test_invariance();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
