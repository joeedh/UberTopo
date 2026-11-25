#include "ubertopo/csg/tree.h"

#include <algorithm>
#include <utility>

#include "litestl/util/alloc.h"
#include "litestl/util/map.h"
#include "ubertopo/topo/io.h"
#include "ubertopo/topo/validate.h"

namespace ubertopo::csg {

using geom::Lattice;
using ident::kNoId;
using litestl::util::Vector;
using LComplex = topo::Complex<Lattice>;

namespace {
constexpr uint32_t kStoreMagic = 0x4f435455u; /* "UTCO" */
constexpr uint32_t kTreeMagic = 0x54435455u;  /* "UTCT" */
constexpr uint32_t kFormat = 1;

void put(litestl::io::ByteWriter &w, PersistentId id)
{
  w.u64(id.lo);
  w.u64(id.hi);
}
PersistentId get(litestl::io::ByteReader &r)
{
  PersistentId id;
  id.lo = r.u64();
  id.hi = r.u64();
  return id;
}
} // namespace

/* ---- operands ---- */

OperandStore::~OperandStore()
{
  for (Entry &e : entries_) {
    litestl::alloc::Delete(e.k);
  }
}

const OperandStore::Entry *OperandStore::entry(PersistentId hash) const
{
  for (const Entry &e : entries_) {
    if (e.hash == hash) {
      return &e;
    }
  }
  return nullptr;
}

PersistentId OperandStore::add(LComplex &&k)
{
  const PersistentId hash = topo::content_hash(k);
  if (!entry(hash)) {
    entries_.append({hash, litestl::alloc::New<LComplex>("csg operand", std::move(k)), -1});
  }
  return hash;
}

const LComplex *OperandStore::find(PersistentId hash) const
{
  const Entry *e = entry(hash);
  return e ? e->k : nullptr;
}

bool OperandStore::is_valid(PersistentId hash) const
{
  const Entry *e = entry(hash);
  if (!e) {
    return false;
  }
  if (e->valid < 0) {
    e->valid = topo::validate(*e->k, topo::ValidateLevel::Global).ok();
  }
  return e->valid != 0;
}

void OperandStore::write(litestl::io::ByteWriter &w) const
{
  w.u32(kStoreMagic);
  w.u32(kFormat);
  Vector<const Entry *> order;
  for (const Entry &e : entries_) {
    order.append(&e);
  }
  std::sort(order.begin(), order.end(), [](const Entry *a, const Entry *b) { return a->hash < b->hash; });
  w.u32(uint32_t(order.size()));
  for (const Entry *e : order) {
    put(w, e->hash);
    topo::write(*e->k, w);
  }
}

bool OperandStore::read(litestl::io::ByteReader &r)
{
  if (r.u32() != kStoreMagic || r.u32() != kFormat) {
    return false;
  }
  const uint32_t n = r.u32();
  Vector<LComplex *> loaded;
  bool ok = !r.failed();
  for (uint32_t i = 0; i < n && ok; i++) {
    const PersistentId hash = get(r);
    LComplex *k = litestl::alloc::New<LComplex>("csg operand");
    loaded.append(k);
    ok = topo::read(*k, r) && topo::content_hash(*k) == hash;
  }
  for (LComplex *k : loaded) {
    if (ok) {
      add(std::move(*k));
    }
    litestl::alloc::Delete(k);
  }
  return ok;
}

/* ---- tree ---- */

PersistentId default_label(PersistentId leaf, PersistentId region)
{
  return ident::derive(ident::tag::BoolLabel, leaf, region);
}

uint32_t CsgTree::find(PersistentId id) const
{
  for (uint32_t i = 0; i < nodes_.size(); i++) {
    if (nodes_[int(i)].id == id) {
      return i;
    }
  }
  return kNone;
}

/* the root has depth 1 */
uint32_t CsgTree::depth(uint32_t node) const
{
  uint32_t n = 0;
  for (; node != kNone; node = nodes_[int(node)].parent) {
    n++;
  }
  return n;
}

/* a leaf has height 1 */
uint32_t CsgTree::height(uint32_t node) const
{
  uint32_t n = 0;
  for (uint32_t c : nodes_[int(node)].children) {
    n = std::max(n, height(c));
  }
  return n + 1;
}

bool CsgTree::below(uint32_t node, uint32_t top) const
{
  for (; node != kNone; node = nodes_[int(node)].parent) {
    if (node == top) {
      return true;
    }
  }
  return false;
}

void CsgTree::insert_child(uint32_t parent, uint32_t child, uint32_t index)
{
  nodes_[int(child)].parent = parent;
  if (parent == kNone) {
    root_ = child;
    return;
  }
  auto &c = nodes_[int(parent)].children;
  c.append(child);
  const uint32_t at = std::min(index, uint32_t(c.size()) - 1);
  for (uint32_t i = uint32_t(c.size()) - 1; i > at; i--) {
    c[int(i)] = c[int(i) - 1];
  }
  c[int(at)] = child;
}

/* takes a node off its parent; the index it had */
uint32_t CsgTree::drop_child(uint32_t child)
{
  const uint32_t parent = nodes_[int(child)].parent;
  nodes_[int(child)].parent = kNone;
  if (parent == kNone) {
    root_ = kNone;
    return 0;
  }
  auto &c = nodes_[int(parent)].children;
  int at = 0;
  while (c[at] != child) {
    at++;
  }
  for (int i = at; i + 1 < int(c.size()); i++) {
    c[i] = c[i + 1];
  }
  c.resize(c.size() - 1);
  return uint32_t(at);
}

CsgCode CsgTree::add(Node &&n, PersistentId parent, uint32_t index)
{
  if (n.id == kNoId || n.op > Op::Difference) {
    return CsgCode::BadTree;
  }
  if (find(n.id) != kNone) {
    return CsgCode::DuplicateId;
  }
  uint32_t p = kNone;
  if (parent == kNoId) {
    if (root_ != kNone) {
      return CsgCode::BadTree;
    }
  }
  else {
    p = find(parent);
    if (p == kNone) {
      return CsgCode::UnknownNode;
    }
    if (nodes_[int(p)].op == Op::Leaf || depth(p) >= kMaxDepth) {
      return CsgCode::BadTree;
    }
  }
  nodes_.append(std::move(n));
  insert_child(p, uint32_t(nodes_.size()) - 1, index);
  return CsgCode::Ok;
}

CsgCode CsgTree::add_leaf(PersistentId id, PersistentId parent, const CsgLeaf &leaf, uint32_t index)
{
  Node n;
  n.id = id;
  n.leaf = leaf;
  return add(std::move(n), parent, index);
}

CsgCode CsgTree::add_op(PersistentId id, PersistentId parent, Op op, uint32_t index)
{
  if (op == Op::Leaf) {
    return CsgCode::BadTree;
  }
  Node n;
  n.id = id;
  n.op = op;
  return add(std::move(n), parent, index);
}

CsgCode CsgTree::wrap(PersistentId id, Op op, PersistentId child)
{
  const uint32_t c = find(child);
  if (c == kNone) {
    return CsgCode::UnknownNode;
  }
  if (id == kNoId || op == Op::Leaf || op > Op::Difference || depth(c) + height(c) > kMaxDepth) {
    return CsgCode::BadTree;
  }
  if (find(id) != kNone) {
    return CsgCode::DuplicateId;
  }
  const uint32_t parent = nodes_[int(c)].parent;
  const uint32_t at = drop_child(c);
  Node n;
  n.id = id;
  n.op = op;
  nodes_.append(std::move(n));
  const uint32_t w = uint32_t(nodes_.size()) - 1;
  insert_child(parent, w, at);
  insert_child(w, c, 0);
  return CsgCode::Ok;
}

CsgCode CsgTree::set_leaf(PersistentId id, const CsgLeaf &leaf)
{
  const uint32_t n = find(id);
  if (n == kNone) {
    return CsgCode::UnknownNode;
  }
  if (nodes_[int(n)].op != Op::Leaf) {
    return CsgCode::BadTree;
  }
  nodes_[int(n)].leaf = leaf;
  return CsgCode::Ok;
}

CsgCode CsgTree::set_op(PersistentId id, Op op)
{
  const uint32_t n = find(id);
  if (n == kNone) {
    return CsgCode::UnknownNode;
  }
  if (nodes_[int(n)].op == Op::Leaf || op == Op::Leaf || op > Op::Difference) {
    return CsgCode::BadTree;
  }
  nodes_[int(n)].op = op;
  return CsgCode::Ok;
}

CsgCode CsgTree::move(PersistentId id, PersistentId parent, uint32_t index)
{
  const uint32_t n = find(id), p = find(parent);
  if (n == kNone || p == kNone) {
    return CsgCode::UnknownNode;
  }
  if (nodes_[int(p)].op == Op::Leaf || below(p, n) || depth(p) + height(n) > kMaxDepth) {
    return CsgCode::BadTree;
  }
  drop_child(n);
  insert_child(p, n, index);
  return CsgCode::Ok;
}

CsgCode CsgTree::remove(PersistentId id)
{
  const uint32_t n = find(id);
  if (n == kNone) {
    return CsgCode::UnknownNode;
  }
  drop_child(n);
  Vector<uint32_t> map;
  for (uint32_t i = 0; i < nodes_.size(); i++) {
    map.append(0);
  }
  preorder(n, [&](uint32_t x) { map[int(x)] = kNone; });
  uint32_t kept = 0;
  for (uint32_t i = 0; i < nodes_.size(); i++) {
    if (map[int(i)] != kNone) {
      map[int(i)] = kept;
      if (kept != i) {
        nodes_[int(kept)] = std::move(nodes_[int(i)]);
      }
      kept++;
    }
  }
  nodes_.resize(int(kept));
  for (Node &x : nodes_) {
    x.parent = x.parent == kNone ? kNone : map[int(x.parent)];
    for (uint32_t &c : x.children) {
      c = map[int(c)];
    }
  }
  root_ = root_ == kNone ? kNone : map[int(root_)];
  return CsgCode::Ok;
}

PersistentId CsgTree::root() const
{
  return root_ == kNone ? kNoId : nodes_[int(root_)].id;
}

PersistentId CsgTree::parent(PersistentId id) const
{
  const uint32_t n = find(id);
  const uint32_t p = n == kNone ? kNone : nodes_[int(n)].parent;
  return p == kNone ? kNoId : nodes_[int(p)].id;
}

const CsgLeaf *CsgTree::leaf(PersistentId id) const
{
  const uint32_t n = find(id);
  return n == kNone || nodes_[int(n)].op != Op::Leaf ? nullptr : &nodes_[int(n)].leaf;
}

Op CsgTree::op(PersistentId id) const
{
  const uint32_t n = find(id);
  return n == kNone ? Op::Leaf : nodes_[int(n)].op;
}

Vector<PersistentId> CsgTree::children(PersistentId id) const
{
  Vector<PersistentId> out;
  const uint32_t n = find(id);
  if (n != kNone) {
    for (uint32_t c : nodes_[int(n)].children) {
      out.append(nodes_[int(c)].id);
    }
  }
  return out;
}

Vector<PersistentId> CsgTree::leaves() const
{
  Vector<PersistentId> out;
  if (root_ != kNone) {
    preorder(root_, [&](uint32_t n) {
      if (nodes_[int(n)].op == Op::Leaf) {
        out.append(nodes_[int(n)].id);
      }
    });
  }
  return out;
}

/* ---- bytes ---- */

void CsgTree::write(litestl::io::ByteWriter &w) const
{
  w.u32(kTreeMagic);
  w.u32(kFormat);
  w.u32(uint32_t(nodes_.size()));
  if (root_ == kNone) {
    return;
  }
  preorder(root_, [&](uint32_t i) {
    const Node &n = nodes_[int(i)];
    put(w, n.id);
    w.u8(uint8_t(n.op));
    w.u32(uint32_t(n.children.size()));
    if (n.op != Op::Leaf) {
      return;
    }
    put(w, n.leaf.operand);
    for (int k = 0; k < 3; k++) {
      w.u8(uint8_t(n.leaf.place.axis[k]));
      w.u8(uint8_t(n.leaf.place.sign[k]));
      w.i64(n.leaf.place.offset[k]);
    }
    w.u8(uint8_t(n.leaf.kind));
    w.u8(n.leaf.thin);
    w.u32(uint32_t(n.leaf.rules.size()));
    for (const LabelRule &l : n.leaf.rules) {
      put(w, l.region);
      w.u8(uint8_t(l.kind));
      put(w, l.name);
    }
  });
}

bool CsgTree::read(litestl::io::ByteReader &r)
{
  if (r.u32() != kTreeMagic || r.u32() != kFormat) {
    return false;
  }
  const uint32_t n = r.u32();
  CsgTree t;
  struct Open {
    PersistentId id;
    uint32_t left;
  };
  Vector<Open> open;
  for (uint32_t i = 0; i < n; i++) {
    const PersistentId id = get(r);
    const uint8_t op = r.u8();
    const uint32_t children = r.u32();
    /* the parent: the last node that lacks children */
    while (open.size() > 0 && open[int(open.size()) - 1].left == 0) {
      open.resize(open.size() - 1);
    }
    /* (add_leaf and add_op refuse the rest: an unknown operation, a second root,
     * a repeated ID) */
    if (r.failed()) {
      return false;
    }
    PersistentId parent = kNoId;
    if (open.size() > 0) {
      Open &o = open[int(open.size()) - 1];
      o.left--;
      parent = o.id;
    }
    CsgCode c;
    if (Op(op) == Op::Leaf) {
      CsgLeaf l;
      l.operand = get(r);
      for (int k = 0; k < 3; k++) {
        l.place.axis[k] = int8_t(r.u8());
        l.place.sign[k] = int8_t(r.u8());
        l.place.offset[k] = r.i64();
      }
      const uint8_t kind = r.u8(), thin = r.u8();
      const uint32_t rules = r.u32();
      if (r.failed() || kind > uint8_t(LabelKind::Solid) || thin > 1 || children != 0 || l.place.det() == 0)
      {
        return false;
      }
      l.kind = LabelKind(kind);
      l.thin = thin != 0;
      for (uint32_t k = 0; k < rules; k++) {
        LabelRule x;
        x.region = get(r);
        const uint8_t rk = r.u8();
        x.name = get(r);
        if (r.failed() || rk > uint8_t(LabelKind::Solid)) {
          return false;
        }
        x.kind = LabelKind(rk);
        l.rules.append(x);
      }
      c = t.add_leaf(id, parent, l);
    }
    else {
      c = t.add_op(id, parent, Op(op));
      open.append({id, children});
    }
    if (c != CsgCode::Ok) {
      return false;
    }
  }
  for (const Open &o : open) {
    if (o.left != 0) {
      return false;
    }
  }
  if (r.failed()) {
    return false;
  }
  *this = std::move(t);
  return true;
}

bool CsgTree::operator==(const CsgTree &b) const
{
  litestl::io::ByteWriter x, y;
  write(x);
  b.write(y);
  return x.bytes.size() == y.bytes.size() && std::equal(x.bytes.begin(), x.bytes.end(), y.bytes.begin());
}

PersistentId csg_hash(const CsgTree &t)
{
  litestl::io::ByteWriter w;
  t.write(w);
  ident::Hasher128 h(ident::tag::BoolTree);
  h.raw_bytes(w.bytes.data(), size_t(w.bytes.size()));
  return h.finish();
}

/* ---- compile ---- */

CsgCode compile(const CsgTree &t, const OperandStore &store, Compiled &out, PersistentId *node)
{
  if (t.empty()) {
    return CsgCode::EmptyTree;
  }
  auto fail = [&](CsgCode c, PersistentId id) {
    if (node) {
      *node = id;
    }
    return c;
  };
  out.expr.add_label(LabelKind::Void);
  out.labels.append(kNoId);
  out.label_source.append({kNone, kNone});
  litestl::util::Map<PersistentId, uint32_t> label_of;
  PersistentId bad = kNoId;
  auto build = [&](auto &&self, PersistentId id, uint32_t &result) -> CsgCode {
    const CsgLeaf *l = t.leaf(id);
    if (!l) {
      Vector<uint32_t> kids;
      for (PersistentId c : t.children(id)) {
        uint32_t k = 0;
        const CsgCode code = self(self, c, k);
        if (code != CsgCode::Ok) {
          return code;
        }
        kids.append(k);
      }
      result = out.expr.add_op(t.op(id), {kids.data(), size_t(kids.size())});
      return CsgCode::Ok;
    }
    bad = id;
    const LComplex *k = store.find(l->operand);
    if (!k) {
      return CsgCode::BadOperand;
    }
    const uint32_t leaf = uint32_t(out.operands.size());
    Vector<uint32_t> labels;
    for (uint32_t r = 0; r < k->regions.capacity(); r++) {
      labels.append(kVoid);
    }
    for (const LabelRule &x : l->rules) {
      bool found = false;
      k->regions.for_each_live([&](uint32_t r) { found = found || (r != 0 && k->regions.get<".id">()[r] == x.region); });
      if (!found || x.name == ident::unbounded_region_id()) {
        return CsgCode::BadLabel;
      }
    }
    CsgCode code = CsgCode::Ok;
    k->regions.for_each_live([&](uint32_t r) {
      if (r == 0 || code != CsgCode::Ok) {
        return;
      }
      const PersistentId region = k->regions.get<".id">()[r];
      LabelKind kind = l->kind;
      PersistentId name = kNoId;
      for (const LabelRule &x : l->rules) {
        if (x.region == region) {
          kind = x.kind;
          name = x.name;
        }
      }
      if (kind == LabelKind::Void) {
        return;
      }
      name = name == kNoId ? default_label(id, region) : name;
      const uint32_t *known = label_of.lookup_ptr(name);
      if (known) {
        labels[int(r)] = *known;
        if (out.expr.kinds[int(*known)] != kind) {
          code = CsgCode::BadLabel; /* one name, solid and air */
        }
        return;
      }
      labels[int(r)] = out.expr.add_label(kind);
      label_of.add(name, labels[int(r)]);
      out.labels.append(name);
      out.label_source.append({leaf, r});
    });
    if (code != CsgCode::Ok) {
      return code;
    }
    out.operands.append({k, l->place});
    out.leaf_ids.append(id);
    result = out.expr.add_leaf(l->thin, {labels.data(), size_t(labels.size())});
    return CsgCode::Ok;
  };
  uint32_t root = 0;
  const CsgCode code = build(build, t.root(), root);
  return code == CsgCode::Ok ? code : fail(code, bad);
}

} // namespace ubertopo::csg
