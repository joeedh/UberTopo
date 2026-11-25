#include "ubertopo/csg/attributes.h"

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
  const Provenance &from;
  const NameSource &src;
  const ElementMap &map;
  const topo::PropagationRules *rules;
  LComplex &k;

  Vector<topo::MixBuild> mixes; /* by leaf and group */

  topo::MixBuild &mix(uint32_t leaf, int group)
  {
    return mixes[int(leaf * kGroups) + group];
  }

  void vertices()
  {
    for (uint32_t p = 0; p < from.verts.size(); p++) {
      const Provenance::Vertex &v = from.verts[int(p)];
      if (v.leaf != kNone && map.vert_of[p] != kNone) {
        mix(v.leaf, 0).add(map.vert_of[p], {{v.vert, 1.0}});
      }
    }
  }

  void edges()
  {
    for (uint32_t x = 0; x < from.edges.size(); x++) {
      const Provenance::Edge &e = from.edges[int(x)];
      if (e.leaf != kNone) {
        mix(e.leaf, 1).add(map.edge_of[x], {{e.edge, e.part}});
      }
    }
  }

  void faces()
  {
    for (uint32_t f = 0; f < from.faces.size(); f++) {
      const Provenance::Face &r = from.faces[int(f)];
      mix(r.leaf, 2).add(map.face_of[f], {{r.face, r.part}});
      for (uint32_t side = 0; side < 2; side++) {
        const uint32_t s = r.reversed ? 1 - side : side;
        mix(r.leaf, 3).add(2 * map.face_of[f] + side, {{2 * r.face + s, r.part}});
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
    const LComplex &op = *src.operands[leaf].k;
    const AttrGroup &sg = group_of(op, kGroup[g]);
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
          !k.geom.import_rows(c, m.batch().targets, op.geom, memo[int(leaf)]))
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

CsgCode copy_attributes(const Provenance &from, const NameSource &source, const ElementMap &map,
                        const topo::PropagationRules *rules, LComplex &k, uint32_t *bad_leaf)
{
  Copier c{from, source, map, rules, k, {}, {}};
  return c.run(bad_leaf);
}

} // namespace ubertopo::csg
