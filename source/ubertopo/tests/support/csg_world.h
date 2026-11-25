#pragma once

/* A CSG tree with its operands, and its result by name. */

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <vector>

#include "csg_oracle.h"
#include "ubertopo/csg/evaluate.h"
#include "ubertopo/topo/io.h"
#include "ubertopo/topo/validate.h"

namespace ubertopo::test {

using csg::BoolOptions;
using csg::BoolReport;
using csg::CsgCode;
using csg::CsgLeaf;
using csg::CsgTree;
using csg::LabelKind;
using csg::Op;
using csg::OperandStore;
using ident::kNoId;
using ident::PersistentId;

using Key = std::pair<uint64_t, uint64_t>;
using P3 = std::array<int64_t, 3>;

inline Key key(PersistentId i)
{
  return {i.lo, i.hi};
}
inline PersistentId id(uint64_t i)
{
  return pid(i, 99);
}

/* a result, by name */
struct Out {
  LComplex k;
  BoolReport rep;
  std::map<Key, P3> verts;
  std::map<Key, std::multiset<std::set<Key>>> faces; /* loops, as sets of vertices */
  std::set<Key> regions;

  P3 at(const topo::VertRef v) const
  {
    const auto &p = k.pos(v);
    return {p[0].raw, p[1].raw, p[2].raw};
  }
  std::set<P3> face_points(Key f) const
  {
    std::set<P3> s;
    for (const auto &l : faces.at(f)) {
      for (const Key &v : l) {
        s.insert(verts.at(v));
      }
    }
    return s;
  }
};

inline Out run(const CsgTree &t, const OperandStore &store, const BoolOptions &opt = {})
{
  Out o;
  UT_CHECK(csg::evaluate(t, store, opt, o.k, o.rep) == CsgCode::Ok);
  o.k.verts.for_each_live([&](uint32_t v) {
    UT_CHECK(o.verts.emplace(key(o.k.vert_id(topo::VertRef(v))), o.at(topo::VertRef(v))).second);
  });
  o.k.faces.for_each_live([&](uint32_t f) {
    auto &loops = o.faces[key(o.k.face_id(topo::FaceRef(f)))];
    UT_CHECK(loops.empty());
    o.k.for_each_loop(topo::FaceRef(f), [&](topo::LoopRef l) {
      std::set<Key> s;
      o.k.for_each_coedge(l, [&](topo::CoedgeRef c) { s.insert(key(o.k.vert_id(o.k.start_vert(c)))); });
      loops.insert(s);
    });
  });
  o.k.regions.for_each_live([&](uint32_t r) {
    UT_CHECK(o.regions.insert(key(o.k.regions.get<".id">()[r])).second);
  });
  return o;
}

inline LComplex build(const GridBuilder<L> &g, const topo::BuildPolicy &policy = {})
{
  LComplex k;
  topo::BuildReport br;
  topo::RegionReport rr;
  UT_CHECK(g.b.build(k, br, policy) && topo::build_regions(k, rr));
  return k;
}

struct World {
  OperandStore store;
  CsgTree t;

  PersistentId add(const GridBuilder<L> &g, const topo::BuildPolicy &policy = {})
  {
    return store.add(build(g, policy));
  }
  PersistentId box(const I3 &lo, const I3 &hi)
  {
    GridBuilder<L> g;
    g.box(lo, hi);
    return add(g);
  }
  PersistentId region(PersistentId operand) const
  {
    return store.find(operand)->regions.get<".id">()[1];
  }
  CsgLeaf leaf(PersistentId operand, const I3 &offset = {0, 0, 0}, PersistentId name = kNoId,
               LabelKind kind = LabelKind::Solid) const
  {
    CsgLeaf l;
    l.operand = operand;
    l.kind = kind;
    for (int i = 0; i < 3; i++) {
      l.place.offset[i] = offset[i];
    }
    if (name != kNoId) {
      l.rules.append({region(operand), kind, name});
    }
    return l;
  }
  void op(uint64_t i, uint64_t parent, Op o)
  {
    UT_CHECK(t.add_op(id(i), parent ? id(parent) : kNoId, o) == CsgCode::Ok);
  }
  void put(uint64_t i, uint64_t parent, const CsgLeaf &l, uint32_t index = CsgTree::kEnd)
  {
    UT_CHECK(t.add_leaf(id(i), parent ? id(parent) : kNoId, l, index) == CsgCode::Ok);
  }
  void set(uint64_t i, const CsgLeaf &l)
  {
    UT_CHECK(t.set_leaf(id(i), l) == CsgCode::Ok);
  }
  Out run() const
  {
    Out o = test::run(t, store);
    UT_CHECK(o.rep.output.names.weak == 0);
    return o;
  }
};

template <typename T> std::set<Key> names(const std::map<Key, T> &m)
{
  std::set<Key> s;
  for (const auto &[k, v] : m) {
    s.insert(k);
  }
  return s;
}
inline std::set<Key> minus(const std::set<Key> &a, const std::set<Key> &b)
{
  std::set<Key> s;
  std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::inserter(s, s.begin()));
  return s;
}
/* the vertices that both have are where they were */
inline uint32_t moved(const Out &a, const Out &b)
{
  uint32_t n = 0;
  for (const auto &[k, p] : a.verts) {
    const auto it = b.verts.find(k);
    n += it != b.verts.end() && it->second != p;
  }
  return n;
}

} // namespace ubertopo::test
