#pragma once

/* Compaction and the shared renumbering step (plan P3b W2).
 *
 * apply_remap() permutes every domain by the given old -> new maps, rewrites
 * every stored index (remap.h) and assigns generations: an element that
 * moves gets max(its gen, the slot's old gen) + 1, so a handle never
 * resolves to a different element at the same index; slots dropped off the
 * end raise the domain's generation floor (domain.h). freeze() builds Morton
 * maps and calls it; compact() calls it with identity maps.
 *
 * compact() drops the tombstones of every domain and keeps live slots in
 * index order (face sides follow faces). It refuses a frozen complex (which
 * is compact already) and asserts that no container names a dropped
 * element. Regions and their dirty state are unaffected. */

#include <cstdint>

#include "ubertopo/topo/complex.h"
#include "ubertopo/topo/remap.h"

namespace ubertopo::topo {

/** old -> new for a domain kept in index order; @p n receives the live count.
 * An empty map means "identity" to apply_remap. */
template <typename D> void identity_map(const D &d, litestl::util::Vector<uint32_t> &map, uint32_t &n)
{
  map.resize(d.capacity());
  n = 0;
  for (uint32_t i = 0; i < d.capacity(); i++) {
    map[i] = d.live(i) ? n++ : attr::ColumnBase::kDropped;
  }
}

/** Applies @p maps (vertex, edge, coedge, loop, face, face_side; shell and
 * region may be empty, meaning the identity over live slots). */
template <geom::CoordType C> void apply_remap(Complex<C> &k, RemapMaps &maps);

/** Drops every tombstone; returns false (and changes nothing) on a frozen
 * complex. @p maps_out, if given, receives the maps applied. */
template <geom::CoordType C> bool compact(Complex<C> &k, RemapMaps *maps_out = nullptr);

/** True if some domain has tombstones (compact() would change something). */
template <geom::CoordType C> bool has_tombstones(const Complex<C> &k);

#define UT_COMPACT_EXTERN(C)                                                                       \
  extern template void apply_remap<C>(Complex<C> &, RemapMaps &);                                 \
  extern template bool compact<C>(Complex<C> &, RemapMaps *);                                     \
  extern template bool has_tombstones<C>(const Complex<C> &);
UT_COMPACT_EXTERN(double)
UT_COMPACT_EXTERN(float)
UT_COMPACT_EXTERN(geom::Lattice)
#undef UT_COMPACT_EXTERN

} // namespace ubertopo::topo
