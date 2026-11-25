# Plan: exact Booleans with a CSG tree (P5)

Status: **v2**, after a fresh-context review (§9). Not implemented.

Phase P5 of [06](../design/core-data-model/06-modules-litestl-and-phasing.md). Background:
[booleans-and-operations](../research/cad-kernels/booleans-and-operations.md) §1,
[robustness](../research/cad-kernels/robustness-and-exact-arithmetic.md) §6,
[02 §5](../design/core-data-model/02-coordinates-and-predicates.md#5-the-plane-based-option-d2)
(OI-1), [determinism-and-identity](../research/procedural-systems/determinism-and-identity.md) §3.

## 1. Requirements

- **Exact.** Every decision is exact. Nothing is rounded before the result is complete.
- **A CSG tree, for stability** (owner). The tree of operands and operations is the stored
  model. The solid is derived from it, and is never an operand itself:
  - *numeric:* rounding never accumulates, because every evaluation starts from the
    original operands;
  - *identity:* elements of the result have IDs that survive edits of the tree elsewhere,
    and don't depend on how the tree is nested. What can't be stable is listed (§4.6) and
    reported as lineage;
  - *determinism:* the same tree gives the same bytes on every platform, at any number of
    threads.
- **Multi-region.** Operands are cell complexes with several regions (rooms, materials),
  thin walls and sheets. The result keeps what the operations leave of them.
- **Degenerate contact is the normal case.** Building parts share planes, edges and
  vertices on purpose.
- **Any orientation.** Most buildings aren't aligned with the tile's axes.
- **Scope of an evaluation:** one procedural object (a building, a junction, a bridge):
  10 to 10^4 faces, up to some hundred leaves. Not a city.
- **First-version goals,** as named fixtures: a window in a thick wall; a door in a thin
  (one-face) wall between two rooms; a courtyard; a room with labelled air; a pitched roof
  with a chimney and a dormer; two wings sharing a wall plane; all of these rotated.

## 2. The CSG tree

### 2.1 Structure

```cpp
enum class CsgOp : uint8_t { Union, Intersection, Difference };

class CsgTree {                           // a value; topo/ and geom/ types only
  CsgNode leaf(PersistentId id, CsgOperandRef operand, GridPlacement, CsgLabels);
  CsgNode op(CsgOp, PersistentId id, std::span<const CsgNode> children);
  void set_root(CsgNode);
  ...edits: replace a leaf's operand or placement, insert, remove, reorder children
};
```

- **Operands** are immutable `Complex<Lattice>` snapshots with current regions. A snapshot
  can be used by several leaves.
- **Node IDs** are given by the caller and unique in the tree (`DuplicateId`). They are
  semantic ("window 3 of floor 2"), so inserting a sibling renames nothing.
- **Tree order** is the order of the leaves in a depth-first walk. It doesn't change when
  (A ∪ B) ∪ C is written as A ∪ (B ∪ C). Ties in names and sources go to the first leaf
  in tree order, never to the smaller hash.
- **`GridPlacement` of a leaf:** a signed axis permutation and an integer translation. Both
  are exact. With determinant −1, loops and sides are reversed.
- **`RigidPlacement` of the whole result** (§4.7): any rotation and translation, applied
  once when the result is rounded. The tree itself is modelled in the object's own frame,
  where its walls are on axes and flush faces are exactly flush. Rotating the operands
  instead would destroy that: the review measured that of T-joints between rotated,
  rounded walls fewer than 0.01% stay flush, at any quantum.
- All operands of a tree share one quantum.

### 2.2 Labels

- **A leaf maps each of its regions to a label:**
  - *solid*, with a name: default for every region but the unbounded one;
  - *air*, with a name: a room. Air is empty, and the name is only for naming the region;
  - *void*: the unbounded region.
- **Default names** are `derive(leaf, region)`, so two leaves of one snapshot have
  different labels. The caller can give equal names to make parts fuse.
- **Name → (leaf, region)** is kept, so that region attributes can be copied.

### 2.3 Meaning

A leaf is a function from points to labels. Operations combine the labels at each point;
"empty" is air or void:

| Op | Result |
|---|---|
| `Union(c1..cn)` | the first solid label in child order; else the first air; else void |
| `Intersection(c1..cn)` | c1's, if all are solid, else empty (c1's air, if it has one) |
| `Difference(c1, c2..cn)` | c1's, if no other is solid, else void |

