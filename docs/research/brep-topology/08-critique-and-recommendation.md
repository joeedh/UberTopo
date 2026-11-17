# 08 — Critique of existing designs, and the recommended UberTopo topology

Part of [B-rep topology research](README.md). This file pulls together
[01](01-survey-classical.md)–[07](07-geometry-embedding.md). The requirements tell us
to be skeptical of 1990s foundations, so first we sort the inherited ideas into
**legacy cruft** and **essential insight**.

## 1. Legacy cruft (do not copy)

| Inherited idea | Where | Why it is cruft | Replace with |
|---|---|---|---|
| Unoriented edge records needing "which side am I on?" tests | Winged-edge | Branchy, bug-prone, slow | Oriented coedges |
| Duplicate *uses on both sides* of every face (face-use, loop-use, 2 edge-uses per face-edge) | Radial-edge | ~2× memory for information a side **bit** encodes (Lee & Lee) | One coedge per face-edge incidence + side bit; `region[2]` on the face |
| Seam edges and degenerated pole edges on periodic surfaces | OCCT | Artifacts of forcing face domains to be rectangles. They are bridge edges in disguise | Periodic-aware parameter domains; loopless and multi-loop faces |
| A stored "outer loop" distinction | Many kernels | Undefined on periodic or closed surfaces | Unordered set of cycles; "outer" derived when meaningful |
| Per-sub-shape locations (transforms on every reference) | OCCT `TopoDS_Shape` | Identity depends on a transform chain. Edits to shared sub-shapes are dangerous, hashing is complex | Instancing only at object level; tile frames for coordinates |
| Downward-only links, with upward adjacency rebuilt as maps per algorithm | OCCT | Repeated O(n) map builds; quadratic surprises | Stored radial and face→region links; CSR disk adjacency |
| Declared body *types* (solid / sheet / wire / general) as storage classes | Parasolid, OCCT shape types | Classification belongs to geometry and topology, not storage. Conversions between types are needless operations | One general complex; derived classification |
| Per-entity heap objects with virtual methods and attribute chains | ACIS `ENTITY`, OCCT `TShape` | Cache-hostile; serialization needs swizzling; no GPU path | SoA arenas, columnar attributes |
| Pointer-based edit structure separate from the storage/eval structure | Blender BMesh vs. Mesh | Conversion becomes the bottleneck | One columnar layout with frozen/editable *modes* |
| Global modeling epsilon | Older kernels, many mesh libraries | Wrong at city scale with mixed-precision inputs | Local journaled tolerances |
| Mutable single-version model; undo by inverse ops only | Most kernels | Blocks procedural caching and safe parallel reads | Chunked copy-on-write versions + journal |
| Euler operators as the *public* modeling API | Textbook kernels | Too fine-grained for procedural or bulk construction | Bulk builders + arrangement; Euler ops as the internal journal vocabulary |
| Identity = storage position (index or pointer) | OCCT, many mesh libraries | Topological naming problem | Separate persistent IDs derived from provenance |
| Manifold-only algorithms as the default | Half-edge libraries | Our data is non-manifold by nature (rooms, apertures, terrain + roads) | Non-manifold core; manifold *views* when an algorithm needs them |

## 2. Essential insights (keep)

| Insight | Origin | Why it is essential here |
|---|---|---|
| Separation of topology and geometry | All B-rep kernels | Topology stays valid while geometry is LOD-dependent, procedural, or approximated |
| Oriented edge-in-face uses (coedge / fin / half-edge) | Mäntylä, Parasolid, ACIS | Natural owner of pcurves and orientation; branch-free traversal |
| Ordered radial cycle around edges | Weiler | Region assignment, diffraction-edge wedges, aperture modelling |
| Faces bounded by multiple cycles | GWB, Parasolid, VGC | Windows, doors, courtyards, islands |
| Explicit regions and shells; an unbounded region | Weiler, Parasolid, Nef | Media for acoustics, optics, and water; containment queries; rooms |
| Cells need not be balls | SGC, VGC, Masuda | Rooms with columns, regions with cavities, annular faces |
| Lower-dimensional inclusions inside higher cells | SGC, OCCT INTERNAL, Q-Complex | Sensors on walls, centerlines on terrain, fences in air |
| Merge-then-select Booleans | Masuda, SGC, mesh arrangements | Robust; re-selectable; suits procedural pipelines |
| Local vertex neighbourhoods on demand (sphere maps) | Gursoz–Choi, CGAL Nef | Resolve non-manifold vertices without storing zones everywhere |
| Local tolerances | Jackson / Parasolid | Mixed GIS + CAD + scan inputs |
| Small closed set of invertible primitive operators | Euler operators, BMesh kernel ops, `Splice` | Journal, undo, lineage, incremental validation |
| Index-based, half-face style 3D complexes are practical | OpenVolumeMesh | Proves the array layout for a full 3-complex |
| Space-first modeling of buildings | Topologic, CityGML 3.0, IFC space boundaries | Simulation-ready building models |

