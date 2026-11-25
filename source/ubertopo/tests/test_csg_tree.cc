/* The CSG tree, its storage and evaluation (plan 2026-11-25-exact-booleans §2,
 * §5): edits against a simple model, bytes, hashes, labels, codes, and what
 * the nesting of the tree doesn't change. */

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "litestl/util/alloc.h"
#include "ut_test.h"

#include "support/csg_oracle.h"
#include "ubertopo/csg/evaluate.h"
#include "ubertopo/topo/io.h"
#include "ubertopo/topo/validate.h"

using namespace ubertopo;
using namespace ubertopo::test;
using csg::BoolOptions;
using csg::BoolReport;
using csg::CsgCode;
using csg::CsgLeaf;
using csg::CsgTree;
using csg::kNone;
using csg::LabelKind;
using csg::Op;
using csg::OperandStore;
using ident::kNoId;
using ident::PersistentId;

namespace {

PersistentId id(uint64_t i)
{
  return pid(i, 99);
}

LComplex box(const I3 &lo, const I3 &hi)
{
  GridBuilder<L> g;
  g.box(lo, hi);
  LComplex k;
  topo::BuildReport br;
  topo::RegionReport rr;
  const bool ok = g.b.build(k, br) && topo::build_regions(k, rr);
  UT_CHECK(ok);
  return k;
}

CsgLeaf leaf(PersistentId operand, bool thin = false)
{
  CsgLeaf l;
  l.operand = operand;
  l.thin = thin;
  return l;
}

/* ---- edits ---- */

struct Model {
  struct N {
    Op op;
    uint64_t parent; /* 0: none */
    std::vector<uint64_t> kids;
  };
  std::map<uint64_t, N> nodes;
  uint64_t root = 0;

