# Plan: connect_loops and disconnect_loops

Status: **implemented** (v2, after a fresh-context review, §8; notes in §9).

## 1. Why, and what it becomes

- **The need.** `split_face` and `imprint` cut a face between two vertices of its outer
  loop, and refuse a cut that meets a hole (`HoleOnChord`, `NotOuterLoop`). A ring-shaped
  face (a courtyard floor, a wall with a window cut in two through the window, a road
  around a square) can't be cut across by any sequence of primitives.
- **Design 01 §7 lists** `connect_loops(f, c_outer, c_hole)`: an edge that joins a hole to
  the outer loop, leaving one loop and one face on both sides of that edge. It is to be
  used transiently and never left in output.
- **That form doesn't fit the editor as built:**
  - the state after it is invalid (`FaceEdgeTwice`), and every primitive validates its
    result in `Full` and `Local`. It could run only in `OnCommit` and `None`;
  - the merged loop visits two vertices twice, so it isn't a simple polygon. A projected
    face has no T(f) then, so its radial order can't be computed;
  - vertices no longer name a place in a loop, so `split_face(f, a, b)` would be ambiguous
    at exactly the vertices that matter.
- **Proposed instead:** one primitive for the whole cut, valid before and after. A cut
  starts and ends on the outer loop and may pass through holes on the way. `split_face`
  is the case with no hole. This is what the transient form was for: it never had another
  use in the designs.
- **Not in scope:** cuts that avoid the outer loop (hole to hole and back), embedded wires
  or curves as connectors (chords only), several cuts at once, surfaced faces
  (`SurfacedFace`, as for every split). §6 lists them.

## 2. Interface

```cpp
/** Splits f along a cut through holes. path has 2(k + 1) vertices, k >= 1: the
 * chords path[0]-path[1], path[2]-path[3], ... path[0] and the last are
 * different vertices of the outer loop. path[2i-1] and path[2i] are different
 * vertices of one hole, a different hole for each i: the cut enters there and
 * leaves there. The child to the left of the first chord keeps f's slot. */
EditCode connect_loops(FaceRef f, std::span<const VertRef> path, FaceRef *other = nullptr,
                       EdgeRef *first_chord = nullptr);

/** The inverse: joins the two faces on either side of e across every edge
 * they share, and removes those edges. */
EditCode disconnect_loops(EdgeRef e, FaceRef *survivor = nullptr);
```

## 3. connect_loops

- **The children.** Let the cut be a = o₀ → i₁ (hole 1) o₁ → … → i_k (hole k) o_k → b.
  Every loop has the face on its left, and `next` is followed in both children:
  - left child: the outer loop from b to a, then chord 0, hole 1 from i₁ to o₁, chord 1,
    …, hole k from i_k to o_k, chord k;
  - right child: the outer loop from a to b, then chord k reversed, hole k from o_k to i_k,
    …, chord 0 reversed.

  Each hole's coedges are divided between the children, so the k holes are used up.
- **One core.** `split_core` (`editor_impl_imprint.h`) is generalised from one chain to a
  list of connectors with a hole arc between neighbours. With k = 0 it must do exactly
  what it does now: same allocation order, same rows, same errors in the same order.
  `split_face` and `imprint` keep calling it.
- **Checks** (before any write):
  - `DeadElement`, `SurfacedFace`; `BadArgument`: odd or short path, dead vertices,
    `path[0]` equal to the last;
  - `NotInLoop`: the first path vertex that is on no loop of f. Then `NotOuterLoop`: an end
    that is on a hole, or a vertex in between that is on the outer loop;
  - `ChainNotSimple`: an entry and exit on different holes, entry equal to exit, or a
    hole used twice;
  - `EdgeExists`: an edge already joins a chord's vertices. `DuplicateId`: as in
    `split_face`;
  - `DegenerateLoop`: a child all on one line;
  - `HoleOnChord`, `InclusionOnChord`: other holes and all inclusions go to the child that
    contains them, wires by the rule of W0 (§7).
  - After writes, and rolled back: `RadialTie`, `NotProjectable`, `DegenerateLoop` from
    `resort_face_edges`, and `PropagationFailed`.
  - A chord that crosses the outer loop, a hole or another chord isn't checked here, as it
    isn't in `split_face`: the validator reports it (`LoopIntersection`, or
    `SkeletonDegenerate` at a curved edge).
- **IDs:** children `derive(OpDerived, f.id, "connect_loops", 0 or 1)`, left and right.
  Chords `edge_id`. Loops `loop_id(face, ordinal)`, the cut loop first.
- **Rows:** k + 1 edges in path order, then per chord the right and the left child's coedge
  (as `x1[i]`, `x2[i]` now), the new face and its loop are allocated; the k hole loops are
  freed last, in path order.