## 3. Recommendation (decision summary)

### R1 — Topological model
A **non-manifold, mixed-dimensional cell complex in 3D with explicit regions**. It
belongs to the radial-edge, Parasolid general body, and SGC lineage, with
partial-entity-style economy:

* Entities: **Vertex, Edge, Coedge, Loop, Face, Shell, Region**. Inclusions (vertices,
  edges, faces, or included bodies inside the open interior of a higher cell) are
  first-class. "Body", "building", and "room" are **semantic groupings** in the
  attribute layer, not topological entity types.
* Faces are bounded by **zero or more cycles** (holes), with no outer flag and no seams.
* Each face has **two sides**, each naming a region. Shared walls exist once.
* **Radial cycles are angularly ordered.** The disk cycle is derived (CSR) in frozen
  mode.
* Region cells may have genus and cavities. There is always one unbounded region and
  an optional symbolic world box.

**Answer to the requirements question:** a 2-manifold is *not* enough. We need a
**3D cell complex (explicit volumes)**. We do **not** need a volumetric
*tessellation* (3-manifold mesh, tets) as the master model. Those are derived per
simulation ([04](04-surface-vs-volumetric.md)).

### R2 — Not generalized maps
3-G-maps, 3-maps, and LCC are rejected as the master model. They are quasi-manifold
only, have no holes in faces, handle mixed dimension awkwardly, and need 2–3× memory.
They remain candidates as *derived* formats for volumetric meshing, alongside
OpenVolumeMesh-style polyhedral meshes and voxel grids.

### R3 — Storage
Index-based **SoA arenas**: `u32` internal links, **generational handles** for external
references, and **persistent provenance IDs** for identity. Use **frozen** (compact,
Morton-ordered, CSR, immutable, GPU-uploadable) and **editable** (free-lists,
small-vector adjacency) *modes of the same columns*.
([06](06-memory-layout-performance.md))

### R4 — Versions
**Chunked copy-on-write columns**, giving immutable versions for procedural caching,
undo, concurrent readers, and diffing. Primitive ops are **journaled**: undo, lineage,
incremental validation.

### R5 — Scale
**Spatial tiles** as independent complexes with replicated virtual boundary cells, plus
**included bodies** (instanced prototypes such as trees, vehicles, and furniture)
referenced inside regions without merging.

### R6 — Geometry
Pcurves on coedges, 3D curves on edges, either primary. Layered **procedural →
certified B-spline → tessellation** geometry. Native parameterizations (station and
offset). **Mesh-backed faces.** f64 tile frames. Local journaled tolerances.
([07](07-geometry-embedding.md))

### R7 — Operators
Bulk parallel builders plus **arrangement-and-select** Booleans as the main
construction path. About 12 journaled invertible primitives underneath. Exact or
certified predicates for every combinatorial decision.
([05](05-invariants-operators-validity.md))

### R8 — Validation
Three-layer validity (combinatorial / embedding / global geometric), a generalized
Euler–Poincaré check in debug builds, and CGAL Nef_3 as a differential oracle on small
linear cases.

## 4. Sketch of the core API surface (illustrative, language-neutral)

```text
// Identity
CellRef     = { kind: V|E|C|L|F|S|R, index: u32, gen: u32 }   // external handle
PersistId   = u128                                              // provenance identity

// Navigation (all O(1) or O(local degree))
coedges_of(loop) -> iter        loops_of(face) -> iter         radial(coedge) -> iter
faces_around(edge) -> iter      edges_at(vertex) -> iter       region(face, side) -> Region
shells_of(region) -> iter       face_sides_of(shell) -> iter   inclusions(face|region) -> iter
container(vertex|edge|face) -> CellRef?

// Classification (geometric, cached)
locate(point) -> (cell, dimension)       // which region / on which face / edge / vertex
is_manifold(vertex|edge) -> bool
classify(set of cells) -> {solid, sheet, wire, acorn, mixed}

// Construction
builder.emit_face(cycles[], surface, pcurves?, region_hint?) ...
builder.emit_wire(curve), builder.emit_vertex(point)
builder.build(tolerance_policy) -> FrozenComplex          // parallel weld/sort/radial/regions

// Editing (on an editable tile; journaled; transactional)
tx = complex.edit(tiles)
tx.imprint(face, curve) / tx.glue(face_a, face_b) / tx.inflate(face, thickness) / ...
tx.commit() -> new FrozenComplex version | error(validation report)

// Booleans
arr = arrangement([complex_a, complex_b, ...])           // all cells, winding vectors
arr.select(predicate over membership/winding) -> complex

// Export (derived products)
tessellate(complex, lod) -> tri mesh with (face_id, side->region) tags
to_volume_mesh(complex, regions) -> polyhedral / tet mesh (OVM/LCC-like)
voxelize(complex, grid) -> region labels
manifold_view(shell) -> half-edge mesh
dual_graph(complex) -> regions × aperture faces
```

