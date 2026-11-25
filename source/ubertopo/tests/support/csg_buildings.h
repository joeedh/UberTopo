#pragma once

/* Buildings as CSG trees, for the Boolean engine's tests and measurements:
 * thick walls, rooms, windows, a gabled roof with a dormer, a chimney and
 * skylights. Dimensions are random integers (quanta), so most cuts of the
 * roof end between the lattice's points. */

#include <algorithm>

#include "csg_scenes.h"
#include "ubertopo/csg/select.h"

namespace ubertopo::test {

struct Building {
  Operands o;
  csg::Expression e;
  uint32_t wall, roof, stone, air;

  Building()
  {
    wall = e.add_label(csg::LabelKind::Solid);
    roof = e.add_label(csg::LabelKind::Solid);
    stone = e.add_label(csg::LabelKind::Solid);
    air = e.add_label(csg::LabelKind::Air);
  }

  uint32_t leaf(const GridBuilder<L> &g, uint32_t label, bool thin = false, const topo::BuildPolicy &policy = {})
  {
    o.add(g, policy);
    std::vector<uint32_t> l(o.k.back()->regions.capacity(), label);
    l[0] = csg::kVoid;
    return e.add_leaf(thin, l);
  }
  uint32_t box(const I3 &lo, const I3 &hi, uint32_t label, bool thin = false)
  {
    GridBuilder<L> g;
    g.box(lo, hi);
    return leaf(g, label, thin);
  }
  /** A prism over a convex polygon (u, v) counter-clockwise, along w = axis;
   * (u, v, w) right-handed. */
  uint32_t prism(const std::vector<std::array<int64_t, 2>> &poly, int axis, int64_t w0, int64_t w1, uint32_t label)
  {
    GridBuilder<L> g;
    auto p = [&](size_t i, int64_t w) {
      I3 q;
      q[(axis + 1) % 3] = poly[i % poly.size()][0];
      q[(axis + 2) % 3] = poly[i % poly.size()][1];
      q[axis] = w;
      return q;
    };
    std::vector<I3> top, bottom;
    for (size_t i = 0; i < poly.size(); i++) {
      top.push_back(p(i, w1));
      bottom.push_back(p(poly.size() - 1 - i, w0));
      g.face({{p(i, w0), p(i + 1, w0), p(i + 1, w1), p(i, w1)}});
    }
    g.face({top});
    g.face({bottom});
    return leaf(g, label);
  }
};

struct BuildingOptions {
  int64_t metre = 8192; /* quanta */
  int windows = 4;      /* per side and floor */
  int floors = 2;
  bool roof = true;
};

inline void make_building(Building &b, SplitMix64 &r, const BuildingOptions &opt)
{
  const int64_t m = opt.metre;
  /* a length in metres (tenths), plus some quanta. (Arguments of a function
   * are evaluated in any order: one call per statement or list element.) */
  auto len = [&](int64_t tenths) { return tenths * m / 10 + int64_t(r.below(uint64_t(m / 50 + 2))); };
  const int64_t w = len(80 + int64_t(r.below(60)));
  const int64_t d = len(60 + int64_t(r.below(40)));
  const int64_t h = len(27);
  const int64_t t = len(3), top = h * opt.floors;
  std::vector<uint32_t> cut = {b.box({0, 0, 0}, {w, d, top}, b.wall)};
  auto add = [&](std::vector<uint32_t> &to, const I3 &lo, const I3 &size, uint32_t label) {
    to.push_back(b.box(lo, {lo[0] + size[0], lo[1] + size[1], lo[2] + size[2]}, label));
  };
  for (int f = 0; f < opt.floors; f++) {
    const int64_t z0 = f * h + len(2);
    const int64_t z1 = (f + 1) * h - len(1);
    const int64_t wx = len(30 + int64_t(r.below(20)));
    const int64_t wall = len(1);
    /* two rooms, with a door between them */
    add(cut, {t, t, z0}, {wx - t, d - 2 * t, z1 - z0}, b.wall);
    add(cut, {wx + wall, t, z0}, {w - t - wx - wall, d - 2 * t, z1 - z0}, b.wall);
    const I3 door = {3 * wall, len(9), len(20)};
    add(cut, {wx - wall, d / 3, z0}, door, b.wall);
    for (int side = 0; side < 4; side++) {
      const int64_t length = side < 2 ? w : d;
      for (int i = 0; i < opt.windows; i++) {
        const int64_t c = length * (2 * i + 1) / (2 * opt.windows) + len(int64_t(r.below(3)));
        const int64_t width = len(8), sill = z0 + len(8), height = len(10), out = len(1);
        const int64_t in = t - int64_t(r.below(2)) * wall; /* (blind ones too) */
        const int64_t across = side == 0 || side == 2 ? -out : (side == 1 ? d : w) - in;
        if (side < 2) {
          add(cut, {c - width / 2, across, sill}, {width, in + out, height}, b.wall);
        }
        else {
          add(cut, {across, c - width / 2, sill}, {in + out, width, height}, b.wall);
        }
      }
    }
  }
  std::vector<uint32_t> all = {b.e.add_op(csg::Op::Difference, cut)};
  if (opt.roof) {
    const int64_t over = len(4);
    const int64_t rise = len(25 + int64_t(r.below(20)));
    const int64_t mid = d / 2 + len(int64_t(r.below(8)));
    /* the gable: a prism along x over (y, z) */
    std::vector<uint32_t> roof = {
        b.prism({{-over, top}, {d + over, top}, {mid, top + rise}}, 0, -over, w + over, b.roof)};
    /* a skylight straight down, and a tilted cutter */
    const I3 sky = {len(10), len(8), top + len(1)};
    const I3 sky_size = {len(7), len(9), 2 * rise};
    add(roof, sky, sky_size, b.roof);
    const int64_t cx = len(25), tilt = len(12), low = top + len(5), back = mid - len(5);
    roof.push_back(
        b.prism({{cx, low + m / 10}, {cx + m, low}, {cx + tilt, top + 2 * rise}}, 1, -over - m, back, b.roof));
    all.push_back(b.e.add_op(csg::Op::Difference, roof));
    /* a dormer along y over (z, x), and a chimney with a flue */
    const int64_t dx = w - len(35), dz = top + rise / 3, half = len(8), peak = len(9);
    all.push_back(b.prism({{dz, dx}, {dz + peak, dx + half}, {dz, dx + 2 * half + 1}}, 1, -over / 2, mid, b.roof));
    const I3 at = {w / 2, mid - len(6), top - len(5)};
    const I3 size = {len(8), len(7), rise + len(13)};
    const I3 flue = {at[0] + len(2), at[1] + len(2), at[2]};
    const I3 flue_size = {len(3), len(2), size[2] + len(1)};
    std::vector<uint32_t> chimney;
    add(chimney, at, size, b.stone);
    add(chimney, flue, flue_size, b.stone);
    all.push_back(b.e.add_op(csg::Op::Difference, chimney));
  }
  b.e.add_op(csg::Op::Union, all);
}

} // namespace ubertopo::test
