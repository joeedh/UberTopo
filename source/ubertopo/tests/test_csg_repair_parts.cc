/* The parts of the repair of results that can't be rounded (plan
 * 2026-11-25-boolean-repair): reading rounded faces, labels from winding
 * numbers, selection, triangles, joining. test_csg_repair.cc has the whole. */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "litestl/util/alloc.h"
#include "ut_test.h"

#include "support/csg_surface.h"
#include "support/csg_world.h"

using namespace ubertopo;
using namespace ubertopo::test;
using csg::Arrangement;
using csg::kNone;
using Selection = litestl::util::Vector<csg::Selected>;

namespace {

/* ---- labels from winding numbers ---- */

void test_winding()
{
  SplitMix64 r(92);
  uint32_t checked = 0, unclear = 0, ties = 0, several = 0, thin = 0, cells = 0, dropped = 0, whole = 0, degenerate = 0,
           shared = 0, airs = 0, edge_uses = 0;
  for (int s = 0; s < 450; s++) {
    Operands o;
    csg::Expression e;
    std::vector<uint32_t> label;
    for (int i = 0; i < 3; i++) {
      label.push_back(e.add_label(csg::LabelKind::Solid));
    }
    const uint32_t airs_of[2] = {e.add_label(csg::LabelKind::Air), e.add_label(csg::LabelKind::Air)};
    std::vector<uint32_t> all;
    const int n = 1 + int(r.below(3));
    for (int i = 0; i < n; i++) {
      add_random(r, 3, o);
      std::vector<uint32_t> l(o.k.back()->regions.capacity(), r.below(4) ? label[r.below(3)] : airs_of[r.below(2)]);
      l[0] = csg::kVoid;
      all.push_back(e.add_leaf(true, l));
    }
    e.add_op(Op::Union, all);
    Arrangement a;
    UT_CHECK(csg::arrange(o.ops, a) == CsgCode::Ok);
    Selection sel;
    csg::select_facets(a, e, sel);
    csg::Result res;
    UT_CHECK(csg::merge_facets(a, e, sel, {}, res) == CsgCode::Ok);
    if (res.faces.size() == 0) {
      continue;
    }
    /* any positions at all */
    const int mode = s % 3;
    litestl::util::Vector<csg::I3> positions;
    litestl::util::Vector<uint8_t> used;
    for (uint32_t p = 0; p < a.in.points.size(); p++) {
      const D3 q = pos(a, p);
      csg::I3 x(0, 0, 0);
      for (int c = 0; c < 3; c++) {
        const int64_t y = int64_t(std::floor(q[c] + 0.5));
        x[c] = mode == 0 ? int64_t(r.below(5)) - 2 : mode == 1 ? y + int64_t(r.below(3)) - 1 : 8 * y + int64_t(r.below(17)) - 8;
      }
      positions.append(x);
      used.append(0);
    }
    for (const csg::ResultFace &f : res.faces) {
      for (uint32_t p : f.pts) {
        used[int(p)] = 1;
      }
    }
    csg::Names names;
    csg::Provenance from;
    csg::find_provenance(a, res, names, from);
    for (uint32_t p = 0; p < a.in.points.size(); p++) {
      names.vertex.append(PersistentId{r.next(), r.next()});
      from.verts.append({r.below(2) ? uint32_t(r.below(2)) : kNone, 0});
    }
    for (uint32_t i = 0; i < res.edges.size(); i++) {
      from.edges.append({r.below(3) ? uint32_t(r.below(3)) : kNone, uint32_t(r.below(9)), 1.0});
    }
    const csg::Rounded rounded{
        a, res, {positions.data(), size_t(positions.size())}, {used.data(), size_t(used.size())}, names, from};
    for (const bool exact : {false, true}) {
      Arrangement b;
      b.leaf_count = 1;
      litestl::util::Vector<uint32_t> point_of, solid, by_name;
      csg::RepairCounts counts, counts2;
      UT_CHECK(csg::read_rounded(rounded, exact, b.in, point_of, counts) == CsgCode::Ok);
      UT_CHECK(b.in.self && csg::build_arrangement(b) == CsgCode::Ok);
      for (const csg::Polygon &g : b.in.polygons) {
        whole += g.whole;
      }
      uint32_t here = 0;
      for (uint32_t p = 0; p < used.size(); p++) {
        here += used[int(p)];
        UT_CHECK((point_of[int(p)] != kNone) == (used[int(p)] != 0));
        if (used[int(p)]) {
          const csg::I3 &q = b.in.ipos[int(point_of[int(p)])], &x = positions[int(p)];
          UT_CHECK(q[0] == x[0] && q[1] == x[1] && q[2] == x[2]);
        }
      }
      UT_CHECK(here == b.in.verts.size() + counts.merged);
      /* of points that came together: one that was there before, one that an
       * operand has, the smallest name */
      for (uint32_t p = 0; p < used.size(); p++) {
        if (used[int(p)]) {
          auto rank = [&](uint32_t q) {
            return std::make_tuple(!a.in.is_input(q), from.verts[int(q)].leaf == kNone, key(names.vertex[int(q)]));
          };
          const csg::SrcVert &x = b.in.verts[int(point_of[int(p)])];
          UT_CHECK(x.point == point_of[int(p)] && point_of[int(x.vert)] == x.point);
          const auto &mine = names.vertex[int(p)], &best = names.vertex[int(x.vert)];
          UT_CHECK(p == x.vert || std::get<0>(rank(x.vert)) < std::get<0>(rank(p)) ||
                   (std::get<0>(rank(x.vert)) == std::get<0>(rank(p)) &&
                    (std::get<1>(rank(x.vert)) < std::get<1>(rank(p)) ||
                     (std::get<1>(rank(x.vert)) == std::get<1>(rank(p)) && best < mine))));
        }
      }
      /* of edges that came together: an operand's, the first one */
      std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> at_pair;
      for (uint32_t i = 0; i < res.edges.size(); i++) {
        const uint32_t p = point_of[int(res.edges[int(i)].a)], q = point_of[int(res.edges[int(i)].b)];
        if (p != q) {
          at_pair[{std::min(p, q), std::max(p, q)}].push_back(i);
        }
      }
      for (const csg::SrcEdge &x : b.in.edges) {
        if (x.edge == kNone) {
          continue;
        }
        UT_CHECK(x.a != x.b);
        auto rank = [&](uint32_t i) {
          return std::make_pair(from.edges[int(i)].leaf, from.edges[int(i)].leaf == kNone ? 0 : from.edges[int(i)].edge);
        };
        const auto &v = at_pair.at({x.a, x.b});
        shared += v.size() > 1;
        for (uint32_t i : v) {
          UT_CHECK(!(rank(i) < rank(x.edge)));
        }
      }
      /* the hash: of polygons, whatever their order and start */
      csg::Input turned_in = b.in;
      const PersistentId hash = csg::polygons_hash(b.in);
      if (b.in.polygons.size() > 1) {
        std::swap(turned_in.polygons[0], turned_in.polygons[1]);
        for (csg::Polygon &g : turned_in.polygons) {
          std::rotate(g.pts.begin(), g.pts.begin() + 1, g.pts.begin() + g.off[1]);
        }
        UT_CHECK(csg::polygons_hash(turned_in) == hash);
        std::swap(turned_in.polygons[0].region[0], turned_in.polygons[0].region[1]);
        UT_CHECK((csg::polygons_hash(turned_in) == hash) ==
                 (turned_in.polygons[0].region[0] == turned_in.polygons[0].region[1]));
        turned_in = b.in;
        turned_in.ipos[int(turned_in.polygons[0].pts[0])][1]++;
        UT_CHECK(!(csg::polygons_hash(turned_in) == hash));
      }
      /* (labels start at the first edge of a facet: any of them) */
      for (csg::Facet &fc : b.facets) {
        if (fc.off.size() > 1) {
          std::rotate(fc.edges.begin(), fc.edges.begin() + r.below(fc.off[1]), fc.edges.begin() + fc.off[1]);
        }
      }
      UT_CHECK(csg::label_by_winding(b, {}, solid, counts) == CsgCode::Ok && solid.size() == 2 * b.facets.size());
      /* names that turn the order of labels around */
      std::vector<PersistentId> turned;
      for (uint32_t l = 0; l < e.kinds.size(); l++) {
        turned.push_back(PersistentId{0, 100 - l});
      }
      UT_CHECK(csg::label_by_winding(b, turned, by_name, counts2) == CsgCode::Ok);
      UT_CHECK(counts2.unclear == counts.unclear);
      unclear += counts.unclear;
      degenerate += counts.broken;
      Selection kept;
      csg::select_repaired(b, solid, rounded, e, kept);
      for (uint32_t f = 0; f < b.facets.size(); f++) {
        const csg::Facet &fc = b.facets[int(f)];
        several += fc.sources.size() > 1;
        const csg::Selected &k = kept[int(f)];
        const uint32_t *l = &solid[int(2 * f)];
        auto side = [&](uint32_t p, uint32_t i) {
          const csg::Polygon &g = b.in.polygons[int(p)];
          return g.region[(g.sign > 0) == (i == 0) ? 0 : 1];
        };
        bool any_thin = false, any_right = false, any_jump = false;
        for (uint32_t p : fc.sources) {
          any_thin = any_thin || side(p, 0) == side(p, 1);
          any_jump = any_jump || side(p, 0) != side(p, 1);
          any_right = any_right || (side(p, 0) == l[0] && side(p, 1) == l[1]);
        }
        UT_CHECK(k.kept == (l[0] != l[1] || any_thin));
        dropped += !k.kept;
        if (k.kept) {
          UT_CHECK(std::find(fc.sources.begin(), fc.sources.end(), k.source) != fc.sources.end());
          for (uint32_t i = 0; i < 2; i++) {
            auto air_of = [&](uint32_t p) {
              const csg::Polygon &g = b.in.polygons[int(p)];
              const uint32_t x = res.faces[int(g.face)].label[(g.sign > 0) == (i == 0) ? 0 : 1];
              return e.kinds[int(x)] == csg::LabelKind::Air ? x : csg::kVoid;
            };
            uint32_t expect = l[i];
            if (l[i] == csg::kVoid) {
              expect = air_of(k.source);
              for (uint32_t p : fc.sources) {
                expect = expect == csg::kVoid ? air_of(p) : expect;
              }
            }
            UT_CHECK(k.label[i] == expect);
            airs += expect != l[i];
          }
          if (l[0] == l[1]) {
            UT_CHECK(side(k.source, 0) == side(k.source, 1));
            thin++;
          }
          else if (any_right) {
            UT_CHECK(side(k.source, 0) == l[0] && side(k.source, 1) == l[1]);
          }
          else if (any_jump) {
            UT_CHECK(side(k.source, 0) != side(k.source, 1));
          }
        }
        if (f > 60) {
          continue;
        }
        for (const D3 &x : inner_points(b, f, 1, r)) {
          for (uint32_t i = 0; i < 2; i++) {
            const std::map<uint32_t, int> w = winding(b.in, off_plane(x, unit_normal(b, fc.plane), i), r);
            if (w.empty()) {
              continue;
            }
            int best = 0, sum = 0;
            for (const auto &[z, c] : w) {
              best = std::max(best, c);
              sum += c;
            }
            UT_CHECK(sum == 1);
            std::vector<uint32_t> top;
            for (const auto &[z, c] : w) {
              if (c == best) {
                top.push_back(z);
              }
            }
            UT_CHECK(top.size() > 0 && solid[int(2 * f + i)] == top[0]);
            UT_CHECK(top.size() > 0 && by_name[int(2 * f + i)] == (top[0] == csg::kVoid ? csg::kVoid : top.back()));
            ties += top.size() > 1;
            checked++;
          }
        }
      }
      cells += 2 * uint32_t(b.facets.size());
      /* merged: edges know how the loops run */
      csg::Expression own;
      own.kinds = e.kinds;
      std::vector<uint32_t> ids;
      for (uint32_t l = 0; l < e.kinds.size(); l++) {
        ids.push_back(l);
      }
      own.add_leaf(true, ids);
      csg::Result merged;
      UT_CHECK(csg::merge_facets(b, own, kept, {}, merged) == CsgCode::Ok);
      std::set<std::array<uint32_t, 3>> runs;
      for (uint32_t f = 0; f < merged.faces.size(); f++) {
        const csg::ResultFace &x = merged.faces[int(f)];
        for (int l = 0; l + 1 < int(x.off.size()); l++) {
          for (uint32_t i = x.off[l]; i < x.off[l + 1]; i++) {
            runs.insert({f, x.pts[int(i)], x.pts[int(i + 1 < x.off[l + 1] ? i + 1 : x.off[l])]});
          }
        }
      }
      for (const csg::ResultEdge &x : merged.edges) {
        for (const csg::EdgeUse &u : x.uses) {
          UT_CHECK(runs.count({u.facet, u.dir > 0 ? x.a : x.b, u.dir > 0 ? x.b : x.a}));
          edge_uses++;
        }
      }
    }
  }
  printf("winding: %u of %u facet sides checked; %u unclear, %u ties, %u facets with several sources, %u thin, %u dropped; "
         "%u planar faces, %u broken, %u shared edges, %u air, %u edge uses\n",
         checked, cells, unclear, ties, several, thin, dropped, whole, degenerate, shared, airs, edge_uses);
  UT_CHECK(checked > 20000 && unclear > 500 && ties > 100 && several > 1000 && thin > 500 && dropped > 500 && whole > 300 &&
           degenerate > 500 && shared > 100 && airs > 100 && edge_uses > 10000);
}

/* ---- triangles ---- */

void test_triangles()
{
  SplitMix64 r(93);
  uint32_t faces = 0, holes = 0, back = 0, big = 0, wholes = 0;
  for (int s = 0; s < 350; s++) {
    Operands o;
    csg::Expression e;
    const uint32_t solid = e.add_label(csg::LabelKind::Solid);
    std::vector<uint32_t> all, first;
    for (int i = 0; i < 4; i++) {
      add_random(r, 3, o);
      std::vector<uint32_t> l(o.k.back()->regions.capacity(), solid);
      l[0] = csg::kVoid;
      all.push_back(e.add_leaf(true, l));
    }
    {
      /* a box with a warped top, that is cut out */
      GridBuilder<L> g;
      const int64_t z = 20 + int64_t(r.below(5));
      const I3 top[4] = {{20, 20, z}, {29, 20, z + 3}, {29, 29, z + 1}, {20, 29, z + 4}};
      std::vector<I3> up, down;
      for (int i = 0; i < 4; i++) {
        const I3 &p = top[i], &q = top[(i + 1) % 4];
        up.push_back(p);
        down.push_back({top[3 - i][0], top[3 - i][1], 10});
        g.face({{{p[0], p[1], 10}, {q[0], q[1], 10}, q, p}});
      }
      g.face({up});
      g.face({down});
      GridBuilder<L> outer;
      outer.box({18, 18, 8}, {31, 31, 40});
      for (const bool warped : {false, true}) {
        topo::BuildPolicy policy;
        policy.allow_projectable_faces = true;
        o.add(warped ? g : outer, policy);
        const std::vector<uint32_t> l = {csg::kVoid, solid};
        (warped ? all : first).push_back(e.add_leaf(false, l));
      }
    }
    first.push_back(all[0]);
    all[0] = e.add_op(Op::Union, first);
    e.add_op(Op::Difference, all);
    Arrangement a;
    UT_CHECK(csg::arrange(o.ops, a) == CsgCode::Ok);
    Selection sel;
    csg::select_facets(a, e, sel);
    csg::Result res;
    UT_CHECK(csg::merge_facets(a, e, sel, {}, res) == CsgCode::Ok);
    litestl::util::Vector<csg::PointTriangle> tris;
    for (const csg::ResultFace &f : res.faces) {
      UT_CHECK(csg::triangulate_face(a, f, tris) == CsgCode::Ok);
      UT_CHECK(tris.size() == f.pts.size() - 2 + 2 * (f.off.size() - 2));
      if (f.whole) {
        /* the solid is above it, and the front is out of the solid */
        UT_CHECK(f.reversed && tris.size() == 2);
        for (const csg::PointTriangle &t : tris) {
          UT_CHECK(cross(sub(pos(a, t[1]), pos(a, t[0])), sub(pos(a, t[2]), pos(a, t[0])))[2] < 0);
        }
        wholes++;
        continue;
      }
      const csg::Polygon &g = a.in.polygons[int(f.source)];
      D3 n = unit_normal(a, g.plane);
      if (f.front < 0) {
        n = {-n[0], -n[1], -n[2]};
      }
      double want = 0, got = 0;
      const D3 p0 = pos(a, f.pts[0]);
      for (int l = 0; l + 1 < int(f.off.size()); l++) {
        for (uint32_t i = f.off[l]; i < f.off[l + 1]; i++) {
          const uint32_t j = i + 1 < f.off[l + 1] ? i + 1 : f.off[l];
          want += dot(n, cross(sub(pos(a, f.pts[int(i)]), p0), sub(pos(a, f.pts[int(j)]), p0)));
        }
      }
      for (const csg::PointTriangle &t : tris) {
        const double x = dot(n, cross(sub(pos(a, t[1]), pos(a, t[0])), sub(pos(a, t[2]), pos(a, t[0]))));
        UT_CHECK(x > 0);
        got += x;
      }
      UT_CHECK(std::abs(want - got) < 1e-9 * std::abs(want) + 1e-12);
      faces++;
      holes += f.off.size() > 2;
      back += f.front < 0;
      big += f.pts.size() > 9;
    }
  }
  printf("triangles: %u faces, %u with holes, %u seen from behind, %u with ten points or more\n", faces, holes, back, big);
  UT_CHECK(faces > 1500 && holes > 50 && back > 500 && big > 40 && wholes == 350);
}

/* ---- joining ---- */

void test_join()
{
  /* a square of two triangles, beside another one */
  auto make = [] {
    csg::Result res;
    auto tri = [&](uint32_t p, uint32_t q, uint32_t t) {
      csg::ResultFace f;
      f.front = 1;
      f.label[0] = f.solid[0] = 0;
      f.label[1] = f.solid[1] = 1;
      for (uint32_t x : {p, q, t}) {
        f.pts.append(x);
      }
      f.off.append(0);
      f.off.append(3);
      const uint32_t id = res.faces.size();
      for (uint32_t i = 0; i < 3; i++) {
        const uint32_t a = f.pts[int(i)], b = f.pts[int((i + 1) % 3)];
        bool known = false;
        for (csg::ResultEdge &x : res.edges) {
          if (x.a == std::min(a, b) && x.b == std::max(a, b)) {
            x.uses.append({id, int8_t(a < b ? 1 : -1)});
            known = true;
          }
        }
        if (!known) {
          res.edges.append({std::min(a, b), std::max(a, b), {0, 0}, {}});
          res.edges[int(res.edges.size()) - 1].uses.append({id, int8_t(a < b ? 1 : -1)});
        }
      }
      res.faces.append(std::move(f));
    };
    /* 3 2 5
     * 0 1 4 */
    tri(0, 1, 2);
    tri(0, 2, 3);
    tri(1, 4, 5);
    tri(1, 5, 2);
    return res;
  };
  const csg::I3 positions[6] = {csg::I3(0, 0, 0), csg::I3(4, 0, 1), csg::I3(4, 4, 0),
                                csg::I3(0, 4, 2), csg::I3(8, 0, 0), csg::I3(8, 4, 3)};
  auto count = [&](const csg::Result &res, const std::vector<uint32_t> &root, csg::Result *keep = nullptr) {
    csg::Result out;
    litestl::util::Vector<litestl::util::Vector<uint32_t, 2>> members;
    const bool any = csg::join_faces(res, positions, root, out, members);
    UT_CHECK(members.size() == out.faces.size() && any == (out.faces.size() < res.faces.size()));
    uint32_t n = 0, loose = 0;
    for (const auto &m : members) {
      n += m.size();
    }
    UT_CHECK(n == res.faces.size());
    for (const csg::ResultFace &f : out.faces) {
      loose += f.joined;
      UT_CHECK(f.off.size() == 2 && f.off[1] == f.pts.size());
    }
    UT_CHECK((loose > 0) == any);
    if (keep) {
      *keep = out;
    }
    return std::make_pair(uint32_t(out.faces.size()), uint32_t(out.edges.size()));
  };
  using P = std::pair<uint32_t, uint32_t>;
  csg::Result out;
  UT_CHECK((count(make(), {0, 0, 0, 0}, &out) == P{1, 6}) && out.faces[0].pts.size() == 6);
  for (const csg::ResultEdge &e : out.edges) {
    UT_CHECK(e.uses.size() == 1 && e.uses[0].facet == 0);
  }
  UT_CHECK((count(make(), {0, 0, 1, 1}) == P{2, 7}));
  UT_CHECK((count(make(), {0, 1, 2, 3}) == P{4, 9}));
  /* what keeps two faces apart */
  for (int why = 0; why < 8; why++) {
    csg::Result res = make();
    csg::ResultFace &f = res.faces[1];
    switch (why) {
      case 0:
        f.label[0] = 2;
        break;
      case 1:
        f.label[1] = 2;
        break;
      case 2:
        f.solid[0] = 2;
        break;
      case 3:
        f.solid[1] = 2;
        break;
      case 4:
        f.reversed = true;
        break;
      default:
        for (csg::ResultEdge &e : res.edges) {
          if (e.a == 0 && e.b == 2) {
            if (why == 5) {
              e.uses.append({2, 1}); /* a third face there */
            }
            else if (why == 6) {
              e.uses[0].dir = e.uses[1].dir;
            }
            else {
              e.uses.resize(1);
            }
          }
        }
    }
    UT_CHECK((count(res, {0, 0, 1, 1}) == P{3, 8}));
  }
  {
    /* a fan about a point in the middle: it would be lost */
    csg::Result res;
    csg::Result ring = make();
    (void)ring;
    const csg::I3 at4[5] = {csg::I3(0, 0, 0), csg::I3(4, 0, 0), csg::I3(4, 4, 0), csg::I3(0, 4, 0), csg::I3(2, 2, 1)};
    for (uint32_t i = 0; i < 4; i++) {
      csg::ResultFace f;
      f.front = 1;
      f.label[0] = f.solid[0] = 0;
      f.label[1] = f.solid[1] = 1;
      for (uint32_t x : {i, (i + 1) % 4, 4u}) {
        f.pts.append(x);
      }
      f.off.append(0);
      f.off.append(3);
      res.faces.append(std::move(f));
    }
    for (uint32_t i = 0; i < 4; i++) {
      csg::ResultEdge e{i, 4, {0, 0}, {}};
      e.uses.append({i, -1});
      e.uses.append({(i + 3) % 4, 1});
      res.edges.append(std::move(e));
      csg::ResultEdge g{std::min(i, (i + 1) % 4), std::max(i, (i + 1) % 4), {0, 0}, {}};
      g.uses.append({i, int8_t(i < (i + 1) % 4 ? 1 : -1)});
      res.edges.append(std::move(g));
    }
    const std::vector<uint32_t> root = {0, 0, 0, 0};
    litestl::util::Vector<litestl::util::Vector<uint32_t, 2>> members;
    /* three of them are a face, with the point on its outline */
    UT_CHECK(csg::join_faces(res, at4, root, out, members) && out.faces.size() == 2 && out.edges.size() == 6);
    UT_CHECK(out.faces[0].pts.size() + out.faces[1].pts.size() == 8);
  }
}

void test_rounding()
{
  csg::ExactPlacement e;
  csg::RigidPlacement p;
  UT_CHECK(e.set(p));
  csg::Input in;
  auto point = [](int64_t x, int64_t w) {
    csg::HPoint h;
    for (int i = 0; i < 3; i++) {
      h.x[i] = csg::Int256::from(i == 0 ? x : -x);
    }
    h.w = csg::Int256::from(w);
    return h;
  };
  for (int64_t num = -70; num <= 70; num++) {
    for (int64_t den : {1, 2, 3, 8, 16}) {
      for (int64_t bias = -3; bias <= 3; bias++) {
        for (int shift = 0; shift < 3; shift++) {
          const csg::I3 got = e.round(in, point(num, den), shift, csg::I3(bias, -bias, bias));
          /* floor(x 2^shift + 1/2 + bias / 8) */
          auto want = [&](int64_t n, int64_t b) {
            const int64_t t = 8 * n * (int64_t(1) << shift) + (4 + b) * den, q = 8 * den;
            return t >= 0 ? t / q : -((-t + q - 1) / q);
          };
          UT_CHECK(got[0] == want(num, bias) && got[1] == want(-num, -bias) && got[2] == want(-num, bias));
          UT_CHECK(den != 1 || got[0] == num * (int64_t(1) << shift));
        }
      }
    }
  }
}

/* what comes together: by hand, on the operands' own lattice */
void test_cases()
{
  SplitMix64 r(96);
  auto outcome = [&](const GridBuilder<L> &g, const std::map<P3, P3> &move, bool exact) {
    Operands o;
    o.add(g);
    csg::Expression e;
    const uint32_t s = e.add_label(csg::LabelKind::Solid);
    std::vector<uint32_t> l(o.k.back()->regions.capacity(), s);
    l[0] = csg::kVoid;
    e.add_leaf(true, l);
    Arrangement a;
    UT_CHECK(csg::arrange(o.ops, a) == CsgCode::Ok);
    Selection sel;
    csg::select_facets(a, e, sel);
    csg::Result res;
    UT_CHECK(csg::merge_facets(a, e, sel, {}, res) == CsgCode::Ok);
    litestl::util::Vector<csg::I3> positions;
    litestl::util::Vector<uint8_t> used;
    for (uint32_t p = 0; p < a.in.points.size(); p++) {
      csg::I3 q(0, 0, 0);
      if (a.in.is_input(p)) {
        q = csg::sub(a.in.ipos[int(p)], a.in.shift);
        const auto it = move.find({q[0], q[1], q[2]});
        if (it != move.end()) {
          q = csg::I3(it->second[0], it->second[1], it->second[2]);
        }
      }
      positions.append(q);
      used.append(0);
    }
    for (const csg::ResultFace &f : res.faces) {
      for (uint32_t p : f.pts) {
        used[int(p)] = 1;
      }
    }
    csg::Names names;
    csg::Provenance from;
    csg::find_provenance(a, res, names, from);
    const csg::Rounded rounded{
        a, res, {positions.data(), size_t(positions.size())}, {used.data(), size_t(used.size())}, names, from};
    Arrangement b;
    b.leaf_count = 1;
    litestl::util::Vector<uint32_t> point_of, solid;
    csg::RepairCounts counts;
    UT_CHECK(csg::read_rounded(rounded, exact, b.in, point_of, counts) == CsgCode::Ok);
    UT_CHECK(csg::build_arrangement(b) == CsgCode::Ok && csg::label_by_winding(b, {}, solid, counts) == CsgCode::Ok);
    Selection kept;
    csg::select_repaired(b, solid, rounded, e, kept);
    uint32_t n = 0;
    for (const csg::Selected &x : kept) {
      n += x.kept;
    }
    struct Outcome {
      uint32_t polygons, broken, merged, kept, unclear;
      bool operator==(const Outcome &) const = default;
    };
    return Outcome{uint32_t(b.in.polygons.size()), counts.broken, counts.merged, n, counts.unclear};
  };
  auto slab = [&](std::vector<std::array<int64_t, 2>> bottom, int64_t h) {
    GridBuilder<L> g;
    std::vector<I3> up, down;
    const size_t n = bottom.size();
    for (size_t i = 0; i < n; i++) {
      const auto &p = bottom[i], &q = bottom[(i + 1) % n];
      up.push_back({p[0], p[1], h});
      down.push_back({bottom[n - 1 - i][0], bottom[n - 1 - i][1], 0});
      g.face({{{p[0], p[1], 0}, {q[0], q[1], 0}, {q[0], q[1], h}, {p[0], p[1], h}}});
    }
    g.face({up});
    g.face({down});
    return g;
  };
  {
    /* a box that falls flat: nothing is left; and only its top half way */
    const GridBuilder<L> g = slab({{0, 0}, {4, 0}, {4, 4}, {0, 4}}, 1);
    const std::map<P3, P3> flat = {{{0, 0, 1}, {0, 0, 0}}, {{4, 0, 1}, {4, 0, 0}}, {{4, 4, 1}, {4, 4, 0}}, {{0, 4, 1}, {0, 4, 0}}};
    for (const bool exact : {false, true}) {
      const auto x = outcome(g, flat, exact);
      UT_CHECK(x.kept == 0 && x.merged == 4 && x.polygons >= 2);
    }
    const auto y = outcome(g, {{{0, 0, 1}, {0, 0, 0}}, {{0, 4, 1}, {0, 4, 0}}}, false);
    UT_CHECK(y.kept == 5 && y.merged == 2 && y.polygons == 5);
  }
  {
    /* turned inside out: solid where nothing is becomes nothing */
    const GridBuilder<L> g = slab({{0, 0}, {4, 0}, {4, 4}, {0, 4}}, 1);
    const auto x = outcome(
        g, {{{0, 0, 1}, {0, 0, -1}}, {{4, 0, 1}, {4, 0, -1}}, {{4, 4, 1}, {4, 4, -1}}, {{0, 4, 1}, {0, 4, -1}}}, false);
    UT_CHECK(x.kept == 0 && x.merged == 0 && x.broken == 0 && x.unclear == 0);
  }
  {
    /* a bow-tie: planar, but not a face (review, B1) */
    const GridBuilder<L> g = slab({{0, 0}, {8, 0}, {8, 4}, {0, 4}}, 2);
    const std::map<P3, P3> swap = {{{8, 0, 2}, {8, 4, 2}}, {{8, 4, 2}, {8, 0, 2}}, {{8, 0, 0}, {8, 4, 0}}, {{8, 4, 0}, {8, 0, 0}}};
    const auto x = outcome(g, swap, false);
    /* one half is solid, the other one inside out */
    UT_CHECK(x.broken == 2 && x.kept >= 5 && x.unclear == 0);
  }
  {
    /* a spike (a, b, a) of a planar face */
    const GridBuilder<L> g = slab({{0, 0}, {8, 0}, {8, 4}, {4, 4}, {4, 8}, {3, 4}, {0, 4}}, 2);
    const auto x = outcome(g, {{{3, 4, 0}, {4, 4, 0}}, {{3, 4, 2}, {4, 4, 2}}}, false);
    UT_CHECK(x.merged == 2 && x.broken == 2 && x.kept >= 6);
  }
  (void)r;
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_rounding();
  test_cases();
  test_join();
  test_triangles();
  test_winding();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
