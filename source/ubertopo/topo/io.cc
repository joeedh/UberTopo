#include "ubertopo/topo/io.h"

#include <bit>

#include "ubertopo/topo/freeze.h"
#include "ubertopo/topo/schema.h"

namespace ubertopo::topo {

namespace {
constexpr ident::DomainTag kTopologyHash = ident::DomainTag(263);

template <geom::CoordType C> void hash_groups(const Complex<C> &k, ident::Hasher128 &h, bool with_pos)
{
  if (with_pos) {
    k.verts.group().hash_into(h);
  }
  else {
    k.verts.group().hash_into(h, [](std::string_view name, const attr::ColumnBase &) { return name != "pos"; });
  }
  k.edges.group().hash_into(h);
  k.coedges.group().hash_into(h);
  k.loops.group().hash_into(h);
  k.faces.group().hash_into(h);
  k.faces.sides().group().hash_into(h);
  k.shells.group().hash_into(h);
  k.regions.group().hash_into(h);
  k.global.group().hash_into(h);
}

template <geom::CoordType C> void hash_frame(const Complex<C> &k, ident::Hasher128 &h)
{
  h.str(geom::CoordTraits<C>::key);
  h.f64_bits(k.frame.origin[0]).f64_bits(k.frame.origin[1]).f64_bits(k.frame.origin[2]);
  h.i32(k.frame.quantum_log2).u64(k.frame.frame_id);
}
} // namespace

template <geom::CoordType C> ident::PersistentId content_hash(const Complex<C> &k)
{
  ident::Hasher128 h(ident::DomainTag::Content);
  hash_frame(k, h);
  h.boolean(k.regions_dirty);
  hash_groups(k, h, true);
  return h.finish();
}

template <geom::CoordType C> ident::PersistentId topology_hash(const Complex<C> &k)
{
  ident::Hasher128 h(kTopologyHash);
  h.u64(k.frame.frame_id);
  h.boolean(k.regions_dirty);
  hash_groups(k, h, false);
  return h.finish();
}

template <geom::CoordType C> void write(const Complex<C> &k, litestl::io::ByteWriter &w)
{
  w.u32(kComplexMagic);
  w.u32(kComplexFormat);
  w.str(geom::CoordTraits<C>::key);
  for (int i = 0; i < 3; i++) {
    w.u64(std::bit_cast<uint64_t>(k.frame.origin[i]));
  }
  w.i64(k.frame.quantum_log2);
  w.u64(k.frame.frame_id);
  w.u8(k.regions_dirty ? 1 : 0);
  w.u8(k.frozen() ? 1 : 0);
  k.verts.group().write(w);
  k.edges.group().write(w);
  k.coedges.group().write(w);
  k.loops.group().write(w);
  k.faces.group().write(w);
  k.faces.sides().group().write(w);
  k.shells.group().write(w);
  k.regions.group().write(w);
  k.global.group().write(w);
  const ident::PersistentId h = content_hash(k);
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
  if (r.u32() != kComplexFormat) {
    return fail("unsupported complex format version");
  }
  if (r.str() != std::string_view(geom::CoordTraits<C>::key)) {
    return fail("coordinate type mismatch");
  }
  Complex<C> s;
  for (int i = 0; i < 3; i++) {
    s.frame.origin[i] = std::bit_cast<double>(r.u64());
  }
  s.frame.quantum_log2 = int(r.i64());
  s.frame.frame_id = r.u64();
  s.regions_dirty = r.u8() != 0;
  const bool frozen = r.u8() != 0;
  if (r.failed()) {
    return fail("truncated header");
  }
  const char *gerr = nullptr;
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
  ident::PersistentId h;
  h.lo = r.u64();
  h.hi = r.u64();
  if (r.failed() || !(h == content_hash(s))) {
    return fail("content hash mismatch");
  }
  s.verts.rebuild_allocator();
  s.edges.rebuild_allocator();
  s.coedges.rebuild_allocator();
  s.loops.rebuild_allocator();
  s.faces.rebuild_allocator();
  s.shells.rebuild_allocator();
  s.regions.rebuild_allocator();
  k = std::move(s);
  if (frozen) {
    build_caches(k);
    k.frozen_stamp = k.version();
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
