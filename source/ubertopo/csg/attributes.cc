#include "ubertopo/csg/attributes.h"

#include <algorithm>
#include <cmath>

#include "litestl/util/map.h"

namespace ubertopo::csg {

namespace {

using litestl::attr::AttrGroup;
using litestl::attr::ColumnBase;
using litestl::attr::ColumnFlags;
using litestl::attr::MixPolicy;
using litestl::util::Vector;
using LComplex = topo::Complex<geom::Lattice>;
using topo::Grp;

constexpr int kGroups = 5;
constexpr Grp kGroup[kGroups] = {Grp::Verts, Grp::Edges, Grp::Faces, Grp::Sides, Grp::Regions};

template <typename K> auto &group_of(K &k, Grp g)
{
  switch (g) {
    case Grp::Verts:
      return k.verts.group();
    case Grp::Edges:
      return k.edges.group();
    case Grp::Faces:
      return k.faces.group();
    case Grp::Sides:
      return k.faces.sides().group();
    default:
      return k.regions.group();
  }
}

struct Copier {
  const Arrangement &a;
  const Result &res;
  const Names &names;
  const NameSource &src;
  const ElementMap &map;
  const topo::PropagationRules *rules;
  LComplex &k;

  Vector<topo::MixBuild> mixes; /* by leaf and group */

  topo::MixBuild &mix(uint32_t leaf, int group)
  {
    return mixes[int(leaf * kGroups) + group];
  }

  /* exact coordinates cut off at 2^-20, so that measures don't depend on the
   * compiler */
  double coord(uint32_t p, int axis) const
  {
    return double(floor_at(a.in.points[p], axis, 20));
  }
  double length(uint32_t p, uint32_t q) const
  {
    double s = 0;
    for (int i = 0; i < 3; i++) {
      const double d = coord(q, i) - coord(p, i);
      s += d * d;
    }
    return std::sqrt(s);
  }
  template <typename P, typename O> double area(const P &pts, const O &off) const
  {
    double n[3] = {0, 0, 0};
    for (int l = 0; l + 1 < int(off.size()); l++) {
      for (uint32_t i = off[l]; i < off[l + 1]; i++) {
        const uint32_t o = pts[0], p = pts[int(i)], q = pts[int(i + 1 < off[l + 1] ? i + 1 : off[l])];
        for (int c = 0; c < 3; c++) {
          const int u = (c + 1) % 3, v = (c + 2) % 3;
          n[c] += (coord(p, u) - coord(o, u)) * (coord(q, v) - coord(o, v)) -
                  (coord(p, v) - coord(o, v)) * (coord(q, u) - coord(o, u));
        }
      }
    }
    return std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  }

  static uint64_t key(uint32_t leaf, uint32_t slot)
  {
    return (uint64_t(leaf) << 32) | slot;
  }

  void vertices()
  {
    for (uint32_t p = 0; p < names.vertex_src.size(); p++) {
      const uint32_t i = names.vertex_src[int(p)];
      if (i != kNone) {
        const SrcVert &v = a.in.verts[int(i)];
        mix(v.leaf, 0).add(map.vert_of[p], {{v.vert, 1.0}});
      }
    }
  }

  void edges()
  {
    /* a source edge is as long as its pieces */
    litestl::util::Map<uint64_t, double> total;
    for (const SrcEdge &e : a.in.edges) {
      if (e.edge != kNone) {
        const double l = length(e.a, e.b);
        double *t = total.lookup_ptr(key(e.leaf, e.edge));
        t ? void(*t += l) : void(total.add(key(e.leaf, e.edge), l));
      }
    }
    for (uint32_t x = 0; x < names.edge_src.size(); x++) {
      const uint32_t i = names.edge_src[int(x)];
      if (i != kNone) {
        const SrcEdge &e = a.in.edges[int(i)];
        const double part = length(res.edges[int(x)].a, res.edges[int(x)].b) / *total.lookup_ptr(key(e.leaf, e.edge));
        mix(e.leaf, 1).add(map.edge_of[x], {{e.edge, part}});
      }
    }
  }

