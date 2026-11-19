/* The geometry store in Complex (plan P4a W5, §6.2 test_geom_store):
 * interning, liveness through the domain groups and inner refs, the
 * accepted kinds per column, hashes independent of insertion order and of
 * unreferenced entries, format-3 round trips and refused streams, the
 * approximation cache, and freeze. */

#include <bit>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <vector>

#include "gen/buildings.h"
#include "ubertopo/curves/alignment.h"
#include "ubertopo/curves/approx.h"
#include "ubertopo/curves/bspline.h"
#include "ubertopo/curves/catenary.h"
#include "ubertopo/curves/cesaro.h"
#include "ubertopo/curves/pcurve.h"
#include "ubertopo/curves/road.h"
#include "ubertopo/topo/freeze.h"
#include "ubertopo/topo/geom_store.h"
#include "ubertopo/topo/io.h"
#include "ubertopo/topo/regions.h"
#include "ut_test.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using namespace ubertopo::curves;
using attr::PoolRef;

namespace {

using K = Complex<double>;

K make_city()
{
  K k;
  test::gen::Model<double> m(ident::derive(ident::tag::TestOnly, std::string_view("geomstore")));
  test::gen::random_city(m, 3, 2);
  BuildReport br;
  RegionReport rr;
  UT_CHECK(m.b.build(k, br, m.policy) && build_regions(k, rr));
  UT_CHECK(k.edges.capacity() >= 4);
  return k;
}

PoolRef add_catenary(K &k, double a)
{
  const double A[3] = {0, 0, 10}, B[3] = {100, 20, 8};
  Catenary3 c;
  UT_CHECK(Catenary3::make(A, B, a, c) == nullptr);
  return k.geom.intern<Catenary3Pool>(pool_id(catenary3_kind()), std::move(c));
}

PoolRef add_cesaro(K &k)
{
  Cesaro2 c;
  UT_CHECK(Cesaro2::arc(0, 0, 0.3, 0.01, 80, c) == nullptr);
  return k.geom.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(c));
}

PoolRef add_alignment(K &k, PoolRef h)
{
  Alignment3 a;
  UT_CHECK(Alignment3::make(k.geom, h, Fn1D::linear(5.0, 9.0, 0, 80), a) == nullptr);
  return k.geom.intern<Alignment3Pool>(pool_id(alignment3_kind()), std::move(a));
}

PoolRef add_bspline(K &k, int dim)
{
  util::Vector<double, 16> knots;
  for (double x : {0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0}) {
    knots.append(x);
  }
  util::Vector<double, 48> ctrl;
  for (int i = 0; i < 4 * dim; i++) {
    ctrl.append(double(i * 3 % 7));
  }
  BSpline b;
  UT_CHECK(BSpline::make(dim, 3, knots, ctrl, attr::kNoRef, 0, 0, 0, b) == nullptr);
  return k.geom.intern<BSplinePool>(pool_id(bspline_kind()), std::move(b));
}

PoolRef add_graph(K &k)
{
  Graph2 g;
  UT_CHECK(Graph2::make(Fn1D::linear(1.0, 2.0, 0, 80), g) == nullptr);
  return k.geom.intern<Graph2Pool>(pool_id(graph2_kind()), std::move(g));
}

PoolRef add_segment(K &k)
{
  Segment2 g;
  UT_CHECK(Segment2::make(0, 1, 0, 3.5, g) == nullptr);
  return k.geom.intern<Segment2Pool>(pool_id(segment2_kind()), std::move(g));
}

/* a road3 whose reference is @p ref, unvalidated (make refuses a reference
 * that isn't a cesaro2, so the accept rule is tested on a raw entry) */
PoolRef add_road_raw(K &k, PoolRef ref)
{
  Road3 d;
  d.reference = ref;
  for (double b : {-3.5, 0.0, 3.5}) {
    d.borders.append(Fn1D::constant(b, 0, 80));
    d.heights.append(Fn1D::constant(10.0, 0, 80));
  }
  return k.geom.intern<Road3Pool>(pool_id(road3_kind()), std::move(d));
}

