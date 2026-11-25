#include "ubertopo/csg/arrangement.h"

#include <algorithm>

#include "litestl/util/map.h"

namespace ubertopo::csg {

namespace {

using litestl::util::Vector;

/* pairs of polygons of different leaves whose boxes meet: a sweep in x */
void intersect_all(Arrangement &a, Vector<PolygonCuts> &cuts)
{
  Input &in = a.in;
  const uint32_t n = uint32_t(in.polygons.size());
  Vector<uint32_t> order, active;
  for (uint32_t i = 0; i < n; i++) {
    order.append(i);
    cuts.append(PolygonCuts());
  }
  std::sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) {
    const int64_t lx = in.polygons[int(x)].lo[0], ly = in.polygons[int(y)].lo[0];
    return lx != ly ? lx < ly : x < y;
  });
  for (uint32_t i : order) {
    const Polygon &p = in.polygons[int(i)];
    int kept = 0;
    for (uint32_t j : active) {
      const Polygon &q = in.polygons[int(j)];
      if (q.hi[0] < p.lo[0]) {
        continue;
      }
      active[kept++] = j;
      if (p.leaf != q.leaf && p.lo[1] <= q.hi[1] && q.lo[1] <= p.hi[1] && p.lo[2] <= q.hi[2] && q.lo[2] <= p.hi[2]) {
        a.stats.pairs++;
        intersect_polygons(in, i, j, cuts[int(i)], cuts[int(j)]);
      }
    }
    active.resize(kept);
    active.append(i);
  }
}

struct Directed {
  uint32_t plane, from, to;
  bool operator==(const Directed &b) const = default;
  uint64_t computeHash() const
  {
    uint64_t h = 0;
    for (const uint32_t v : {plane, from, to}) {
      h = (h ^ v) * 0x9e3779b97f4a7c15ull;
      h ^= h >> 29;
    }
    return h;
  }
};

struct Merger {
  Arrangement &a;
  litestl::util::Map<uint64_t, uint32_t> edge_of;
  litestl::util::Map<Directed, uint32_t> facet_of; /* by a directed edge with it on the left */

  uint32_t edge(uint32_t p, uint32_t q)
  {
    const uint32_t lo = std::min(p, q), hi = std::max(p, q);
    const uint64_t key = (uint64_t(lo) << 32) | hi;
    if (const uint32_t *e = edge_of.lookup_ptr(key)) {
      return *e;
    }
    const uint32_t e = uint32_t(a.edges.size());
    a.edges.append(ArrEdge{lo, hi, {}});
    edge_of.add(key, e);
    return e;
  }

  CsgCode add(uint32_t polygon, PolygonFacet &f)
  {
    const uint32_t plane = a.in.polygons[int(polygon)].plane;
    const HalfEdge &first = f.edges[0];
    if (const uint32_t *known = facet_of.lookup_ptr({plane, first.from, first.to})) {
      /* the same piece on another polygon: all of its boundary agrees */
      Facet &g = a.facets[int(*known)];
      bool same = g.edges.size() == f.edges.size() && g.dots.size() == f.dots.size();
      for (const HalfEdge &h : f.edges) {
        const uint32_t *k = facet_of.lookup_ptr({plane, h.from, h.to});
        same = same && k && *k == *known;
      }
      for (uint32_t d : f.dots) {
        same = same && std::find(g.dots.begin(), g.dots.end(), d) != g.dots.end();
      }
      if (!same) {
        return CsgCode::Inconsistent;
      }
      g.sources.append(polygon);
      return CsgCode::Ok;
    }
    const uint32_t id = uint32_t(a.facets.size());
    Facet g;
    g.plane = plane;
    g.sources.append(polygon);
    for (const HalfEdge &h : f.edges) {
      if (facet_of.lookup_ptr({plane, h.from, h.to})) {
        return CsgCode::Inconsistent; /* another facet there, with another boundary */
      }
      facet_of.add({plane, h.from, h.to}, id);
      const int8_t dir = h.from < h.to ? 1 : -1;
      const uint32_t e = edge(h.from, h.to);
      g.edges.append({e, dir});
      a.edges[int(e)].uses.append({id, dir});
    }
    g.off = f.off;
    g.dots = f.dots;
    a.facets.append(std::move(g));
    return CsgCode::Ok;
  }
};

/* orders the facets at an edge. A facet extends from it along
 * dir * (n x (b - a)), so the order is that of the vectors dir * n. */
void sort_radial(Arrangement &a, ArrEdge &e)
{
  if (e.uses.size() < 2) {
    return;
  }
  const Input &in = a.in;
  int axis = 0, up = 0;
  for (; axis < 3 && !up; axis++) {
    up = compare(in.points[e.b], in.points[e.a], axis);
  }
  axis--;
  auto normal = [&](const EdgeUse &u) {
    const I3 &n = in.planes[a.facets[int(u.facet)].plane].n;
    return I3(u.dir * n[0], u.dir * n[1], u.dir * n[2]);
  };
  /* the turn from x to y about a -> b; both are across it */
  auto turn = [&](const I3 &x, const I3 &y) { return up * cross(x, y).v[axis].sign(); };
  const I3 ref = normal(e.uses[0]);
  /* 0: along ref, 1: within half a turn, 2: against it, 3: the rest */
  auto sector = [&](const I3 &x) {
    const int t = turn(ref, x);
    return t > 0 ? 1 : t < 0 ? 3 : dot(ref, x).sign() > 0 ? 0 : 2;
  };
  std::sort(e.uses.begin(), e.uses.end(), [&](const EdgeUse &p, const EdgeUse &q) {
    const I3 x = normal(p), y = normal(q);
    const int sx = sector(x), sy = sector(y);
    return sx != sy ? sx < sy : turn(x, y) > 0;
  });
}

} // namespace

CsgCode build_arrangement(Arrangement &a)
{
  Vector<PolygonCuts> cuts;
  intersect_all(a, cuts);
  Merger m{a, {}, {}};
  Vector<PolygonFacet> pieces;
  for (uint32_t i = 0; i < a.in.polygons.size(); i++) {
    CsgCode c = subdivide(a.in, a.in.polygons[int(i)], cuts[int(i)], pieces);
    for (PolygonFacet &f : pieces) {
      c = c == CsgCode::Ok ? m.add(i, f) : c;
    }
    if (c != CsgCode::Ok) {
      return c;
    }
  }
  for (ArrEdge &e : a.edges) {
    sort_radial(a, e);
  }
  return CsgCode::Ok;
}

CsgCode arrange(std::span<const Operand> leaves, Arrangement &a, uint32_t *bad_leaf)
{
  a.leaf_count = uint32_t(leaves.size());
  CsgCode c = read_operands(leaves, a.in, bad_leaf);
  c = c == CsgCode::Ok ? build_arrangement(a) : c;
  return c == CsgCode::Ok ? label_arrangement(a) : c;
}

} // namespace ubertopo::csg
