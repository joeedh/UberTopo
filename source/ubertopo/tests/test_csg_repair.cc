/* Repair of results that can't be rounded (plan 2026-11-25-boolean-repair). */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

#include "litestl/attr/column.h"
#include "litestl/util/alloc.h"
#include "ut_test.h"

#include "support/csg_surface.h"
#include "support/csg_world.h"
#include "ubertopo/csg/diff.h"

using namespace ubertopo;
using namespace ubertopo::test;
using csg::Arrangement;
using csg::kNone;
using csg::OutputOptions;
using csg::OutputReport;
using csg::RoundCode;
using litestl::attr::Column;
using Selection = litestl::util::Vector<csg::Selected>;

namespace {

using I3x = csg::I3;

GridBuilder<L> tetrahedron(SplitMix64 &r, int64_t range, bool flat)
{
  I3 v[4];
  int o = 0;
  while (o == 0) {
    for (I3 &x : v) {
      x = vertex(r, range);
    }
    if (flat) {
      v[0][2] = v[1][2] = v[2][2] = 0;
    }
    o = orientation(v[0], v[1], v[2], v[3]);
  }
  if (o < 0) {
    std::swap(v[0], v[1]);
  }
  GridBuilder<L> g;
  g.face({{v[0], v[2], v[1]}});
  g.face({{v[0], v[1], v[3]}});
  g.face({{v[1], v[2], v[3]}});
  g.face({{v[0], v[3], v[2]}});
  return g;
}

/* a pile: tetrahedra of some labels minus two more, or with sheets through it */
void pile(World &w, SplitMix64 &r, int64_t range, int kind, uint64_t base, uint64_t parent)
{
  w.op(base, parent, Op::Union);
  w.op(base + 1, base, Op::Difference);
  w.op(base + 2, base + 1, Op::Union);
  for (uint64_t i = 0; i < 6; i++) {
    const PersistentId o = w.add(tetrahedron(r, range, kind == 1));
    w.put(base + 10 + i, i < 4 ? base + 2 : base + 1,
          w.leaf(o, {0, 0, 0}, kind == 1 ? id(7000 + i % 3) : id(7000)));
  }
  for (uint64_t i = 0; i < 3 && kind == 2; i++) {
    GridBuilder<L> g;
    I3 p[3];
    do {
      for (I3 &x : p) {
        x = vertex(r, range);
        x[2] = int64_t(i % 2);
      }
    } while ((p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) == (p[1][1] - p[0][1]) * (p[2][0] - p[0][0]));
    g.face({{p[0], p[1], p[2]}});
    CsgLeaf l = w.leaf(w.add(g));
    l.thin = true;
    w.put(base + 20 + i, base, l);
  }
}

struct Evaluated {
  csg::Compiled c;
  Arrangement a;
  Selection sel;

