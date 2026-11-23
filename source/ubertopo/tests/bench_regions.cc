/* bench_regions: what a full region rebuild costs, to decide whether
 * incremental region maintenance (OI-P3-1) is needed. Reported, not gated.
 * Not in ctest: run by hand.
 *  - build_regions on room-grid cities of ~1e4, ~3e4 and ~1e5 faces, at the
 *    default band count and at 1 band;
 *  - the editor's rebuild_regions barrier after a face split (Local and None
 *    validation), and its undo, against the split itself;
 *  - build_regions on the road-on-terrain case (curved edges). */

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "gen/buildings.h"
#include "gen/road_terrain.h"
#include "support/splitmix64.h"
#include "ubertopo/topo/edit/editor.h"
#include "ubertopo/topo/regions.h"

using namespace ubertopo;
using namespace ubertopo::topo;
using test::gen::GridOpts;
using test::gen::Model;

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

double median(std::vector<double> v)
{
  if (v.empty()) {
    return 0;
  }
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

/* build_regions on a fresh copy of k, n times; the copy isn't timed */
template <geom::CoordType C> double time_build(const Complex<C> &k, int n)
{
  std::vector<double> t;
  for (int i = 0; i < n; i++) {
    Complex<C> c = k;
    RegionReport rr;
    const auto t0 = Clock::now();
    const bool ok = build_regions(c, rr);
    t.push_back(ms_since(t0));
    if (!ok) {
      printf("  build_regions failed\n");
      return 0;
    }
  }
  return median(t);
}

bool build_city(Complex<double> &k, int n)
{
  Model<double> m(ident::derive(ident::tag::TestOnly, std::string_view("bench-city")));
  GridOpts g;
  g.nx = n;
  g.ny = n;
  g.floors = 2;
  g.cell = 400;
  g.height = 300;
  test::gen::room_grid(m, {0, 0, 0}, g, 1);
  BuildReport br;
  RegionReport rr;
  return m.b.build(k, br, m.policy) && build_regions(k, rr);
}

/* a quad face without holes: its outer loop's four vertices */
bool quad(const Complex<double> &k, FaceRef f, VertRef v[4])
{
  const LoopRef l = k.first_loop(f);
  if (!l || k.next_loop(l) || k.loop_size(l) != 4) {
    return false;
  }
  int i = 0;
  k.for_each_coedge(l, [&](CoedgeRef c) { v[i++] = VertRef(k.coedges.get<".c.v">()[c.i]); });
  return true;
}

void city(int n)
{
  Complex<double> k;
  if (!build_city(k, n)) {
    printf("city %d: build failed\n", n);
    return;
  }
  uint32_t regions = 0;
  k.regions.for_each_live([&](uint32_t) { regions++; });
  const double t_all = time_build(k, 5);
  double t_one = 0;
  {
    litestl::task::ScopedBandCount one(1);
    t_one = time_build(k, 3);
  }
  printf("city %dx%d: %u faces, %u regions: build_regions %.1f ms (1 band %.1f ms)\n", n, n, k.faces.live_count(),
         regions, t_all, t_one);
  /* the barrier after an edit that dirties the regions, which is what such
   * an edit pays: a face split (regions unchanged), and an interior wall
   * removed (two rooms merge, and every region after the removed one in ID
   * order shifts down: about half are renumbered) */
  Journal<double> j;
  Editor<double> ed(k, &j, nullptr, EditorOptions{ValidateMode::Local});
  ed.options().verify_redo = false;
  for (const bool merge : {false, true}) {
    litestl::test::SplitMix64 r(7);
    std::vector<double> edit, barrier[2], undo_b, undo_e;
    int done = 0;
    for (int tries = 0; tries < 20000 && done < 10; tries++) {
      const uint32_t f = uint32_t(r.below(k.faces.capacity()));
      VertRef v[4];
      if (!k.faces.live(f) || !quad(k, FaceRef(f), v)) {
        continue;
      }
      if (merge && (k.region(FaceRef(f), 0).i == 0 || k.region(FaceRef(f), 1).i == 0)) {
        continue; /* not between two rooms */
      }
      auto t0 = Clock::now();
      const EditCode ce = merge ? ed.kill_face(FaceRef(f)) : ed.split_face(FaceRef(f), v[0], v[2]);
      if (ce != EditCode::Ok) {
        continue;
      }
      edit.push_back(ms_since(t0));
      const ValidateMode mode = done % 2 ? ValidateMode::None : ValidateMode::Local;
      ed.options().validate = mode;
      t0 = Clock::now();
      const EditCode c = ed.rebuild_regions();
      barrier[done % 2].push_back(ms_since(t0));
      ed.options().validate = ValidateMode::Local;
      if (c != EditCode::Ok) {
        printf("  rebuild_regions failed: %s\n", edit_code_name(c));
        return;
      }
      t0 = Clock::now();
      ed.undo();
      undo_b.push_back(ms_since(t0));
      t0 = Clock::now();
      ed.undo();
      undo_e.push_back(ms_since(t0));
      done++;
    }
    printf("  %s %.3f ms; rebuild_regions barrier %.1f ms (Local), %.1f ms (None); undo of the barrier "
           "%.1f ms, of the edit %.3f ms (%d samples)\n",
           merge ? "kill_face (merge)" : "split_face", median(edit), median(barrier[0]), median(barrier[1]),
           median(undo_b), median(undo_e), done);
  }
}

void road()
{
  namespace rt = test::gen::rt;
  rt::Plan plan;
  if (rt::plan_road(plan)) {
    return;
  }
  rt::Model<double> m;
  rt::build_terrain(m);
  EditorOptions opt;
  opt.validate = ValidateMode::None;
  Editor<double> ed(m.k, &m.j, nullptr, opt);
  if (rt::place_road(ed, m, plan) != EditCode::Ok) {
    printf("road: place_road failed\n");
    return;
  }
  printf("road on terrain: %u faces, 140 curved edges: build_regions %.1f ms\n", m.k.faces.live_count(),
         time_build(m.k, 5));
}

} // namespace

int main()
{
  topo::register_column_types();
  for (int n : {40, 72, 126}) {
    city(n);
  }
  road();
  return 0;
}
