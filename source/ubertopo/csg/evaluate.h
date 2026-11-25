#pragma once

/* Evaluates a CSG tree (plan 2026-11-25-exact-booleans §5). */

#include "ubertopo/csg/output.h"
#include "ubertopo/csg/tree.h"

namespace ubertopo::csg {

/** Part of every cache key: results can change when it does. */
inline constexpr uint32_t kEngineVersion = 1;

struct BoolOptions {
  /** From the tree's frame to the output's, in quanta of the operands. */
  RigidPlacement placement;
  /** Frame of the output; its quantum must be the operands'. */
  geom::TileFrame frame;
  /** If the result can't be rounded: how many finer quanta (by 2 each) are
   * tried. The output then has that quantum. */
  int max_quantum_shift = 0;
  /** How attributes of faces and edges are split (default: copied). */
  const topo::PropagationRules *rules = nullptr;
  bool validate_operands = true;
  bool validate_result = true;
};

struct BoolReport {
  CsgCode code = CsgCode::Ok;
  PersistentId node; /* of a failure, if it is a node's */
  int quantum_shift = 0;
  ArrangementStats arrangement;
  uint32_t points = 0, facets = 0;
  OutputReport output;
  litestl::util::Vector<PersistentId> labels;      /* names, by label */
  litestl::util::Vector<LabelSource> label_source; /* by label: the first region with it */
  litestl::util::Vector<PersistentId> leaf_ids;    /* by leaf */
};

/** The tree's solid as a complex with regions. @p out is written only if the
 * result is Ok. */
CsgCode evaluate(const CsgTree &t, const OperandStore &store, const BoolOptions &options,
                 topo::Complex<geom::Lattice> &out, BoolReport &report);

/** Of the result: the tree, the options that matter, the engine's version. */
PersistentId evaluation_key(const CsgTree &t, const BoolOptions &options);

} // namespace ubertopo::csg
