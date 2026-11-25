#pragma once

/* Random CSG trees on random operands, and their meaning (plan
 * 2026-11-25-exact-booleans section 2.3) written a second time. */

#include <algorithm>
#include <vector>

#include "csg_scenes.h"
#include "ubertopo/csg/select.h"

namespace ubertopo::test {

struct Tree {
  csg::Op op = csg::Op::Leaf;
  uint32_t leaf = 0;
  std::vector<Tree> kids;
};

/* void: -1, air: -2 - name, solid: name */
struct Meaning {
  std::vector<std::vector<int>> label; /* by leaf and region */
  std::vector<bool> thin;

  int at(const Tree &t, const std::vector<uint32_t> &region) const
  {
    if (t.op == csg::Op::Leaf) {
      return label[t.leaf][region[t.leaf]];
    }
    std::vector<int> v;
    for (const Tree &k : t.kids) {
      v.push_back(at(k, region));
    }
    const int others = int(std::count_if(v.begin() + 1, v.end(), [](int x) { return x >= 0; }));
    switch (t.op) {
      case csg::Op::Union: {
        const auto s = std::find_if(v.begin(), v.end(), [](int x) { return x >= 0; });
        const auto air = std::find_if(v.begin(), v.end(), [](int x) { return x < -1; });
        return s != v.end() ? *s : air != v.end() ? *air : -1;
      }
      case csg::Op::Intersection:
        return others == int(v.size()) - 1 ? v[0] : -1;
      default:
        return others == 0 ? v[0] : -1;
    }
  }
  /* 2: leaf not in t; else whether it is present */
  int present(const Tree &t, uint32_t leaf, const std::vector<uint32_t> &region) const
  {
    if (t.op == csg::Op::Leaf) {
      return t.leaf == leaf ? 1 : 2;
    }
    for (size_t i = 0; i < t.kids.size(); i++) {
      const int p = present(t.kids[i], leaf, region);
      if (p == 2) {
        continue;
      }
      if (p == 0 || (t.op == csg::Op::Difference && i > 0)) {
        return 0;
      }
      for (size_t j = 0; j < t.kids.size(); j++) {
        const bool solid = j != i && at(t.kids[j], region) >= 0;
        if (j != i && solid != (t.op == csg::Op::Intersection)) {
          return 0;
        }
      }
      return 1;
    }
    return 2;
  }
};

/* numbers the leaves of a random tree in order */
inline Tree random_tree(SplitMix64 &r, int depth, uint32_t &leaves)
{
  Tree t;
  if (depth == 0 || (depth < 3 && r.below(4) == 0)) {
    t.leaf = leaves++;
    return t;
  }
  t.op = csg::Op(1 + r.below(3));
  const int n = 1 + int(r.below(3));
  for (int i = 0; i < n; i++) {
    t.kids.push_back(random_tree(r, depth - 1, leaves));
  }
  return t;
}

inline uint32_t add_tree(csg::Expression &e, const Tree &t, const std::vector<std::vector<uint32_t>> &labels,
                  const std::vector<bool> &thin)
{
  if (t.op == csg::Op::Leaf) {
    return e.add_leaf(thin[t.leaf], labels[t.leaf]);
  }
  std::vector<uint32_t> kids;
  for (const Tree &k : t.kids) {
    kids.push_back(add_tree(e, k, labels, thin));
  }
  return e.add_op(t.op, kids);
}

/** Labels: two of air, three solid. */
struct RandomCase {
  Operands o;
  csg::Expression e;
  Meaning m;
  Tree t;
  uint32_t n = 0; /* leaves */
  uint32_t air[2], solid[3];

  /* false: too large */
  bool make(SplitMix64 &r)
  {
    t = random_tree(r, 1 + int(r.below(3)), n);
    if (n > 7) {
      return false;
    }
    for (uint32_t i = 0; i < n; i++) {
      if (r.below(3) == 0) {
        /* big ones, so that they overlap */
        GridBuilder<L> g;
        const I3 a = vertex(r, 1);
        g.box({a[0] - 3, a[1] - 3, a[2] - 3}, {a[0] + 2, a[1] + 2, a[2] + 2});
        o.add(g);
      }
      else {
        add_random(r, 3, o);
      }
    }
    for (uint32_t &x : air) {
      x = e.add_label(csg::LabelKind::Air);
    }
    for (uint32_t &x : solid) {
      x = e.add_label(csg::LabelKind::Solid);
    }
    std::vector<std::vector<uint32_t>> labels;
    for (uint32_t i = 0; i < n; i++) {
      labels.push_back({csg::kVoid});
      m.label.push_back({-1});
      m.thin.push_back(r.below(2) == 0);
      for (uint32_t q = 1; q < o.k[i]->regions.capacity(); q++) {
        const uint64_t c = r.below(8);
        labels.back().push_back(c == 0 ? csg::kVoid : c < 3 ? air[c - 1] : solid[c % 3]);
        m.label.back().push_back(c == 0 ? -1 : c < 3 ? -2 - int(c - 1) : int(c % 3));
      }
    }
    const uint32_t root = add_tree(e, t, labels, m.thin);
    UT_CHECK(root == e.root);
    return true;
  }
};

} // namespace ubertopo::test
