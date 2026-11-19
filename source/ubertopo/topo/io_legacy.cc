/* Reading UTCX formats 1 and 2 (plan P4a §4.3).
 *
 * The streams hold the old column layouts: 4-byte "gref" Geom columns and
 * no .e.range, .e.geom_flags or .g.curve_tol. They are read into groups of
 * the legacy schemas, checked against the stream's trailing hash with the
 * old hash, and moved into a fresh format-3 complex (AttrGroup::migrate_from):
 * every legacy Geom value must be NONE, the new columns take their
 * defaults, and the store stays empty. */

#include <bit>

#include "ubertopo/topo/freeze.h"
#include "ubertopo/topo/io.h"
#include "ubertopo/topo/io_detail.h"
#include "ubertopo/topo/schema.h"

namespace ubertopo::topo::detail {

namespace {

bool all_none(const attr::Column<LegacyGeomRef> &c)
{
  for (uint32_t i = 0; i < c.size(); i++) {
    if (!(c[i] == LegacyGeomRef{})) {
      return false;
    }
  }
  return true;
}

} // namespace

template <geom::CoordType C> bool read_legacy(Complex<C> &k, litestl::io::ByteReader &r, uint32_t format, const char **err)
{
  auto fail = [&](const char *m) {
    if (err) {
      *err = m;
    }
    return false;
  };
  Complex<C> s;
  for (int i = 0; i < 3; i++) {
    s.frame.origin[i] = std::bit_cast<double>(r.u64());
  }
  s.frame.quantum_log2 = int(r.i64());
  s.frame.frame_id = r.u64();
  s.regions_dirty = r.u8() != 0;
  const bool frozen = r.u8() != 0;
  uint32_t floors[7] = {};
  if (format >= 2) {
    for (uint32_t &f : floors) {
      f = r.u32();
    }
  }
  if (r.failed()) {
    return fail("truncated header");
  }
  LegacyGroups lgs;
  auto &le = lgs.edges;
  auto &lc = lgs.coedges;
  auto &lf = lgs.faces;
  auto &lg = lgs.global;
  const char *gerr = nullptr;
  if (!read_legacy_groups(s, lgs, r, &gerr)) {
    return fail(gerr ? gerr : "group read failed");
  }
  if (s.faces.sides().size() != 2 * lf.size() || lg.size() != 1) {
    return fail("face side or global group size mismatch");
  }
  set_floors(s, floors);
  {
    ident::PersistentId want;
    want.lo = r.u64();
    want.hi = r.u64();
    if (r.failed() || !(want == legacy_content_hash(s, lgs))) {
      return fail("content hash mismatch");
    }
  }
  if (!all_none(le.get<".e.curve">()) || !all_none(lc.get<".c.pcurve">()) || !all_none(lf.get<".f.surface">())) {
    return fail("legacy stream: a geometry ref is not NONE");
  }
  auto keep_all = [](std::string_view, const attr::ColumnBase &) { return true; };
  auto not_curve = [](std::string_view n, const attr::ColumnBase &) { return n != ".e.curve"; };
  auto not_pcurve = [](std::string_view n, const attr::ColumnBase &) { return n != ".c.pcurve"; };
  auto not_surface = [](std::string_view n, const attr::ColumnBase &) { return n != ".f.surface"; };
  if (!s.edges.group().migrate_from(le.group(), not_curve, &gerr) ||
      !s.coedges.group().migrate_from(lc.group(), not_pcurve, &gerr) ||
      !s.faces.group().migrate_from(lf.group(), not_surface, &gerr) ||
      !s.global.group().migrate_from(lg.group(), keep_all, &gerr))
  {
    return fail(gerr ? gerr : "legacy migration failed");
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

template <geom::CoordType C>
bool read_legacy_groups(Complex<C> &s, LegacyGroups &lg, litestl::io::ByteReader &r, const char **err)
{
  return s.verts.group().read(r, {}, err) && lg.edges.group().read(r, {}, err) &&
         lg.coedges.group().read(r, {}, err) && s.loops.group().read(r, {}, err) &&
         lg.faces.group().read(r, {}, err) && s.faces.sides().group().read(r, {}, err) &&
         s.shells.group().read(r, {}, err) && s.regions.group().read(r, {}, err) && lg.global.group().read(r, {}, err);
}

template <geom::CoordType C> void write_legacy_groups(const Complex<C> &s, const LegacyGroups &lg, litestl::io::ByteWriter &w)
{
  for (const attr::AttrGroup *g : {&s.verts.group(), &lg.edges.group(), &lg.coedges.group(), &s.loops.group(),
                                   &lg.faces.group(), &s.faces.sides().group(), &s.shells.group(),
                                   &s.regions.group(), &lg.global.group()})
  {
    g->write(w);
  }
}

template <geom::CoordType C> ident::PersistentId legacy_content_hash(const Complex<C> &s, const LegacyGroups &lg)
{
  ident::Hasher128 h(ident::DomainTag::Content);
  hash_frame(s, h);
  h.boolean(s.regions_dirty);
  for (const attr::AttrGroup *g : {&s.verts.group(), &lg.edges.group(), &lg.coedges.group(), &s.loops.group(),
                                   &lg.faces.group(), &s.faces.sides().group(), &s.shells.group(),
                                   &s.regions.group(), &lg.global.group()})
  {
    g->hash_into(h);
  }
  hash_floors(s, h);
  return h.finish();
}

#define UT_LEGACY_INST(C)                                                                                    \
  template bool read_legacy_groups<C>(Complex<C> &, LegacyGroups &, litestl::io::ByteReader &, const char **); \
  template void write_legacy_groups<C>(const Complex<C> &, const LegacyGroups &, litestl::io::ByteWriter &);    \
  template ident::PersistentId legacy_content_hash<C>(const Complex<C> &, const LegacyGroups &);
UT_LEGACY_INST(double)
UT_LEGACY_INST(float)
UT_LEGACY_INST(geom::Lattice)
#undef UT_LEGACY_INST

template bool read_legacy<double>(Complex<double> &, litestl::io::ByteReader &, uint32_t, const char **);
template bool read_legacy<float>(Complex<float> &, litestl::io::ByteReader &, uint32_t, const char **);
template bool read_legacy<geom::Lattice>(Complex<geom::Lattice> &, litestl::io::ByteReader &, uint32_t, const char **);

} // namespace ubertopo::topo::detail
