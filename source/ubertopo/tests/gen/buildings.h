#pragma once

/* Deterministic building generators with ground truth (plan P2 W9).
 *
 * Every face side is emitted with a region hint: the generator's own label of
 * the region on that side (a room, a wall solid, a pillar, ...), or the
 * unbounded hint for the outside. build_regions() computes regions from
 * geometry alone; the tests then require the computed partition of face sides
 * to equal the generator's labelling exactly (no hint conflicts, one label per
 * region, one region per label) and the counts to match.
 *
 * Construction rules (review 16):
 *  - vertices are keyed by grid position, so neighbouring rooms weld;
 *  - all rooms of a grid share one lattice of wall planes, so shared walls are
 *    identical faces (emitted once) and no T-junctions arise;
 *  - coordinates are integers, used directly by Lattice and scaled by 2^-8
 *    for double (exact), so both instantiations build the same geometry. */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../support/splitmix64.h"
#include "ubertopo/ident/id.h"
#include "ubertopo/topo/builder.h"

namespace ubertopo::test::gen {

using I3 = std::array<int64_t, 3>;
using ident::PersistentId;

template <geom::CoordType C> geom::Point<C> gpt(const I3 &v)
{
  if constexpr (std::is_same_v<C, geom::Lattice>) {
    return geom::Point<C>(geom::Lattice(v[0]), geom::Lattice(v[1]), geom::Lattice(v[2]));
  }
  else {
    return geom::Point<C>(C(std::ldexp(double(v[0]), -8)), C(std::ldexp(double(v[1]), -8)),
                          C(std::ldexp(double(v[2]), -8)));
  }
}

/** Ground truth of a generated model. */
struct Truth {
  std::set<PersistentId> labels;   /* bounded region labels (unbounded excluded) */
  uint32_t sheet_faces = 0;        /* faces with both sides in one region */
  std::vector<PersistentId> vertex_containers; /* expected container label per contained vertex id */
  std::vector<PersistentId> contained_vertex_ids;
};

template <geom::CoordType C> struct Model {
  topo::Builder<C> b;
  Truth truth;
  PersistentId root;
  std::map<I3, uint32_t> vkeys;
  std::map<std::vector<uint32_t>, uint32_t> fkeys; /* by vertex set: shared walls once */
  topo::BuildPolicy policy;

  /* the frame quantum matches the double scaling (2^-8), so Morton codes and
   * therefore frozen orders agree between double and Lattice */
  static geom::TileFrame frame()
  {
    geom::TileFrame f;
    f.quantum_log2 = -8;
    return f;
  }
  explicit Model(PersistentId r) : b(frame()), root(r) {}

  static PersistentId outside()
  {
    return ident::unbounded_region_id();
  }
  PersistentId label(std::string_view what, int64_t a = 0, int64_t b = 0, int64_t c = 0)
  {
    const PersistentId id = ident::derive_generator(root, "region", what, a, b, c);
    truth.labels.insert(id);
    return id;
  }

  uint32_t v(const I3 &p)
  {
    auto it = vkeys.find(p);
    if (it != vkeys.end()) {
      return it->second;
    }
    const uint32_t k = b.vertex(ident::derive_generator(ident::PersistentId{}, "v", p[0], p[1], p[2]), gpt<C>(p));
    vkeys[p] = k;
    return k;
  }

  /** A face from loops of grid points, with region labels for its front and
   * back sides. A face already emitted (same vertex set) is not emitted again:
   * the caller must give the same labels from either side, which is how
   * shared walls get both their rooms. */
  uint32_t face(const std::vector<std::vector<I3>> &loops, PersistentId front, PersistentId back, uint8_t flags = 0)
  {
    std::vector<uint32_t> keys, sizes, canon;
    ident::Hasher128 h(ident::tag::TestOnly);
    for (auto &l : loops) {
      sizes.push_back(uint32_t(l.size()));
      for (auto &p : l) {
        keys.push_back(v(p));
        h.i64(p[0]).i64(p[1]).i64(p[2]);
      }
      h.u32(0xffffffffu);
    }
    canon = keys;
    std::sort(canon.begin(), canon.end());
    auto it = fkeys.find(canon);
    if (it != fkeys.end()) {
      return it->second;
    }
    if (front == back) {
      truth.sheet_faces++;
    }
    const uint32_t k = b.face(ident::derive_generator(root, "face", h.finish()), std::span<const uint32_t>(keys),
                              std::span<const uint32_t>(sizes), front, back, flags);
    fkeys[canon] = k;
    return k;
  }

  /** The six faces of an axis-aligned box. @p in labels the inside, @p out the
   * outside; front sides face outward. A face shared with an earlier box is
   * re-labelled on the side that box left as "outside" (see room_grid). */
  void box(const I3 &lo, const I3 &hi, PersistentId in, PersistentId out)
  {
    const int64_t x0 = lo[0], y0 = lo[1], z0 = lo[2], x1 = hi[0], y1 = hi[1], z1 = hi[2];
    face({{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}}, out, in);
    face({{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, out, in);
    face({{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, out, in);
    face({{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}}, out, in);
    face({{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}, out, in);
    face({{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}}, out, in);
  }
};

/** Options of room_grid. */
struct GridOpts {
  int nx = 2, ny = 2, floors = 1;
  int64_t cell = 400, height = 300;
  bool thick = false;
  int64_t wall = 20;    /* thick walls only */
  int windows = 0;      /* thin walls: exterior wall windows, on the first rooms */
  bool glazing = true;  /* glazed (aperture face) or open */
};

/** A grid of rooms.
 *
 * Thin walls: each room is a cell of a shared lattice; a wall between two
 * rooms is one face with a room on each side. Windows are square holes in
 * +y exterior walls; with glazing an aperture face fills the hole (it
 * separates room and outside), without it the room joins the outside region.
 *
 * Thick walls: the building is a solid box (the wall region) with every room
 * a box-shaped cavity inset by the wall thickness; floors are slabs of the
 * same solid. */
template <geom::CoordType C> void room_grid(Model<C> &m, const I3 &origin, const GridOpts &o, int64_t bid = 0)
{
  const PersistentId out = m.outside();
  const int64_t W = o.cell, H = o.height;
  if (o.thick) {
    const PersistentId wall = m.label("wall", bid);
    const int64_t t = o.wall;
    const I3 lo = origin, hi = {origin[0] + o.nx * W + t, origin[1] + o.ny * W + t, origin[2] + o.floors * H + t};
    m.box(lo, hi, wall, out);
    for (int f = 0; f < o.floors; f++) {
      for (int i = 0; i < o.nx; i++) {
        for (int j = 0; j < o.ny; j++) {
          const PersistentId room = m.label("room", bid, i + 100 * j, f);
          const I3 rlo = {origin[0] + i * W + t, origin[1] + j * W + t, origin[2] + f * H + t};
          const I3 rhi = {origin[0] + (i + 1) * W, origin[1] + (j + 1) * W, origin[2] + (f + 1) * H};
          /* room faces: front toward the wall solid (outward from the room) */
          m.box(rlo, rhi, room, wall);
        }
      }
    }
    return;
  }
  /* thin walls */
  int windows_left = o.windows;
  std::map<std::array<int, 3>, PersistentId> room_label;
  for (int f = 0; f < o.floors; f++) {
    for (int i = 0; i < o.nx; i++) {
      for (int j = 0; j < o.ny; j++) {
        const bool window = windows_left > 0 && j == o.ny - 1;
        windows_left -= window ? 1 : 0;
        /* an open window merges the room into the outside */
        room_label[{i, j, f}] = (window && !o.glazing) ? out : m.label("room", bid, i + 100 * j, f);
        if (window && !o.glazing) {
          m.truth.labels.erase(m.label("room", bid, i + 100 * j, f));
        }
      }
    }
  }
  auto lab = [&](int i, int j, int f) {
    auto it = room_label.find({i, j, f});
    return it == room_label.end() ? out : it->second;
  };
  windows_left = o.windows;
  for (int f = 0; f < o.floors; f++) {
    for (int i = 0; i < o.nx; i++) {
      for (int j = 0; j < o.ny; j++) {
        const int64_t x0 = origin[0] + i * W, y0 = origin[1] + j * W, z0 = origin[2] + f * H;
        const int64_t x1 = x0 + W, y1 = y0 + W, z1 = z0 + H;
        const PersistentId in = lab(i, j, f);
        /* each wall: front side faces the -/+ neighbour as below; labels from both cells */
        m.face({{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}}, lab(i, j, f - 1), in); /* -z: front below */
        m.face({{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, lab(i, j, f + 1), in); /* +z */
        m.face({{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, lab(i, j - 1, f), in); /* -y */
        m.face({{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}, lab(i - 1, j, f), in); /* -x */
        m.face({{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}}, lab(i + 1, j, f), in); /* +x */
        const bool window = windows_left > 0 && j == o.ny - 1;
        windows_left -= window ? 1 : 0;
        if (!window) {
          m.face({{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}}, lab(i, j + 1, f), in); /* +y */
        }
        else {
          /* +y exterior wall with a square hole (CW seen from the front, +y) */
          const int64_t a = x0 + W / 4, b = x1 - W / 4, c = z0 + H / 4, d = z1 - H / 4;
          m.face({{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}, {{a, y1, c}, {b, y1, c}, {b, y1, d}, {a, y1, d}}},
                 out, in);
          if (o.glazing) {
            m.face({{{a, y1, c}, {a, y1, d}, {b, y1, d}, {b, y1, c}}}, out, in, topo::face_flags::Aperture);
          }
        }
      }
    }
  }
}

/** A ring-shaped single room around an open courtyard: floor and roof are
 * faces with a hole; the room region is a solid torus (genus 1). */
template <geom::CoordType C> void courtyard(Model<C> &m, const I3 &o, int64_t bid = 0)
{
  const PersistentId out = m.outside(), room = m.label("ring", bid);
  const int64_t A = 900, a0 = 300, a1 = 600, H = 300;
  auto x = [&](int64_t v) { return o[0] + v; };
  auto y = [&](int64_t v) { return o[1] + v; };
  const int64_t z0 = o[2], z1 = o[2] + H;
  /* floor (front -z) and roof (front +z), each with the courtyard hole */
  m.face({{{x(0), y(0), z0}, {x(0), y(A), z0}, {x(A), y(A), z0}, {x(A), y(0), z0}},
          {{x(a0), y(a0), z0}, {x(a1), y(a0), z0}, {x(a1), y(a1), z0}, {x(a0), y(a1), z0}}},
         out, room);
  m.face({{{x(0), y(0), z1}, {x(A), y(0), z1}, {x(A), y(A), z1}, {x(0), y(A), z1}},
          {{x(a0), y(a0), z1}, {x(a0), y(a1), z1}, {x(a1), y(a1), z1}, {x(a1), y(a0), z1}}},
         out, room);
  /* outer walls, front outward */
  m.face({{{x(0), y(0), z0}, {x(A), y(0), z0}, {x(A), y(0), z1}, {x(0), y(0), z1}}}, out, room);
  m.face({{{x(0), y(A), z0}, {x(0), y(A), z1}, {x(A), y(A), z1}, {x(A), y(A), z0}}}, out, room);
  m.face({{{x(0), y(0), z0}, {x(0), y(0), z1}, {x(0), y(A), z1}, {x(0), y(A), z0}}}, out, room);
  m.face({{{x(A), y(0), z0}, {x(A), y(A), z0}, {x(A), y(A), z1}, {x(A), y(0), z1}}}, out, room);
  /* courtyard walls, front toward the courtyard (outside) */
  m.face({{{x(a0), y(a0), z0}, {x(a0), y(a0), z1}, {x(a1), y(a0), z1}, {x(a1), y(a0), z0}}}, out, room);
  m.face({{{x(a0), y(a1), z0}, {x(a1), y(a1), z0}, {x(a1), y(a1), z1}, {x(a0), y(a1), z1}}}, out, room);
  m.face({{{x(a0), y(a0), z0}, {x(a0), y(a1), z0}, {x(a0), y(a1), z1}, {x(a0), y(a0), z1}}}, out, room);
  m.face({{{x(a1), y(a0), z0}, {x(a1), y(a0), z1}, {x(a1), y(a1), z1}, {x(a1), y(a1), z0}}}, out, room);
}

/** A thin-walled room with a free-standing pillar (a solid not touching it). */
template <geom::CoordType C> void pillar_room(Model<C> &m, const I3 &o, int64_t bid = 0)
{
  const PersistentId out = m.outside(), room = m.label("proom", bid), pillar = m.label("pillar", bid);
  m.box(o, {o[0] + 600, o[1] + 600, o[2] + 300}, room, out);
  m.box({o[0] + 250, o[1] + 250, o[2] + 50}, {o[0] + 350, o[1] + 350, o[2] + 250}, pillar, room);
}

/** Three levels of nesting: a room holding a solid pillar holding a sealed
 * void. The pillar's cavity shell lies inside both the pillar and the room,
 * so only the *innermost* containing shell gives the right region. */
template <geom::CoordType C> void nested_solids(Model<C> &m, const I3 &o, int64_t bid = 0)
{
  const PersistentId out = m.outside(), room = m.label("nroom", bid), pillar = m.label("npillar", bid),
                     voidr = m.label("nvoid", bid);
  m.box(o, {o[0] + 900, o[1] + 900, o[2] + 600}, room, out);
  m.box({o[0] + 200, o[1] + 200, o[2] + 100}, {o[0] + 700, o[1] + 700, o[2] + 500}, pillar, room);
  m.box({o[0] + 350, o[1] + 300, o[2] + 200}, {o[0] + 550, o[1] + 600, o[2] + 400}, voidr, pillar);
}

/** A solid block with a sealed void inside it. */
template <geom::CoordType C> void sealed_cavity(Model<C> &m, const I3 &o, int64_t bid = 0)
{
  const PersistentId out = m.outside(), solid = m.label("solid", bid), voidr = m.label("void", bid);
  m.box(o, {o[0] + 500, o[1] + 500, o[2] + 500}, solid, out);
  m.box({o[0] + 100, o[1] + 100, o[2] + 100}, {o[0] + 300, o[1] + 400, o[2] + 200}, voidr, solid);
}

/** A fence: a strip of vertical panels sharing edges, both sides outside. */
template <geom::CoordType C> void fence_in_air(Model<C> &m, const I3 &o, int panels = 3)
{
  const PersistentId out = m.outside();
  for (int i = 0; i < panels; i++) {
    const int64_t x0 = o[0] + 200 * i, x1 = x0 + 200;
    m.face({{{x0, o[1], o[2]}, {x1, o[1], o[2]}, {x1, o[1], o[2] + 150}, {x0, o[1], o[2] + 150}}}, out, out);
  }
}

/** A room with a sensor vertex inside it, and a power-line wire in the air. */
template <geom::CoordType C> void sensor_and_wire(Model<C> &m, const I3 &o, int64_t bid = 0)
{
  const PersistentId out = m.outside(), room = m.label("sroom", bid);
  m.box(o, {o[0] + 400, o[1] + 400, o[2] + 300}, room, out);
  const PersistentId sid = ident::derive_generator(m.root, "sensor", bid);
  m.b.vertex(sid, gpt<C>({o[0] + 123, o[1] + 77, o[2] + 201}), topo::ContainerHint::in_region(room));
  m.truth.contained_vertex_ids.push_back(sid);
  m.truth.vertex_containers.push_back(room);
  const uint32_t a = m.b.vertex(ident::derive_generator(m.root, "pole", bid, 0), gpt<C>({o[0] - 300, o[1], o[2] + 500}),
                                topo::ContainerHint::in_region(out));
  const uint32_t c = m.b.vertex(ident::derive_generator(m.root, "pole", bid, 1),
                                gpt<C>({o[0] - 300, o[1] + 900, o[2] + 520}), topo::ContainerHint::in_region(out));
  m.b.wire(ident::derive_generator(m.root, "wire", bid), a, c, topo::ContainerHint::in_region(out));
}

/** A thin-walled room with a sloped, non-planar roof: one roof corner is
 * lifted by @p lift quanta (snapped roof). Needs allow_projectable_faces. */
template <geom::CoordType C> void sloped_roof(Model<C> &m, const I3 &o, int64_t lift, int64_t bid = 0)
{
  const PersistentId out = m.outside(), room = m.label("roofroom", bid);
  const int64_t A = 500, Hh = 300;
  const I3 p000 = o, p100 = {o[0] + A, o[1], o[2]}, p110 = {o[0] + A, o[1] + A, o[2]}, p010 = {o[0], o[1] + A, o[2]};
  const I3 q000 = {o[0], o[1], o[2] + Hh}, q100 = {o[0] + A, o[1], o[2] + Hh},
           q110 = {o[0] + A, o[1] + A, o[2] + Hh + lift}, q010 = {o[0], o[1] + A, o[2] + Hh};
  m.face({{p000, p010, p110, p100}}, out, room);
  m.face({{q000, q100, q110, q010}}, out, room); /* roof: non-planar when lift != 0 */
  m.face({{p000, p100, q100, q000}}, out, room);
  m.face({{p010, q010, q110, p110}}, out, room);
  m.face({{p000, q000, q010, p010}}, out, room);
  m.face({{p100, p110, q110, q100}}, out, room);
  m.policy.allow_projectable_faces = true;
}

/** A random city: lots on a grid, each holding one family with random
 * parameters, separated so buildings never touch. */
template <geom::CoordType C> void random_city(Model<C> &m, uint64_t seed, int n_buildings)
{
  litestl::test::SplitMix64 r(seed);
  const int side = int(std::ceil(std::sqrt(double(n_buildings))));
  const int64_t lot = 4000;
  for (int b = 0; b < n_buildings; b++) {
    const I3 o = {int64_t(b % side) * lot, int64_t(b / side) * lot, 0};
    const int family = int(r.below(8));
    GridOpts g;
    switch (family) {
      case 0:
      case 1:
        g.nx = 1 + int(r.below(4));
        g.ny = 1 + int(r.below(3));
        g.floors = 1 + int(r.below(3));
        g.windows = int(r.below(3));
        g.glazing = r.coin();
        room_grid(m, o, g, b);
        break;
      case 2:
        g.nx = 1 + int(r.below(3));
        g.ny = 1 + int(r.below(3));
        g.floors = 1 + int(r.below(2));
        g.thick = true;
        room_grid(m, o, g, b);
        break;
      case 3:
        courtyard(m, o, b);
        break;
      case 4:
        pillar_room(m, o, b);
        break;
      case 5:
        sealed_cavity(m, o, b);
        break;
      case 6:
        nested_solids(m, o, b);
        break;
      default:
        sensor_and_wire(m, {o[0] + 500, o[1], o[2]}, b);
        fence_in_air(m, {o[0] + 1500, o[1] + 1500, 0}, 1 + int(r.below(4)));
        break;
    }
  }
}

} // namespace ubertopo::test::gen
