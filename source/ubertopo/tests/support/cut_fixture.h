#pragma once

/* Shared by the tests that cut faces (test_edit_connect*.cc). */

#include <algorithm>
#include <vector>

#include "edit_fixture.h"

namespace ubertopo::test {

using Loop = std::vector<I3>;
using Loops = std::vector<Loop>;

/* a sheet in the air, with regions */
template <geom::CoordType C> bool sheet(Fixture<C> &fx, const Loops &loops, const Loops &fill = {})
{
  test::GridBuilder<C> g;
  g.face(loops);
  for (const auto &l : fill) {
    g.face({l});
  }
  BuildReport br;
  RegionReport rr;
  return g.b.build(fx.k, br) && build_regions(fx.k, rr);
}

template <geom::CoordType C> bool contained_in(const Complex<C> &k, EdgeRef e, FaceRef f)
{
  const ident::ContainerRef c = k.edges.template get<".e.container">()[e.i];
  return c.kind() == ident::ContainerKind::Face && c.index() == f.i;
}

/* a face's loops as vertex positions, each from its smallest, sorted */
template <geom::CoordType C> std::vector<std::vector<uint32_t>> loops_of(const Complex<C> &k, FaceRef f)
{
  std::vector<std::vector<uint32_t>> out;
  k.for_each_loop(f, [&](LoopRef l) {
    std::vector<uint32_t> v;
    k.for_each_coedge(l, [&](CoedgeRef c) { v.push_back(k.start_vert(c).i); });
    std::rotate(v.begin(), std::min_element(v.begin(), v.end()), v.end());
    out.push_back(v);
  });
  std::sort(out.begin(), out.end());
  return out;
}

template <geom::CoordType C> std::vector<std::vector<uint32_t>> as_loops(const Fixture<C> &fx, const Loops &ls)
{
  std::vector<std::vector<uint32_t>> out;
  for (const Loop &l : ls) {
    std::vector<uint32_t> v;
    for (const I3 &p : l) {
      v.push_back(fx.vert_at(p).i);
    }
    std::rotate(v.begin(), std::min_element(v.begin(), v.end()), v.end());
    out.push_back(v);
  }
  std::sort(out.begin(), out.end());
  return out;
}

template <geom::CoordType C> std::vector<VertRef> verts_at(const Fixture<C> &fx, const Loop &path)
{
  std::vector<VertRef> out;
  for (const I3 &p : path) {
    out.push_back(fx.vert_at(p));
  }
  return out;
}

/* the editor's derived state, and that radial cycles start at the smallest
 * face ID */
template <geom::CoordType C> bool maintained(Editor<C> &ed)
{
  const Complex<C> &k = ed.complex();
  bool heads = true;
  k.edges.for_each_live([&](uint32_t e) {
    k.for_each_radial(EdgeRef(e), [&](CoedgeRef c) {
      heads = heads && !(k.face_id(k.face_of(c)) < k.face_id(k.face_of(k.edge_coedge(EdgeRef(e)))));
    });
  });
  return heads && ed.index_consistent() && ed.grid_consistent() && ed.region_hints_consistent();
}

/* the region of each side of a face, by ID */
template <geom::CoordType C> std::pair<ident::PersistentId, ident::PersistentId> sides_of(const Complex<C> &k, FaceRef f)
{
  const auto &id = k.regions.template get<".id">();
  return {id[k.region(f, 0).i], id[k.region(f, 1).i]};
}

/* a face, a cut through its holes, and the children it makes */
struct CutCase {
  const char *what;
  Loops face, fill;
  Loop path;
  Loops left, right; /* the children's loops */
};

inline std::vector<CutCase> cut_cases()
{
  const Loop outer = {{0, 0, 0}, {40, 0, 0}, {40, 20, 0}, {40, 40, 0}, {0, 40, 0}, {0, 20, 0}};
  const Loop hole = {{10, 10, 0}, {10, 20, 0}, {10, 30, 0}, {30, 30, 0}, {30, 20, 0}, {30, 10, 0}};
  const Loop fill = {{10, 10, 0}, {30, 10, 0}, {30, 20, 0}, {30, 30, 0}, {10, 30, 0}, {10, 20, 0}};
  const Loops up = {{{40, 20, 0}, {40, 40, 0}, {0, 40, 0}, {0, 20, 0}, {10, 20, 0}, {10, 30, 0}, {30, 30, 0}, {30, 20, 0}}};
  const Loops down = {{{0, 20, 0}, {0, 0, 0}, {40, 0, 0}, {40, 20, 0}, {30, 20, 0}, {30, 10, 0}, {10, 10, 0}, {10, 20, 0}}};
  std::vector<CutCase> out;
  out.push_back({"ring", {outer, hole}, {}, {{0, 20, 0}, {10, 20, 0}, {30, 20, 0}, {40, 20, 0}}, up, down});
  out.push_back({"ring, the other way", {outer, hole}, {}, {{40, 20, 0}, {30, 20, 0}, {10, 20, 0}, {0, 20, 0}}, down, up});
  out.push_back({"ring, hole first", {hole, outer}, {}, {{0, 20, 0}, {10, 20, 0}, {30, 20, 0}, {40, 20, 0}}, up, down});
  out.push_back({"filled hole", {outer, hole}, {fill}, {{0, 20, 0}, {10, 20, 0}, {30, 20, 0}, {40, 20, 0}}, up, down});
  /* neighbours on both loops */
  out.push_back({"adjacent ends",
                 {outer, hole},
                 {},
                 {{0, 40, 0}, {10, 30, 0}, {10, 20, 0}, {0, 20, 0}},
                 {{{0, 20, 0}, {0, 0, 0}, {40, 0, 0}, {40, 20, 0}, {40, 40, 0}, {0, 40, 0}, {10, 30, 0}, {30, 30, 0},
                   {30, 20, 0}, {30, 10, 0}, {10, 10, 0}, {10, 20, 0}}},
                 {{{0, 40, 0}, {0, 20, 0}, {10, 20, 0}, {10, 30, 0}}}});
  /* two holes in a row, and one on each side */
  const Loop wide = {{0, 0, 0}, {80, 0, 0}, {80, 20, 0}, {80, 40, 0}, {0, 40, 0}, {0, 20, 0}};
  const Loop h1 = {{10, 16, 0}, {10, 24, 0}, {20, 24, 0}, {20, 16, 0}}, h2 = {{40, 16, 0}, {40, 24, 0}, {50, 24, 0}, {50, 16, 0}};
  const Loop above = {{60, 30, 0}, {60, 34, 0}, {64, 34, 0}, {64, 30, 0}}, below = {{60, 4, 0}, {60, 8, 0}, {64, 8, 0}, {64, 4, 0}};
  out.push_back({"two holes",
                 {wide, above, h1, below, h2},
                 {},
                 {{0, 20, 0}, {10, 24, 0}, {20, 16, 0}, {40, 24, 0}, {50, 16, 0}, {80, 20, 0}},
                 {{{80, 20, 0}, {80, 40, 0}, {0, 40, 0}, {0, 20, 0}, {10, 24, 0}, {20, 24, 0}, {20, 16, 0}, {40, 24, 0},
                   {50, 24, 0}, {50, 16, 0}},
                  above},
                 {{{0, 20, 0}, {0, 0, 0}, {80, 0, 0}, {80, 20, 0}, {50, 16, 0}, {40, 16, 0}, {40, 24, 0}, {20, 16, 0},
                   {10, 16, 0}, {10, 24, 0}},
                  below}});
  /* a concave outer loop, and a cut that turns back */
  const Loop u = {{0, 0, 0}, {60, 0, 0}, {60, 50, 0}, {40, 50, 0}, {40, 20, 0}, {20, 20, 0}, {20, 50, 0}, {0, 50, 0}};
  const Loop hu = {{26, 8, 0}, {26, 14, 0}, {34, 14, 0}, {34, 8, 0}};
  out.push_back({"concave",
                 {u, hu},
                 {},
                 {{20, 20, 0}, {26, 14, 0}, {34, 14, 0}, {40, 20, 0}},
                 {{{40, 20, 0}, {20, 20, 0}, {26, 14, 0}, {34, 14, 0}}},
                 {{{20, 20, 0}, {20, 50, 0}, {0, 50, 0}, {0, 0, 0}, {60, 0, 0}, {60, 50, 0}, {40, 50, 0}, {40, 20, 0},
                   {34, 14, 0}, {34, 8, 0}, {26, 8, 0}, {26, 14, 0}}}});
  /* two U-shaped holes, each with a hole in its notch, which the cut's chords
   * don't separate from the lower child but the holes do */
  const auto shifted = [](Loop l, int64_t x) {
    for (I3 &p : l) {
      p[0] += x;
    }
    return l;
  };
  const Loop long_plate = {{0, 0, 0}, {160, 0, 0}, {160, 30, 0}, {160, 40, 0}, {0, 40, 0}, {0, 30, 0}};
  const Loop u1 = {{20, 10, 0}, {20, 30, 0}, {28, 30, 0}, {28, 16, 0}, {52, 16, 0}, {52, 30, 0}, {60, 30, 0}, {60, 10, 0}};
  const Loop n1 = {{36, 20, 0}, {36, 24, 0}, {40, 24, 0}, {40, 20, 0}};
  out.push_back({"notches",
                 {long_plate, u1, n1, shifted(u1, 80), shifted(n1, 80)},
                 {},
                 {{0, 30, 0}, {20, 30, 0}, {60, 30, 0}, {100, 30, 0}, {140, 30, 0}, {160, 30, 0}},
                 {{{160, 30, 0}, {160, 40, 0}, {0, 40, 0}, {0, 30, 0}, {20, 30, 0}, {28, 30, 0}, {28, 16, 0}, {52, 16, 0},
                   {52, 30, 0}, {60, 30, 0}, {100, 30, 0}, {108, 30, 0}, {108, 16, 0}, {132, 16, 0}, {132, 30, 0},
                   {140, 30, 0}},
                  n1,
                  shifted(n1, 80)},
                 {{{0, 30, 0}, {0, 0, 0}, {160, 0, 0}, {160, 30, 0}, {140, 30, 0}, {140, 10, 0}, {100, 10, 0}, {100, 30, 0},
                   {60, 30, 0}, {60, 10, 0}, {20, 10, 0}, {20, 30, 0}}}});
  return out;
}

} // namespace ubertopo::test
