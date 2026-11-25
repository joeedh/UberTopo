#pragma once

/* What an edit of a tree did to the names of its result (plan
 * 2026-11-25-exact-booleans §4.6). */

#include "ubertopo/csg/evaluate.h"

namespace ubertopo::csg {

/** A face of the new result, and the face of the old one that it continues. */
struct FaceLineage {
  PersistentId now, before;
};

struct NameChanges {
  litestl::util::Vector<PersistentId> kept, gone, added; /* sorted */
};

struct CsgDiff {
  NameChanges vertices, faces, regions;
  /** Faces of the new result that have no vertex in common with the old face
   * of their name (if there is one), but with another piece of their source:
   * split off from it, or exchanged with it. .before is the one with the most
   * vertices in common, of several the one with the smallest name. Sorted by
   * .now. */
  litestl::util::Vector<FaceLineage> moved;
};

/** False (and nothing in @p out) unless both evaluations were successful. */
bool csg_diff(const BoolReport &before, const BoolReport &now, CsgDiff &out);

} // namespace ubertopo::csg
