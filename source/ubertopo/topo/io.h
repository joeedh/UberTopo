#pragma once

/* Whole-complex serialisation and hashing (plan P2 W10).
 *
 * Stream: magic "UTCX", format version, coordinate key, tile frame,
 * regions_dirty and frozen flags, the domains' generation floors (format 2),
 * then each domain group in a fixed order
 * (vertices, edges, coedges, loops, faces, face sides, shells, regions,
 * global) through AttrGroup::write (layout-checked on read), then the
 * trailing content hash. The allocator state is the ".live" columns; frozen
 * caches are rebuilt after read(). read() replaces the target only on
 * success.
 *
 * content_hash covers everything serialised; the generation floors enter the
 * hashes only when one is non-zero, so all-zero states hash as in format 1. topology_hash leaves out the
 * coordinate type, the frame's geometry and vertex positions, so the same
 * model built in double and in Lattice has the same topology hash. */

#include <cstdint>

#include "litestl/io/bytes.h"
#include "ubertopo/ident/id.h"
#include "ubertopo/topo/complex.h"

namespace ubertopo::topo {

inline constexpr uint32_t kComplexMagic = 0x58435455u; /* "UTCX" little-endian */
/* 1: P2. 2: P3b, adds the seven generation floors after the header (read
 * from format 1 as zero). */
inline constexpr uint32_t kComplexFormat = 2;
inline constexpr uint32_t kComplexFormatMin = 1;

template <geom::CoordType C> void write(const Complex<C> &k, litestl::io::ByteWriter &w);
template <geom::CoordType C> bool read(Complex<C> &k, litestl::io::ByteReader &r, const char **err = nullptr);

template <geom::CoordType C> ident::PersistentId content_hash(const Complex<C> &k);
template <geom::CoordType C> ident::PersistentId topology_hash(const Complex<C> &k);

#define UT_IO_EXTERN(C)                                                                            \
  extern template void write<C>(const Complex<C> &, litestl::io::ByteWriter &);                   \
  extern template bool read<C>(Complex<C> &, litestl::io::ByteReader &, const char **);           \
  extern template ident::PersistentId content_hash<C>(const Complex<C> &);                        \
  extern template ident::PersistentId topology_hash<C>(const Complex<C> &);
UT_IO_EXTERN(double)
UT_IO_EXTERN(float)
UT_IO_EXTERN(geom::Lattice)
#undef UT_IO_EXTERN

} // namespace ubertopo::topo
