#pragma once

/* The engine of build_regions() (regions.h says what it computes; plan
 * docs/plans/2026-11-24-incremental-regions.md §4).
 *
 * One object runs one build, stage by stage:
 *  - summarise_faces: triangulates every face once and keeps only a summary
 *    (FaceSummary); triangles are rebuilt for the shells nesting tests;
 *  - make_shells: union-find over the wedges, labels, shell order, side lists;
 *  - summarise_shells: kind, volume, box and hint counts, from face summaries;
 *  - nest: the containing outer shell of every cavity and sheet shell;
 *  - name_regions: votes and IDs, from shell summaries; the final order;
 *  - resolve_containers: container hints;
 *  - write: only what differs from the complex's current columns.
 * A stage that returns false has reported an error, and nothing was written.
 *
 * Template definitions are in the region_*_impl.h headers, included only by
 * regions.cc (which instantiates build_regions for every coordinate type). */

#include <cstdint>
#include <span>
#include <utility>

#include "litestl/util/map.h"
#include "litestl/util/sort.h"
#include "litestl/util/task.h"
#include "litestl/util/vector.h"
#include "ubertopo/topo/hint_votes.h"
#include "ubertopo/topo/radial.h"
#include "ubertopo/topo/regions.h"
#include "ubertopo/topo/regions/region_geometry.h"

namespace ubertopo::topo::region_detail {

namespace util = litestl::util;
namespace task = litestl::task;
using ident::PersistentId;

enum ShellKind : uint8_t { Sheet, Outer, Cavity, Degenerate };

/** What a build keeps of one shell. */
template <geom::CoordType C> struct ShellSummary {
  Volume<C> vol; /* outer and cavity shells only */
  BBox box;
  PersistentId label; /* its smallest face-side ID */
  uint8_t kind = Degenerate;
  /** Containing outer shell of a cavity or sheet shell, NONE for region 0. */
  uint32_t parent = NONE;
  /** Distinct hints of its sides, usually none or one. */
  util::Vector<HintVotes::Vote, 1> hints;
};

template <geom::CoordType C> class RegionBuilder {
public:
  using Point = geom::Point<C>;

  RegionBuilder(Complex<C> &k, RegionReport &report)
      : k_(k), report_(report), F_(k.faces.capacity()), sk_(k)
  {
  }
  bool build();

private:
  /* ---- stages ---- */
  void summarise_faces();
  void make_shells();
  bool summarise_shells();
  bool nest();
  void name_regions();
  bool resolve_containers();
  void write();

  /* ---- shared by stages ---- */
  /** The vertices of @p sh's faces, ascending and distinct. */
  void shell_verts(uint32_t sh, util::Vector<uint32_t> &out) const;
  /** A vertex of shell @p a, or else an interior point of a face of it, that
   * isn't on a candidate shell (vertices @p vb). Returns the point's scale for
   * ray_winding, or -1. */
  int test_point(uint32_t a, std::span<const uint32_t> va, std::span<const uint32_t> vb, Point &p);
  void write_faces(const util::Vector<uint32_t> &shell_region);
  void write_shells(const util::Vector<uint32_t> &shell_region, util::Vector<uint32_t> &region_first);
  void write_regions(const util::Vector<uint32_t> &region_first);

  Complex<C> &k_;
  RegionReport &report_;
  const uint32_t F_;
  /* the refined skeleton (plan P4b §4.3); warmed first, so the parallel
   * loops only read it */
  Skeleton<C> sk_;

  util::Vector<FaceSummary<C>> face_;
  util::Vector<PersistentId> side_id_; /* by face side; dead faces' unset */
  util::Vector<uint32_t> side_shell_;  /* by face side; NONE for dead faces */
  /* each shell's sides in ascending order: the shell chains, the test points,
   * the volume sums and the nesting rays depend on it */
  util::KeyGroups shell_sides_;
  util::Vector<ShellSummary<C>> shell_; /* by label */

  /* Regions. Provisional: 0 = unbounded, then one per outer shell in shell
   * order. Final: 0, then by ID. */
  uint32_t R_ = 1;
  util::Vector<uint32_t> shell_preg_; /* shell -> provisional region */
  util::Vector<uint32_t> preg_final_;
  util::Vector<PersistentId> reg_id_; /* by final region */

  /* Per hint, over regions r > 0: how many win it, the final index of the
   * winner (when exactly one wins), and the minority carrier (P3b C4): the
   * provisional region carrying it without winning it, NONE if two do. */
  struct HintInfo {
    uint32_t winners = 0, seen = 0, win = NONE, carrier = NONE;
    bool has_carrier = false;
  };
  util::Map<PersistentId, HintInfo> info_;
  util::Vector<std::pair<uint32_t, ident::ContainerRef>> vcont_, econt_;
};

} // namespace ubertopo::topo::region_detail
