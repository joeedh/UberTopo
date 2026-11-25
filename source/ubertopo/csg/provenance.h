#pragma once

/* Which elements of the operands the elements of a result are parts of (plan
 * 2026-11-25-boolean-repair §4). */

#include "ubertopo/csg/names.h"

namespace ubertopo::csg {

struct Provenance {
  struct Vertex {
    uint32_t leaf = kNone, vert = kNone; /* leaf kNone: from none */
  };
  struct Edge {
    uint32_t leaf = kNone, edge = kNone;
    double part = 0; /* of the operand's edge */
  };
  struct Face {
    uint32_t leaf = kNone, face = kNone;
    bool reversed = false;
    double part = 0;
  };

  litestl::util::Vector<Vertex> verts; /* by point */
  litestl::util::Vector<Edge> edges;   /* by result edge */
  litestl::util::Vector<Face> faces;   /* by result face */
};

/** Of a result of operands. Without names (of vertices and edges): faces only. */
void find_provenance(const Arrangement &a, const Result &result, const Names &names, Provenance &out);

/** Of a result whose arrangement is that of another result's faces: what
 * Polygon::face, SrcEdge::edge and SrcVert::vert are there (a point) is known
 * by @p before. Pieces of an edge share what it has. Faces share what their
 * @p root has, a face of a first result (@p first); what was outside of a solid
 * still is. */
void compose_provenance(const Provenance &before, const Provenance &first, std::span<const uint32_t> root,
                        const Arrangement &a, const Result &result, const Names &names, Provenance &out);

} // namespace ubertopo::csg