bool collect(K &k, attr::RefRoot *extra = nullptr)
{
  attr::RefRoot *ex[] = {extra};
  const GeomRoots<double> roots(k, util::span<attr::RefRoot *const>(ex, extra ? 1 : 0));
  return k.geom.collect(roots.roots());
}

std::vector<uint8_t> bytes_of(const K &k)
{
  litestl::io::ByteWriter w;
  write(k, w);
  return std::vector<uint8_t>(w.data(), w.data() + w.size());
}

bool read_bytes(K &k, const std::vector<uint8_t> &b, const char **err)
{
  litestl::io::ByteReader r(b.data(), b.size());
  return read(k, r, err);
}

/* a format-3 stream from groups and a store chosen separately (write() can
 * only produce consistent ones) */
std::vector<uint8_t> raw_stream(const K &g, const attr::PoolStore &store, const attr::PoolStore::Roots &store_roots)
{
  litestl::io::ByteWriter w;
  w.u32(kComplexMagic);
  w.u32(kComplexFormat);
  w.str(geom::CoordTraits<double>::key);
  for (int i = 0; i < 3; i++) {
    w.u64(std::bit_cast<uint64_t>(g.frame.origin[i]));
  }
  w.i64(g.frame.quantum_log2);
  w.u64(g.frame.frame_id);
  w.u8(g.regions_dirty ? 1 : 0);
  w.u8(0);
  for (int i = 0; i < 7; i++) {
    w.u32(0);
  }
  UT_CHECK(store.write(w, store_roots));
  for (const attr::AttrGroup *grp : {&g.verts.group(), &g.edges.group(), &g.coedges.group(), &g.loops.group(),
                                     &g.faces.group(), &g.faces.sides().group(), &g.shells.group(),
                                     &g.regions.group(), &g.global.group()})
  {
    grp->write(w);
  }
  w.u64(0);
  w.u64(0);
  return std::vector<uint8_t>(w.data(), w.data() + w.size());
}

void test_interning_and_liveness()
{
  K k = make_city();
  auto &curve = k.edges.get<".e.curve">();
  const PoolRef c1 = add_catenary(k, 150), c2 = add_catenary(k, 150);
  UT_CHECK(c1 == c2 && k.geom.live_count() == 1); /* deduplicated */
  /* an alignment keeps its horizontal alive through a collect */
  const PoolRef h = add_cesaro(k);
  const PoolRef al = add_alignment(k, h);
  curve.set(0, al);
  curve.set(1, c1);
  UT_CHECK(collect(k) && k.geom.live(h) && k.geom.live(al) && k.geom.live_count() == 3);
  curve.set(0, attr::kNoRef);
  UT_CHECK(collect(k) && !k.geom.live(h) && !k.geom.live(al) && k.geom.live_count() == 1);
  curve.set(1, attr::kNoRef);
  UT_CHECK(collect(k) && k.geom.live_count() == 0);
}

