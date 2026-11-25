#include "ubertopo/csg/merge.h"

#include <algorithm>

#include "ubertopo/csg/legal.h"

namespace ubertopo::csg {

namespace {

using litestl::util::Vector;

/* a chain of arrangement edges with the same facets */
struct Chain {
  uint32_t a, b;         /* a < b */
  uint32_t first;        /* an arrangement edge of it */
  bool along;            /* first runs along a -> b */
  uint32_t arr[2];       /* the edges at a and at b */
};

struct Merger {
  const Arrangement &a;
  const Expression &e;
  const Vector<Selected> &sel;
  Result &out;
  std::span<const uint8_t> split; /* outlines */

  Vector<uint32_t> parent;                    /* of facets: groups */
  Vector<uint8_t> dissolved;                  /* edges inside a group */
  Vector<Vector<EdgeUse, 4>> uses;            /* of edges: the kept facets */
  Vector<Vector<uint32_t, 4>> at;             /* of points: the edges of the result */
  Vector<uint8_t> dropped;                    /* points within a chain */
  Vector<Vector<uint32_t, 2>> group_dots;     /* by group root */
  Vector<Chain> chains;
  Vector<Vector<uint32_t, 8>> group_chains;   /* by group root */
  Vector<uint8_t> whole;                      /* group roots that a whole face covers */

  uint32_t root(uint32_t f)
  {
    while (parent[int(f)] != f) {
      parent[int(f)] = parent[int(parent[int(f)])];
      f = parent[int(f)];
    }
    return f;
  }

  const Polygon &source(uint32_t f) const
  {
    return a.in.polygons[int(sel[int(f)].source)];
  }
  uint32_t solid(uint32_t f, uint32_t side) const
  {
    return e.solid_part(sel[int(f)].label[side]);
  }
  /* pieces of one face (and where nothing else is kept between them, with the
   * same solid labels) */
  bool same_face(uint32_t f, uint32_t g) const
  {
    const Polygon &p = source(f), &q = source(g);
    return p.leaf == q.leaf && p.face == q.face;
  }

  void join()
  {
    for (uint32_t f = 0; f < a.facets.size(); f++) {
      parent.append(f);
      group_dots.append({});
      group_chains.append({});
      whole.append(0);
    }
    for (const ArrEdge &x : a.edges) {
      Vector<EdgeUse, 4> kept;
      for (const EdgeUse &u : x.uses) {
        if (sel[int(u.facet)].kept) {
          kept.append(u);
        }
      }
      bool join = false;
      if (kept.size() == 2) {
        const uint32_t f = kept[0].facet, g = kept[1].facet;
        join = a.facets[int(f)].plane == a.facets[int(g)].plane && same_face(f, g);
        if (join) {
          parent[int(root(f))] = root(g);
        }
      }
      dissolved.append(join);
      uses.append(std::move(kept));
    }
  }

  bool own_vertex(uint32_t p, const Vector<EdgeUse, 4> &us) const
  {
    for (const EdgeUse &u : us) {
      const Polygon &g = source(u.facet);
      if (std::find(g.pts.begin(), g.pts.end(), p) != g.pts.end()) {
        return true;
      }
    }
    return false;
  }

  /* two edges at p, the only ones there, that could be one. (The groups around
   * them are the same: a boundary that comes to p goes on.) */
  bool straight(uint32_t p, uint32_t x, uint32_t y) const
  {
    const ArrEdge &ex = a.edges[int(x)];
    /* the line of y, in the plane of one of its facets */
    const Facet &f = a.facets[int(uses[int(y)][0].facet)];
    uint32_t cut = kNone;
    for (const FacetEdge &fe : f.edges) {
      if (fe.edge == y) {
        cut = fe.cut;
      }
    }
    const HPoint &far = a.in.points[ex.a == p ? ex.b : ex.a];
    return classify(far, a.in.planes[f.plane]) == 0 && classify(far, a.in.planes[cut]) == 0;
  }

