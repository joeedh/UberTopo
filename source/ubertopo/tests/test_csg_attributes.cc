/* What a CSG result has from its operands (plan 2026-11-25-exact-booleans §5):
 * attributes, flags, provenance; and the differences between two results. */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

#include "litestl/attr/column.h"
#include "litestl/util/alloc.h"
#include "ut_test.h"

#include "support/csg_world.h"
#include "support/curve_fixture.h"
#include "ubertopo/csg/diff.h"

using namespace ubertopo;
using namespace ubertopo::test;
using litestl::attr::Column;
using litestl::attr::ColumnFlags;
using litestl::attr::MixPolicy;
using litestl::attr::PoolRef;
using topo::Grp;

namespace {

using IntCol = Column<int32_t>;
using DoubleCol = Column<double>;

/* a number on everything: the domain, the operand and the slot */
void paint(LComplex &k, int operand)
{
  auto put = [&](litestl::attr::AttrGroup &g, int domain) {
    auto *c = g.ensure<IntCol>("mark", -1);
    auto *d = g.ensure<DoubleCol>("amount", -1.0);
    for (uint32_t i = 0; i < g.size(); i++) {
      c->set(i, domain * 1000000 + operand * 1000 + int32_t(i));
      d->set(i, 100.0 + i);
    }
    /* what is never copied */
    auto *t = g.ensure<IntCol>("scratch", 7, ColumnFlags::Temp);
    for (uint32_t i = 0; i < g.size(); i++) {
      t->set(i, 8);
    }
  };
  put(k.verts.group(), 1);
  put(k.edges.group(), 2);
  put(k.faces.group(), 3);
  put(k.faces.sides().group(), 4);
  put(k.regions.group(), 5);
}

/* a value, or null */
template <typename Col> const typename Col::value_type *at(const litestl::attr::AttrGroup &g, const char *name, uint32_t i)
{
  const litestl::attr::ColumnBase *c = g.find_any(name);
  return c && &c->type() == &Col::static_type() ? &static_cast<const Col &>(*c)[i] : nullptr;
}

int32_t mark(const litestl::attr::AttrGroup &g, uint32_t i)
{
  const int32_t *m = at<IntCol>(g, "mark", i);
  return m ? *m : -2;
}

double face_area(const LComplex &k, topo::FaceRef f)
{
  double n[3] = {0, 0, 0};
  k.for_each_loop(f, [&](topo::LoopRef l) {
    k.for_each_coedge(l, [&](topo::CoedgeRef c) {
      const auto &p = k.pos(k.start_vert(c)), &q = k.pos(k.start_vert(k.next(c)));
      const double a[3] = {double(p[0].raw), double(p[1].raw), double(p[2].raw)};
      const double b[3] = {double(q[0].raw), double(q[1].raw), double(q[2].raw)};
      for (int i = 0; i < 3; i++) {
        n[i] += a[(i + 1) % 3] * b[(i + 2) % 3] - a[(i + 2) % 3] * b[(i + 1) % 3];
      }
    });
  });
  return std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) / 2;
}

