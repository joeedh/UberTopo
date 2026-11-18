#pragma once

/* Bulk builder (plan P2 W6): procedural input -> a linked Complex<C>.
 *
 * Emit vertices, faces (loops of vertex keys; holes are extra loops) and wire
 * edges, then build(). Keys are the indices returned by vertex() / face() /
 * wire(). The output is a function of the *set* of emitted elements and their
 * IDs only, never of emission order:
 *  - vertices weld by persistent ID (equal IDs must have bitwise-equal
 *    positions); optionally distinct IDs with bitwise-equal positions weld too,
 *    keeping the smaller ID;
 *  - every domain is filled densely in sorted-key order: vertices by ID, edges
 *    by (min vertex ID, max vertex ID), faces by ID, loops by (face, ordinal),
 *    coedges by (face, loop ordinal, position);
 *  - edges run from the smaller vertex ID to the larger (vs[0] -> vs[1]); disk
 *    cycles are in edge-index order; radial cycles are exact (radial.h).
 *
 * build() is all-or-nothing: any error leaves @p out empty and is listed in
 * the report; warnings (e.g. exactly coincident radial angles) don't stop it.
 * Regions are not built (regions_dirty stays set). */

#include <cstdint>
#include <initializer_list>
#include <span>

#include "litestl/util/vector.h"
#include "ubertopo/geom/coord.h"
#include "ubertopo/ident/id.h"
#include "ubertopo/topo/complex.h"

namespace ubertopo::topo {

/** What a vertex or wire is included in. A face is named by its builder key;
 * a region by a generator hint ID, resolved by build_regions(). */
struct ContainerHint {
  enum class Kind : uint8_t { None, Face, Region };
  Kind kind = Kind::None;
  uint32_t face_key = NONE;
  ident::PersistentId region{};

  static ContainerHint in_face(uint32_t face_key)
  {
    ContainerHint h;
    h.kind = Kind::Face;
    h.face_key = face_key;
    return h;
  }
  static ContainerHint in_region(ident::PersistentId hint)
  {
    ContainerHint h;
    h.kind = Kind::Region;
    h.region = hint;
    return h;
  }
  friend bool operator==(const ContainerHint &, const ContainerHint &) = default;
};

enum class BuildCode : uint16_t {
  /* errors */
  VertexPositionMismatch, /* one ID, different positions */
  VertexContainerMismatch,
  BadVertexKey,
  BadFaceKey,
  DuplicateFaceId,
  DuplicateWireId,
  LooplessFace,
  LoopTooShort,
  RepeatedVertexInLoop,
  VertexSharedBetweenLoops,
  EdgeUsedTwice,
  DegenerateLoop, /* collinear or zero area */
  NonPlanarFace,
  HoleOrientation, /* a hole wound like its outer loop */
  DegenerateWire,  /* both ends weld to one vertex */
  WireOnFaceEdge,
  DuplicateWire,
  ContainerOnFaceVertex, /* a container on a vertex that face edges use */
  InternalError,
  /* warnings */
  RadialTie, /* exactly equal angles about an edge: overlapping coplanar faces */
};

const char *build_code_name(BuildCode c);
bool build_code_is_error(BuildCode c);

struct BuildIssue {
  BuildCode code;
  ident::PersistentId id; /* of the element concerned, when known */
  uint32_t key = NONE;    /* its builder key, when known */
};

struct BuildReport {
  litestl::util::Vector<BuildIssue> issues;
  uint32_t errors = 0;
  uint32_t warnings = 0;

  void add(BuildCode c, ident::PersistentId id = {}, uint32_t key = NONE)
  {
    issues.append({c, id, key});
    (build_code_is_error(c) ? errors : warnings)++;
  }
  bool ok() const
  {
    return errors == 0;
  }
  uint32_t count(BuildCode c) const
  {
    uint32_t n = 0;
    for (const BuildIssue &i : issues) {
      n += i.code == c;
    }
    return n;
  }
};

struct BuildPolicy {
  /** Also weld distinct IDs whose positions are bitwise equal. */
  bool weld_equal_positions = false;
};

template <geom::CoordType C> class Builder {
public:
  using Point = geom::Point<C>;

  explicit Builder(const geom::TileFrame &frame = {}) : frame_(frame) {}

  uint32_t vertex(ident::PersistentId id, const Point &p, ContainerHint c = {});

  /** A face: @p keys holds the loops back to back, @p loop_sizes their lengths.
   * @p front / @p back are region hints (kNoId for none). */
  uint32_t face(ident::PersistentId id,
                std::span<const uint32_t> keys,
                std::span<const uint32_t> loop_sizes,
                ident::PersistentId front = {},
                ident::PersistentId back = {},
                uint8_t flags = 0);
  uint32_t face(ident::PersistentId id,
                std::initializer_list<std::initializer_list<uint32_t>> loops,
                ident::PersistentId front = {},
                ident::PersistentId back = {},
                uint8_t flags = 0);

  uint32_t wire(ident::PersistentId id, uint32_t a, uint32_t b, ContainerHint c = {});

  bool build(Complex<C> &out, BuildReport &report, const BuildPolicy &policy = {}) const;

private:
  struct VIn {
    ident::PersistentId id;
    Point p;
    ContainerHint c;
  };
  struct FIn {
    ident::PersistentId id;
    uint32_t key_begin, loop_begin, nloops;
    ident::PersistentId hint[2];
    uint8_t flags;
  };
  struct WIn {
    ident::PersistentId id;
    uint32_t a, b;
    ContainerHint c;
  };

  geom::TileFrame frame_;
  litestl::util::Vector<VIn> verts_;
  litestl::util::Vector<FIn> faces_;
  litestl::util::Vector<uint32_t> fkeys_;
  litestl::util::Vector<uint32_t> fsizes_;
  litestl::util::Vector<WIn> wires_;
};

extern template class Builder<double>;
extern template class Builder<float>;
extern template class Builder<geom::Lattice>;

} // namespace ubertopo::topo
