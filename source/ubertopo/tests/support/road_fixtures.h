#pragma once

/* road3 oracle fixtures (plan P4b §3.2), shared by the quad oracle tool
 * (tools/curve_oracle/road_oracle.cc) and test_curves_road. Plain data
 * only: references as cesaro fixtures, borders and heights as piecewise
 * polynomials in each piece's local sigma. The values are dyadic or come
 * from one correctly rounded division each, so every compiler builds the
 * same doubles. Changing anything here requires regenerating
 * tests/data/road_oracle.inc with the tool. */

#include <vector>

#include "cesaro_fixtures.h"

namespace ubertopo::test {

struct FnData {
  std::vector<double> breaks;              /* n + 1 */
  std::vector<std::vector<double>> pieces; /* n, low degree first, in sigma */
};

struct RoadFixture {
  const char *name;
  CesaroFixture ref;
  std::vector<FnData> borders, heights;
  std::vector<double> stations;  /* sample stations, off every break */
  std::vector<double> fractions; /* sample positions across each link */
};

struct RoadOracleSample {
  double s, t;
  int link;      /* -1: on a border (S only; the partials jump there) */
  double v[15];  /* S, S_s, S_t, S_ss, S_st (x, y, z each) */
};
struct RoadOracleSet {
  const char *name;
  const RoadOracleSample *samples;
  int n;
};

namespace road_fx {

inline FnData constant(double v, double s0, double s1)
{
  return {{s0, s1}, {{v}}};
}

/* piecewise linear through (breaks[i], values[i]) */
inline FnData linear(const std::vector<double> &breaks, const std::vector<double> &values)
{
  FnData f;
  f.breaks = breaks;
  for (size_t i = 0; i + 1 < breaks.size(); i++) {
    f.pieces.push_back({values[i], (values[i + 1] - values[i]) / (breaks[i + 1] - breaks[i])});
  }
  return f;
}

/* a + b on the same breaks, coefficient by coefficient */
inline FnData plus(const FnData &a, const FnData &b)
{
  FnData f = a;
  for (size_t i = 0; i < f.pieces.size(); i++) {
    auto &p = f.pieces[i];
    const auto &q = b.pieces[i];
    if (q.size() > p.size()) {
      p.resize(q.size(), 0.0);
    }
    for (size_t k = 0; k < q.size(); k++) {
      p[k] += q[k];
    }
  }
  return f;
}

/* c f */
inline FnData times(double c, const FnData &a)
{
  FnData f = a;
  for (auto &p : f.pieces) {
    for (double &x : p) {
      x *= c;
    }
  }
  return f;
}

} // namespace road_fx

inline std::vector<RoadFixture> road_fixtures()
{
  using namespace road_fx;
  std::vector<RoadFixture> v;
  const std::vector<double> lanes = {0.125, 0.5, 0.875, 0.0, 1.0};
  {
    /* a straight crowned road on a grade: lanes of 3.5 falling at 1/32 */
    RoadFixture f{"crown_straight", {"line", 0.0, 0.0, 0.25, {{200.0, 0, 0, 0, 0}}, {}}, {}, {}, {}, lanes};
    const FnData z = linear({0, 200}, {10, 10 + 200.0 / 64});
    f.borders = {constant(-3.5, 0, 200), constant(0, 0, 200), constant(3.5, 0, 200)};
    f.heights = {plus(z, constant(-3.5 / 32, 0, 200)), z, plus(z, constant(-3.5 / 32, 0, 200))};
    f.stations = {12.5, 50.25, 100.75, 175.125};
    v.push_back(f);
  }
  {
    /* a left curve (clothoid, arc, clothoid) with superelevation: the right
     * (outer) lane turns from -1/32 to +1/16, the left from -1/32 to -1/16 */
    const double k = 1.0 / 256;
    RoadFixture f{"superelevation",
                  {"spiral_arc", 100.0, -50.0, 1.0, {{64.0, 0, k / 64, 0, 0}, {96.0, k, 0, 0, 0}, {64.0, k, -k / 64, 0, 0}}, {}},
                  {},
                  {},
                  {},
                  lanes};
    const std::vector<double> br = {0, 64, 160, 224};
    const FnData z = linear(br, {20, 22, 25, 27});
    const FnData cr = linear(br, {-1.0 / 32, 1.0 / 16, 1.0 / 16, -1.0 / 32});
    const FnData cl = linear(br, {-1.0 / 32, -1.0 / 16, -1.0 / 16, -1.0 / 32});
    f.borders = {linear(br, {-3.75, -3.75, -3.75, -3.75}), linear(br, {0, 0, 0, 0}), linear(br, {3.75, 3.75, 3.75, 3.75})};
    f.heights = {plus(z, times(3.75, cr)), z, plus(z, times(3.75, cl))};
    f.stations = {10.5, 40.25, 70.125, 120.0625, 170.5, 210.25};
    v.push_back(f);
  }
  {
    /* a right-hand arc whose outer lane widens from 3.5 to 4.5 along a
     * parabola, with a shoulder of 3 beyond it */
    RoadFixture f{"widening", {"right_arc", -20.0, 30.0, -0.5, {{160.0, -1.0 / 128, 0, 0, 0}}, {}}, {}, {}, {}, lanes};
    const std::vector<double> br = {0, 40, 120, 160};
    FnData b1;
    b1.breaks = br;
    b1.pieces = {{3.5}, {3.5, 0, 1.0 / 6400}, {4.5}};
    const FnData b2 = plus(b1, linear(br, {3, 3, 3, 3}));
    const FnData z0 = linear(br, {8, 8 + 40.0 / 128, 8 + 120.0 / 128, 8 + 160.0 / 128});
    const FnData z1 = plus(z0, times(-1.0 / 32, b1));
    const FnData z2 = plus(z1, linear(br, {-3.0 / 16, -3.0 / 16, -3.0 / 16, -3.0 / 16}));
    f.borders = {linear(br, {0, 0, 0, 0}), b1, b2};
    f.heights = {z0, z1, z2};
    f.stations = {20.25, 60.5, 99.125, 140.75};
    v.push_back(f);
  }
  {
    /* a road whose borders cross the reference line (t changes sign) */
    RoadFixture f{"crossing", {"line", 0.0, 0.0, 0.0, {{200.0, 0, 0, 0, 0}}, {}}, {}, {}, {}, lanes};
    f.borders = {linear({0, 200}, {-6, 2}), linear({0, 200}, {-2, 6})};
    f.heights = {constant(3, 0, 200), linear({0, 200}, {3, 5})};
    f.stations = {25.5, 50.0625, 125.25, 190.5};
    v.push_back(f);
  }
  return v;
}

} // namespace ubertopo::test
