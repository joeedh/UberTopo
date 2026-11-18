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
  /* global layer */
  LoopIntersection,     /* a loop self-intersects, or two loops of a face meet */
  HoleOutside,          /* a hole is not inside its face's outer loop */
  FaceIntersection,     /* two faces meet away from shared vertices and edges */
  WireCrossesFace,      /* a wire meets a face other than at a shared endpoint (P3b C5) */
  RegionsStale,         /* stored regions differ from a fresh build_regions() */
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

/** Restricts the embedding and global layers to some elements (sorted,
 * unique index lists; plan P3 §3.6). Face-face contact is checked for every
 * pair with at least one listed face; containers for listed vertices and
 * edges; radial order for listed edges. RegionsStale runs only if
 * @p regions is set. */
struct ValidateFilter {
  litestl::util::Vector<uint32_t> faces, edges, verts;
  bool regions = false;

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
  validate_combinatorial(k, v, rep);
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

} // namespace ubertopo::topo
