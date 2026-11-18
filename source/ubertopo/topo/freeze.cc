#include "ubertopo/topo/freeze.h"

#include <algorithm>
#include <cmath>

#include "litestl/util/sort.h"
#include "ubertopo/geom/cdt/cdt.h"
#include "ubertopo/topo/radial.h"
#include "ubertopo/topo/remap.h"

namespace ubertopo::topo {

namespace util = litestl::util;
using ident::PersistentId;

namespace {
constexpr uint32_t kDropped = attr::ColumnBase::kDropped;

uint64_t spread21(uint64_t x)
{
  x &= 0x1fffff;
  x = (x | (x << 32)) & 0x1f00000000ffffULL;
  x = (x | (x << 16)) & 0x1f0000ff0000ffULL;
  x = (x | (x << 8)) & 0x100f00f00f00f00fULL;
  x = (x | (x << 4)) & 0x10c30c30c30c30c3ULL;
  x = (x | (x << 2)) & 0x1249249249249249ULL;
  return x;
}

/* lattice quanta in [-2^26, 2^26] -> [0, 2^21) */
uint64_t quant21(double q)
{
  const double lim = double(int64_t(1) << 26);
  q = std::floor(std::min(std::max(q, -lim), lim - 1)) + lim;
  return uint64_t(q) >> 6;
}

template <geom::CoordType C> double quanta(const Complex<C> &k, C v)
{
  if constexpr (std::is_same_v<C, geom::Lattice>) {
    return double(v.raw);
  }
  else {
    return std::ldexp(double(v), -k.frame.quantum_log2);
  }
}

struct Key {
  uint64_t morton;
  PersistentId id;
  uint32_t old;
  friend bool operator<(const Key &a, const Key &b)
  {
    return a.morton != b.morton ? a.morton < b.morton : a.id < b.id;
  }
};

/* old -> new map from keys sorted canonically */
void order_map(util::Vector<Key> &keys, uint32_t old_n, util::Vector<uint32_t> &map)
{
  util::sort_unique_keys(std::span<Key>(keys.data(), keys.size()), [](const Key &a, const Key &b) { return a < b; });
  map.resize(old_n);
  for (uint32_t i = 0; i < old_n; i++) {
    map[i] = kDropped;
  }
  for (uint32_t i = 0; i < keys.size(); i++) {
    map[keys[i].old] = i;
  }
}

/* new gens: an element that moved gets max(own, slot's old) + 1 */
void new_generations(attr::Column<uint32_t> &gen,
                     const util::Vector<uint32_t> &before, /* gen per old index */
                     const util::Vector<uint32_t> &map)
{
  for (uint32_t old = 0; old < map.size(); old++) {
    const uint32_t n = map[old];
    if (n == kDropped || n == old) {
      continue;
    }
    const uint32_t slot_old = n < before.size() ? before[n] : 0;
    gen.set(n, std::max(before[old], slot_old) + 1);
  }
}

template <typename D> util::Vector<uint32_t> gens_of(const D &d)
{
  util::Vector<uint32_t> g;
  for (uint32_t i = 0; i < d.capacity(); i++) {
    g.append(d.template get<".gen">()[i]);
  }
  return g;
}

/* compaction that keeps live slots in index order */
template <typename D> void identity_map(const D &d, util::Vector<uint32_t> &map, uint32_t &n)
{
  map.resize(d.capacity());
  n = 0;
  for (uint32_t i = 0; i < d.capacity(); i++) {
    map[i] = d.live(i) ? n++ : kDropped;
  }
}

} // namespace

uint64_t morton_quanta(double x, double y, double z)
{
  return spread21(quant21(x)) | (spread21(quant21(y)) << 1) | (spread21(quant21(z)) << 2);
}

template <geom::CoordType C> void build_caches(Complex<C> &k)
{
  FrozenCaches c;
  const uint32_t V = k.verts.capacity(), E = k.edges.capacity(), F = k.faces.capacity(), S = k.shells.capacity(),
                 R = k.regions.capacity();
  /* vertex -> edges */
  util::Vector<uint32_t> cnt;
  cnt.resize(V + 1);
  for (uint32_t i = 0; i <= V; i++) {
    cnt[i] = 0;
  }
  for (uint32_t e = 0; e < E; e++) {
    if (k.edges.live(e)) {
      const auto vs = k.edge_verts(EdgeRef(e));
      cnt[vs[0].i + 1]++;
      cnt[vs[1].i + 1]++;
    }
  }
  for (uint32_t i = 0; i < V; i++) {
    cnt[i + 1] += cnt[i];
  }
  c.v2e_off = cnt;
  c.v2e.resize(cnt[V]);
  for (uint32_t e = 0; e < E; e++) {
    if (k.edges.live(e)) {
      const auto vs = k.edge_verts(EdgeRef(e));
      c.v2e[cnt[vs[0].i]++] = e;
      c.v2e[cnt[vs[1].i]++] = e;
    }
  }
  /* shell -> face sides */
  c.shell_off.append(0);
  const auto &sn = k.faces.sides().template get<".fs.shell_next">();
  for (uint32_t s = 0; s < S; s++) {
    if (k.shells.live(s)) {
      util::Vector<uint32_t> sides;
      for (uint32_t fs = k.shells.template get<".s.first">()[s]; fs != NONE && sides.size() <= 2 * F; fs = sn[fs]) {
        sides.append(fs);
      }
      std::sort(sides.begin(), sides.end());
      for (uint32_t x : sides) {
        c.shell_sides.append(x);
      }
    }
    c.shell_off.append(uint32_t(c.shell_sides.size()));
  }
  /* inclusions */
  auto csr = [&](ident::ContainerKind kind, uint32_t n, util::Vector<uint32_t> &off, util::Vector<uint32_t> &out) {
    util::Vector<util::Vector<uint32_t>> lists;
    lists.resize(n);
    for (uint32_t v = 0; v < V; v++) {
      const ident::ContainerRef cr = k.vert_container(VertRef(v));
      if (k.verts.live(v) && cr.kind() == kind && cr.index() < n) {
        lists[cr.index()].append(v << 1);
      }
    }
    for (uint32_t e = 0; e < E; e++) {
      const ident::ContainerRef cr = k.edges.template get<".e.container">()[e];
      if (k.edges.live(e) && cr.kind() == kind && cr.index() < n) {
        lists[cr.index()].append((e << 1) | 1u);
      }
    }
    off.append(0);
    for (uint32_t i = 0; i < n; i++) {
      std::sort(lists[i].begin(), lists[i].end());
      for (uint32_t x : lists[i]) {
        out.append(x);
      }
      off.append(uint32_t(out.size()));
    }
  };
  csr(ident::ContainerKind::Face, F, c.face_incl_off, c.face_incl);
  csr(ident::ContainerKind::Region, R, c.region_incl_off, c.region_incl);
  /* T(f) of projected faces */
  c.tri_off.append(0);
  for (uint32_t f = 0; f < F; f++) {
    util::Vector<geom::Triangle> t;
    if (k.faces.live(f) && face_triangles<C>(k, FaceRef(f), t)) {
      for (auto &x : t) {
        c.tris.append(x);
      }
    }
    c.tri_off.append(uint32_t(c.tris.size()));
  }
  c.stamp = k.version();
  k.caches = std::move(c);
}

template <geom::CoordType C> FreezeResult freeze(Complex<C> &k)
{
  if (k.regions_dirty) {
    return FreezeResult::RegionsDirty;
  }
  if (k.frozen()) {
    return FreezeResult::AlreadyFrozen;
  }
  auto qpos = [&](VertRef v, double out[3]) {
    const auto &p = k.pos(v);
    for (int i = 0; i < 3; i++) {
      out[i] = quanta<C>(k, p[i]);
    }
  };
  RemapMaps maps;
  /* vertices */
  {
    util::Vector<Key> keys;
    for (uint32_t v = 0; v < k.verts.capacity(); v++) {
      if (k.verts.live(v)) {
        double q[3];
        qpos(VertRef(v), q);
        keys.append({morton_quanta(q[0], q[1], q[2]), k.vert_id(VertRef(v)), v});
      }
    }
    order_map(keys, k.verts.capacity(), maps.vertex);
  }
  /* edges: midpoint */
  {
    util::Vector<Key> keys;
    for (uint32_t e = 0; e < k.edges.capacity(); e++) {
      if (k.edges.live(e)) {
        const auto vs = k.edge_verts(EdgeRef(e));
        double a[3], b[3];
        qpos(vs[0], a);
        qpos(vs[1], b);
        keys.append({morton_quanta(0.5 * (a[0] + b[0]), 0.5 * (a[1] + b[1]), 0.5 * (a[2] + b[2])),
                     k.edge_id(EdgeRef(e)), e});
      }
    }
    order_map(keys, k.edges.capacity(), maps.edge);
  }
  /* faces: corner centroid */
  {
    util::Vector<Key> keys;
    for (uint32_t f = 0; f < k.faces.capacity(); f++) {
      if (!k.faces.live(f)) {
        continue;
      }
      double s[3] = {0, 0, 0};
      uint32_t n = 0;
      k.for_each_loop(FaceRef(f), [&](LoopRef l) {
        k.for_each_coedge(l, [&](CoedgeRef c) {
          double q[3];
          qpos(k.start_vert(c), q);
          for (int i = 0; i < 3; i++) {
            s[i] += q[i];
          }
          n++;
        });
      });
      const double d = n ? double(n) : 1.0;
      keys.append({morton_quanta(s[0] / d, s[1] / d, s[2] / d), k.face_id(FaceRef(f)), f});
    }
    order_map(keys, k.faces.capacity(), maps.face);
  }
  /* loops follow faces; coedges contiguous per loop */
  {
    util::Vector<uint32_t> face_order; /* new index -> old face */
    face_order.resize(k.faces.live_count());
    for (uint32_t f = 0; f < k.faces.capacity(); f++) {
      if (maps.face[f] != kDropped) {
        face_order[maps.face[f]] = f;
      }
    }
    maps.loop.resize(k.loops.capacity());
    for (uint32_t i = 0; i < maps.loop.size(); i++) {
      maps.loop[i] = kDropped;
    }
    maps.coedge.resize(k.coedges.capacity());
    for (uint32_t i = 0; i < maps.coedge.size(); i++) {
      maps.coedge[i] = kDropped;
    }
    uint32_t nl = 0, nc = 0;
    for (uint32_t f : face_order) {
      k.for_each_loop(FaceRef(f), [&](LoopRef l) {
        maps.loop[l.i] = nl++;
        k.for_each_coedge(l, [&](CoedgeRef c) { maps.coedge[c.i] = nc++; });
      });
    }
  }
  maps.face_side.resize(2 * k.faces.capacity());
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    const uint32_t n = maps.face[f];
    maps.face_side[2 * f] = n == kDropped ? kDropped : 2 * n;
    maps.face_side[2 * f + 1] = n == kDropped ? kDropped : 2 * n + 1;
  }
  uint32_t ns, nr;
  identity_map(k.shells, maps.shell, ns);
  identity_map(k.regions, maps.region, nr);

