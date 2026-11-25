#pragma once

/* Places and rounds the result of a CSG expression, and builds a complex (plan
 * 2026-11-25-exact-booleans §4.7). */

#include "ubertopo/csg/attributes.h"
#include "ubertopo/csg/repair.h"

namespace ubertopo::csg {

/** Output point = r * operand point + t, in the operands' quanta. */
struct RigidPlacement {
  double r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  double t[3] = {0, 0, 0};
};

/** A placement in integers, so that a rounded point depends on the point only. */
class ExactPlacement {
public:
  /** False: not a rotation (to 2^-40), or t out of range. */
  bool set(const RigidPlacement &p);
  /** @p p (shifted as @p in is) at a quantum 2^-shift times the operands'.
   * @p bias: added before rounding, in eighths of the result's quanta; below 4. */
  I3 round(const Input &in, const HPoint &p, int shift, const I3 &bias = I3(0, 0, 0)) const;

private:
  static constexpr int kRotBits = 62, kTransBits = 20;
  int64_t r_[3][3] = {};
  int64_t t_[3] = {};
};

struct OutputOptions {
  RigidPlacement placement;
  geom::TileFrame frame; /* of the output, before the shift */
  int quantum_shift = 0; /* finer than the operands' quantum by 2^this */
  bool validate = true;
  const NameSource *names = nullptr; /* none: IDs are ordinals, and no attributes */
  const topo::PropagationRules *rules = nullptr;
  int max_repair_rounds = 0; /* per attempt (repair.h); 0: no repair */
};

/** Why a result couldn't be rounded. */
enum class RoundCode : uint8_t {
  None,
  SamePosition, /* two points */
  Build,        /* Builder refused */
  Regions,      /* build_regions refused */
  Radial,       /* facets changed their order about an edge */
  Labels,       /* a region got sides of two solid labels */
  Invalid,      /* validate(Global) */
};

/** What it took to round a result. */
struct RepairReport {
  uint32_t rounds = 0;   /* of the last attempt; 0: it wasn't repaired, or only rounded otherwise */
  uint32_t attempts = 0; /* beyond the first; each rounds differently */
  RoundCode first = RoundCode::None; /* what failed without repair */
  RepairCounts counts;
  uint32_t vanished = 0; /* faces of the exact result that have no piece left */
  uint32_t joined = 0;   /* faces that were joined again */
};

/** Where a face of the result comes from. */
struct FaceSource {
  PersistentId leaf, face;
  bool reversed;
};

struct OutputReport {
  RoundCode round = RoundCode::None;
  uint32_t split_whole = 0; /* projected source faces that were output as triangles */
  uint32_t projected = 0;   /* faces that aren't planar */
  RepairReport repair;
  Result result; /* with points of the last arrangement: the given one, if there were no rounds */
  litestl::util::Vector<I3> positions;       /* by point: rounded; points not in the result are 0 */
  litestl::util::Vector<uint32_t> face_of;   /* by result face: in the complex */
  litestl::util::Vector<uint32_t> vert_of;   /* by point */
  litestl::util::Vector<uint32_t> edge_of;   /* by result edge */
  litestl::util::Vector<FaceSource> sources; /* by result face */
  uint32_t bad_leaf = kNone;                 /* whose attributes don't go with the others' */
  Names names;
  Provenance provenance;
  litestl::util::Vector<RegionSides> regions; /* by region of the complex; 0 is the unbounded one */
};

/** Merges, places, rounds and builds; repairs if it must and may.
 * @p out is written only if the result is Ok. */
CsgCode build_output(const Arrangement &a, const Expression &e, const litestl::util::Vector<Selected> &selected,
                     const OutputOptions &options, topo::Complex<geom::Lattice> &out, OutputReport &report);

} // namespace ubertopo::csg