- **Faces of the result:**
  1. where the solid label changes. Air and void are the same for this, so there is no
     face across an opening;
  2. *thin content:* a source face of leaf L where L is *present* on both sides. This is
     how one-face walls, sheets and aperture faces survive.
- **L is present in a cell** if at each operation on its path to the root:
  - Union: no sibling is solid there;
  - Intersection: every sibling is solid there;
  - Difference: L is under the first child, and no other child is solid there.

  So a cutter removes thin walls inside it and leaves no faces of its own in the air.
- **Child order** is priority where operands overlap. The solid set doesn't depend on it.
- The tree is evaluated **at once**: one arrangement of all the leaves' faces, a vector of
  labels per cell, one expression per cell. There are no intermediate solids.

### 2.4 Storage

- `csg_hash(tree)`: a 128-bit hash of node IDs, operations, placements, labels and the
  operands' `content_hash`es. The cache key of a result is this, the options and an engine
  version.
- Serialised as bytes with the operands' hashes; operands are stored by hash in a table
  beside it. It doesn't change UTCX.
- `content_hash` covers allocator state, so operands should come from `Builder` or
  `compact`. Two operands with the same geometry and different histories are different
  operands; that costs a cache miss only.

## 3. Exact geometry

All within the Boolean engine (`source/ubertopo/boolean/`), on litestl's `WideInt`
(wrapping, so only final values must fit). |coordinate| <= 2^26 after moving the operands'
common box to the origin (`OutOfRange` if it is larger). Bounds are analytic; the review
ran 1.2·10^5 random and extreme inputs against them:

| Quantity | Bound | Type |
|---|---|---|
| Plane normal, from three integer points | 2^55 per component, length 2^55.6 | int64 |
| Plane offset | 2^82.6 | Int128 |
| Edge plane (contains an edge and an axis direction transverse to the polygon) | normal 2^28 | int64 |
| Point = three planes: w | 2^166.8 | Int256 |
| its X, Y, Z (the point is in the box) | 2^192.8 | Int256 |
| **classify(point, plane)** | 2^250.2 | Int256 |
| Line direction = two planes | 2^111.2 | Int128 |
| Order of two lines about a point of one plane = sign of a w | 2^166.8 | Int256 |
| Radial order of two planes about a line | 2^222.4 | Int256 |
| Coordinate comparison of two points, X1·w2 against X2·w1 | 2^360 | 512 bits |
| In-plane point in polygon: a comparison and a classify per edge | 2^360 | 512 bits |
| Floor of 2^20 · X/w | 2^214 | Int256 |
| Midpoint of two points against a plane | 2^418 | 512 bits |
| Ray from a midpoint against an edge of input points | 2^400 + direction | 512 bits |
| orient2d of three points of one plane (diagonals of §4.5 only) | 2^555 | 1024 bits |

- **Points** are homogeneous integers (X, Y, Z, w), w > 0, in a table with one entry per
  geometric point.
  - Input vertices have w = 1.
  - Key for hashing: `floor(2^20 · X/w)` per axis. It depends on the point and not on its
    representation. The floor is found from a `double` estimate and corrected exactly.
  - Equality of entries with equal keys: cross-multiplication.
- **Planes:** the int64 normal is divided by its gcd and given a canonical sign, the offset
  recomputed from a point; in a table. Coplanarity is equality of indices.