  void faces()
  {
    litestl::util::Map<uint64_t, double> total;
    for (const Polygon &g : a.in.polygons) {
      const double s = area(g.pts, g.off);
      double *t = total.lookup_ptr(key(g.leaf, g.face));
      t ? void(*t += s) : void(total.add(key(g.leaf, g.face), s));
    }
    for (uint32_t f = 0; f < res.faces.size(); f++) {
      const ResultFace &r = res.faces[int(f)];
      const Polygon &g = a.in.polygons[int(r.source)];
      const double part = r.whole ? 1.0 : area(r.pts, r.off) / *total.lookup_ptr(key(g.leaf, g.face));
      mix(g.leaf, 2).add(map.face_of[f], {{g.face, part}});
      for (uint32_t side = 0; side < 2; side++) {
        const uint32_t from = r.reversed ? 1 - side : side;
        mix(g.leaf, 3).add(2 * map.face_of[f] + side, {{2 * g.face + from, part}});
      }
    }
  }

  void regions()
  {
    for (uint32_t i = 1; i < map.regions.size(); i++) {
      const uint32_t label = map.regions[i].solid != kVoid ? map.regions[i].solid : map.regions[i].air;
      if (label != kVoid) {
        mix(src.label_source[label].leaf, 4).add(i, {{src.label_source[label].region, 1.0}});
      }
    }
  }

  bool copy(uint32_t leaf, int g)
  {
    const topo::MixBuild &m = mix(leaf, g);
    if (m.empty()) {
      return true;
    }
    const LComplex &from = *src.operands[leaf].k;
    const AttrGroup &sg = group_of(from, kGroup[g]);
    AttrGroup &dg = group_of(k, kGroup[g]);
    auto policy = [&](std::string_view name, uint32_t, const ColumnBase &col) {
      if (kGroup[g] == Grp::Verts && name == "pos") {
        return MixPolicy::Skip;
      }
      if (has_flag(col.flags(), ColumnFlags::Temp) || has_flag(col.flags(), ColumnFlags::Derived)) {
        return MixPolicy::SetDefault;
      }
      const topo::ColumnRule *r = rules ? rules->find(kGroup[g], name) : nullptr;
      return r && r->split ? *r->split : MixPolicy::CopyFirst;
    };
    const AttrGroup::PolicyFn pf(policy);
    const topo::PropEventInfo info{topo::PropEvent::Split, kGroup[g]};
    /* (false: a column of the same name has another type) */
    if (!dg.mix_from(sg, m.batch(), &pf, &info)) {
      return false;
    }
    /* geometry refs are the operand's: take them over */
    for (uint32_t i = 0; i < dg.column_count(); i++) {
      ColumnBase &c = dg.column(i);
      if (!has_flag(c.flags(), ColumnFlags::Topo) && c.holds_refs() && sg.find_any(dg.name(i)) &&
          !k.geom.import_rows(c, m.batch().targets, from.geom, memo[int(leaf)]))
      {
        return false;
      }
    }
    return true;
  }
  Vector<litestl::attr::PoolStore::ImportMemo> memo;

  CsgCode run(uint32_t *bad_leaf)
  {
    for (uint32_t i = 0; i < src.operands.size(); i++) {
      for (int g = 0; g < kGroups; g++) {
        mixes.append({});
      }
      memo.append({});
    }
    vertices();
    edges();
    faces();
    regions();
    for (uint32_t leaf = 0; leaf < src.operands.size(); leaf++) {
      for (int g = 0; g < kGroups; g++) {
        if (!copy(leaf, g)) {
          if (bad_leaf) {
            *bad_leaf = leaf;
          }
          return CsgCode::BadOperand;
        }
      }
    }
    return CsgCode::Ok;
  }
};

} // namespace

CsgCode copy_attributes(const Arrangement &a, const Result &result, const Names &names, const NameSource &source,
                        const ElementMap &map, const topo::PropagationRules *rules, LComplex &k, uint32_t *bad_leaf)
{
  Copier c{a, result, names, source, map, rules, k, {}, {}};
  return c.run(bad_leaf);
}

} // namespace ubertopo::csg
