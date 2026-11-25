#pragma once

/* The CSG tree: what is stored and edited (plan 2026-11-25-exact-booleans §2).
 * A solid is always derived from its tree, by evaluate(). */

#include <cstdint>
#include <span>

#include "litestl/io/bytes.h"
#include "litestl/util/vector.h"
#include "ubertopo/csg/input.h"
#include "ubertopo/csg/names.h"
#include "ubertopo/ident/id.h"

namespace ubertopo::csg {

using ident::PersistentId;

/** Operands: immutable complexes, by content hash. Trees refer to them, and
 * any number of leaves can share one. */
class OperandStore {
public:
  OperandStore() = default;
  ~OperandStore();
  OperandStore(const OperandStore &) = delete;
  OperandStore &operator=(const OperandStore &) = delete;

  /** Takes @p k; its content hash. */
  PersistentId add(topo::Complex<geom::Lattice> &&k);
  const topo::Complex<geom::Lattice> *find(PersistentId hash) const;
  uint32_t size() const
  {
    return uint32_t(entries_.size());
  }
  /** validate(Global), once per operand. */
  bool is_valid(PersistentId hash) const;

  void write(litestl::io::ByteWriter &w) const;
  /** Adds what was written; false (and nothing added) if it is corrupt. */
  bool read(litestl::io::ByteReader &r);

private:
  struct Entry {
    PersistentId hash;
    topo::Complex<geom::Lattice> *k;
    mutable int8_t valid; /* -1: not checked */
  };
  litestl::util::Vector<Entry> entries_;
  const Entry *entry(PersistentId hash) const;
};

/** What a region of a leaf is. */
struct LabelRule {
  PersistentId region; /* in the operand */
  LabelKind kind = LabelKind::Solid;
  PersistentId name; /* kNoId: the region's own */
};

struct CsgLeaf {
  PersistentId operand; /* in the OperandStore */
  GridPlacement place;
  LabelKind kind = LabelKind::Solid; /* of bounded regions without a rule */
  bool thin = false;                 /* contributes its one-face walls and sheets */
  litestl::util::Vector<LabelRule> rules;
};

/** The label name of a region that isn't given one. */
PersistentId default_label(PersistentId leaf, PersistentId region);

/** A value. Nodes have IDs, given by the caller ("window 3 of floor 2"), which
 * the elements of results are named by. Edits are refused with a code and
 * change nothing then. */
class CsgTree {
public:
  static constexpr uint32_t kEnd = 0xffffffffu;
  static constexpr uint32_t kMaxDepth = 256;

  /** @p parent: kNoId for the root of an empty tree. @p index: among the
   * children; kEnd for the last. */
  CsgCode add_leaf(PersistentId id, PersistentId parent, const CsgLeaf &leaf, uint32_t index = kEnd);
  CsgCode add_op(PersistentId id, PersistentId parent, Op op, uint32_t index = kEnd);
  /** Puts an operation above @p child (the root too), in its place. */
  CsgCode wrap(PersistentId id, Op op, PersistentId child);
  CsgCode set_leaf(PersistentId id, const CsgLeaf &leaf);
  CsgCode set_op(PersistentId id, Op op);
  /** Moves a node and what is below it. */
  CsgCode move(PersistentId id, PersistentId parent, uint32_t index = kEnd);
  /** Removes a node and what is below it. */
  CsgCode remove(PersistentId id);

  bool empty() const
  {
    return nodes_.size() == 0;
  }
  uint32_t size() const
  {
    return uint32_t(nodes_.size());
  }
  bool contains(PersistentId id) const
  {
    return find(id) != kNone;
  }
  PersistentId root() const;
  /** kNoId for the root. */
  PersistentId parent(PersistentId id) const;
  /** Null if it is no leaf. */
  const CsgLeaf *leaf(PersistentId id) const;
  /** Op::Leaf for a leaf. */
  Op op(PersistentId id) const;
  litestl::util::Vector<PersistentId> children(PersistentId id) const;
  /** The leaves in tree order. */
  litestl::util::Vector<PersistentId> leaves() const;

  void write(litestl::io::ByteWriter &w) const;
  /** Replaces the tree; false (unchanged) if the bytes are corrupt. */
  bool read(litestl::io::ByteReader &r);
  bool operator==(const CsgTree &b) const;

private:
  struct Node {
    PersistentId id;
    Op op = Op::Leaf;
    uint32_t parent = kNone;
    litestl::util::Vector<uint32_t, 4> children;
    CsgLeaf leaf;
  };
  litestl::util::Vector<Node> nodes_; /* in no order */
  uint32_t root_ = kNone;

  uint32_t find(PersistentId id) const;
  uint32_t depth(uint32_t node) const;
  uint32_t height(uint32_t node) const;
  bool below(uint32_t node, uint32_t top) const;
  void insert_child(uint32_t parent, uint32_t child, uint32_t index);
  uint32_t drop_child(uint32_t child);
  CsgCode add(Node &&n, PersistentId parent, uint32_t index);
  template <typename F> void preorder(uint32_t node, F &&f) const
  {
    f(node);
    for (uint32_t c : nodes_[int(node)].children) {
      preorder(c, f);
    }
  }
  friend PersistentId csg_hash(const CsgTree &);
};

/** The tree as a value: IDs, operations, leaves, the operands' hashes. Node
 * order within the object doesn't matter. */
PersistentId csg_hash(const CsgTree &t);

/** A tree ready for evaluation. */
struct Compiled {
  Expression expr;
  litestl::util::Vector<Operand> operands;       /* by leaf */
  litestl::util::Vector<PersistentId> leaf_ids;  /* by leaf */
  litestl::util::Vector<PersistentId> labels;    /* names, by label; kNoId for void */
  litestl::util::Vector<LabelSource> label_source; /* the first region with the label */

  NameSource names() const
  {
    return {{operands.data(), size_t(operands.size())},
            {leaf_ids.data(), size_t(leaf_ids.size())},
            {labels.data(), size_t(labels.size())},
            {label_source.data(), size_t(label_source.size())}};
  }
};

/** *node: where a failure is. */
CsgCode compile(const CsgTree &t, const OperandStore &store, Compiled &out, PersistentId *node = nullptr);

} // namespace ubertopo::csg