- **Construction depth is one.** Every plane comes from input points, every point from
  three planes. The only thing built from constructed points is the midpoint of a ray.
- **Filters** in `double` with certified error bounds come in S5. The exact path is the
  reference.
- **Plane-defined operands** (integer planes shared between leaves, vertices as three
  planes) fit the same bounds. They are OI-BOOL-6, not in this plan.

**OI-1.** DEC-008 accepted integer vertices plus an exact plane table, with homogeneous
points in the engine. Here the plane table is the engine's own and is rebuilt per
evaluation from the vertices of exactly planar faces. A stored plane pool is needed only for
plane-defined operands. S2c re-reviews this, as DEC-008 asks.

## 4. Evaluation

### 4.1 Input polygons

- A planar face is one polygon with holes, in its plane.
- A projected face is one polygon per triangle of T(f) (`cdt_proj`). So is a face with
  curved edges: its T(f) is on the refined skeleton. Surfaced faces are refused, as are wires
  and inclusions (`UnsupportedOperand`).
- Each polygon has: plane, loops as integer points, leaf, face, and the regions of its sides.
- Operands must be valid (`Global`). `evaluate` checks it unless told it is known.

### 4.2 Pairs and curves

- **Candidate pairs** of polygons of different leaves, by their boxes. Polygons of one leaf
  meet only in shared edges and vertices.
- **A non-coplanar pair** meets in intervals and points of the line of its two planes. Each
  polygon's interval ends are found on its own edges (edge against plane, and the ends of
  edges that lie on the line); whether a piece between two ends is inside is decided at its
  midpoint.
- **A coplanar pair:** each polygon's edges are curves on the other one.
- **Curves** are the leaves' edges and the pair intervals. Each has one list of points on
  it, shared by every polygon it lies on. Into it goes every point in which it meets any
  polygon or curve, of the same leaf too: ends of intervals, ends of collinear overlaps,
  crossings and single-point contacts.
- **Why atomic edges agree.** If x is on the curve of A and B, and on C, then x is found by
  the pairs (A, C) and (B, C) too, as an end, a crossing or a contact. S2a checks this
  after the fact for every curve.

### 4.3 Facets

For each polygon, in its plane: the arrangement of its own edges and the curves on it.

- Collinear overlapping curves are found by `classify` of their ends, and merged.
- A crossing on a face's plane is the point of three planes. For a curve that is an edge in
  the plane, the second plane is its edge plane.
- **Atomic edges** are pairs of consecutive points.
- **A facet** is a connected open piece of the polygon without the curves. Its boundary is:
  - loops of atomic edges, traced by the order of edges about each point;
  - *slits:* atomic edges with the facet on both sides, as trees hanging from a loop,
    bridges between loops, or free components. Each appears twice in the trace;
  - *isolated points* in which another leaf touches it.
- Components of the boundary are placed by exact point-in-polygon tests.
- **Coincident facets** of coplanar polygons have the same boundary. They become one facet
  with several sources.

### 4.4 Labels

A facet has, for each leaf, a region on each of its sides.

- **Leaves the facet lies on:** known from the source face.
- **Leaves with a facet at one of its edges:** from the radial order of facets about that
  edge, where a facet with a slit there occurs twice. Going round, a leaf's region changes
  only at its own facets.
- **Other leaves:** the region is the same on both sides, and the same for all facets
  connected by edges where that leaf has none. One ray per such component and leaf:
  - from the midpoint of one of the component's edges. It isn't on the leaf: the leaf has
    no facet at that edge, and atomic edges are cut wherever it touches them;
  - the region is found as a formal sum over the crossings, (near side − far side), plus
    void. It needs no order of crossings. It must come out as one region;
  - polygons whose plane contains the origin are skipped; the others rule out finitely
    many directions (1, t, t²). t is tried from 1 to 2^12 (`Inconsistent` beyond);
  - leaves whose box doesn't contain the origin need no ray.