/* random trees: every element's mark is the one of where it comes from */
void test_marks()
{
  SplitMix64 r(81);
  uint32_t faces = 0, reversed = 0, verts = 0, edges = 0, regions = 0, no_vert = 0, no_edge = 0, parts = 0;
  for (int s = 0; s < 400; s++) {
    World w;
    w.op(1, 0, Op(1 + s % 3));
    std::map<uint64_t, const LComplex *> of;
    std::map<uint64_t, csg::GridPlacement> place;
    const int n = 2 + int(r.below(4));
    for (int i = 0; i < n; i++) {
      Operands o;
      add_random(r, 3, o);
      LComplex k = *o.k.back();
      paint(k, i);
      CsgLeaf l;
      l.thin = r.below(2);
      l.kind = r.below(4) ? LabelKind::Solid : LabelKind::Air;
      l.operand = w.store.add(std::move(k));
      l.place = random_placement(r);
      for (int j = 0; j < 3; j++) {
        l.place.offset[j] = int64_t(r.below(3));
      }
      of[uint64_t(i)] = w.store.find(l.operand);
      place[uint64_t(i)] = l.place;
      w.put(10 + uint64_t(i), 1, l);
    }
    BoolOptions opt;
    opt.max_quantum_shift = 14;
    const Out a = test::run(w.t, w.store, opt);
    const double scale = std::ldexp(1.0, a.rep.quantum_shift);
    auto operand = [&](int32_t m) { return (m / 1000) % 1000; };
    auto slot = [&](int32_t m) { return uint32_t(m % 1000); };
    const csg::OutputReport &o = a.rep.output;
    for (uint32_t f = 0; f < o.result.faces.size(); f++) {
      const topo::FaceRef fr(o.face_of[int(f)]);
      const int32_t m = mark(a.k.faces.group(), fr.i);
      UT_CHECK(m / 1000000 == 3);
      const LComplex &k = *of[uint64_t(operand(m))];
      UT_CHECK(o.sources[int(f)].leaf == id(uint64_t(10 + operand(m))));
      UT_CHECK(o.sources[int(f)].face == k.face_id(topo::FaceRef(slot(m))));
      const bool rev = o.sources[int(f)].reversed;
      faces++;
      reversed += rev;
      for (uint32_t side = 0; side < 2; side++) {
        const int32_t sm = mark(a.k.faces.sides().group(), 2 * fr.i + side);
        UT_CHECK(sm == 4000000 + operand(m) * 1000 + int32_t(2 * slot(m) + (rev ? 1 - side : side)));
      }
      UT_CHECK(*at<DoubleCol>(a.k.faces.group(), "amount", fr.i) == 100.0 + slot(m));
      UT_CHECK(*at<IntCol>(a.k.faces.group(), "scratch", fr.i) == 7);
    }
    a.k.verts.for_each_live([&](uint32_t v) {
      const int32_t m = mark(a.k.verts.group(), v);
      if (m < 0) {
        no_vert++;
        return;
      }
      verts++;
      const csg::GridPlacement &pl = place[uint64_t(operand(m))];
      const auto &p = of[uint64_t(operand(m))]->pos(topo::VertRef(slot(m)));
      const csg::I3 q = pl.apply(csg::I3(p[0].raw, p[1].raw, p[2].raw));
      const auto &at = a.k.pos(topo::VertRef(v));
      UT_CHECK(m / 1000000 == 1);
      for (int i = 0; i < 3; i++) {
        UT_CHECK(double(at[i].raw) == double(q[i]) * scale);
      }
    });
    a.k.edges.for_each_live([&](uint32_t e) {
      const int32_t m = mark(a.k.edges.group(), e);
      if (m < 0) {
        no_edge++;
        return;
      }
      edges++;
      UT_CHECK(m / 1000000 == 2);
      /* both ends are on the source edge */
      const LComplex &k = *of[uint64_t(operand(m))];
      const csg::GridPlacement &pl = place[uint64_t(operand(m))];
      const auto src = k.edge_verts(topo::EdgeRef(slot(m))), dst = a.k.edge_verts(topo::EdgeRef(e));
      double p[2][3], len2 = 0;
      for (int x = 0; x < 2; x++) {
        const auto &v = k.pos(src[x]);
        const csg::I3 q = pl.apply(csg::I3(v[0].raw, v[1].raw, v[2].raw));
        for (int i = 0; i < 3; i++) {
          p[x][i] = double(q[i]) * scale;
        }
      }
      for (int i = 0; i < 3; i++) {
        len2 += (p[1][i] - p[0][i]) * (p[1][i] - p[0][i]);
      }
      for (int x = 0; x < 2; x++) {
        const auto &v = a.k.pos(dst[x]);
        double t = 0, d2 = 0;
        for (int i = 0; i < 3; i++) {
          t += (double(v[i].raw) - p[0][i]) * (p[1][i] - p[0][i]) / len2;
        }
        for (int i = 0; i < 3; i++) {
          const double d = p[0][i] + t * (p[1][i] - p[0][i]) - double(v[i].raw);
          d2 += d * d;
        }
        UT_CHECK(d2 <= 1 && t > -1e-3 && t < 1 + 1e-3);
        parts += t > 1e-3 && t < 1 - 1e-3;
      }
    });
    a.k.regions.for_each_live([&](uint32_t q) {
      const int32_t m = mark(a.k.regions.group(), q);
      if (q == 0) {
        UT_CHECK(m < 0);
        return;
      }
      const csg::RegionSides &sides = o.regions[int(q)];
      const uint32_t label = sides.solid ? sides.solid : sides.air;
      UT_CHECK((m >= 0) == (label != 0));
      if (label) {
        regions++;
        UT_CHECK(m / 1000000 == 5);
        const PersistentId region = of[uint64_t(operand(m))]->regions.get<".id">()[slot(m)];
        UT_CHECK(csg::default_label(id(uint64_t(10 + operand(m))), region) == a.rep.labels[int(label)]);
      }
    });
  }
  printf("marks: %u faces (%u reversed), %u vertices (%u without), %u edges (%u without, %u cut ends), %u regions\n", faces,
         reversed, verts, no_vert, edges, no_edge, parts, regions);
  UT_CHECK(faces > 800 && reversed > 30 && verts > 600 && no_vert > 100 && edges > 1000 && no_edge > 100 && parts > 100 &&
           regions > 100);
}

