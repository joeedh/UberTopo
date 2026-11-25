#pragma once

/* The meaning of a CSG expression, and the facets that its result keeps (plan
 * 2026-11-25-exact-booleans §2.3, §4.5). */

#include "ubertopo/csg/arrangement.h"

namespace ubertopo::csg {

enum class Op : uint8_t { Leaf, Union, Intersection, Difference };
enum class LabelKind : uint8_t { Void, Air, Solid };

/** The void label. Labels are indices into Expression::kinds. */
inline constexpr uint32_t kVoid = 0;

struct ExprNode {
  Op op = Op::Leaf;
  uint32_t leaf = kNone; /* of Op::Leaf; leaves are numbered in tree order */
  uint32_t parent = kNone;
  litestl::util::Vector<uint32_t, 4> children;
};

struct ExprLeaf {
  uint32_t node = kNone;
  bool thin = false;                       /* contributes its thin content */
  litestl::util::Vector<uint32_t, 4> label; /* by region */
};

/** A label, and the leaf that it comes from. */
struct Labelled {
  uint32_t label = kVoid;
  uint32_t leaf = kNone;
};

struct Expression {
  litestl::util::Vector<ExprNode> nodes;
  litestl::util::Vector<ExprLeaf> leaves;
  litestl::util::Vector<LabelKind> kinds; /* kinds[0] is Void */
  uint32_t root = kNone;

  uint32_t add_label(LabelKind k);
  /** Children first; the last one is the root. */
  uint32_t add_leaf(bool thin, std::span<const uint32_t> labels);
  uint32_t add_op(Op op, std::span<const uint32_t> children);

  bool solid(uint32_t label) const
  {
    return kinds[int(label)] == LabelKind::Solid;
  }
  /** The label if it is a solid one, else kVoid. */
  uint32_t solid_part(uint32_t label) const
  {
    return solid(label) ? label : kVoid;
  }

  /** The label of a subtree where leaf l is in region region(l). */
  template <typename RegionOf> Labelled eval(uint32_t node, RegionOf &&region) const
  {
    const ExprNode &n = nodes[int(node)];
    if (n.op == Op::Leaf) {
      const uint32_t l = leaves[int(n.leaf)].label[int(region(n.leaf))];
      return {l, n.leaf};
    }
    Labelled out;
    if (n.op == Op::Union) {
      for (uint32_t c : n.children) {
        const Labelled x = eval(c, region);
        if (solid(x.label)) {
          return x;
        }
        out = out.label == kVoid ? x : out;
      }
      return out;
    }
    for (int i = 0; i < int(n.children.size()); i++) {
      const Labelled x = eval(n.children[i], region);
      if (i == 0) {
        out = x;
      }
      else if (solid(x.label) != (n.op == Op::Intersection)) {
        return {};
      }
    }
    return out;
  }

  /** Whether a leaf's thin content counts in a cell. */
  template <typename RegionOf> bool present(uint32_t leaf, RegionOf &&region) const
  {
    uint32_t at = leaves[int(leaf)].node;
    for (uint32_t p = nodes[int(at)].parent; p != kNone; at = p, p = nodes[int(p)].parent) {
      const ExprNode &n = nodes[int(p)];
      if (n.op == Op::Difference && n.children[0] != at) {
        return false;
      }
      for (uint32_t c : n.children) {
        if (c != at && solid(eval(c, region).label) != (n.op == Op::Intersection)) {
          return false;
        }
      }
    }
    return true;
  }

  /** Whether a polygon with these regions is thin content of its leaf. */
  bool thin_content(uint32_t leaf, uint32_t front, uint32_t back) const
  {
    const ExprLeaf &l = leaves[int(leaf)];
    return l.thin && (front == back || (!solid(l.label[int(front)]) && !solid(l.label[int(back)])));
  }
};

struct Selected {
  bool kept = false;
  uint32_t label[2] = {kVoid, kVoid}; /* on each side, air included */
  uint32_t source = kNone;            /* polygon, if kept */
};

/** One entry per facet. */
void select_facets(const Arrangement &a, const Expression &e, litestl::util::Vector<Selected> &out);

} // namespace ubertopo::csg
