# Repair after rounding (S7 of exact Booleans)

- **Status:** **implemented** 2026-11-25; results and differences from the plan in §11. v3: v2 after one
  fresh-context review (§9) that ran a prototype on the engine; §3.6 again, after experiments with that
  prototype (§10).
- **Parent:** [2026-11-25-exact-booleans.md](2026-11-25-exact-booleans.md) (§4.7 rounding, §7.1 numbers, OI-BOOL-2).
- **Owner, 2026-11-25:** do S7. S5 (speed) may wait for the BVH branch. The comparison with Manifold
  (OI-BOOL-1) is put off.

## 1. Problem

A result is exact until its points are rounded to the output lattice. The rounded faces may then
not form a valid complex, and `evaluate` gives `RoundingFailed`. Failures of 1000 models of random
tetrahedra (`bench_csg_rounding`), any rotation:

| | shift 0 | shift 4 |
|---|---|---|
| 1 mm | 25% | 2.8% |
| 2^-13 m | 4.6% | 0.3% |

- Some 75–80% are two points at one position. The rest are faces that cross (`Invalid`), loops that
  cross (`Build`), a few wrong labels or radial orders.
- Buildings: none at 2^-13 m, some 0.2% at 1 mm.

**Goal:**

- A valid result in all these cases, within a few quanta of the exact one.
- Nothing changes where nothing was wrong: geometry, names and attributes.
- A result that rounds without trouble is byte for byte what it is today.

**Not a goal:** keeping features smaller than a quantum. They may vanish, and the report says so.

## 2. Idea

