#pragma once

/* cesaro2 oracle fixtures (plan P4a §3.8, §6.1), shared by the quad oracle
 * tool (tools/curve_oracle) and test_curves_cesaro. Plain data only: the
 * definitions are doubles, and the oracle evaluates exactly these doubles in
 * quad precision. Changing anything here requires regenerating
 * tests/data/curve_oracle.inc with the tool. */

#include <array>
#include <cmath>
#include <vector>

namespace ubertopo::test {

struct CesaroFixture {
  const char *name;
  double x0, y0, th0;
  std::vector<std::array<double, 5>> segs; /* length, kappa c0..c3 (local sigma) */
  std::vector<double> stations;
};

struct OracleSample {
  double s, x, y, tx, ty;
};
struct OracleSet {
  const char *name;
  const OracleSample *samples;
  int n;
};

inline double fixture_length(const CesaroFixture &f)
{
  double L = 0;
  for (auto &s : f.segs) {
    L += s[0];
  }
  return L;
}

inline std::vector<CesaroFixture> cesaro_fixtures()
{
  std::vector<CesaroFixture> v;
  v.push_back({"line", 3.0, -4.0, 0.3, {{100.0, 0, 0, 0, 0}}, {}});
  v.push_back({"arc", 0.0, 0.0, 0.0, {{200.0, 1.0 / 50, 0, 0, 0}}, {}});
  v.push_back({"clothoid", 10.0, 20.0, -1.0, {{150.0, 0, 1.0 / (600.0 * 150.0), 0, 0}}, {}});
  v.push_back({"tight_clothoid", 0.0, 0.0, 1.5, {{40.0, 0, 1.0 / (15.0 * 40.0), 0, 0}}, {}});
  {
    const double L = 80, k1 = 1.0 / 100;
    v.push_back({"bloss", -50.0, 5.0, 0.7, {{L, 0, 0, 3 * k1 / (L * L), -2 * k1 / (L * L * L)}}, {}});
  }
  v.push_back({"s_curve", 0.0, 0.0, 0.0, {{100.0, -50.0 / (50.0 * 200.0), 1.0 / (50.0 * 200.0), 0, 0}}, {}});
  v.push_back({"slow_spiral", 0.0, 0.0, 0.0, {{5000.0, 0, 1e-9, 0, 0}}, {}});
  v.push_back({"cubic", 1234.5, -987.25, 2.5, {{60.0, 0.01, -2e-4, 3e-6, -2e-8}}, {}});
  {
    /* a 10 km chain: line, clothoid up, arc, clothoid down, repeated; kappa
     * continuous at the clothoid joins, jumps from line to arc are absent */
    CesaroFixture f{"chain_10km", 500.0, -300.0, 0.2, {}, {}};
    double total = 0;
    int j = 0;
    while (total < 10000.0) {
      const double R = 200.0 + double((j * 53) % 800);
      const double sgn = (j / 4) % 2 ? -1.0 : 1.0;
      const double k = sgn / R;
      const double L = 30.0 + double((j * 37) % 50);
      switch (j % 4) {
        case 0:
          f.segs.push_back({L, 0, 0, 0, 0});
          break;
        case 1:
          f.segs.push_back({L, 0, k / L, 0, 0});
          break;
        case 2:
          f.segs.push_back({L, k, 0, 0, 0});
          break;
        default:
          f.segs.push_back({L, k, -k / L, 0, 0});
          break;
      }
      total += L;
      j++;
    }
    v.push_back(f);
  }
  /* stations: 33 uniform plus the joins of short curves */
  for (auto &f : v) {
    const double L = fixture_length(f);
    for (int i = 0; i <= 32; i++) {
      f.stations.push_back(L * double(i) / 32.0);
    }
    if (f.segs.size() <= 4) {
      double s = 0;
      for (size_t i = 0; i + 1 < f.segs.size(); i++) {
        s += f.segs[i][0];
        f.stations.push_back(s);
      }
    }
  }
  return v;
}

} // namespace ubertopo::test
