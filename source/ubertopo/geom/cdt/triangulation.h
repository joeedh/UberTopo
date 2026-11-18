#pragma once

/* Incremental Delaunay triangulation in 2D with ghost triangles (plan P2
 * amendment A1 §12.2). Internal to geom/cdt/; see cdt.h for the public API.
 *
 * Vertices 0..n-1 are the input points; G = n is the symbolic vertex at
 * infinity. A triangle (v0, v1, v2) is counter-clockwise; a ghost triangle is
 * a rotation of (a, b, G) where the hull lies to the right of a -> b, so a
 * point p is beyond that hull edge iff orient2d(a, b, p) > 0.
 *
 * Insertion is Bowyer-Watson: the triangles in conflict with p (perturbed
 * incircle > 0; for a ghost (a,b,G): orient2d(a,b,p) > 0, or = 0 with p
 * strictly inside segment ab) form a star-shaped cavity that is replaced by a
 * fan around p. Cocircular ties are broken by incircle_sos with lexicographic
 * ranks, so the result is the unique perturbed Delaunay triangulation of the
 * point set, independent of insertion order. */

#include <cstdint>
#include <span>

#include "litestl/util/vector.h"
#include "ubertopo/geom/coord.h"

namespace ubertopo::geom::cdt {

namespace util = litestl::util;

inline constexpr uint32_t kNone = 0xffffffffu;

struct Tri {
  uint32_t v[3];
  uint32_t n[3];      /* n[i]: neighbour across the edge (v[i+1], v[i+2]), opposite v[i] */
  uint8_t cons = 0;   /* bit i: the edge opposite v[i] is a constraint */
  bool dead = false;
};

template <CoordType C> class Triangulation {
public:
  using P2 = Point2<C>;
  using T = CoordTraits<C>;

  /** @p pts: distinct projected points; @p rank: distinct ranks (lexicographic order). */
  Triangulation(std::span<const P2> pts, std::span<const uint32_t> rank) : pts_(pts), rank_(rank)
  {
    G = uint32_t(pts.size());
    vtri_.resize(pts.size() + 1);
    for (uint32_t i = 0; i <= G; i++) {
      vtri_[i] = kNone;
    }
  }

  uint32_t G = 0;

  util::Vector<Tri> tris;

  bool is_ghost(const Tri &t) const
  {
    return t.v[0] == G || t.v[1] == G || t.v[2] == G;
  }

  int orient(uint32_t a, uint32_t b, uint32_t c) const
  {
    return T::orient2d(pts_[a], pts_[b], pts_[c]);
  }

  /** -1/0/+1 lexicographic comparison of two input points. */
  int lex(uint32_t a, uint32_t b) const
  {
    const P2 &p = pts_[a], &q = pts_[b];
    if (p[0] < q[0]) {
      return -1;
    }
    if (q[0] < p[0]) {
      return 1;
    }
    if (p[1] < q[1]) {
      return -1;
    }
    if (q[1] < p[1]) {
      return 1;
    }
    return 0;
  }

  /** p strictly inside segment ab, given that the three are collinear. */
  bool strictly_between(uint32_t p, uint32_t a, uint32_t b) const
  {
    const int ca = lex(p, a), cb = lex(p, b);
    return ca != 0 && cb != 0 && ca != cb;
  }

  /** Builds the initial triangulation from points given in insertion order,
   * consuming the collinear prefix. Returns the number of points consumed, or
   * 0 if every point is collinear (no triangle exists). */
  uint32_t init(std::span<const uint32_t> order)
  {
    const uint32_t n = uint32_t(order.size());
    if (n < 3) {
      return 0;
    }
    uint32_t m = 2;
    while (m < n && orient(order[0], order[1], order[m]) == 0) {
      m++;
    }
    if (m == n) {
      return 0;
    }
    util::Vector<uint32_t> chain;
    for (uint32_t i = 0; i < m; i++) {
      chain.append(order[i]);
    }
    /* collinear points: lexicographic order is their order along the line */
    std::sort(chain.begin(), chain.end(), [&](uint32_t a, uint32_t b) { return lex(a, b) < 0; });
    const uint32_t q = order[m];
    const bool left = orient(chain[0], chain[1], q) > 0;
    util::Vector<uint32_t> created;
    for (uint32_t i = 0; i + 1 < chain.size(); i++) {
      const uint32_t a = chain[i], b = chain[i + 1];
      created.append(left ? add_tri(a, b, q) : add_tri(b, a, q));
    }
    /* ghosts on every edge without a real neighbour, then link everything */
    link_set(std::span<const uint32_t>(created.data(), created.size()), {});
    util::Vector<uint32_t> ghosts;
    for (uint32_t t : created) {
      for (int i = 0; i < 3; i++) {
        if (tris[t].n[i] == kNone) {
          const uint32_t a = tris[t].v[(i + 1) % 3], b = tris[t].v[(i + 2) % 3];
          ghosts.append(add_tri(b, a, G));
        }
      }
    }
    for (uint32_t g : ghosts) {
      created.append(g);
    }
    for (Tri &t : tris) {
      for (int i = 0; i < 3; i++) {
        t.n[i] = kNone;
      }
    }
    link_set(std::span<const uint32_t>(created.data(), created.size()), {});
    return m + 1;
  }

  /** Inserts point p. Returns false if p coincides with an existing vertex. */
  bool insert(uint32_t p, uint32_t hint_vertex)
  {
    const uint32_t start = locate(p, hint_vertex);
    if (start == kNone) {
      return false;
    }
    /* conflict region (BFS in a fixed neighbour order) */
    util::Vector<uint32_t> cavity, stack;
    stack.append(start);
    const size_t old = mark_.size();
    mark_.resize(tris.size());
    for (size_t i = old; i < mark_.size(); i++) {
      mark_[i] = 0;
    }
    epoch_++;
    if (epoch_ == 0) {
      for (auto &x : mark_) {
        x = 0;
      }
      epoch_ = 1;
    }
    mark_[start] = epoch_;
    while (stack.size() > 0) {
      const uint32_t t = stack.pop_back();
      cavity.append(t);
      for (int i = 0; i < 3; i++) {
        const uint32_t nb = tris[t].n[i];
        if (nb != kNone && mark_[nb] != epoch_ && !(tris[t].cons & (1u << i)) && conflicts(nb, p)) {
          mark_[nb] = epoch_;
          stack.append(nb);
        }
      }
    }
    /* boundary edges u -> v (as they ran in the removed triangles) */
    struct Bd {
      uint32_t u, v, out, out_slot;
      bool cons;
    };
    util::Vector<Bd, 16> bd;
    for (uint32_t t : cavity) {
      for (int i = 0; i < 3; i++) {
        const uint32_t nb = tris[t].n[i];
        if (nb == kNone || mark_[nb] != epoch_) {
          const uint32_t u = tris[t].v[(i + 1) % 3], v = tris[t].v[(i + 2) % 3];
          bd.append({u, v, nb, nb == kNone ? kNone : slot_of(nb, t), bool(tris[t].cons & (1u << i))});
        }
      }
    }
    for (uint32_t t : cavity) {
      kill(t);
    }
    util::Vector<uint32_t, 16> created;
    for (const Bd &e : bd) {
      if (e.u != G && e.v != G && orient(e.u, e.v, p) <= 0) {
        return false; /* not star-shaped: cannot happen for distinct points */
      }
      created.append(add_tri(e.u, e.v, p));
    }
    util::Vector<Ext, 16> ext;
    for (size_t k = 0; k < bd.size(); k++) {
      ext.append({bd[k].u, bd[k].v, bd[k].out, bd[k].out_slot, bd[k].cons});
    }
    link_set(std::span<const uint32_t>(created.data(), created.size()), std::span<const Ext>(ext.data(), ext.size()));
    return true;
  }

  /** An edge outside a re-triangulated set: directed u -> v as seen from the
   * inside, owned on the outside by triangle out at slot out_slot. */
  struct Ext {
    uint32_t u, v, out, out_slot;
    bool cons;
  };

  /** Links the triangles of @p set with each other and with @p ext. */
  void link_set(std::span<const uint32_t> set, std::span<const Ext> ext)
  {
    struct DE {
      uint32_t u, v, t, s;
    };
    util::Vector<DE, 32> es;
    for (uint32_t t : set) {
      for (uint32_t i = 0; i < 3; i++) {
        es.append({tris[t].v[(i + 1) % 3], tris[t].v[(i + 2) % 3], t, i});
      }
    }
    std::sort(es.begin(), es.end(), [](const DE &a, const DE &b) { return a.u != b.u ? a.u < b.u : a.v < b.v; });
    auto find = [&](uint32_t u, uint32_t v) -> const DE * {
      const DE *lo = es.data(), *hi = es.data() + es.size();
      const DE *it = std::lower_bound(lo, hi, DE{u, v, 0, 0}, [](const DE &a, const DE &b) {
        return a.u != b.u ? a.u < b.u : a.v < b.v;
      });
      return (it != hi && it->u == u && it->v == v) ? it : nullptr;
    };
    for (const DE &e : es) {
      if (const DE *tw = find(e.v, e.u)) {
        tris[e.t].n[e.s] = tw->t;
      }
    }
    for (const Ext &x : ext) {
      if (const DE *in = find(x.u, x.v)) {
        tris[in->t].n[in->s] = x.out;
        if (x.cons) {
          tris[in->t].cons |= uint8_t(1u << in->s);
        }
        if (x.out != kNone) {
          tris[x.out].n[x.out_slot] = in->t;
        }
      }
    }
  }

  uint32_t slot_of(uint32_t t, uint32_t nb) const
  {
    for (uint32_t i = 0; i < 3; i++) {
      if (tris[t].n[i] == nb) {
        return i;
      }
    }
    return kNone;
  }

  uint32_t add_tri(uint32_t a, uint32_t b, uint32_t c)
  {
    Tri t;
    t.v[0] = a, t.v[1] = b, t.v[2] = c;
    t.n[0] = t.n[1] = t.n[2] = kNone;
    uint32_t id;
    if (free_.size() > 0) {
      id = free_.pop_back();
      tris[id] = t;
    }
    else {
      id = uint32_t(tris.size());
      tris.append(t);
    }
    vtri_[a] = vtri_[b] = vtri_[c] = id;
    return id;
  }

  void kill(uint32_t t)
  {
    tris[t].dead = true;
    free_.append(t);
  }

  /** Some live triangle incident to vertex v (kNone if none yet). */
  uint32_t vertex_tri(uint32_t v) const
  {
    const uint32_t t = vtri_[v];
    return (t != kNone && !tris[t].dead && has_vertex(t, v)) ? t : kNone;
  }
  void set_vertex_tri(uint32_t v, uint32_t t)
  {
    vtri_[v] = t;
  }

  bool has_vertex(uint32_t t, uint32_t v) const
  {
    return tris[t].v[0] == v || tris[t].v[1] == v || tris[t].v[2] == v;
  }
  uint32_t index_of(uint32_t t, uint32_t v) const
  {
    for (uint32_t i = 0; i < 3; i++) {
      if (tris[t].v[i] == v) {
        return i;
      }
    }
    return kNone;
  }

  /** Perturbed Delaunay conflict of point p with triangle t. */
  bool conflicts(uint32_t t, uint32_t p) const
  {
    const Tri &x = tris[t];
    for (int i = 0; i < 3; i++) {
      if (x.v[i] == G) {
        const uint32_t a = x.v[(i + 1) % 3], b = x.v[(i + 2) % 3];
        const int o = orient(a, b, p);
        return o > 0 || (o == 0 && strictly_between(p, a, b));
      }
    }
    return T::incircle_sos(pts_[x.v[0]], pts_[x.v[1]], pts_[x.v[2]], pts_[p], rank_[x.v[0]], rank_[x.v[1]],
                           rank_[x.v[2]], rank_[p]) > 0;
  }

  /** A triangle in conflict with p: visibility walk from a triangle at
   * @p hint_vertex. kNone if p coincides with a vertex. */
  uint32_t locate(uint32_t p, uint32_t hint_vertex) const
  {
    uint32_t t = vertex_tri(hint_vertex);
    if (t == kNone) {
      for (uint32_t i = 0; i < tris.size(); i++) {
        if (!tris[i].dead) {
          t = i;
          break;
        }
      }
    }
    /* start from a real triangle */
    if (is_ghost(tris[t])) {
      const uint32_t g = index_of(t, G);
      t = tris[t].n[g];
    }
    for (uint32_t guard = 0; guard < 4 * tris.size() + 8; guard++) {
      const Tri &x = tris[t];
      if (is_ghost(x)) {
        return conflicts(t, p) ? t : kNone;
      }
      bool moved = false;
      for (int i = 0; i < 3; i++) {
        const uint32_t a = x.v[(i + 1) % 3], b = x.v[(i + 2) % 3];
        if (orient(a, b, p) < 0) {
          t = x.n[i];
          moved = true;
          break;
        }
      }
      if (!moved) {
        /* p in the closed triangle: a vertex means a duplicate */
        for (int i = 0; i < 3; i++) {
          if (lex(x.v[i], p) == 0) {
            return kNone;
          }
        }
        return t;
      }
    }
    return kNone;
  }

  std::span<const P2> pts() const
  {
    return pts_;
  }
  uint32_t rank(uint32_t v) const
  {
    return rank_[v];
  }

private:
  std::span<const P2> pts_;
  std::span<const uint32_t> rank_;
  util::Vector<uint32_t> vtri_;
  util::Vector<uint32_t> free_;
  util::Vector<uint32_t> mark_;
  uint32_t epoch_ = 0;
};

} // namespace ubertopo::geom::cdt