/* a conserved quantity: parts by area, by length */
void test_conserved()
{
  World w;
  LComplex wall = box_complex({0, 0, 0}, {100, 10, 30});
  paint(wall, 0);
  LComplex copy = wall;
  const PersistentId h = w.store.add(std::move(wall));
  w.op(1, 0, Op::Difference);
  w.put(2, 1, w.leaf(h));
  w.put(3, 1, w.leaf(w.box({0, -1, -1}, {20, 11, 31}), {30, 0, 0}));
  topo::PropagationRules rules;
  rules.conserved(Grp::Faces, "amount");
  rules.conserved(Grp::Sides, "amount");
  rules.conserved(Grp::Edges, "amount");
  BoolOptions opt;
  opt.rules = &rules;
  {
    const Out a = test::run(w.t, w.store, opt);
    uint32_t cut = 0;
    a.k.faces.for_each_live([&](uint32_t f) {
      const int32_t m = mark(a.k.faces.group(), f);
      const uint32_t src = uint32_t(m % 1000);
      if (m < 0 || a.rep.output.sources.size() == 0) {
        return;
      }
      const double part = face_area(a.k, topo::FaceRef(f)) / face_area(copy, topo::FaceRef(src));
      const double got = *at<DoubleCol>(a.k.faces.group(), "amount", f);
      UT_CHECK(std::abs(got - part * (100.0 + src)) < 1e-9);
      cut += part < 0.99;
      for (uint32_t side = 0; side < 2; side++) {
        const double gs = *at<DoubleCol>(a.k.faces.sides().group(), "amount", 2 * f + side);
        const double s0 = 100.0 + 2 * src + side, s1 = 100.0 + 2 * src + 1 - side;
        UT_CHECK(std::abs(gs - part * s0) < 1e-9 || std::abs(gs - part * s1) < 1e-9);
      }
    });
    UT_CHECK(cut == 8);
    cut = 0;
    double sum = 0;
    a.k.edges.for_each_live([&](uint32_t e) {
      const int32_t m = mark(a.k.edges.group(), e);
      if (m < 0) {
        return;
      }
      auto len = [](const LComplex &k, uint32_t x) {
        const auto vs = k.edge_verts(topo::EdgeRef(x));
        double s = 0;
        for (int i = 0; i < 3; i++) {
          const double d = double(k.pos(vs[0])[i].raw - k.pos(vs[1])[i].raw);
          s += d * d;
        }
        return std::sqrt(s);
      };
      const double part = len(a.k, e) / len(copy, uint32_t(m % 1000));
      sum += part;
      cut += part < 0.99;
      UT_CHECK(std::abs(*at<DoubleCol>(a.k.edges.group(), "amount", e) - part * (100.0 + m % 1000)) < 1e-9);
    });
    UT_CHECK(cut == 8 && std::abs(sum - (8 + 4 * 0.8)) < 1e-9);
  }
  /* without rules it is copied; the rules are part of the key */
  const Out b = test::run(w.t, w.store);
  b.k.faces.for_each_live([&](uint32_t f) {
    const int32_t m = mark(b.k.faces.group(), f);
    UT_CHECK(m < 0 || *at<DoubleCol>(b.k.faces.group(), "amount", f) == 100.0 + m % 1000);
  });
  {
    /* a rule is for one domain */
    topo::PropagationRules sides;
    sides.conserved(Grp::Sides, "amount");
    BoolOptions so;
    so.rules = &sides;
    const Out c = test::run(w.t, w.store, so);
    uint32_t less = 0;
    c.k.faces.for_each_live([&](uint32_t f) {
      const int32_t m = mark(c.k.faces.group(), f);
      if (m >= 0) {
        UT_CHECK(*at<DoubleCol>(c.k.faces.group(), "amount", f) == 100.0 + m % 1000);
        less += *at<DoubleCol>(c.k.faces.sides().group(), "amount", 2 * f) < 90;
      }
    });
    UT_CHECK(less == 8);
  }
  std::set<Key> keys = {key(csg::evaluation_key(w.t, {})), key(csg::evaluation_key(w.t, opt))};
  topo::PropagationRules other;
  other.conserved(Grp::Faces, "amount");
  opt.rules = &other;
  keys.insert(key(csg::evaluation_key(w.t, opt)));
  topo::PropagationRules third;
  third.conserved(Grp::Sides, "amount");
  opt.rules = &third;
  keys.insert(key(csg::evaluation_key(w.t, opt)));
  UT_CHECK(keys.size() == 4);
}

