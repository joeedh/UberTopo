#pragma once

/* Places and rounds the result of a CSG expression, and builds a complex (plan
 * 2026-11-25-exact-booleans §4.7). */

#include "ubertopo/csg/merge.h"

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
  /** @p p (shifted as @p in is) at a quantum 2^-shift times the operands'. */
  I3 round(const Input &in, const HPoint &p, int shift) const;

private:
  static constexpr int kRotBits = 62, kTransBits = 20;
  int64_t r_[3][3] = {};
  int64_t t_[3] = {};
};

struct OutputOptions {
  RigidPlacement placement;
  int quantum_shift = 0; /* finer than the operands' quantum by 2^this */
  bool validate = true;
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

struct OutputReport {
  RoundCode round = RoundCode::None;
  uint32_t split_whole = 0; /* projected source faces that were output as triangles */
  uint32_t projected = 0;   /* faces that aren't planar */
  Result result;
  litestl::util::Vector<I3> positions;       /* by point: rounded; points not in the result are 0 */
  litestl::util::Vector<uint32_t> face_of;   /* by result face: in the complex */
};

/** Merges, places, rounds and builds. (IDs are ordinals.)
 * @p out is written only if the result is Ok. */
CsgCode build_output(const Arrangement &a, const Expression &e, const litestl::util::Vector<Selected> &selected,
                     const OutputOptions &options, topo::Complex<geom::Lattice> &out, OutputReport &report);

} // namespace ubertopo::csg