  /* generations before */
  const auto gv = gens_of(k.verts), ge = gens_of(k.edges), gc = gens_of(k.coedges), gl = gens_of(k.loops),
             gf = gens_of(k.faces), gs = gens_of(k.shells), gr = gens_of(k.regions);
  auto span_of = [](const util::Vector<uint32_t> &m) { return util::span<const uint32_t>(m.data(), m.size()); };
  k.verts.permute(span_of(maps.vertex), k.verts.live_count());
  k.edges.permute(span_of(maps.edge), k.edges.live_count());
  k.coedges.permute(span_of(maps.coedge), k.coedges.live_count());
  k.loops.permute(span_of(maps.loop), k.loops.live_count());
  k.faces.permute(span_of(maps.face), k.faces.live_count());
  k.shells.permute(span_of(maps.shell), ns);
  k.regions.permute(span_of(maps.region), nr);
  /* remap stored indices */
  remap_group<VertexSchema<C>>(k.verts.typed(), maps);
  remap_group<EdgeSchema>(k.edges.typed(), maps);
  remap_group<CoedgeSchema>(k.coedges.typed(), maps);
  remap_group<LoopSchema>(k.loops.typed(), maps);
  remap_group<FaceSchema>(k.faces.typed(), maps);
  remap_group<FaceSideSchema>(k.faces.sides(), maps);
  remap_group<ShellSchema>(k.shells.typed(), maps);
  remap_group<RegionSchema>(k.regions.typed(), maps);
  /* generations after */
  new_generations(k.verts.template get<".gen">(), gv, maps.vertex);
  new_generations(k.edges.template get<".gen">(), ge, maps.edge);
  new_generations(k.coedges.template get<".gen">(), gc, maps.coedge);
  new_generations(k.loops.template get<".gen">(), gl, maps.loop);
  new_generations(k.faces.template get<".gen">(), gf, maps.face);
  new_generations(k.shells.template get<".gen">(), gs, maps.shell);
  new_generations(k.regions.template get<".gen">(), gr, maps.region);
  build_caches(k);
  k.frozen_stamp = k.version();
  return FreezeResult::Ok;
}

template FreezeResult freeze<double>(Complex<double> &);
template FreezeResult freeze<float>(Complex<float> &);
template FreezeResult freeze<geom::Lattice>(Complex<geom::Lattice> &);
template void build_caches<double>(Complex<double> &);
template void build_caches<float>(Complex<float> &);
template void build_caches<geom::Lattice>(Complex<geom::Lattice> &);

} // namespace ubertopo::topo
