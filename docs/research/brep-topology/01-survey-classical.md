# 01 — Survey: classical B-rep topology structures (literature)

Part of [B-rep topology research](README.md). This file covers the data structures
from the academic literature, from winged-edge (1970s) to partial-entity (2001).
Industrial kernels and libraries are in [02-survey-systems.md](02-survey-systems.md).

The main question for each structure: **what can it represent, and what does it
cost?** The things we test for are non-manifold edges and vertices, faces with
holes, explicit regions/volumes, and mixed-dimensional elements (wires, isolated
vertices, sheets).

> Note on sources: the citations were found with web search. Full texts could not be
> fetched from this session, because outbound fetches to most hosts are blocked. Some
> structural details below therefore come from my background knowledge of the
> literature. Where a detail is not confirmed by a source I could reach, it is marked
> *(unverified detail)*.

---

## 1. Vocabulary: the three cycles

Every boundary-representation (B-rep) topology structure has to answer three cyclic
adjacency queries.

| Cycle | Question | Manifold case | Non-manifold case |
|---|---|---|---|
| **Loop cycle** | Edges around a face boundary, in order | One per face, or one per face boundary component | Same |
| **Radial cycle** | Faces around an edge, in angular order | Always exactly 2 faces, so trivial | k ≥ 0 faces; the angular order matters for region assignment |
| **Disk cycle** | Edges/faces around a vertex | One fan, which you can walk with the next/twin pointers | Several fans ("zones"), dangling edges, cones touching at an apex |

Manifold structures (winged-edge, half-edge, quad-edge) store the loop cycle and get
the other two for free, because the radial cycle has exactly two elements. Non-manifold
structures must store the radial cycle explicitly. Some also store the disk cycle;
others derive it.

A second axis is **uses**. A *use* is an oriented incidence of a lower-dimensional
entity in a higher one: the edge-in-a-loop (coedge, fin, BMLoop, half-edge), the
face-side bounding a region (face-use, half-face), and so on. Most of the design space
comes down to *which uses are stored explicitly*.

---

## 2. Winged-edge (Baumgart, 1972/1975)

```
          \  ccw_left       cw_right /
           \                        /
            v_end ---------------- 
            |        EDGE e        |
  left face |  -----------------> | right face
            |                      |
            v_start --------------
           /                        \
          / cw_left        ccw_right \
```

* **Record per edge:** 2 vertices, 2 faces, and 4 "wing" edges (prev/next on the left
  face, prev/next on the right face). Each vertex and each face stores one edge.
* **Strength:** a fixed-size edge record, compact for its era.
* **Weakness:** edge direction is arbitrary relative to each face. So every traversal
  step must test "am I the left or the right face of this edge?" This branch is the
  classic source of bugs and of mispredicted branches. Manifold only. Faces with holes
  need an extra loop entity (Braid's and Mäntylä's extensions).
* **Verdict:** historical. Every modern design replaces it with oriented uses
  (half-edges or coedges), which remove the orientation test.

References: Baumgart, "A polyhedron representation for computer vision", AFIPS 1975.

## 3. Half-edge / DCEL (Muller & Preparata 1978; Mäntylä 1988; Kettner 1999)

* Each edge is split into two oppositely oriented half-edges. Each half-edge stores
  `twin`, `next` (optionally `prev`), `vertex` (origin or target), and `face`. Each
  vertex stores one outgoing half-edge, and each face stores one half-edge.
* **Mäntylä's GWB** (Geometric WorkBench) wraps half-edges in a full solid hierarchy:
  `Solid → Face → Loop → HalfEdge → Vertex`. Faces have **multiple loops**, so holes
  ("rings") are first-class, and the Euler operators `kemr`/`mekr` create and destroy
  rings. This is the textbook manifold solid modeler. Mäntylä's book is still the best
  single reference for the operator layer.
* **Kettner's CGAL design** (1999) separates combinatorics from geometry. It is the
  ancestor of CGAL `Polyhedron_3` and `Surface_mesh`.
* **Limits:** a half-edge has exactly one twin, so an edge has ≤ 2 faces. A vertex
  stores one outgoing half-edge, so it has a single fan. Both limits rule out
  non-manifold edges and non-manifold vertices. There is no region concept beyond
  "solid".
* **Index-based variants** (OpenMesh, CGAL Surface_mesh, PMP, geometry-central) store
  half-edges in pairs, `twin(h) = h ^ 1`. This removes the twin pointer entirely and
  makes the structure very cache- and GPU-friendly (Dupuy & Vanhoey 2021 refine
  half-edge meshes fully in parallel on the GPU).

**Verdict:** the gold standard for *manifold surface* processing. It cannot be our
master structure, but it is an excellent **derived/export** format for per-object
manifold surfaces: rendering LODs, subdivision, remeshing.