- **Propagation:** faces and sides split by area, as today. The right loop copies the outer
  loop; the left keeps its row. The freed holes' loop attributes are lost, as when a hole
  is removed. A chord's coedge copies the parent's coedge that starts at the same vertex, as
  `split_core` does at a and b.
- **Lineage:** two `Split` records from f, as today.

## 4. disconnect_loops

- **Shared edges:** all edges with a coedge on both faces. Each must have exactly two
  coedges with opposite senses (`NotJoinable`), all on one loop of each face.
- **Their vertices are distinct,** and the faces share no other vertex, on any loop
  (`NotJoinable`). Chords that meet are one chain, which is `unimprint`'s case. So once a
  chord has been split by `split_edge`, the cut can't be taken back until `join_edge` has
  restored it.
- At least two shared edges; one is `join_faces`' case, refused here with `NotJoinable`.
- **Compatibility** as in `join_faces`: flags, surface, hints, coplanar unless projected,
  no surfaced face. The survivor is the smaller face ID.
- **Relinking.** At each shared edge the two loops are spliced as in `join_faces`. The m
  splices turn two loops into m cycles. Every cycle has coedges of the survivor's loop.
  Cycles are found by a walk along that loop from its first coedge (`.l.c`), and each one
  starts at its first coedge in that walk. Nothing depends on which shared edge was given.
- **Which is first:** if the survivor's loop was its outer loop, the new outer loop (by
  `outer_loop_proj`, on the skeleton) takes the survivor's loop row and place, and the
  other cycles follow in walk order. Otherwise (a strip across a hole) the first cycle in
  walk order does.
- **Rows:** the new loops are allocated first; then edges, coedges in walk order, the
  absorbed loop and the absorbed face are freed.
- **The number of cycles must be m,** else `NotJoinable`: the union isn't planar. Gluing two
  disks along m segments gives m − 2g cycles. For planar faces g is 0; overlapping
  projected faces can have g > 0 (a tripod over a disk).
- **Loop order:** the survivor's loops, then the new cycles, then the absorbed face's other
  loops. If the absorbed face held the outer loop (the survivor is the strip across a
  hole), the whole chain is put in the order: outer loop, the rest. A loop chain starting
  with the outer loop isn't an invariant, but `unimprint` relied on it (W0).
- **Propagation:** faces and sides as in `join_faces`. Loops: the two are merged, then each
  new loop is a copy (`Split`, weight 1) of the survivor's, so a conserved loop column is
  duplicated, as `split_core` does. Lineage as in `join_faces`.
- **Not an exact inverse.** IDs and slots differ from the state before `connect_loops`, as
  for `split_face` and `join_faces`. A projected face whose child became planar can't be
  joined back (`Incompatible`, or `NotJoinable` if both did). Undo is exact, through the
  journal.

## 5. Testing

`test_edit_connect.cc` and `test_edit_connect_random.cc`, in `Lattice`, `double` and `float`, in all four modes:

- **Cases:** a ring with one hole; two and three holes in a row; other holes and inclusions
  on both sides; a hole filled by another face (window), its edges shared with walls;
  adjacent ends on the outer loop and on a hole; concave outer loops; flipped faces; a
  projected terrain face with a hole; curved outer and hole edges; a strip across a hole,
  with either face as survivor; a ring bounded by arcs, horizontal (Ok) and in a tilted
  plane (refused: the child's stored vertices are collinear and its rounded skeleton isn't
  planar; `split_face` has the same gap).
- **Wires** from the outer loop, from cut and uncut holes, and between them, with the cut
  given in both directions.
- **Loop order:** connect through an imprinted window, disconnect, then unimprint the
  window.
- **A tripod** for the cycle count.
- **Any shared edge** given to `disconnect_loops` gives the same hash.
- **Reuse:** a later edit takes the freed loop slots, then undo and redo; `compact` between
  connect and disconnect.
- **Against a fresh build:** the children, as sets of vertex loops, equal those of a
  complex built by the builder from the same loops; regions after `rebuild_regions` equal
  those before, with hints.
- **Each refusal** of §3 and §4, leaving the hash unchanged.
- **Chords that cross** a hole, the outer loop or each other: `Invalid` in `Full` and
  `Local`.
- **Round trips:** connect, then disconnect: the same loops as vertex cycles, and the same
  regions. Undo and redo exactly, with verify_redo.
- **Random:** ring and plate faces with 1–6 holes, random valid cuts, in sequences with
  undo, redo, splits and joins. Full and Local agree, and every step is compared with a
  fresh build. `test_edit_random` and its golden don't change.
- **`split_face` and `imprint` unchanged:** all goldens stay, and a temporary A/B hook
  compares the old `split_core` with the new one on every call of the test suite.
