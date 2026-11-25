#include "ubertopo/csg/select.h"

#include <algorithm>

namespace ubertopo::csg {

uint32_t Expression::add_label(LabelKind k)
{
  if (kinds.size() == 0) {
    kinds.append(LabelKind::Void);
  }
  if (k == LabelKind::Void) {
    return kVoid;
  }
  kinds.append(k);
  return uint32_t(kinds.size()) - 1;
}

uint32_t Expression::add_leaf(bool thin, std::span<const uint32_t> labels)
{
  const uint32_t node = uint32_t(nodes.size());
  ExprLeaf l;
  l.node = node;
  l.thin = thin;
  for (uint32_t x : labels) {
    l.label.append(x);
  }
  ExprNode n;
  n.leaf = uint32_t(leaves.size());
  leaves.append(std::move(l));
  nodes.append(std::move(n));
  root = node;
  return node;
}

uint32_t Expression::add_op(Op op, std::span<const uint32_t> children)
{
  const uint32_t node = uint32_t(nodes.size());
  ExprNode n;
  n.op = op;
  for (uint32_t c : children) {
    n.children.append(c);
    nodes[int(c)].parent = node;
  }
  nodes.append(std::move(n));
  root = node;
  return node;
}

void select_facets(const Arrangement &a, const Expression &e, litestl::util::Vector<Selected> &out)
{
  out.clear();
  for (uint32_t f = 0; f < a.facets.size(); f++) {
    const Facet &fc = a.facets[int(f)];
    Selected s;
    auto on = [&](uint32_t side) { return [&a, f, side](uint32_t leaf) { return a.region(f, leaf, side); }; };
    Labelled r[2];
    for (uint32_t side = 0; side < 2; side++) {
      r[side] = e.eval(e.root, on(side));
      s.label[side] = r[side].label;
    }
    /* (polygon, leaf) in tree order */
    auto leaf_of = [&](uint32_t p) { return a.in.polygons[int(p)].leaf; };
    auto first = [&](auto &&good) {
      uint32_t best = kNone;
      for (uint32_t p : fc.sources) {
        if ((best == kNone || leaf_of(p) < leaf_of(best)) && good(leaf_of(p))) {
          best = p;
        }
      }
      return best;
    };
    const uint32_t part[2] = {e.solid_part(r[0].label), e.solid_part(r[1].label)};
    if (part[0] != part[1]) {
      s.kept = true;
      s.source = first([&](uint32_t l) {
        return (part[0] != kVoid && r[0].leaf == l) || (part[1] != kVoid && r[1].leaf == l);
      });
      if (s.source == kNone) {
        /* a leaf that makes the difference */
        s.source = first([&](uint32_t l) {
          for (uint32_t side = 0; side < 2; side++) {
            const Labelled x = e.eval(
                e.root, [&](uint32_t leaf) { return a.region(f, leaf, leaf == l ? 1 - side : side); });
            if (e.solid_part(x.label) != part[side]) {
              return true;
            }
          }
          return false;
        });
      }
      if (s.source == kNone) {
        s.source = first([](uint32_t) { return true; });
      }
    }
    else {
      s.source = first([&](uint32_t l) {
        return e.thin_content(l, a.region(f, l, 0), a.region(f, l, 1)) && (e.present(l, on(0)) || e.present(l, on(1)));
      });
      s.kept = s.source != kNone;
    }
    out.append(s);
  }
}

} // namespace ubertopo::csg
