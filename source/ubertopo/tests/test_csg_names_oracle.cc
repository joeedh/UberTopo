/* Names of the elements of a CSG result (plan 2026-11-25-exact-booleans §4.6),
 * derived once more from the operands' faces, by brute force and in doubles. */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

#include "litestl/util/alloc.h"
#include "ut_test.h"

#include "support/csg_world.h"

using namespace ubertopo;
using namespace ubertopo::test;
using csg::Arrangement;
using csg::kNone;

namespace {

/* ---- the names, once more from the operands ---- */

struct Scene {
  World w;
  csg::Compiled c;
  Arrangement a;
  litestl::util::Vector<csg::Selected> sel;
  csg::OutputReport rep;
  LComplex k;

  bool make(int shift)
  {
    UT_CHECK(csg::compile(w.t, w.store, c) == CsgCode::Ok);
    UT_CHECK(csg::arrange({c.operands.data(), size_t(c.operands.size())}, a) == CsgCode::Ok);
    csg::select_facets(a, c.expr, sel);
    const csg::NameSource names = c.names();
    csg::OutputOptions opt;
    opt.quantum_shift = shift;
    opt.names = &names;
    return csg::build_output(a, c.expr, sel, opt, k, rep) == CsgCode::Ok;
  }
};

D3 minus3(const D3 &a, const D3 &b)
{
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
double len(const D3 &a)
{
  return std::sqrt(dot(a, a));
}
double to_segment(const D3 &p, const D3 &u, const D3 &v)
{
  const D3 d = minus3(v, u), e = minus3(p, u);
  const double t = std::clamp(dot(d, e) / dot(d, d), 0.0, 1.0);
  return len(minus3(e, {t * d[0], t * d[1], t * d[2]}));
}

/* a face of an operand, in the arrangement's frame */
struct OpFace {
  uint32_t leaf;
  PersistentId id;
  std::vector<std::vector<std::pair<D3, PersistentId>>> loops;
  std::vector<std::vector<PersistentId>> edges;
  D3 n;

  OpFace(const Scene &s, uint32_t leaf_, uint32_t f) : leaf(leaf_)
  {
    const csg::Operand &op = s.c.operands[int(leaf)];
    id = op.k->face_id(topo::FaceRef(f));
    n = {0, 0, 0};
    op.k->for_each_loop(topo::FaceRef(f), [&](topo::LoopRef l) {
      loops.push_back({});
      edges.push_back({});
      op.k->for_each_coedge(l, [&](topo::CoedgeRef c) {
        const auto v = op.k->start_vert(c);
        const auto &q = op.k->pos(v);
        const csg::I3 p = op.place.apply(csg::I3(q[0].raw, q[1].raw, q[2].raw));
        loops.back().push_back({{double(p[0] + s.a.in.shift[0]), double(p[1] + s.a.in.shift[1]), double(p[2] + s.a.in.shift[2])},
                                op.k->vert_id(v)});
        edges.back().push_back(op.k->edge_id(op.k->edge_of(c)));
      });
    });
    for (const auto &l : loops) {
      for (size_t i = 0; i < l.size(); i++) {
        const D3 x = cross(l[i].first, l[(i + 1) % l.size()].first);
        for (int j = 0; j < 3; j++) {
          n[j] += x[j];
        }
      }
    }
    const double s1 = len(n);
    n = {n[0] / s1, n[1] / s1, n[2] / s1};
  }
  double height(const D3 &p) const
  {
    return std::abs(dot(n, minus3(p, loops[0][0].first)));
  }
  bool contains(const D3 &p) const
  {
    if (height(p) > 1e-7) {
      return false;
    }
    double angle = 0;
    for (const auto &l : loops) {
      for (size_t i = 0; i < l.size(); i++) {
        const D3 &u = l[i].first, &v = l[(i + 1) % l.size()].first;
        if (to_segment(p, u, v) < 1e-7) {
          return true;
        }
        const D3 x = minus3(u, p), y = minus3(v, p);
        angle += std::atan2(dot(n, cross(x, y)), dot(x, y));
      }
    }
    return std::abs(angle) > 3;
  }
};

PersistentId tuple(uint32_t kind, std::vector<std::pair<PersistentId, PersistentId>> parts)
{
  std::sort(parts.begin(), parts.end());
  ident::Hasher128 h(ident::tag::BoolVertex);
  h.u32(kind);
  for (const auto &[l, x] : parts) {
    h.h128(l).h128(x);
  }
  return h.finish();
}

/* kinds: 0 own, 1 edge and edge, 2 edge and face, 3 faces */
PersistentId expected(const Scene &s, const std::vector<OpFace> &all, uint32_t p, uint32_t kinds[4])
{
  const csg::Result &res = s.rep.result;
  const D3 at = pos(s.a, p);
  std::vector<const OpFace *> here;
  for (const csg::ResultFace &f : res.faces) {
    if (std::count(f.pts.begin(), f.pts.end(), p)) {
      const csg::Polygon &g = s.a.in.polygons[int(f.source)];
      const PersistentId fid = s.c.operands[int(g.leaf)].k->face_id(topo::FaceRef(g.face));
      for (const OpFace &x : all) {
        if (x.leaf == g.leaf && x.id == fid && !std::count(here.begin(), here.end(), &x)) {
          here.push_back(&x);
        }
      }
    }
  }
  std::sort(here.begin(), here.end(), [](const OpFace *x, const OpFace *y) {
    return x->leaf != y->leaf ? x->leaf < y->leaf : x->id < y->id;
  });
  struct Edge {
    uint32_t leaf;
    PersistentId id;
    D3 d;
  };
  std::vector<Edge> edges;
  for (const OpFace *f : here) {
    for (size_t l = 0; l < f->loops.size(); l++) {
      for (size_t i = 0; i < f->loops[l].size(); i++) {
        const auto &[u, uid] = f->loops[l][i];
        const D3 &v = f->loops[l][(i + 1) % f->loops[l].size()].first;
        if (len(minus3(u, at)) < 1e-7) {
          kinds[0]++;
          return ident::derive(ident::tag::BoolVertex, uint32_t(0), s.c.leaf_ids[int(f->leaf)], uid);
        }
        if (to_segment(at, u, v) < 1e-7) {
          edges.push_back({f->leaf, f->edges[l][i], minus3(v, u)});
        }
      }
    }
  }
  std::sort(edges.begin(), edges.end(), [](const Edge &x, const Edge &y) {
    return x.leaf != y.leaf ? x.leaf < y.leaf : x.id < y.id;
  });
  const auto lid = [&](uint32_t leaf) { return s.c.leaf_ids[int(leaf)]; };
  auto turned = [](const D3 &x, const D3 &y) { return len(cross(x, y)) > 1e-7 * len(x) * len(y); };
  if (!edges.empty()) {
    for (const Edge &x : edges) {
      if (turned(x.d, edges[0].d)) {
        kinds[1]++;
        return tuple(1, {{lid(edges[0].leaf), edges[0].id}, {lid(x.leaf), x.id}});
      }
    }
    for (const OpFace *f : here) {
      if (std::abs(dot(f->n, edges[0].d)) > 1e-7 * len(edges[0].d)) {
        kinds[2]++;
        return tuple(2, {{lid(edges[0].leaf), edges[0].id}, {lid(f->leaf), f->id}});
      }
    }
  }
  /* faces that cut the result's edges there, and aren't in it */
  std::vector<const OpFace *> cut;
  for (const csg::ResultEdge &e : res.edges) {
    if ((e.a != p && e.b != p) || e.arr[0] == kNone) {
      continue;
    }
    const D3 far = pos(s.a, e.a == p ? e.b : e.a);
    D3 q;
    for (int j = 0; j < 3; j++) {
      q[j] = at[j] + (far[j] - at[j]) * 0.003;
    }
    std::vector<const OpFace *> kept;
    for (const csg::EdgeUse &u : e.uses) {
      const csg::Polygon &g = s.a.in.polygons[int(res.faces[int(u.facet)].source)];
      const PersistentId fid = s.c.operands[int(g.leaf)].k->face_id(topo::FaceRef(g.face));
      for (const OpFace &x : all) {
        if (x.leaf == g.leaf && x.id == fid) {
          kept.push_back(&x);
        }
      }
    }
    const OpFace *best = nullptr;
    for (const OpFace &x : all) {
      bool in_plane = false;
      for (const OpFace *y : kept) {
        in_plane = in_plane || !turned(x.n, y->n);
      }
      if (!in_plane && x.contains(q) && (!best || x.leaf < best->leaf || (x.leaf == best->leaf && x.id < best->id))) {
        best = &x;
      }
    }
    if (best && !std::count(cut.begin(), cut.end(), best)) {
      cut.push_back(best);
    }
  }
  std::sort(cut.begin(), cut.end(), [](const OpFace *x, const OpFace *y) {
    return x->leaf != y->leaf ? x->leaf < y->leaf : x->id < y->id;
  });
  if (!edges.empty()) {
    for (const OpFace *f : cut) {
      if (std::abs(dot(f->n, edges[0].d)) > 1e-7 * len(edges[0].d)) {
        kinds[2]++;
        return tuple(2, {{lid(edges[0].leaf), edges[0].id}, {lid(f->leaf), f->id}});
      }
    }
  }
  here.insert(here.end(), cut.begin(), cut.end());
  const OpFace *x = here[0], *y = nullptr;
  for (const OpFace *f : here) {
    if (!y) {
      y = turned(f->n, x->n) ? f : nullptr;
    }
    else if (std::abs(dot(f->n, cross(x->n, y->n))) > 1e-7) {
      kinds[3]++;
      return tuple(3, {{lid(x->leaf), x->id}, {lid(y->leaf), y->id}, {lid(f->leaf), f->id}});
    }
  }
  return kNoId;
}

/* the name of a coedge of a piece: its start, and what it is on */
struct CoedgeName {
  PersistentId start, on;
  bool source_edge;

  bool operator<(const CoedgeName &b) const
  {
    return start == b.start ? on < b.on : start < b.start;
  }
};

const OpFace &op_face(const std::vector<OpFace> &all, const Scene &s, uint32_t f)
{
  const csg::Polygon &g = s.a.in.polygons[int(s.rep.result.faces[int(f)].source)];
  const PersistentId fid = s.c.operands[int(g.leaf)].k->face_id(topo::FaceRef(g.face));
  for (const OpFace &x : all) {
    if (x.leaf == g.leaf && x.id == fid) {
      return x;
    }
  }
  UT_CHECK(false);
  return all[0];
}

/* the front normal, as the operand's placement leaves it */
D3 front(const Scene &s, const OpFace &f)
{
  const double d = s.c.operands[int(f.leaf)].place.det();
  return {f.n[0] * d, f.n[1] * d, f.n[2] * d};
}

CoedgeName coedge_name(const Scene &s, const std::vector<OpFace> &all, const OpFace &own, uint32_t p, uint32_t q,
                       uint32_t kinds[4])
{
  const csg::Result &res = s.rep.result;
  const PersistentId start = s.rep.names.vertex[int(p)];
  auto on = [&](uint32_t kind, PersistentId x) { return ident::derive(ident::tag::BoolFace, kind, x); };
  const D3 a = pos(s.a, p), b = pos(s.a, q);
  for (size_t l = 0; l < own.loops.size(); l++) {
    for (size_t i = 0; i < own.loops[l].size(); i++) {
      const D3 &u = own.loops[l][i].first, &v = own.loops[l][(i + 1) % own.loops[l].size()].first;
      if (to_segment(a, u, v) < 1e-7 && to_segment(b, u, v) < 1e-7) {
        kinds[0]++;
        return {start, on(0, own.edges[l][i]), true};
      }
    }
  }
  const csg::ResultEdge *e = nullptr;
  for (const csg::ResultEdge &x : res.edges) {
    e = (x.a == p && x.b == q) || (x.a == q && x.b == p) ? &x : e;
  }
  if (!e || e->arr[0] == kNone) {
    UT_CHECK(e != nullptr);
    return {start, on(2, s.rep.names.vertex[int(q)]), false};
  }
  auto coplanar = [&](const OpFace &x) { return len(cross(x.n, own.n)) < 1e-9; };
  auto first = [](const OpFace *x, const OpFace *y) {
    return !y || x->leaf < y->leaf || (x->leaf == y->leaf && x->id < y->id) ? x : y;
  };
  D3 near;
  for (int j = 0; j < 3; j++) {
    near[j] = a[j] + (b[j] - a[j]) * 0.003;
  }
  const OpFace *best = nullptr;
  for (const csg::EdgeUse &u : e->uses) {
    const OpFace &x = op_face(all, s, u.facet);
    if (!coplanar(x)) {
      best = first(&x, best);
    }
  }
  kinds[1] += best != nullptr;
  if (!best) {
    for (const OpFace &x : all) {
      if (!coplanar(x) && x.contains(near)) {
        best = first(&x, best);
      }
    }
    kinds[2] += best != nullptr;
  }
  if (!best) {
    for (const OpFace &x : all) {
      if (&x != &own && x.contains(near)) {
        best = first(&x, best);
      }
    }
    kinds[3] += best != nullptr;
  }
  if (!best) {
    return {start, on(1, kNoId), false};
  }
  const bool along = dot(cross(front(s, own), front(s, *best)), minus3(b, a)) > 1e-12;
  return {start,
          on(1, ident::derive(ident::tag::BoolFace, uint32_t(1), s.c.leaf_ids[int(best->leaf)], best->id, along)),
          false};
}

/* the names of the pieces of one face, the primary one first */
std::vector<std::pair<uint32_t, PersistentId>> expected_faces(const Scene &s, const std::vector<OpFace> &all,
                                                              const std::vector<uint32_t> &fs, uint32_t kinds[4])
{
  const OpFace &own = op_face(all, s, fs[0]);
  struct Piece {
    uint32_t face;
    PersistentId low = kNoId;
    CoedgeName key;
  };
  std::vector<Piece> v;
  for (uint32_t f : fs) {
    const csg::ResultFace &r = s.rep.result.faces[int(f)];
    Piece x;
    x.face = f;
    uint32_t at = kNone;
    for (uint32_t p : r.pts) {
      for (const auto &l : own.loops) {
        for (const auto &[u, uid] : l) {
          if (len(minus3(u, pos(s.a, p))) < 1e-7 && (x.low == kNoId || uid < x.low)) {
            x.low = uid;
            at = p;
          }
        }
      }
    }
    std::vector<CoedgeName> names;
    for (int l = 0; l + 1 < int(r.off.size()); l++) {
      const uint32_t b = r.off[l], n = r.off[l + 1] - b;
      for (uint32_t i = 0; i < n; i++) {
        uint32_t p = r.pts[int(b + i)], q = r.pts[int(b + (i + 1) % n)];
        if (r.reversed) {
          std::swap(p, q);
        }
        if (at == kNone || p == at) {
          names.push_back(coedge_name(s, all, own, p, q, kinds));
        }
      }
    }
    if (at == kNone && std::any_of(names.begin(), names.end(), [](const CoedgeName &c) { return c.source_edge; })) {
      std::erase_if(names, [](const CoedgeName &c) { return !c.source_edge; });
    }
    x.key = *std::min_element(names.begin(), names.end());
    v.push_back(x);
  }
  std::sort(v.begin(), v.end(), [](const Piece &x, const Piece &y) {
    const bool a = x.low == kNoId, b = y.low == kNoId;
    return a != b ? b : !(x.low == y.low) ? x.low < y.low : x.key < y.key;
  });
  const PersistentId lid = s.c.leaf_ids[int(own.leaf)];
  std::vector<std::pair<uint32_t, PersistentId>> out;
  for (size_t i = 0; i < v.size(); i++) {
    const bool shared = std::any_of(v.begin(), v.end(), [&](const Piece &y) { return &y != &v[i] && y.low == v[i].low; });
    out.push_back({v[i].face, i == 0    ? ident::derive(ident::tag::BoolFace, lid, own.id)
                              : !shared ? ident::derive(ident::tag::BoolFace, lid, own.id, v[i].low)
                                        : ident::derive(ident::tag::BoolFace, lid, own.id, v[i].key.start, v[i].key.on)});
    /* (one piece without a vertex shares it with none, but has no anchor) */
    if (i > 0 && v[i].low == kNoId) {
      out.back().second = ident::derive(ident::tag::BoolFace, lid, own.id, v[i].key.start, v[i].key.on);
    }
  }
  return out;
}

void test_oracle()
{
  uint32_t on[4] = {};
  SplitMix64 r(74);
  uint32_t kinds[4] = {}, runs = 0, pieces = 0, anchored = 0, keyed = 0;
  for (int n = 0; n < 1000; n++) {
    Scene s;
    World &w = s.w;
    w.op(1, 0, n % 5 != 3 && (n % 3 == 0 || n % 5 == 4 || n % 5 == 2) ? Op::Union : Op::Difference);
    const int leaves = (n % 5 == 3 ? 12 : n % 5 == 2 ? 12 : 3) + int(r.below(4));
    for (int i = 0; i < leaves; i++) {
      LComplex k;
      if (n % 5 == 3) {
        /* a sheet, and bars that cut it up */
        GridBuilder<L> g;
        if (i == 0) {
          g.face({{{0, 0, 0}, {24, 0, 0}, {24, 24, 0}, {0, 24, 0}}});
        }
        else {
          const int64_t x = 3 * int64_t(r.below(7)) + 2, y = 3 * int64_t(r.below(7)) + 1, l = 3 + 3 * int64_t(r.below(6));
          if (i % 2) {
            g.box({x, y, -2}, {x + 1, y + l, 2});
          }
          else {
            g.box({y, x, -2}, {y + l, x + 1, 2});
          }
        }
        k = build(g);
      }
      else if (n % 5 == 2) {
        /* sheets in one plane, and walls on it */
        GridBuilder<L> g;
        const int64_t x = 2 * int64_t(r.below(5)), y = 2 * int64_t(r.below(5)), dx = 2 + 2 * int64_t(r.below(5)),
                      dy = 2 + 2 * int64_t(r.below(5));
        if (i % 4 == 1) {
          /* (a room's name, with no faces in the result) */
          g.box({x, y, 0}, {x + dx, y + dy, 2});
        }
        else if (i % 3 == 0) {
          g.face({{{x, y, 0}, {x + dx, y, 0}, {x + dx, y + dy, 0}, {x, y + dy, 0}}});
        }
        else if (i % 3 == 1) {
          g.face({{{x, y, 0}, {x + dx, y, 0}, {x + dx, y, 3}, {x, y, 3}}});
        }
        else {
          g.face({{{x, y, 0}, {x, y + dy, 0}, {x, y + dy, 3}, {x, y, 3}}});
        }
        k = build(g);
      }
      else if (n % 5 == 4) {
        /* sheets through one point, many of them through one line */
        GridBuilder<L> g;
        I3 u, v;
        bool ok = false;
        while (!ok) {
          u = i % 2 ? I3{0, 0, 4} : vertex(r, 3);
          v = vertex(r, 3);
          const csg::I3 c = csg::cross_small(csg::I3(u[0], u[1], u[2]), csg::I3(v[0], v[1], v[2]));
          ok = c[0] || c[1] || c[2];
        }
        auto at = [&](int64_t x, int64_t y) -> I3 {
          return {x * u[0] + y * v[0], x * u[1] + y * v[1], x * u[2] + y * v[2]};
        };
        g.face({{at(-3, -3), at(3, -3), at(3, 3), at(-3, 3)}});
        k = build(g);
      }
      else if (n % 2) {
        Operands o;
        add_random(r, 3, o);
        k = *o.k.back();
      }
      else {
        GridBuilder<L> g;
        I3 lo, hi;
        for (int j = 0; j < 3; j++) {
          const int64_t x = int64_t(r.below(7)), y = int64_t(r.below(7));
          lo[j] = std::min(x, y) - (i ? 1 : 3);
          hi[j] = std::max(x, y) + (i ? 0 : 4);
        }
        /* a sheet, or a box */
        if (r.below(3) == 0) {
          g.face({{{lo[0], lo[1], 2}, {hi[0] + 1, lo[1], 2}, {hi[0] + 1, hi[1] + 1, 2}, {lo[0], hi[1] + 1, 2}}});
        }
        else {
          g.box(lo, {hi[0] + 1, hi[1] + 1, hi[2] + 1});
        }
        k = build(g);
      }
      CsgLeaf l;
      l.operand = w.store.add(std::move(k));
      l.thin = n % 5 != 2 || i % 4 != 1;
      l.kind = l.thin ? LabelKind::Solid : LabelKind::Air;
      /* (turned, but not moved away from the others) */
      l.place = n % 5 == 3 || n % 5 == 2 ? csg::GridPlacement() : random_placement(r);
      for (int j = 0; j < 3; j++) {
        l.place.offset[j] = 0;
      }
      w.put(10 + uint64_t(i), 1, l);
    }
    if (!s.make(16)) {
      continue;
    }
    if (s.a.in.outlines.size() > 0 || s.rep.result.faces.size() == 0) {
      continue;
    }
    runs++;
    std::vector<OpFace> all;
    for (uint32_t l = 0; l < s.c.operands.size(); l++) {
      s.c.operands[int(l)].k->faces.for_each_live([&](uint32_t f) { all.emplace_back(s, l, f); });
    }
    std::set<uint32_t> points;
    for (const csg::ResultFace &f : s.rep.result.faces) {
      points.insert(f.pts.begin(), f.pts.end());
    }
    for (uint32_t p : points) {
      UT_CHECK(s.rep.names.vertex[int(p)] == expected(s, all, p, kinds));
    }
    /* pieces of a face */
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> groups;
    for (uint32_t f = 0; f < s.rep.result.faces.size(); f++) {
      const csg::Polygon &g = s.a.in.polygons[int(s.rep.result.faces[int(f)].source)];
      groups[{g.leaf, g.face}].push_back(f);
    }
    for (const auto &[src, fs] : groups) {
      for (const auto &[f, name] : expected_faces(s, all, fs, on)) {
        UT_CHECK(s.rep.names.face[int(f)] == name);
      }
      const LComplex &k = *s.c.operands[int(src.first)].k;
      const PersistentId lid = s.c.leaf_ids[int(src.first)], fid = k.face_id(topo::FaceRef(src.second));
      const PersistentId primary = ident::derive(ident::tag::BoolFace, lid, fid);
      const OpFace *of = nullptr;
      for (const OpFace &x : all) {
        of = x.leaf == src.first && x.id == fid ? &x : of;
      }
      /* the smallest own vertex of each */
      std::vector<PersistentId> low;
      for (uint32_t f : fs) {
        PersistentId m = kNoId;
        for (uint32_t p : s.rep.result.faces[int(f)].pts) {
          for (const auto &l : of->loops) {
            for (const auto &[u, uid] : l) {
              if (len(minus3(u, pos(s.a, p))) < 1e-7 && (m == kNoId || uid < m)) {
                m = uid;
              }
            }
          }
        }
        low.push_back(m);
      }
      PersistentId best = kNoId;
      for (const PersistentId &m : low) {
        best = m == kNoId || (!(best == kNoId) && best < m) ? best : m;
      }
      uint32_t primaries = 0;
      for (size_t i = 0; i < fs.size(); i++) {
        const PersistentId name = s.rep.names.face[int(fs[i])];
        if (name == primary) {
          primaries++;
          UT_CHECK(low[i] == best);
        }
        else if (!(low[i] == kNoId) && std::count_if(low.begin(), low.end(), [&](const PersistentId &m) { return m == low[i]; }) == 1) {
          anchored++;
          UT_CHECK(name == ident::derive(ident::tag::BoolFace, lid, fid, low[i]));
        }
        else {
          keyed++;
          UT_CHECK(low[i] == kNoId || !(name == ident::derive(ident::tag::BoolFace, lid, fid, low[i])));
        }
      }
      UT_CHECK(primaries == 1);
      pieces += uint32_t(fs.size()) - 1;
    }
    UT_CHECK(s.rep.names.weak == 0 && s.rep.names.ordinals == 0 && s.rep.names.collisions == 0);
  }
  printf("oracle: %u runs; own %u, edge-edge %u, edge-face %u, faces %u; %u pieces (%u anchored, %u keyed)\n", runs,
         kinds[0], kinds[1], kinds[2], kinds[3], pieces, anchored, keyed);
  UT_CHECK(runs > 150 && kinds[0] > 800 && kinds[1] > 100 && kinds[2] > 300 && kinds[3] > 60 && anchored > 60 && keyed > 10);
  printf("coedges on: own edges %u, faces of the result %u, cutters %u, faces in the plane %u\n", on[0], on[1], on[2], on[3]);
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_oracle();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
