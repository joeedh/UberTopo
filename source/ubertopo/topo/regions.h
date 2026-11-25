#pragma once

/* build_regions() (design 01 §6, plan P2 W7): shells and regions from the
 * radial order and the wedge rule. Regions are rebuilt wholesale (DEC-008).
 *
 *  1. Shells: union-find over wedges. The wedge between coedge c and
 *     cn = radial_next(c) joins face sides (face(c), side_toward_next(c)) and
 *     (face(cn), 1 - side_toward_next(cn)); a radial cycle of one coedge (a
 *     sheet boundary) joins both sides of its face. Shells are indexed by
 *     their smallest face-side persistent ID.
 *  2. Kind: a *sheet* shell holds both sides of every face it touches (a
 *     fence; a thin-walled shell open through a window). Other shells get the
 *     exact signed volume V of their face sides, with each side's normal
 *     pointing away from the side's region: V > 0 is an *outer* shell (it
 *     encloses its region), V < 0 a *cavity* shell. Lattice: exact in Int128.
 *     double: a filtered sum; |V| within its error bound is reported as a
 *     degenerate shell and the build fails (owner decision P2 §9.1).
 *  3. Nesting: each cavity and sheet shell A belongs to the region of the
 *     innermost outer shell B containing it (smallest enclosed volume). A and
 *     B sharing a face on opposite sides means "not inside"; otherwise a point
 *     of A (a vertex not on B, else an interior point of a face of A that
 *     isn't B's) is tested with an exact ray winding against B.
 *  4. Regions: region 0 is unbounded; every outer shell starts a region.
 *  5. IDs: region 0 is unbounded_region_id(); a region whose face sides carry
 *     hint h is region_id(h, min side ID); otherwise region_id_from_sides.
 *     Conflicting hints are reported (the majority wins, ties to the smallest
 *     hint); the same hint on two regions is a warning.
 *  6. Region columns that aren't Topo are carried over from the previous
 *     region set by persistent ID.
 *  7. Container hints (".v/.e.container_hint") resolve to region containers.
 *
 * The engine is topo/regions/ (RegionBuilder). It writes the face, side and
 * container columns only where they change (plan 2026-11-24 section 4.5). */

#include <cstdint>

#include "litestl/util/vector.h"
#include "ubertopo/ident/id.h"
#include "ubertopo/topo/complex.h"

namespace ubertopo::topo {

enum class RegionCode : uint16_t {
  /* errors: regions are not built */
  DegenerateShell,         /* double volume not certifiable */
  NestingDegenerate,       /* every ray direction hit an edge or vertex */
  TouchingShell,           /* a test point lies on another shell: unrecorded contact */
  NoTestPoint,             /* no exact test point off the candidate shell */
  UnresolvedContainerHint, /* no region carries the hint */
  AmbiguousContainerHint,  /* several regions carry the hint */
  /* warnings */
  HintConflict,  /* a region's face sides carry different hints */
  DuplicateHint, /* two regions carry the same hint (IDs stay unique) */
};

const char *region_code_name(RegionCode c);
bool region_code_is_error(RegionCode c);

struct RegionIssue {
  RegionCode code;
  uint32_t index; /* shell, region, face side, vertex or edge, by code */
};

struct RegionReport {
  litestl::util::Vector<RegionIssue> issues;
  uint32_t errors = 0, warnings = 0;
  uint32_t shells = 0, outer_shells = 0, cavity_shells = 0, sheet_shells = 0, regions = 0;
  uint32_t ray_tests = 0, ray_retries = 0;

  void add(RegionCode c, uint32_t index)
  {
    issues.append({c, index});
    (region_code_is_error(c) ? errors : warnings)++;
  }
  bool ok() const
  {
    return errors == 0;
  }
  uint32_t count(RegionCode c) const
  {
    uint32_t n = 0;
    for (const RegionIssue &i : issues) {
      n += i.code == c;
    }
    return n;
  }
};

/** Builds shells and regions. On success writes .f.region, .f.shell, the
 * face-side shell chains, the shell and region domains and resolved
 * containers, and clears regions_dirty. On error the complex is unchanged. */
template <geom::CoordType C> bool build_regions(Complex<C> &k, RegionReport &report);

extern template bool build_regions<double>(Complex<double> &, RegionReport &);
extern template bool build_regions<float>(Complex<float> &, RegionReport &);
extern template bool build_regions<geom::Lattice>(Complex<geom::Lattice> &, RegionReport &);

} // namespace ubertopo::topo
