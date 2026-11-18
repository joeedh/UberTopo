#pragma once

/* Test fixtures for topo/: grid-keyed builders for boxes and walls, and a
 * whole-complex hash. Vertices are keyed by grid position (so neighbouring
 * boxes weld); faces are keyed by their vertex set (so a wall shared by two
 * boxes is emitted once, with the first emitter's orientation). */

#include <algorithm>
#include <array>
#include <map>
#include <vector>

#include "ubertopo/topo/builder.h"

namespace ubertopo::test {

using I3 = std::array<int64_t, 3>;

inline ident::PersistentId pid(uint64_t lo, uint64_t hi = 0)
{
  return ident::PersistentId{lo, hi};
}

template <geom::CoordType C> geom::Point<C> pt(const I3 &v, int scale_log2 = 0)
{
  if constexpr (std::is_same_v<C, geom::Lattice>) {
    return geom::Point<C>(geom::Lattice(v[0] << scale_log2), geom::Lattice(v[1] << scale_log2),
                          geom::Lattice(v[2] << scale_log2));
  }
  else {
    /* the same geometry: integer * 2^-8 is exact */
    return geom::Point<C>(C(std::ldexp(double(v[0]), scale_log2 - 8)), C(std::ldexp(double(v[1]), scale_log2 - 8)),
                          C(std::ldexp(double(v[2]), scale_log2 - 8)));
  }
}

template <geom::CoordType C> struct GridBuilder {
  topo::Builder<C> b;
  ident::PersistentId root = ident::derive(ident::tag::TestOnly, std::string_view("grid"));
  std::map<I3, uint32_t> vkeys;
  std::map<std::vector<uint32_t>, uint32_t> fkeys;

  uint32_t v(const I3 &p)
  {
    auto it = vkeys.find(p);
    if (it != vkeys.end()) {
      return it->second;
    }
    const uint32_t k = b.vertex(ident::derive_generator(root, "v", p[0], p[1], p[2]), pt<C>(p));
    vkeys[p] = k;
    return k;
  }

  /** A face from loops of grid points; deduplicated by vertex set. Returns
   * the face key. */
  uint32_t face(const std::vector<std::vector<I3>> &loops,
                ident::PersistentId front = {},
                ident::PersistentId back = {},
                uint8_t flags = 0)
  {
    std::vector<uint32_t> keys, sizes, canon;
    for (auto &l : loops) {
      sizes.push_back(uint32_t(l.size()));
      for (auto &p : l) {
        keys.push_back(v(p));
      }
    }
    canon = keys;
    std::sort(canon.begin(), canon.end());
    auto it = fkeys.find(canon);
    if (it != fkeys.end()) {
      return it->second;
    }
    std::vector<int64_t> fid_parts;
    ident::Hasher128 h(ident::tag::TestOnly);
    for (auto &l : loops) {
      for (auto &p : l) {
        h.i64(p[0]).i64(p[1]).i64(p[2]);
      }
      h.u32(0xffffffffu);
    }
    const uint32_t k = b.face(h.finish(), std::span<const uint32_t>(keys), std::span<const uint32_t>(sizes), front,
                              back, flags);
    fkeys[canon] = k;
    return k;
  }

  /** Axis-aligned box [lo, hi] with outward (front) normals. */
  void box(const I3 &lo, const I3 &hi)
  {
    const int64_t x0 = lo[0], y0 = lo[1], z0 = lo[2], x1 = hi[0], y1 = hi[1], z1 = hi[2];
    face({{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}}); /* -z */
    face({{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}); /* +z */
    face({{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}); /* -y */
    face({{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}}); /* +y */
    face({{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}); /* -x */
    face({{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}}); /* +x */
  }
};

/** Hash of every group of a complex, in a fixed order. */
template <geom::CoordType C> ident::PersistentId complex_hash(const topo::Complex<C> &k)
{
  ident::Hasher128 h(ident::tag::TestOnly);
  k.verts.group().hash_into(h);
  k.edges.group().hash_into(h);
  k.coedges.group().hash_into(h);
  k.loops.group().hash_into(h);
  k.faces.group().hash_into(h);
  k.faces.sides().group().hash_into(h);
  k.shells.group().hash_into(h);
  k.regions.group().hash_into(h);
  return h.finish();
}

} // namespace ubertopo::test
