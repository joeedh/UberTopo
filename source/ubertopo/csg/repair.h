#pragma once

/* One round of the repair of a result that can't be rounded: its rounded faces
 * as polygons, arranged and labelled again (plan 2026-11-25-boolean-repair). */

#include <span>

#include "ubertopo/csg/provenance.h"

namespace ubertopo::csg {

/** A result, rounded. */
struct Rounded {
  const Arrangement &a;
  const Result &result;
  std::span<const I3> positions; /* by point of a */
  std::span<const uint8_t> used; /* by point of a: in the result */
  const Names &names;            /* may be empty */
  const Provenance &provenance;
};

struct RepairCounts {
  uint32_t merged = 0;  /* points that round to another one's position */
  uint32_t broken = 0;  /* faces whose exact triangles were taken */
  uint32_t unclear = 0; /* cells with more than one candidate for their label */
};

/** The faces of @p r as polygons of one leaf, in @p in. The region of a side is
 * its solid label; Polygon::face, SrcEdge::edge and SrcVert::vert are the face,
 * the edge and a point of r. @p point_of, by point of r: that of in. With
 * @p exact, faces that bent or lost a point are their exact triangles, moved. */
CsgCode read_rounded(const Rounded &r, bool exact, Input &in, litestl::util::Vector<uint32_t> &point_of,
                     RepairCounts &counts);

/** A hash of the polygons, that tells whether a round is the same as another. */
ident::PersistentId polygons_hash(const Input &in);

/** The solid labels of both sides of every facet (by facet and side) of a
 * self-arranged leaf, from winding numbers. @p labels: names that decide ties;
 * may be empty. */
CsgCode label_by_winding(const Arrangement &a, std::span<const PersistentId> labels,
                         litestl::util::Vector<uint32_t> &solid, RepairCounts &counts);

/** What the arrangement @p a of r's faces keeps. */
void select_repaired(const Arrangement &a, const litestl::util::Vector<uint32_t> &solid, const Rounded &r,
                     const Expression &e, litestl::util::Vector<Selected> &out);

/** Joins pieces of one face (@p root, by result face) to a face again, where its
 * rounded outline allows it. @p members: the faces of @p result by face of
 * @p out. False: there is nothing to join. */
bool join_faces(const Result &result, std::span<const I3> positions, std::span<const uint32_t> root,
                Result &out, litestl::util::Vector<litestl::util::Vector<uint32_t, 2>> &members);

} // namespace ubertopo::csg
