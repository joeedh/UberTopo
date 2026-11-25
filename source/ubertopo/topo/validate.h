#pragma once

/* Validator (design 01 §8, plan P2 W8). Returns a structured report and never
 * asserts.
 *
 * P2a implements the combinatorial layer. Region checks (invariant W, shell
 * partition, the unbounded region) run only when regions are current
 * (!regions_dirty). The embedding and global layers, and the Euler-Poincare
 * cross-check, land in P2b. */

#include <algorithm>
#include <cstdint>

#include "litestl/util/vector.h"
#include "ubertopo/geom/predicates.h"
#include "ubertopo/ident/id.h"
#include "ubertopo/ident/ref.h"
#include "ubertopo/topo/complex.h"

namespace ubertopo::topo {

enum class ValidateCode : uint16_t {
  DeadReference,        /* a link to a freed or out-of-range slot */
  EdgeEndpoints,        /* .e.vs NONE or both ends equal */
  CoedgeNextPrev,       /* prev(next(c)) != c */
  CoedgeChain,          /* end_vert(c) != start_vert(next(c)) */
  CoedgeStartVertex,    /* .c.v disagrees with .e.vs and the sense flag */
  LoopNotClosed,        /* the next-walk doesn't return within .l.size, or leaves the loop */
  LoopSize,             /* .l.size != number of coedges in the loop */
  LoopFace,             /* a loop's .l.f doesn't own it, or a face's loop list is broken */
  LooplessFace,         /* planar faces need a loop */
  FaceEdgeTwice,        /* a face uses one edge twice */
  RadialNotClosed,      /* radial walk broken or prev/next not inverse */
  RadialMembership,     /* radial cycle members don't all use the edge, or miss some */
  EdgeCoedge,           /* .e.c NONE although coedges use the edge (or the reverse) */
  DiskNotClosed,        /* disk walk broken or leaves the vertex */
  DiskInverse,          /* disk prev/next not inverse */
  DiskMembership,       /* disk cycle size != vertex degree */
  VertexEdge,           /* .v.e NONE although edges use the vertex */
  ContainerInvalid,     /* bad kind, index, or freed target */
  ContainerOnFaceVertex, /* a vertex with face (non-embedded) edges has a container */
  DuplicateId,          /* two live elements of one domain share a persistent ID */
  WedgeInvariant,       /* invariant W */
  ShellPartition,       /* a face side in no shell, in two shells, or with the wrong .f.shell */
  ShellRegion,          /* .f.region[side] != the shell's region, or region shell chain broken */
  UnboundedRegion,      /* not exactly one unbounded region, at index 0 */
  VertexLink,           /* Euler-Poincare at a vertex: sector count inconsistent with a planar link */
  /* embedding layer */
  NonPlanarFace,        /* an unflagged face is not exactly planar */
  SpuriousProjection,   /* a projected flag on a planar face, or an invalid axis */
  ProjectionNotAllowed, /* a projected face in a complex built without the opt-in */
  NotProjectable,       /* a projected face is not projectable along its stored axis */
  HoleWinding,          /* a planar face's hole winds like its outer loop */
  RadialOrder,          /* the stored radial order differs from the geometric one */
  ContainerGeometry,    /* an included vertex or wire is not inside its container */
  CurveKind,            /* .e.curve is not live, or not a kind edges accept (P4a) */
  CurveRange,           /* .e.range outside the domain, equal ends, across an offset fold,
                         * or non-zero with no carrier */
  CurveEndpoint,        /* an end vertex lies off the carrier (beyond the §2 tolerance) */
  GeometryUnsupported,  /* non-zero .e.geom_flags (reserved; plan P4b §4.1) */
  /* global layer */
  LoopIntersection,     /* a loop self-intersects, or two loops of a face meet */
  HoleOutside,          /* a hole is not inside its face's outer loop */
  FaceIntersection,     /* two faces meet away from shared vertices and edges */
  WireCrossesFace,      /* a wire meets a face other than at a shared endpoint (P3b C5) */
  RegionsStale,         /* stored regions differ from a fresh build_regions() */
  SkeletonDegenerate,   /* a curved edge's refined skeleton touches its own loop or another
                         * edge of the face, or the edge doesn't flatten (P4b §4.3) */
  /* surfaces and pcurves (plan P4b §4.2; embedding layer) */
  SurfaceKind,          /* .f.surface is not a live, valid surface of an accepted kind */
  PcurveKind,           /* a pcurve of the wrong kind for its edge, one on a coedge of an
                         * unsurfaced face, or one missing on a surfaced face */
  PcurveRange,          /* the pcurve's domain doesn't cover the edge's range, or it leaves
                         * the surface's u domain */
  PcurveVertex,         /* S(pcurve) misses the coedge's end vertex beyond the tolerance */
  PcurveDeviation,      /* the edge leaves S(pcurve) beyond the tolerance, or the bound is
                         * inconclusive within the subdivision cap */
  SurfaceLink,          /* no single piece (road3 link) holds all of the face's pcurves */
  SurfaceOrientation,   /* .f.flags Reversed disagrees with the surface normal */
};

const char *validate_code_name(ValidateCode c);

struct ValidationIssue {
  ValidateCode code;
  ident::ElemKind kind;
  uint32_t index;
};

struct ValidationReport {
  litestl::util::Vector<ValidationIssue> issues;
  uint32_t total = 0;                  /* including issues past the cap */
  static constexpr uint32_t kCap = 256; /* issues recorded */