## 4. Quad-edge (Guibas & Stolfi, 1985)

* One record per edge holds 4 directed edges: the edge, its reverse, and the two dual
  edges. Navigation uses `Onext` and `Rot` (plus `Flip` for non-orientable surfaces).
  The single topological operator is `Splice`.
* It represents a subdivision **and its dual** at the same time. This is elegant for
  Delaunay/Voronoi computations.
* Faces are *orbits* of `Lnext`, so a face is by definition a single boundary
  cycle. **Faces with holes cannot be expressed** except with bridge edges.
  Manifold 2D subdivisions only.
* **Verdict:** a niche structure (Delaunay, planar subdivisions), but `Splice` is a
  beautiful lesson: one closed operator can generate all topology edits. Generalized
  maps follow the same "orbits define cells" philosophy, with the same holes
  limitation.

## 5. Radial-edge structure (Weiler, 1986 thesis; 1988)

Weiler's thesis *Topological Structures for Geometric Modeling* (RPI, 1986) is the
foundational non-manifold B-rep. The hierarchy is:

```
Model
 └─ Region (a volume; there is always one unbounded "outside" region)
     └─ Shell (a connected boundary component of a region; may be a lone vertex or a wire)
         ├─ FaceUse  (2 per face: one per side; mate pointers link the pair)
         │    └─ LoopUse (2 per loop)
         │         └─ EdgeUse (2 per edge-in-loop, one per face side; radial + mate pointers)
         │              └─ VertexUse
         ├─ EdgeUse (wire edges directly in a shell)
         └─ VertexUse (isolated vertex)
```

* The **radial cycle** is stored: edge-uses around an edge are linked in angular order
  by `radial` pointers, and `mate` links the two sides of the same face. The disk cycle
  is **not** stored. Vertex-uses form a plain list per vertex.
* Supports non-manifold edges, wire edges, isolated vertices (a "vertex shell"),
  faces with holes (several loops per face), and **explicit regions and shells**.
* **Cost:** every face-edge incidence produces *two* edge-uses (one per face side),
  every face two face-uses, and so on. Lee & Lee measured the partial-entity structure
  at about **half the storage of radial-edge**.
* **Verdict:** the conceptual blueprint for what we need. Its "everything has a use
  on both sides" redundancy is mostly legacy. The orientation of a face-side can be a
  single bit instead of a duplicated object.

## 6. Vertex-based representation (Choi 1989; Gursoz, Choi & Prinz 1990)

* Extends radial-edge thinking with explicit **vertex neighbourhoods**: *disk* cycles
  and *zones* (the regions of space around a vertex), so that non-manifold *vertices*
  can be resolved. Examples are two cones touching at their apices, or two cubes
  sharing only a corner.
* Motivation: the radial-edge structure alone cannot tell which region a non-manifold
  vertex's neighbourhood belongs to without geometric computation.
* Gursoz, Choi & Prinz used it for Boolean operations on non-manifold objects (CAD
  1991).
* **Verdict:** the problem is real. Our domain has, for example, a lamp post touching
  a road surface at a point, and drone geometry touching a roof. Storing zones
  permanently is expensive, though. A modern answer is to compute the vertex
  neighbourhood on demand, the way CGAL Nef polyhedra do with *sphere maps* (a planar
  map on a small sphere around the vertex), and cache it only for the rare
  non-manifold vertices.

## 7. Masuda: complex-based non-manifold models (1993)

Masuda, "Topological operators and Boolean operations for complex-based nonmanifold
geometric models", *CAD* 25(2), 1993.

* The model is a **cell complex**: vertices, edges, faces, and volumes, with
  non-manifold adjacency allowed.
* He gives a **generalized Euler–Poincaré formula** for these complexes and a matching
  set of non-manifold Euler operators. See
  [05-invariants-operators-validity.md](05-invariants-operators-validity.md).
* **Booleans as "merge, then select":** all operand cells are merged into one complex
  that keeps *every* intersection cell. A Boolean result is then just a *selection* of
  cells. Changing union to difference re-selects without recomputing intersections.
* **Verdict:** highly relevant. "Merge + select" is the right model for procedural
  pipelines and LOD (re-evaluate cheaply, keep provenance). It is also the same idea as
  SGC and as modern *mesh arrangements* (Zhou et al. 2016).

## 8. Selective Geometric Complexes (Rossignac & O'Connor, 1989)

"SGC: A dimension-independent model for pointsets with internal structures and
incomplete boundaries" (IFIP *Geometric Modeling for Product Engineering*, 1989).

* An SGC is a set of **mutually disjoint open cells of any dimension**. Each cell is a
  connected open manifold, not necessarily a ball, with a subset of its boundary.
  Incidence is stored as a Hasse-diagram-like graph. Each cell carries a **selection
  bit** (and, more generally, attributes).