  int depth(uint64_t n) const
  {
    int d = 0;
    for (; n; n = nodes.at(n).parent) {
      d++;
    }
    return d;
  }
  bool below(uint64_t n, uint64_t top) const
  {
    for (; n; n = nodes.at(n).parent) {
      if (n == top) {
        return true;
      }
    }
    return false;
  }
  void attach(uint64_t n, uint64_t p, uint32_t index)
  {
    nodes[n].parent = p;
    if (!p) {
      root = n;
      return;
    }
    auto &k = nodes[p].kids;
    k.insert(k.begin() + std::min<size_t>(index, k.size()), n);
  }
  size_t detach(uint64_t n)
  {
    const uint64_t p = nodes[n].parent;
    if (!p) {
      root = 0;
      return 0;
    }
    auto &k = nodes[p].kids;
    const auto it = std::find(k.begin(), k.end(), n);
    const size_t at = size_t(it - k.begin());
    k.erase(it);
    return at;
  }
  void erase(uint64_t n)
  {
    for (uint64_t k : std::vector<uint64_t>(nodes[n].kids)) {
      erase(k);
    }
    nodes.erase(n);
  }
  void walk(uint64_t n, std::vector<uint64_t> &leaves) const
  {
    if (nodes.at(n).op == Op::Leaf) {
      leaves.push_back(n);
    }
    for (uint64_t k : nodes.at(n).kids) {
      walk(k, leaves);
    }
  }
};

void compare(const CsgTree &t, const Model &m)
{
  UT_CHECK(t.size() == m.nodes.size() && t.empty() == m.nodes.empty());
  UT_CHECK(t.root() == (m.root ? id(m.root) : kNoId));
  for (const auto &[n, x] : m.nodes) {
    UT_CHECK(t.contains(id(n)) && t.op(id(n)) == x.op && (t.leaf(id(n)) != nullptr) == (x.op == Op::Leaf));
    UT_CHECK(t.parent(id(n)) == (x.parent ? id(x.parent) : kNoId));
    const auto kids = t.children(id(n));
    UT_CHECK(kids.size() == x.kids.size());
    for (size_t i = 0; i < x.kids.size() && i < kids.size(); i++) {
      UT_CHECK(kids[int(i)] == id(x.kids[i]));
    }
  }
  std::vector<uint64_t> leaves;
  if (m.root) {
    m.walk(m.root, leaves);
  }
  const auto got = t.leaves();
  UT_CHECK(got.size() == leaves.size());
  for (size_t i = 0; i < leaves.size() && i < got.size(); i++) {
    UT_CHECK(got[int(i)] == id(leaves[i]));
  }
}

void test_edits()
{
  SplitMix64 r(61);
  uint32_t done[7] = {}, refused = 0;
  for (int run = 0; run < 30; run++) {
    CsgTree t;
    Model m;
    uint64_t fresh = 1;
    for (int step = 0; step < 150; step++) {
      const litestl::util::Vector<uint8_t, 64> none;
      litestl::io::ByteWriter before;
      t.write(before);
      const PersistentId hash = csg::csg_hash(t);
      const uint64_t pick = 1 + r.below(fresh + 1), other = r.below(fresh + 1);
      const uint32_t index = r.below(3) ? uint32_t(r.below(4)) : CsgTree::kEnd;
      const bool known = m.nodes.count(pick), other_known = m.nodes.count(other);
      const Op op = Op(1 + r.below(3));
      const uint64_t kind = r.below(8);
      CsgCode got, want = CsgCode::Ok;
      if (kind < 4) {
        /* add; now and then with an ID that is there */
        const uint64_t n = r.below(8) ? fresh : pick;
        const bool l = kind % 2;
        const uint64_t p = m.nodes.empty() ? 0 : other;
        got = l ? t.add_leaf(id(n), p ? id(p) : kNoId, leaf(id(1000 + n)), index)
                : t.add_op(id(n), p ? id(p) : kNoId, op, index);
        if (m.nodes.count(n)) {
          want = CsgCode::DuplicateId;
        }
        else if (p == 0) {
          want = m.root ? CsgCode::BadTree : CsgCode::Ok;
        }
        else if (!other_known) {
          want = CsgCode::UnknownNode;
        }
        else if (m.nodes[p].op == Op::Leaf) {
          want = CsgCode::BadTree;
        }
        if (want == CsgCode::Ok) {
          m.nodes[n] = {l ? Op::Leaf : op, 0, {}};
          m.attach(n, p, index);
          fresh = std::max(fresh, n + 1);
          done[l]++;
        }
      }
      else if (kind == 4) {
        got = t.wrap(id(fresh), op, id(pick));
        want = known ? CsgCode::Ok : CsgCode::UnknownNode;
        if (known) {
          const uint64_t p = m.nodes[pick].parent;
          const size_t at = m.detach(pick);
          m.nodes[fresh] = {op, 0, {}};
          m.attach(fresh, p, uint32_t(at));
          m.attach(pick, fresh, 0);
          fresh++;
          done[2]++;
        }
      }
      else if (kind == 5) {
        got = t.move(id(pick), other ? id(other) : kNoId, index);
        if (!known || !other_known) {
          want = CsgCode::UnknownNode;
        }
        else if (m.nodes[other].op == Op::Leaf || m.below(other, pick)) {
          want = CsgCode::BadTree;
        }
        else {
          m.detach(pick);
          m.attach(pick, other, index);
          done[3]++;
        }
      }
      else if (kind == 6 && r.below(4) == 0) {
        got = t.remove(id(pick));
        want = known ? CsgCode::Ok : CsgCode::UnknownNode;
        if (known) {
          m.detach(pick);
          m.erase(pick);
          done[4]++;
        }
      }
      else if (kind == 6) {
        got = t.set_op(id(pick), op);
        want = !known ? CsgCode::UnknownNode : m.nodes[pick].op == Op::Leaf ? CsgCode::BadTree : CsgCode::Ok;
        if (want == CsgCode::Ok) {
          m.nodes[pick].op = op;
          done[5]++;
        }
      }
      else {
        CsgLeaf l = leaf(id(2000 + r.below(50)), r.below(2));
        l.place = random_placement(r);
        l.rules.append({id(5), LabelKind::Air, id(6)});
        got = t.set_leaf(id(pick), l);
        want = !known ? CsgCode::UnknownNode : m.nodes[pick].op != Op::Leaf ? CsgCode::BadTree : CsgCode::Ok;
        done[6] += want == CsgCode::Ok;
      }
      UT_CHECK(got == want);
      compare(t, m);
      litestl::io::ByteWriter after;
      t.write(after);
      const bool same = before.bytes.size() == after.bytes.size() &&
                        std::equal(before.bytes.begin(), before.bytes.end(), after.bytes.begin());
      UT_CHECK((csg::csg_hash(t) == hash) == same);
      if (got != CsgCode::Ok) {
        refused++;
        UT_CHECK(same);
      }
      if (step % 10 == 0) {
        CsgTree u;
        litestl::io::ByteReader rd(after.bytes.data(), size_t(after.bytes.size()));
        UT_CHECK(u.read(rd) && u == t && csg::csg_hash(u) == csg::csg_hash(t));
        compare(u, m);
        /* a copy is another value */
        CsgTree c = t;
        UT_CHECK(c == t);
        if (!c.empty()) {
          UT_CHECK(c.remove(c.root()) == CsgCode::Ok && c.empty() && !t.empty() && !(c == t));
        }
      }
    }
  }
  for (uint32_t n : done) {
    UT_CHECK(n > 30);
  }
  UT_CHECK(refused > 500);
  /* misuse */
  CsgTree t;
  UT_CHECK(t.add_op(kNoId, kNoId, Op::Union) == CsgCode::BadTree);
  UT_CHECK(t.add_op(id(1), kNoId, Op::Leaf) == CsgCode::BadTree);
  UT_CHECK(t.add_op(id(1), kNoId, Op(7)) == CsgCode::BadTree);
  UT_CHECK(t.add_op(id(1), kNoId, Op::Union) == CsgCode::Ok);
  UT_CHECK(t.wrap(id(1), Op::Union, id(1)) == CsgCode::DuplicateId);
  UT_CHECK(t.wrap(id(2), Op::Leaf, id(1)) == CsgCode::BadTree && t.wrap(kNoId, Op::Union, id(1)) == CsgCode::BadTree);
  UT_CHECK(t.set_op(id(1), Op::Leaf) == CsgCode::BadTree && t.set_op(id(1), Op(7)) == CsgCode::BadTree);
  UT_CHECK(t.wrap(id(2), Op(7), id(1)) == CsgCode::BadTree);
  /* depth */
  for (uint64_t i = 2; i <= CsgTree::kMaxDepth; i++) {
    UT_CHECK(t.add_op(id(i), id(i - 1), Op::Union) == CsgCode::Ok);
  }
  UT_CHECK(t.add_op(id(1000), id(CsgTree::kMaxDepth), Op::Union) == CsgCode::BadTree);
  UT_CHECK(t.wrap(id(1000), Op::Union, id(1)) == CsgCode::BadTree && t.wrap(id(1000), Op::Union, id(9)) == CsgCode::BadTree);
  UT_CHECK(t.add_leaf(id(1001), id(1), leaf(id(5))) == CsgCode::Ok && t.wrap(id(1002), Op::Union, id(1001)) == CsgCode::Ok);
  UT_CHECK(t.move(id(1002), id(CsgTree::kMaxDepth - 2)) == CsgCode::Ok);
  UT_CHECK(t.move(id(1002), id(CsgTree::kMaxDepth - 1)) == CsgCode::BadTree);
}

/* every cut or changed byte of a stream is refused, or gives another tree */
void test_corrupt()
{
  CsgTree t;
  UT_CHECK(t.add_op(id(1), kNoId, Op::Difference) == CsgCode::Ok);
  CsgLeaf l = leaf(id(50), true);
  l.rules.append({id(60), LabelKind::Air, id(61)});
  UT_CHECK(t.add_leaf(id(2), id(1), l) == CsgCode::Ok && t.add_op(id(3), id(1), Op::Union) == CsgCode::Ok);
  UT_CHECK(t.add_leaf(id(4), id(3), leaf(id(51))) == CsgCode::Ok && t.add_leaf(id(5), id(1), l) == CsgCode::Ok);
  litestl::io::ByteWriter w;
  t.write(w);
  const int n = int(w.bytes.size());
  for (int cut = 0; cut < n; cut++) {
    CsgTree u;
    UT_CHECK(u.add_op(id(9), kNoId, Op::Union) == CsgCode::Ok);
    litestl::io::ByteReader rd(w.bytes.data(), size_t(cut));
    UT_CHECK(!u.read(rd) && u.contains(id(9)));
  }
  uint32_t refused = 0;
  for (int i = 0; i < n; i++) {
    for (const uint8_t x : {1, 0x40, 0xff}) {
      auto bytes = w.bytes;
      bytes[i] ^= x;
      CsgTree u;
      litestl::io::ByteReader rd(bytes.data(), size_t(n));
      if (u.read(rd)) {
        /* another tree, and a proper one */
        litestl::io::ByteWriter again;
        u.write(again);
        UT_CHECK(!(u == t) && again.bytes.size() <= n);
        UT_CHECK(std::equal(again.bytes.begin(), again.bytes.end(), bytes.begin()));
        for (const PersistentId &x : u.leaves()) {
          const CsgLeaf &l = *u.leaf(x);
          UT_CHECK(l.place.det() != 0 && l.kind <= LabelKind::Solid);
          for (const csg::LabelRule &rule : l.rules) {
            UT_CHECK(rule.kind <= LabelKind::Solid);
          }
        }
      }
      else {
        refused++;
      }
    }
  }
  UT_CHECK(refused > 100);
  /* operands */
  OperandStore s;
  const PersistentId a = s.add(box({0, 0, 0}, {1, 2, 3})), b = s.add(box({0, 0, 0}, {4, 4, 4}));
  UT_CHECK(a != b && s.add(box({0, 0, 0}, {1, 2, 3})) == a && s.size() == 2);
  litestl::io::ByteWriter sw;
  s.write(sw);
  OperandStore s2;
  litestl::io::ByteReader rd(sw.bytes.data(), size_t(sw.bytes.size()));
  UT_CHECK(s2.read(rd) && s2.size() == 2 && s2.find(a) && s2.find(b) && s2.find(a) != s.find(a));
  UT_CHECK(topo::content_hash(*s2.find(a)) == a && s2.is_valid(a) && !s2.is_valid(id(3)) && !s2.find(id(3)));
  {
    /* the order of adding doesn't matter */
    OperandStore s4;
    UT_CHECK(s4.add(box({0, 0, 0}, {4, 4, 4})) == b && s4.add(box({0, 0, 0}, {1, 2, 3})) == a);
    litestl::io::ByteWriter w4;
    s4.write(w4);
    UT_CHECK(w4.bytes.size() == sw.bytes.size() && std::equal(w4.bytes.begin(), w4.bytes.end(), sw.bytes.begin()));
  }
  /* magic, format, and the hash of the first operand */
  for (const int i : {0, 4, 12, 27}) {
    auto bytes = sw.bytes;
    bytes[i] ^= 1;
    OperandStore s5;
    litestl::io::ByteReader rc(bytes.data(), size_t(bytes.size()));
    UT_CHECK(!s5.read(rc) && s5.size() == 0);
  }
  for (int cut = 0; cut < int(sw.bytes.size()); cut += 97) {
    OperandStore s3;
    litestl::io::ByteReader rc(sw.bytes.data(), size_t(cut));
    UT_CHECK(!s3.read(rc) && s3.size() == 0);
  }
}

/* ---- evaluation ---- */

struct World {
  OperandStore store;
  PersistentId block, window, unit;