- **Index, grid, region hints** (`*_consistent`) after every step.
- **Propagation:** areas add up; sides and corners as specified.
- **Mutations:** each check dropped; the arcs swapped; a hole's arc reversed; freed holes
  kept in the chain; loop sizes off by one; the cycle count unchecked; IDs swapped;
  other holes all to one side; `refresh_ec` and `resort_face_edges` skipped; holes
  classified against the left child; a shared edge with a third coedge; a shared vertex on
  another loop; a chord's corner copied from the wrong end.

## 6. Open

- **OI-CL-1:** cuts between holes that avoid the outer loop. They give a child and a face
  with a merged hole, and which is which needs the winding.
- **OI-CL-2:** embedded wires or curved connectors. The core takes chains, so this is a
  matter of the interface and its checks.
- **OI-CL-3:** surfaced faces, with OI-P4b-2.

## 7. Work items

- **W0: two bugs found by the review,** a commit of its own:
  - `unimprint` takes a face's first loop for its outer one, and refuses an island whose
    host has a hole first. `join_faces` can produce that. Fix: find the outer loop by
    `outer_loop_proj`.
  - `split_core` places an embedded wire by its endpoints' positions alone. A wire from
    the right child's loop is refused (`InclusionOnChord`), one from the left child's is
    accepted, so the answer depends on the cut's direction. Fix: an endpoint that is a
    vertex of f's loops counts for the child whose loop it is on (both, for the cut's own
    ends); any other endpoint by its position. Endpoints that disagree are refused. If both
    are on the cut's ends, the wire's midpoint decides, exactly. A position on the cut
    is refused.
- **W1:** `split_core` moves to `editor_impl_split.h` and is generalised, with no new
  behaviour. A temporary A/B hook compares hash, allocation log and report, also for
  failing calls.
- **W2:** `connect_loops`: op record, redo, tests. New file `editor_impl_connect.h`.
- **W3:** `disconnect_loops`: the same.
- **W4:** mutations; docs: 01 §7, 06 (P3 done but for incremental regions), 07 §14 (its "not
  in P3a" list too) and §15, DEC entry for the changed form, index.

New `OpKind`s go at the end of the enum. The BVH plan moves `FacePoints` to another header
and changes nothing in `split_core`.

## 8. Review (fresh context, 2026-11-24, of v1)

- **Confirmed:** the surgery (both children of a cut through one hole, built by hand around
  a filled window, pass `Full` and a region rebuild); the composite form, and that nothing
  in docs or code needs the transient one; that `join_faces` and `unimprint` refuse the
  result; m splices give m − 2g cycles; that generalising `split_core` is better than a
  second function; no other invariant is missed.
- **Findings, all adopted:**
  1. **Major:** disconnecting put a hole first every time, and `unimprint` relies on the
     outer loop being first (run). Order fixed in §4; `unimprint` fixed in W0.
  2. **Major:** wires on the cut loops are placed by direction (run). W0.
  3. **Minor:** projected children that flatten can't be joined back; the cycle count needs
     a projected fixture; strips across a hole; loop propagation; chord corner sources; the
     refusal list; rings of arcs in tilted planes; starting points and allocation order; a
     split chord; a chord output; tests and mutations; file sizes; docs.
- **Not verified by it:** k > 1, curved and projected cuts.

## 9. Implementation notes (2026-11-25)

- **Files:** `editor_impl_split.h` (the core), `editor_impl_connect.h` (both primitives).
  Tests: `test_edit_connect.cc` (W0), `test_edit_connect_loops.cc`,
  `test_edit_connect_geom.cc` (projected and curved), `test_edit_connect_random.cc`.
- **W1 A/B:** a trace of every primitive (code, report, allocation log, content hash)
  before and after the move: 1,503 split and imprint calls in nine tests, 882 of them
  refused, the same.
- **Changed in §4:** the cycle that takes the survivor's row is always the one
  `outer_loop_proj` picks, also for a strip across a hole, where every cycle is a hole.
- **The tilted arc ring** of §5 isn't tested: `alignment3` carriers can't express an arc in
  a tilted plane.
- **Random test:** 100 seeds, 798 cuts (68 through two or three holes) and 489 joins, no
  failure. 12 seeds run in CI.
- **Mutations:** 89 of the two primitives and the core. They found gaps in the tests:
  - holes were all convex, so the arcs of cut holes could be left out of the side test
    (now: U-shaped holes with a hole in the notch);
  - radial order of projected faces and the first coedge of a cycle weren't checked;
  - an edge elsewhere with a chord's ID, coplanarity across collinear chords, a shared
    vertex between chords, an outer arc that bulges past a hole.

  Three checks turned out redundant and were removed. What survives is equivalent: the arc
  points of the left child in the degeneracy test; the two-coedge count (repeated later);
  one face on both sides of an edge (invalid before); shared edges on other loops (the
  cycle count refuses them); the survivor's order (radial cycles start at the smaller ID);
  `fix_projection` after a join; the survivor's own lineage record.