- **Self-check:** around every edge the regions close, for every leaf. A failure is an
  internal error (`Inconsistent`), never a wrong result.

### 4.5 Selection and merging

- **Kept facets** are those of §2.3.
- **The source** of a facet with several: the leaf that provides the solid label (on either
  side; if both sides have one, the first in tree order), if it has a face here; else the
  first leaf in tree order that has one.
- **Merging.** Kept facets with the same source face, plane and labels that meet in an edge
  with no other kept facet are joined, and slits that nothing else uses disappear. Facets
  in different planes (triangles of a projected face) are never joined. A projected source
  face with all its facets kept, unchanged and uncut is put out as it was.
- **Collinear points** with the same facets on both sides are removed, unless they are
  vertices of a source face of those facets.
- **Faces the complex can't hold.** Loops of a face must be simple and disjoint, no edge may
  be used twice by a face, and nothing may touch its interior. A merged face with a pinch,
  touching loops, a slit or an isolated point is cut along diagonals:
  - candidate diagonals between its points are tried in lexicographic order of their ends,
    and taken if they lie inside and cross nothing, until the pieces are triangles;
  - they are then removed again, in reverse order, wherever the joined piece is legal.

### 4.6 Names

Tags `BoolVertex`, `BoolFace` and `BoolRegion` (new `DomainTag`s), each with a kind field.
Only elements of kept facets take part.

| Element | Name |
|---|---|
| Vertex at an operand's vertex | (leaf, vertex), of the first such leaf in tree order |
| Vertex of two crossing edges | (leaf, edge, leaf', edge'), in tree order |
| Vertex where an edge crosses a face | (leaf, edge, leaf', face') |
| Vertex of three faces | the three, in tree order (the first three, if more meet there) |
| Several vertices with one name | plus the ordinal along the line or edge they share |
| Face, primary piece of its source | (leaf, face) |
| Face, other piece | (leaf, face, anchor) |
| Edge, loop | `edge_id`, `loop_id`, as everywhere |
| Solid region | hint = its label's name |
| Air region (bounded) | hint = the first air name in tree order inside it, else none |

- **The anchor of a piece** is the smallest (by ID) of the source face's own vertices on
  its boundary. If another piece has that vertex too, or if there is none, it is the
  smallest coedge (start name, end name), of those from that vertex, or of those on source
  edges, or of all. Two pieces never share a coedge.
- **The primary piece** is the one with the smallest surviving own vertex of the source. So a
  window, a door or a trimmed corner doesn't rename a wall that stays in one piece, and the
  larger part of a wall cut in two often keeps its name.
- **Not stable:**
  - a cut that separates the anchor vertex from the rest gives the name to the piece with
    that vertex, however small;
  - if a split face loses its smallest vertex, the primary name moves to another piece;
  - pieces without own vertices are named by their cutters;
  - holes' loop IDs (ordinals);
  - which leaf is first at a coincidence, when the tree order changes.

  `BoolReport` has the pieces of every source face. `csg_diff(old, new)` gives the lineage
  between two results: kept, gone, new, and pieces whose name moved (S3).
- **Uniqueness is checked** at the end. A collision takes an ordinal by position and is
  counted in the report.
- **Regions** are named in a second pass: build, `build_regions`, then set the hints of
  each region's sides, then again. Region 0 gets none.

### 4.7 Rounding and output

- **Output:** a `Complex<Lattice>` built by `Builder`, with regions, in a given frame:
  `RigidPlacement` (a rotation as a `double` matrix, and a translation) and quantum. Each
  point is placed and rounded once, half up. With the identity and the operands' quantum,
  integer points are unchanged.
- **The topology is fixed before rounding,** so flush faces share their edges and stay
  closed whatever the rotation.
- **Faces that stop being planar** are stored as projected faces (P2 A1): exact
  piecewise-linear surfaces with the same boundary as their neighbours. A face none of
  whose edges gains a vertex, in an unrotated result, keeps its vertices and stays planar.
  Inside one projected face, T(f) differs from the exact face by less than a quantum or so.