  /* points in the middle of a group, where something else touches it */
  void find_points()
  {
    const uint32_t np = a.in.points.size();
    for (uint32_t p = 0; p < np; p++) {
      at.append({});
      dropped.append(0);
    }
    Vector<Vector<uint32_t, 2>> inner; /* points: groups they are in the middle of */
    inner.resize(np);
    auto add_inner = [&](uint32_t p, uint32_t f) {
      auto &v = inner[int(p)];
      const uint32_t g = root(f);
      if (std::find(v.begin(), v.end(), g) == v.end()) {
        v.append(g);
      }
    };
    for (uint32_t i = 0; i < a.edges.size(); i++) {
      const ArrEdge &x = a.edges[int(i)];
      if (uses[int(i)].size() == 0) {
        continue;
      }
      if (dissolved[int(i)]) {
        add_inner(x.a, uses[int(i)][0].facet);
        add_inner(x.b, uses[int(i)][0].facet);
      }
      else {
        at[int(x.a)].append(i);
        at[int(x.b)].append(i);
      }
    }
    for (uint32_t f = 0; f < a.facets.size(); f++) {
      if (sel[int(f)].kept) {
        for (uint32_t d : a.facets[int(f)].dots) {
          add_inner(d, f);
        }
      }
    }
    for (uint32_t p = 0; p < np; p++) {
      const auto &es = at[int(p)];
      for (uint32_t g : inner[int(p)]) {
        bool on = es.size() == 0; /* nothing left there */
        for (uint32_t x : es) {
          for (const EdgeUse &u : uses[int(x)]) {
            on = on || root(u.facet) == g;
          }
        }
        if (!on) {
          group_dots[int(g)].append(p);
        }
      }
      if (es.size() == 2 && !own_vertex(p, uses[int(es[0])]) && !own_vertex(p, uses[int(es[1])]) &&
          straight(p, es[0], es[1]))
      {
        dropped[int(p)] = 1;
        out.stats.dropped_points++;
      }
    }
  }

  uint32_t other(uint32_t x, uint32_t p) const
  {
    return a.edges[int(x)].a == p ? a.edges[int(x)].b : a.edges[int(x)].a;
  }

  void make_chains()
  {
    Vector<uint8_t> done;
    for (uint32_t i = 0; i < a.edges.size(); i++) {
      done.append(dissolved[int(i)] || uses[int(i)].size() == 0);
    }
    for (uint32_t i = 0; i < a.edges.size(); i++) {
      if (done[int(i)]) {
        continue;
      }
      uint32_t ends[2] = {a.edges[int(i)].a, a.edges[int(i)].b};
      done[int(i)] = 1;
      uint32_t last[2] = {i, i};
      for (int k = 0; k < 2; k++) {
        uint32_t &p = ends[k], &x = last[k];
        while (dropped[int(p)]) {
          x = at[int(p)][0] == x ? at[int(p)][1] : at[int(p)][0];
          done[int(x)] = 1;
          p = other(x, p);
        }
      }
      const uint32_t id = uint32_t(chains.size());
      chains.append({std::min(ends[0], ends[1]), std::max(ends[0], ends[1]), i, ends[0] < ends[1], {}});
      Chain &ch = chains[int(id)];
      ch.arr[0] = last[ch.along ? 0 : 1];
      ch.arr[1] = last[ch.along ? 1 : 0];
      for (const EdgeUse &u : uses[int(i)]) {
        auto &gc = group_chains[int(root(u.facet))];
        if (gc.size() == 0 || gc[int(gc.size()) - 1] != id) {
          gc.append(id);
        }
      }
    }
  }

  /* projected faces that nothing cuts */
  void find_whole()
  {
    for (uint32_t i = 0; i < a.in.outlines.size(); i++) {
      if (i >= split.size() || !split[i]) {
        try_whole(a.in.outlines[int(i)].first, a.in.outlines[int(i)].first + a.in.outlines[int(i)].count);
      }
    }
  }

  Vector<Vector<uint32_t, 1>> facets_of; /* by polygon */

  void try_whole(uint32_t begin, uint32_t end)
  {
    for (uint32_t i = begin; i < end; i++) {
      if (facets_of[int(i)].size() != 1) {
        return;
      }
      const uint32_t f = facets_of[int(i)][0];
      const Facet &fc = a.facets[int(f)];
      if (!sel[int(f)].kept || sel[int(f)].source != i || fc.edges.size() != 3 || fc.dots.size() != 0) {
        return;
      }
      /* a diagonal has the two triangles only (so their labels agree) */
      for (int x = 0; x < 3; x++) {
        const uint32_t se = a.in.polygons[int(i)].edges[x];
        if (a.in.edges[int(se)].edge != kNone) {
          continue;
        }
        for (const FacetEdge &fe : fc.edges) {
          const ArrEdge &ae = a.edges[int(fe.edge)];
          if (ae.a == a.in.edges[int(se)].a && ae.b == a.in.edges[int(se)].b && uses[int(fe.edge)].size() != 2) {
            return;
          }
        }
      }
    }
    for (uint32_t i = begin; i < end; i++) {
      whole[int(root(facets_of[int(i)][0]))] = 1;
    }
    whole_faces.append(begin);
  }
  Vector<uint32_t> whole_faces; /* first polygons */

