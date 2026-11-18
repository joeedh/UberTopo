#include "ubertopo/topo/compact.h"

#include <algorithm>
#include <cassert>

namespace ubertopo::topo {

namespace util = litestl::util;

namespace {
constexpr uint32_t kDropped = attr::ColumnBase::kDropped;

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

uint32_t kept(const util::Vector<uint32_t> &map)
{
  uint32_t n = 0;
  for (uint32_t x : map) {
    n += x != kDropped;
  }
  return n;
}

template <typename D> bool any_free(const D &d)
{
  return d.live_count() != d.capacity();
}
} // namespace

template <geom::CoordType C> void apply_remap(Complex<C> &k, RemapMaps &maps)
{
  uint32_t ns = 0, nr = 0;
  if (maps.shell.size() == 0) {
    identity_map(k.shells, maps.shell, ns);
  }
  else {
    ns = kept(maps.shell);
  }
  if (maps.region.size() == 0) {
    identity_map(k.regions, maps.region, nr);
  }
  else {
    nr = kept(maps.region);
  }
  /* generations before */
  const auto gv = gens_of(k.verts), ge = gens_of(k.edges), gc = gens_of(k.coedges), gl = gens_of(k.loops),
             gf = gens_of(k.faces), gs = gens_of(k.shells), gr = gens_of(k.regions);
  auto span_of = [](const util::Vector<uint32_t> &m) { return util::span<const uint32_t>(m.data(), m.size()); };
  k.verts.permute(span_of(maps.vertex), kept(maps.vertex));
  k.edges.permute(span_of(maps.edge), kept(maps.edge));
  k.coedges.permute(span_of(maps.coedge), kept(maps.coedge));
  k.loops.permute(span_of(maps.loop), kept(maps.loop));
  k.faces.permute(span_of(maps.face), kept(maps.face));
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
}

template <geom::CoordType C> bool has_tombstones(const Complex<C> &k)
{
  return any_free(k.verts) || any_free(k.edges) || any_free(k.coedges) || any_free(k.loops) || any_free(k.faces) ||
         any_free(k.shells) || any_free(k.regions);
}

template <geom::CoordType C> bool compact(Complex<C> &k, RemapMaps *maps_out)
{
  if (k.frozen()) {
    return false;
  }
  RemapMaps maps;
  uint32_t n;
  identity_map(k.verts, maps.vertex, n);
  identity_map(k.edges, maps.edge, n);
  identity_map(k.coedges, maps.coedge, n);
  identity_map(k.loops, maps.loop, n);
  identity_map(k.faces, maps.face, n);
  maps.face_side.resize(2 * k.faces.capacity());
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    const uint32_t x = maps.face[f];
    maps.face_side[2 * f] = x == kDropped ? kDropped : 2 * x;
    maps.face_side[2 * f + 1] = x == kDropped ? kDropped : 2 * x + 1;
  }
#ifndef NDEBUG
  /* the editor never frees a referenced container */
  auto check = [&](const ident::ContainerRef &c) {
    if (c.kind() == ident::ContainerKind::Face) {
      assert(k.faces.live(c.index()));
    }
    else if (c.kind() == ident::ContainerKind::Region) {
      assert(k.regions.live(c.index()));
    }
  };
  k.verts.for_each_live([&](uint32_t v) { check(k.verts.template get<".v.container">()[v]); });
  k.edges.for_each_live([&](uint32_t e) { check(k.edges.template get<".e.container">()[e]); });
#endif
  apply_remap(k, maps);
  if (maps_out) {
    *maps_out = std::move(maps);
  }
  return true;
}

#define UT_COMPACT_INST(C)                                                                         \
  template void apply_remap<C>(Complex<C> &, RemapMaps &);                                        \
  template bool compact<C>(Complex<C> &, RemapMaps *);                                            \
  template bool has_tombstones<C>(const Complex<C> &);
UT_COMPACT_INST(double)
UT_COMPACT_INST(float)
UT_COMPACT_INST(geom::Lattice)
#undef UT_COMPACT_INST

} // namespace ubertopo::topo