  World()
  {
    block = store.add(box({0, 0, 0}, {40, 10, 30}));
    window = store.add(box({0, -2, 0}, {6, 12, 8}));
    unit = store.add(box({0, 0, 0}, {10, 10, 10}));
  }
  CsgLeaf at(PersistentId operand, const I3 &offset, PersistentId name = kNoId) const
  {
    CsgLeaf l = leaf(operand);
    for (int i = 0; i < 3; i++) {
      l.place.offset[i] = offset[i];
    }
    if (name != kNoId) {
      l.rules.append({store.find(operand)->regions.get<".id">()[1], LabelKind::Solid, name});
    }
    return l;
  }
};

std::multiset<std::string> shape(const LComplex &k)
{
  std::multiset<std::string> out;
  k.faces.for_each_live([&](uint32_t f) {
    std::multiset<std::string> loops;
    k.for_each_loop(topo::FaceRef(f), [&](topo::LoopRef l) {
      std::vector<std::string> pts;
      k.for_each_coedge(l, [&](topo::CoedgeRef c) {
        const auto &p = k.pos(k.start_vert(c));
        pts.push_back(std::to_string(p[0].raw) + "," + std::to_string(p[1].raw) + "," + std::to_string(p[2].raw) + " ");
      });
      std::rotate(pts.begin(), std::min_element(pts.begin(), pts.end()), pts.end());
      std::string s;
      for (const auto &x : pts) {
        s += x;
      }
      loops.insert(s);
    });
    std::string s;
    for (const auto &x : loops) {
      s += x + ";";
    }
    out.insert(s);
  });
  return out;
}

void test_evaluate()
{
  World w;
  CsgTree t;
  LComplex k;
  BoolReport rep;
  BoolOptions opt;
  UT_CHECK(csg::evaluate(t, w.store, opt, k, rep) == CsgCode::EmptyTree && rep.code == CsgCode::EmptyTree);
  UT_CHECK(t.add_op(id(1), kNoId, Op::Difference) == CsgCode::Ok);
  /* an operation without children is void */
  UT_CHECK(csg::evaluate(t, w.store, opt, k, rep) == CsgCode::Ok && k.faces.live_count() == 0 &&
           k.regions.live_count() == 1);
  UT_CHECK(t.add_leaf(id(2), id(1), w.at(w.block, {0, 0, 0})) == CsgCode::Ok);
  UT_CHECK(t.add_leaf(id(3), id(1), w.at(w.window, {4, 0, 5})) == CsgCode::Ok);
  UT_CHECK(t.add_leaf(id(4), id(1), w.at(w.window, {20, 0, 5})) == CsgCode::Ok);
  opt.frame.frame_id = 77;
  UT_CHECK(csg::evaluate(t, w.store, opt, k, rep) == CsgCode::Ok);
  UT_CHECK(k.faces.live_count() == 14 && k.regions.live_count() == 2 && k.frame.frame_id == 77);
  UT_CHECK(k.frame.quantum_log2 == opt.frame.quantum_log2 && rep.quantum_shift == 0);
  UT_CHECK(rep.leaf_ids.size() == 3 && rep.leaf_ids[0] == id(2) && rep.leaf_ids[2] == id(4));
  UT_CHECK(rep.labels.size() == 4);
  UT_CHECK(rep.labels[1] == csg::default_label(id(2), w.store.find(w.block)->regions.get<".id">()[1]));
  /* failures change nothing, and name the node */
  const auto before = shape(k);
  CsgTree u = t;
  UT_CHECK(u.add_leaf(id(5), id(1), leaf(id(12345))) == CsgCode::Ok);
  UT_CHECK(csg::evaluate(u, w.store, opt, k, rep) == CsgCode::BadOperand && rep.node == id(5) && shape(k) == before);
  u = t;
  CsgLeaf l = w.at(w.window, {0, 0, 0});
  l.place.axis[0] = 1;
  UT_CHECK(u.set_leaf(id(3), l) == CsgCode::Ok);
  UT_CHECK(csg::evaluate(u, w.store, opt, k, rep) == CsgCode::BadOperand && rep.node == id(3));
  u = t;
  l = w.at(w.window, {int64_t(1) << 28, 0, 0});
  UT_CHECK(u.set_leaf(id(4), l) == CsgCode::Ok && csg::evaluate(u, w.store, opt, k, rep) == CsgCode::OutOfRange &&
           rep.node == kNoId);
  l = w.at(w.window, {int64_t(1) << 41, 0, 0});
  UT_CHECK(u.set_leaf(id(4), l) == CsgCode::Ok && csg::evaluate(u, w.store, opt, k, rep) == CsgCode::OutOfRange &&
           rep.node == id(4));
  opt.frame.quantum_log2 = -10;
  UT_CHECK(csg::evaluate(t, w.store, opt, k, rep) == CsgCode::MixedFrames && rep.node == id(2) && shape(k) == before);
  opt.frame.quantum_log2 = -13;
  opt.placement.r[0][0] = 2;
  UT_CHECK(csg::evaluate(t, w.store, opt, k, rep) == CsgCode::BadPlacement && shape(k) == before);
  opt = {};
  {
    /* an invalid operand: two boxes that overlap */
    GridBuilder<L> g;
    g.box({0, 0, 0}, {4, 4, 4});
    g.box({2, 1, 1}, {6, 3, 3});
    LComplex bad;
    topo::BuildReport br;
    topo::RegionReport rr;
    UT_CHECK(g.b.build(bad, br));
    (void)topo::build_regions(bad, rr);
    bad.regions_dirty = false;
    const PersistentId h = w.store.add(std::move(bad));
    u = t;
    UT_CHECK(u.add_leaf(id(6), id(1), leaf(h)) == CsgCode::Ok);
    UT_CHECK(csg::evaluate(u, w.store, opt, k, rep) == CsgCode::BadOperand && rep.node == id(6));
  }
  {
    /* a finer quantum, if the caller allows it */
    GridBuilder<L> g;
    g.face({{{-1, 9, -1}, {-1, 10, -1}, {-1, 10, 29}}});
    LComplex wedge;
    GridBuilder<L> p;
    const I3 a[3] = {{-1, 10, -1}, {-1, 9, -1}, {-1, 10, 29}};
    I3 b[3];
    for (int i = 0; i < 3; i++) {
      b[i] = a[i];
      b[i][0] = 21;
    }
    /* (over (y, z), along x) */
    p.face({{a[2], a[1], a[0]}});
    p.face({{b[0], b[1], b[2]}});
    for (int i = 0; i < 3; i++) {
      const int j = (i + 1) % 3;
      p.face({{a[i], a[j], b[j], b[i]}});
    }
    topo::BuildReport br;
    topo::RegionReport rr;
    UT_CHECK(p.b.build(wedge, br) && topo::build_regions(wedge, rr));
    const PersistentId wh = w.store.add(std::move(wedge)), cube = w.store.add(box({0, 0, 0}, {20, 20, 20}));
    CsgTree f;
    UT_CHECK(f.add_op(id(1), kNoId, Op::Difference) == CsgCode::Ok && f.add_leaf(id(2), id(1), leaf(cube)) == CsgCode::Ok &&
             f.add_leaf(id(3), id(1), leaf(wh)) == CsgCode::Ok);
    /* (an operand that isn't valid is told apart; this one is) */
    const CsgCode c0 = csg::evaluate(f, w.store, opt, k, rep);
    UT_CHECK(c0 == CsgCode::RoundingFailed && rep.output.round != csg::RoundCode::None && shape(k) == before);
    opt.max_quantum_shift = 3;
    UT_CHECK(csg::evaluate(f, w.store, opt, k, rep) == CsgCode::Ok && rep.quantum_shift > 0);
    UT_CHECK(k.frame.quantum_log2 == -13 - rep.quantum_shift);
    UT_CHECK(topo::validate(k, topo::ValidateLevel::Global).ok());
  }
}

/* a result that isn't valid is refused, unless the caller didn't want to know */
void test_validate()
{
  SplitMix64 r(64);
  uint32_t found = 0;
  for (int s = 0; s < 400 && found < 3; s++) {
    OperandStore store;
    CsgTree t;
    UT_CHECK(t.add_op(id(1), kNoId, Op::Union) == CsgCode::Ok);
    for (int i = 0; i < 5; i++) {
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
      LComplex k;
      topo::BuildReport br;
      topo::RegionReport rr;
      UT_CHECK(g.b.build(k, br) && topo::build_regions(k, rr));
      UT_CHECK(t.add_leaf(id(10 + uint64_t(i)), id(1), leaf(store.add(std::move(k)))) == CsgCode::Ok);
    }
    LComplex k;
    BoolReport rep;
    BoolOptions opt;
    if (csg::evaluate(t, store, opt, k, rep) == CsgCode::Ok || rep.output.round != csg::RoundCode::Invalid) {
      continue;
    }
    found++;
    UT_CHECK(rep.code == CsgCode::RoundingFailed && k.faces.live_count() == 0);
    opt.validate_result = false;
    UT_CHECK(csg::evaluate(t, store, opt, k, rep) == CsgCode::Ok && k.faces.live_count() > 0);
    UT_CHECK(!topo::validate(k, topo::ValidateLevel::Global).ok());
  }
  UT_CHECK(found == 3);
}

void test_labels()
{
  World w;
  const PersistentId region = w.store.find(w.unit)->regions.get<".id">()[1];
  LComplex k;
  BoolReport rep;
  for (int kind = 0; kind < 6; kind++) {
    /* two units side by side */
    CsgTree t;
    UT_CHECK(t.add_op(id(1), kNoId, Op::Union) == CsgCode::Ok);
    CsgLeaf a = w.at(w.unit, {0, 0, 0}), b = w.at(w.unit, {10, 0, 0});
    if (kind == 1) {
      /* one name: they fuse */
      a.rules.append({region, LabelKind::Solid, id(70)});
      b.rules.append({region, LabelKind::Solid, id(70)});
    }
    else if (kind == 2) {
      b.kind = LabelKind::Air;
    }
    else if (kind == 3) {
      b.kind = LabelKind::Air;
      b.thin = true;
    }
    else if (kind == 4) {
      /* a rule beats the default, and a name can be left out */
      b.kind = LabelKind::Air;
      b.rules.append({region, LabelKind::Solid, kNoId});
    }
    else if (kind == 5) {
      b.rules.append({region, LabelKind::Void, kNoId});
      b.thin = true;
    }
    UT_CHECK(t.add_leaf(id(2), id(1), a) == CsgCode::Ok && t.add_leaf(id(3), id(1), b) == CsgCode::Ok);
    UT_CHECK(csg::evaluate(t, w.store, {}, k, rep) == CsgCode::Ok);
    const uint32_t faces[6] = {11, 10, 6, 11, 11, 11}, regions[6] = {3, 2, 2, 3, 3, 3};
    UT_CHECK(k.faces.live_count() == faces[kind] && k.regions.live_count() == regions[kind]);
    UT_CHECK(rep.labels.size() == (kind == 1 || kind == 5 ? 2u : 3u));
  }
  {
    /* two rooms of one leaf have names of their own */
    GridBuilder<L> g;
    g.box({0, 0, 0}, {4, 4, 4});
    g.box({6, 0, 0}, {9, 4, 4});
    LComplex rooms;
    topo::BuildReport br;
    topo::RegionReport rr;
    UT_CHECK(g.b.build(rooms, br) && topo::build_regions(rooms, rr));
    const PersistentId h = w.store.add(std::move(rooms));
    CsgTree t;
    UT_CHECK(t.add_leaf(id(2), kNoId, leaf(h)) == CsgCode::Ok);
    UT_CHECK(csg::evaluate(t, w.store, {}, k, rep) == CsgCode::Ok);
    UT_CHECK(rep.labels.size() == 3 && rep.labels[1] != rep.labels[2] && k.regions.live_count() == 3);
  }
  /* refused: a region there isn't, the unbounded one, its name, two kinds */
  for (int kind = 0; kind < 4; kind++) {
    CsgTree t;
    UT_CHECK(t.add_op(id(1), kNoId, Op::Union) == CsgCode::Ok);
    CsgLeaf a = w.at(w.unit, {0, 0, 0}), b = w.at(w.unit, {10, 0, 0});
    if (kind == 0) {
      b.rules.append({id(71), LabelKind::Solid, id(70)});
    }
    else if (kind == 1) {
      b.rules.append({ident::unbounded_region_id(), LabelKind::Air, id(70)});
    }
    else if (kind == 2) {
      b.rules.append({region, LabelKind::Solid, ident::unbounded_region_id()});
    }
    else {
      a.rules.append({region, LabelKind::Solid, id(70)});
      b.rules.append({region, LabelKind::Air, id(70)});
    }
    UT_CHECK(t.add_leaf(id(2), id(1), a) == CsgCode::Ok && t.add_leaf(id(3), id(1), b) == CsgCode::Ok);
    UT_CHECK(csg::evaluate(t, w.store, {}, k, rep) == CsgCode::BadLabel && rep.node == id(3));
  }
}

void test_keys()
{
  World w;
  CsgTree t;
  UT_CHECK(t.add_op(id(1), kNoId, Op::Union) == CsgCode::Ok && t.add_leaf(id(2), id(1), w.at(w.unit, {0, 0, 0})) == CsgCode::Ok &&
           t.add_leaf(id(3), id(1), w.at(w.block, {1, 2, 3})) == CsgCode::Ok);
  std::set<std::pair<uint64_t, uint64_t>> keys;
  uint32_t n = 0;
  auto note = [&](const CsgTree &x, const BoolOptions &o) {
    const PersistentId k = csg::evaluation_key(x, o);
    keys.insert({k.lo, k.hi});
    n++;
  };
  note(t, {});
  BoolOptions o;
  o.validate_operands = o.validate_result = false; /* (don't matter) */
  UT_CHECK(csg::evaluation_key(t, o) == csg::evaluation_key(t, {}));
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      BoolOptions x;
      x.placement.r[i][j] += 0.5;
      note(t, x);
    }
    BoolOptions x, y;
    x.placement.t[i] = 1;
    y.frame.origin[i] = 1;
    note(t, x);
    note(t, y);
  }
  BoolOptions x, y, z;
  x.frame.quantum_log2 = -12;
  y.frame.frame_id = 5;
  z.max_quantum_shift = 1;
  note(t, x);
  note(t, y);
  note(t, z);
  auto with = [&](auto &&edit) {
    CsgTree u = t;
    UT_CHECK(edit(u) == CsgCode::Ok);
    note(u, {});
  };
  with([&](CsgTree &u) { return u.set_op(id(1), Op::Difference); });
  with([&](CsgTree &u) { return u.move(id(2), id(1), 1); });
  with([&](CsgTree &u) { return u.set_leaf(id(2), w.at(w.window, {0, 0, 0})); });
  with([&](CsgTree &u) { return u.set_leaf(id(2), w.at(w.unit, {0, 0, 1})); });
  with([&](CsgTree &u) { return u.set_leaf(id(2), w.at(w.unit, {0, 0, 0}, id(70))); });
  with([&](CsgTree &u) { return u.set_leaf(id(2), w.at(w.unit, {0, 0, 0}, id(71))); });
  with([&](CsgTree &u) { return u.wrap(id(4), Op::Union, id(1)); });
  with([&](CsgTree &u) { return u.wrap(id(5), Op::Union, id(1)); });
  for (int kind = 0; kind < 4; kind++) {
    CsgLeaf l = w.at(w.unit, {0, 0, 0});
    l.thin = kind == 0;
    l.kind = kind == 1 ? LabelKind::Air : LabelKind::Solid;
    l.place.sign[1] = kind == 2 ? -1 : 1;
    if (kind == 3) {
      std::swap(l.place.axis[0], l.place.axis[2]);
    }
    with([&](CsgTree &u) { return u.set_leaf(id(2), l); });
  }
  UT_CHECK(keys.size() == n);
  /* the same edits in another order: the same tree */
  CsgTree a, b;
  UT_CHECK(a.add_op(id(1), kNoId, Op::Union) == CsgCode::Ok && a.add_leaf(id(2), id(1), w.at(w.unit, {0, 0, 0})) == CsgCode::Ok &&
           a.add_leaf(id(3), id(1), w.at(w.unit, {9, 0, 0})) == CsgCode::Ok);
  UT_CHECK(b.add_op(id(1), kNoId, Op::Union) == CsgCode::Ok && b.add_leaf(id(3), id(1), w.at(w.unit, {9, 0, 0})) == CsgCode::Ok &&
           b.add_leaf(id(2), id(1), w.at(w.unit, {0, 0, 0}), 0) == CsgCode::Ok);
  UT_CHECK(a == b && csg::csg_hash(a) == csg::csg_hash(b));
}