  void add(ValidateCode c, ident::ElemKind k, uint32_t index)
  {
    if (issues.size() < kCap) {
      issues.append({c, k, index});
    }
    total++;
  }
  bool ok() const
  {
    return total == 0;
  }
  uint32_t count(ValidateCode c) const
  {
    uint32_t n = 0;
    for (const ValidationIssue &i : issues) {
      n += i.code == c;
    }
    return n;
  }
  void print(FILE *out) const;
};

/** Combinatorial (cheap), + Embedding (geometry on topology), + Global
 * (expensive: loop simplicity, face-face intersections, regions recompute). */
enum class ValidateLevel : uint8_t { Combinatorial, Embedding, Global };

/** The vertex columns the non-template checks need. */
struct VertexTopoView {
  const attr::Column<uint32_t> &e;
  const attr::Column<ident::ContainerRef> &container;
  const attr::Column<ident::PersistentId> &id;
  const attr::BoolColumn &live;
  uint32_t capacity;
};

/** Combinatorial layer over the coordinate-independent domains. */
void validate_combinatorial(const ComplexBase &k, const VertexTopoView &v, ValidationReport &rep);
/** The part of the combinatorial layer that reads what build_regions writes
 * (plan 2026-11-22 §8): vertex and edge containers (live; none on a face
 * vertex), unique region IDs, and the shell and region structure with
 * invariant W (skipped while regions are dirty, as in the full layer). It
 * assumes nothing about links: coedges it can't follow are skipped, never
 * read out of range. It reports at least the region issues the full layer
 * reports, and may report more where the full layer stops at a broken link.
 * For rebuild_regions barriers, whose other topology earlier commits
 * validated. A flat, parallel pre-check (validate_regions.cc) settles the
 * usual clean case; otherwise the walk runs, and reports. */
void validate_regions(const ComplexBase &k, const VertexTopoView &v, ValidationReport &rep);

/** Restricts the embedding and global layers to some elements (sorted,
 * unique index lists; plan P3 §3.6). Face-face contact is checked for every
 * pair with at least one listed face; containers for listed vertices and
 * edges; radial order for listed edges. RegionsStale runs only if
 * @p regions is set. */
class FaceGrid;

/** Counts live elements holding a persistent ID (the editor's index): lets
 * the local combinatorial layer check ID uniqueness without a scan. */
struct IdOracle {
  virtual ~IdOracle() = default;
  /** @p kind: Vertex, Edge or Face. */
  virtual uint32_t count(ident::ElemKind kind, ident::PersistentId id) const = 0;
};

struct ValidateFilter {
  litestl::util::Vector<uint32_t> faces, edges, verts;
  bool regions = false;
  /** The editor's face-box grid, flushed (else a filtered global layer
   * builds one: O(N)). */
  const FaceGrid *grid = nullptr;
  /** The editor's ID index (else the combinatorial layer runs in full). */
  const IdOracle *ids = nullptr;