- **What can go wrong:** points that round to one position; slivers that round to a line;
  loops that cross after rounding (`NotProjectable`); faces that cross. All are found by
  the builder and by `validate(Global)`.
- **Then:** `RoundingFailed`, naming the faces. `evaluate` retries at a finer output quantum
  if the caller allows it (`max_quantum_shift`; a 100 m building has some 6 bits to spare
  at 2^-13 m).
- **Repair** (collapsing, or repeating the arrangement on the rounded facets) is
  OI-BOOL-2. It is planned only if S2c's numbers ask for it.

## 5. Result

```cpp
BoolCode evaluate(const CsgTree &, const BoolOptions &, topo::Complex<Lattice> &out, BoolReport &);
```

- **`BoolReport`:** code, node and element; counts (points, curves, facets, rays, projected
  faces, names with ordinals, quantum used); provenance of each output face: leaf, source
  face, reversed or not; label name → (leaf, region).
- **Codes:** `Ok`, `EmptyTree`, `DuplicateId`, `BadOperand`, `UnsupportedOperand`,
  `MixedFrames`, `OutOfRange`, `RoundingFailed`, `Inconsistent`.
- **All or nothing:** a failed evaluation doesn't touch `out`.
- **Attributes** (S4): face, face-side and region columns, copied from the source with
  `AttrGroup::mix_from` and `PropagationRules` (`Split`). Columns are created in the output
  by name and type; the same name with two types is `BadOperand`. Vertex and edge columns
  only for elements that are an operand's own. Face flags (`Virtual`, `Aperture`) are
  copied.
- **Not here:** putting the result into an existing complex with the editor (OI-BOOL-3);
  2D Booleans; offsets; incremental evaluation (OI-BOOL-4); curved results.

## 6. Testing

- **Predicates:** against `bigint_oracle.h`; the extreme inputs of each bound in §3;
  collinear and coplanar rays.
- **Point membership (the main oracle).** For random points: each leaf's region by
  `ray_winding` on the operand's triangles (a retry if it is degenerate), combined by an
  independent evaluation of the tree, must equal the label of the arrangement's cell, found
  by a second ray on the facets. It shares no code with §4.2–4.5. On the rounded output,
  points more than 2 quanta from every face, with `locate` by rays.
- **Volume identities,** exactly, as residues modulo a 61-bit prime:
  |A∪B| + |A∩B| = |A| + |B|; |A−B| + |A∩B| = |A|; per label, the sum over the cells.
- **Structure:** every evaluated arrangement is closed around edges; the output is valid
  (`Global`) and `build_regions` finds the labels.
- **Invariance of the arrangement** (exact points, before rounding): signed permutations and
  translations; nesting (A∪B)∪C = A∪(B∪C), IDs and bytes included; (A−B)−C = A−(B∪C) and
  De Morgan inside a box, as sets; child order of commutative operations, as sets.
- **Locality:** adding a leaf far away that adds no faces and shares no label changes
  nothing, byte for byte. One that adds faces leaves all other IDs alone.
- **ID stability:** edit one window of a wall with many; a full-height door; trim a corner;
  a partition flush in an inner corner; move a leaf; swap the nesting; pieces split and
  rejoin; two pieces of one face meeting at a vertex, and along an edge (different labels).
- **Degenerate fixtures:** shared faces, half-shared faces, edge on face (slit), vertex on
  face, edge on edge, vertex on vertex, vertex on edge, coincident operands, a cutter flush
  with a wall, cutters meeting inside a wall, nested shells, operands inside and outside
  each other, a torus, three and more planes through one line and through one point, the
  same snapshot in two leaves.
- **Random:** integer boxes on a small grid (nearly everything degenerate); random convex
  polyhedra; random trees of depth 1–5; models from `gen/buildings.h` minus openings.