  explicit Evaluated(const World &w)
  {
    PersistentId bad;
    UT_CHECK(csg::compile(w.t, w.store, c, &bad) == CsgCode::Ok);
    UT_CHECK(csg::arrange({c.operands.data(), size_t(c.operands.size())}, a) == CsgCode::Ok);
    csg::select_facets(a, c.expr, sel);
  }
  CsgCode output(const OutputOptions &o, LComplex &k, OutputReport &rep) const
  {
    OutputOptions opt = o;
    const csg::NameSource n = c.names();
    opt.names = &n;
    return csg::build_output(a, c.expr, sel, opt, k, rep);
  }
};

void rotate(csg::RigidPlacement &p, SplitMix64 &r)
{
  double q[4], n = 0;
  for (double &x : q) {
    x = real(r) - 0.5;
    n += x * x;
  }
  n = std::sqrt(n);
  const double w = q[0] / n, x = q[1] / n, y = q[2] / n, z = q[3] / n;
  const double m[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                          {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                          {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      p.r[i][j] = m[i][j];
    }
    p.t[i] = 100 * real(r);
  }
}

D3 placed(const csg::RigidPlacement &p, const D3 &x)
{
  D3 y;
  for (int i = 0; i < 3; i++) {
    y[i] = p.t[i] + p.r[i][0] * x[0] + p.r[i][1] * x[1] + p.r[i][2] * x[2];
  }
  return y;
}

struct Tally {
  uint32_t runs = 0, failed = 0, rounds = 0, max_rounds = 0, attempts = 0, vanished = 0, joined = 0, unclear = 0,
           merged = 0, broken = 0, samples = 0, solid = 0, kept_verts = 0, new_verts = 0, whole_parts = 0, new_faces = 0, replaced = 0, left = 0, owned = 0, edges_same = 0, edges_other = 0,
           fronts = 0, wrong_fronts = 0, right_fronts = 0;
  double worst = 0; /* distance from the exact surface, in quanta */
};

/* one tree: what repair gives must be near the exact result */
void check_one(const Evaluated &v, const OutputOptions &options, SplitMix64 &r, Tally &n)
{
  LComplex k0, k;
  OutputReport rep0, rep;
  n.runs++;
  const CsgCode c0 = v.output(options, k0, rep0);
  if (c0 == CsgCode::Ok) {
    return;
  }
  UT_CHECK(c0 == CsgCode::RoundingFailed && rep0.repair.rounds == 0 && rep0.repair.first == RoundCode::None);
  n.failed++;
  OutputOptions opt = options;
  opt.max_repair_rounds = 16;
  const CsgCode c = v.output(opt, k, rep);
  UT_CHECK(c == CsgCode::Ok || (c == CsgCode::RoundingFailed && rep.round != RoundCode::None && k.verts.live_count() == 0));
  if (c != CsgCode::Ok) {
    n.left++; /* (thin solids on a coarse grid) */
    return;
  }
  const csg::RepairReport &x = rep.repair;
  UT_CHECK(x.first == rep0.round && rep.round == RoundCode::None && (x.rounds > 0 || x.attempts > 0));
  n.rounds += x.rounds;
  n.max_rounds = std::max(n.max_rounds, x.rounds);
  n.attempts += x.attempts;
  n.vanished += x.vanished;
  n.joined += x.joined;
  n.unclear += x.counts.unclear;
  n.merged += x.counts.merged;
  n.broken += x.counts.broken;
  UT_CHECK(topo::validate(k, topo::ValidateLevel::Global).ok());

  /* geometry */
  std::vector<Tri> exact = exact_triangles(v.a, rep0.result);
  for (Tri &t : exact) {
    for (D3 &p : t.p) {
      p = placed(options.placement, p);
    }
  }
  const double bound = std::sqrt(3.0) * (x.attempts ? 0.875 : 0.5) * (x.rounds + 1) + 1e-6;
  k.verts.for_each_live([&](uint32_t i) {
    const auto &p = k.pos(topo::VertRef(i));
    const double d = distance({double(p[0].raw), double(p[1].raw), double(p[2].raw)}, exact);
    n.worst = std::max(n.worst, d);
    UT_CHECK(d <= bound);
  });
  Arrangement b;
  std::vector<Tri> got;
  if (k.faces.live_count() > 0) {
    got = output_triangles(k, rep, b);
  }
  std::map<uint32_t, double> want = volumes(exact), have;
  if (got.size() > 0) {
    have = volumes(got);
  }
  for (const auto &[label, volume] : want) {
    UT_CHECK(std::abs(volume - have[label]) / 6 < area(exact) * bound);
  }
  for (const auto &[label, volume] : have) {
    UT_CHECK(want.count(label));
  }
  for (int i = 0; i < 60; i++) {
    /* near a face, but off it */
    const Tri &t = exact[r.below(exact.size())];
    const D3 d = {real(r) - 0.5, real(r) - 0.5, real(r) - 0.5};
    const double s = bound * (1 + 6 * real(r)) / len(d), wa = real(r), wb = (1 - wa) * real(r);
    D3 p;
    for (int j = 0; j < 3; j++) {
      p[j] = wa * t.p[0][j] + wb * t.p[1][j] + (1 - wa - wb) * t.p[2][j] + s * d[j];
    }
    if (distance(p, exact) <= bound) {
      continue;
    }
    /* the exact label there: back to the operands */
    D3 q;
    for (int j = 0; j < 3; j++) {
      q[j] = double(v.a.in.shift[j]);
      for (int l = 0; l < 3; l++) {
        q[j] += options.placement.r[l][j] * (p[l] - options.placement.t[l]);
      }
    }
    std::vector<uint32_t> region;
    for (uint32_t l = 0; l < v.a.leaf_count; l++) {
      region.push_back(oracle(v.a, l, q, r));
    }
    for (int j = 0; j < 3; j++) {
      p[j] += double(b.in.shift[j]);
    }
    const uint32_t there = got.size() > 0 ? oracle(b, 0, p, r) : 0;
    if (std::find(region.begin(), region.end(), kNone) != region.end() || there == kNone) {
      continue;
    }
    const uint32_t label = v.c.expr.solid_part(v.c.expr.eval(v.c.expr.root, [&](uint32_t l) { return region[l]; }).label);
    UT_CHECK((there == 0 ? csg::kVoid : rep.regions[int(there)].solid) == label);
    n.samples++;
    n.solid += label != csg::kVoid;
  }

  /* names: of the exact result's, where they were */
  std::map<P3, std::set<Key>> at_position;
  std::set<Key> vertex_names, face_names, seen;
  for (uint32_t p = 0; p < rep0.names.vertex.size(); p++) {
    if (!(rep0.names.vertex[int(p)] == kNoId)) {
      const csg::I3 &q = rep0.positions[int(p)];
      at_position[{q[0], q[1], q[2]}].insert(key(rep0.names.vertex[int(p)]));
      vertex_names.insert(key(rep0.names.vertex[int(p)]));
    }
  }
  k.verts.for_each_live([&](uint32_t i) {
    const auto &p = k.pos(topo::VertRef(i));
    const Key name = key(k.vert_id(topo::VertRef(i)));
    UT_CHECK(seen.insert(name).second);
    const auto it = at_position.find({p[0].raw, p[1].raw, p[2].raw});
    if (x.attempts == 0) {
      /* a vertex that was there is where it was. (One that went away can make
       * room for a new one at its position.) */
      const bool own = it != at_position.end() && it->second.count(name);
      UT_CHECK(own || !vertex_names.count(name));
      n.replaced += !own && it != at_position.end();
    }
    vertex_names.count(name) ? n.kept_verts++ : n.new_verts++;
  });
  for (const PersistentId &f : rep0.names.face) {
    face_names.insert(key(f));
  }
  uint32_t kept = 0;
  seen.clear();
  for (const PersistentId &f : rep.names.face) {
    UT_CHECK(seen.insert(key(f)).second);
    kept += uint32_t(face_names.count(key(f)));
    n.new_faces += !face_names.count(key(f));
  }
  UT_CHECK(x.rounds == 0 || kept + x.vanished == face_names.size());

  /* provenance */
  std::map<std::pair<uint32_t, uint32_t>, double> part[2];
  for (int i = 0; i < 2; i++) {
    const OutputReport &q = i ? rep : rep0;
    UT_CHECK(q.provenance.faces.size() == q.result.faces.size());
    for (const auto &f : q.provenance.faces) {
      part[i][{f.leaf, f.face}] += f.part;
    }
  }
  if (x.vanished == 0) {
    for (const auto &[f, p] : part[0]) {
      UT_CHECK(std::abs(part[1][f] - p) < 1e-9);
      n.whole_parts++;
    }
  }
  for (const auto &[f, p] : part[1]) {
    UT_CHECK(p < part[0][f] + 1e-9);
  }
  std::map<std::pair<uint32_t, uint32_t>, double> length;
  for (const auto &e : rep.provenance.edges) {
    UT_CHECK(e.leaf == kNone || e.part > 0);
    if (e.leaf != kNone) {
      length[{e.leaf, e.edge}] += e.part;
    }
  }
  for (const auto &[e, l] : length) {
    UT_CHECK(l < 1 + 1e-9);
  }
  /* an operand's vertex that others came to goes on as that */
  std::set<P3> own_at;
  std::map<P3, int> alone;
  for (uint32_t p = 0; p < rep0.names.vertex.size(); p++) {
    const I3x &q = rep0.positions[int(p)];
    if (!(rep0.names.vertex[int(p)] == kNoId)) {
      alone[{q[0], q[1], q[2]}]++;
      if (rep0.provenance.verts[int(p)].leaf != kNone) {
        own_at.insert({q[0], q[1], q[2]});
      }
    }
  }
  for (uint32_t p = 0; p < rep.names.vertex.size() && x.attempts == 0 && x.rounds == 1; p++) {
    const I3x &q = rep.positions[int(p)];
    const P3 at{q[0], q[1], q[2]};
    if (!(rep.names.vertex[int(p)] == kNoId) && own_at.count(at) && vertex_names.count(key(rep.names.vertex[int(p)]))) {
      UT_CHECK(rep.provenance.verts[int(p)].leaf != kNone);
      n.owned += alone[at] > 1;
    }
  }
  /* an edge whose ends nothing came to is what it was */
  auto edges_of = [&](const OutputReport &o) {
    std::map<std::pair<Key, Key>, std::pair<uint32_t, uint32_t>> m;
    for (uint32_t i = 0; i < o.result.edges.size(); i++) {
      const csg::ResultEdge &e = o.result.edges[int(i)];
      const I3x &p = o.positions[int(e.a)], &q = o.positions[int(e.b)];
      if (alone[{p[0], p[1], p[2]}] == 1 && alone[{q[0], q[1], q[2]}] == 1) {
        const Key k1 = key(o.names.vertex[int(e.a)]), k2 = key(o.names.vertex[int(e.b)]);
        const auto &f = o.provenance.edges[int(i)];
        m[{std::min(k1, k2), std::max(k1, k2)}] = {f.leaf, f.leaf == kNone ? 0 : f.edge};
      }
    }
    return m;
  };
  if (x.attempts == 0 && x.rounds > 0) {
    const auto before = edges_of(rep0), after = edges_of(rep);
    for (const auto &[e, f] : before) {
      const auto it = after.find(e);
      n.edges_same += it != after.end() && it->second == f;
      n.edges_other += it != after.end() && it->second != f;
    }
  }
  /* the front's side */
  for (uint32_t f = 0; f < rep.result.faces.size(); f++) {
    const auto &from = rep.provenance.faces[int(f)];
    const csg::ResultFace &rf = rep.result.faces[int(f)];
    D3 normal = {0, 0, 0};
    for (int l = 0; l + 1 < int(rf.off.size()); l++) {
      for (uint32_t i = rf.off[l]; i < rf.off[l + 1]; i++) {
        const D3 p = at(rep.positions[int(rf.pts[int(i)])]);
        const D3 q = at(rep.positions[int(rf.pts[int(i + 1 < rf.off[l + 1] ? i + 1 : rf.off[l])])]);
        const D3 o = at(rep.positions[int(rf.pts[0])]);
        const D3 c = cross(sub(p, o), sub(q, o));
        normal = {normal[0] + c[0], normal[1] + c[1], normal[2] + c[2]};
      }
    }
    for (const csg::Polygon &g : v.a.in.polygons) {
      if (g.leaf != from.leaf || g.face != from.face) {
        continue;
      }
      D3 m = placed(options.placement, unit_normal(v.a, g.plane));
      for (int i = 0; i < 3; i++) {
        m[i] = (m[i] - options.placement.t[i]) * g.sign;
      }
      const double cosine = len(normal) > 0 ? dot(m, normal) / len(normal) : 0;
      if (std::abs(cosine) > 0.9 && len(normal) > 200) {
        n.fronts += from.reversed;
        n.wrong_fronts += (cosine < 0) != from.reversed;
        n.right_fronts += (cosine < 0) == from.reversed;
      }
      break;
    }
  }
}

void print(const char *name, const Tally &n)
{
  printf("%s: %u of %u failed (%u for good); %.2f rounds (max %u), %u more attempts; %u points merged, %u faces broken, %u cells unclear; "
         "%u faces vanished, %u joined, %u new; %u vertices kept, %u new; %u samples (%u solid), %u whole parts, %u replaced, at most "
         "%.2f quanta off\n",
         name, n.failed, n.runs, n.left, n.rounds / std::max(1.0, double(n.failed)), n.max_rounds, n.attempts, n.merged, n.broken,
         n.unclear, n.vanished, n.joined, n.new_faces, n.kept_verts, n.new_verts, n.samples, n.solid, n.whole_parts, n.replaced,
         n.worst);
}

void test_random()
{
  SplitMix64 r(91);
  struct Case {
    const char *name;
    int kind, count;
    int64_t range;
    bool turned;
  };
  const Case cases[] = {{"tetrahedra, 20", 0, 30, 20, false},   {"tetrahedra, 1000", 0, 80, 1000, false},
                        {"tetrahedra, turned", 0, 80, 1000, true}, {"three labels, 20", 1, 30, 20, false},
                        {"three labels, turned", 1, 40, 100, true}, {"sheets, 100", 2, 40, 100, true},
                        {"sheets, 20", 2, 25, 20, false}};
  Tally all;
  for (const Case &c : cases) {
    Tally n;
    for (int s = 0; s < c.count; s++) {
      World w;
      pile(w, r, c.range, c.kind, 100, 0);
      const Evaluated v(w);
      OutputOptions opt;
      if (c.turned) {
        rotate(opt.placement, r);
      }
      check_one(v, opt, r, n);
    }
    print(c.name, n);
    UT_CHECK(n.failed > 8 && n.samples > 100 && n.solid > 2 && n.replaced < n.kept_verts / 10 && n.left <= n.failed / 50);
    printf("   %u owned, edges %u same, %u other; fronts %u reversed, %u right, %u wrong\n", n.owned, n.edges_same,
           n.edges_other, n.fronts, n.right_fronts, n.wrong_fronts);
    UT_CHECK(n.edges_other <= n.edges_same / 30 && n.wrong_fronts <= n.right_fronts / 100);
    all.replaced += n.replaced;
    all.owned += n.owned;
    all.edges_other += n.edges_other;
    all.fronts += n.fronts;
    all.rounds += n.rounds;
    all.max_rounds = std::max(all.max_rounds, n.max_rounds);
    all.attempts += n.attempts;
    all.joined += n.joined;
    all.unclear += n.unclear;
    all.vanished += n.vanished;
    all.broken += n.broken;
  }
  UT_CHECK(all.max_rounds > 3 && all.attempts > 0 && all.joined > 100 && all.unclear > 100 && all.vanished > 10 &&
           all.broken > 100 && all.owned > 20 && all.fronts > 100 && all.replaced < 1000 &&
           all.edges_other < 100); /* (more without what find_edges does for joined faces) */
}

/* ---- what isn't touched ---- */

using IntCol = Column<int32_t>;
using DoubleCol = Column<double>;

LComplex painted(LComplex k, int operand)
{
  auto put = [&](litestl::attr::AttrGroup &g, int domain) {
    auto *c = g.ensure<IntCol>("mark", -1);
    auto *d = g.ensure<DoubleCol>("amount", -1.0);
    for (uint32_t i = 0; i < g.size(); i++) {
      c->set(i, domain * 1000000 + operand * 1000 + int32_t(i));
      d->set(i, 1.0);
    }
  };
  put(k.verts.group(), 1);
  put(k.edges.group(), 2);
  put(k.faces.group(), 3);
  put(k.faces.sides().group(), 4);
  return k;
}
template <typename Col>
typename Col::value_type value(const litestl::attr::AttrGroup &g, const char *name, uint32_t i)
{
  const litestl::attr::ColumnBase *c = g.find_any(name);
  UT_CHECK(c && &c->type() == &Col::static_type());
  return static_cast<const Col &>(*c)[i];
}

/* a house of boxes */
void house(World &w, uint64_t base, uint64_t parent, int mark)
{
  w.op(base, parent, Op::Difference);
  auto box = [&](uint64_t i, const I3 &lo, const I3 &size) {
    CsgLeaf l = w.leaf(w.store.add(painted(box_complex({0, 0, 0}, size), mark + int(i))), lo);
    w.put(base + 1 + i, base, l);
  };
  box(0, {5000, 5000, 5000}, {4000, 3000, 2500});
  box(1, {5200, 5200, 5200}, {3600, 2600, 2100});
  box(2, {6000, 4900, 5800}, {700, 500, 900});
  box(3, {7400, 4900, 5800}, {700, 500, 900});
}

void test_elsewhere()
{
  SplitMix64 r(94);
  uint32_t repaired = 0;
  for (int s = 0; s < 40; s++) {
    World w;
    w.op(1, 0, Op::Union);
    house(w, 10, 1, 0);
    BoolOptions opt;
    if (s % 2) {
      rotate(opt.placement, r);
    }
    const Out a = test::run(w.t, w.store, opt);
    UT_CHECK(a.rep.output.repair.rounds == 0);
    pile(w, r, 20 + int64_t(r.below(60)), 0, 100, 1);
    opt.max_repair_rounds = 0;
    LComplex k;
    BoolReport rep;
    const CsgCode c = csg::evaluate(w.t, w.store, opt, k, rep);
    UT_CHECK(c == CsgCode::Ok || c == CsgCode::RoundingFailed);
    opt.max_repair_rounds = 16;
    const Out b = test::run(w.t, w.store, opt);
    UT_CHECK((c == CsgCode::Ok) == (b.rep.output.repair.first == RoundCode::None));
    if (c == CsgCode::Ok || b.rep.output.repair.attempts > 0) {
      continue; /* (another threshold moves other points too, if it is turned) */
    }
    repaired++;
    uint32_t verts = 0;
    for (const auto &[name, p] : a.verts) {
      UT_CHECK(b.verts.count(name) && b.verts.at(name) == p);
      verts++;
    }
    for (const auto &[name, loops] : a.faces) {
      UT_CHECK(b.faces.count(name) && b.faces.at(name) == loops);
    }
    for (const Key &name : a.regions) {
      UT_CHECK(b.regions.count(name));
    }
    UT_CHECK(verts == 8 + 8 + 16);
    /* and all the attributes */
    std::map<Key, std::array<int32_t, 3>> marks;
    std::map<Key, int32_t> vmarks, emarks;
    auto gather = [&](const Out &o, bool check) {
      o.k.faces.for_each_live([&](uint32_t f) {
        const Key name = key(o.k.face_id(topo::FaceRef(f)));
        const std::array<int32_t, 3> m = {value<IntCol>(o.k.faces.group(), "mark", f),
                                          value<IntCol>(o.k.faces.sides().group(), "mark", 2 * f),
                                          value<IntCol>(o.k.faces.sides().group(), "mark", 2 * f + 1)};
        if (check) {
          UT_CHECK(!marks.count(name) || marks[name] == m);
        }
        else {
          marks[name] = m;
        }
      });
      o.k.verts.for_each_live([&](uint32_t v) {
        const Key name = key(o.k.vert_id(topo::VertRef(v)));
        const int32_t m = value<IntCol>(o.k.verts.group(), "mark", v);
        if (check) {
          UT_CHECK(!vmarks.count(name) || vmarks[name] == m);
        }
        else {
          vmarks[name] = m;
        }
      });
      o.k.edges.for_each_live([&](uint32_t e) {
        const Key name = key(o.k.edge_id(topo::EdgeRef(e)));
        const int32_t m = value<IntCol>(o.k.edges.group(), "mark", e);
        if (check) {
          UT_CHECK(!emarks.count(name) || emarks[name] == m);
        }
        else {
          emarks[name] = m;
        }
      });
    };
    gather(a, false);
    gather(b, true);
    UT_CHECK(marks.size() == a.faces.size() && vmarks.size() == a.verts.size());
    csg::CsgDiff d;
    UT_CHECK(csg::csg_diff(a.rep, b.rep, d) && d.faces.gone.size() == 0 && d.vertices.gone.size() == 0 &&
             d.faces.kept.size() == a.faces.size());
  }
  printf("elsewhere: %u repaired\n", repaired);
  UT_CHECK(repaired > 15);
}

/* a house whose wall itself has the trouble: the rest stays as it was, and the
 * wall stays mostly one face */
void test_wall()
{
  SplitMix64 r(95);
  uint32_t repaired = 0, touched = 0, same = 0, pieces = 0, big = 0, others = 0;
  for (int s = 0; s < 200; s++) {
    World w;
    w.op(1, 0, Op::Difference);
    house(w, 10, 1, 0);
    /* thin wedges into the front wall, near each other */
    for (uint64_t i = 0; i < 3; i++) {
      GridBuilder<L> g;
      const int64_t x = 5600 + int64_t(r.below(12)), z = 5500 + int64_t(r.below(12));
      const I3 p[4] = {{x, 4990, z}, {x + 8 + int64_t(r.below(20)), 4990, z + int64_t(r.below(5))},
                       {x + int64_t(r.below(5)), 4990, z + 8 + int64_t(r.below(20))}, {x + 3, 5000 + int64_t(1 + r.below(30)), z + 3}};
      const bool out = orientation(p[0], p[1], p[2], p[3]) > 0;
      const I3 v[4] = {p[out ? 0 : 1], p[out ? 1 : 0], p[2], p[3]};
      g.face({{v[0], v[2], v[1]}});
      g.face({{v[0], v[1], v[3]}});
      g.face({{v[1], v[2], v[3]}});
      g.face({{v[0], v[3], v[2]}});
      w.put(50 + i, 1, w.leaf(w.store.add(painted(build(g), 50 + int(i)))));
    }
    BoolOptions opt;
    rotate(opt.placement, r);
    opt.max_repair_rounds = 0;
    LComplex k;
    BoolReport rep;
    if (csg::evaluate(w.t, w.store, opt, k, rep) == CsgCode::Ok) {
      continue;
    }
    UT_CHECK(rep.code == CsgCode::RoundingFailed);
    opt.max_repair_rounds = 16;
    const Out b = test::run(w.t, w.store, opt);
    if (b.rep.output.repair.attempts > 0) {
      continue;
    }
    repaired++;
    const OutputReport &x = rep.output, &y = b.rep.output;
    UT_CHECK(y.repair.rounds > 0);
    /* faces whose points are all alone where they are keep name and loops */
    std::map<P3, int> there;
    std::set<Key> exact_names;
    for (const PersistentId &name : x.names.vertex) {
      const uint32_t p = uint32_t(&name - x.names.vertex.data());
      if (!(name == kNoId)) {
        const csg::I3 &q = x.positions[int(p)];
        there[{q[0], q[1], q[2]}]++;
        exact_names.insert(key(name));
      }
    }
    for (uint32_t f = 0; f < x.result.faces.size(); f++) {
      const Key name = key(x.names.face[int(f)]);
      std::set<Key> mine, now;
      bool alone = true;
      for (uint32_t p : x.result.faces[int(f)].pts) {
        const csg::I3 &q = x.positions[int(p)];
        alone = alone && there[{q[0], q[1], q[2]}] == 1;
        mine.insert(key(x.names.vertex[int(p)]));
      }
      if (!alone || !b.faces.count(name)) {
        touched++;
        continue;
      }
      for (const auto &l : b.faces.at(name)) {
        now.insert(l.begin(), l.end());
      }
      same += mine == now;
      /* (or something that it meets did) */
      const bool more = std::any_of(now.begin(), now.end(), [&](const Key &n) { return !exact_names.count(n); });
      others += mine != now && !more;
    }
    /* the front wall: its large piece has its name */
    double most = 0, sum = 0;
    for (const auto &f : y.provenance.faces) {
      if (b.rep.leaf_ids[int(f.leaf)] == id(11)) {
        big += f.part > 0.5;
        sum += f.part;
        most = std::max(most, f.part);
        pieces += f.part < 0.5 && f.part > 0;
      }
    }
    UT_CHECK(most > 0.8 && sum > 5.5);
  }
  printf("wall: %u repaired; %u faces touched, %u the same, %u with other vertices, %u large pieces, %u small ones\n", repaired,
         touched, same, others, big, pieces);
  UT_CHECK(repaired > 30 && same > 2000 && others < same / 50 && big > 6 * repaired * 98 / 100);
}

/* a solid that rounding turns inside out, and nothing else: it goes away */
void test_inverted()
{
  SplitMix64 r(98);
  uint32_t found = 0;
  for (int s = 0; s < 6000 && found < 5; s++) {
    World w;
    w.op(1, 0, Op::Union);
    w.put(2, 1, w.leaf(w.add(tetrahedron(r, 1, false))));
    const Evaluated v(w);
    OutputOptions opt;
    rotate(opt.placement, r);
    LComplex k;
    OutputReport rep;
    if (v.output(opt, k, rep) == CsgCode::Ok || rep.round != RoundCode::Labels) {
      continue;
    }
    found++;
    opt.max_repair_rounds = 16;
    OutputReport fixed;
    UT_CHECK(v.output(opt, k, fixed) == CsgCode::Ok && k.faces.live_count() == 0);
    UT_CHECK(fixed.repair.vanished == 4 && fixed.repair.counts.merged == 0 && fixed.repair.rounds == 1 &&
             fixed.repair.attempts == 0);
  }
  printf("inverted: %u\n", found);
  UT_CHECK(found == 5);
}

/* which label wins doesn't depend on leaves far away */
void test_ties()
{
  SplitMix64 r(99);
  uint32_t unclear = 0;
  for (int s = 0; s < 25; s++) {
    const SplitMix64 same = r;
    Out o[2];
    for (int far = 0; far < 2; far++) {
      World w;
      w.op(1, 0, Op::Union);
      if (far) {
        /* it has the labels in another order */
        for (uint64_t i = 0; i < 3; i++) {
          w.put(2 + i, 1, w.leaf(w.box({0, 0, 0}, {5, 5, 5}), {5000, 100 * int64_t(i), 0}, id(7002 - i)));
        }
      }
      SplitMix64 q = same;
      pile(w, q, 20, 1, 100, 1);
      r = q;
      o[far] = test::run(w.t, w.store);
    }
    unclear += o[0].rep.output.repair.counts.unclear;
    for (const auto &[name, p] : o[0].verts) {
      UT_CHECK(o[1].verts.count(name) && o[1].verts.at(name) == p);
    }
    UT_CHECK(o[1].verts.size() == o[0].verts.size() + 24);
    for (const auto &[name, loops] : o[0].faces) {
      UT_CHECK(o[1].faces.count(name) && o[1].faces.at(name) == loops);
    }
  }
  printf("ties: %u cells unclear\n", unclear);
  UT_CHECK(unclear > 100);
}

/* ---- options ---- */

/* a wedge that takes less than a quantum off a cube's edge */
PersistentId wedge(World &w, const I3 &o)
{
  GridBuilder<L> p;
  I3 a[3] = {{-1, 10, -1}, {-1, 9, -1}, {-1, 10, 29}}, b[3];
  for (int i = 0; i < 3; i++) {
    for (int c = 0; c < 3; c++) {
      a[i][c] += o[c];
    }
    b[i] = a[i];
    b[i][0] = o[0] + 21;
  }
  p.face({{a[2], a[1], a[0]}});
  p.face({{b[0], b[1], b[2]}});
  for (int i = 0; i < 3; i++) {
    const int j = (i + 1) % 3;
    p.face({{a[i], a[j], b[j], b[i]}});
  }
  return w.add(p);
}

void test_options()
{
  World w;
  const PersistentId cube = w.box({0, 0, 0}, {20, 20, 20});
  w.op(1, 0, Op::Union);
  w.op(2, 1, Op::Difference);
  w.put(3, 2, w.leaf(cube));
  w.put(4, 2, w.leaf(wedge(w, {0, 0, 0})));
  BoolOptions opt;
  LComplex k;
  BoolReport rep;
  UT_CHECK(csg::evaluate(w.t, w.store, opt, k, rep) == CsgCode::Ok && rep.quantum_shift == 0);
  UT_CHECK(rep.output.repair.rounds > 0 && rep.output.repair.first != RoundCode::None);
  /* not asked for */
  opt.max_repair_rounds = 0;
  LComplex k2;
  UT_CHECK(csg::evaluate(w.t, w.store, opt, k2, rep) == CsgCode::RoundingFailed && k2.faces.live_count() == 0);
  UT_CHECK(rep.output.repair.rounds == 0);
  const PersistentId key0 = csg::evaluation_key(w.t, opt);
  opt.max_repair_rounds = 16;
  UT_CHECK(!(csg::evaluation_key(w.t, opt) == key0));
  /* a finer quantum comes first */
  opt.max_quantum_shift = 3;
  UT_CHECK(csg::evaluate(w.t, w.store, opt, k, rep) == CsgCode::Ok && rep.quantum_shift > 0 &&
           rep.output.repair.first == RoundCode::None);
  /* if it is in range; else the last one that is */
  const int64_t far = (int64_t(1) << 25) + (int64_t(1) << 24);
  World v;
  v.op(1, 0, Op::Union);
  v.op(2, 1, Op::Difference);
  v.put(3, 2, v.leaf(v.box({0, 0, 0}, {20, 20, 20})));
  v.put(4, 2, v.leaf(wedge(v, {0, 0, 0})));
  v.put(5, 1, v.leaf(v.box({far, 0, 0}, {far + 5, 5, 5})));
  opt.max_quantum_shift = 2;
  UT_CHECK(csg::evaluate(v.t, v.store, opt, k, rep) == CsgCode::Ok && rep.quantum_shift == 0 && rep.output.repair.rounds > 0);
  opt.max_repair_rounds = 0;
  UT_CHECK(csg::evaluate(v.t, v.store, opt, k, rep) == CsgCode::OutOfRange);
}

/* a failure that isn't one of rounding: nothing to repair, and said at once.
 * (A pillar that is flush with a ring around it: build_regions refuses it until
 * plan 2026-11-24-bvh's W4a is merged. Then this takes another failure.) */
void test_hopeless()
{
  World w;
  w.op(1, 0, Op::Union);
  w.op(2, 1, Op::Difference);
  w.put(3, 2, w.leaf(w.box({0, 0, 0}, {30, 30, 10})));
  w.put(4, 2, w.leaf(w.box({10, 10, -1}, {20, 20, 11})));
  w.put(5, 1, w.leaf(w.box({13, 13, 0}, {17, 17, 10})));
  BoolOptions opt;
  LComplex k;
  BoolReport rep;
  if (csg::evaluate(w.t, w.store, opt, k, rep) != CsgCode::Ok) {
    UT_CHECK(rep.code == CsgCode::RoundingFailed && rep.output.round == RoundCode::Regions);
    UT_CHECK(rep.output.repair.attempts == 0 && rep.output.repair.rounds == 1);
  }
}

/* one result everywhere */
void test_golden()
{
  SplitMix64 r(97);
  ident::Hasher128 h(ident::tag::BoolTree);
  uint32_t repaired = 0;
  for (int s = 0; s < 30; s++) {
    World w;
    pile(w, r, 60, s % 3, 100, 0);
    BoolOptions opt;
    if (s % 2) {
      rotate(opt.placement, r);
    }
    Out a = test::run(w.t, w.store, opt);
    const Out again = test::run(w.t, w.store, opt);
    repaired += a.rep.output.repair.rounds > 0;
    const PersistentId x = w.store.add(std::move(a.k));
    LComplex k2 = again.k;
    UT_CHECK(w.store.add(std::move(k2)) == x);
    h.h128(x);
  }
  const PersistentId got = h.finish();
  printf("golden: %u repaired, {0x%016llx, 0x%016llx}\n", repaired, (unsigned long long)got.lo, (unsigned long long)got.hi);
  UT_CHECK(repaired > 12);
  UT_CHECK((got == PersistentId{0x4a75803fda524665, 0x6590c80674ba1b52}));
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_options();
  test_hopeless();
  test_inverted();
  test_ties();
  test_random();
  test_elsewhere();
  test_wall();
  test_golden();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
