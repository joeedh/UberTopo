#include "ubertopo/csg/subdivide.h"

#include <algorithm>

#include "litestl/util/map.h"

namespace ubertopo::csg {

namespace {

using Points = litestl::util::Vector<uint32_t, 16>;

struct Atom {
  uint32_t u, v; /* u < v */
  uint32_t cut;
  int8_t dir;  /* u -> v along cross(plane, cut): +1, against: -1 */
  uint8_t own; /* bit 0: the polygon is on the left of u -> v, on one of its edges;
                * bit 1: on the right */
};

struct Subdivider {
  Input &in;
  const Polygon &g;
  const Plane plane;
  int u, v;   /* projection axes */
  int orient; /* +1 if (u, v) sees the normal's side counter-clockwise */

  litestl::util::Vector<Cut, 16> segs;
  litestl::util::Vector<uint8_t, 16> seg_own; /* 1 an edge of g along the loop, 0 not */
  Points points;
  litestl::util::Vector<Atom, 32> atoms;
  litestl::util::Map<uint64_t, uint32_t> atom_of;
  /* half edges 2 a (u -> v) and 2 a + 1; outgoing at local vertices, sorted */
  litestl::util::Map<uint32_t, uint32_t> local;
  litestl::util::Vector<litestl::util::Vector<uint32_t, 6>> out;
  litestl::util::Vector<uint32_t, 64> next, cycle;
  litestl::util::Vector<uint32_t, 16> cycle_start;
  litestl::util::Vector<uint8_t, 16> cycle_outer; /* faces away from its component */

  Subdivider(Input &in_, const Polygon &g_) : in(in_), g(g_), plane(in_.planes[g_.plane])
  {
    u = (g.axis + 1) % 3;
    v = (g.axis + 2) % 3;
    orient = plane.n[g.axis] > 0 ? 1 : -1;
  }

  uint32_t from(uint32_t h) const
  {
    const Atom &a = atoms[int(h >> 1)];
    return h & 1 ? a.v : a.u;
  }
  uint32_t to(uint32_t h) const
  {
    return from(h ^ 1);
  }
  /* +1 if h runs along cross(plane, cut) */
  int dir(uint32_t h) const
  {
    const int d = atoms[int(h >> 1)].dir;
    return h & 1 ? -d : d;
  }
  const Plane &cut(uint32_t h) const
  {
    return in.planes[atoms[int(h >> 1)].cut];
  }

  void add_point(uint32_t p)
  {
    if (std::find(points.begin(), points.end(), p) == points.end()) {
      points.append(p);
    }
  }

  void gather(const PolygonCuts &cuts)
  {
    g.for_each_edge([&](uint32_t i, uint32_t j) {
      segs.append({g.pts[int(i)], g.pts[int(j)], cut_plane(in, g, g.pts[int(i)], g.pts[int(j)])});
      seg_own.append(1);
    });
    for (const Cut &c : cuts.segments) {
      segs.append(c);
      seg_own.append(0);
    }
    for (const Cut &c : segs) {
      add_point(c.a);
      add_point(c.b);
    }
    for (uint32_t p : cuts.dots) {
      add_point(p);
    }
    /* crossings */
    for (int i = 0; i < int(segs.size()); i++) {
      for (int j = i + 1; j < int(segs.size()); j++) {
        const Cut s = segs[i], t = segs[j];
        if (s.cut == t.cut || s.a == t.a || s.a == t.b || s.b == t.a || s.b == t.b) {
          continue;
        }
        const Plane &ps = in.planes[s.cut], &pt = in.planes[t.cut];
        if (classify(in.points[t.a], ps) * classify(in.points[t.b], ps) < 0 &&
            classify(in.points[s.a], pt) * classify(in.points[s.b], pt) < 0)
        {
          HPoint x;
          meet(plane, ps, pt, x);
          add_point(in.points.add(x));
        }
      }
    }
  }

