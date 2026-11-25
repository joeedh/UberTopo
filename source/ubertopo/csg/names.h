#pragma once

/* Names of the elements of a result: from the elements of the operands that
 * meet there, so that an edit elsewhere in the tree renames nothing (plan
 * 2026-11-25-exact-booleans §4.6). */

#include "ubertopo/csg/merge.h"
#include "ubertopo/ident/id.h"

namespace ubertopo::csg {

using ident::PersistentId;

/** A region with some label. */
struct LabelSource {
  uint32_t leaf, region;
};

/** Names of a result, whose faces are the polygons of another arrangement
 * (repair.h). */
struct PreviousNames {
  const Result &result;
  std::span<const PersistentId> vertex; /* by point */
  std::span<const PersistentId> face;
  std::span<const uint32_t> point_of; /* by point: that of the arrangement */
};

/** What names are made of. */
struct NameSource {
  std::span<const Operand> operands;     /* by leaf */
  std::span<const PersistentId> leaf_ids; /* by leaf */
  std::span<const PersistentId> labels;   /* by label */
  std::span<const LabelSource> label_source; /* by label: the first region with it */
  const PreviousNames *previous = nullptr; /* instead of the operands'; elements that stay keep theirs */
};

struct Names {
  litestl::util::Vector<PersistentId> vertex; /* by point; kNoId if it is not in the result */
  litestl::util::Vector<PersistentId> face;   /* by result face */
  litestl::util::Vector<PersistentId> region; /* by region of the complex */
  /* what elements of the result are in an operand; kNone for none */
  litestl::util::Vector<uint32_t> vertex_src; /* by point: in Input::verts */
  litestl::util::Vector<uint32_t> edge_src;   /* by result edge: in Input::edges */
  uint32_t ordinals = 0; /* vertices that share a name with others, beyond the first */
  uint32_t weak = 0;     /* vertices whose sources don't fix a point */
  uint32_t collisions = 0; /* faces that share a name with others, beyond the first */
};

void name_elements(const Arrangement &a, const Expression &e, const litestl::util::Vector<Selected> &selected,
                   const Result &result, const NameSource &source, Names &out);

/** A bounded region of the result. */
struct RegionSides {
  uint32_t solid = kVoid;                /* label */
  uint32_t air = kVoid;                  /* the first air label on its sides */
  uint32_t leaf = kNone;                 /* the first leaf with a face on its sides */
  PersistentId min_face;                 /* that leaf's smallest face name there */
};

/** The hint of each region; kNoId if nothing names it, and for region 0 (the
 * unbounded one, which has a name already). */
void name_regions(std::span<const RegionSides> regions, const NameSource &source,
                  litestl::util::Vector<PersistentId> &hints);

} // namespace ubertopo::csg
