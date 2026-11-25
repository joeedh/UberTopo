/* From kept facets to a complex (plan 2026-11-25-exact-booleans §4.5, §4.7):
 * merging, diagonals, rounding and the checks after it. Random trees on random
 * operands, output at a much finer quantum (so that rounding hardly matters)
 * and at their own, unrotated and rotated:
 *
 *  - a result that is Ok is valid, and has the labels of the expression at
 *    points off its faces;
 *  - its area is that of the kept facets;
 *  - failures are counted, and at the fine quantum there are hardly any. */

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>

#include "litestl/util/alloc.h"
#include "ut_test.h"

#include "support/csg_buildings.h"
#include "support/csg_oracle.h"
#include "support/csg_trees.h"
#include "ubertopo/csg/legal.h"
#include "ubertopo/csg/output.h"
#include "ubertopo/topo/validate.h"

using namespace ubertopo;
using namespace ubertopo::test;
using csg::Arrangement;
using csg::CsgCode;
using csg::Expression;
using csg::kNone;
using csg::kVoid;
using csg::LabelKind;
using csg::Op;
using csg::OutputOptions;
using csg::OutputReport;
using csg::RoundCode;
using csg::Selected;

namespace {

using Selection = litestl::util::Vector<Selected>;

csg::RigidPlacement rotation(SplitMix64 &r, bool vertical)
{
  /* from a random unit quaternion */
  double q[4] = {real(r) - 0.5, real(r) - 0.5, real(r) - 0.5, real(r) - 0.5};
  if (vertical) {
    q[1] = q[2] = 0;
  }
  const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  for (double &x : q) {
    x /= n;
  }
  csg::RigidPlacement p;
  const double w = q[0], x = q[1], y = q[2], z = q[3];
  const double m[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                          {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                          {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      p.r[i][j] = m[i][j];
    }
    p.t[i] = 4 * real(r) - 2;
  }
  return p;
}

D3 apply(const csg::RigidPlacement &p, const D3 &x, double scale)
{
  D3 out;
  for (int i = 0; i < 3; i++) {
    out[i] = scale * (p.r[i][0] * x[0] + p.r[i][1] * x[1] + p.r[i][2] * x[2] + p.t[i]);
  }
  return out;
}

/* areas of the kept facets, and of the result's faces */
double facet_area(const Arrangement &a, const Selection &sel)
{
  double total = 0;
  for (uint32_t f = 0; f < a.facets.size(); f++) {
    if (!sel[f].kept) {
      continue;
    }
    D3 sum = {0, 0, 0};
    for (const csg::FacetEdge &e : a.facets[int(f)].edges) {
      const csg::ArrEdge &x = a.edges[int(e.edge)];
      const D3 c = cross(pos(a, e.dir > 0 ? x.a : x.b), pos(a, e.dir > 0 ? x.b : x.a));
      sum = {sum[0] + c[0], sum[1] + c[1], sum[2] + c[2]};
    }
    total += len(sum) / 2;
  }
  return total;
}
double polygon_area(const csg::Input &in)
{
  double total = 0;
  for (const csg::Polygon &g : in.polygons) {
    D3 sum = {0, 0, 0};
    g.for_each_edge([&](uint32_t i, uint32_t j) {
      const auto &p = in.ipos[int(g.pts[int(i)])], &q = in.ipos[int(g.pts[int(j)])];
      const D3 c = cross({double(p[0]), double(p[1]), double(p[2])}, {double(q[0]), double(q[1]), double(q[2])});
      sum = {sum[0] + c[0], sum[1] + c[1], sum[2] + c[2]};
    });
    total += len(sum) / 2;
  }
  return total;
}

struct Tally {
  uint32_t runs = 0, ok = 0, faces = 0, illegal = 0, diagonals = 0, projected = 0, dropped = 0, whole = 0, split = 0,
           samples = 0, solid = 0, reversed = 0;
  std::map<int, uint32_t> codes;
};

void run_one(const Arrangement &a, const Expression &e, const Meaning &m, const Tree &t, const Operands &o,
             const Selection &sel, const OutputOptions &opt, bool oracles, Tally &n, SplitMix64 &r)
{
  LComplex k;
  OutputReport rep;
  const CsgCode c = csg::build_output(a, e, sel, opt, k, rep);
  n.runs++;
  UT_CHECK(c == CsgCode::Ok || c == CsgCode::RoundingFailed);
  if (c != CsgCode::Ok) {
    n.codes[int(rep.round)]++;
    UT_CHECK(rep.round != RoundCode::None && k.verts.live_count() == 0);
    return;
  }
  n.ok++;
  n.faces += rep.result.faces.size();
  n.illegal += rep.result.stats.illegal;
  n.diagonals += rep.result.stats.diagonals;
  n.projected += rep.projected;
  n.dropped += rep.result.stats.dropped_points;
  n.whole += rep.result.stats.whole;
  n.split += rep.split_whole;
  for (const csg::ResultFace &f : rep.result.faces) {
    n.reversed += f.reversed;
  }
  if (!oracles || k.faces.live_count() == 0) {
    return;
  }
  UT_CHECK(topo::validate(k, topo::ValidateLevel::Global).ok());
  std::vector<int> label(k.regions.capacity(), -1);
  for (uint32_t i = 0; i < rep.result.faces.size(); i++) {
    for (uint32_t s = 0; s < 2; s++) {
      label[k.region(topo::FaceRef(rep.face_of[int(i)]), s).i] = int(rep.result.faces[int(i)].solid[s]);
    }
  }
  /* the output as an operand, for its polygons */
  Arrangement b;
  const csg::Operand op{&k, {}};
  UT_CHECK(csg::read_operands({&op, 1}, b.in) == CsgCode::Ok);
  const double scale = std::ldexp(1.0, opt.quantum_shift);
  const double want = facet_area(a, sel), got = polygon_area(b.in) / (scale * scale);
  UT_CHECK(std::abs(want - got) < 1e-3 * want + 1e-3);
  for (int i = 0; i < 12; i++) {
    const D3 x = {7 * real(r) - 3.5, 7 * real(r) - 3.5, 7 * real(r) - 3.5};
    std::vector<uint32_t> region;
    D3 q; /* in the arrangement's coordinates */
    for (int d = 0; d < 3; d++) {
      q[d] = x[d] + double(a.in.shift[d]);
    }
    for (uint32_t l = 0; l < a.leaf_count; l++) {
      region.push_back(oracle(a, l, q, r));
    }
    D3 y = apply(opt.placement, x, scale);
    for (int d = 0; d < 3; d++) {
      y[d] += double(b.in.shift[d]);
    }
    const uint32_t at = oracle(b, 0, y, r);
    if (std::find(region.begin(), region.end(), kNone) != region.end() || at == kNone) {
      continue;
    }
    const int in = m.at(t, region);
    UT_CHECK(label[at] == (in < 0 ? 0 : in + 3));
    n.samples++;
    n.solid += in >= 0;
  }
  (void)o;
}

void print(const char *name, const Tally &n)
{
  printf("%s: %u runs, %u ok; %u faces, %u illegal (%u diagonals), %u projected, %u points dropped, %u whole (%u "
         "split), %u reversed, %u samples (%u solid); failures:",
         name, n.runs, n.ok, n.faces, n.illegal, n.diagonals, n.projected, n.dropped, n.whole, n.split, n.reversed,
         n.samples, n.solid);
  for (const auto &[c, x] : n.codes) {
    printf(" code %d x%u", c, x);
  }
  printf("\n");
}

void test_random()
{
  SplitMix64 r(41);
  Tally fine, own, turned, coarse;
  for (int s = 0; s < 400; s++) {
    RandomCase c;
    if (!c.make(r)) {
      continue;
    }
    Arrangement a;
    UT_CHECK(csg::arrange(c.o.ops, a) == CsgCode::Ok);
    Selection sel;
    csg::select_facets(a, c.e, sel);
    OutputOptions opt;
    opt.quantum_shift = 20;
    run_one(a, c.e, c.m, c.t, c.o, sel, opt, true, fine, r);
    opt.placement = rotation(r, s % 2 == 0);
    run_one(a, c.e, c.m, c.t, c.o, sel, opt, true, turned, r);
    opt.quantum_shift = 10;
    run_one(a, c.e, c.m, c.t, c.o, sel, opt, false, coarse, r);
    opt = OutputOptions();
    run_one(a, c.e, c.m, c.t, c.o, sel, opt, false, own, r);
  }
  print("unrotated, 2^-20", fine);
  print("rotated, 2^-20", turned);
  print("rotated, 2^-10", coarse);
  print("unrotated, whole quanta", own);
  UT_CHECK(fine.runs > 200 && fine.ok + 3 >= fine.runs && turned.ok + 3 >= turned.runs);
  UT_CHECK(fine.samples > 2000 && fine.solid > 200 && fine.illegal > 20 && fine.dropped > 100);
  /* on the integer grid itself, most cuts collapse: it never goes unnoticed */
  UT_CHECK(own.ok < own.runs && own.ok > own.runs / 8);
}

/* ---- named cases ---- */

struct Case : Building {
  Arrangement a;
  Selection sel;
  LComplex k;
  OutputReport rep;

  CsgCode run(const OutputOptions &opt = {})
  {
    UT_CHECK(csg::arrange(o.ops, a) == CsgCode::Ok);
    csg::select_facets(a, e, sel);
    const CsgCode code = csg::build_output(a, e, sel, opt, k, rep);
    if (code != CsgCode::Ok) {
      printf("  code %d, round %d\n", int(code), int(rep.round));
    }
    return code;
  }
  bool counts(uint32_t verts, uint32_t edges, uint32_t faces, uint32_t regions) const
  {
    const bool ok = k.verts.live_count() == verts && k.edges.live_count() == edges && k.faces.live_count() == faces &&
                    k.regions.live_count() == regions;
    if (!ok) {
      printf("  got %u vertices, %u edges, %u faces, %u regions\n", k.verts.live_count(), k.edges.live_count(),
             k.faces.live_count(), k.regions.live_count());
    }
    return ok;
  }
  uint32_t op(Op x, const std::vector<uint32_t> &c)
  {
    return e.add_op(x, c);
  }
};

void test_cases()
{
  {
    /* one box: as it is, with its faces facing out */
    Case c;
    c.box({0, 0, 0}, {4, 5, 6}, c.wall);
    UT_CHECK(c.run() == CsgCode::Ok && c.counts(8, 12, 6, 2));
    for (const csg::ResultFace &f : c.rep.result.faces) {
      UT_CHECK(!f.reversed && f.label[0] == kVoid && f.label[1] == c.wall && f.source != kNone);
    }
    /* integer points stay where they are */
    c.k.verts.for_each_live([&](uint32_t v) {
      const auto &p = c.k.pos(topo::VertRef(v));
      UT_CHECK((p[0].raw == 0 || p[0].raw == 4) && (p[1].raw == 0 || p[1].raw == 5) && (p[2].raw == 0 || p[2].raw == 6));
    });
  }
  {
    /* flush boxes of one name fuse; the joints on the faces disappear */
    Case c;
    c.op(Op::Union, {c.box({0, 0, 0}, {4, 4, 4}, c.wall), c.box({4, 0, 0}, {8, 4, 4}, c.wall)});
    /* (the two leaves' faces stay apart: 10 faces, 12 vertices) */
    UT_CHECK(c.run() == CsgCode::Ok && c.counts(12, 20, 10, 2));
  }
  {
    /* ... and nothing is left of a box inside another */
    Case c;
    c.op(Op::Union, {c.box({0, 0, 0}, {8, 8, 8}, c.wall), c.box({0, 2, 2}, {4, 4, 4}, c.wall)});
    UT_CHECK(c.run() == CsgCode::Ok && c.counts(8, 12, 6, 2));
    UT_CHECK(c.rep.result.stats.illegal == 0);
  }
  {
    /* a window in a wall: a tunnel, reveals facing into it */
    Case c;
    c.op(Op::Difference, {c.box({0, 0, 0}, {9, 2, 9}, c.wall), c.box({3, -1, 3}, {6, 3, 6}, c.wall)});
    UT_CHECK(c.run() == CsgCode::Ok && c.counts(16, 24, 10, 2));
    uint32_t reversed = 0;
    for (const csg::ResultFace &f : c.rep.result.faces) {
      reversed += f.reversed;
      UT_CHECK(f.label[0] == kVoid && f.label[1] == c.wall);
    }
    UT_CHECK(reversed == 4);
  }
  {
    /* a blind window, a full-height slot, and a cut through everything */
    Case c, d, f;
    c.op(Op::Difference, {c.box({0, 0, 0}, {9, 3, 9}, c.wall), c.box({3, -1, 3}, {6, 2, 6}, c.wall)});
    UT_CHECK(c.run() == CsgCode::Ok && c.counts(16, 24, 11, 2));
    d.op(Op::Difference, {d.box({0, 0, 0}, {9, 3, 9}, d.wall), d.box({3, -1, 0}, {6, 4, 9}, d.wall)});
    UT_CHECK(d.run() == CsgCode::Ok && d.counts(16, 24, 12, 3));
    f.op(Op::Difference, {f.box({0, 0, 0}, {9, 3, 9}, f.wall), f.box({-1, -1, -1}, {10, 10, 10}, f.wall)});
    UT_CHECK(f.run() == CsgCode::Ok && f.counts(0, 0, 0, 1));
  }
  {
    /* boxes that touch in an edge, or in a vertex: one complex, two regions */
    Case c, d;
    c.op(Op::Union, {c.box({0, 0, 0}, {4, 4, 4}, c.wall), c.box({4, 4, 0}, {8, 8, 4}, c.wall)});
    UT_CHECK(c.run() == CsgCode::Ok && c.counts(14, 23, 12, 3));
    d.op(Op::Union, {d.box({0, 0, 0}, {4, 4, 4}, d.wall), d.box({4, 4, 4}, {8, 8, 8}, d.wall)});
    UT_CHECK(d.run() == CsgCode::Ok && d.counts(15, 24, 12, 3));
  }
  {
    /* a box that stands on another one's face with an edge, or a vertex: the
     * face is cut up, so that the contact is an edge, or a vertex of it */
    Case c;
    GridBuilder<L> g;
    c.op(Op::Union, {c.box({0, 0, 0}, {8, 8, 8}, c.wall),
                     c.prism({{8, 4}, {12, 2}, {12, 6}}, 0, 2, 6, c.wall)});
    /* (a wedge over (y, z), its edge on the face y = 8) */
    UT_CHECK(c.run() == CsgCode::Ok && c.rep.result.stats.illegal == 1 && c.k.regions.live_count() == 3);
    const uint32_t faces = c.rep.result.faces.size();
    UT_CHECK(faces > 6 + 5);
    Case d;
    const uint32_t box = d.box({0, 0, 0}, {8, 8, 8}, d.wall);
    const I3 v[4] = {{3, 4, 8}, {5, 4, 12}, {2, 6, 12}, {2, 2, 12}};
    UT_CHECK(orientation(v[0], v[1], v[2], v[3]) > 0);
    g.face({{v[0], v[2], v[1]}});
    g.face({{v[0], v[1], v[3]}});
    g.face({{v[1], v[2], v[3]}});
    g.face({{v[0], v[3], v[2]}});
    d.op(Op::Union, {box, d.leaf(g, d.wall)});
    UT_CHECK(d.run() == CsgCode::Ok && d.rep.result.stats.illegal == 1 && d.k.regions.live_count() == 3);
    /* the top is cut by diagonals to the vertex: at least two pieces */
    UT_CHECK(d.k.faces.live_count() >= 5 + 2 + 4 && d.k.verts.live_count() == 12);
  }
  {
    /* cutters that meet in a vertex on a wall, or in an edge of two windows */
    Case c, d;
    c.op(Op::Difference, {c.box({0, 0, 0}, {9, 2, 9}, c.wall), c.box({1, -1, 1}, {4, 3, 4}, c.wall),
                          c.box({4, -1, 4}, {7, 3, 7}, c.wall)});
    UT_CHECK(c.run() == CsgCode::Ok && c.rep.result.stats.illegal == 2);
    d.op(Op::Difference, {d.box({0, 0, 0}, {9, 2, 9}, d.wall), d.box({1, -1, 1}, {4, 3, 4}, d.wall),
                          d.box({4, -1, 1}, {7, 3, 4}, d.wall)});
    /* side by side: one opening, with 4 + 2 reveals */
    UT_CHECK(d.run() == CsgCode::Ok && d.rep.result.stats.illegal == 0 && d.k.faces.live_count() == 6 + 6);
  }
  {
    /* a thin-walled house with a door and a window */
    Case c;
    const uint32_t cut = c.op(
        Op::Difference, {c.box({0, 0, 0}, {20, 10, 8}, c.air, true), c.box({4, -1, 2}, {7, 1, 5}, c.wall),
                         c.box({10, -1, 0}, {12, 1, 6}, c.wall)});
    (void)cut;
    UT_CHECK(c.run() == CsgCode::Ok && c.k.faces.live_count() == 6 && c.k.regions.live_count() == 1);
    for (const csg::ResultFace &f : c.rep.result.faces) {
      UT_CHECK(!f.reversed && f.solid[0] == kVoid && f.solid[1] == kVoid);
    }
  }
  {
    /* nested and torus-shaped: regions. (Flush with the ring, build_regions says
     * TouchingShell until plan 2026-11-24-bvh's W4a is merged.) */
    Case c;
    const uint32_t ring = c.op(Op::Difference, {c.box({0, 0, 0}, {20, 20, 4}, c.wall), c.box({5, 5, -1}, {15, 15, 5}, c.wall)});
    c.op(Op::Union, {ring, c.box({8, 8, 1}, {12, 12, 3}, c.stone)});
    UT_CHECK(c.run() == CsgCode::Ok && c.counts(24, 36, 16, 3));
  }
  {
    /* coincident operands; a leaf twice */
    Case c, d;
    c.op(Op::Union, {c.box({0, 0, 0}, {4, 4, 4}, c.wall), c.box({0, 0, 0}, {4, 4, 4}, c.wall)});
    UT_CHECK(c.run() == CsgCode::Ok && c.counts(8, 12, 6, 2));
    for (const csg::ResultFace &f : c.rep.result.faces) {
      UT_CHECK(c.a.in.polygons[int(f.source)].leaf == 0);
    }
    d.op(Op::Difference, {d.box({0, 0, 0}, {4, 4, 4}, d.wall), d.box({0, 0, 0}, {4, 4, 4}, d.wall)});
    UT_CHECK(d.run() == CsgCode::Ok && d.counts(0, 0, 0, 1));
  }
  {
    /* two points that round to one; a finer quantum tells them apart */
    for (int shift = 0; shift < 4; shift++) {
      Case c;
      c.op(Op::Difference, {c.box({0, 0, 0}, {20, 20, 20}, c.wall),
                            c.prism({{-1, 10}, {-1, 9}, {29, 10}}, 0, -1, 21, c.wall)});
      OutputOptions opt;
      opt.quantum_shift = shift;
      const CsgCode code = c.run(opt);
      UT_CHECK(code == (shift == 0 ? CsgCode::RoundingFailed : CsgCode::Ok));
      UT_CHECK((c.rep.round == RoundCode::SamePosition) == (shift == 0));
    }
  }
}

/* The faces of a result: labels, and loops of rounded points, each from its
 * smallest. */
std::multiset<std::string> shape(const OutputReport &rep)
{
  std::multiset<std::string> out;
  for (const csg::ResultFace &f : rep.result.faces) {
    std::multiset<std::string> loops;
    for (int l = 0; l + 1 < int(f.off.size()); l++) {
      std::vector<std::string> pts;
      for (uint32_t i = f.off[l]; i < f.off[l + 1]; i++) {
        const csg::I3 &p = rep.positions[int(f.pts[int(i)])];
        pts.push_back(std::to_string(p[0]) + "," + std::to_string(p[1]) + "," + std::to_string(p[2]) + " ");
      }
      std::rotate(pts.begin(), std::min_element(pts.begin(), pts.end()), pts.end());
      std::string s;
      for (const std::string &x : pts) {
        s += x;
      }
      loops.insert(s);
    }
    std::string s = std::to_string(f.solid[0]) + "/" + std::to_string(f.solid[1]) + ": ";
    for (const std::string &l : loops) {
      s += l + "; ";
    }
    out.insert(s);
  }
  return out;
}

/* Leaves that add no faces (rooms' names) change nothing, whatever they cut on
 * the way. */
void test_names_only()
{
  SplitMix64 r(45);
  uint32_t runs = 0, faces = 0, cut = 0, dropped = 0, illegal = 0;
  for (int s = 0; s < 300; s++) {
    RandomCase c;
    if (!c.make(r)) {
      continue;
    }
    OutputOptions opt;
    opt.quantum_shift = 20;
    std::multiset<std::string> got[2];
    uint32_t facets[2];
    for (int more = 0; more < 2; more++) {
      if (more) {
        std::vector<uint32_t> all = {c.e.root};
        for (int i = 0; i < 2; i++) {
          add_random(r, 3, c.o);
          std::vector<uint32_t> l(c.o.k.back()->regions.capacity(), c.air[0]);
          l[0] = kVoid;
          all.push_back(c.e.add_leaf(false, l));
        }
        c.e.add_op(Op::Union, all);
      }
      Arrangement a;
      UT_CHECK(csg::arrange(c.o.ops, a) == CsgCode::Ok);
      Selection sel;
      csg::select_facets(a, c.e, sel);
      facets[more] = 0;
      for (uint32_t f = 0; f < a.facets.size(); f++) {
        facets[more] += sel[f].kept;
      }
      LComplex k;
      OutputReport rep;
      UT_CHECK(csg::build_output(a, c.e, sel, opt, k, rep) == CsgCode::Ok);
      got[more] = shape(rep);
      if (more) {
        runs++;
        faces += rep.result.faces.size();
        dropped += rep.result.stats.dropped_points;
        illegal += rep.result.stats.illegal;
        cut += facets[1] - facets[0];
      }
    }
    UT_CHECK(got[0] == got[1]);
  }
  printf("names only: %u runs, %u faces, %u more facets, %u points dropped, %u illegal\n", runs, faces, cut, dropped,
         illegal);
  UT_CHECK(cut > 500 && dropped > 400 && illegal > 15);
}

/* what a face's own edges and vertices keep, or not */
void test_merging()
{
  {
    /* a vertex of the operand in the middle of an edge stays; one that a cut
     * made, and that nothing needs, doesn't */
    Case c;
    c.op(Op::Union, {c.prism({{0, 0}, {4, 0}, {8, 0}, {8, 8}, {0, 8}}, 2, 0, 5, c.wall),
                     c.box({2, -2, 1}, {6, 10, 3}, c.air)});
    UT_CHECK(c.run() == CsgCode::Ok && c.counts(10, 15, 7, 2) && c.rep.result.stats.dropped_points == 2);
    Case d;
    d.op(Op::Union, {d.prism({{0, 0}, {4, 0}, {8, 0}, {8, 8}, {0, 8}}, 2, 0, 5, d.wall),
                     d.box({2, -2, 1}, {6, 10, 7}, d.air)});
    UT_CHECK(d.run() == CsgCode::Ok && d.counts(10, 15, 7, 2) && d.rep.result.stats.dropped_points == 5);
  }
  {
    /* also one with nothing but its two edges */
    Case c;
    GridBuilder<L> g;
    g.face({{{0, 0, 0}, {0, 4, 0}, {4, 4, 0}, {4, 0, 0}, {2, 0, 0}}});
    g.face({{{0, 0, 4}, {4, 0, 4}, {4, 4, 4}, {0, 4, 4}}});
    g.face({{{0, 0, 0}, {2, 0, 0}, {4, 0, 0}, {4, 0, 4}, {0, 0, 4}}});
    g.face({{{0, 4, 0}, {0, 4, 4}, {4, 4, 4}, {4, 4, 0}}});
    g.face({{{0, 0, 0}, {0, 0, 4}, {0, 4, 4}, {0, 4, 0}}});
    g.face({{{4, 0, 0}, {4, 4, 0}, {4, 4, 4}, {4, 0, 4}}});
    c.op(Op::Union, {c.leaf(g, c.wall), c.box({1, -1, -1}, {3, 5, 1}, c.air)});
    UT_CHECK(c.run() == CsgCode::Ok && c.counts(9, 13, 6, 2) && c.rep.result.stats.dropped_points == 4);
  }
  {
    /* a vertex touches a wall on a line that a room's name cuts into it */
    Case c;
    const uint32_t box = c.box({0, 0, 0}, {8, 8, 8}, c.wall);
    GridBuilder<L> g;
    const I3 v[4] = {{3, 4, 8}, {5, 4, 12}, {2, 6, 12}, {2, 2, 12}};
    g.face({{v[0], v[2], v[1]}});
    g.face({{v[0], v[1], v[3]}});
    g.face({{v[1], v[2], v[3]}});
    g.face({{v[0], v[3], v[2]}});
    const uint32_t spike = c.leaf(g, c.wall);
    c.op(Op::Union, {box, spike, c.box({3, -1, 6}, {20, 20, 20}, c.air)});
    UT_CHECK(c.run() == CsgCode::Ok && c.rep.result.stats.illegal == 1 && c.k.regions.live_count() == 3);
  }
  {
    /* a room's name that stands on a wall with an edge: no slit is left */
    Case c;
    c.op(Op::Union, {c.box({0, 0, 0}, {8, 8, 8}, c.wall),
                     c.prism({{8, 4}, {12, 2}, {12, 6}}, 0, 2, 6, c.air)});
    UT_CHECK(c.run() == CsgCode::Ok && c.counts(8, 12, 6, 2) && c.rep.result.stats.illegal == 0);
  }
  {
    /* a wall between two solid regions, one of which is cut away: it isn't
     * turned round, though its front had a solid and now has none */
    for (int which = 0; which < 2; which++) {
      Case c;
      GridBuilder<L> g;
      g.box({0, 0, 0}, {4, 4, 4});
      g.box({4, 0, 0}, {8, 4, 4});
      c.o.add(g);
      const uint32_t l[3] = {kVoid, c.wall, c.stone};
      const uint32_t leaf = c.e.add_leaf(false, l);
      c.op(Op::Difference, {leaf, which ? c.box({4, 0, 0}, {8, 4, 4}, c.wall) : c.box({0, 0, 0}, {4, 4, 4}, c.wall)});
      UT_CHECK(c.run() == CsgCode::Ok && c.counts(8, 12, 6, 2));
      for (const csg::ResultFace &f : c.rep.result.faces) {
        UT_CHECK(!f.reversed && c.a.in.polygons[int(f.source)].leaf == 0);
      }
    }
  }
}

/* ---- projected operands ---- */

/* a block with a warped top: its corners are lifted */
uint32_t warped(Building &b, const I3 &lo, const I3 &hi, int64_t lift)
{
  GridBuilder<L> g;
  auto p = [&](int i, bool top) -> I3 {
    const bool x = i == 1 || i == 2, y = i >= 2;
    return {x ? hi[0] : lo[0], y ? hi[1] : lo[1], top ? hi[2] + (x != y ? lift : 0) : lo[2]};
  };
  g.face({{p(3, false), p(2, false), p(1, false), p(0, false)}});
  g.face({{p(0, true), p(1, true), p(2, true), p(3, true)}});
  for (int i = 0; i < 4; i++) {
    const int j = (i + 1) % 4;
    g.face({{p(i, false), p(j, false), p(j, true), p(i, true)}});
  }
  topo::BuildPolicy policy;
  policy.allow_projectable_faces = true;
  return b.leaf(g, b.wall, false, policy);
}

/* a warped block with a lift of each corner, over a convex quadrilateral */
uint32_t warped_any(Building &b, SplitMix64 &r, int64_t size)
{
  GridBuilder<L> g;
  const int64_t c[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  I3 lo[4], hi[4];
  for (int i = 0; i < 4; i++) {
    const int64_t x = c[i][0] * size + int64_t(r.below(uint64_t(size / 5)));
    const int64_t y = c[i][1] * size + int64_t(r.below(uint64_t(size / 5)));
    /* (even, so that edges have integer middles) */
    lo[i] = {2 * x, 2 * y, 0};
    hi[i] = {2 * x, 2 * y, 2 * size + 2 * int64_t(r.below(uint64_t(size / 3)))};
  }
  g.face({{lo[3], lo[2], lo[1], lo[0]}});
  g.face({{hi[0], hi[1], hi[2], hi[3]}});
  for (int i = 0; i < 4; i++) {
    const int j = (i + 1) % 4;
    g.face({{lo[i], lo[j], hi[j], hi[i]}});
  }
  topo::BuildPolicy policy;
  policy.allow_projectable_faces = true;
  return b.leaf(g, b.wall, false, policy);
}

/* the corners of a warped block's top, and how T(f) cuts it */
struct Top {
  I3 p[4];
  int d[2]; /* the ends of the diagonal */
  int t[2][3];

  /* false: the top is planar */
  bool read(const Building &b)
  {
    csg::Input in;
    const csg::Operand op{b.o.k[0].get(), {}};
    UT_CHECK(csg::read_operands({&op, 1}, in) == CsgCode::Ok);
    if (in.outlines.size() == 0) {
      return false;
    }
    const csg::Outline &o = in.outlines[0];
    std::map<uint32_t, int> at;
    for (int i = 0; i < 4; i++) {
      const csg::I3 &q = in.ipos[int(o.pts[i])];
      p[i] = {q[0] - in.shift[0], q[1] - in.shift[1], q[2] - in.shift[2]};
      at[o.pts[i]] = i;
    }
    for (int k = 0; k < 2; k++) {
      for (int i = 0; i < 3; i++) {
        t[k][i] = at[in.polygons[int(o.first) + k].pts[i]];
      }
    }
    int n = 0;
    for (int i = 0; i < 4; i++) {
      const bool both = std::count(t[0], t[0] + 3, i) && std::count(t[1], t[1] + 3, i);
      if (both) {
        d[n++] = i;
      }
    }
    UT_CHECK(n == 2);
    return true;
  }
  /* a prism on triangle k, upwards */
  void prism_on(GridBuilder<L> &g, int k, int64_t height) const
  {
    I3 a[3], b[3];
    for (int i = 0; i < 3; i++) {
      a[i] = b[i] = p[t[k][i]];
      b[i][2] += height;
    }
    g.face({{a[2], a[1], a[0]}});
    g.face({{b[0], b[1], b[2]}});
    for (int i = 0; i < 3; i++) {
      g.face({{a[i], a[(i + 1) % 3], b[(i + 1) % 3], b[i]}});
    }
  }
};

/* the top of a warped block, and something at it */
void test_whole()
{
  SplitMix64 r(46);
  uint32_t whole = 0, split = 0, cut = 0;
  for (int s = 0; s < 120; s++) {
    Case c;
    std::vector<uint32_t> all = {warped_any(c, r, 30)};
    const csg::GridPlacement pl = random_placement(r);
    Top top;
    if (!top.read(c)) {
      continue;
    }
    const int kind = s % 9;
    GridBuilder<L> g;
    const I3 far = {30, 30, 200};
    if (kind == 1) {
      /* a block of another name on one triangle: two faces at the top */
      top.prism_on(g, 0, 40);
      all.push_back(c.leaf(g, c.stone));
    }
    else if (kind == 2) {
      /* a fin on the diagonal */
      g.face({{top.p[top.d[0]], top.p[top.d[1]], far}});
      all.push_back(c.leaf(g, c.wall, true));
    }
    else if (kind == 3) {
      /* a spike at a corner: it stays one face */
      g.face({{top.p[0], far, {31, 30, 200}}});
      all.push_back(c.leaf(g, c.wall, true));
    }
    else if (kind == 4) {
      /* a fin in the middle of an edge of the top */
      const I3 m = {(top.p[0][0] + top.p[1][0]) / 2, (top.p[0][1] + top.p[1][1]) / 2,
                    (top.p[0][2] + top.p[1][2]) / 2};
      g.face({{m, {m[0], m[1] - 20, m[2] + 9}, {m[0] + 1, m[1] - 20, m[2] + 10}}});
      all.push_back(c.leaf(g, c.wall, true));
    }
    else if (kind == 8) {
      /* a fin that cuts one triangle into two, from a corner */
      int apex = 0;
      while (std::count(top.d, top.d + 2, top.t[0][apex]) == 0) {
        apex++;
      }
      const I3 &u = top.p[top.t[0][(apex + 1) % 3]], &v = top.p[top.t[0][(apex + 2) % 3]];
      const I3 m = {(u[0] + v[0]) / 2, (u[1] + v[1]) / 2, (u[2] + v[2]) / 2};
      /* (the edge opposite to an end of the diagonal isn't the diagonal) */
      g.face({{top.p[top.t[0][apex]], m, far}});
      all.push_back(c.leaf(g, c.wall, true));
    }
    else if (kind == 5) {
      /* one triangle is inside another block of the same name */
      top.prism_on(g, 0, 40);
      all.push_back(c.leaf(g, c.wall));
    }
    c.op(Op::Union, all);
    if (kind == 6 || kind == 7) {
      c.o.ops[0].place = pl;
    }
    OutputOptions opt;
    if (kind == 7) {
      opt.placement = rotation(r, false);
      opt.quantum_shift = 2;
    }
    UT_CHECK(c.run(opt) == CsgCode::Ok);
    const uint32_t w = c.rep.result.stats.whole, sp = c.rep.split_whole;
    whole += w;
    split += sp;
    cut += w + sp == 0;
    UT_CHECK(w + sp == (kind == 1 || kind == 2 || kind == 4 || kind == 5 || kind == 8 ? 0u : 1u));
    UT_CHECK(sp == 0 || kind >= 6);
    /* it has the area of the facets, and two faces if it isn't whole */
    Arrangement b;
    const csg::Operand op{&c.k, {}};
    UT_CHECK(csg::read_operands({&op, 1}, b.in) == CsgCode::Ok);
    const double want = facet_area(c.a, c.sel), got = polygon_area(b.in) * std::ldexp(1.0, -2 * opt.quantum_shift);
    UT_CHECK(std::abs(want - got) < 0.02 * want);
    uint32_t pieces = 0;
    for (const csg::ResultFace &f : c.rep.result.faces) {
      pieces += !c.a.in.polygons[int(f.source)].whole;
    }
    UT_CHECK(pieces == (w ? 1u : kind == 5 ? 1u : kind == 8 ? 3u : 2u));
  }
  printf("warped tops: %u whole, %u split, %u cut\n", whole, split, cut);
  UT_CHECK(whole > 30 && split > 2 && cut > 40);
}

/* piles of tetrahedra at a coarse quantum: failures are found */
void test_coarse()
{
  SplitMix64 r(47);
  std::map<int, uint32_t> codes;
  uint32_t ok = 0;
  for (int s = 0; s < 250; s++) {
    Case c;
    std::vector<uint32_t> all;
    for (int t = 0; t < 5; t++) {
      GridBuilder<L> g;
      I3 v[4];
      int o = 0;
      while (o == 0) {
        for (I3 &x : v) {
          x = vertex(r, 250);
        }
        o = orientation(v[0], v[1], v[2], v[3]);
      }
      if (o < 0) {
        std::swap(v[0], v[1]);
      }
      g.face({{v[0], v[2], v[1]}});
      g.face({{v[0], v[1], v[3]}});
      g.face({{v[1], v[2], v[3]}});
      g.face({{v[0], v[3], v[2]}});
      all.push_back(c.leaf(g, t % 2 ? c.wall : c.stone));
    }
    c.op(Op::Union, all);
    const CsgCode code = c.run();
    codes[int(c.rep.round)]++;
    if (code == CsgCode::Ok) {
      ok++;
      UT_CHECK(topo::validate(c.k, topo::ValidateLevel::Global).ok());
      std::vector<uint32_t> label(c.k.regions.capacity(), kNone);
      for (uint32_t i = 0; i < c.rep.result.faces.size(); i++) {
        for (uint32_t side = 0; side < 2; side++) {
          uint32_t &l = label[c.k.region(topo::FaceRef(c.rep.face_of[int(i)]), side).i];
          UT_CHECK(l == kNone || l == c.rep.result.faces[int(i)].solid[side]);
          l = c.rep.result.faces[int(i)].solid[side];
        }
      }
    }
  }
  printf("coarse: %u ok;", ok);
  for (const auto &[x, n] : codes) {
    printf(" code %d x%u", x, n);
  }
  printf("\n");
  UT_CHECK(ok > 20 && codes[int(RoundCode::SamePosition)] > 20 && codes[int(RoundCode::Invalid)] > 5 &&
           codes[int(RoundCode::Radial)] > 2);
}

void test_projected()
{
  {
    /* nothing cuts the top: it stays one face, mirrored too */
    for (int mirror = 0; mirror < 2; mirror++) {
      Case c;
      c.op(Op::Union, {warped(c, {0, 0, 0}, {8, 8, 4}, 3), c.box({20, 0, 0}, {24, 4, 4}, c.wall)});
      if (mirror) {
        c.o.ops[0].place.sign[0] = -1;
      }
      /* (mirrored, ties in the CDT may go the other way: then it is output as
       * its two triangles) */
      UT_CHECK(c.run() == CsgCode::Ok && c.rep.result.stats.whole + c.rep.split_whole == 1);
      UT_CHECK(mirror || (c.counts(16, 24, 12, 3) && c.rep.result.stats.whole == 1 && c.rep.projected == 1));
    }
  }
  {
    /* a chimney through it: triangles and their pieces, and the chimney */
    Case c;
    c.op(Op::Union, {warped(c, {0, 0, 0}, {16, 16, 8}, 3), c.box({6, 7, 2}, {9, 9, 20}, c.stone)});
    OutputOptions opt;
    opt.quantum_shift = 8;
    UT_CHECK(c.run(opt) == CsgCode::Ok && c.rep.result.stats.whole == 0 && c.k.regions.live_count() == 3);
  }
  {
    /* a hole in it: no face may be reversed wrongly */
    Case c;
    c.op(Op::Difference, {warped(c, {0, 0, 0}, {16, 16, 8}, 3), warped(c, {4, 4, 4}, {9, 9, 20}, 2)});
    OutputOptions opt;
    opt.quantum_shift = 8;
    UT_CHECK(c.run(opt) == CsgCode::Ok && c.k.regions.live_count() == 2);
    for (const csg::ResultFace &f : c.rep.result.faces) {
      UT_CHECK(f.label[0] == kVoid && f.label[1] == c.wall);
      UT_CHECK(f.reversed == (c.a.in.polygons[int(f.source)].leaf == 1));
    }
  }
  {
    /* a cavity with a warped ceiling: the whole face is turned round */
    Case c;
    c.op(Op::Difference, {c.box({0, 0, 0}, {20, 20, 20}, c.wall), warped(c, {4, 4, 4}, {9, 9, 9}, 3)});
    UT_CHECK(c.run() == CsgCode::Ok && c.counts(16, 24, 12, 3) && c.rep.result.stats.whole == 1);
    for (const csg::ResultFace &f : c.rep.result.faces) {
      UT_CHECK(f.reversed == (c.a.in.polygons[int(f.source)].leaf == 1));
    }
  }
  {
    /* turned: the same triangles, or (if the CDT is another one) triangles */
    SplitMix64 r(42);
    uint32_t whole = 0, split = 0;
    for (int i = 0; i < 60; i++) {
      Case c;
      const int64_t w = 40 + int64_t(r.below(40));
      c.op(Op::Union, {warped(c, {0, 0, 0}, {w, 40 + int64_t(r.below(40)), 40}, 1 + int64_t(r.below(30))),
                       c.box({200, 0, 0}, {210, 4, 4}, c.wall)});
      OutputOptions opt;
      opt.placement = rotation(r, false);
      UT_CHECK(c.run(opt) == CsgCode::Ok);
      UT_CHECK(c.rep.result.stats.whole + c.rep.split_whole == 1);
      whole += c.rep.result.stats.whole;
      split += c.rep.split_whole;
      /* four quads and the two triangles, or the top in one piece */
      UT_CHECK(c.k.faces.live_count() == 6 + 5 + (c.rep.split_whole ? 2u : 1u));
      double top = 0;
      for (uint32_t t = 0; t < c.a.in.polygons.size(); t++) {
        top += !c.a.in.polygons[int(t)].whole;
      }
      UT_CHECK(top == 2);
      Arrangement b;
      const csg::Operand op{&c.k, {}};
      UT_CHECK(csg::read_operands({&op, 1}, b.in) == CsgCode::Ok);
      const double want = polygon_area(c.a.in), got = polygon_area(b.in);
      UT_CHECK(std::abs(want - got) < 0.02 * want);
    }
    UT_CHECK(whole > 5 && split > 5);
  }
}

/* ---- buildings ---- */

void test_buildings()
{
  SplitMix64 r(43);
  uint32_t ok = 0, faces = 0;
  for (int i = 0; i < 12; i++) {
    Case c;
    BuildingOptions opt;
    opt.windows = 2;
    opt.metre = i % 2 ? 1000 : 8192;
    opt.floors = 1 + i % 2;
    make_building(c, r, opt);
    OutputOptions oo;
    if (i >= 4) {
      oo.placement = rotation(r, i < 8);
      oo.quantum_shift = 1;
    }
    UT_CHECK(c.run(oo) == CsgCode::Ok);
    ok++;
    faces += c.k.faces.live_count();
    /* walls, roof and chimney; a region of each, at least */
    std::map<uint32_t, uint32_t> labels;
    for (const csg::ResultFace &f : c.rep.result.faces) {
      labels[f.solid[0]]++;
      labels[f.solid[1]]++;
    }
    UT_CHECK(labels[c.wall] > 20 && labels[c.roof] > 8 && labels[c.stone] > 8);
  }
  printf("buildings: %u, %u faces\n", ok, faces);
}

/* ---- placements ---- */

void test_placement()
{
  csg::ExactPlacement e;
  csg::RigidPlacement p;
  UT_CHECK(e.set(p));
  /* half up, independent of the representation; with a shift */
  csg::Input in;
  in.shift = csg::I3(5, 0, 0);
  for (int64_t n = -40; n < 40; n++) {
    csg::HPoint h;
    h.x[0] = csg::Int256::from(n + 5 * 8);
    h.x[1] = csg::Int256::from(n);
    h.x[2] = csg::Int256::from(0);
    h.w = csg::Int256::from(8);
    csg::HPoint h7 = h;
    for (int i = 0; i < 3; i++) {
      h7.x[i] = h.x[i] * csg::Int256::from(7);
    }
    h7.w = h.w * csg::Int256::from(7);
    const csg::I3 a = e.round(in, h, 0), b = e.round(in, h7, 0), c = e.round(in, h, 2);
    const int64_t want = int64_t(std::floor(double(n) / 8 + 0.5)), fine = int64_t(std::floor(double(n) / 2 + 0.5));
    UT_CHECK(a[0] == want && a[1] == want && a[2] == 0 && b[0] == want && b[1] == want);
    UT_CHECK(c[0] == fine && c[1] == fine);
  }
  /* a quarter turn about z, and a translation of part of a quantum */
  p.r[0][0] = p.r[1][1] = 0;
  p.r[0][1] = -1;
  p.r[1][0] = 1;
  p.t[0] = 10.25;
  p.t[2] = -0.5;
  UT_CHECK(e.set(p));
  in.shift = csg::I3(0, 0, 0);
  const csg::I3 q = e.round(in, csg::HPoint::from(csg::I3(3, 4, 5)), 0);
  UT_CHECK(q[0] == 6 && q[1] == 3 && q[2] == 5);
  const csg::I3 q2 = e.round(in, csg::HPoint::from(csg::I3(3, 4, 5)), 2);
  UT_CHECK(q2[0] == 25 && q2[1] == 12 && q2[2] == 18);
  /* against doubles */
  SplitMix64 r(44);
  for (int i = 0; i < 2000; i++) {
    p = rotation(r, i % 2 == 0);
    UT_CHECK(e.set(p));
    const csg::I3 v(int64_t(r.below(2000)) - 1000, int64_t(r.below(2000)) - 1000, int64_t(r.below(2000)) - 1000);
    const csg::I3 got = e.round(in, csg::HPoint::from(v), 10);
    const D3 want = apply(p, {double(v[0]), double(v[1]), double(v[2])}, 1024);
    for (int d = 0; d < 3; d++) {
      UT_CHECK(std::abs(double(got[d]) - want[d]) <= 0.5 + 1e-6);
    }
  }
  p = {};
  p.r[0][0] = -1;
  UT_CHECK(!e.set(p)); /* a mirror */
  p = {};
  p.r[0][1] = 1e-6;
  UT_CHECK(!e.set(p)); /* a shear */
  p = {};
  p.r[1][1] = 1.001;
  UT_CHECK(!e.set(p));
  p = {};
  p.r[2][0] = std::nan("");
  UT_CHECK(!e.set(p));
  p = {};
  p.t[1] = std::nan("");
  UT_CHECK(!e.set(p));
  p.t[1] = 1e13;
  UT_CHECK(!e.set(p));
  /* BadPlacement and OutOfRange come out of build_output */
  for (int kind = 0; kind < 3; kind++) {
    Case c;
    c.box({0, 0, 0}, {4, 5, 6}, c.wall);
    OutputOptions opt;
    opt.placement.r[0][0] = kind == 0 ? 2 : 1;
    opt.placement.t[0] = kind == 1 ? double(csg::kCoordLimit) - 4 : kind == 2 ? double(csg::kCoordLimit) - 3 : 0;
    UT_CHECK(c.run(opt) == (kind == 0 ? CsgCode::BadPlacement : kind == 1 ? CsgCode::Ok : CsgCode::OutOfRange));
  }
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_placement();
  test_cases();
  test_merging();
  test_names_only();
  test_projected();
  test_whole();
  test_coarse();
  test_buildings();
  test_random();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