/* a projected face, whole and cut; a curved edge */
void test_projected()
{
  World w;
  GridBuilder<L> g;
  const I3 c[4] = {{0, 0, 0}, {200, 0, 0}, {200, 200, 0}, {0, 200, 0}};
  I3 hi[4];
  for (int i = 0; i < 4; i++) {
    hi[i] = {c[i][0], c[i][1], i % 2 ? 100 : 140};
  }
  g.face({{c[3], c[2], c[1], c[0]}});
  g.face({{hi[0], hi[1], hi[2], hi[3]}});
  for (int i = 0; i < 4; i++) {
    const int j = (i + 1) % 4;
    g.face({{c[i], c[j], hi[j], hi[i]}});
  }
  topo::BuildPolicy policy;
  policy.allow_projectable_faces = true;
  LComplex k = build(g, policy);
  paint(k, 0);
  uint32_t top = csg::kNone;
  k.faces.for_each_live([&](uint32_t f) { top = k.face_proj_axis(topo::FaceRef(f)) >= 0 ? f : top; });
  UT_CHECK(top != csg::kNone);
  const PersistentId h = w.store.add(std::move(k));
  topo::PropagationRules rules;
  rules.conserved(Grp::Faces, "amount");
  BoolOptions opt;
  opt.rules = &rules;
  w.op(1, 0, Op::Difference);
  w.put(2, 1, w.leaf(h));
  auto sum = [&](const Out &a, uint32_t &pieces, uint32_t &edges) {
    double s = 0;
    pieces = edges = 0;
    a.k.faces.for_each_live([&](uint32_t f) {
      if (mark(a.k.faces.group(), f) == 3000000 + int32_t(top)) {
        s += *at<DoubleCol>(a.k.faces.group(), "amount", f);
        pieces++;
      }
    });
    a.k.edges.for_each_live([&](uint32_t e) { edges += mark(a.k.edges.group(), e) >= 0; });
    return s / (100.0 + top);
  };
  uint32_t pieces, edges;
  UT_CHECK(std::abs(sum(test::run(w.t, w.store, opt), pieces, edges) - 1) < 1e-12 && pieces == 1 && edges == 12);
  /* half of it is cut off, by symmetry half of the area */
  w.put(3, 1, w.leaf(w.box({0, -5, -5}, {300, 205, 300}), {100, 0, 0}));
  const double s = sum(test::run(w.t, w.store, opt), pieces, edges);
  UT_CHECK(std::abs(s - 0.5) < 1e-9 && pieces >= 2 && edges == 8);
  w.set(3, w.leaf(w.box({0, 0, -5}, {10, 10, 300}), {20, 20, 0}));
  const double t = sum(test::run(w.t, w.store, opt), pieces, edges);
  UT_CHECK(t > 0.99 && t < 0.998 && pieces == 2 && edges == 12);
}

/* a curved edge: its pieces add up, and the points of its skeleton have no
 * vertex to copy from */