- **Determinism:** golden hashes; 1 and 4 threads (S5); every CI configuration.
- **Mutations** of each stage.

## 7. Work items

Each is a commit with a green gate. Files under `source/ubertopo/boolean/`, one per
section of §3 and §4, below 40 KB each.

- **S1 — exact numbers.** `exact.h`, `points.h`: planes, points, tables, classify,
  comparisons, ray predicates, floor and rounding. Tests.
- **S2a — arrangement** (§4.1–4.3), planar and projected operands, box culling. Structural
  checks.
- **S2b — labels, selection** (§4.4, §2.3) on a plain expression; membership and volume
  oracles.
- **S2c — merging, rounding, `Builder`** (§4.5, §4.7), with placeholder IDs. Then measured,
  on random operands and the named fixtures, at 1 mm and 2^-13 m, unrotated and rotated:
  points that fall together, faces that become non-planar, results that are invalid, and
  how many a finer quantum saves. **Decision,** recorded here and in `decisions.md`:
  - OI-1;
  - repair is needed if more than 1 in 1000 of the fixtures' rotated variants fail at
    2^-13 m with `max_quantum_shift` = 4, or if any unrotated one does.
- **S3 — the tree:** `CsgTree`, its edits, hash and serialisation; `evaluate`; `csg_diff`.
- **S4 — names** (§4.6), diagonals, regions, provenance, attributes; stability tests.
- **S5 — speed:** filters, grid or BVH for pairs and rays, threads; `bench_boolean`.
- **S6 — docs:** 02 §5, 06 P5, a new 08 for the engine, DEC-022, index.

**With the BVH branch:** everything up to S4 is new files on committed `topo` interfaces
(`face_triangles` until `FaceTess` is on master). S5 waits for it.

## 8. Open

- **OI-BOOL-1:** the exit criterion in 06 names Manifold and CGAL Nef as oracles. The
  membership and volume oracles of §6 are stronger for what they test, and in-tree. Whether
  to write an out-of-tree comparison too is the owner's decision.
- **OI-BOOL-2:** repair after rounding.
- **OI-BOOL-3:** applying a result to an existing complex, with lineage in the journal.
- **OI-BOOL-4:** incremental evaluation after one leaf changed.
- **OI-BOOL-5:** open sheets as half-spaces (terrain as "below the sheet").
- **OI-BOOL-6:** plane-defined operands, for flush contact at angles that aren't on the
  object's axes (an octagonal tower against a wall).

## 9. Review (fresh context, 2026-11-25, of v1)

- **Verified by it:** the bounds of §3 (run); the midpoint argument; associativity, De
  Morgan and the volume identities as functions; that the loop, pinch and contact rules are
  the validator's. No C++ was written; the arrangement findings are reasoned.
- **Blockers, adopted:**
  - B1: face names collided (two pieces with one anchor). New anchor rule, §4.6.
  - B2: rotated operands lose flush contact (run: < 0.01% stay flush; about one joint in
    ten fails). The tree is modelled in the object's frame and placed once, §2.1, §4.7.
  - B3: slits. §4.3, §4.5.
- **Major, adopted:** the source of a coincident facet (M1); names from dropped geometry
  (M2); missing vertex kinds (M3); one piece and several (M4: a primary piece); region
  naming and equal labels of two leaves (M5); unchanged faces that don't stay planar (M6);
  collapse isn't an algorithm (M7: refuse, finer quantum, measure); predicates and rays
  (M8); stages (M9: S2a–c); merging across planes (M10); thin walls, sheets and apertures
  (M11: presence, §2.3).
- **Minor, adopted:** invariance only before rounding; half-up; curve completeness; gcd;
  snapshots; det −1; cache key; tags; attributes; box culling; file layout; DEC number.
- **Not adopted:** plane-defined operands as the base (its option 2): OI-BOOL-6. It would
  change how generators emit geometry; modelling in the object's frame covers most cases.
