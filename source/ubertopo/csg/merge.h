#pragma once

/* From the kept facets to the faces of the result, still with exact points
 * (plan 2026-11-25-exact-booleans §4.5). */

#include "ubertopo/csg/select.h"

namespace ubertopo::csg {

/** A face of the result: a piece of its source, or (whole) the projected
 * source face itself. */
struct ResultFace {
  uint32_t source = kNone; /* polygon; of a whole face, its first triangle */
  int8_t front = 0;        /* the front is the plane's positive (+1) or negative side */
  bool reversed = false;   /* the front is the source's back */
  bool whole = false;
  uint32_t label[2] = {kNone, kNone}; /* front, back; air included */
  uint32_t solid[2] = {kNone, kNone}; /* the same, solid labels only */
  litestl::util::Vector<uint32_t, 8> pts; /* loops, the outer one first; the face on the left from the front */
  litestl::util::Vector<uint32_t, 2> off;
};

/** An edge of the result, with its faces counter-clockwise about a -> b. */
struct ResultEdge {
  uint32_t a, b; /* points, a < b */
  uint32_t arr[2]; /* the arrangement's edge at each end; kNone for a diagonal */
  litestl::util::Vector<EdgeUse, 2> uses; /* facet: a result face; dir: +1 if the face's loop runs a -> b */
};

struct MergeStats {
  uint32_t kept_facets = 0, groups = 0, dropped_points = 0;
  uint32_t illegal = 0, diagonals = 0; /* groups that were cut up, and by what */
  uint32_t whole = 0;
};

struct Result {
  litestl::util::Vector<ResultFace> faces;
  litestl::util::Vector<ResultEdge> edges;
  MergeStats stats;
};

/** Projected source faces that nothing cuts stay one face, except those that
 * @p split (by outline; may be empty) has. */
CsgCode merge_facets(const Arrangement &a, const Expression &e, const litestl::util::Vector<Selected> &selected,
                     std::span<const uint8_t> split, Result &out);

} // namespace ubertopo::csg
