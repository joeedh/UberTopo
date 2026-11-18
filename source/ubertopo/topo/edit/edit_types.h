#pragma once

/* Shared types of the editor (plan P3a): group ids, result codes, the
 * failure report, op records (for redo) and lineage records. */

#include <cstdint>
#include <vector>

#include "litestl/util/vector.h"
#include "ubertopo/geom/coord.h"
#include "ubertopo/ident/id.h"
#include "ubertopo/ident/ref.h"
#include "ubertopo/topo/validate.h"

namespace ubertopo::topo {

/** The attribute groups of a complex, in a fixed order (captures, roots). */
enum class Grp : uint8_t { Verts, Edges, Coedges, Loops, Faces, Sides, Shells, Regions, Global };
inline constexpr uint32_t kGroupCount = 9;

enum class EditCode : uint16_t {
  Ok,
  /* transactions and journal */
  Frozen,          /* the complex is frozen: thaw it first */
  NoTransaction,   /* commit/rollback without begin */
  OpenTransaction, /* undo/redo/barrier inside an open transaction */
  SchemaChanged,   /* columns were added or removed inside a transaction */
  NothingToUndo,
  NothingToRedo,
  JournalStale, /* the complex changed outside the editor since the transaction */
  /* arguments */
  DeadElement,  /* an argument names a freed or out-of-range slot */
  DuplicateId,  /* a new element's persistent ID is already used */
  BadArgument,  /* malformed argument (side, column, sizes) */
  /* preconditions */
  NotIsolated,       /* kill_vertex / embed: the vertex has edges */
  NotWire,           /* the edge has coedges */
  SameVertex,        /* make_edge(a, a) */
  EdgeExists,        /* an edge already joins the two vertices */
  NotOnEdge,         /* split_edge: the point isn't strictly inside the edge */
  NotJoinable,       /* join_edge / join_faces preconditions */
  Incompatible,      /* merged elements differ in flags, containers or hints */
  NotInLoop,         /* split_face: a or b isn't on the face's outer loop */
  Adjacent,          /* split_face: a and b are adjacent along the loop */
  NotOuterLoop,      /* split_face: the vertices lie on a hole loop */
  BadLoop,           /* make_face: loop too short, missing edge, repeated vertex */
  EdgeUsedTwice,     /* make_face: a face would use an edge twice */
  ContainedElement,  /* make_face over embedded vertices / wires */
  FaceHasInclusions, /* kill_face: vertices or wires are embedded in the face */
  HintInUse,         /* removing the last carrier of a container hint */
  UnhintedRegion,    /* embed into a region no hint names */
  RegionsDirty,      /* the operation needs current regions */
  ReservedFaceFlags, /* the caller set the projection bits */
  InclusionOnChord,  /* split_face: an embedded element lies on the chord */
  HoleOnChord,       /* split_face: a hole touches a child's outer loop */
  BadRule,           /* a propagation rule doesn't fit the column type */
  /* geometry */
  NonPlanarFace,
  NotProjectable,
  HoleOrientation,
  DegenerateLoop,
  RadialTie, /* two faces at exactly equal angles about an edge */
  /* results */
  Invalid,         /* the edit produced an invalid complex (see report().validation) */
  RegionsFailed,   /* build_regions failed (see report().region_code) */
  PropagationFailed,
};

const char *edit_code_name(EditCode c);

/** Details of the last failure. */
struct EditReport {
  EditCode code = EditCode::Ok;
  ident::ElemKind kind = ident::ElemKind::Vertex;
  uint32_t index = NONE;
  ValidationReport validation; /* for Invalid */
  int region_code = -1;        /* RegionCode for RegionsFailed */
  const char *detail = nullptr;
};

/** Journaled operations (redo re-executes them). */
enum class OpKind : uint8_t {
  MakeVertex,
  KillVertex,
  MakeEdge,
  KillEdge,
  SetPosition,
  EmbedVertex,
  EmbedEdge,
  SplitEdge,
  JoinEdge,
  MakeFace,
  KillFace,
  SplitFace,
  JoinFaces,
  FlipFace,
  SetSideHint,
  SetAttr,
  RebuildRegions,
  Compact,
};

/** Arguments of one operation: indices, IDs, points and raw bytes. P3a
 * payloads never hold PoolRefs except SetAttr bytes, which JournalRoots
 * visits through the column (plan P3 C9). */
template <geom::CoordType C> struct OpRecord {
  OpKind kind;
  litestl::util::Vector<uint32_t, 8> u;
  litestl::util::Vector<ident::PersistentId, 4> ids;
  litestl::util::Vector<geom::Point<C>, 2> pts;
  std::vector<uint8_t> bytes;
};

enum class LineageKind : uint8_t { Split, Merge };

/** ID lineage: @p child was derived from @p parent by a split or a merge. */
struct Lineage {
  ident::PersistentId parent, child;
  ident::ElemKind kind;
  LineageKind how;
};

} // namespace ubertopo::topo
