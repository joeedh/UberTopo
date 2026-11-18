/* freeze / thaw / serialisation / hashes (plan P2 W10). */

#include "ubertopo/topo/freeze.h"
#include "ubertopo/topo/io.h"
#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/validate.h"

#include "gen/buildings.h"
#include "litestl/util/alloc.h"
#include "ut_test.h"

#include <map>
#include <set>
#include <vector>

using namespace ubertopo;
using namespace ubertopo::topo;
using ident::PersistentId;
using test::gen::Model;

static PersistentId root(const char *what)
{
  return ident::derive(ident::tag::TestOnly, std::string_view(what));
}

template <geom::CoordType C> static bool make_city(Complex<C> &k, uint64_t seed, int n)
{
  Model<C> m(root("city"));
  test::gen::random_city(m, seed, n);
  test::gen::sloped_roof(m, {-3000, -3000, 0}, 2);
  test::gen::fence_in_air(m, {-6000, -6000, 0}, 3);
  test::gen::sensor_and_wire(m, {-9000, -9000, 0}, 99);
  BuildReport br;
  RegionReport rr;
  return m.b.build(k, br, m.policy) && build_regions(k, rr);
}

template <geom::CoordType C> static std::vector<uint8_t> bytes_of(const Complex<C> &k)
{
  litestl::io::ByteWriter w;
  write(k, w);
  return std::vector<uint8_t>(w.data(), w.data() + w.size());
}

/* Frees the isolated components made of faces that are sheets in region 0
 * (fences) and the wires whose endpoints have no other edge. */
template <geom::CoordType C> static uint32_t free_fences_and_wires(Complex<C> &k)
{
  uint32_t freed = 0;
  std::set<uint32_t> faces, verts, edges;
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    if (k.faces.live(f) && k.region(FaceRef(f), 0).i == 0 && k.region(FaceRef(f), 1).i == 0 &&
        k.shell(FaceRef(f), 0) == k.shell(FaceRef(f), 1))
    {
      faces.insert(f);
    }
  }
  /* keep only components closed under edge adjacency (fences, not open-window walls) */
  for (bool changed = true; changed;) {
    changed = false;
    for (auto it = faces.begin(); it != faces.end();) {
      bool isolated = true;
      k.for_each_loop(FaceRef(*it), [&](LoopRef l) {
        k.for_each_coedge(l, [&](CoedgeRef c) {
          k.for_each_radial(k.edge_of(c), [&](CoedgeRef o) { isolated = isolated && faces.count(k.face_of(o).i); });
        });
      });
      if (!isolated) {
        it = faces.erase(it);
        changed = true;
      }
      else {
        ++it;
      }
    }
  }
  /* collect first: free() resets links, so walking while freeing stops early */
  std::vector<uint32_t> cos, lps;
  for (uint32_t f : faces) {
    k.for_each_loop(FaceRef(f), [&](LoopRef l) {
      k.for_each_coedge(l, [&](CoedgeRef c) {
        edges.insert(k.edge_of(c).i);
        verts.insert(k.start_vert(c).i);
        cos.push_back(c.i);
      });
      lps.push_back(l.i);
    });
  }
  for (uint32_t c : cos) {
    k.coedges.free(c);
    freed++;
  }
  for (uint32_t l : lps) {
    k.loops.free(l);
  }
  for (uint32_t f : faces) {
    k.faces.free(f);
  }
  for (uint32_t e = 0; e < k.edges.capacity(); e++) {
    if (!k.edges.live(e) || k.edge_coedge(EdgeRef(e))) {
      continue;
    }
    const auto vs = k.edge_verts(EdgeRef(e));
    uint32_t d0 = 0, d1 = 0;
    k.for_each_edge_at(vs[0], [&](EdgeRef) { d0++; });
    k.for_each_edge_at(vs[1], [&](EdgeRef) { d1++; });
    if (d0 == 1 && d1 == 1) {
      edges.insert(e);
      verts.insert(vs[0].i);
      verts.insert(vs[1].i);
    }
  }
  for (uint32_t e : edges) {
    k.edges.free(e);
  }
  for (uint32_t v : verts) {
    k.verts.free(v);
  }
  k.regions_dirty = true;
  return freed + uint32_t(edges.size() + verts.size());
}

