#include "ubertopo/csg/legal.h"

#include <algorithm>

namespace ubertopo::csg {

namespace {

using litestl::util::Vector;
using Points = Vector<uint32_t, 32>;

/* the points that make a cell illegal, sorted by index */
void offending(const PolygonFacet &cell, Points &out)
{
  out.clear();
  Points from;
  for (const HalfEdge &h : cell.edges) {
    from.append(h.from);
  }
  std::sort(from.begin(), from.end());
  for (int i = 1; i < int(from.size()); i++) {
    if (from[i] == from[i - 1]) {
      out.append(from[i]);
    }
  }
  for (int c = 0; c + 1 < int(cell.off.size()); c++) {
    if (cell.off[c + 1] - cell.off[c] == 2) {
      /* a slit by itself */
      out.append(cell.edges[int(cell.off[c])].from);
      out.append(cell.edges[int(cell.off[c])].to);
    }
  }
  for (uint32_t d : cell.dots) {
    out.append(d);
  }
  std::sort(out.begin(), out.end());
  out.resize(int(std::unique(out.begin(), out.end()) - out.begin()));
}

struct Cutter {
  const Input &in;
  PlanarGraph graph;
  std::span<const BoundarySegment> boundary;
  std::span<const uint32_t> dots;
  Vector<PolygonFacet> &cells;
  Vector<Diagonal> &diagonals;
  Points points; /* in lexicographic order */
  litestl::util::Map<uint32_t, uint32_t> rank;

  CsgCode build()
  {
    graph.clear();
    for (const BoundarySegment &s : boundary) {
      graph.add(s.p, s.q, s.cut, s.out);
    }
    for (const Diagonal &d : diagonals) {
      graph.add(d.p, d.q, kNone, 0);
    }
    return graph.cells(dots, cells);
  }

  bool legal() const
  {
    for (const PolygonFacet &c : cells) {
      if (!is_legal(c)) {
        return false;
      }
    }
    return true;
  }

  void sort_points()
  {
    for (const BoundarySegment &s : boundary) {
      points.append(s.p);
      points.append(s.q);
    }
    for (uint32_t d : dots) {
      points.append(d);
    }
    std::sort(points.begin(), points.end());
    points.resize(int(std::unique(points.begin(), points.end()) - points.begin()));
    std::sort(points.begin(), points.end(),
              [&](uint32_t p, uint32_t q) { return lex_compare(in.points[p], in.points[q]) < 0; });
    for (uint32_t i = 0; i < points.size(); i++) {
      rank.add(points[int(i)], i);
    }
  }

  bool crosses(uint32_t p, uint32_t q, uint32_t r, uint32_t s) const
  {
    return p != r && p != s && q != r && q != s && graph.turn(p, q, r) * graph.turn(p, q, s) < 0 &&
           graph.turn(r, s, p) * graph.turn(r, s, q) < 0;
  }

  /* p-q meets nothing but its ends */
  bool free(uint32_t p, uint32_t q) const
  {
    if (graph.has(p, q)) {
      return false;
    }
    for (uint32_t x : points) {
      if (x != p && x != q && graph.turn(p, q, x) == 0 && graph.between(p, q, x)) {
        return false;
      }
    }
    for (const BoundarySegment &s : boundary) {
      if (crosses(p, q, s.p, s.q)) {
        return false;
      }
    }
    for (const Diagonal &d : diagonals) {
      if (crosses(p, q, d.p, d.q)) {
        return false;
      }
    }
    return true;
  }

  static bool in_cell(const PolygonFacet &c, uint32_t p)
  {
    for (const HalfEdge &h : c.edges) {
      if (h.from == p) {
        return true;
      }
    }
    return std::find(c.dots.begin(), c.dots.end(), p) != c.dots.end();
  }

  /* both in a cell that isn't legal yet */
  bool useful(uint32_t p, uint32_t q) const
  {
    for (const PolygonFacet &c : cells) {
      if (!is_legal(c) && in_cell(c, p) && in_cell(c, q)) {
        return true;
      }
    }
    return false;
  }

  /* whether the diagonal lies in the face: it is an edge of some cell */
  bool inside(uint32_t p, uint32_t q) const
  {
    for (const PolygonFacet &c : cells) {
      for (const HalfEdge &h : c.edges) {
        if (h.from == p && h.to == q) {
          return true;
        }
      }
    }
    return false;
  }

  /* one pass over the candidates; all: from any point, else from offending
   * ones */
  CsgCode pass(bool all)
  {
    struct Pair {
      uint32_t i, j; /* ranks, i < j */
    };
    Vector<Pair> pairs;
    for (const PolygonFacet &c : cells) {
      if (is_legal(c)) {
        continue;
      }
      Points ends, off;
      for (const HalfEdge &h : c.edges) {
        ends.append(h.from);
      }
      for (uint32_t d : c.dots) {
        ends.append(d);
      }
      std::sort(ends.begin(), ends.end());
      ends.resize(int(std::unique(ends.begin(), ends.end()) - ends.begin()));
      offending(c, off);
      for (uint32_t p : all ? ends : off) {
        for (uint32_t q : ends) {
          if (p != q) {
            const uint32_t i = *rank.lookup_ptr(p), j = *rank.lookup_ptr(q);
            pairs.append({std::min(i, j), std::max(i, j)});
          }
        }
      }
    }
    std::sort(pairs.begin(), pairs.end(),
              [](const Pair &x, const Pair &y) { return x.i != y.i ? x.i < y.i : x.j < y.j; });
    for (const Pair &c : pairs) {
      const uint32_t p = points[int(c.i)], q = points[int(c.j)];
      if (!useful(p, q) || !free(p, q)) {
        continue;
      }
      diagonals.append({p, q});
      CsgCode code = build();
      if (code == CsgCode::Ok && !inside(p, q)) {
        diagonals.resize(diagonals.size() - 1);
        code = build();
      }
      if (code != CsgCode::Ok || legal()) {
        return code;
      }
    }
    return CsgCode::Ok;
  }

  CsgCode run()
  {
    CsgCode code = build();
    if (code != CsgCode::Ok || legal()) {
      return code;
    }
    sort_points();
    code = pass(false);
    if (code == CsgCode::Ok && !legal()) {
      code = pass(true);
    }
    if (code == CsgCode::Ok && !legal()) {
      code = CsgCode::Inconsistent;
    }
    /* take back what isn't needed */
    for (int i = int(diagonals.size()) - 1; i >= 0 && code == CsgCode::Ok; i--) {
      const Diagonal d = diagonals[i];
      for (int j = i; j + 1 < int(diagonals.size()); j++) {
        diagonals[j] = diagonals[j + 1];
      }
      diagonals.resize(diagonals.size() - 1);
      code = build();
      if (code == CsgCode::Ok && !legal()) {
        diagonals.append(d);
        for (int j = int(diagonals.size()) - 1; j > i; j--) {
          diagonals[j] = diagonals[j - 1];
        }
        diagonals[i] = d;
        code = build();
      }
    }
    return code;
  }
};

} // namespace

bool is_legal(const PolygonFacet &cell)
{
  Points off;
  offending(cell, off);
  return off.size() == 0;
}

CsgCode make_legal(const Input &in, uint32_t plane, int axis, std::span<const BoundarySegment> boundary,
                   std::span<const uint32_t> dots, Vector<PolygonFacet> &cells, Vector<Diagonal> &diagonals)
{
  diagonals.clear();
  Cutter c{in, PlanarGraph(in, plane, axis), boundary, dots, cells, diagonals, {}, {}};
  return c.run();
}

} // namespace ubertopo::csg