  void split()
  {
    for (int i = 0; i < int(segs.size()); i++) {
      const Cut s = segs[i];
      Points on;
      for (uint32_t p : points) {
        if (p == s.a || p == s.b || classify(in.points[p], in.planes[s.cut]) == 0) {
          on.append(p);
        }
      }
      sort_along(in, g.plane, s.cut, on);
      int ia = 0, ib = 0;
      for (int k = 0; k < int(on.size()); k++) {
        ia = on[k] == s.a ? k : ia;
        ib = on[k] == s.b ? k : ib;
      }
      /* along cross(plane, cut), or against it: sort_along's axis decides */
      const Dir d = cross(plane.n, in.planes[s.cut].n);
      const int axis = !d.v[0].is_zero() ? 0 : !d.v[1].is_zero() ? 1 : 2;
      const int up = d.v[axis].sign();
      for (int k = std::min(ia, ib); k < std::max(ia, ib); k++) {
        const uint32_t p = on[k], q = on[k + 1];
        const uint32_t lo = std::min(p, q), hi = std::max(p, q);
        const uint64_t key = (uint64_t(lo) << 32) | hi;
        uint32_t *known = atom_of.lookup_ptr(key);
        uint32_t a = known ? *known : uint32_t(atoms.size());
        if (!known) {
          /* p before q on the axis */
          atoms.append({lo, hi, s.cut, int8_t(p == lo ? up : -up), 0});
          atom_of.add(key, a);
        }
        if (seg_own[i]) {
          /* the loop has the polygon on the left, seen from the front */
          const bool along = ia < ib ? p == lo : q == lo; /* the loop runs lo -> hi */
          atoms[int(a)].own |= along == (g.sign > 0) ? 1 : 2;
        }
      }
    }
  }

  /* outgoing half edges about a vertex, counter-clockwise seen from where the
   * normal points, from the projection's +u axis */
  int half(uint32_t h) const
  {
    const Dir d = cross(plane.n, cut(h).n);
    const int s = dir(h);
    const int du = s * d.v[u].sign(), dv = s * orient * d.v[v].sign();
    return dv > 0 || (dv == 0 && du > 0) ? 0 : 1;
  }
  bool before(uint32_t a, uint32_t b) const
  {
    const int ha = half(a), hb = half(b);
    if (ha != hb) {
      return ha < hb;
    }
    return dir(a) * dir(b) * triple(plane.n, cut(a).n, cut(b).n).sign() > 0;
  }

  void trace()
  {
    for (uint32_t h = 0; h < 2 * atoms.size(); h++) {
      const uint32_t p = from(h);
      uint32_t *l = local.lookup_ptr(p);
      if (!l) {
        local.add(p, uint32_t(out.size()));
        out.append({});
        l = local.lookup_ptr(p);
      }
      out[int(*l)].append(h);
    }
    next.resize(2 * atoms.size());
    cycle.resize(2 * atoms.size());
    for (auto &o : out) {
      std::sort(o.begin(), o.end(), [&](uint32_t a, uint32_t b) { return before(a, b); });
      const int n = int(o.size());
      for (int i = 0; i < n; i++) {
        /* arriving on o[i]'s twin, leave by the next one clockwise */
        next[int(o[i] ^ 1)] = o[(i + n - 1) % n];
      }
    }
    for (uint32_t h = 0; h < 2 * atoms.size(); h++) {
      cycle[int(h)] = kNone;
    }
    for (uint32_t h = 0; h < 2 * atoms.size(); h++) {
      if (cycle[int(h)] != kNone) {
        continue;
      }
      const uint32_t c = uint32_t(cycle_start.size());
      cycle_start.append(h);
      cycle_outer.append(0);
      for (uint32_t x = h; cycle[int(x)] == kNone; x = next[int(x)]) {
        cycle[int(x)] = c;
      }
    }
  }

  /* whether a point off cycle c is inside it (even-odd along u; slits are run
   * twice and cancel) */
  bool contains(uint32_t c, uint32_t p) const
  {
    bool inside = false;
    const HPoint &x = in.points[p];
    for (uint32_t h = cycle_start[int(c)];;) {
      const int a = compare(x, in.points[from(h)], v), b = compare(x, in.points[to(h)], v);
      if ((a < 0) != (b < 0)) {
        /* the cut's normal points to the right of cross(plane, cut) */
        const int left = -dir(h) * classify(x, cut(h));
        inside = inside != ((left > 0) == (b < 0));
      }
      h = next[int(h)];
      if (h == cycle_start[int(c)]) {
        return inside;
      }
    }
  }