template <geom::CoordType C> static void test_freeze_basics()
{
  Complex<C> k;
  UT_CHECK(make_city(k, 5, 16));
  UT_CHECK(validate(k, ValidateLevel::Global).ok());
  const PersistentId topo0 = topology_hash(k);

  Complex<C> dirty = k;
  dirty.regions_dirty = true;
  UT_CHECK(freeze(dirty) == FreezeResult::RegionsDirty);

  UT_CHECK(freeze(k) == FreezeResult::Ok && k.frozen() && k.caches_valid());
  const ValidationReport rep = validate(k, ValidateLevel::Global);
  if (!rep.ok()) {
    rep.print(stderr);
  }
  UT_CHECK(rep.ok());
  UT_CHECK(!(topology_hash(k) == topo0)); /* renumbered */
  /* a second freeze is a no-op with identical bytes */
  const auto b1 = bytes_of(k);
  UT_CHECK(freeze(k) == FreezeResult::AlreadyFrozen);
  UT_CHECK(bytes_of(k) == b1);
  /* thaw: editable, caches dropped; a mutation invalidates caches */
  Complex<C> t = thaw(k);
  UT_CHECK(!t.frozen() && !t.caches_valid() && content_hash(t) == content_hash(k));
  Complex<C> m = k;
  m.verts.template get<".v.container_hint">().set(0, PersistentId{1, 0});
  UT_CHECK(!m.caches_valid() && !m.frozen());

  /* caches agree with the links */
  bool ok = true;
  for (uint32_t v = 0; v < k.verts.capacity(); v++) {
    std::set<uint32_t> walk;
    k.for_each_edge_at(VertRef(v), [&](EdgeRef e) { walk.insert(e.i); });
    std::set<uint32_t> cache(k.caches.v2e.begin() + k.caches.v2e_off[v], k.caches.v2e.begin() + k.caches.v2e_off[v + 1]);
    ok = ok && walk == cache;
  }
  for (uint32_t s = 0; s < k.shells.capacity(); s++) {
    for (uint32_t i = k.caches.shell_off[s]; i < k.caches.shell_off[s + 1]; i++) {
      const uint32_t fs = k.caches.shell_sides[i];
      ok = ok && k.shell(FaceRef(fs / 2), fs % 2).i == s;
    }
  }
  uint32_t incl = 0;
  for (uint32_t r = 0; r < k.regions.capacity(); r++) {
    for (uint32_t i = k.caches.region_incl_off[r]; i < k.caches.region_incl_off[r + 1]; i++) {
      const uint32_t x = k.caches.region_incl[i];
      const ident::ContainerRef cr =
          (x & 1u) ? k.edges.template get<".e.container">()[x >> 1] : k.vert_container(VertRef(x >> 1));
      ok = ok && cr.as_region().i == r;
      incl++;
    }
  }
  uint32_t proj_tris = 0;
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    const uint32_t n = k.caches.tri_off[f + 1] - k.caches.tri_off[f];
    ok = ok && (n > 0) == (k.face_proj_axis(FaceRef(f)) >= 0);
    proj_tris += n;
  }
  UT_CHECK(ok && incl > 0 && proj_tris > 0);
}

