/* The labels of a rounded result's cells, from winding numbers (plan
 * 2026-11-25-boolean-repair §2, §3.3). With labels L, the faces that have L on
 * one side are a closed surface, however they cross after rounding. w(L) is
 * its winding number; over all labels they add up to 1 in every cell. */

#include <algorithm>

#include "ubertopo/csg/ray.h"
#include "ubertopo/csg/repair.h"

namespace ubertopo::csg {

namespace {

using litestl::util::Vector;

struct Winder {
  const Arrangement &a;
  Vector<uint32_t> labels;   /* those there are; void first */
  Vector<uint32_t> index_of; /* by label */
  int n = 0;                 /* labels */
  Vector<int32_t> w;         /* by facet, side and label */
  Vector<int32_t> jump;      /* by facet and label: side 0 minus side 1 */
  Vector<uint8_t> known;     /* by facet and side */
  Vector<uint32_t> queue;    /* facet sides */
  Vector<int32_t> tmp;
  bool ok = true;

  int32_t *at(uint32_t s)
  {
    return &w[int(s) * n];
  }

  void find_labels()
  {
    labels.append(kVoid);
    for (const Polygon &g : a.in.polygons) {
      for (uint32_t l : g.region) {
        labels.append(l);
      }
    }
    std::sort(labels.begin(), labels.end());
    labels.resize(int(std::unique(labels.begin(), labels.end()) - labels.begin()));
    n = int(labels.size());
    index_of.resize(labels[n - 1] + 1);
    for (int i = 0; i < n; i++) {
      index_of[int(labels[i])] = uint32_t(i);
    }
  }

  void find_jumps()
  {
    const int nf = int(a.facets.size());
    jump.resize(nf * n);
    w.resize(2 * nf * n);
    known.resize(2 * nf);
    std::fill(jump.begin(), jump.end(), 0);
    std::fill(known.begin(), known.end(), uint8_t(0));
    for (int f = 0; f < nf; f++) {
      for (uint32_t s : a.facets[f].sources) {
        const Polygon &g = a.in.polygons[int(s)];
        jump[f * n + int(index_of[int(g.region[0])])] += g.sign;
        jump[f * n + int(index_of[int(g.region[1])])] -= g.sign;
      }
    }
    tmp.resize(n);
  }

  void set(uint32_t s, const int32_t *x)
  {
    if (!known[int(s)]) {
      known[int(s)] = 1;
      std::copy(x, x + n, at(s));
      queue.append(s);
    }
    else {
      ok = ok && std::equal(x, x + n, at(s));
    }
  }

  void flood()
  {
    while (queue.size() > 0 && ok) {
      const uint32_t s = queue.pop_back(), f = s >> 1, side = s & 1;
      for (int i = 0; i < n; i++) {
        tmp[i] = at(s)[i] + (side == 0 ? -1 : 1) * jump[int(f) * n + i];
      }
      set(2 * f + (1 - side), tmp.data());
      for (const FacetEdge &fe : a.facets[int(f)].edges) {
        const auto &uses = a.edges[int(fe.edge)].uses;
        const int m = int(uses.size());
        for (int i = 0; i < m; i++) {
          if (uses[i].facet != f) {
            continue;
          }
          /* the facet's side on the other end of the wedge */
          if (uses[i].side_after() == side) {
            const EdgeUse &u = uses[(i + 1) % m];
            set(2 * u.facet + (1 - u.side_after()), at(s));
          }
          else {
            const EdgeUse &u = uses[(i + m - 1) % m];
            set(2 * u.facet + u.side_after(), at(s));
          }
        }
      }
    }
  }

