#pragma once

/* A graph of segments in a plane, and its cells (plan
 * 2026-11-25-exact-booleans §4.3). Segments meet in their ends only. */

#include "litestl/util/map.h"
#include "ubertopo/csg/input.h"

namespace ubertopo::csg {

/** A directed piece of a line between consecutive points. */
struct HalfEdge {
  uint32_t from, to; /* points */
  uint32_t cut;      /* the line is the graph's plane and this one; kNone for a diagonal */
};

/** A cell of the graph. Its boundary runs with the cell on the left, seen from
 * where the plane's normal points: the cycle around it, then cycles around
 * holes, with slits run twice. */
struct PolygonFacet {
  litestl::util::Vector<HalfEdge, 8> edges;
  litestl::util::Vector<uint32_t, 4> off;  /* cycles, as offsets into edges */
  litestl::util::Vector<uint32_t, 1> dots; /* isolated points */
};

/** PlanarGraph::add: what isn't wanted of the sides of a segment. */
inline constexpr uint8_t kLeftOut = 1, kRightOut = 2;

class PlanarGraph {
public:
  /** @p axis: a projection axis; the plane's normal is non-zero there. */
  PlanarGraph(const Input &in, uint32_t plane, int axis)
      : in_(in), plane_(in.planes[plane]), axis_(axis), u_((axis + 1) % 3), v_((axis + 2) % 3),
        orient_(plane_.n[axis] > 0 ? 1 : -1)
  {
  }

  void clear();
  /** Adds the segment p-q. A segment may be added again, with more to leave
   * out. */
  void add(uint32_t p, uint32_t q, uint32_t cut, uint8_t out);
  bool has(uint32_t p, uint32_t q) const
  {
    return atom_of_.contains(key(p, q));
  }
  /** The cells that no segment leaves out; @p dots that aren't on the graph go
   * into the cells around them. Inconsistent: a dot is in no cell. */
  CsgCode cells(std::span<const uint32_t> dots, litestl::util::Vector<PolygonFacet> &out);

  /** The turn from a to b to c, seen from where the normal points. */
  int turn(uint32_t a, uint32_t b, uint32_t c) const
  {
    return orient_ * orient2d(in_.points[a], in_.points[b], in_.points[c], axis_);
  }
  /** Whether p, on the line of a and b, is strictly between them. */
  bool between(uint32_t a, uint32_t b, uint32_t p) const
  {
    for (const int x : {u_, v_}) {
      const int s = compare(in_.points[a], in_.points[p], x), t = compare(in_.points[p], in_.points[b], x);
      if (s != t) {
        return false;
      }
      if (s != 0) {
        return true;
      }
    }
    return false;
  }

private:
  struct Atom {
    uint32_t u, v; /* u < v */
    uint32_t cut;
    int8_t dir;  /* u -> v along cross(plane, cut): +1, against: -1 */
    uint8_t out; /* of u -> v */
  };

  const Input &in_;
  const Plane plane_;
  const int axis_, u_, v_;
  const int orient_; /* +1 if (u, v) sees the normal's side counter-clockwise */
  litestl::util::Vector<Atom, 32> atoms_;
  litestl::util::Map<uint64_t, uint32_t> atom_of_;
  /* half edges 2 a (u -> v) and 2 a + 1; outgoing at local vertices, sorted */
  litestl::util::Map<uint32_t, uint32_t> local_;
  litestl::util::Vector<litestl::util::Vector<uint32_t, 6>> out_;
  litestl::util::Vector<uint32_t, 32> vertex_; /* of a local vertex */
  litestl::util::Vector<uint32_t, 64> next_, cycle_;
  litestl::util::Vector<uint32_t, 16> cycle_start_;

  static uint64_t key(uint32_t p, uint32_t q)
  {
    return (uint64_t(p < q ? p : q) << 32) | (p < q ? q : p);
  }
  uint32_t from(uint32_t h) const
  {
    const Atom &a = atoms_[int(h >> 1)];
    return h & 1 ? a.v : a.u;
  }
  uint32_t to(uint32_t h) const
  {
    return from(h ^ 1);
  }
  int dir(uint32_t h) const
  {
    const int d = atoms_[int(h >> 1)].dir;
    return h & 1 ? -d : d;
  }
  uint32_t cut(uint32_t h) const
  {
    return atoms_[int(h >> 1)].cut;
  }
  uint32_t local(uint32_t p) const
  {
    return *local_.lookup_ptr(p);
  }
  int half(uint32_t h) const;
  bool before(uint32_t a, uint32_t b) const;
  bool contains(uint32_t c, uint32_t p) const;
  void trace();
};

} // namespace ubertopo::csg
