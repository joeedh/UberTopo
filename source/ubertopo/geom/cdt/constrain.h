#pragma once

/* Constraint-edge insertion for the CDT (plan P2 amendment A1 §12.2 step 2).
 * Internal to geom/cdt/.
 *
 * The triangles crossed by segment a-b are removed and the two cavity
 * pseudo-polygons are re-triangulated as their own CDTs with the same
 * perturbed incircle (Anglada's recursion), so the result stays the unique
 * perturbed constrained Delaunay triangulation. A segment through a vertex or
 * across another constraint is a failure (the face is not projectable). */

#include "ubertopo/geom/cdt/triangulation.h"

namespace ubertopo::geom::cdt {

enum class ConstrainResult : uint8_t { Ok, ThroughVertex, CrossesConstraint, Internal };

template <CoordType C> class Constrainer {
public:
  explicit Constrainer(Triangulation<C> &tr) : tr_(tr) {}

  /** The triangle (a, x, y) around vertex a with directed edge a -> b, or
   * kNone. *slot receives the index of a in it. */
  uint32_t find_edge(uint32_t a, uint32_t b, uint32_t *slot) const
  {
    uint32_t t = tr_.vertex_tri(a);
    if (t == kNone) {
      return kNone;
    }
    const uint32_t guard = uint32_t(tr_.tris.size()) + 1;
    const uint32_t start = t;
    for (uint32_t k = 0; k < guard; k++) {
      const uint32_t i = tr_.index_of(t, a);
      if (tr_.tris[t].v[(i + 1) % 3] == b) {
        *slot = i;
        return t;
      }
      t = tr_.tris[t].n[(i + 1) % 3]; /* next triangle counter-clockwise around a */
      if (t == kNone || t == start) {
        break;
      }
    }
    return kNone;
  }

  /** Marks the edge of triangle t opposite slot s (and its twin) constrained. */
  void mark(uint32_t t, uint32_t s)
  {
    tr_.tris[t].cons |= uint8_t(1u << s);
    const uint32_t nb = tr_.tris[t].n[s];
    if (nb != kNone) {
      const uint32_t ns = tr_.slot_of(nb, t);
      tr_.tris[nb].cons |= uint8_t(1u << ns);
    }
  }

  ConstrainResult insert(uint32_t a, uint32_t b)
  {
    uint32_t s;
    if (uint32_t t = find_edge(a, b, &s); t != kNone) {
      mark(t, (s + 2) % 3); /* edge a -> b is opposite v[s+2] */
      return ConstrainResult::Ok;
    }
    if (uint32_t t = find_edge(b, a, &s); t != kNone) {
      mark(t, (s + 2) % 3);
      return ConstrainResult::Ok;
    }
    /* the wedge (a, x, y) around a that segment a-b leaves through */
    uint32_t t = tr_.vertex_tri(a);
    if (t == kNone) {
      return ConstrainResult::Internal;
    }
    const uint32_t start = t, guard = uint32_t(tr_.tris.size()) + 1;
    uint32_t wedge = kNone;
    for (uint32_t k = 0; k < guard; k++) {
      const Tri &x = tr_.tris[t];
      const uint32_t i = tr_.index_of(t, a);
      const uint32_t p = x.v[(i + 1) % 3], q = x.v[(i + 2) % 3];
      if (p != tr_.G && q != tr_.G) {
        const int op = tr_.orient(a, p, b), oq = tr_.orient(a, q, b);
        if (op == 0 && tr_.strictly_between(p, a, b)) {
          return ConstrainResult::ThroughVertex;
        }
        if (oq == 0 && tr_.strictly_between(q, a, b)) {
          return ConstrainResult::ThroughVertex;
        }
        if (op > 0 && oq < 0) {
          wedge = t;
          break;
        }
      }
      t = x.n[(i + 1) % 3];
      if (t == kNone || t == start) {
        break;
      }
    }
    if (wedge == kNone) {
      return ConstrainResult::Internal;
    }
    /* walk across the crossed edges, collecting both cavity chains */
    util::Vector<uint32_t, 16> removed, left, right;
    uint32_t cur = wedge;
    uint32_t i = tr_.index_of(cur, a);
    uint32_t r = tr_.tris[cur].v[(i + 1) % 3], l = tr_.tris[cur].v[(i + 2) % 3]; /* crossed edge r -> l */
    if (tr_.tris[cur].cons & (1u << i)) {
      return ConstrainResult::CrossesConstraint;
    }
    right.append(r);
    left.append(l);
    removed.append(cur);
    for (uint32_t k = 0; k < guard; k++) {
      /* neighbour across edge (r, l): it contains edge l -> r */
      const uint32_t nb = tr_.tris[cur].n[tr_.index_of(cur, third(cur, r, l))];
      if (nb == kNone || tr_.is_ghost(tr_.tris[nb])) {
        return ConstrainResult::Internal;
      }
      const uint32_t z = third(nb, r, l);
      removed.append(nb);
      cur = nb;
      if (z == b) {
        break;
      }
      const int oz = tr_.orient(a, b, z);
      if (oz == 0) {
        return ConstrainResult::ThroughVertex;
      }
      /* next crossed edge: (z, l) if z is right of a->b, else (r, z) */
      if (oz < 0) {
        const uint32_t zs = tr_.index_of(nb, r); /* edge l -> z is opposite r */
        if (tr_.tris[nb].cons & (1u << zs)) {
          return ConstrainResult::CrossesConstraint;
        }
        r = z;
        right.append(r);
      }
      else {
        const uint32_t ls = tr_.index_of(nb, l); /* edge z -> r is opposite l */
        if (tr_.tris[nb].cons & (1u << ls)) {
          return ConstrainResult::CrossesConstraint;
        }
        l = z;
        left.append(l);
      }
    }
    if (tr_.tris[cur].v[0] != b && tr_.tris[cur].v[1] != b && tr_.tris[cur].v[2] != b) {
      return ConstrainResult::Internal;
    }
    /* boundary of the removed set */
    using Ext = typename Triangulation<C>::Ext;
    util::Vector<Ext, 32> ext;
    for (uint32_t rt : removed) {
      for (uint32_t s2 = 0; s2 < 3; s2++) {
        const uint32_t nb = tr_.tris[rt].n[s2];
        bool inside = false;
        for (uint32_t o : removed) {
          inside = inside || o == nb;
        }
        if (!inside) {
          const uint32_t u = tr_.tris[rt].v[(s2 + 1) % 3], v = tr_.tris[rt].v[(s2 + 2) % 3];
          ext.append({u, v, nb, nb == kNone ? kNone : tr_.slot_of(nb, rt), bool(tr_.tris[rt].cons & (1u << s2))});
        }
      }
    }
    for (uint32_t rt : removed) {
      tr_.kill(rt);
    }
    util::Vector<uint32_t, 32> created;
    /* left chain runs from a to b on the left of a->b; right chain likewise on the right */
    retriangulate(a, b, std::span<const uint32_t>(left.data(), left.size()), created);
    util::Vector<uint32_t, 16> rrev;
    for (size_t k = right.size(); k-- > 0;) {
      rrev.append(right[k]);
    }
    retriangulate(b, a, std::span<const uint32_t>(rrev.data(), rrev.size()), created);
    tr_.link_set(std::span<const uint32_t>(created.data(), created.size()),
                 std::span<const Ext>(ext.data(), ext.size()));
    uint32_t s3;
    const uint32_t t2 = find_edge(a, b, &s3);
    if (t2 == kNone) {
      return ConstrainResult::Internal;
    }
    mark(t2, (s3 + 2) % 3);
    return ConstrainResult::Ok;
  }

private:
  uint32_t third(uint32_t t, uint32_t x, uint32_t y) const
  {
    for (uint32_t k = 0; k < 3; k++) {
      const uint32_t v = tr_.tris[t].v[k];
      if (v != x && v != y) {
        return v;
      }
    }
    return kNone;
  }

  /* Anglada: CDT of the pseudo-polygon u, chain..., w lying left of u -> w. */
  void retriangulate(uint32_t u, uint32_t w, std::span<const uint32_t> chain, util::Vector<uint32_t, 32> &created)
  {
    if (chain.empty()) {
      return;
    }
    const auto &P = tr_.pts();
    size_t ci = 0;
    for (size_t k = 1; k < chain.size(); k++) {
      const uint32_t c = chain[ci], x = chain[k];
      if (CoordTraits<C>::incircle_sos(P[u], P[w], P[c], P[x], tr_.rank(u), tr_.rank(w), tr_.rank(c), tr_.rank(x)) > 0) {
        ci = k;
      }
    }
    retriangulate(u, chain[ci], chain.subspan(0, ci), created);
    retriangulate(chain[ci], w, chain.subspan(ci + 1), created);
    created.append(tr_.add_tri(u, w, chain[ci]));
  }

  Triangulation<C> &tr_;
};

} // namespace ubertopo::geom::cdt
