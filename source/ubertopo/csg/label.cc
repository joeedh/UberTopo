/* The regions on both sides of every facet (plan 2026-11-25-exact-booleans
 * §4.4), one leaf after the other. Going round an edge, a leaf's region
 * changes only at the leaf's own facets. Pieces of the arrangement that the
 * leaf doesn't touch in an edge take one ray each. */

#include <algorithm>

#include "ubertopo/csg/arrangement.h"
#include "ubertopo/csg/ray.h"

namespace ubertopo::csg {

namespace {

using litestl::util::Vector;

struct Labeller {
  Arrangement &a;
  uint32_t leaf;
  I3 lo{0, 0, 0}, hi{0, 0, 0}; /* of the leaf */
  Vector<uint32_t> &queue;
  bool ok = true;

  uint32_t &at(uint32_t facet, uint32_t side)
  {
    return a.regions[int(2 * (facet * a.leaf_count + leaf) + side)];
  }
  bool own(uint32_t facet) const
  {
    return a.source_of(facet, leaf) != kNone;
  }
  void set(uint32_t facet, uint32_t region)
  {
    uint32_t &x = at(facet, 0), &y = at(facet, 1);
    if (x == kNone) {
      x = y = region;
      queue.append(facet);
    }
    else if (x != region || y != region) {
      ok = false;
    }
  }

  /* goes round an edge that the leaf has a facet at */
  void walk(const ArrEdge &e, int start)
  {
    const int n = int(e.uses.size());
    uint32_t wedge = at(e.uses[start].facet, e.uses[start].side_after());
    for (int i = 1; i <= n; i++) {
      const EdgeUse &u = e.uses[(start + i) % n];
      if (own(u.facet)) {
        ok = ok && at(u.facet, 1 - u.side_after()) == wedge;
        wedge = at(u.facet, u.side_after());
      }
      else {
        set(u.facet, wedge);
      }
    }
  }

  void flood(const Vector<uint8_t> &touched)
  {
    while (queue.size() > 0 && ok) {
      const uint32_t f = queue.pop_back();
      const uint32_t r = at(f, 0);
      for (const FacetEdge &fe : a.facets[int(f)].edges) {
        if (!touched[int(fe.edge)]) {
          for (const EdgeUse &u : a.edges[int(fe.edge)].uses) {
            set(u.facet, r);
          }
        }
      }
    }
  }

  /* the leaf's region at the middle of an edge it doesn't touch */
  uint32_t ray(const ArrEdge &e)
  {
    const Input &in = a.in;
    const Mid o(in.points[e.a], in.points[e.b]);
    for (int i = 0; i < 3; i++) {
      const Int512 l = Int512::from(lo[i]) * o.w, h = Int512::from(hi[i]) * o.w;
      if (o.x[i] < l || h < o.x[i]) {
        return 0;
      }
    }
    a.stats.rays++;
    const uint32_t b = in.leaf_begin[int(leaf)], n = in.leaf_begin[int(leaf) + 1] - b;
    Vector<int8_t> sides;
    for (uint32_t i = 0; i < n; i++) {
      sides.append(int8_t(classify(o, in.planes[in.polygons[int(b + i)].plane])));
    }
    struct Term {
      uint32_t region;
      int32_t count;
    };
    Vector<Term, 8> sum;
    auto add = [&](uint32_t r, int32_t d) {
      for (Term &t : sum) {
        if (t.region == r) {
          t.count += d;
          return;
        }
      }
      sum.append({r, d});
    };
    for (int64_t t = 1; t <= kMaxRay; t++) {
      const I3 dir = ray_direction(t);
      bool bad = false;
      sum.clear();
      add(0, 1);
      for (uint32_t i = 0; i < n && !bad; i++) {
        const Polygon &g = in.polygons[int(b + i)];
        const int hit = ray_hits(in, o, dir, g, sides[int(i)]);
        bad = hit < 0;
        if (hit > 0) {
          const uint32_t near = sides[int(i)] == g.sign ? 0 : 1;
          add(g.region[near], 1);
          add(g.region[1 - near], -1);
        }
      }
      if (bad) {
        a.stats.retries++;
        continue;
      }
      uint32_t found = kNone;
      for (const Term &x : sum) {
        if (x.count == 1 && found == kNone) {
          found = x.region;
        }
        else if (x.count != 0) {
          return kNone;
        }
      }
      return found;
    }
    return kNone;
  }

  CsgCode run()
  {
    const uint32_t nf = uint32_t(a.facets.size());
    const uint32_t pb = a.in.leaf_begin[int(leaf)], pe = a.in.leaf_begin[int(leaf) + 1];
    for (uint32_t i = pb; i < pe; i++) {
      const Polygon &g = a.in.polygons[int(i)];
      for (int k = 0; k < 3; k++) {
        lo[k] = i > pb ? std::min(lo[k], g.lo[k]) : g.lo[k];
        hi[k] = i > pb ? std::max(hi[k], g.hi[k]) : g.hi[k];
      }
    }
    for (uint32_t f = 0; f < nf; f++) {
      const uint32_t s = a.source_of(f, leaf);
      if (s != kNone) {
        const Polygon &g = a.in.polygons[int(s)];
        at(f, 0) = g.region[g.sign > 0 ? 0 : 1];
        at(f, 1) = g.region[g.sign > 0 ? 1 : 0];
      }
    }
    Vector<uint8_t> touched;
    touched.resize(a.edges.size());
    for (int i = 0; i < int(a.edges.size()); i++) {
      const ArrEdge &e = a.edges[i];
      touched[i] = 0;
      for (int j = 0; j < int(e.uses.size()) && !touched[i]; j++) {
        if (own(e.uses[j].facet)) {
          touched[i] = 1;
          walk(e, j);
        }
      }
    }
    flood(touched);
    for (uint32_t f = 0; f < nf && ok; f++) {
      if (at(f, 0) == kNone) {
        /* none of its edges is touched */
        const uint32_t r = ray(a.edges[int(a.facets[int(f)].edges[0].edge)]);
        ok = r != kNone;
        set(f, r);
        flood(touched);
      }
    }
    return ok ? CsgCode::Ok : CsgCode::Inconsistent;
  }
};

} // namespace

CsgCode label_arrangement(Arrangement &a)
{
  a.regions.resize(2 * a.facets.size() * a.leaf_count);
  for (uint32_t &r : a.regions) {
    r = kNone;
  }
  Vector<uint32_t> queue;
  for (uint32_t l = 0; l < a.leaf_count; l++) {
    Labeller x{a, l, I3(0, 0, 0), I3(0, 0, 0), queue};
    const CsgCode c = x.run();
    if (c != CsgCode::Ok) {
      return c;
    }
    queue.clear();
  }
  return CsgCode::Ok;
}

} // namespace ubertopo::csg
