# 08 — Exact Booleans (as implemented)

- **Status:** S1 to S4 of the [plan](../../plans/2026-11-25-exact-booleans.md), and S7
  ([repair after rounding](../../plans/2026-11-25-boolean-repair.md)), implemented 2026-11-25.
  Open: S5 (speed; waits for the face BVH).
- **Scope:** the API and the contracts of `source/ubertopo/csg/`. Why it is so, proofs and
  measurements are in the plan.
- **Decision:** DEC-022.

## 1. Model

- **The CSG tree is what is stored and edited** (`CsgTree`, a value). A solid is always derived
  from it by `evaluate`, all at once, and is never an operand itself. So rounding doesn't
  accumulate, and names don't depend on nesting.
- **Operands** are immutable `Complex<Lattice>` in an `OperandStore`, by `content_hash`. They may have
  several regions, thin walls, sheets, projected faces and curved edges (as their skeletons).
  Refused: surfaced faces, wires, inclusions.
- **Leaves** place an operand by a signed axis permutation and an integer translation. Both are exact.
- **Every region of a leaf has a label:** solid, air or void, with a name. Equal solid names fuse.
- **Operations:** `Union`, `Intersection`, `Difference`. Child order is priority where operands overlap.
- **Faces of a result** are where the solid label changes, and the thin content of leaves with `thin`.

## 2. Modules

| File | Contents |
|---|---|
| `exact.h`, `points.h` | planes, homogeneous points, predicates with fixed integer widths; tables |
| `input.h/.cc` | operands as polygons |
| `pairs.h/.cc`, `subdivide.h/.cc`, `planar.h/.cc` | intersections; facets in a plane |
| `arrangement.h/.cc`, `label.cc` | the arrangement; every leaf's region on both sides of a facet |
| `select.h/.cc` | the meaning of the expression; the facets that are kept |
| `merge.h/.cc`, `legal.h/.cc` | faces of the result; diagonals where the complex can't hold one |
| `names.h/.cc` | names of vertices, faces and regions |
| `provenance.h/.cc` | which operand element a vertex, edge or face comes from, and what share of it |
| `attributes.h/.cc` | columns from the operands |
| `repair.h/.cc`, `relabel.cc`, `triangulate.h/.cc` | a rounded result as polygons; labels from winding numbers; joining pieces again |
| `output.h/.cc` | placement, rounding, `Builder`, regions, checks |
| `tree.h/.cc`, `evaluate.h/.cc`, `diff.h/.cc` | the tree, its storage, `evaluate`, `csg_diff` |

## 3. Contracts

- **All or nothing.** A failed edit of a tree, or a failed `evaluate`, changes nothing. The code says
  why, and `BoolReport::node` says where, if it is one node's fault.
- **Exact until the result is complete.** Then each point is placed (one rotation and translation for
  the whole result) and rounded once, independently of how it was computed.
- **Checked after rounding:** positions are distinct, the radial order about every edge is the exact
  one, every region has one solid label, `validate(Global)`.
- **If the check fails:** first a finer quantum, if `max_quantum_shift` allows one, then repair
  (`max_repair_rounds`, 16 by default; 0 turns it off), then `RoundingFailed` with a `RoundCode`.
- **Repair** arranges the rounded faces again exactly and labels the cells by winding numbers.
  - The result is valid like any other. It can lose features smaller than a quantum.
  - `BoolReport::output.repair` says that it happened (`rounds`, `attempts`), why (`first`), and how many
    faces `vanished`.
  - Vertices lie within a few quanta of the exact surface (3 at most in the tests, 1 as a rule).
    There is no proof of a bound.
  - A result that rounds without trouble is the same with repair on or off.
- **Deterministic.** The bytes of a result depend on the tree and the options only (`evaluation_key`).
- **Local.** A leaf that adds no face and meets no other leaf changes no byte of a result. One that
  meets others can change names at what it touches, if it comes before them in the tree. Repair keeps
  names, positions and attributes away from the trouble, with one exception: if `attempts > 0`, all
  points were rounded with other thresholds, so points that aren't integers can be one quantum off
  from where they would be, anywhere in the result.

## 4. Names

- **Vertex:** the operand's own vertex, or the two or three source elements that meet there.
- **Face:** its source face, if it is the only or the primary piece; else with an anchor.
- **Region:** its label's name; further parts of one label have an anchor.
- **After a repair** what was there keeps its name. A face in pieces has its name on one of them, and
  a new point is named by what meets there.
- **What isn't stable** is listed in plan §4.6 and §7.4, and `csg_diff` reports it as `moved`.

## 5. Limits

- **Range:** operands within a box of ±2^26 quanta (8 km at 2^-13 m).
- **Rounding can fail for good,** because repair has no proof that it ends. Of 5711 evaluations that
  needed it, one was left (random slivers at 1 mm). Piles of thin solids of several labels on a grid of
  some 100 quanta: 4 of 840. Buildings at 2^-13 m never needed it in 45 000 evaluations.
- **A solid that is flush with a ring around it** is refused by `build_regions` until plan
  2026-11-24-bvh is merged (plan §7.2, §7.5).
- **Speed:** every pair of boxes is tried, on one thread. A building of 150 faces takes about 10 ms.
- **Not done:** applying a result to an existing complex, incremental evaluation, corner columns.