  /* the use that the wedge with @p dir follows; -1 if it is in a facet's plane */
  int wedge(const ArrEdge &e, const I3 &dir) const
  {
    const Input &in = a.in;
    auto normal = [&](const EdgeUse &u) {
      const I3 &p = in.planes[a.facets[int(u.facet)].plane].n;
      return I3(u.dir * p[0], u.dir * p[1], u.dir * p[2]);
    };
    int axis = 0, up = 0;
    for (; axis < 3 && !up; axis++) {
      up = compare(in.points[e.b], in.points[e.a], axis);
    }
    axis--;
    const int m = int(e.uses.size());
    for (const EdgeUse &u : e.uses) {
      if (dot(normal(u), dir).sign() == 0) {
        return -1;
      }
    }
    for (int i = 0; i < m; i++) {
      const I3 x = normal(e.uses[i]), y = normal(e.uses[(i + 1) % m]);
      const bool after = dot(x, dir).sign() > 0, until = dot(y, dir).sign() < 0;
      /* from x to y: less than, exactly or more than half a turn; all around if
       * there is one facet only */
      const int turn = up * cross(x, y).v[axis].sign();
      if (m == 1 || (turn > 0 ? after && until : turn == 0 ? after : after || until)) {
        return i;
      }
    }
    return -1;
  }

  /* one cell of the facets connected with f */
  void start(uint32_t f)
  {
    const Input &in = a.in;
    const ArrEdge &e = a.edges[int(a.facets[int(f)].edges[0].edge)];
    const Mid o(in.points[e.a], in.points[e.b]);
    Vector<int8_t> sides;
    for (const Polygon &g : in.polygons) {
      sides.append(int8_t(classify(o, in.planes[g.plane])));
    }
    for (int64_t t = 1; t <= kMaxRay; t++) {
      const I3 dir = ray_direction(t);
      const int i = wedge(e, dir);
      bool bad = i < 0;
      std::fill(tmp.begin(), tmp.end(), 0);
      tmp[0] = 1;
      for (uint32_t p = 0; p < in.polygons.size() && !bad; p++) {
        const Polygon &g = in.polygons[int(p)];
        const int hit = ray_hits(in, o, dir, g, sides[int(p)]);
        bad = hit < 0;
        if (hit > 0) {
          const uint32_t near = sides[int(p)] == g.sign ? 0 : 1;
          tmp[int(index_of[int(g.region[near])])]++;
          tmp[int(index_of[int(g.region[1 - near])])]--;
        }
      }
      if (!bad) {
        const EdgeUse &u = e.uses[i];
        set(2 * u.facet + u.side_after(), tmp.data());
        return;
      }
    }
    ok = false;
  }

  CsgCode run(std::span<const PersistentId> names, Vector<uint32_t> &solid, RepairCounts &counts)
  {
    find_labels();
    find_jumps();
    const uint32_t ns = 2 * uint32_t(a.facets.size());
    for (uint32_t s = 0; s < ns && ok; s++) {
      if (!known[int(s)]) {
        start(s >> 1);
        flood();
        /* (all sides of connected facets are reached) */
        ok = ok && known[int(s)];
      }
    }
    if (!ok) {
      return CsgCode::Inconsistent;
    }
    solid.clear();
    for (uint32_t s = 0; s < ns; s++) {
      const int32_t *x = at(s);
      int best = 0, candidates = 0, sum = 0;
      for (int i = 0; i < n; i++) {
        sum += x[i];
        candidates += x[i] > 0;
        if (x[i] > x[best] ||
            (x[i] == x[best] && best > 0 && names.size() > 0 && names[labels[i]] < names[labels[best]]))
        {
          best = i;
        }
      }
      if (sum != 1) {
        return CsgCode::Inconsistent;
      }
      counts.unclear += candidates > 1;
      solid.append(labels[best]);
    }
    return CsgCode::Ok;
  }
};

} // namespace

CsgCode label_by_winding(const Arrangement &a, std::span<const PersistentId> labels, Vector<uint32_t> &solid,
                         RepairCounts &counts)
{
  Winder x{a, {}, {}, 0, {}, {}, {}, {}, {}};
  return x.run(labels, solid, counts);
}

} // namespace ubertopo::csg