  static bool has(const litestl::util::Vector<uint32_t> &v, uint32_t i)
  {
    return std::binary_search(v.begin(), v.end(), i);
  }
  bool face(uint32_t f) const
  {
    return has(faces, f);
  }
  bool edge(uint32_t e) const
  {
    return has(edges, e);
  }
  bool vert(uint32_t v) const
  {
    return has(verts, v);
  }
};

/** Embedding and global layers (validate_geom.cc); run only on complexes
 * that pass the combinatorial layer. A null filter checks everything. */
template <geom::CoordType C>
void validate_embedding(const Complex<C> &k, ValidationReport &rep, const ValidateFilter *filter = nullptr);
template <geom::CoordType C>
void validate_global(const Complex<C> &k, ValidationReport &rep, const ValidateFilter *filter = nullptr);

/** The embedding layer's curve checks (validate_curves.cc): CurveKind,
 * CurveRange, CurveEndpoint, GeometryUnsupported. Edges only. */
template <geom::CoordType C>
void validate_curves(const Complex<C> &k, ValidationReport &rep, const ValidateFilter *filter = nullptr);

/** The embedding layer's surface checks (validate_surfaces.cc, plan P4b
 * §4.2): SurfaceKind, PcurveKind, PcurveRange, SurfaceLink, PcurveVertex,
 * PcurveDeviation, SurfaceOrientation. With a filter: its faces and the
 * faces around its edges. */
template <geom::CoordType C>
void validate_surfaces(const Complex<C> &k, ValidationReport &rep, const ValidateFilter *filter = nullptr);

/** The global layer restricted to @p filter, over @p grid (validate_grid.cc). */
template <geom::CoordType C>
void validate_global_grid(const Complex<C> &k, ValidationReport &rep, const ValidateFilter &filter, const FaceGrid &grid);
/** Winding of @p p about region 0's boundary: one ray through @p grid. */
template <geom::CoordType C>
geom::RayResult region0_winding_grid(const Complex<C> &k, const geom::Point<C> &p, const FaceGrid &grid);

/** Combinatorial layer restricted to @p filter and its one-ring (plan P3b
 * C9; validate_local.cc). Needs filter.ids. */
void validate_combinatorial_local(const ComplexBase &k,
                                  const VertexTopoView &v,
                                  const ValidateFilter &filter,
                                  ValidationReport &rep);

template <geom::CoordType C> ValidationReport validate(const Complex<C> &k, ValidateLevel level = ValidateLevel::Combinatorial)
{
  ValidationReport rep;
  const VertexTopoView v{k.verts.template get<".v.e">(),
                         k.verts.template get<".v.container">(),
                         k.verts.template get<".id">(),
                         k.verts.template get<".live">(),
                         k.verts.capacity()};
  validate_combinatorial(k, v, rep);
  if (rep.ok() && level >= ValidateLevel::Embedding) {
    validate_embedding(k, rep);
  }
  if (rep.ok() && level >= ValidateLevel::Global) {
    validate_global(k, rep);
  }
  return rep;
}

/** Local validation (plan P3 §3.6, C14): the whole combinatorial layer (linear
 * and cheap), then the embedding and global layers restricted to @p filter. */
template <geom::CoordType C> ValidationReport validate_local(const Complex<C> &k, const ValidateFilter &filter)
{
  ValidationReport rep;
  const VertexTopoView v{k.verts.template get<".v.e">(),
                         k.verts.template get<".v.container">(),
                         k.verts.template get<".id">(),
                         k.verts.template get<".live">(),
                         k.verts.capacity()};
  if (filter.ids) {
    validate_combinatorial_local(k, v, filter, rep);
  }
  else {
    validate_combinatorial(k, v, rep);
  }
  if (rep.ok()) {
    validate_embedding(k, rep, &filter);
  }
  if (rep.ok()) {
    validate_global(k, rep, &filter);
  }
  return rep;
}

extern template void validate_embedding<double>(const Complex<double> &, ValidationReport &, const ValidateFilter *);
extern template void validate_embedding<float>(const Complex<float> &, ValidationReport &, const ValidateFilter *);
extern template void validate_embedding<geom::Lattice>(const Complex<geom::Lattice> &, ValidationReport &, const ValidateFilter *);
extern template void validate_global<double>(const Complex<double> &, ValidationReport &, const ValidateFilter *);
extern template void validate_global<float>(const Complex<float> &, ValidationReport &, const ValidateFilter *);
extern template void validate_global<geom::Lattice>(const Complex<geom::Lattice> &, ValidationReport &, const ValidateFilter *);
#define UT_VGRID_EXTERN(C)                                                                                            \
  extern template void validate_global_grid<C>(const Complex<C> &, ValidationReport &, const ValidateFilter &,        \
                                               const FaceGrid &);                                                     \
  extern template geom::RayResult region0_winding_grid<C>(const Complex<C> &, const geom::Point<C> &, const FaceGrid &);
UT_VGRID_EXTERN(double)
UT_VGRID_EXTERN(float)
UT_VGRID_EXTERN(geom::Lattice)
#undef UT_VGRID_EXTERN

} // namespace ubertopo::topo