## 5. Phased adoption (suggested)

1. **P0 — Core tables and bulk build** for planar polygonal data (buildings from
   footprints, terrain TIN). Regions, holes, radial order, frozen mode, invariants,
   and the Euler check. Export tessellation and the dual graph.
2. **P1 — Editing + journal + versions**: primitives, transactions, chunked CoW, and
   persistent IDs from generators.
3. **P2 — Curved geometry**: pcurves, B-spline curves and surfaces, procedural
   alignment curves, roads imprinted on terrain via a 2D arrangement in parameter
   space.
4. **P3 — Arrangement Booleans** (planar first with exact predicates, then curved),
   healing and import (CityGML, IFC, OSM).
5. **P4 — Tiles + inclusions at city scale**, mesh-backed faces for scan data, and
   simulation exports (voxel, BEM surface, volume mesh).

Each phase should go through the project's plan → fresh-context pressure-test process
(see `CLAUDE.md`).

## 6. Open questions for the project owner

1. **Simulation fidelity targets.** Will wave-based acoustics (FDTD/BEM) or
   ground-coupled and seismic sensing be used? If acoustics is strictly geometric
   (rays + diffraction), regions are still valuable, but soil layering can be
   deferred.
2. **Indoor scenarios.** Are interior spaces (rooms, atria, stairwells, parking
   structures) in scope at high LOD? This decides how much effort goes into building
   cell complexes vs. outdoor envelopes only.
3. **Input data mix.** What share is procedurally generated vs. imported (OSM,
   CityGML, IFC, lidar/photogrammetry meshes)? A large scan share raises the priority
   of mesh-backed faces and healing.
4. **Scale and precision.** What is the largest contiguous site (km), and the finest
   detail needed (cm? mm?)? This sets tile size, tolerance defaults, and whether f64
   suffices everywhere.
5. **Interactive editing.** Is there a human-in-the-loop modeling UI, or is the system
   primarily procedural/batch? This changes how much to invest in the editable mode
   and in single-element edit latency.
6. **Determinism and reproducibility.** Must the same seed and graph give bit-identical
   topology and IDs across machines and thread counts (important for test
   reproducibility in defense evaluation)? The recommendation assumes **yes**.
7. **Implementation language.** C++ vs. Rust affects the arena/handle idioms
   (slotmap crates vs. custom C++ arenas) and the persistence libraries available.
8. **Time-varying scenes.** Are moving objects (vehicles, doors opening, water level
   changes) needed? If so, they argue for keeping such objects as *included bodies*
   with transforms rather than merged topology.

## References

See the per-file reference lists in [01](01-survey-classical.md)–[07](07-geometry-embedding.md).
Key sources for this synthesis:

* K. Weiler, radial-edge structure, RPI 1986 — https://www.scorec.rpi.edu/reports/view_report.php?id=597
* S. H. Lee, K. Lee, Partial entity structure, SMA 2001 — https://dl.acm.org/doi/10.1145/376957.376976
* J. Rossignac, M. O'Connor, SGC — https://cgvr.cs.uni-bremen.de/teaching/cg_literatur/SIG96sgc.pdf
* H. Masuda, CAD 25(2), 1993 — https://portal.mardi4nfdi.de/wiki/Publication:1209190
* D. J. Jackson, local tolerances, SMA 1995 — https://ftp1.cs.wisc.edu/pub/users/prem/jackson-SM-95.pdf
* CGAL Nef_3 — https://doc.cgal.org/latest/Nef_3
* OpenVolumeMesh — https://www.graphics.rwth-aachen.de/software/openvolumemesh/
* Topologic — https://orca.cardiff.ac.uk/113472/1/AAG2018_Topologic_ORCA.pdf
* OCCT TopoDS_Shape — https://dev.opencascade.org/doc/refman/html/classTopoDS__Shape.html
* BMesh design — https://wiki.blender.org/features/objects/mesh/bmesh/ ; Blender Mesh SoA — https://developer.blender.org/T95965
* Q. Zhou et al., Mesh arrangements — https://www.cs.columbia.edu/cg/mesh-arrangements
* FreeCAD topological naming — https://wiki.freecad.org/Topological_naming_problem