void test_accept()
{
  K k = make_city();
  auto &curve = k.edges.get<".e.curve">();
  /* .e.curve takes 3D kinds only */
  const PoolRef h = add_cesaro(k);
  curve.set(0, h);
  UT_CHECK(!collect(k) && k.geom.live(h));
  {
    const GeomRoots<double> roots(k);
    UT_CHECK(!k.geom.audit(roots.roots(), false));
  }
  RegionReport rr;
  UT_CHECK(build_regions(k, rr));
  UT_CHECK(freeze(k) == FreezeResult::GeometryInvalid && !k.frozen());
  curve.set(0, add_bspline(k, 2));
  UT_CHECK(!collect(k));
  curve.set(0, add_bspline(k, 3));
  UT_CHECK(collect(k));
  /* no pcurves or surfaces in P4a */
  k.coedges.get<".c.pcurve">().set(0, add_catenary(k, 90));
  UT_CHECK(!collect(k));
  k.coedges.get<".c.pcurve">().set(0, attr::kNoRef);
  k.faces.get<".f.surface">().set(0, add_catenary(k, 90));
  UT_CHECK(!collect(k));
  k.faces.get<".f.surface">().set(0, attr::kNoRef);
  UT_CHECK(collect(k));
  /* P4b (plan §4.1): .c.pcurve takes graph2 and segment2, .f.surface takes
   * road3, and neither kind is an edge carrier */
  auto &pc = k.coedges.get<".c.pcurve">();
  auto &sf = k.faces.get<".f.surface">();
  pc.set(0, add_graph(k));
  pc.set(1, add_segment(k));
  sf.set(0, add_road_raw(k, add_cesaro(k)));
  UT_CHECK(collect(k) && k.geom.live_count() == 5); /* bspline, graph, segment, road, its cesaro */
  sf.set(1, add_graph(k));
  UT_CHECK(!collect(k));
  sf.set(1, attr::kNoRef);
  for (PoolRef bad : {add_graph(k), add_segment(k), add_road_raw(k, add_cesaro(k))}) {
    curve.set(1, bad);
    UT_CHECK(!collect(k));
  }
  curve.set(1, attr::kNoRef);
  pc.set(2, add_road_raw(k, add_cesaro(k)));
  UT_CHECK(!collect(k));
  pc.set(2, attr::kNoRef);
  UT_CHECK(collect(k));
  /* inner refs: a road3 names a cesaro2 only; a bspline's source is never a
   * pcurve or surface */
  sf.set(1, add_road_raw(k, add_alignment(k, add_cesaro(k))));
  UT_CHECK(!collect(k));
  sf.set(1, attr::kNoRef);
  UT_CHECK(collect(k));
  {
    util::Vector<double, 16> knots;
    for (double x : {0.0, 0.0, 1.0, 1.0}) {
      knots.append(x);
    }
    util::Vector<double, 48> ctrl;
    for (double x : {0.0, 0.0, 0.0, 1.0, 2.0, 3.0}) {
      ctrl.append(x);
    }
    BSpline b;
    UT_CHECK(BSpline::make(3, 1, knots, ctrl, add_graph(k), 0, 80, 0.5, b) == nullptr);
    curve.set(1, k.geom.intern<BSplinePool>(pool_id(bspline_kind()), std::move(b)));
    UT_CHECK(!collect(k));
    curve.set(1, attr::kNoRef);
    UT_CHECK(collect(k));
  }
}

