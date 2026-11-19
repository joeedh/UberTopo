/* Formats 1 and 2 (plan P4a §4.3): streams written by the format-2 code read
 * into format-3 complexes with the same structure (the structure hash leaves
 * out the geometry columns), the same validation result and the old floors;
 * they write back as format 3 and round-trip; corrupted streams and legacy
 * geometry refs other than NONE are refused. */

#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#include "support/legacy_fixture.h"
#include "ubertopo/topo/io.h"
#include "ubertopo/topo/io_detail.h"
#include "ubertopo/topo/validate.h"
#include "ut_test.h"

#include "data/utcx_f3.inc"
#include "data/utcx_legacy.inc"

using namespace ubertopo;
using namespace ubertopo::topo;

namespace {

template <geom::CoordType C> std::vector<uint8_t> bytes_of(const Complex<C> &k)
{
  litestl::io::ByteWriter w;
  write(k, w);
  return std::vector<uint8_t>(w.data(), w.data() + w.size());
}

template <geom::CoordType C> bool read_bytes(Complex<C> &k, const std::vector<uint8_t> &b, const char **err)
{
  litestl::io::ByteReader r(b.data(), b.size());
  return read(k, r, err);
}

/* the header of a legacy stream: magic, format, key, frame, two flags,
 * floors (format 2) */
size_t header_size(const char *key, uint32_t format)
{
  litestl::io::ByteWriter w;
  w.u32(0);
  w.u32(0);
  w.str(key);
  for (int i = 0; i < 5; i++) {
    w.u64(0);
  }
  w.u8(0);
  w.u8(0);
  return w.size() + (format >= 2 ? 7 * 4 : 0);
}

template <geom::CoordType C> void one(const test::LegacyFixture &f)
{
  const std::vector<uint8_t> in(f.bytes, f.bytes + f.size);
  Complex<C> k;
  const char *err = nullptr;
  UT_CHECK(read_bytes(k, in, &err));
  if (err) {
    fprintf(stderr, "%s: %s\n", f.name, err);
    return;
  }
  /* the same structure, validation result and floors */
  const auto sh = test::structure_hash(k);
  UT_CHECK(sh.lo == f.structure_lo && sh.hi == f.structure_hi);
  UT_CHECK(validate(k, ValidateLevel::Combinatorial).total == f.combinatorial_issues);
  UT_CHECK(validate(k, ValidateLevel::Global).total == f.global_issues);
  UT_CHECK(k.frozen() && k.geom.live_count() == 0);
  if (f.format == 2) {
    UT_CHECK(k.faces.gen_floor() == 3 && k.edges.gen_floor() == 5);
  }
  /* the new columns hold their defaults */
  for (uint32_t e = 0; e < k.edges.capacity(); e++) {
    UT_CHECK(k.edges.template get<".e.curve">()[e].none() && k.edges.template get<".e.geom_flags">()[e] == 0);
  }
  UT_CHECK(k.global.template get<".g.curve_tol">()[0] == 0x1p-30);
  /* the old content hash is not the new one: the layouts differ */
  const auto ch = content_hash(k);
  UT_CHECK(!(ch.lo == f.content_lo && ch.hi == f.content_hi));
  /* format 3 round trip, byte-stable */
  const std::vector<uint8_t> b3 = bytes_of(k);
  UT_CHECK(b3[4] == kComplexFormat);
  Complex<C> back;
  UT_CHECK(read_bytes(back, b3, &err) && content_hash(back) == ch && bytes_of(back) == b3);
  UT_CHECK(back.frozen() && test::structure_hash(back) == sh);
  /* corruption: a flipped byte inside the groups, a truncated stream */
  {
    std::vector<uint8_t> bad = in;
    bad[bad.size() / 2] ^= 0x40;
    Complex<C> x;
    UT_CHECK(!read_bytes(x, bad, &err));
    std::vector<uint8_t> cut(in.begin(), in.end() - 9);
    UT_CHECK(!read_bytes(x, cut, &err));
  }
  /* re-serialised unchanged, the stream reads; a legacy geometry ref other
   * than NONE is refused though the stream's hash is right; a wrong trailing
   * hash on otherwise good data is refused */
  for (const int mode : {0, 1, 2}) {
    const bool poke = mode == 1;
    const size_t hs = header_size(f.coord, f.format);
    litestl::io::ByteReader r(in.data() + hs, in.size() - hs);
    Complex<C> s;
    detail::LegacyGroups lg;
    UT_CHECK(detail::read_legacy_groups(s, lg, r, &err));
    if (poke) {
      lg.edges.template get<".e.curve">().set(0, LegacyGeomRef{7});
    }
    /* the header terms of the hash */
    Complex<C> h;
    UT_CHECK(read_bytes(h, in, &err));
    s.frame = h.frame;
    s.regions_dirty = h.regions_dirty;
    const uint32_t fl[7] = {h.verts.gen_floor(),  h.edges.gen_floor(),  h.coedges.gen_floor(), h.loops.gen_floor(),
                            h.faces.gen_floor(),  h.shells.gen_floor(), h.regions.gen_floor()};
    detail::set_floors(s, fl);
    litestl::io::ByteWriter w;
    w.put(in.data(), hs);
    detail::write_legacy_groups(s, lg, w);
    const ident::PersistentId lh = detail::legacy_content_hash(s, lg);
    w.u64(mode == 2 ? lh.lo ^ 1 : lh.lo);
    w.u64(lh.hi);
    const std::vector<uint8_t> out(w.data(), w.data() + w.size());
    Complex<C> x;
    const bool ok = read_bytes(x, out, &err);
    UT_CHECK(ok == (mode == 0));
    if (poke) {
      UT_CHECK(err && std::string_view(err).find("NONE") != std::string_view::npos);
    }
    else if (mode == 2) {
      UT_CHECK(err && std::string_view(err).find("hash") != std::string_view::npos);
    }
    else {
      UT_CHECK(content_hash(x) == ch);
    }
  }
}

/* Format 3 (plan P4b §2): streams written by the P4a code read into format-4
 * complexes with the same structure, validation result and geometry (four
 * curved edges, eight entries); .g.skeleton_tol takes its default; they write
 * back as format 4 and round-trip; a wrong trailing hash, a hash over all of
 * today's pools, and a flipped byte are refused. */
template <geom::CoordType C> void one_f3(const test::LegacyFixture &f)
{
  const std::vector<uint8_t> in(f.bytes, f.bytes + f.size);
  UT_CHECK(in[4] == 3);
  Complex<C> k;
  const char *err = nullptr;
  UT_CHECK(read_bytes(k, in, &err));
  if (err) {
    fprintf(stderr, "%s: %s\n", f.name, err);
    return;
  }
  const auto sh = test::structure_hash(k);
  UT_CHECK(sh.lo == f.structure_lo && sh.hi == f.structure_hi);
  UT_CHECK(validate(k, ValidateLevel::Combinatorial).total == f.combinatorial_issues);
  UT_CHECK(validate(k, ValidateLevel::Global).total == f.global_issues);
  UT_CHECK(k.frozen() && k.geom.live_count() == 8);
  uint32_t curved = 0;
  for (uint32_t e = 0; e < k.edges.capacity(); e++) {
    curved += k.edges.live(e) && !k.edges.template get<".e.curve">()[e].none() ? 1 : 0;
  }
  UT_CHECK(curved == 4);
  UT_CHECK(k.global.template get<".g.curve_tol">()[0] == 0x1p-30);
  UT_CHECK(k.global.template get<".g.skeleton_tol">()[0] == 0x1p-6);
  /* the old content hash is not the new one: the pool count and the global
   * group differ */
  const auto ch = content_hash(k);
  UT_CHECK(!(ch.lo == f.content_lo && ch.hi == f.content_hi));
  const auto th = topology_hash(k);
  UT_CHECK(!(th.lo == f.topology_lo && th.hi == f.topology_hi));
  /* format 4 round trip, byte-stable */
  const std::vector<uint8_t> b4 = bytes_of(k);
  UT_CHECK(b4[4] == kComplexFormat && kComplexFormat == 4);
  Complex<C> back;
  UT_CHECK(read_bytes(back, b4, &err) && content_hash(back) == ch && bytes_of(back) == b4);
  UT_CHECK(back.frozen() && test::structure_hash(back) == sh);
  /* refusals */
  {
    std::vector<uint8_t> bad = in;
    bad[bad.size() - 3] ^= 1; /* the trailing hash */
    Complex<C> x;
    UT_CHECK(!read_bytes(x, bad, &err) && std::string_view(err).find("hash") != std::string_view::npos);
    bad = in;
    bad[bad.size() / 2] ^= 0x40;
    UT_CHECK(!read_bytes(x, bad, &err));
    /* the trailing hash replaced by today's hash of the same complex: a
     * format-3 reader must not accept the format-4 hash */
    bad = in;
    const auto now = content_hash(k);
    std::memcpy(bad.data() + bad.size() - 16, &now.lo, 8);
    std::memcpy(bad.data() + bad.size() - 8, &now.hi, 8);
    UT_CHECK(!read_bytes(x, bad, &err));
  }
}

} // namespace

int main()
{
  register_column_types();
  for (const test::LegacyFixture *f : kLegacyFixtures) {
    if (std::string_view(f->coord) == "f64") {
      one<double>(*f);
    }
    else {
      one<geom::Lattice>(*f);
    }
  }
  for (const test::LegacyFixture *f : kF3Fixtures) {
    if (std::string_view(f->coord) == "f64") {
      one_f3<double>(*f);
    }
    else {
      one_f3<geom::Lattice>(*f);
    }
  }
  return ut_test_end();
}