* It represents **internal structure** (cells inside other cells: a wire inside a
  volume, a face embedded in a volume), **incomplete boundaries** (open sets: a face
  without its boundary edges), and **mixed dimensions** uniformly.
* Operations follow a three-step pattern: *subdivide* (make two complexes compatible),
  *select* (set bits), *simplify* (merge adjacent cells with identical attributes).
* The follow-ups are Rossignac's *Structured Topological Complex* (1997) and
  Rappoport's *Generic Geometric Complex* (GGC), which addresses invariant naming for
  families of shapes.
* **Verdict:** the best *theoretical* frame for this project. "Subdivide / select /
  simplify" with per-cell attributes maps directly onto material regions, LOD, and
  semantic labelling. Its cells are not required to be balls, which matches faces with
  holes and regions with cavities. What SGC lacks is a concrete, efficient storage
  layout. That is what the later structures provide.

## 9. Yamaguchi & Kimura: coupling entities (1995)

"Nonmanifold topology based on coupling entities", *IEEE CG&A* 15(1), 1995.

* Scope: cell decompositions of 3D Euclidean space. Instead of uses, it stores
  **coupling entities** between adjacent pairs of primitive entities (vertex–edge,
  edge–face, face–region, and so on, six kinds), and these carry the ordering
  information: radial order around edges and cyclic order around vertices.
* Emphasis on representing **ordering** compactly, and on two classes of operators
  (neighbourhood and boundary).
* **Verdict:** confirms the right abstraction: *adjacency relations between cells,
  with ordering where geometry demands it*. A coupling entity is a general version of
  "use". Every design is a choice of which couplings to materialize.

## 10. Partial Entity Structure (Lee & Lee, 2001)

S.H. Lee & K. Lee, "Partial entity structure: a compact non-manifold boundary
representation based on partial topological entities", ACM SMA 2001. Journal version
in *ASME JCISE* 1(4), 2001.

* Entities: region, face, edge, vertex, plus **partial-face**, **partial-edge**, and
  **partial-vertex**. These capture non-manifold conditions only where they occur.
* The authors report about **½ the storage of radial-edge** with no loss of adjacency
  queries, and give complexity derivations for all adjacency queries.
* As I understand the design *(unverified detail)*, the saving comes from not
  duplicating uses per face side. A partial-edge is the use of an edge by a face, not
  by a face *side*, so a face's two sides share one set of partial-edges, and the side
  becomes an orientation flag. Loops and shells become lighter (or derivable).
* The successors are **Q-Complex** (Tsinghua, *CAD* 2012), which adds *inclusion
  topology* (lower-dimensional cells embedded inside higher ones) and claims better
  compactness with auxiliary data, and **X-maps**.
* **Verdict:** the closest prior art to our recommended layout: radial-edge semantics
  at roughly half-edge storage cost.

## 11. Generalized and combinatorial maps (Lienhardt, 1991/1994)

Lienhardt, "N-dimensional generalized combinatorial maps and cellular
quasi-manifolds", *IJCGA* 4(3), 1994. The standard reference is Damiand & Lienhardt,
*Combinatorial Maps* (CRC, 2014).

* A **dart** is a (vertex, edge, face, volume) "flag". An n-G-map is a set of darts
  with n+1 involutions α0…αn. Cells are **orbits**: for example, a face is the orbit
  ⟨α0, α1⟩. The *combinatorial* map (n-map) halves the darts by using orientation
  (β1 is a permutation, the others involutions), but it is orientable only.
* Dimension-independent: the same code handles 2D, 3D, and nD. The operations are
  `sew` and `unsew` along αi. Attributes attach to i-cells.
* **Represents:** *quasi-manifold* cellular complexes, meaning manifold-like gluing
  where each (i−1)-cell is shared by at most two i-cells *along its full extent*.
  Two volumes sharing a face works well (3-sew). It handles many non-manifold *edge*
  situations inside 3-complexes.
* **Does not natively represent:** (a) vertex-only or edge-only contacts between
  volumes, which must be modeled as separate darts that share an embedding attribute.
  That is identification by geometry, not by topology. (b) Dangling faces and wire
  edges, which are awkward: free αi darts. (c) **Faces with holes**, because a face is
  an orbit and therefore connected. Damiand's work adds *inclusion trees* or fictive
  edges to work around this.
* **Cost:** a cube needs 48 darts in a 3-G-map or 24 in a 3-map. Each dart holds
  n+1 links plus attribute links. Expect roughly 2–3× (G-map) or 1–1.5× (map) the
  memory of a coedge-based structure, with correspondingly more records touched per
  traversal. Detailed comparison in
  [04-surface-vs-volumetric.md](04-surface-vs-volumetric.md).
