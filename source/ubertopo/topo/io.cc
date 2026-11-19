#include "ubertopo/topo/io.h"

#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>

#include "ubertopo/topo/freeze.h"
#include "ubertopo/topo/geom_store.h"
#include "ubertopo/topo/io_detail.h"
#include "ubertopo/topo/schema.h"

namespace ubertopo::topo {

using namespace detail;

namespace {

/* the groups in stream order, ref columns through the canonical map */
template <geom::CoordType C>
void hash_groups(const Complex<C> &k, ident::Hasher128 &h, bool with_pos, const attr::RefRemap &map)
{
  auto all = [](std::string_view, const attr::ColumnBase &) { return true; };
  auto no_pos = [](std::string_view name, const attr::ColumnBase &) { return name != "pos"; };
  if (with_pos) {
    k.verts.group().hash_into(h, all, map);
  }
  else {
    k.verts.group().hash_into(h, no_pos, map);
  }
  for (const attr::AttrGroup *g : {&k.edges.group(), &k.coedges.group(), &k.loops.group(), &k.faces.group(),
                                   &k.faces.sides().group(), &k.shells.group(), &k.regions.group(),
                                   &k.global.group()})
  {
    g->hash_into(h, all, map);
  }
}

/* One path for both hashes (plan P4a §4.3): the groups hashed through the
 * store's canonical map, which is cheap (no copy, no renumbering), so the
 * hashes equal those of the canonicalised complex that write() emits. A
 * store whose refs don't validate gets a marker and an empty map, so its
 * hash is still deterministic but matches no valid complex. */
template <geom::CoordType C> void hash_body(const Complex<C> &k, ident::Hasher128 &h, bool content)
{
  const GeomRoots<C> roots(k);
  attr::PoolStore::CanonicalMap map;
  if (!k.geom.canonical_map(roots.roots(), map)) {
    h.u32(0xBAD6E0u);
  }
  hash_groups(k, h, content, map);
  if (content) {
    k.geom.hash_into(h, map);
  }
  hash_floors(k, h);
}

} // namespace

template <geom::CoordType C> ident::PersistentId content_hash(const Complex<C> &k)
{
  ident::Hasher128 h(ident::DomainTag::Content);
  hash_frame(k, h);
  h.boolean(k.regions_dirty);
  hash_body(k, h, true);
  return h.finish();
}

template <geom::CoordType C> ident::PersistentId topology_hash(const Complex<C> &k)
{
  ident::Hasher128 h(kTopologyHash);
  h.u64(k.frame.frame_id);
  h.boolean(k.regions_dirty);
  hash_body(k, h, false);
  return h.finish();
}

template <geom::CoordType C> void write(const Complex<C> &k, litestl::io::ByteWriter &w)
{
  /* from a canonicalised copy (07 §13): copy-on-write, so k is untouched */
  Complex<C> c = k;
  {
    const GeomMutRoots<C> roots(c);
    const char *err = nullptr;
    if (!c.geom.canonicalize(roots.roots(), nullptr, &err)) {
      fprintf(stderr, "topo::write: the geometry store does not validate: %s\n", err ? err : "?");
      abort();
    }
  }
  w.u32(kComplexMagic);
  w.u32(kComplexFormat);
  w.str(geom::CoordTraits<C>::key);
  for (int i = 0; i < 3; i++) {
    w.u64(std::bit_cast<uint64_t>(c.frame.origin[i]));
  }
  w.i64(c.frame.quantum_log2);
  w.u64(c.frame.frame_id);
  w.u8(c.regions_dirty ? 1 : 0);
  w.u8(k.frozen() ? 1 : 0);
  for (uint32_t f : floors_of(c)) {
    w.u32(f);
  }
  {
    const GeomRoots<C> roots(c);
    if (!c.geom.write(w, roots.roots())) {
      fprintf(stderr, "topo::write: the geometry store is not canonical after canonicalize\n");
      abort();
    }
  }
  c.verts.group().write(w);
  c.edges.group().write(w);
  c.coedges.group().write(w);
  c.loops.group().write(w);
  c.faces.group().write(w);
  c.faces.sides().group().write(w);
  c.shells.group().write(w);
  c.regions.group().write(w);
  c.global.group().write(w);
  const ident::PersistentId h = content_hash(c);
  w.u64(h.lo);
  w.u64(h.hi);
}

template <geom::CoordType C> bool read(Complex<C> &k, litestl::io::ByteReader &r, const char **err)
{
  auto fail = [&](const char *m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  register_column_types();
  if (r.u32() != kComplexMagic || r.failed()) {
    return fail("not a complex stream");
  }
  const uint32_t format = r.u32();
  if (format < kComplexFormatMin || format > kComplexFormat) {
    return fail("unsupported complex format version");
  }
  if (r.str() != std::string_view(geom::CoordTraits<C>::key)) {
    return fail("coordinate type mismatch");
  }
  if (format < 3) {
    return read_legacy(k, r, format, err);
  }
  /* the strict owner sequence (plan P4a §4.3) */
  Complex<C> s;
  for (int i = 0; i < 3; i++) {
    s.frame.origin[i] = std::bit_cast<double>(r.u64());
  }
  s.frame.quantum_log2 = int(r.i64());
  s.frame.frame_id = r.u64();
  s.regions_dirty = r.u8() != 0;
  const bool frozen = r.u8() != 0;
  uint32_t floors[7] = {};
  for (uint32_t &f : floors) {
    f = r.u32();
  }
  if (r.failed()) {
    return fail("truncated header");
  }
  const char *gerr = nullptr;
  if (!s.geom.read(r, {}, &gerr)) {
    return fail(gerr ? gerr : "geometry store read failed");
  }
  if (!s.verts.group().read(r, {}, &gerr) || !s.edges.group().read(r, {}, &gerr) ||
      !s.coedges.group().read(r, {}, &gerr) || !s.loops.group().read(r, {}, &gerr) ||
      !s.faces.group().read(r, {}, &gerr) || !s.faces.sides().group().read(r, {}, &gerr) ||
      !s.shells.group().read(r, {}, &gerr) || !s.regions.group().read(r, {}, &gerr) ||
      !s.global.group().read(r, {}, &gerr))
  {
    return fail(gerr ? gerr : "group read failed");
  }
  if (s.faces.sides().size() != 2 * s.faces.capacity() || s.global.size() != 1) {
    return fail("face side or global group size mismatch");
  }
  set_floors(s, floors);
  {
    const GeomRoots<C> roots(s);
    if (!s.geom.audit(roots.roots(), true)) {
      return fail("geometry store: a dangling, refused or unreachable entry");
    }
  }
  {
    const GeomMutRoots<C> roots(s);
    bool changed = true;
    if (!s.geom.canonicalize(roots.roots(), &changed, &gerr) || changed) {
      return fail("geometry store is not in canonical order");
    }
  }
  ident::PersistentId h;
  h.lo = r.u64();
  h.hi = r.u64();
  if (r.failed() || !(h == content_hash(s))) {
    return fail("content hash mismatch");
  }
  rebuild_allocators(s);
  k = std::move(s);
  if (frozen) {
    build_caches(k);
    k.frozen_stamp = k.version();
    k.frozen_geom_version = k.geom.version();
  }
  return true;
}

#define UT_IO_INST(C)                                                                              \
  template void write<C>(const Complex<C> &, litestl::io::ByteWriter &);                          \
  template bool read<C>(Complex<C> &, litestl::io::ByteReader &, const char **);                  \
  template ident::PersistentId content_hash<C>(const Complex<C> &);                               \
  template ident::PersistentId topology_hash<C>(const Complex<C> &);
UT_IO_INST(double)
UT_IO_INST(float)
UT_IO_INST(geom::Lattice)
#undef UT_IO_INST

} // namespace ubertopo::topo
