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

/* the groups in stream order, ref columns through the canonical map (raw
 * ref values without one) */
template <geom::CoordType C>
void hash_groups(const Complex<C> &k,
                 ident::Hasher128 &h,
                 bool with_pos,
                 const attr::RefRemap *map,
                 const attr::AttrGroup *global = nullptr)
{
  auto all = [](std::string_view, const attr::ColumnBase &) { return true; };
  auto no_pos = [](std::string_view name, const attr::ColumnBase &) { return name != "pos"; };
  auto one = [&](const attr::AttrGroup &g, bool skip_pos) {
    if (map) {
      skip_pos ? g.hash_into(h, no_pos, *map) : g.hash_into(h, all, *map);
    }
    else {
      skip_pos ? g.hash_into(h, no_pos) : g.hash_into(h, all);
    }
  };
  one(k.verts.group(), !with_pos);
  for (const attr::AttrGroup *g : {&k.edges.group(), &k.coedges.group(), &k.loops.group(), &k.faces.group(),
                                   &k.faces.sides().group(), &k.shells.group(), &k.regions.group()})
  {
    one(*g, false);
  }
  /* the topology hash also leaves out .g.skeleton_tol: in frame units, like
   * positions, so double and Lattice builds of one model differ there */
  const attr::AttrGroup &gg = global ? *global : k.global.group();
  auto no_tol = [](std::string_view name, const attr::ColumnBase &) { return name != ".g.skeleton_tol"; };
  if (with_pos) {
    one(gg, false);
  }
  else if (map) {
    gg.hash_into(h, no_tol, *map);
  }
  else {
    gg.hash_into(h, no_tol);
  }
}

/* One path for both hashes (plan P4a §4.3): the groups hashed through the
 * store's canonical map, which is cheap (no copy, no renumbering), so the
 * hashes equal those of the canonicalised complex that write() emits. Refs
 * that don't validate (dangling, refused, or into another store) get a
 * marker and the groups hashed with raw ref values: deterministic, still
 * sensitive to every ref, and equal to no valid complex's hash. */
template <geom::CoordType C> void hash_body(const Complex<C> &k, ident::Hasher128 &h, bool content)
{
  const GeomRoots<C> roots(k);
  attr::PoolStore::CanonicalMap map;
  if (!k.geom.canonical_map(roots.roots(), map)) {
    h.u32(0xBAD6E0u);
    hash_groups(k, h, content, nullptr);
  }
  else {
    hash_groups(k, h, content, &map);
    if (content) {
      k.geom.hash_into(h, map);
    }
  }
  hash_floors(k, h);
}

/* The content hash a format-3 writer computed for s (plan P4b §2): the
 * format-3 global group g3 in place of s's, and the store hashed over its
 * first kFormat3Pools pools. False if the refs don't validate or a later
 * pool has a reachable entry. */
template <geom::CoordType C> bool format3_content_hash(const Complex<C> &s, const attr::AttrGroup &g3, ident::PersistentId &out)
{
  ident::Hasher128 h(ident::DomainTag::Content);
  hash_frame(s, h);
  h.boolean(s.regions_dirty);
  const GeomRoots<C> roots(s);
  attr::PoolStore::CanonicalMap map;
  if (!s.geom.canonical_map(roots.roots(), map)) {
    return false;
  }
  hash_groups(s, h, true, &map, &g3);
  if (!s.geom.hash_into(h, map, kFormat3Pools)) {
    return false;
  }
  hash_floors(s, h);
  out = h.finish();
  return true;
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
  /* format 3: the global group without .g.skeleton_tol, migrated below */
  attr::TypedGroup<Format3GlobalSchema> g3;
  attr::AttrGroup &global = format == 3 ? g3.group() : s.global.group();
  if (!s.verts.group().read(r, {}, &gerr) || !s.edges.group().read(r, {}, &gerr) ||
      !s.coedges.group().read(r, {}, &gerr) || !s.loops.group().read(r, {}, &gerr) ||
      !s.faces.group().read(r, {}, &gerr) || !s.faces.sides().group().read(r, {}, &gerr) ||
      !s.shells.group().read(r, {}, &gerr) || !s.regions.group().read(r, {}, &gerr) ||
      !global.read(r, {}, &gerr))
  {
    return fail(gerr ? gerr : "group read failed");
  }
  if (s.faces.sides().size() != 2 * s.faces.capacity() || global.size() != 1) {
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
  if (format == 3) {
    ident::PersistentId want;
    if (r.failed() || !format3_content_hash(s, global, want) || !(h == want)) {
      return fail("content hash mismatch");
    }
    auto keep_all = [](std::string_view, const attr::ColumnBase &) { return true; };
    if (!s.global.group().migrate_from(global, keep_all, &gerr) || s.global.size() != 1) {
      return fail(gerr ? gerr : "format 3 migration failed");
    }
  }
  else if (r.failed() || !(h == content_hash(s))) {
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