* **Verdict:** excellent for volumetric meshing, image segmentation, and
  dimension-generic algorithms. A poor fit as the master model for mixed-dimensional,
  hole-rich environment data.

---

## Summary table

| Structure | NM edge | NM vertex | Holes in faces | Regions | Wire/acorn | Sheets | Memory (relative) |
|---|---|---|---|---|---|---|---|
| Winged-edge | ✗ | ✗ | via extension | ✗ | ✗ | boundary only | 1× |
| Half-edge (GWB) | ✗ | ✗ | ✓ (rings) | solid only | ✗ | boundary only | 1× |
| Quad-edge | ✗ | ✗ | ✗ | ✗ | ✗ | 2D | ~1× |
| Radial-edge | ✓ | partial | ✓ | ✓ | ✓ | ✓ | ~2–3× |
| Vertex-based | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ~3× |
| Masuda complex | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | n/a |
| SGC | ✓ | ✓ | ✓ (cells not balls) | ✓ | ✓ | ✓ | abstract |
| Yamaguchi-Kimura | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ~radial-edge |
| Partial entity | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ~1–1.5× |
| 3-G-map / 3-map | ✓ (quasi) | ✗ (identification) | ✗ natively | ✓ | awkward | awkward | ~2–3× / ~1–1.5× |

Memory figures are rough, normalized to a manifold half-edge model with the same face
count. They are illustrative, not measured.

## References

* B. G. Baumgart, "A polyhedron representation for computer vision", AFIPS NCC 1975.
* D. E. Muller, F. P. Preparata, "Finding the intersection of two convex polyhedra", *TCS* 1978 (DCEL).
* M. Mäntylä, *An Introduction to Solid Modeling*, Computer Science Press, 1988.
* L. Kettner, "Using generic programming for designing a data structure for polyhedral surfaces", *CGTA* 13, 1999.
* L. Guibas, J. Stolfi, "Primitives for the manipulation of general subdivisions and the computation of Voronoi diagrams", *ACM TOG* 4(2), 1985.
* K. Weiler, *Topological Structures for Geometric Modeling*, PhD thesis, RPI, 1986 — https://www.scorec.rpi.edu/reports/view_report.php?id=597
* Y. Choi, *Vertex-based Boundary Representation of Non-manifold Geometric Models*, PhD thesis, CMU, 1989.
* E. L. Gursoz, Y. Choi, F. B. Prinz, "Vertex-based representation of non-manifold boundaries", in *Geometric Modeling for Product Engineering*, 1990, pp. 107–130.
* E. L. Gursoz, Y. Choi, F. B. Prinz, "Boolean set operations on non-manifold boundary representation objects", *CAD* 23(1), 1991.
* H. Masuda, "Topological operators and Boolean operations for complex-based nonmanifold geometric models", *CAD* 25(2), 1993 — https://portal.mardi4nfdi.de/wiki/Publication:1209190 ; https://researcher.ibm.com/publications/topological-operators-and-boolean-operations-for-complex-based-nonmanifold-geometric-models
* J. Rossignac, M. O'Connor, "SGC: A dimension-independent model for pointsets with internal structures and incomplete boundaries", 1989. Overview in the SIGGRAPH 96 course notes: https://cgvr.cs.uni-bremen.de/teaching/cg_literatur/SIG96sgc.pdf
* A. Rappoport, "The Generic Geometric Complex (GGC)" — https://www.cs.huji.ac.il/~arir/ggc.pdf
* Y. Yamaguchi, F. Kimura, "Nonmanifold topology based on coupling entities", *IEEE CG&A* 15(1), 1995, doi:10.1109/38.364963.
* S. H. Lee, K. Lee, "Partial entity structure", ACM SMA 2001 — https://dl.acm.org/doi/10.1145/376957.376976 ; ASME JCISE version: https://asmedigitalcollection.asme.org/computingengineering/article-abstract/1/4/356/471622
* Q-Complex (inclusion topology), *CAD* 2012 — https://cg.cs.tsinghua.edu.cn/papers/lyj-CAD12.pdf
* X-maps — https://www.researchgate.net/publication/221326552_X-maps_An_Efficient_Model_for_Non-manifold_Modeling
* P. Lienhardt, "N-dimensional generalized combinatorial maps and cellular quasi-manifolds", *IJCGA* 4(3), 1994, doi:10.1142/S0218195994000173.
* G. Damiand, P. Lienhardt, *Combinatorial Maps*, CRC 2014 — https://www.oreilly.com/library/view/combinatorial-maps/9781482206524
* J. Dupuy, K. Vanhoey, "A halfedge refinement rule for parallel Catmull-Clark subdivision", *CGF* 40(8), 2021 — https://diglib.eg.org/handle/10.1111/cgf14381
* Overview survey: "A note on solid modeling: history, state of the art, future", 2023 — https://arxiv.org/pdf/2302.14373
