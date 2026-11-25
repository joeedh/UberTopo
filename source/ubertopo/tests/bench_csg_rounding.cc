/* How often the result of a CSG tree can't be rounded (plan
 * 2026-11-25-exact-booleans §7, S2c), and what repair makes of that (plan
 * 2026-11-25-boolean-repair). Not a test.
 *
 * Buildings (support/csg_buildings.h) and random polyhedra, at 1 mm and at
 * 2^-13 m; unrotated, turned about the vertical, and turned freely; at output
 * quanta finer by 2^0 to 2^4. Usage: bench_csg_rounding [evaluations]. */

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "ut_test.h"

#include "support/csg_buildings.h"
#include "support/csg_oracle.h"
#include "support/csg_trees.h"
#include "ubertopo/csg/output.h"

using namespace ubertopo;
using namespace ubertopo::test;
using csg::CsgCode;

namespace {

csg::RigidPlacement placement(SplitMix64 &r, int kind)
{
  csg::RigidPlacement p;
  if (kind == 0) {
    return p;
  }
  double q[4];
  for (double &x : q) {
    x = real(r) - 0.5;
  }
  if (kind == 1) {
    q[1] = q[2] = 0;
  }
  const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  const double w = q[0] / n, x = q[1] / n, y = q[2] / n, z = q[3] / n;
  const double m[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                          {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                          {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      p.r[i][j] = m[i][j];
    }
    p.t[i] = 1000 * real(r);
  }
  return p;
}

struct Row {
  uint32_t runs = 0, failed = 0, code[8] = {};
  uint64_t faces = 0, projected = 0, illegal = 0;
  uint32_t repaired = 0, other = 0, rounds = 0, max_rounds = 0, attempts = 0, vanished = 0, joined = 0, max_grown = 0;
  uint64_t before = 0, after = 0;
  double ms = 0, max_ms = 0;
};

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

/* kind 0: buildings, 1: a pile of tetrahedra */
void run(const char *name, int kind, int64_t metre, int n)
{
  Row rows[3][5];
  double arrange_ms = 0, output_ms = 0;
  uint64_t facets = 0, pairs = 0, outputs = 0;
  SplitMix64 r(uint64_t(100 + kind) * uint64_t(metre));
  for (int i = 0; i < n; i++) {
    Building b;
    if (kind == 0) {
      BuildingOptions opt;
      opt.metre = metre;
      opt.windows = 2 + int(r.below(3));
      opt.floors = 1 + int(r.below(2));
      make_building(b, r, opt);
    }
    else {
      std::vector<uint32_t> all;
      for (int t = 0; t < 6; t++) {
        GridBuilder<L> g;
        I3 v[4];
        int s = 0;
        while (s == 0) {
          for (I3 &x : v) {
            x = vertex(r, metre);
          }
          s = orientation(v[0], v[1], v[2], v[3]);
        }
        if (s < 0) {
          std::swap(v[0], v[1]);
        }
        g.face({{v[0], v[2], v[1]}});
        g.face({{v[0], v[1], v[3]}});
        g.face({{v[1], v[2], v[3]}});
        g.face({{v[0], v[3], v[2]}});
        all.push_back(b.leaf(g, b.wall));
      }
      const uint32_t d[2] = {b.e.add_op(csg::Op::Union, {all.data(), 4}), b.e.add_op(csg::Op::Union, {all.data() + 4, 2})};
      b.e.add_op(csg::Op::Difference, d);
    }
    csg::Arrangement a;
    const auto t0 = Clock::now();
    UT_CHECK(csg::arrange(b.o.ops, a) == CsgCode::Ok);
    litestl::util::Vector<csg::Selected> sel;
    csg::select_facets(a, b.e, sel);
    arrange_ms += ms_since(t0);
    facets += a.facets.size();
    pairs += a.stats.pairs;
    for (int turn = 0; turn < 3; turn++) {
      csg::OutputOptions opt;
      opt.placement = placement(r, turn);
      for (int shift = 0; shift < 5; shift++) {
        opt.quantum_shift = shift;
        LComplex k;
        csg::OutputReport rep;
        const auto t1 = Clock::now();
        const CsgCode c = csg::build_output(a, b.e, sel, opt, k, rep);
        output_ms += ms_since(t1);
        outputs++;
        UT_CHECK(c == CsgCode::Ok || c == CsgCode::RoundingFailed);
        Row &row = rows[turn][shift];
        row.runs++;
        row.faces += rep.result.faces.size();
        row.projected += rep.projected;
        row.illegal += rep.result.stats.illegal;
        if (c != CsgCode::Ok) {
          row.failed++;
          row.code[int(rep.round)]++;
          /* and with repair */
          const uint32_t faces = rep.result.faces.size();
          opt.max_repair_rounds = 16;
          const auto t2 = Clock::now();
          csg::OutputReport rr;
          const CsgCode c2 = csg::build_output(a, b.e, sel, opt, k, rr);
          const double ms = ms_since(t2);
          opt.max_repair_rounds = 0;
          row.ms += ms;
          row.max_ms = std::max(row.max_ms, ms);
          if (c2 == CsgCode::Ok) {
            row.repaired++;
            row.rounds += rr.repair.rounds;
            row.max_rounds = std::max(row.max_rounds, rr.repair.rounds);
            row.attempts = std::max(row.attempts, rr.repair.attempts);
            row.vanished += rr.repair.vanished;
            row.joined += rr.repair.joined;
            row.before += faces;
            row.after += rr.result.faces.size();
            row.max_grown = std::max(row.max_grown, uint32_t(rr.result.faces.size()) - std::min(faces, uint32_t(rr.result.faces.size())));
          }
          else {
            row.other += c2 != CsgCode::RoundingFailed;
          }
        }
      }
    }
  }
  printf("\n%s: %d models, %.0f facets and %.0f pairs each; arrangement %.1f ms, output %.1f ms\n", name, n,
         double(facets) / n, double(pairs) / n, arrange_ms / n, output_ms / double(outputs));
  printf("  (failures: same position, builder, regions, radial order, labels, invalid)\n");
  static const char *turns[3] = {"unrotated", "vertical", "free"};
  for (int turn = 0; turn < 3; turn++) {
    for (int shift = 0; shift < 5; shift++) {
      const Row &x = rows[turn][shift];
      printf("  %-9s shift %d: %u of %u failed (%.2f per 1000; %.3f per 1000 faces): %u %u %u %u %u %u; %.1f faces, "
             "%.1f projected, %.1f illegal\n",
             turns[turn], shift, x.failed, x.runs, 1000.0 * x.failed / x.runs, 1000.0 * x.failed / double(x.faces),
             x.code[1], x.code[2], x.code[3], x.code[4], x.code[5], x.code[6], double(x.faces) / x.runs,
             double(x.projected) / x.runs, double(x.illegal) / x.runs);
      if (x.failed) {
        const double q = std::max(1u, x.repaired);
        printf("      repaired %u (%u left, %u other codes): %.2f rounds (max %u), %u attempts more; faces %.1f -> %.1f "
               "(max +%u), %.2f vanished, %.2f joined; %.1f ms, max %.0f\n",
               x.repaired, x.failed - x.repaired, x.other, x.rounds / q, x.max_rounds, x.attempts, double(x.before) / q,
               double(x.after) / q, x.max_grown, x.vanished / q, x.joined / q, x.ms / x.failed, x.max_ms);
      }
    }
  }
}

} // namespace

int main(int argc, char **argv)
{
  const int n = argc > 1 ? std::atoi(argv[1]) : 20;
  run("buildings, 1 mm", 0, 1000, n);
  run("buildings, 2^-13 m", 0, 8192, n);
  run("tetrahedra of 1 m, 1 mm", 1, 1000, n);
  run("tetrahedra of 1 m, 2^-13 m", 1, 8192, n);
  return ut_test_end();
}