void test_hashes_and_io()
{
  /* two insertion orders: equal hashes and bytes */
  K a = make_city(), b = make_city();
  const PoolRef a1 = add_catenary(a, 150), a2 = add_catenary(a, 300);
  const PoolRef b2 = add_catenary(b, 300), b1 = add_catenary(b, 150);
  UT_CHECK(!(a1 == b1)); /* different slots */
  a.edges.get<".e.curve">().set(0, a1);
  a.edges.get<".e.curve">().set(2, a2);
  b.edges.get<".e.curve">().set(0, b1);
  b.edges.get<".e.curve">().set(2, b2);
  UT_CHECK(content_hash(a) == content_hash(b) && topology_hash(a) == topology_hash(b));
  UT_CHECK(bytes_of(a) == bytes_of(b));
  /* the curve is content: another one changes the content hash */
  K c = b;
  c.edges.get<".e.curve">().set(2, add_catenary(c, 301));
  UT_CHECK(!(content_hash(c) == content_hash(b)));
  /* an entry interned but never referenced changes nothing */
  const ident::PersistentId h0 = content_hash(a);
  add_catenary(a, 999);
  UT_CHECK(content_hash(a) == h0);
  /* round trip: only the reachable entries, refs resolving to the same curves */
  const std::vector<uint8_t> bytes = bytes_of(a);
  K back;
  const char *err = nullptr;
  UT_CHECK(read_bytes(back, bytes, &err));
  UT_CHECK(content_hash(back) == h0 && bytes_of(back) == bytes && back.geom.live_count() == 2);
  {
    const auto ea = curve3(a.geom, a.edges.get<".e.curve">()[2]);
    const auto eb = curve3(back.geom, back.edges.get<".e.curve">()[2]);
    UT_CHECK(ea && eb);
    Vec<3> pa, pb;
    ea->eval(37.5, 0, &pa);
    eb->eval(37.5, 0, &pb);
    UT_CHECK(pa[0] == pb[0] && pa[1] == pb[1] && pa[2] == pb[2]);
  }
  /* refused: a dangling ref (the store lacks the entry) */
  {
    attr::PoolStore empty;
    register_pools(empty);
    const attr::PoolStore::Roots none{};
    UT_CHECK(empty.canonicalize(attr::PoolStore::MutRoots{}));
    K x;
    UT_CHECK(!read_bytes(x, raw_stream(back, empty, none), &err));
    UT_CHECK(err && std::string_view(err).find("dangling") != std::string_view::npos); /* the audit's report */
  }
  /* refused: a store canonical for other groups (refs swapped) */
  {
    K sw = back;
    auto &col = sw.edges.get<".e.curve">();
    const PoolRef r0 = col[0], r2 = col[2];
    col.set(0, r2);
    col.set(2, r0);
    const GeomRoots<double> roots(back);
    K x;
    UT_CHECK(!read_bytes(x, raw_stream(sw, back.geom, roots.roots()), &err));
    UT_CHECK(err && std::string_view(err).find("canonical") != std::string_view::npos);
  }
  /* refused: entries that are not canonical or not finite (interned without
   * make, so the store writes their bytes as they are) */
  for (int bad = 0; bad < 3; bad++) {
    K x = make_city();
    Cesaro2 raw;
    UT_CHECK(Cesaro2::arc(1, 2, 0.5, 0.01, 50, raw) == nullptr);
    if (bad == 0) {
      raw.theta0 = 7.0; /* unreduced heading */
    }
    else if (bad == 1) {
      raw.x0 = -0.0;
    }
    else {
      raw.segs[0].length = NAN;
    }
    const PoolRef base = x.geom.intern<Cesaro2Pool>(pool_id(cesaro2_kind()), std::move(raw));
    Alignment3 al;
    al.horizontal = base;
    al.z = Fn1D::constant(3.0, 0, 50);
    x.edges.get<".e.curve">().set(0, x.geom.intern<Alignment3Pool>(pool_id(alignment3_kind()), std::move(al)));
    const std::vector<uint8_t> xb = bytes_of(x);
    K y;
    UT_CHECK(!read_bytes(y, xb, &err));
  }
  /* refused: a piece count over the cap (cesaro2's segment count) */
  {
    litestl::io::ByteWriter w;
    for (int i = 0; i < 3; i++) {
      w.u64(0);
    }
    w.u32(Cesaro2::kMaxPieces + 1);
    litestl::io::ByteReader r(w.data(), w.size());
    Cesaro2 out;
    UT_CHECK(!Cesaro2::read(r, out, {}));
  }
}

void test_cache_and_freeze()
{
  K k = make_city();
  const PoolRef al = add_alignment(k, add_cesaro(k));
  k.edges.get<".e.curve">().set(1, al);
  ApproxCache cache;
  const PoolRef sp = cache.get(k.geom, al, 0, 80, 1e-6);
  UT_CHECK(!sp.none() && cache.size() == 1);
  UT_CHECK(collect(k, &cache) && k.geom.live(sp) && cache.size() == 1);
  /* the cache's spline is not content */
  K plain = k;
  UT_CHECK(content_hash(plain) == content_hash(k));
  k.edges.get<".e.curve">().set(1, attr::kNoRef);
  UT_CHECK(collect(k, &cache) && cache.size() == 0 && k.geom.live_count() == 0);
  /* freeze canonicalises; interning afterwards unfreezes */
  K f = make_city();
  f.edges.get<".e.curve">().set(3, add_catenary(f, 150));
  add_catenary(f, 7777); /* unreferenced: dropped by freeze */
  UT_CHECK(freeze(f) == FreezeResult::Ok && f.frozen() && f.geom.live_count() == 1);
  add_catenary(f, 150); /* a hit: still frozen */
  UT_CHECK(f.frozen());
  add_catenary(f, 151); /* a miss */
  UT_CHECK(!f.frozen());
}

} // namespace

int main()
{
  register_column_types();
  test_interning_and_liveness();
  test_accept();
  test_hashes_and_io();
  test_cache_and_freeze();
  return ut_test_end();
}