Round, arrange the rounded faces again exactly, label the cells, keep the faces between cells of
different labels, merge, round the new points, and repeat until the result is valid. (Iterated
snap rounding. It is what CGAL's `autorefine` and libigl do for triangle soups. There is no proof
that it ends, so the number of rounds has a limit. In practice one to three are enough, with §3.6's means for the rest.)

**Why the labels always exist.**

- Let L be a solid label, or void. (Not air: no face of a result lies between air and void, so its surface isn't
  closed. Air plays no part until §3.4.) The result's faces, with +1 where L is in front and −1 where it is
  behind, are the boundary of L's region: a closed surface (a 2-cycle).
- Rounding moves vertices. Triangulate every face (any triangulation of its loops does), and move the
  corners. The surface stays closed, because neighbours share their edges.
- So every cell of the new arrangement has an integer winding number w(L), however badly the faces
  cross. At infinity w(void) = 1 and the others are 0. A face changes +1 and −1, so **Σ w = 1 in every
  cell**.
- Therefore some w(L) > 0. The cell's label is the one with the largest w; of equal ones, the
  void first, then the smallest label ID (a label's index depends on leaves far away: M3). Where nothing was inverted, exactly one w is 1, and the label is the
  one it was.

**Thin content** (the same solid label on both sides) changes no winding number. It is kept wherever its
pieces are.

## 3. A round

Input: a result (faces with points, labels), its rounded positions, names and provenance (§4).

### 3.1 The rounded faces as polygons (`repair.cc`)

An `Input` with one leaf, shift 0, in output quanta (positions are within 2^26 already, else `OutOfRange`
as today):

- **Points:** the distinct positions.
- **Each face:** its loops, without repeated consecutive points. A loop of fewer than three points is
  dropped.
  - If `classify_face` accepts the rounded loops, and for a planar face the CDT does too (`classify_face` doesn't
    look for crossing loops there: B1): a planar face is one polygon, as it is. A projected
    face is the triangles of its T(f), with an `Outline`. This is the surface that the complex will have.
  - Otherwise (*broken*): the exact face is triangulated in its exact plane, and the corners are
    moved. (A broken whole projected face: the triangles that it came from.) Triangles of no area are
    dropped: they change no winding number.
- **Edges:** one `SrcEdge` per pair of positions for edges of the result, and another one for diagonals (B2:
  `try_whole` takes a diagonal that is also some face's edge for a boundary).
- **Triangulating an exact face:** free diagonals in lexicographic order until every cell is a triangle. Free: crosses nothing, passes
  through no point, and starts into the face (a test of the cone at its end). Boxes in `double` cull first.

### 3.2 Arranging

`build_arrangement` as it is, except that polygons of one leaf are paired too (`Input::self`).
Neighbours meet in their common edge or corner, which is nothing new to the arrangement: operands with flush faces do the
same. A facet may have several sources of the one leaf.

### 3.3 Labels (`relabel.cc`)

- A facet's **jump** is the formal sum, over its sources, of (solid label on side 0) − (solid label on side 1).
- **Cells:** the facet sides are joined at every edge, between consecutive uses.
- **Per component** (facets joined by edges), one ray from the middle of one of its edges, in a direction
  (1, t, t²) that is in no plane of the facets there. The direction lies in one wedge of the
  edge. The sum over the polygons it hits gives w in that wedge. Polygons whose plane has
  the origin aren't hit again. From there, w goes over the facets to all cells of the component.
- **Check:** every wedge is reached with one w only, and Σ w = 1. Otherwise `Inconsistent`: an internal
  error, not a failed repair.

### 3.4 Selection

- **Kept:** facets with different labels on their sides, and facets with a thin source.
- **Source:** of those whose own labels are the two chosen ones, in the right order; else of those with a jump;
  else of all: the smallest face name, then the smallest polygon.
- **Air** on a void side: the air label of the source on that side, if it has one there, else that of
  the first other source that does, else none.
- Then `merge_facets` with an `Expression` whose only leaf has the labels as regions. It already knows how to merge
  pieces, make faces legal and keep projected faces whole. (It must not join pieces of opposite orientation: compare the
  polygons' signs.)

### 3.5 Rounding again

With the identity. Integer points don't move any more; only new intersection points are rounded. Then
`write` and `check`, as today.

### 3.6 From the third round on: exact triangles, and other thresholds

Without this, most of what fails is a fixed point of the round (B3): a new point x on a face rounds
to a vertex V, the face bends there, and its T(f) has a diagonal that is the old edge again; or V
pokes through a face, the hole it makes rounds to a point, and the face closes again. So:

- **From the third round on,** a face is triangulated exactly, and its corners are moved, if
  - one of its points shares its position with another point, or its loops lost a point; or
  - it is projected, and wasn't whole.

  Then V stays a corner of the face. Its triangles have different planes, so they stay faces of their
  own, and nothing brings the old diagonal back.
- **Other thresholds:** an attempt that fails starts again from the exact result, and rounds
  differently: floor(x + 1/2 + b), b = ±3/8 per axis, the same in all its rounds. Integer points don't
  move. The first attempt has b = 0; then the eight corners of the cube in a fixed order, but one. Rounding
  moves a point by less than √3 · 7/8 then.

### 3.7 Joining triangles again

A projected face that a new point touches falls into its triangles (M1), also a whole wall of a
rotated building. So before a result is written, pieces of one face with the same labels that meet
in edges with no other face are joined, if `classify_face` accepts their outline. If the joined
result fails, the one that isn't joined is tried, and the next round starts from that one.

### 3.8 When it stops

- **Ok,** or a failure that isn't `RoundingFailed`.
- **After `max_rounds`** (default 16; §11), or a repeat, or beyond the budget: the next attempt; after eight of them
  `RoundingFailed`.
- **A repeat:** the polygons are those of an earlier round, and it is the fourth round or a later one. (In
  the first attempt, if they also are what the result was before: nothing to repair, `RoundingFailed` at once.)
- **Budget:** more faces than 4 times those of the exact result, and 300.

## 4. Names, provenance, attributes

Everything that reads names and sources today reads them from the arrangement. After a repair that is
the wrong arrangement.

- **`Provenance`** (new, a refactoring before anything else): by result vertex (leaf, vertex), by
  result edge (leaf, edge, part), by result face (leaf, face, reversed, part). `copy_attributes`, face flags, `sources`
  and the regions' first leaf read it. A round composes it: the part of a piece is its
  share of the previous face's (or edge's) measure, times that one's part.
- **Conserved columns:** shares are of what is kept of a face (M5), so a face that is cut or partly folded away keeps its whole amount. One that
  vanishes takes it along. Two edges that become one keep one's.
- **Names:** the previous result acts as the operand. `Namer` reads the IDs of source elements through `NameSource`,
  which gets an alternative, *previous names*:
  - a point that was there keeps its name. If several came together: an operand's own vertex first, then the smallest name;
  - only corners of the previous face count as its own vertices (M2);
  - a face that is the only or primary piece of its source keeps its name; other pieces derive from it with an anchor,
    as in parent §4.6;
  - a new point is named by the edges and faces that meet there.
  - So names away from the trouble don't change.
- **Regions** are named as now, from labels.

## 5. API

- `BoolOptions::max_repair_rounds` (default 16; §11; 0: off), part of `evaluation_key`; `kEngineVersion` 2.
- **Order in `evaluate`:** shifts 0 to `max_quantum_shift` without repair, as today; then repair at the last shift whose failure was `RoundingFailed` (M4).
  The default has no shifts, so it repairs at the operands' quantum.
- **`RepairReport`** in `OutputReport`: rounds, points merged, broken faces, new points, faces that vanished (names), cells
  with more than one candidate label. `rounds > 0` says that the result was repaired. `round` (the first failure's
  code) is kept.
- On by default, because a caller that gets a result within the rounding error has what it asked for. A
  caller that wants to know, reads the report.

## 6. Phases

- **R1:** `Provenance` refactoring; no change of any byte (existing tests).
- **R2:** `repair.cc`, `relabel.cc`, the loop in `output.cc`, without names; oracles; measurements.
- **R3:** names, attributes, `csg_diff`; stability tests.
- **R4:** default, docs (08, DEC-022, parent plan), bench numbers.

## 7. Tests

- **Validity:** `check` and `validate(Global)`, as for every result. Random tetrahedra at 1 mm, 2^-13 m and at
  coarse quanta (8 and 2 per metre); buildings at 1 cm; flush and near-flush boxes turned by tiny angles.
- **Labels at points:** random points farther from every exact face than the bound: the label of the repaired complex
  equals the expression's.
- **Distance:** every vertex of the result within rounds · √3/2 quanta of the exact surface.
- **Nothing changed elsewhere:** a building and, far away, a pile of slivers that needs repair. The building's elements have
  the names, positions and attributes they have without the pile.
- **Unrepaired results are the same:** all existing goldens.
- **Hand-made:** a tetrahedron turned inside out; two sheets passed through each other; a sliver that closes; a solid inside another
  that comes to touch it; a thin face that falls onto a wall; a room that loses a wall.
- **Relabelling** against an oracle: random closed, self-crossing label surfaces (moved vertices of a valid result by many
  quanta), winding numbers by rays in floating point; and the same on a grid of ±3, where most faces coincide or fold.
- **Determinism:** the same bytes in all ten CI configurations (a golden of some hashes).
- Mutation checks of all new code.

- **Review's cases:** planar bow-tie; spike (a, b, a); two holes that overlap; a diagonal on another face's
  edge; a sheet folded in its plane; a vertex within half a quantum of a bent edge; a rotated wall that
  itself has the sliver; a far leaf that mentions labels in another order.
- **Volume:** of every label within (area × bound) of the exact one.

**Success:** of 3000 models of each kind, at 2^-13 m none fails, at 1 mm at most 1 in 1000, and no
`Inconsistent` anywhere. Else the numbers and what is left.

## 8. Risks

- **It doesn't end** for some input. Limit, report.
- **Drift:** an edge may be bent in each round. Bounded by rounds × √3/2 quanta.
- **A chain of contacts** that a single rounding makes over a large area: two almost flush large faces
  (within a quantum) turned freely cross each other along many lines, and each crossing cuts them. Result: many small faces. Measured by the near-flush test.

## 9. Review (fresh context, 2026-11-25, of v1)

The reviewer built a prototype of §3 on the engine and ran 15 000 models. Repaired: 740 of 769 failures
(tetrahedra, 1 mm), 125 of 129 (2^-13 m), 238 of 300 (20 quanta per metre), 5 of 5 buildings.

- **B1:** `classify_face` accepts planar faces whose loops cross. → §3.1.
- **B2:** a diagonal on another face's edge is taken for a boundary. → §3.1.
- **B3:** what is left are mostly exact fixed points, not slow convergence; and faces can multiply.
  → §3.6, §3.8, and a criterion that is a rate.
- **M1:** projected faces fall apart. → §3.7. **M2:** foreign vertices as a face's own. **M3:** ties by label
  index. **M4:** `OutOfRange` at a finer shift. **M5:** shares. **M6:** solid labels only. All taken.
- **Minor:** signs in a merge group; tie-break of sources; cost of triangulating; weak tests. All taken.
  The bound √3/2 per round isn't proved (T(f) of a bent face isn't the moved exact one), so the tests
  measure it.
- **Confirmed by running:** closed surfaces, Σ w = 1, the largest w, pairs within one leaf, one ray per
  component, dropping degenerate triangles; widths by reading.

## 10. Experiments (with the reviewer's prototype, B1 and B2 fixed)

Left over after repair, of the failures:

| | plain | v2's hot points | exact triangles from round 3 | with other thresholds too |
|---|---|---|---|---|
| tetrahedra, 20 quanta per metre | 62 of 300 | oscillates | 9 | **0** |
| 1 mm | 29 of 769 | | 8 | **0** |
| 2^-13 m, free | 4 of 129 | | 1 | **0** |
| three labels, 20 per metre | | | 4 of 300 | **0** |
| with sheets, 1 mm, free | | | 10 of 197 | **0** |

- **Hot points** (v2: a triangle that meets the unit cube around a point where points merged becomes a fan)
  fail: two such points next to each other flip the diagonal of one thin quadrilateral back and forth.
- **Time** per repaired model: 10 to 20 ms on average, 0.8 s at most. Faces: 45 → 61 on average at 1 mm.
- 97–99% need one attempt only, none more than three.

## 11. Results (2026-11-25)

`bench_csg_rounding 3000`: 3000 models of each kind, three orientations, shifts 0 to 4.

| | evaluations | repaired | left | rounds (mean, max) | faces more (mean) | ms (mean, max) |
|---|---|---|---|---|---|---|
| buildings, 1 mm | 45 000 | 30 | 0 | 1.07, 2 | 0.8 | 13, 32 |
| buildings, 2^-13 m | 45 000 | none needed | 0 | | | |
| tetrahedra, 1 mm | 45 000 | 4835 | 1 | 1.40, 16 | 5.4 | 12, 895 |
| tetrahedra, 2^-13 m | 45 000 | 846 | 0 | 1.26, 16 | 3.5 | 11, 528 |

- **The criterion of §7 is met:** none at 2^-13 m, 1 in 45 000 at 1 mm, no `Inconsistent`.
- **Harder than that:** piles of tetrahedra of three labels with a common plane, coordinates up to 100, freely
  turned: 4 of 840 failures are left, all with `Build` as the last code. One that was traced (with 8 rounds) had
  `Build` in every round of every attempt that was looked at, while the number of faces grew. `test_csg_repair` allows 1 in 50 of its cases, and needs none now.
- **Distance:** at most 2.98 quanta, usually under 1.

What differs from the plan:

- **16 rounds instead of 8.** With 8, 1 of some 330 failing test models was left. With 16, none of them is.
- **Faces share by root** (§4): pieces of one face of the exact result share its amount by their area, however
  the rounds cut it. Composing shares round by round lost some when a piece vanished.
- **A face that still has pieces keeps its name** on one of them, even if the piece that had it vanished.
- **Joining** (§3.7): a group of pieces that isn't a face as a whole is joined edge by edge, as far as it goes
  (groups of at most 200). In the wall test this left 189 small pieces instead of 1247.
- **A vertex can take the place of one that went away.** So a position doesn't imply a name.
- **`reversed`** of a face is that of the exact result's: what was outside of the solid still is.

Tests: `test_csg_repair.cc` (the whole: validity, labels, distance, volumes, names, provenance, locality, options,
a golden) and `test_csg_repair_parts.cc` (reading, winding numbers, selection, triangles, joining).

Mutants: 120. Not killed, and why:

- equivalent: "better input" (input points have the smaller indices);
- guards that no test triggers: "wind known", "wind plane", "name joined plane", "ray side" (a ray that is
  parallel to a polygon);
- cost only: "seen" (a repeat is also ended by the limit of rounds);
- not reached, it seems: "sel air" (two airs on one side of coincident faces, the source not being the first).

Not done:

- Repair starts over for the whole result. The trouble is local, so an arrangement of its
  neighbourhood would do (with S5).
- The test of hopeless failures uses the false `TouchingShell`, and needs another failure once plan 2026-11-24-bvh's
  W4a is merged.
