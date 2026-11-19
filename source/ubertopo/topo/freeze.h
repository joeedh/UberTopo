#pragma once

/* Freeze and thaw (design 01 §9, plan P2 W10).
 *
 * freeze() compacts every domain and renumbers it canonically:
 *  - vertices, edges and faces by the Morton code of their position, edge
 *    midpoint and face corner centroid, quantised to 21 bits per axis over the
 *    TileFrame's fixed lattice extent (never the data's bounding box), ties by
 *    persistent ID — identical for double and Lattice on a dyadic quantum;
 *  - loops follow faces (in each face's loop order); coedges are contiguous
 *    per loop, in loop order; face sides follow faces (2f + s);
 *  - shells and regions keep build_regions()'s canonical order (region 0
 *    stays at index 0).
 * Rows are permuted, then every index column is remapped (remap.h).
 * Generations: an element that moves gets max(its gen, the slot's old gen)
 * + 1, so a handle never resolves to a different element at the same index;
 * unmoved elements keep their gen and handles. The frozen caches are built.
 *
 * Freezing a frozen, unchanged complex is a no-op. freeze() refuses while
 * regions_dirty is set. thaw() returns an editable copy without caches. */

#include <cstdint>

#include "ubertopo/topo/complex.h"

namespace ubertopo::topo {

/** GeometryInvalid: a geometry ref is dangling, malformed or of a kind its
 * column does not accept (geom_store.h); nothing was changed. */
enum class FreezeResult : uint8_t { Ok, AlreadyFrozen, RegionsDirty, GeometryInvalid };

template <geom::CoordType C> FreezeResult freeze(Complex<C> &k);

/** Rebuilds the frozen caches for the current contents (freeze() and read()
 * call this). */
template <geom::CoordType C> void build_caches(Complex<C> &k);

/** An editable copy: caches dropped, not frozen. */
template <geom::CoordType C> Complex<C> thaw(const Complex<C> &k)
{
  Complex<C> t = k;
  t.caches = FrozenCaches();
  t.frozen_stamp = 0;
  return t;
}

/** The 63-bit Morton code of a point given in lattice quanta (tile frame). */
uint64_t morton_quanta(double x, double y, double z);

extern template FreezeResult freeze<double>(Complex<double> &);
extern template FreezeResult freeze<float>(Complex<float> &);
extern template FreezeResult freeze<geom::Lattice>(Complex<geom::Lattice> &);
extern template void build_caches<double>(Complex<double> &);
extern template void build_caches<float>(Complex<float> &);
extern template void build_caches<geom::Lattice>(Complex<geom::Lattice> &);

} // namespace ubertopo::topo
