/* bench_edit (plan P3b §4, Goal 6, C7): per-primitive cost in Local mode on
 * a room-grid city at ~10^4 and ~10^5 faces, and island imprints on a large
 * terrain face (reported, not gated). Not in ctest: run by hand.
 *
 *   bench_edit            both city sizes, then the terrain case
 *
 * Goal 6: each measured operation stays within 2x between the two sizes. */

#include <chrono>
#include <cstdio>
#include <vector>

#include "gen/buildings.h"
#include "litestl/util/alloc.h"
#include "support/splitmix64.h"
#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/regions.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using test::gen::GridOpts;
using test::gen::gpt;
using test::gen::I3;
using test::gen::Model;

namespace {

using Clock = std::chrono::steady_clock;

double us_since(Clock::time_point t0)
{
  return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

struct Timing {
  double total = 0;
  uint32_t n = 0;
  void add(double us)
  {
    total += us;
    n++;
  }
  double avg() const
  {
    return n ? total / n : 0;
  }
};

constexpr int64_t kCell = 400, kHeight = 300;

template <geom::CoordType C> bool build_city(Complex<C> &k, int n)
{
  Model<C> m(ident::derive(ident::tag::TestOnly, std::string_view("bench-city")));
  GridOpts g;
  g.nx = n;
  g.ny = n;
  g.floors = 2;
  g.cell = kCell;
  g.height = kHeight;
  test::gen::room_grid(m, {0, 0, 0}, g, 1);
  BuildReport br;
  RegionReport rr;
  return m.b.build(k, br, m.policy) && build_regions(k, rr);
}

struct CityResult {
  uint32_t faces = 0;
  Timing split, join, make_vertex, pierce_fail, kill_fail, undo, redo;
  size_t journal_bytes = 0;
};

template <geom::CoordType C> CityResult run_city(int n, uint32_t ops)
{
  CityResult res;
  Complex<C> k;
  Journal<C> j;
  if (!build_city(k, n)) {
    fprintf(stderr, "city %d: build failed\n", n);
    return res;
  }
  res.faces = k.faces.live_count();
  Editor<C> ed(k, &j, nullptr, EditorOptions{ValidateMode::Local});
  ed.options().verify_redo = false;
  litestl::test::SplitMix64 r(42);
  /* warm the index and grid (built on first use) */
  {
    VertRef v;
    ed.make_vertex(gpt<C>({-5000, -5000, 5000}), ident::derive(ident::tag::TestOnly, std::string_view("warm")), &v);
    ed.undo();
  }
  const size_t mem0 = litestl::alloc::getMemorySize();
  for (uint32_t i = 0; i < ops; i++) {
    /* split an edge at its midpoint and join it again */
    const uint32_t e = uint32_t(r.below(k.edges.capacity()));
    if (k.edges.live(e)) {
      const auto vs = k.edge_verts(EdgeRef(e));
      const auto a = k.pos(vs[0]), b = k.pos(vs[1]);
      geom::Point<C> mid;
      bool even = true;
      for (int d = 0; d < 3; d++) {
        const double x = (geom::CoordTraits<C>::to_double(a[d]) + geom::CoordTraits<C>::to_double(b[d])) * 0.5;
        const double q = std::ldexp(x, 8);
        even = even && q == std::floor(q);
      }
      if (even) {
        const auto ga = test::gen::I3{int64_t(std::ldexp(geom::CoordTraits<C>::to_double(a[0]), 8)),
                                      int64_t(std::ldexp(geom::CoordTraits<C>::to_double(a[1]), 8)),
                                      int64_t(std::ldexp(geom::CoordTraits<C>::to_double(a[2]), 8))};
        const auto gb = test::gen::I3{int64_t(std::ldexp(geom::CoordTraits<C>::to_double(b[0]), 8)),
                                      int64_t(std::ldexp(geom::CoordTraits<C>::to_double(b[1]), 8)),
                                      int64_t(std::ldexp(geom::CoordTraits<C>::to_double(b[2]), 8))};
        if (((ga[0] + gb[0]) | (ga[1] + gb[1]) | (ga[2] + gb[2])) % 2 == 0) {
          mid = gpt<C>({(ga[0] + gb[0]) / 2, (ga[1] + gb[1]) / 2, (ga[2] + gb[2]) / 2});
          VertRef m;
          auto t0 = Clock::now();
          const EditCode c1 = ed.split_edge(EdgeRef(e), mid, &m);
          const double t_split = us_since(t0);
          if (c1 == EditCode::Ok) {
            res.split.add(t_split);
            t0 = Clock::now();
            if (ed.undo() == EditCode::Ok) {
              res.undo.add(us_since(t0));
            }
            t0 = Clock::now();
            if (ed.redo() == EditCode::Ok) {
              res.redo.add(us_since(t0));
            }
            t0 = Clock::now();
            if (ed.join_edge(m) == EditCode::Ok) {
              res.join.add(us_since(t0));
            }
          }
        }
      }
    }
    /* two vertices inside neighbouring rooms, and a wire through their wall:
     * refused by validation */
    {
      const int64_t ix = int64_t(r.below(uint64_t(n - 1))), iy = int64_t(r.below(uint64_t(n)));
      const I3 p = {ix * kCell + kCell / 2, iy * kCell + kCell / 2, kHeight / 2};
      VertRef a, b;
      auto t0 = Clock::now();
      const EditCode ca = ed.make_vertex(gpt<C>(p), ident::derive(ident::tag::TestOnly, std::string_view("bench-a"), i), &a);
      res.make_vertex.add(us_since(t0));
      const EditCode cb = ed.make_vertex(gpt<C>({p[0] + kCell, p[1], p[2]}),
                                         ident::derive(ident::tag::TestOnly, std::string_view("bench-b"), i), &b);
      if (ca == EditCode::Ok && cb == EditCode::Ok) {
        t0 = Clock::now();
        const EditCode cw = ed.make_edge(a, b);
        if (cw != EditCode::Ok) {
          res.pierce_fail.add(us_since(t0));
        }
      }
    }
    /* a face vertex cannot be killed: a precondition failure */
    {
      const uint32_t c = uint32_t(r.below(k.coedges.capacity()));
      if (k.coedges.live(c)) {
        const auto t0 = Clock::now();
        if (ed.kill_vertex(k.start_vert(CoedgeRef(c))) != EditCode::Ok) {
          res.kill_fail.add(us_since(t0));
        }
      }
    }
  }
  const size_t mem1 = litestl::alloc::getMemorySize();
  res.journal_bytes = mem1 > mem0 ? mem1 - mem0 : 0;
  return res;
}

void print_city(const char *label, const CityResult &r)
{
  printf("%s: %u faces\n", label, r.faces);
  printf("  split_edge %8.1f us (%u)   join_edge %8.1f us (%u)   make_vertex %8.1f us (%u)\n", r.split.avg(), r.split.n,
         r.join.avg(), r.join.n, r.make_vertex.avg(), r.make_vertex.n);
  printf("  pierce (Invalid) %8.1f us (%u)   kill face vertex (NotIsolated) %8.1f us (%u)\n", r.pierce_fail.avg(),
         r.pierce_fail.n, r.kill_fail.avg(), r.kill_fail.n);
  printf("  undo %8.1f us (%u)   redo %8.1f us (%u)   journal + derived state growth %.1f MB\n", r.undo.avg(), r.undo.n,
         r.redo.avg(), r.redo.n, double(r.journal_bytes) / (1 << 20));
}

/* terrain: a 20 km slab among small rooms; island imprints on its top face
 * re-derive T(f) each time (the C7 exception) */
template <geom::CoordType C> void run_terrain(uint32_t islands)
{
  Model<C> m(ident::derive(ident::tag::TestOnly, std::string_view("bench-terrain")));
  GridOpts g;
  g.nx = 20;
  g.ny = 20;
  g.cell = kCell;
  g.height = kHeight;
  test::gen::room_grid(m, {0, 0, 0}, g, 1);
  const int64_t L = 10000 * 256 / 256;
  m.box({-L, -L, -2000}, {L, L, -1000}, m.label("slab"), Model<C>::outside());
  Complex<C> k;
  Journal<C> j;
  BuildReport br;
  RegionReport rr;
  if (!(m.b.build(k, br, m.policy) && build_regions(k, rr))) {
    fprintf(stderr, "terrain: build failed\n");
    return;
  }
  Editor<C> ed(k, &j, nullptr, EditorOptions{ValidateMode::Local});
  ed.options().verify_redo = false;
  /* the slab's top face: all corners at z = -1000 and |x| = L */
  FaceRef top;
  k.faces.for_each_live([&](uint32_t f) {
    bool all = true;
    uint32_t nv = 0;
    k.for_each_coedge(k.first_loop(FaceRef(f)), [&](CoedgeRef c) {
      const auto p = k.pos(k.start_vert(c));
      nv++;
      all = all && p[2] == gpt<C>({0, 0, -1000})[2] &&
            (p[0] == gpt<C>({L, 0, 0})[0] || p[0] == gpt<C>({-L, 0, 0})[0]);
    });
    if (all && nv == 4) {
      top = FaceRef(f);
    }
  });
  if (!top) {
    fprintf(stderr, "terrain: no top face\n");
    return;
  }
  litestl::test::SplitMix64 r(7);
  Timing first, last;
  for (uint32_t i = 0; i < islands; i++) {
    const int64_t cx = (int64_t(r.below(uint64_t(2 * L / 200 - 4))) + 2) * 200 - L;
    const int64_t cy = (int64_t(r.below(uint64_t(2 * L / 200 - 4))) + 2) * 200 - L;
    const I3 q[3] = {{cx, cy, -1000}, {cx + 100, cy, -1000}, {cx, cy + 100, -1000}};
    const EmbedTarget in = EmbedTarget::face(top);
    const auto t0 = Clock::now();
    ed.begin("island");
    VertRef v[3];
    EdgeRef es[3];
    EditCode c = EditCode::Ok;
    for (int a = 0; a < 3 && c == EditCode::Ok; a++) {
      c = ed.make_vertex(gpt<C>(q[a]), ident::derive(ident::tag::TestOnly, std::string_view("isl"), i, uint32_t(a)), &v[a]);
      if (c == EditCode::Ok) {
        c = ed.embed_vertex(v[a], in);
      }
    }
    for (int a = 0; a < 3 && c == EditCode::Ok; a++) {
      c = ed.make_edge(v[a], v[(a + 1) % 3], ident::kNoId, &es[a], in);
    }
    if (c == EditCode::Ok) {
      c = ed.imprint(top, std::span<const EdgeRef>(es, 3));
    }
    c = c == EditCode::Ok ? ed.commit() : (ed.rollback(), c);
    const double us = us_since(t0);
    if (c == EditCode::Ok) {
      (i < islands / 8 ? first : last).add(us);
    }
  }
  uint32_t holes = 0;
  k.for_each_loop(top, [&](LoopRef) { holes++; });
  printf("terrain (%u faces, %u loops on the slab top at the end):\n", k.faces.live_count(), holes);
  printf("  island imprint transaction: first eighth %8.1f us (%u)   rest %8.1f us (%u)\n", first.avg(), first.n,
         last.avg(), last.n);
}

} // namespace

int main()
{
  topo::register_column_types();
  const uint32_t ops = 2000;
  const CityResult small = run_city<double>(40, ops);
  print_city("city ~1e4", small);
  const CityResult big = run_city<double>(126, ops);
  print_city("city ~1e5", big);
  auto ratio = [](const Timing &a, const Timing &b) { return a.avg() > 0 ? b.avg() / a.avg() : 0.0; };
  printf("ratios (1e5 / 1e4): split %.2f join %.2f make_vertex %.2f pierce %.2f kill %.2f undo %.2f redo %.2f\n",
         ratio(small.split, big.split), ratio(small.join, big.join), ratio(small.make_vertex, big.make_vertex),
         ratio(small.pierce_fail, big.pierce_fail), ratio(small.kill_fail, big.kill_fail), ratio(small.undo, big.undo),
         ratio(small.redo, big.redo));
  run_terrain<double>(400);
  return 0;
}
