#pragma once

/* Internals shared by io.cc and io_legacy.cc. */

#include <array>
#include <cstdint>

#include "ubertopo/topo/complex.h"

namespace ubertopo::topo::detail {

inline constexpr ident::DomainTag kTopologyHash = ident::DomainTag(263);

template <geom::CoordType C> void hash_frame(const Complex<C> &k, ident::Hasher128 &h)
{
  h.str(geom::CoordTraits<C>::key);
  h.f64_bits(k.frame.origin[0]).f64_bits(k.frame.origin[1]).f64_bits(k.frame.origin[2]);
  h.i32(k.frame.quantum_log2).u64(k.frame.frame_id);
}

/* the generation floors in a fixed domain order (faces carry their sides) */
template <geom::CoordType C> std::array<uint32_t, 7> floors_of(const Complex<C> &k)
{
  return {k.verts.gen_floor(),  k.edges.gen_floor(),  k.coedges.gen_floor(), k.loops.gen_floor(),
          k.faces.gen_floor(),  k.shells.gen_floor(), k.regions.gen_floor()};
}

template <geom::CoordType C> void set_floors(Complex<C> &k, const uint32_t f[7])
{
  k.verts.set_gen_floor(f[0]);
  k.edges.set_gen_floor(f[1]);
  k.coedges.set_gen_floor(f[2]);
  k.loops.set_gen_floor(f[3]);
  k.faces.set_gen_floor(f[4]);
  k.shells.set_gen_floor(f[5]);
  k.regions.set_gen_floor(f[6]);
}

template <geom::CoordType C> void hash_floors(const Complex<C> &k, ident::Hasher128 &h)
{
  const auto fl = floors_of(k);
  bool any = false;
  for (uint32_t f : fl) {
    any = any || f != 0;
  }
  if (!any) {
    return; /* all-zero states hash as before format 2 */
  }
  h.u8(0xF1);
  for (uint32_t f : fl) {
    h.u32(f);
  }
}

template <geom::CoordType C> void rebuild_allocators(Complex<C> &k)
{
  k.verts.rebuild_allocator();
  k.edges.rebuild_allocator();
  k.coedges.rebuild_allocator();
  k.loops.rebuild_allocator();
  k.faces.rebuild_allocator();
  k.shells.rebuild_allocator();
  k.regions.rebuild_allocator();
}

/* ---- formats 1 and 2 (io_legacy.cc) ---- */

/* the old layouts: 4-byte "gref" Geom columns, no .e.range, .e.geom_flags or
 * .g.curve_tol (the other domains are unchanged) */
using LegacyEdgeSchema = Schema<Link2<".e.vs">,
                                Link4<".e.disk">,
                                Link<".e.c">,
                                Container<".e.container">,
                                ContainerHintId<".e.container_hint">,
                                LegacyGeom<".e.curve">,
                                Id,
                                Gen,
                                Live>;

using LegacyCoedgeSchema = Schema<Link<".c.e">,
                                  Link<".c.v">,
                                  Link<".c.l">,
                                  Link<".c.next">,
                                  Link<".c.prev">,
                                  Link<".c.radial_next">,
                                  Link<".c.radial_prev">,
                                  Flags8<".c.flags">,
                                  LegacyGeom<".c.pcurve">,
                                  Gen,
                                  Live>;

using LegacyFaceSchema = Schema<Link<".f.l">,
                                Link2<".f.region">,
                                Link2<".f.shell">,
                                LegacyGeom<".f.surface">,
                                Flags8<".f.flags">,
                                Id,
                                Gen,
                                Live>;

using LegacyGlobalSchema = Schema<AllowsProjectable>;

struct LegacyGroups {
  attr::TypedGroup<LegacyEdgeSchema> edges;
  attr::TypedGroup<LegacyCoedgeSchema> coedges;
  attr::TypedGroup<LegacyFaceSchema> faces;
  attr::TypedGroup<LegacyGlobalSchema> global;
};

/** The nine groups in stream order: the unchanged ones into s, the changed
 * ones into lg. */
template <geom::CoordType C>
bool read_legacy_groups(Complex<C> &s, LegacyGroups &lg, litestl::io::ByteReader &r, const char **err);
template <geom::CoordType C> void write_legacy_groups(const Complex<C> &s, const LegacyGroups &lg, litestl::io::ByteWriter &w);
/** The format-1/2 content hash (s supplies the header terms and floors). */
template <geom::CoordType C> ident::PersistentId legacy_content_hash(const Complex<C> &s, const LegacyGroups &lg);

/** Formats 1 and 2: the stream after magic, format and coordinate key. */
template <geom::CoordType C>
bool read_legacy(Complex<C> &k, litestl::io::ByteReader &r, uint32_t format, const char **err);

} // namespace ubertopo::topo::detail