void test_curved()
{
  topo::Builder<L> b;
  std::vector<uint32_t> keys;
  const int64_t pt[4][2] = {{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
  for (int i = 0; i < 4; i++) {
    keys.push_back(b.vertex(pid(uint64_t(1 + i)), fpt<L>(double(pt[i][0]), double(pt[i][1]), 0)));
  }
  const uint32_t sizes[1] = {4};
  b.face(pid(50), keys, sizes);
  LComplex k;
  topo::BuildReport br;
  topo::RegionReport rr;
  UT_CHECK(b.build(k, br) && topo::build_regions(k, rr));
  uint32_t curved = csg::kNone;
  {
    topo::Editor<L> ed(k, nullptr);
    topo::VertRef v[2];
    k.verts.for_each_live([&](uint32_t i) {
      for (int n = 0; n < 2; n++) {
        v[n] = k.vert_id(topo::VertRef(i)) == pid(uint64_t(1 + n)) ? topo::VertRef(i) : v[n];
      }
    });
    const topo::EdgeRef e = ed.find_edge(v[0], v[1]);
    const auto vs = k.edge_verts(e);
    PoolRef arc;
    double s = 0;
    UT_CHECK(arc_carrier(ed, vs[0], vs[1], vs[0] == v[0] ? 0.4 : -0.4, &arc, &s) == topo::EditCode::Ok);
    UT_CHECK(ed.set_edge_curve(e, arc, 0, s) == topo::EditCode::Ok);
    curved = e.i;
  }
  UT_CHECK(topo::build_regions(k, rr));
  paint(k, 0);
  World w;
  CsgLeaf l = w.leaf(w.store.add(std::move(k)));
  l.thin = true;
  w.op(1, 0, Op::Difference);
  w.put(2, 1, l);
  topo::PropagationRules rules;
  rules.conserved(Grp::Edges, "amount");
  BoolOptions opt;
  opt.rules = &rules;
  for (const bool cut : {false, true}) {
    if (cut) {
      w.put(3, 1, w.leaf(w.box({0, -5000, -10}, {300, 6000, 10}), {1501, 0, 0}));
    }
    const Out a = test::run(w.t, w.store, opt);
    double sum = 0;
    uint32_t n = 0, own = 0;
    a.k.edges.for_each_live([&](uint32_t e) {
      if (mark(a.k.edges.group(), e) == 2000000 + int32_t(curved)) {
        sum += *at<DoubleCol>(a.k.edges.group(), "amount", e) / (100.0 + curved);
        n++;
      }
    });
    a.k.verts.for_each_live([&](uint32_t v) { own += mark(a.k.verts.group(), v) >= 0; });
    UT_CHECK(own == 4 && n > 6 && a.verts.size() > 10);
    UT_CHECK(cut ? sum > 0.9 && sum < 0.95 : std::abs(sum - 1) < 1e-12);
  }
}

/* a whole projected face by itself; edges of two operands at one place */
void test_edges()
{
  {
    GridBuilder<L> g;
    g.face({{{0, 0, 0}, {200, 0, 40}, {230, 190, 0}, {0, 200, 50}}});
    topo::BuildPolicy policy;
    policy.allow_projectable_faces = true;
    LComplex k = build(g, policy);
    paint(k, 0);
    World w;
    CsgLeaf l = w.leaf(w.store.add(std::move(k)));
    l.thin = true;
    w.op(1, 0, Op::Union);
    w.put(2, 1, l);
    const Out a = w.run();
    std::set<int32_t> marks;
    a.k.edges.for_each_live([&](uint32_t e) { marks.insert(mark(a.k.edges.group(), e)); });
    UT_CHECK(a.faces.size() == 1 && a.k.edges.live_count() == 4 && marks.size() == 4 && *marks.begin() >= 2000000);
  }
  for (const bool swap : {false, true}) {
    World w;
    LComplex k[2] = {box_complex({0, 0, 0}, {10, 10, 10}), box_complex({10, 0, 0}, {20, 10, 10})};
    paint(k[0], 0);
    paint(k[1], 1);
    w.op(1, 0, Op::Union);
    for (int i = 0; i < 2; i++) {
      w.put(uint64_t(2 + i), 1, w.leaf(w.store.add(std::move(k[swap ? 1 - i : i]))));
    }
    const Out a = w.run();
    UT_CHECK(a.faces.size() == 11 && a.k.edges.live_count() == 20);
    uint32_t of[2] = {};
    a.k.edges.for_each_live([&](uint32_t e) { of[(mark(a.k.edges.group(), e) / 1000) % 1000]++; });
    UT_CHECK(of[swap ? 1 : 0] == 12 && of[swap ? 0 : 1] == 8);
  }
}

void test_flags()
{
  std::vector<uint32_t> keys;
  const int64_t at[4][3] = {{0, 0, 0}, {100, 0, 0}, {100, 0, 30}, {0, 0, 30}};
  const uint32_t sizes[1] = {4};
  for (const uint8_t flag : {topo::face_flags::Virtual, topo::face_flags::Aperture, uint8_t(0)}) {
    topo::Builder<L> c;
    keys.clear();
    for (int i = 0; i < 4; i++) {
      keys.push_back(c.vertex(pid(uint64_t(1 + i)), fpt<L>(double(at[i][0]), double(at[i][1]), double(at[i][2]))));
    }
    c.face(pid(50), keys, sizes, {}, {}, flag);
    LComplex k;
    topo::BuildReport br;
    topo::RegionReport rr;
    UT_CHECK(c.build(k, br) && topo::build_regions(k, rr));
    World x;
    CsgLeaf l = x.leaf(x.store.add(std::move(k)));
    l.thin = true;
    x.op(1, 0, Op::Difference);
    x.put(2, 1, l);
    x.put(3, 1, x.leaf(x.box({0, -3, -4}, {9, 3, 40}), {20, 0, 0}));
    const Out a = x.run();
    UT_CHECK(a.faces.size() == 2);
    a.k.faces.for_each_live([&](uint32_t f) {
      UT_CHECK((a.k.faces.get<".f.flags">()[f] & (topo::face_flags::Virtual | topo::face_flags::Aperture)) == flag);
    });
  }
}

/* columns: of what contributes only; a name has one type */
void test_columns()
{
  World w;
  LComplex k = box_complex({0, 0, 0}, {10, 10, 10});
  paint(k, 0);
  const PersistentId painted = w.store.add(std::move(k)), plain = w.box({0, 0, 0}, {5, 5, 5});
  LComplex q = box_complex({0, 0, 0}, {10, 10, 10});
  q.faces.group().ensure<DoubleCol>("mark", 0.0);
  const PersistentId other = w.store.add(std::move(q));
  w.op(1, 0, Op::Union);
  w.put(2, 1, w.leaf(plain));
  const Out a = w.run();
  UT_CHECK(!a.k.faces.group().find_any("mark"));
  /* a painted leaf without faces in the result */
  w.put(3, 1, w.leaf(painted, {50, 0, 0}, kNoId, LabelKind::Air));
  const Out b = w.run();
  UT_CHECK(topo::content_hash(b.k) == topo::content_hash(a.k) && !b.k.faces.group().find_any("mark"));
  w.set(3, w.leaf(painted, {50, 0, 0}));
  const Out c = w.run();
  UT_CHECK(c.k.faces.group().find_any("mark") && c.k.regions.group().find_any("mark"));
  uint32_t with = 0, without = 0;
  c.k.faces.for_each_live([&](uint32_t f) { (mark(c.k.faces.group(), f) >= 0 ? with : without)++; });
  UT_CHECK(with == 6 && without == 6);
  UT_CHECK(topo::validate(c.k, topo::ValidateLevel::Global).ok());
  /* the same name for something else */
  w.put(4, 1, w.leaf(other, {90, 0, 0}));
  LComplex out = c.k;
  BoolReport rep;
  UT_CHECK(csg::evaluate(w.t, w.store, {}, out, rep) == CsgCode::BadOperand && rep.node == id(4));
  UT_CHECK(topo::content_hash(out) == topo::content_hash(c.k));
  /* it doesn't matter where it has no face */
  w.set(4, w.leaf(other, {90, 0, 0}, kNoId, LabelKind::Air));
  UT_CHECK(csg::evaluate(w.t, w.store, {}, out, rep) == CsgCode::Ok);
  /* a ref column: its entries come along */
  LComplex s = box_complex({0, 0, 0}, {10, 10, 10});
  PoolRef arc;
  {
    topo::Editor<L> ed(s, nullptr);
    double len = 0;
    const auto vs = s.edge_verts(topo::EdgeRef(0));
    UT_CHECK(arc_carrier(ed, vs[0], vs[1], 0.3, &arc, &len) == topo::EditCode::Ok ||
             arc_carrier(ed, topo::VertRef(0), topo::VertRef(2), 0.3, &arc, &len) == topo::EditCode::Ok);
  }
  auto *refs = s.faces.group().ensure<Column<PoolRef>>("shape");
  for (uint32_t i = 0; i < s.faces.capacity(); i++) {
    refs->set(i, i % 2 ? arc : litestl::attr::kNoRef);
  }
  World y;
  const PersistentId sh = y.store.add(std::move(s));
  y.op(1, 0, Op::Union);
  y.put(2, 1, y.leaf(sh));
  y.put(3, 1, y.leaf(sh, {40, 0, 0}));
  const Out z = y.run();
  const auto *got = z.k.faces.group().find_any("shape");
  UT_CHECK(got != nullptr);
  uint32_t live = 0;
  z.k.faces.for_each_live([&](uint32_t f) {
    const PoolRef r = *at<Column<PoolRef>>(z.k.faces.group(), "shape", f);
    live += !r.none() && z.k.geom.live(r);
    UT_CHECK(r.none() || z.k.geom.live(r));
  });
  UT_CHECK(live == 6 && topo::validate(z.k, topo::ValidateLevel::Global).ok());
  litestl::io::ByteWriter bytes;
  topo::write(z.k, bytes);
  LComplex back;
  litestl::io::ByteReader rd(bytes.bytes.data(), size_t(bytes.bytes.size()));
  UT_CHECK(topo::read(back, rd) && topo::content_hash(back) == topo::content_hash(z.k));
}

/* ---- csg_diff ---- */

/* the lineage, by brute force */
void check_moved(const Out &a, const Out &b, const csg::CsgDiff &d)
{
  auto verts = [](const Out &o, Key f) {
    std::set<Key> s;
    for (const auto &l : o.faces.at(f)) {
      s.insert(l.begin(), l.end());
    }
    return s;
  };
  std::map<Key, std::pair<Key, Key>> src[2];
  for (int i = 0; i < 2; i++) {
    const csg::OutputReport &o = (i ? b : a).rep.output;
    for (uint32_t f = 0; f < o.result.faces.size(); f++) {
      src[i][key(o.names.face[int(f)])] = {key(o.sources[int(f)].leaf), key(o.sources[int(f)].face)};
    }
  }
  std::set<Key> want;
  for (const auto &[f, loops] : b.faces) {
    const std::set<Key> mine = verts(b, f);
    bool same = false, any = false;
    for (const auto &[g, l2] : a.faces) {
      const std::set<Key> theirs = verts(a, g);
      const bool meets = std::any_of(mine.begin(), mine.end(), [&](const Key &k) { return theirs.count(k) > 0; });
      if (src[0][g] == src[1][f] && meets) {
        any = true;
        same = same || g == f;
      }
    }
    if (any && !same) {
      want.insert(f);
    }
  }
  std::set<Key> got;
  for (const csg::FaceLineage &x : d.moved) {
    got.insert(key(x.now));
    const std::set<Key> mine = verts(b, key(x.now));
    size_t best = 0, its = 0;
    for (const auto &[g, l2] : a.faces) {
      if (src[0][g] != src[1][key(x.now)]) {
        continue;
      }
      const std::set<Key> theirs = verts(a, g);
      const size_t n = size_t(std::count_if(mine.begin(), mine.end(), [&](const Key &k) { return theirs.count(k) > 0; }));
      best = std::max(best, n);
      its = g == key(x.before) ? n : its;
    }
    UT_CHECK(its == best && its > 0);
    /* the smallest of several */
    for (const auto &[g, l2] : a.faces) {
      const std::set<Key> theirs = verts(a, g);
      const size_t n = size_t(std::count_if(mine.begin(), mine.end(), [&](const Key &k) { return theirs.count(k) > 0; }));
      const PersistentId gid{g.first, g.second};
      UT_CHECK(src[0][g] != src[1][key(x.now)] || n < best || !(gid < x.before));
    }
  }
  UT_CHECK(got == want);
  for (int i = 1; i < int(d.moved.size()); i++) {
    UT_CHECK(d.moved[i - 1].now < d.moved[i].now);
  }
}

struct Diff {
  csg::CsgDiff d;

  Diff(const Out &a, const Out &b)
  {
    UT_CHECK(csg::csg_diff(a.rep, b.rep, d));
    const std::set<Key> av = names(a.verts), bv = names(b.verts), af = names(a.faces), bf = names(b.faces);
    UT_CHECK(d.vertices.gone.size() == minus(av, bv).size() && d.vertices.added.size() == minus(bv, av).size());
    UT_CHECK(d.faces.gone.size() == minus(af, bf).size() && d.faces.added.size() == minus(bf, af).size());
    UT_CHECK(d.regions.gone.size() == minus(a.regions, b.regions).size());
    UT_CHECK(d.regions.added.size() == minus(b.regions, a.regions).size());
    UT_CHECK(d.vertices.kept.size() == av.size() - d.vertices.gone.size());
    UT_CHECK(d.faces.kept.size() == af.size() - d.faces.gone.size());
    UT_CHECK(d.regions.kept.size() == a.regions.size() - d.regions.gone.size());
    for (const PersistentId &x : d.faces.added) {
      UT_CHECK(bf.count(key(x)) && !af.count(key(x)));
    }
    for (const PersistentId &x : d.faces.gone) {
      UT_CHECK(af.count(key(x)) && !bf.count(key(x)));
    }
    check_moved(a, b, d);
    for (const csg::FaceLineage &x : d.moved) {
      UT_CHECK(bf.count(key(x.now)) && af.count(key(x.before)) && !(x.now == x.before));
    }
  }
};

void test_diff()
{
  World w;
  const PersistentId door = w.box({0, -2, -2}, {8, 12, 102}), window = w.box({0, -2, 0}, {20, 12, 20});
  w.op(1, 0, Op::Difference);
  w.put(2, 1, w.leaf(w.box({0, 0, 0}, {400, 10, 100})));
  w.put(3, 1, w.leaf(window, {50, 0, 40}));
  w.put(4, 1, w.leaf(window, {250, 0, 40}));
  const Out a = w.run();
  {
    const Diff d(a, w.run());
    UT_CHECK(d.d.vertices.gone.size() == 0 && d.d.faces.added.size() == 0 && d.d.moved.size() == 0 &&
             d.d.regions.kept.size() == 2);
  }
  w.set(3, w.leaf(window, {60, 0, 40}));
  const Out b = w.run();
  const Diff d1(a, b);
  UT_CHECK(d1.d.vertices.gone.size() == 0 && d1.d.faces.gone.size() == 0 && d1.d.moved.size() == 0);
  UT_CHECK(w.t.remove(id(3)) == CsgCode::Ok);
  const Out c = w.run();
  const Diff d2(b, c);
  UT_CHECK(d2.d.vertices.gone.size() == 8 && d2.d.faces.gone.size() == 4 && d2.d.faces.added.size() == 0 &&
           d2.d.moved.size() == 0);
  /* a door: four faces are split; the other parts continue them */
  w.put(5, 1, w.leaf(door, {150, 0, 0}));
  const Out e = w.run();
  const Diff d3(c, e);
  UT_CHECK(d3.d.faces.added.size() == 4 + 2 && d3.d.faces.gone.size() == 0 && d3.d.regions.added.size() == 1);
  UT_CHECK(d3.d.moved.size() == 4);
  for (const csg::FaceLineage &x : d3.d.moved) {
    UT_CHECK(std::any_of(d3.d.faces.kept.begin(), d3.d.faces.kept.end(), [&](const PersistentId &y) { return y == x.before; }));
    UT_CHECK(std::any_of(d3.d.faces.added.begin(), d3.d.faces.added.end(), [&](const PersistentId &y) { return y == x.now; }));
  }
  /* and back */
  const Diff d4(e, c);
  UT_CHECK(d4.d.faces.gone.size() == 6 && d4.d.faces.added.size() == 0 && d4.d.moved.size() == 0);
  {
    /* of a failure there is none */
    BoolReport bad;
    LComplex k;
    csg::CsgDiff d;
    UT_CHECK(csg::evaluate(CsgTree(), w.store, {}, k, bad) == CsgCode::EmptyTree);
    UT_CHECK(!csg::csg_diff(a.rep, bad, d) && !csg::csg_diff(bad, a.rep, d) && d.faces.kept.size() == 0);
  }
  /* two leaves of one operand, side by side: the pieces of one's top have
   * vertices in common with the other's */
  for (uint64_t n = 0; n < 8; n++) {
    World v;
    const PersistentId unit = v.box({0, 0, 0}, {100, 100, 100});
    v.op(1, 0, Op::Difference);
    v.op(2, 1, Op::Union);
    v.put(100 + n, 2, v.leaf(unit));
    v.put(200 + n, 2, v.leaf(unit, {100, 0, 0}));
    v.put(300 + n, 2, v.leaf(unit, {-100, 0, 0}));
    const Out p = v.run();
    v.put(3, 1, v.leaf(v.box({0, -2, 50}, {8, 102, 102}), {50, 0, 0}));
    const Diff d(p, v.run());
    UT_CHECK(d.d.moved.size() == 1);
  }
  /* a part that has the name's vertex is cut off: the rest continues it under
   * another name, and the face of the name is a new piece of it */
  for (const int64_t x : {10, 382}) {
    World v;
    v.op(1, 0, Op::Difference);
    v.put(2, 1, v.leaf(v.box({0, 0, 0}, {400, 10, 100})));
    const Out p = v.run();
    v.put(3, 1, v.leaf(v.box({0, -2, -2}, {8, 12, 102}), {x, 0, 0}));
    const Out q = v.run();
    const Diff d(p, q);
    UT_CHECK(d.d.moved.size() == 4);
  }
}

/* random edits */
void test_diff_random()
{
  SplitMix64 r(82);
  uint32_t moved_faces = 0, gone = 0, kept = 0, skipped = 0;
  /* A pillar that is flush with a ring around it: build_regions refuses it
   * until plan 2026-11-24-bvh's W4a is merged. (None is skipped with it.) */
  auto refused = [&](const World &w) {
    LComplex k;
    BoolReport rep;
    const bool no = csg::evaluate(w.t, w.store, {}, k, rep) == CsgCode::RoundingFailed && rep.output.round == csg::RoundCode::Regions;
    skipped += no;
    return no;
  };
  for (int s = 0; s < 60; s++) {
    World w;
    w.op(1, 0, Op::Difference);
    w.put(2, 1, w.leaf(w.box({0, 0, 0}, {20, 20, 20})));
    std::vector<PersistentId> bars;
    for (uint64_t i = 0; i < 5; i++) {
      const int64_t l = 10 + int64_t(r.below(16));
      bars.push_back(r.below(2) ? w.box({0, -1, -1}, {3, l, 24}) : w.box({-1, 0, -1}, {l, 2, 24}));
      const int64_t x = int64_t(r.below(18));
      const int64_t y = int64_t(r.below(18));
      w.put(10 + i, 1, w.leaf(bars.back(), {x, y, 0}));
    }
    if (refused(w)) {
      continue;
    }
    Out a = test::run(w.t, w.store);
    for (int e = 0; e < 4; e++) {
      const uint64_t i = r.below(5);
      const int64_t x = int64_t(r.below(18));
      const int64_t y = int64_t(r.below(18));
      w.set(10 + i, w.leaf(bars[size_t(i)], {x, y, 0}));
      if (refused(w)) {
        continue;
      }
      Out b = test::run(w.t, w.store);
      const Diff d(a, b);
      moved_faces += uint32_t(d.d.moved.size());
      gone += uint32_t(d.d.faces.gone.size());
      kept += uint32_t(d.d.faces.kept.size());
      a = std::move(b);
    }
  }
  printf("diff: %u moved, %u gone, %u skipped, %u kept\n", moved_faces, gone, skipped, kept);
  UT_CHECK(moved_faces > 200 && gone > 300 && kept > 3000 && skipped < 80);
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_marks();
  test_conserved();
  test_projected();
  test_curved();
  test_edges();
  test_flags();
  test_columns();
  test_diff();
  test_diff_random();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
