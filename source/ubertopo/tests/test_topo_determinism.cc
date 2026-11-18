/* Determinism and cross-instantiation suite (plan P2 W11).
 *
 *  - a large city (>= 200 buildings, >= 10^5 faces at full scale) built,
 *    region-built and frozen under 1, 2, 4 and 12 bands gives identical
 *    content hashes and bytes;
 *  - the same model in Lattice and double gives identical topology hashes
 *    (links, IDs, regions, frozen order);
 *  - a pinned topology hash of a fixed room grid joins the determinism
 *    contract (the same under every compiler and -march).
 * LITESTL_TEST_SCALE_DIV divides the city size (sanitizer builds). */

#include "ubertopo/topo/freeze.h"
#include "ubertopo/topo/io.h"
#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

#include "gen/buildings.h"
#include "ut_test.h"

#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <vector>

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
using test::gen::GridOpts;
using test::gen::Model;

static int scale_div()
{
  const char *e = getenv("LITESTL_TEST_SCALE_DIV");
  const int d = e ? atoi(e) : 1;
  return d > 0 ? d : 1;
}

/* A dense city: mostly large thin room grids, with every other family mixed in. */
template <geom::CoordType C> static void big_city(Model<C> &m, uint64_t seed, int n)
{
  litestl::test::SplitMix64 r(seed);
  const int side = int(std::ceil(std::sqrt(double(n))));
  const int64_t lot = 6000;
  for (int b = 0; b < n; b++) {
    const test::gen::I3 o = {int64_t(b % side) * lot, int64_t(b / side) * lot, 0};
    const int fam = int(r.below(10));
    GridOpts g;
    if (fam < 6) {
      g.nx = 4 + int(r.below(7));
      g.ny = 4 + int(r.below(7));
      g.floors = 3 + int(r.below(5));
      g.windows = int(r.below(4));
      g.glazing = r.coin();
      test::gen::room_grid(m, o, g, b);
    }
    else if (fam == 6) {
      g.nx = 2 + int(r.below(3));
      g.ny = 2 + int(r.below(3));
      g.floors = 1 + int(r.below(3));
      g.thick = true;
      test::gen::room_grid(m, o, g, b);
    }
    else if (fam == 7) {
      test::gen::courtyard(m, o, b);
      test::gen::nested_solids(m, {o[0] + 1500, o[1], 0}, b);
    }
    else if (fam == 8) {
      test::gen::sloped_roof(m, o, 1 + int64_t(r.below(5)), b);
      test::gen::pillar_room(m, {o[0] + 1500, o[1], 0}, b);
    }
    else {
      test::gen::sensor_and_wire(m, {o[0] + 500, o[1], 0}, b);
      test::gen::fence_in_air(m, {o[0] + 1500, o[1] + 1500, 0}, 2 + int(r.below(3)));
      test::gen::sealed_cavity(m, {o[0] + 3000, o[1], 0}, b);
    }
  }
}

template <geom::CoordType C> static bool pipeline(Complex<C> &k, uint64_t seed, int n)
{
  Model<C> m(ident::derive(ident::tag::TestOnly, std::string_view("determinism")));
  big_city(m, seed, n);
  BuildReport br;
  RegionReport rr;
  if (!m.b.build(k, br, m.policy) || !build_regions(k, rr) || rr.warnings) {
    return false;
  }
  return freeze(k) == FreezeResult::Ok;
}

template <geom::CoordType C> static std::vector<uint8_t> bytes_of(const Complex<C> &k)
{
  litestl::io::ByteWriter w;
  write(k, w);
  return std::vector<uint8_t>(w.data(), w.data() + w.size());
}

template <geom::CoordType C> static PersistentId test_bands(int n, PersistentId *topo)
{
  PersistentId ref{};
  std::vector<uint8_t> ref_bytes;
  for (int bands : {1, 2, 4, 12}) {
    litestl::task::ScopedBandCount scope(bands);
    const auto t0 = std::chrono::steady_clock::now();
    Complex<C> k;
    UT_CHECK(pipeline(k, 2026, n));
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (bands == 1) {
      printf("city (%s): %d buildings, %u faces, %u regions, %.2f s at 1 band\n", geom::CoordTraits<C>::key, n,
             k.faces.live_count(), k.regions.live_count(), s);
      if (scale_div() == 1) {
        UT_CHECK(n >= 200 && k.faces.live_count() >= 100000);
      }
      /* the combinatorial and embedding layers on the full city */
      const ValidationReport vr = validate(k, ValidateLevel::Embedding);
      if (!vr.ok()) {
        vr.print(stderr);
      }
      UT_CHECK(vr.ok());
    }
    const PersistentId h = content_hash(k);
    const auto b = bytes_of(k);
    if (bands == 1) {
      ref = h;
      ref_bytes = b;
      *topo = topology_hash(k);
    }
    UT_CHECK(h == ref && b == ref_bytes);
  }
  return ref;
}

static void test_golden()
{
  auto grid = [](auto &k) {
    using C = typename std::remove_reference_t<decltype(k)>::coord_type;
    Model<C> m(ident::derive(ident::tag::TestOnly, std::string_view("golden")));
    GridOpts g;
    g.nx = 3;
    g.ny = 2;
    g.floors = 2;
    g.windows = 2;
    test::gen::room_grid(m, {0, 0, 0}, g);
    test::gen::sloped_roof(m, {3000, 0, 0}, 3);
    test::gen::sensor_and_wire(m, {0, 3000, 0});
    BuildReport br;
    RegionReport rr;
    UT_CHECK(m.b.build(k, br, m.policy) && build_regions(k, rr) && freeze(k) == FreezeResult::Ok);
  };
  Complex<geom::Lattice> a;
  Complex<double> b;
  grid(a);
  grid(b);
  const PersistentId ha = topology_hash(a), hb = topology_hash(b);
  /* re-pinned for DEC-017 (hinted region IDs from the hint alone); only the
   * region IDs, .f.region, .s.region and .r.shell changed (P3b C8) */
  const PersistentId golden = {0x824b09e6bbd23a72ULL, 0xf96f12ef6fe48699ULL};
  if (getenv("LITESTL_PRINT_GOLDENS")) {
    printf("  topology golden: {0x%016" PRIx64 "ULL, 0x%016" PRIx64 "ULL}\n", ha.lo, ha.hi);
  }
  else {
    if (!(ha == golden)) {
      fprintf(stderr, "topology golden changed: {0x%016" PRIx64 ", 0x%016" PRIx64 "}\n", ha.lo, ha.hi);
    }
    UT_CHECK(ha == golden);
  }
  UT_CHECK(ha == hb);
}

int main()
{
  const int n = std::max(16, 200 / scale_div());
  PersistentId tl, td;
  test_bands<geom::Lattice>(n, &tl);
  test_bands<double>(n, &td);
  UT_CHECK(tl == td); /* same topology in both instantiations */
  test_golden();
  return ut_test_end();
}
