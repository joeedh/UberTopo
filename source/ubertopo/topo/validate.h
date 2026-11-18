#pragma once

/* Validator (design 01 §8, plan P2 W8). Returns a structured report and never
 * asserts.
 *
 * P2a implements the combinatorial layer. Region checks (invariant W, shell
 * partition, the unbounded region) run only when regions are current
 * (!regions_dirty). The embedding and global layers, and the Euler-Poincare
 * cross-check, land in P2b. */

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

enum class ValidateLevel : uint8_t { Combinatorial };

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

template <geom::CoordType C> ValidationReport validate(const Complex<C> &k, ValidateLevel = ValidateLevel::Combinatorial)
{
  ValidationReport rep;
  const VertexTopoView v{k.verts.template get<".v.e">(),
                         k.verts.template get<".v.container">(),
                         k.verts.template get<".id">(),
                         k.verts.template get<".live">(),
                         k.verts.capacity()};
  validate_combinatorial(k, v, rep);
  return rep;
}

} // namespace ubertopo::topo