  /* ---- output ---- */

  litestl::util::Map<uint64_t, uint32_t> half; /* (chain, dir) of a group: result face */

  void set_up(ResultFace &r, uint32_t f)
  {
    const Polygon &g = source(f);
    const uint32_t fs = g.sign > 0 ? 0 : 1; /* the side of the source's front */
    r.source = sel[int(f)].source;
    /* the front faces out of a solid, if it does in the operand */
    const ExprLeaf &l = e.leaves[int(g.leaf)];
    const bool was = e.solid(l.label[int(g.region[0])]), was_back = e.solid(l.label[int(g.region[1])]);
    const bool is = solid(f, fs) != kVoid, is_back = solid(f, 1 - fs) != kVoid;
    r.reversed = was != was_back && is != is_back && was != is;
    const uint32_t front = r.reversed ? 1 - fs : fs;
    r.front = front == 0 ? 1 : -1;
    for (uint32_t k = 0; k < 2; k++) {
      const uint32_t side = k == 0 ? front : 1 - front;
      r.label[k] = sel[int(f)].label[side];
      r.solid[k] = solid(f, side);
    }
  }

  CsgCode add_group(uint32_t g)
  {
    const Polygon &src = source(g);
    Vector<BoundarySegment> boundary;
    for (uint32_t c : group_chains[int(g)]) {
      const Chain &ch = chains[int(c)];
      uint8_t sides = 0; /* bit 0: the group is on the left of a -> b; bit 1: on the right */
      uint32_t cut = kNone;
      for (const EdgeUse &u : uses[int(ch.first)]) {
        if (root(u.facet) != g) {
          continue;
        }
        sides |= (u.dir > 0) == ch.along ? 1 : 2;
        for (const FacetEdge &fe : a.facets[int(u.facet)].edges) {
          cut = fe.edge == ch.first ? fe.cut : cut;
        }
      }
      boundary.append({ch.a, ch.b, cut, uint8_t((sides & 1 ? 0 : kLeftOut) | (sides & 2 ? 0 : kRightOut))});
    }
    Vector<PolygonFacet> cells;
    Vector<Diagonal> diagonals;
    const uint32_t plane = a.facets[int(g)].plane;
    const auto &dots = group_dots[int(g)];
    const CsgCode code = make_legal(a.in, plane, src.axis, {boundary.data(), size_t(boundary.size())},
                                    {dots.data(), size_t(dots.size())}, cells, diagonals);
    if (code != CsgCode::Ok || (diagonals.size() == 0 && cells.size() != 1)) {
      return CsgCode::Inconsistent;
    }
    out.stats.illegal += diagonals.size() > 0;
    out.stats.diagonals += uint32_t(diagonals.size());
    litestl::util::Map<uint64_t, uint32_t> face_at; /* by directed segment, the face on its left */
    auto key = [](uint32_t p, uint32_t q) { return (uint64_t(p) << 32) | q; };
    for (const PolygonFacet &cell : cells) {
      const uint32_t id = uint32_t(out.faces.size());
      ResultFace r;
      r.whole = false;
      set_up(r, g);
      r.off.append(0);
      for (int c = 0; c + 1 < int(cell.off.size()); c++) {
        const uint32_t b = cell.off[c], n = cell.off[c + 1] - b;
        for (uint32_t i = 0; i < n; i++) {
          const HalfEdge &h = cell.edges[int(b + (r.front > 0 ? i : n - 1 - i))];
          r.pts.append(r.front > 0 ? h.from : h.to);
        }
        r.off.append(uint32_t(r.pts.size()));
      }
      for (const HalfEdge &h : cell.edges) {
        face_at.add(key(h.from, h.to), id);
      }
      out.faces.append(std::move(r));
    }
    for (const Diagonal &d : diagonals) {
      const uint32_t lo = std::min(d.p, d.q), hi = std::max(d.p, d.q);
      const int8_t s = out.faces[int(*face_at.lookup_ptr(key(lo, hi)))].front;
      ResultEdge x{lo, hi, {kNone, kNone}, {}};
      x.uses.append({*face_at.lookup_ptr(key(lo, hi)), s});
      x.uses.append({*face_at.lookup_ptr(key(hi, lo)), int8_t(-s)});
      out.edges.append(std::move(x));
    }
    for (uint32_t c : group_chains[int(g)]) {
      const Chain &ch = chains[int(c)];
      for (const EdgeUse &u : uses[int(ch.first)]) {
        if (root(u.facet) == g) {
          const bool left = (u.dir > 0) == ch.along;
          half.add_overwrite((uint64_t(c) << 34) | (uint64_t(g) << 2) | (left ? 1 : 0),
                             *face_at.lookup_ptr(left ? key(ch.a, ch.b) : key(ch.b, ch.a)));
        }
      }
    }
    return CsgCode::Ok;
  }

