/* CSG expressions and the facets they keep (plan 2026-11-25-exact-booleans
 * §2.3, §4.5): random trees on random operands against an evaluation written
 * a second time, from points; volume identities, exactly, modulo a prime;
 * and the cases that the meaning was defined for (rooms, windows, thin
 * walls). */

#include <algorithm>
#include <cstdio>
#include <functional>
#include <set>

#include "litestl/util/alloc.h"
#include "ut_test.h"

#include "support/csg_oracle.h"
#include "support/csg_trees.h"
#include "ubertopo/csg/select.h"

using namespace ubertopo;
using namespace ubertopo::test;
using csg::Arrangement;
using csg::Expression;
using csg::kNone;
using csg::kVoid;
using csg::LabelKind;
using csg::Op;
using csg::Selected;

namespace {

using Selection = litestl::util::Vector<Selected>;

/* ---- random trees ---- */

void test_random()
{
  SplitMix64 r(31);
  uint32_t kept = 0, thin_kept = 0, dropped = 0, samples = 0;
  std::vector<uint32_t> src_kind(4, 0);
  for (int s = 0; s < 400; s++) {
    RandomCase c;
    if (!c.make(r)) {
      continue;
    }
    const Operands &o = c.o;
    const Expression &e = c.e;
    const Meaning &m = c.m;
    const Tree &t = c.t;
    const uint32_t n = c.n, *air = c.air, *solid = c.solid;
    auto as_meaning = [&](uint32_t l) {
      return l == kVoid ? -1 : l == air[0] ? -2 : l == air[1] ? -3 : int(l - solid[0]);
    };
    Arrangement a;
    UT_CHECK(csg::arrange(o.ops, a) == csg::CsgCode::Ok);
    Selection sel;
    csg::select_facets(a, e, sel);
    for (uint32_t f = 0; f < a.facets.size(); f++) {
      const csg::Facet &fc = a.facets[int(f)];
      const D3 nrm = unit_normal(a, fc.plane);
      for (const D3 &x : inner_points(a, f, 1, r)) {
        samples++;
        std::vector<uint32_t> region[2];
        for (uint32_t side = 0; side < 2; side++) {
          for (uint32_t l = 0; l < n; l++) {
            region[side].push_back(oracle(a, l, off_plane(x, nrm, side), r));
          }
        }
        const int in[2] = {m.at(t, region[0]), m.at(t, region[1])};
        UT_CHECK(as_meaning(sel[f].label[0]) == in[0] && as_meaning(sel[f].label[1]) == in[1]);
        const bool rule1 = std::max(in[0], -1) != std::max(in[1], -1);
        uint32_t thin_leaf = kNone;
        for (uint32_t p : fc.sources) {
          const uint32_t l = a.in.polygons[int(p)].leaf;
          const uint32_t rf = region[0][l], rb = region[1][l];
          const bool content = m.thin[l] && (rf == rb || (m.label[l][rf] < 0 && m.label[l][rb] < 0));
          if (content && (m.present(t, l, region[0]) == 1 || m.present(t, l, region[1]) == 1)) {
            thin_leaf = std::min(thin_leaf, l);
          }
        }
        UT_CHECK(sel[f].kept == (rule1 || thin_leaf != kNone));
        kept += rule1;
        thin_kept += !rule1 && thin_leaf != kNone;
        dropped += !sel[f].kept;
        if (!sel[f].kept) {
          continue;
        }
        UT_CHECK(std::find(fc.sources.begin(), fc.sources.end(), sel[f].source) != fc.sources.end());
        const uint32_t src = a.in.polygons[int(sel[f].source)].leaf;
        if (!rule1) {
          UT_CHECK(src == thin_leaf);
          continue;
        }
        /* the source changes the result, if any leaf here does by itself */
        auto changes = [&](uint32_t l) {
          for (int side = 0; side < 2; side++) {
            auto other = region[side];
            other[l] = region[1 - side][l];
            if (std::max(m.at(t, other), -1) != std::max(in[side], -1)) {
              return true;
            }
          }
          return false;
        };
        bool any = false;
        for (uint32_t p : fc.sources) {
          any = any || changes(a.in.polygons[int(p)].leaf);
        }
        src_kind[any ? (changes(src) ? 1 : 2) : 3]++;
      }
    }
  }
  printf("samples %u: kept %u, thin %u, dropped %u; sources: change %u, provide %u, neither %u\n", samples, kept,
         thin_kept, dropped, src_kind[1], src_kind[2], src_kind[3]);
  UT_CHECK(samples > 8000 && kept > 2000 && thin_kept > 400 && dropped > 2000);
  /* a source that doesn't change the result provides its label */
  UT_CHECK(src_kind[1] > 10 * src_kind[2]);
}

/* ---- volumes, modulo a prime ---- */

constexpr uint64_t kPrime = (uint64_t(1) << 61) - 1;

uint64_t reduce(const csg::Int128 &v)
{
  uint64_t x = (v.w[0] & kPrime) + ((v.w[0] >> 61) | (v.w[1] << 3));
  x = (x & kPrime) + (x >> 61);
  return x >= kPrime ? x - kPrime : x;
}
uint64_t mul(uint64_t a, uint64_t b)
{
  return reduce(csg::lmath::mul_wide(int64_t(a), int64_t(b)));
}
uint64_t add(uint64_t a, uint64_t b)
{
  const uint64_t s = a + b;
  return s >= kPrime ? s - kPrime : s;
}
uint64_t neg(uint64_t a)
{
  return a ? kPrime - a : 0;
}
uint64_t residue(csg::Int256 v)
{
  const bool minus = v.is_negative();
  if (minus) {
    v = -v;
  }
  uint64_t x = 0;
  for (int i = 3; i >= 0; i--) {
    /* 2^64 = 8 */
    x = add(mul(x, 8), v.w[i] % kPrime);
  }
  return minus ? neg(x) : x;
}
uint64_t inverse(uint64_t a)
{
  uint64_t r = 1;
  for (uint64_t e = kPrime - 2; e; e >>= 1, a = mul(a, a)) {
    r = e & 1 ? mul(r, a) : r;
  }
  return r;
}

/* six times the volume of the cells with a label that test() accepts */
uint64_t volume(const Arrangement &a, const Selection &sel, const std::function<bool(uint32_t)> &test)
{
  std::vector<std::array<uint64_t, 3>> pts;
  for (uint32_t p = 0; p < a.in.points.size(); p++) {
    const csg::HPoint &h = a.in.points[p];
    const uint64_t w = inverse(residue(h.w));
    pts.push_back({mul(residue(h.x[0]), w), mul(residue(h.x[1]), w), mul(residue(h.x[2]), w)});
  }
  uint64_t sum = 0;
  for (uint32_t f = 0; f < a.facets.size(); f++) {
    const bool pos = test(sel[f].label[0]), minus = test(sel[f].label[1]);
    if (pos == minus) {
      continue;
    }
    const csg::Facet &fc = a.facets[int(f)];
    const auto &o = pts[a.edges[int(fc.edges[0].edge)].a];
    uint64_t v = 0;
    for (const csg::FacetEdge &e : fc.edges) {
      const csg::ArrEdge &x = a.edges[int(e.edge)];
      const auto &p = pts[e.dir > 0 ? x.a : x.b], &q = pts[e.dir > 0 ? x.b : x.a];
      for (int i = 0; i < 3; i++) {
        const int j = (i + 1) % 3, k = (i + 2) % 3;
        v = add(v, mul(o[i], add(mul(p[j], q[k]), neg(mul(p[k], q[j])))));
      }
    }
    sum = add(sum, minus ? v : neg(v));
  }
  return sum;
}

void test_volumes()
{
  SplitMix64 r(32);
  uint32_t nonzero = 0, cut = 0;
  for (int s = 0; s < 150; s++) {
    Operands o;
    add_random(r, 3, o);
    add_random(r, 3, o);
    Arrangement a;
    UT_CHECK(csg::arrange(o.ops, a) == csg::CsgCode::Ok);
    uint64_t v[5];
    Selection sel;
    for (int k = 0; k < 5; k++) {
      Expression e;
      const uint32_t solid = e.add_label(LabelKind::Solid);
      std::vector<uint32_t> l[2];
      for (int i = 0; i < 2; i++) {
        for (uint32_t q = 0; q < o.k[size_t(i)]->regions.capacity(); q++) {
          /* for A alone, B is void, and the other way round */
          l[i].push_back(q == 0 || k == 1 - i ? kVoid : solid);
        }
      }
      const uint32_t x[2] = {e.add_leaf(false, l[0]), e.add_leaf(false, l[1])};
      e.add_op(k < 3 ? Op::Union : k == 3 ? Op::Intersection : Op::Difference, x);
      csg::select_facets(a, e, sel);
      v[k] = volume(a, sel, [&](uint32_t y) { return y == solid; });
    }
    UT_CHECK(add(v[2], v[3]) == add(v[0], v[1])); /* union + intersection = A + B */
    UT_CHECK(add(v[4], v[3]) == v[0]);            /* difference + intersection = A */
    nonzero += v[0] != 0 && v[1] != 0;
    cut += v[3] != 0;
    /* per label: the cells add up */
    Expression e;
    std::vector<uint32_t> l[2];
    std::vector<uint32_t> all;
    for (int i = 0; i < 2; i++) {
      for (uint32_t q = 0; q < o.k[size_t(i)]->regions.capacity(); q++) {
        l[i].push_back(q == 0 ? e.add_label(LabelKind::Void) : e.add_label(LabelKind::Solid));
        if (q) {
          all.push_back(l[i].back());
        }
      }
    }
    const uint32_t x[2] = {e.add_leaf(false, l[0]), e.add_leaf(false, l[1])};
    e.add_op(Op::Union, x);
    csg::select_facets(a, e, sel);
    uint64_t sum = 0;
    for (uint32_t y : all) {
      sum = add(sum, volume(a, sel, [&](uint32_t z) { return z == y; }));
    }
    UT_CHECK(sum == v[2]);
  }
  UT_CHECK(nonzero > 30 && cut > 15);
}

/* ---- what the meaning is for ---- */

struct Case {
  Operands o;
  Expression e;
  Arrangement a;
  Selection sel;

