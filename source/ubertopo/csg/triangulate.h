#pragma once

/* Triangles of a face of a result, between its exact points (plan
 * 2026-11-25-boolean-repair §3.1). */

#include <array>

#include "ubertopo/csg/merge.h"

namespace ubertopo::csg {

using PointTriangle = std::array<uint32_t, 3>;

/** Triangles of @p f, seen from its front: of a whole face those of its source,
 * else free diagonals in lexicographic order. Inconsistent: they leave a cell
 * that isn't a triangle. */
CsgCode triangulate_face(const Arrangement &a, const ResultFace &f,
                         litestl::util::Vector<PointTriangle> &out);

} // namespace ubertopo::csg
