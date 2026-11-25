#pragma once

/* The polygons of an evaluation (plan 2026-11-25-exact-booleans §4.1): the
 * faces of placed operands, with one plane each. */

#include <cstdint>
#include <span>

#include "ubertopo/csg/points.h"
#include "ubertopo/topo/complex.h"

namespace ubertopo::csg {

inline constexpr uint32_t kNone = 0xffffffffu;

enum class CsgCode : uint8_t {
  Ok,
  BadOperand,         /* not valid, or its regions are not current */
  UnsupportedOperand, /* surfaced faces, wires or inclusions */
  MixedFrames,        /* operands of different quanta */
  OutOfRange,         /* the operands' box is too large */
  Inconsistent,       /* internal: a self-check failed */
};

/** An exact placement, in quanta: output axis i is sign[i] * input axis axis[i], plus the
 * offset. */
struct GridPlacement {
  int8_t axis[3] = {0, 1, 2};
  int8_t sign[3] = {1, 1, 1};
  I3 offset{0, 0, 0};

  I3 apply_dir(const I3 &p) const
  {
    return I3(sign[0] * p[axis[0]], sign[1] * p[axis[1]], sign[2] * p[axis[2]]);
  }
  I3 apply(const I3 &p) const
  {
    const I3 q = apply_dir(p);
    return I3(q[0] + offset[0], q[1] + offset[1], q[2] + offset[2]);
  }
  /** +-1, or 0 if the axes are not a permutation. */
  int det() const
  {
    bool seen[3] = {};
    for (int i = 0; i < 3; i++) {
      if (axis[i] < 0 || axis[i] > 2 || (sign[i] != 1 && sign[i] != -1)) {
        return 0;
      }
      seen[axis[i]] = true;
    }
    if (!seen[0] || !seen[1] || !seen[2]) {
      return 0;
    }
    const int even = (axis[1] + 3 - axis[0]) % 3 == 1 ? 1 : -1;
    return even * sign[0] * sign[1] * sign[2];
  }
};

struct Operand {
  const topo::Complex<geom::Lattice> *k = nullptr;
  GridPlacement place;
};

/** A straight edge of an operand: an edge, a piece of its skeleton, or a
 * diagonal of T(f). */
struct SrcEdge {
  uint32_t leaf;
  uint32_t edge; /* in the operand; kNone for a diagonal */
  uint32_t a, b; /* points */
};

/** A planar face, or a triangle of T(f). Loops run with the interior on the
 * left, seen from the front. */
struct Polygon {
  uint32_t leaf, face, plane;
  uint32_t region[2]; /* front, back */
  int8_t sign;        /* the front normal along (+1) or against the plane's */
  int8_t axis;        /* a projection axis: the plane's normal is non-zero there */
  bool whole;         /* the face itself, not a triangle of it */
  I3 lo{0, 0, 0}, hi{0, 0, 0};
  litestl::util::Vector<uint32_t, 8> pts, edges; /* points; the SrcEdge from each to the next */
  litestl::util::Vector<uint32_t, 2> off;        /* loops, as offsets into pts */

  uint32_t loop_count() const
  {
    return uint32_t(off.size()) - 1;
  }
  /** Calls f(corner, next corner) for every edge. */
  template <typename F> void for_each_edge(F &&f) const
  {
    for (uint32_t l = 0; l + 1 < off.size(); l++) {
      const uint32_t b = off[int(l)], e = off[int(l) + 1];
      for (uint32_t i = b; i < e; i++) {
        f(i, i + 1 < e ? i + 1 : b);
      }
    }
  }
};

struct Input {
  PlaneTable planes;
  PointTable points;
  litestl::util::Vector<I3> ipos; /* of the points below ipos.size(): the input points */
  litestl::util::Vector<SrcEdge> edges;
  litestl::util::Vector<Polygon> polygons;
  litestl::util::Vector<uint32_t> leaf_begin; /* first polygon of each leaf, and the end */
  I3 shift{0, 0, 0};                          /* added to all coordinates */

  bool is_input(uint32_t point) const
  {
    return point < ipos.size();
  }
};

/** Reads the faces of @p leaves into @p out. *bad_leaf names the operand of a
 * failure. */
CsgCode read_operands(std::span<const Operand> leaves, Input &out, uint32_t *bad_leaf = nullptr);

} // namespace ubertopo::csg