  void add_whole(uint32_t begin)
  {
    const uint32_t f0 = facets_of[int(begin)][0];
    ResultFace r;
    r.whole = true;
    set_up(r, f0);
    const uint32_t id = uint32_t(out.faces.size());
    const Outline &o = a.in.outlines[int(a.in.polygons[int(begin)].outline)];
    r.off = o.off;
    for (int l = 0; l + 1 < int(o.off.size()); l++) {
      for (uint32_t i = o.off[l]; i < o.off[l + 1]; i++) {
        r.pts.append(o.pts[int(r.reversed ? o.off[l + 1] - 1 - (i - o.off[l]) : i)]);
      }
    }
    for (uint32_t i = begin; i < begin + o.count; i++) {
      const uint32_t g = root(facets_of[int(i)][0]);
      for (uint32_t c : group_chains[int(g)]) {
        for (const EdgeUse &u : uses[int(chains[int(c)].first)]) {
          if (root(u.facet) == g) {
            const bool left = (u.dir > 0) == chains[int(c)].along;
            half.add_overwrite((uint64_t(c) << 34) | (uint64_t(g) << 2) | (left ? 1 : 0), id);
          }
        }
      }
    }
    out.stats.whole++;
    out.faces.append(std::move(r));
  }

  CsgCode run()
  {
    join();
    find_points();
    make_chains();
    facets_of.resize(a.in.polygons.size());
    for (uint32_t f = 0; f < a.facets.size(); f++) {
      out.stats.kept_facets += sel[int(f)].kept;
      for (uint32_t s : a.facets[int(f)].sources) {
        facets_of[int(s)].append(f);
      }
    }
    find_whole();
    for (uint32_t f = 0; f < a.facets.size(); f++) {
      if (sel[int(f)].kept && root(f) == f) {
        out.stats.groups++;
        const CsgCode c = whole[int(f)] ? CsgCode::Ok : add_group(f);
        if (c != CsgCode::Ok) {
          return c;
        }
      }
    }
    for (uint32_t b : whole_faces) {
      add_whole(b);
    }
    for (uint32_t c = 0; c < chains.size(); c++) {
      const Chain &ch = chains[int(c)];
      ResultEdge x{ch.a, ch.b, {ch.arr[0], ch.arr[1]}, {}};
      const auto &us = uses[int(ch.first)];
      for (int i = 0; i < int(us.size()); i++) {
        const EdgeUse &u = us[ch.along ? i : int(us.size()) - 1 - i];
        const bool left = (u.dir > 0) == ch.along;
        const uint32_t *f = half.lookup_ptr((uint64_t(c) << 34) | (uint64_t(root(u.facet)) << 2) | (left ? 1 : 0));
        if (!f) {
          return CsgCode::Inconsistent;
        }
        /* seen from its front, the face is on the left of its loops */
        const uint32_t side = front_side(u.facet, *f);
        x.uses.append({*f, int8_t((left ? 1 : -1) * (side == 0 ? 1 : -1))});
      }
      /* the diagonals of a whole face are gone */
      if (x.uses.size() == 2 && x.uses[0].facet == x.uses[1].facet && out.faces[int(x.uses[0].facet)].whole) {
        continue;
      }
      out.edges.append(std::move(x));
    }
    return CsgCode::Ok;
  }

  /* the side of facet f's plane that its result face's front is on */
  uint32_t front_side(uint32_t f, uint32_t face) const
  {
    const uint32_t fs = source(f).sign > 0 ? 0 : 1;
    return out.faces[int(face)].reversed ? 1 - fs : fs;
  }
};

} // namespace

CsgCode merge_facets(const Arrangement &a, const Expression &e, const Vector<Selected> &selected,
                     std::span<const uint8_t> split, Result &out)
{
  Merger m{a, e, selected, out, split};
  return m.run();
}

} // namespace ubertopo::csg
