/* bench_surfaces (plan P4b §8, W7): reported, not gated. Not in ctest: run by
 * hand. On the road-on-terrain case (gen/road_terrain.h):
 *  - road3 evaluations per second (S alone, and S with its partials to order 2);
 *  - place_road's time, in double and Lattice;
 *  - validate(Global) on the road case against the terrain without the road,
 *    with its Embedding level and the surface checks alone;
 *  - the refined skeleton's flattening point count. */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <type_traits>
#include <vector>

#include "gen/road_terrain.h"
#include "support/splitmix64.h"
#include "ubertopo/topo/face_skeleton.h"
#include "ubertopo/topo/validate.h"

using namespace ubertopo;
using namespace ubertopo::topo;
namespace rt = ubertopo::test::gen::rt;

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

/* the median of n runs of f, in milliseconds */
template <typename F> double median_ms(int n, F &&f)
{
  std::vector<double> t;
  for (int i = 0; i < n; i++) {
    const auto t0 = Clock::now();
    f();
    t.push_back(ms_since(t0));
  }
  std::sort(t.begin(), t.end());
  return t[t.size() / 2];
}

template <geom::CoordType C> void road_case(const rt::Plan &plan)
{
  const char *key = geom::CoordTraits<C>::key;
  /* validate(Global) on the terrain alone */
  rt::Model<C> base;
  rt::build_terrain(base);
  const double v0 = median_ms(5, [&] { validate(base.k, ValidateLevel::Global); });
  /* place_road, from a fresh terrain each time */
  double place = 0;
  {
    std::vector<double> t;
    for (int i = 0; i < 5; i++) {
      rt::Model<C> m;
      rt::build_terrain(m);
      EditorOptions opt;
      opt.validate = ValidateMode::OnCommit;
      Editor<C> ed(m.k, &m.j, nullptr, opt);
      const auto t0 = Clock::now();
      const EditCode c = rt::place_road(ed, m, plan);
      t.push_back(ms_since(t0));
      if (c != EditCode::Ok) {
        printf("%s: place_road failed: %s\n", key, edit_code_name(c));
        return;
      }
    }
    std::sort(t.begin(), t.end());
    place = t[t.size() / 2];
  }
  rt::Model<C> m;
  rt::build_terrain(m);
  EditorOptions opt;
  opt.validate = ValidateMode::None;
  Editor<C> ed(m.k, &m.j, nullptr, opt);
  rt::place_road(ed, m, plan);
  const double v1 = median_ms(5, [&] { validate(m.k, ValidateLevel::Global); });
  const double v1e = median_ms(5, [&] { validate(m.k, ValidateLevel::Embedding); });
  const double v1s = median_ms(5, [&] {
    ValidationReport rep;
    validate_surfaces(m.k, rep);
  });
  const double v0e = median_ms(5, [&] { validate(base.k, ValidateLevel::Embedding); });
  /* the skeleton's flattening points */
  Skeleton<C> sk(m.k);
  size_t points = 0, curved = 0;
  m.k.edges.for_each_live([&](uint32_t e) {
    const size_t n = sk.interior(EdgeRef(e)).size();
    points += n;
    curved += !m.k.edges.template get<".e.curve">()[e].none();
  });
  uint32_t faces = 0;
  m.k.faces.for_each_live([&](uint32_t) { faces++; });
  printf("%s: place_road %.1f ms (OnCommit, validation included)\n", key, place);
  printf("%s: validate(Global) %.1f ms on the terrain (1600 faces), %.1f ms with the road (%u faces)\n", key, v0, v1,
         faces);
  printf("%s:   of which up to Embedding %.1f / %.1f ms, the surface checks alone %.1f ms\n", key, v0e, v1e, v1s);
  printf("%s: skeleton: %zu flattening points on %zu curved edges\n", key, points, curved);
  /* road3 evaluation throughput, at random points of random faces */
  if constexpr (std::is_same_v<C, double>) {
    const auto ev = curves::surface(m.k.geom, m.rb.road);
    litestl::test::SplitMix64 r(7);
    const int ns = int(m.s.size()), nl = int(m.rb.borders.size()) - 1, N = 2000000;
    std::vector<std::array<double, 3>> pts(N);
    for (auto &p : pts) {
      const int i = int(r.below(uint64_t(ns - 1))), k = int(r.below(uint64_t(nl)));
      const double s = m.s[i] + (m.s[i + 1] - m.s[i]) * (double(r.below(1u << 20)) + 0.5) / (1u << 20);
      const double b0 = m.rb.borders[k].eval(s), b1 = m.rb.borders[k + 1].eval(s);
      p = {s, b0 + (b1 - b0) * (double(r.below(1u << 20)) + 0.5) / (1u << 20), double(k)};
    }
    for (int n : {0, 2}) {
      double sink = 0;
      const auto t0 = Clock::now();
      for (const auto &p : pts) {
        curves::Vec<3> out[6];
        ev->eval_piece(p[0], p[1], n, out, int(p[2]));
        sink += out[0][2];
      }
      const double ms = ms_since(t0);
      printf("road3: %.2f M evaluations/s with partials to order %d (sink %g)\n", N / ms / 1e3, n, sink);
    }
  }
}

} // namespace

int main()
{
  rt::Plan plan;
  if (const char *e = rt::plan_road(plan)) {
    printf("plan_road: %s\n", e);
    return 1;
  }
  printf("plan: %zu stations, %d cells killed\n", plan.stations.size(), plan.n_killed);
  road_case<double>(plan);
  road_case<geom::Lattice>(plan);
  return 0;
}