template <geom::CoordType C> static void test_tombstones_and_generations()
{
  Complex<C> k;
  UT_CHECK(make_city(k, 9, 25));
  UT_CHECK(freeze(k) == FreezeResult::Ok);
  const uint32_t removed = free_fences_and_wires(k);
  UT_CHECK(removed > 0 && k.verts.free_count() > 0 && k.edges.free_count() > 0 && k.faces.free_count() > 0);
  /* handles and generations of survivors, by persistent ID */
  std::map<PersistentId, ident::Handle<ident::ElemKind::Face>> fh;
  std::map<PersistentId, ident::Handle<ident::ElemKind::Vertex>> vh;
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    if (k.faces.live(f)) {
      fh[k.face_id(FaceRef(f))] = k.faces.handle(FaceRef(f));
    }
  }
  for (uint32_t v = 0; v < k.verts.capacity(); v++) {
    if (k.verts.live(v)) {
      vh[k.vert_id(VertRef(v))] = k.verts.handle(VertRef(v));
    }
  }
  RegionReport rr;
  UT_CHECK(build_regions(k, rr));
  UT_CHECK(validate(k, ValidateLevel::Global).ok());
  UT_CHECK(freeze(k) == FreezeResult::Ok);
  UT_CHECK(k.verts.free_count() == 0 && k.faces.free_count() == 0 && k.edges.free_count() == 0 &&
           k.verts.capacity() == k.verts.live_count() && k.faces.capacity() == k.faces.live_count());
  const ValidationReport rep = validate(k, ValidateLevel::Global);
  if (!rep.ok()) {
    rep.print(stderr);
  }
  UT_CHECK(rep.ok()); /* the remap is complete */
  /* generations never decrease; an old handle never resolves to another element */
  bool ok = true;
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    const auto it = fh.find(k.face_id(FaceRef(f)));
    ok = ok && it != fh.end() && k.faces.gen(f) >= it->second.gen;
    ok = ok && (it->second.index == f) == (k.faces.gen(f) == it->second.gen);
  }
  for (auto &[id, h] : fh) {
    if (k.faces.resolve(h)) {
      ok = ok && k.face_id(h.ref()) == id;
    }
  }
  for (auto &[id, h] : vh) {
    if (k.verts.resolve(h)) {
      ok = ok && k.vert_id(h.ref()) == id;
    }
  }
  UT_CHECK(ok);

  /* P3b §3.1: slots dropped off the end come back past every old handle */
  UT_CHECK(k.faces.gen_floor() > 0 && k.verts.gen_floor() > 0);
  Complex<C> t = thaw(k);
  const uint32_t cap = t.faces.capacity();
  const uint32_t extra = t.faces.alloc_n(uint32_t(fh.size()));
  UT_CHECK(extra == cap && t.faces.gen(cap) == t.faces.gen_floor());
  for (auto &[id, h] : fh) {
    if (h.index >= cap) {
      ok = ok && !t.faces.resolve(h);
    }
  }
  UT_CHECK(ok);
  /* floors survive a round trip and enter the hashes */
  {
    const auto b = bytes_of(t);
    Complex<C> back;
    litestl::io::ByteReader r(b.data(), b.size());
    UT_CHECK(read(back, r));
    UT_CHECK(back.faces.gen_floor() == t.faces.gen_floor() && content_hash(back) == content_hash(t));
    Complex<C> nofloor = t;
    nofloor.faces.set_gen_floor(0);
    UT_CHECK(!(content_hash(nofloor) == content_hash(t)) && !(topology_hash(nofloor) == topology_hash(t)));
  }
}

/* A format 1 stream (no floors) still reads, with zero floors, and a P2
 * pipeline (build, regions, freeze) leaves every floor at zero. */
template <geom::CoordType C> static void test_format1()
{
  Complex<C> k;
  UT_CHECK(make_city(k, 5, 9));
  RegionReport rr;
  UT_CHECK(build_regions(k, rr));
  UT_CHECK(freeze(k) == FreezeResult::Ok);
  UT_CHECK(k.verts.gen_floor() == 0 && k.edges.gen_floor() == 0 && k.coedges.gen_floor() == 0 &&
           k.loops.gen_floor() == 0 && k.faces.gen_floor() == 0 && k.shells.gen_floor() == 0 &&
           k.regions.gen_floor() == 0);
  std::vector<uint8_t> b = bytes_of(k);
  /* the floors follow the header: magic, format, key, frame, two flags */
  litestl::io::ByteWriter hw;
  hw.u32(kComplexMagic);
  hw.u32(kComplexFormat);
  hw.str(geom::CoordTraits<C>::key);
  for (int i = 0; i < 3; i++) {
    hw.u64(0);
  }
  hw.i64(0);
  hw.u64(0);
  hw.u8(0);
  hw.u8(0);
  const size_t off = hw.size();
  std::vector<uint8_t> v1(b.begin(), b.begin() + off);
  v1.insert(v1.end(), b.begin() + off + 7 * 4, b.end());
  v1[4] = 1; /* format 1 */
  Complex<C> back;
  litestl::io::ByteReader r(v1.data(), v1.size());
  const char *err = nullptr;
  UT_CHECK(read(back, r, &err));
  if (err) {
    fprintf(stderr, "read v1: %s\n", err);
  }
  UT_CHECK(content_hash(back) == content_hash(k) && bytes_of(back) == b);
}