/* (A + B) + C and A + (B + C), (A - B) - C and A - (B + C): the same bytes */
void test_nesting()
{
  SplitMix64 r(62);
  uint32_t faces = 0;
  for (int s = 0; s < 120; s++) {
    Operands o;
    OperandStore store;
    std::vector<CsgLeaf> leaves;
    for (int i = 0; i < 4; i++) {
      add_random(r, 3, o);
      CsgLeaf l;
      LComplex k = *o.k.back();
      l.thin = r.below(2);
      l.operand = store.add(std::move(k));
      leaves.push_back(l);
    }
    const bool minus = s % 2;
    std::vector<PersistentId> hash;
    for (int shape = 0; shape < 4; shape++) {
      CsgTree t;
      CsgCode c = CsgCode::Ok;
      auto add = [&](CsgCode x) { c = c == CsgCode::Ok ? x : c; };
      auto put = [&](int i, uint64_t parent) { add(t.add_leaf(id(100 + uint64_t(i)), id(parent), leaves[size_t(i)])); };
      const Op op = minus ? Op::Difference : Op::Union;
      if (shape == 0) {
        add(t.add_op(id(1), kNoId, op));
        for (int i = 0; i < 4; i++) {
          put(i, 1);
        }
      }
      else if (shape == 1) {
        /* from the left */
        add(t.add_op(id(1), kNoId, op));
        add(t.add_op(id(2), id(1), op));
        add(t.add_op(id(3), id(2), op));
        put(0, 3);
        put(1, 3);
        put(2, 2);
        put(3, 1);
      }
      else if (shape == 2) {
        /* from the right */
        add(t.add_op(id(1), kNoId, op));
        put(0, 1);
        add(t.add_op(id(2), id(1), Op::Union));
        put(1, 2);
        add(t.add_op(id(3), id(2), Op::Union));
        put(2, 3);
        put(3, 3);
      }
      else {
        add(t.add_op(id(1), kNoId, op));
        add(t.add_op(id(2), id(1), op));
        put(0, 2);
        put(1, 2);
        add(t.add_op(id(3), id(1), Op::Union));
        put(2, 3);
        put(3, 3);
      }
      UT_CHECK(c == CsgCode::Ok);
      LComplex k;
      BoolReport rep;
      BoolOptions opt;
      opt.max_quantum_shift = 12;
      UT_CHECK(csg::evaluate(t, store, opt, k, rep) == CsgCode::Ok);
      hash.push_back(topo::content_hash(k));
      faces += k.faces.live_count();
    }
    UT_CHECK(hash[0] == hash[1] && hash[0] == hash[2] && hash[0] == hash[3]);
  }
  UT_CHECK(faces > 2000);
}