  CsgCode emit(const PolygonCuts &cuts, litestl::util::Vector<PolygonFacet> &facets)
  {
    /* components of the graph, by their vertices */
    const int nv = int(out.size());
    litestl::util::Vector<uint32_t, 64> comp, vertex;
    vertex.resize(nv);
    for (uint32_t h = 0; h < 2 * atoms.size(); h++) {
      vertex[int(*local.lookup_ptr(from(h)))] = from(h);
    }
    for (int i = 0; i < nv; i++) {
      comp.append(kNone);
    }
    litestl::util::Vector<uint32_t, 8> leftmost; /* of each component */
    litestl::util::Vector<uint32_t, 64> stack;
    for (int i = 0; i < nv; i++) {
      if (comp[i] != kNone) {
        continue;
      }
      const uint32_t c = uint32_t(leftmost.size());
      leftmost.append(uint32_t(i));
      comp[i] = c;
      stack.append(uint32_t(i));
      while (stack.size() > 0) {
        const uint32_t x = stack.pop_back();
        if (compare(in.points[vertex[int(x)]], in.points[vertex[int(leftmost[int(c)])]], u) < 0) {
          leftmost[int(c)] = x;
        }
        for (uint32_t h : out[int(x)]) {
          const uint32_t y = *local.lookup_ptr(to(h));
          if (comp[int(y)] == kNone) {
            comp[int(y)] = c;
            stack.append(y);
          }
        }
      }
    }
    /* at a leftmost vertex no edge runs to the left; the wedge that looks left
     * is on the left of the last one that runs up or to the right */
    litestl::util::Vector<uint32_t, 8> outer; /* cycle of each component */
    for (uint32_t x : leftmost) {
      const auto &o = out[int(x)];
      uint32_t h = o[int(o.size()) - 1];
      for (int i = int(o.size()) - 1; i >= 0; i--) {
        if (half(o[i]) == 0) {
          h = o[i];
          break;
        }
      }
      outer.append(cycle[int(h)]);
      cycle_outer[int(cycle[int(h)])] = 1;
    }
    /* the bounded cycle directly around a point that isn't on component
     * skip */
    const int nc = int(cycle_start.size());
    auto around = [&](uint32_t p, uint32_t skip) {
      litestl::util::Vector<uint32_t, 8> in_cycles;
      for (int c = 0; c < nc; c++) {
        if (!cycle_outer[c] && comp[int(*local.lookup_ptr(from(cycle_start[c])))] != skip && contains(uint32_t(c), p)) {
          in_cycles.append(uint32_t(c));
        }
      }
      /* the innermost: contained in all the others */
      uint32_t best = kNone;
      int best_depth = -1;
      for (uint32_t c : in_cycles) {
        const uint32_t own = comp[int(*local.lookup_ptr(from(cycle_start[int(c)])))];
        int depth = 0;
        for (uint32_t d : in_cycles) {
          depth += d != c && contains(d, vertex[int(leftmost[int(own)])]);
        }
        if (depth > best_depth) {
          best_depth = depth;
          best = c;
        }
      }
      return best;
    };
    litestl::util::Vector<uint32_t, 16> facet_of; /* by bounded cycle */
    for (int c = 0; c < nc; c++) {
      facet_of.append(kNone);
      if (!cycle_outer[c]) {
        facet_of[c] = uint32_t(facets.size());
        facets.append(PolygonFacet());
        facets[int(facet_of[c])].off.append(0);
      }
    }
    const int base = int(facets.size());
    litestl::util::Vector<uint8_t, 16> outside;
    for (int i = 0; i < base; i++) {
      outside.append(0);
    }
    auto add_cycle = [&](uint32_t f, uint32_t c) {
      PolygonFacet &pf = facets[int(f)];
      for (uint32_t h = cycle_start[int(c)];;) {
        pf.edges.append({from(h), to(h), atoms[int(h >> 1)].cut});
        if (atoms[int(h >> 1)].own == (h & 1 ? 1 : 2)) {
          outside[int(f)] = 1; /* on the far side of one of the polygon's edges */
        }
        h = next[int(h)];
        if (h == cycle_start[int(c)]) {
          break;
        }
      }
      pf.off.append(uint32_t(pf.edges.size()));
    };
    for (int c = 0; c < nc; c++) {
      if (!cycle_outer[c]) {
        add_cycle(facet_of[c], uint32_t(c));
      }
    }
    for (int i = 0; i < int(outer.size()); i++) {
      const uint32_t c = around(vertex[int(leftmost[i])], uint32_t(i));
      if (c != kNone) {
        add_cycle(facet_of[int(c)], outer[i]);
      }
    }
    for (uint32_t p : cuts.dots) {
      if (local.lookup_ptr(p)) {
        continue;
      }
      const uint32_t c = around(p, kNone);
      if (c == kNone) {
        return CsgCode::Inconsistent;
      }
      auto &dots = facets[int(facet_of[int(c)])].dots;
      if (std::find(dots.begin(), dots.end(), p) == dots.end()) {
        dots.append(p);
      }
    }
    /* keep the pieces inside */
    int kept = 0;
    for (int i = 0; i < base; i++) {
      if (!outside[i]) {
        if (kept != i) {
          facets[kept] = std::move(facets[i]);
        }
        kept++;
      }
    }
    facets.resize(kept);
    return kept > 0 ? CsgCode::Ok : CsgCode::Inconsistent;
  }
};

} // namespace

CsgCode subdivide(Input &in, const Polygon &g, const PolygonCuts &cuts,
                  litestl::util::Vector<PolygonFacet> &out)
{
  out.clear();
  Subdivider s(in, g);
  s.gather(cuts);
  s.split();
  s.trace();
  return s.emit(cuts, out);
}

} // namespace ubertopo::csg
