#pragma once

/* Legacy UTCX streams (formats 1 and 2) captured before the P4a geometry
 * store (plan P4a §4.3), and the structure hash that must survive their
 * conversion to format 3.
 *
 * The structure hash covers the header fields, the generation floors and
 * every group with the geometry columns left out (.e.curve, .c.pcurve,
 * .f.surface and the P4a columns .e.range, .e.geom_flags, .g.curve_tol),
 * so the same code computes the same value before and after the switch. It
 * hashes the kept columns itself: AttrGroup's filtered hash also covers the
 * group's builtin count, which the switch changes. */

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "ubertopo/topo/complex.h"

namespace ubertopo::test {

struct LegacyFixture {
  const char *name;
  const char *coord; /* coordinate key */
  uint32_t format;
  const uint8_t *bytes;
  size_t size;
  uint64_t content_lo, content_hi;     /* content_hash in the old code */
  uint64_t topology_lo, topology_hi;   /* topology_hash in the old code */
  uint64_t structure_lo, structure_hi; /* structure_hash (both codes) */
  uint32_t combinatorial_issues, global_issues;
};

inline bool is_geometry_column(std::string_view n)
{
  return n == ".e.curve" || n == ".c.pcurve" || n == ".f.surface" || n == ".e.range" || n == ".e.geom_flags" ||
         n == ".g.curve_tol";
}

/* @p coord_free leaves out the coordinate key and "pos" too: equal for the
 * same edits on complexes of different coordinate types */
template <geom::CoordType C> ident::PersistentId structure_hash(const topo::Complex<C> &k, bool coord_free = false)
{
  ident::Hasher128 h(ident::DomainTag::Content);
  if (!coord_free) {
    h.str(geom::CoordTraits<C>::key);
  }
  h.f64_bits(k.frame.origin[0]).f64_bits(k.frame.origin[1]).f64_bits(k.frame.origin[2]);
  h.i32(k.frame.quantum_log2).u64(k.frame.frame_id);
  h.boolean(k.regions_dirty);
  for (uint32_t f : {k.verts.gen_floor(), k.edges.gen_floor(), k.coedges.gen_floor(), k.loops.gen_floor(),
                     k.faces.gen_floor(), k.shells.gen_floor(), k.regions.gen_floor()})
  {
    h.u32(f);
  }
  for (const litestl::attr::AttrGroup *g :
       {&k.verts.group(), &k.edges.group(), &k.coedges.group(), &k.loops.group(), &k.faces.group(),
        &k.faces.sides().group(), &k.shells.group(), &k.regions.group(), &k.global.group()})
  {
    h.u32(g->size());
    for (int i : g->canonical_order()) {
      const litestl::attr::ColumnBase &c = g->column(uint32_t(i));
      using litestl::attr::ColumnFlags;
      if (is_geometry_column(g->name(uint32_t(i))) || has_flag(c.flags(), ColumnFlags::Temp) ||
          has_flag(c.flags(), ColumnFlags::Derived) || (coord_free && g->name(uint32_t(i)) == "pos"))
      {
        continue;
      }
      h.str(g->name(uint32_t(i)));
      c.hash_into(h);
    }
  }
  return h.finish();
}

} // namespace ubertopo::test