template <geom::CoordType C> static void test_io()
{
  for (int frozen = 0; frozen < 2; frozen++) {
    Complex<C> k;
    UT_CHECK(make_city(k, 3, 9));
    if (frozen) {
      UT_CHECK(freeze(k) == FreezeResult::Ok);
    }
    const auto b = bytes_of(k);
    Complex<C> back;
    litestl::io::ByteReader r(b.data(), b.size());
    const char *err = nullptr;
    UT_CHECK(read(back, r, &err));
    if (err) {
      fprintf(stderr, "read: %s\n", err);
    }
    UT_CHECK(content_hash(back) == content_hash(k) && bytes_of(back) == b);
    UT_CHECK(back.verts.live_count() == k.verts.live_count() && back.faces.live_count() == k.faces.live_count());
    UT_CHECK(back.frozen() == bool(frozen) && back.caches_valid() == bool(frozen));
    UT_CHECK(validate(back, ValidateLevel::Global).ok());

    /* corruption: every flipped byte position class is rejected and the target is unchanged */
    Complex<C> target = k;
    const PersistentId before = content_hash(target);
    for (size_t pos : {size_t(0), size_t(9), b.size() / 3, b.size() / 2, b.size() - 3}) {
      std::vector<uint8_t> bad = b;
      bad[pos] ^= 0x5a;
      litestl::io::ByteReader rb(bad.data(), bad.size());
      UT_CHECK(!read(target, rb));
      UT_CHECK(content_hash(target) == before);
    }
    std::vector<uint8_t> cut(b.begin(), b.begin() + b.size() / 2);
    litestl::io::ByteReader rc(cut.data(), cut.size());
    UT_CHECK(!read(target, rc) && content_hash(target) == before);
  }
  /* a stream of the other coordinate type is refused */
  {
    Complex<C> k;
    UT_CHECK(make_city(k, 3, 4));
    const auto b = bytes_of(k);
    litestl::io::ByteReader r(b.data(), b.size());
    if constexpr (std::is_same_v<C, double>) {
      Complex<geom::Lattice> other;
      const char *err = nullptr;
      UT_CHECK(!read(other, r, &err) && err != nullptr);
    }
    else {
      Complex<double> other;
      UT_CHECK(!read(other, r));
    }
  }
}

/* The same generated model in both instantiations has the same topology. */
static void test_cross_instantiation()
{
  for (int frozen = 0; frozen < 2; frozen++) {
    Complex<geom::Lattice> a;
    Complex<double> b;
    UT_CHECK(make_city(a, 21, 25) && make_city(b, 21, 25));
    if (frozen) {
      UT_CHECK(freeze(a) == FreezeResult::Ok && freeze(b) == FreezeResult::Ok);
    }
    UT_CHECK(topology_hash(a) == topology_hash(b));
    UT_CHECK(!(content_hash(a) == content_hash(b))); /* positions and key differ */
  }
}

template <geom::CoordType C> static void run_all()
{
  test_freeze_basics<C>();
  test_tombstones_and_generations<C>();
  test_io<C>();
  test_format1<C>();
}

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  run_all<geom::Lattice>();
  run_all<double>();
  test_cross_instantiation();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