/* leaves and regions of a result, against volumes: child order of a union of
 * one name, De Morgan in a box */
void test_sets()
{
  SplitMix64 r(63);
  uint32_t checked = 0;
  for (int s = 0; s < 60; s++) {
    Operands o;
    OperandStore store;
    std::vector<CsgLeaf> leaves;
    for (int i = 0; i < 3; i++) {
      GridBuilder<L> g;
      I3 a, b;
      do {
        a = vertex(r, 3);
        b = vertex(r, 3);
      } while (a[0] == b[0] || a[1] == b[1] || a[2] == b[2]);
      g.box({std::min(a[0], b[0]), std::min(a[1], b[1]), std::min(a[2], b[2])},
            {std::max(a[0], b[0]), std::max(a[1], b[1]), std::max(a[2], b[2])});
      o.add(g);
      CsgLeaf l;
      LComplex k = *o.k.back();
      l.operand = store.add(std::move(k));
      l.rules.append({store.find(l.operand)->regions.get<".id">()[1], LabelKind::Solid, id(70)});
      leaves.push_back(l);
    }
    CsgLeaf all;
    all.operand = store.add(box({-4, -4, -4}, {4, 4, 4}));
    all.rules.append({store.find(all.operand)->regions.get<".id">()[1], LabelKind::Solid, id(70)});
    /* the volume of a result, six times (all points are integers) */
    auto volume = [&](const CsgTree &t) {
      LComplex k;
      BoolReport rep;
      UT_CHECK(csg::evaluate(t, store, {}, k, rep) == CsgCode::Ok);
      double v = 0;
      for (const csg::ResultFace &f : rep.output.result.faces) {
        if ((f.solid[0] != 0) == (f.solid[1] != 0)) {
          continue;
        }
        const csg::I3 &p = rep.output.positions[int(f.pts[0])];
        double sum = 0;
        for (int l = 0; l + 1 < int(f.off.size()); l++) {
          for (uint32_t i = f.off[l]; i < f.off[l + 1]; i++) {
            const csg::I3 &x = rep.output.positions[int(f.pts[int(i)])];
            const csg::I3 &y = rep.output.positions[int(f.pts[int(i + 1 < f.off[l + 1] ? i + 1 : f.off[l])])];
            const D3 c = cross({double(x[0]), double(x[1]), double(x[2])}, {double(y[0]), double(y[1]), double(y[2])});
            sum += dot({double(p[0]), double(p[1]), double(p[2])}, c);
          }
        }
        v += f.solid[1] != 0 ? sum : -sum;
      }
      return v;
    };
    auto tree = [&](Op top, const std::vector<int> &order) {
      CsgTree t;
      CsgCode c = t.add_op(id(1), kNoId, top);
      for (int i : order) {
        c = c == CsgCode::Ok ? t.add_leaf(id(100 + uint64_t(i)), id(1), leaves[size_t(i)]) : c;
      }
      UT_CHECK(c == CsgCode::Ok);
      return t;
    };
    const double u = volume(tree(Op::Union, {0, 1, 2}));
    UT_CHECK(u == volume(tree(Op::Union, {2, 0, 1})) && u > 0);
    const double x = volume(tree(Op::Intersection, {0, 1, 2}));
    UT_CHECK(x == volume(tree(Op::Intersection, {2, 1, 0})));
    /* box - (A + B + C) is (box - A) * (box - B) * (box - C) */
    CsgTree m, n;
    CsgCode c = m.add_op(id(1), kNoId, Op::Difference);
    auto add = [&](CsgCode y) { c = c == CsgCode::Ok ? y : c; };
    add(m.add_leaf(id(50), id(1), all));
    add(m.add_op(id(2), id(1), Op::Union));
    add(n.add_op(id(1), kNoId, Op::Intersection));
    for (int i = 0; i < 3; i++) {
      add(m.add_leaf(id(100 + uint64_t(i)), id(2), leaves[size_t(i)]));
      add(n.add_op(id(10 + uint64_t(i)), id(1), Op::Difference));
      add(n.add_leaf(id(20 + uint64_t(i)), id(10 + uint64_t(i)), all));
      add(n.add_leaf(id(30 + uint64_t(i)), id(10 + uint64_t(i)), leaves[size_t(i)]));
    }
    UT_CHECK(c == CsgCode::Ok);
    const double rest = volume(m);
    UT_CHECK(rest == volume(n) && rest + u == 6 * 512);
    checked++;
  }
  UT_CHECK(checked == 60);
}

} // namespace

int main()
{
  const size_t baseline = litestl::alloc::getMemorySize();
  test_edits();
  test_corrupt();
  test_evaluate();
  test_validate();
  test_labels();
  test_keys();
  test_nesting();
  test_sets();
  UT_CHECK(litestl::alloc::getMemorySize() == baseline);
  return ut_test_end();
}
