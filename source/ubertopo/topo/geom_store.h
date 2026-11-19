#pragma once

/* The complex's geometry store: its roots and accepted kinds (plan P4a §4.2).
 *
 * Roots are every domain group in stream order plus global; owners add
 * extra roots (the editor's journal). accept_geom_ref is the store's
 * Roots::accept:
 *  - .e.curve holds 3D kinds only (alignment3, catenary3, polyline3, a 3D
 *    bspline);
 *  - .c.pcurve and .f.surface hold nothing in P4a;
 *  - inner refs: offset2.base is a cesaro2, alignment3.horizontal a cesaro2
 *    or offset2, bspline.source any curve kind.
 * The checks read only the kind and, for a bspline, its dimension: no
 * evaluator is built, so collect and hashing stay O(ref rows). */

#include "litestl/attr/pool_store.h"
#include "ubertopo/topo/complex.h"

namespace ubertopo::topo {

/** Group order of the roots (the stream order). */
enum GeomRootGroup : uint32_t {
  kRootVerts,
  kRootEdges,
  kRootCoedges,
  kRootLoops,
  kRootFaces,
  kRootFaceSides,
  kRootShells,
  kRootRegions,
  kRootGlobal,
  kRootCount
};

bool accept_geom_ref(const attr::PoolStore &s, const attr::RefSite &site, attr::PoolRef r);

/** Roots of k's store, for collect, audit, canonical_map, hash and write.
 * Holds pointers into k: build it in place, use it while k is unchanged. */
template <geom::CoordType C> class GeomRoots {
public:
  explicit GeomRoots(const Complex<C> &k, litestl::util::span<attr::RefRoot *const> extra = {})
      : store_(k.geom),
        groups_{&k.verts.group(),  &k.edges.group(),  &k.coedges.group(),
                &k.loops.group(),  &k.faces.group(),  &k.faces.sides().group(),
                &k.shells.group(), &k.regions.group(), &k.global.group()},
        extra_(extra), fn_(*this)
  {
  }
  GeomRoots(const GeomRoots &) = delete;
  GeomRoots &operator=(const GeomRoots &) = delete;

  attr::PoolStore::Roots roots() const
  {
    return {litestl::util::span<const attr::AttrGroup *const>(groups_, kRootCount), extra_, &fn_};
  }
  bool operator()(const attr::RefSite &site, attr::PoolRef r) const
  {
    return accept_geom_ref(store_, site, r);
  }

private:
  const attr::PoolStore &store_;
  const attr::AttrGroup *groups_[kRootCount];
  litestl::util::span<attr::RefRoot *const> extra_;
  attr::PoolStore::AcceptFn fn_;
};

/** The same roots, mutable, for canonicalize. */
template <geom::CoordType C> class GeomMutRoots {
public:
  explicit GeomMutRoots(Complex<C> &k, litestl::util::span<attr::RefRoot *const> extra = {})
      : store_(k.geom),
        groups_{&k.verts.group(),  &k.edges.group(),  &k.coedges.group(),
                &k.loops.group(),  &k.faces.group(),  &k.faces.sides().group(),
                &k.shells.group(), &k.regions.group(), &k.global.group()},
        extra_(extra), fn_(*this)
  {
  }
  GeomMutRoots(const GeomMutRoots &) = delete;
  GeomMutRoots &operator=(const GeomMutRoots &) = delete;

  attr::PoolStore::MutRoots roots() const
  {
    return {litestl::util::span<attr::AttrGroup *const>(groups_, kRootCount), extra_, &fn_};
  }
  bool operator()(const attr::RefSite &site, attr::PoolRef r) const
  {
    return accept_geom_ref(store_, site, r);
  }

private:
  const attr::PoolStore &store_;
  attr::AttrGroup *groups_[kRootCount];
  litestl::util::span<attr::RefRoot *const> extra_;
  attr::PoolStore::AcceptFn fn_;
};

} // namespace ubertopo::topo