  uint32_t box(const I3 &lo, const I3 &hi, uint32_t label, bool thin = false)
  {
    GridBuilder<L> g;
    g.box(lo, hi);
    o.add(g);
    const uint32_t l[2] = {kVoid, label};
    return e.add_leaf(thin, l);
  }
  void run()
  {
    UT_CHECK(csg::arrange(o.ops, a) == csg::CsgCode::Ok);
    csg::select_facets(a, e, sel);
  }
  /* the area of the kept facets that come from a leaf */
  double area(uint32_t leaf) const
  {
    double total = 0;
    for (uint32_t f = 0; f < a.facets.size(); f++) {
      if (!sel[f].kept || a.in.polygons[int(sel[f].source)].leaf != leaf) {
        continue;
      }
      D3 sum = {0, 0, 0};
      for (const csg::FacetEdge &fe : a.facets[int(f)].edges) {
        const csg::ArrEdge &x = a.edges[int(fe.edge)];
        const D3 c = cross(pos(a, fe.dir > 0 ? x.a : x.b), pos(a, fe.dir > 0 ? x.b : x.a));
        sum = {sum[0] + c[0], sum[1] + c[1], sum[2] + c[2]};
      }
      total += std::round(len(sum)) / 2;
    }
    return total;
  }
};

void test_cases()
{
  {
    /* a solid wall with a door: the reveal comes from the door */
    Case c;
    const uint32_t s = c.e.add_label(LabelKind::Solid);
    const uint32_t x[2] = {c.box({0, 0, 0}, {10, 1, 6}, s), c.box({4, -1, 0}, {6, 2, 4}, s)};
    c.e.add_op(Op::Difference, x);
    c.run();
    UT_CHECK(c.area(0) == 2 * (60 - 8) + 2 * 6 + 10 + 8 && c.area(1) == 4 + 4 + 2);
  }
  {
    /* a thin-walled house with a window, and a room name: nothing but walls */
    for (int naming = 0; naming < 2; naming++) {
      Case c;
      const uint32_t s = c.e.add_label(LabelKind::Solid), air = c.e.add_label(LabelKind::Air),
                     room = c.e.add_label(LabelKind::Air);
      const uint32_t x[2] = {c.box({0, 0, 0}, {10, 10, 5}, air, true), c.box({4, -1, 1}, {6, 1, 3}, s)};
      uint32_t y[2] = {c.e.add_op(Op::Difference, x), 0};
      if (naming) {
        y[1] = c.box({1, -2, 0}, {9, 10, 5}, room);
        c.e.add_op(Op::Union, y);
      }
      c.run();
      UT_CHECK(c.area(0) == 2 * 100 + 4 * 50 - 4 && c.area(1) == 0 && c.area(2) == 0);
    }
  }
  {
    /* a cutter that touches a thin wall from either side leaves it there; one
     * on both sides, or around it, doesn't */
    for (int kind = 0; kind < 4; kind++) {
      Case c;
      const uint32_t s = c.e.add_label(LabelKind::Solid), air = c.e.add_label(LabelKind::Air);
      std::vector<uint32_t> x = {c.box({0, 0, 0}, {10, 10, 10}, air, true)};
      if (kind == 0 || kind == 2) {
        x.push_back(c.box({0, 0, 0}, {10, 10, 10}, s)); /* the room itself */
      }
      if (kind == 1 || kind == 2) {
        x.push_back(c.box({10, 0, 0}, {12, 10, 10}, s)); /* outside, flush */
      }
      if (kind == 3) {
        x.push_back(c.box({-1, -1, -1}, {11, 11, 11}, s));
      }
      c.e.add_op(Op::Difference, x);
      c.run();
      UT_CHECK(c.area(0) == (kind == 2 ? 500 : kind == 3 ? 0 : 600) && c.area(1) == 0);
    }
  }
  {
    /* solids with one name fuse; with two, the face between stays, and comes
     * from the first */
    for (int names = 1; names < 3; names++) {
      Case c;
      const uint32_t s = c.e.add_label(LabelKind::Solid), t = c.e.add_label(LabelKind::Solid);
      const uint32_t x[2] = {c.box({0, 0, 0}, {2, 2, 2}, s), c.box({2, 0, 0}, {4, 2, 2}, names == 1 ? s : t, true)};
      c.e.add_op(Op::Union, x);
      c.run();
      UT_CHECK(c.area(0) == (names == 1 ? 20 : 24) && c.area(1) == 20);
    }
  }
  {
    /* Intersection: the first one's labels, air too, inside all the others */
    Case c;
    const uint32_t s = c.e.add_label(LabelKind::Solid), air = c.e.add_label(LabelKind::Air);
    const uint32_t x[2] = {c.box({0, 0, 0}, {10, 10, 10}, air, true), c.box({5, -1, -1}, {12, 11, 11}, s)};
    c.e.add_op(Op::Intersection, x);
    c.run();
    UT_CHECK(c.area(0) == 100 + 4 * 50 && c.area(1) == 0);
    std::set<uint32_t> seen;
    for (uint32_t f = 0; f < c.a.facets.size(); f++) {
      seen.insert(c.sel[f].label[0]);
      seen.insert(c.sel[f].label[1]);
    }
    UT_CHECK((seen == std::set<uint32_t>{kVoid, air}));
  }
  {
    /* a sheet in a solid is thin content; a wall between two solid regions of
     * one name is not */
    for (int kind = 0; kind < 3; kind++) {
      Case c;
      const uint32_t s = c.e.add_label(LabelKind::Solid), t = c.e.add_label(LabelKind::Solid);
      GridBuilder<L> g;
      g.box({0, 0, 0}, {4, 4, 4});
      if (kind == 0) {
        g.face({{{1, 1, 2}, {3, 1, 2}, {3, 3, 2}, {1, 3, 2}}});
      }
      else {
        g.box({4, 0, 0}, {8, 4, 4});
      }
      c.o.add(g);
      const uint32_t l[3] = {kVoid, s, kind == 2 ? t : s};
      c.e.add_leaf(true, {l, size_t(kind == 0 ? 2 : 3)});
      c.run();
      UT_CHECK(c.area(0) == (kind == 0 ? 100 : kind == 1 ? 160 : 176));
    }
  }
  {
    /* air gives way to solid, also when it comes first; the first solid wins */
    Case c;
    const uint32_t s = c.e.add_label(LabelKind::Solid), t = c.e.add_label(LabelKind::Solid),
                   air = c.e.add_label(LabelKind::Air);
    const uint32_t x[3] = {c.box({0, 0, 0}, {6, 6, 6}, air), c.box({0, 0, 0}, {4, 6, 6}, s),
                           c.box({2, 0, 0}, {6, 6, 6}, t)};
    c.e.add_op(Op::Union, x);
    c.run();
    /* s: 4 x 6 x 6, with the face at x = 4; t: 2 x 6 x 6 without it */
    UT_CHECK(c.area(0) == 0 && c.area(1) == 2 * 36 + 4 * 24 && c.area(2) == 36 + 4 * 12);
  }
  {
    /* the source of a face of several leaves: not an air leaf that happens to
     * have a face there */
    for (int kind = 0; kind < 3; kind++) {
      Case c;
      const uint32_t s = c.e.add_label(LabelKind::Solid), t = c.e.add_label(LabelKind::Solid),
                     air = c.e.add_label(LabelKind::Air);
      const uint32_t room = kind < 2 ? c.box({5, 0, 0}, {9, 6, 6}, air) : kNone;
      const uint32_t wall = c.box({0, 0, 0}, {kind == 0 ? 5 : 10, 6, 6}, s);
      if (kind == 0) {
        /* a wall beside the room */
        const uint32_t x[2] = {room, wall};
        c.e.add_op(Op::Union, x);
        c.run();
        UT_CHECK(c.area(0) == 0 && c.area(1) == 2 * 36 + 4 * 30);
        continue;
      }
      /* the room's wall is cut back to the room */
      const uint32_t cut = c.box({5, 0, 0}, {10, 6, 6}, s);
      uint32_t x[2] = {wall, cut};
      uint32_t y[2] = {room, c.e.add_op(Op::Difference, x)};
      if (kind == 2) {
        /* and a solid of another name stands in the cut: it has the face */
        y[0] = y[1];
        y[1] = c.box({5, 0, 0}, {8, 6, 6}, t);
      }
      c.e.add_op(Op::Union, y);
      c.run();
      if (kind == 1) {
        UT_CHECK(c.area(0) == 0 && c.area(1) == 36 + 4 * 30 && c.area(2) == 36);
      }
      else {
        UT_CHECK(c.area(0) == 36 + 4 * 30 && c.area(1) == 0 && c.area(2) == 2 * 36 + 4 * 18);
      }
    }
  }
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_cases();
  test_volumes();
  test_random();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
