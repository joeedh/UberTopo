# 02 — Survey: topology in industrial kernels and open libraries

Part of [B-rep topology research](README.md). For the academic structures these build
on, see [01-survey-classical.md](01-survey-classical.md).

Each system below gets the same five sections: **model** (what entities exist),
**non-manifold / mixed-dimension**, **holes**, **regions**, and a **verdict** on what
to copy and what to avoid.

> Some details of the closed-source kernels (Parasolid, ACIS) come from public
> functional descriptions, lecture material, and my background knowledge. They are
> marked *(unverified detail)* where I could not confirm them against a reachable
> source.

---

## 1. Parasolid (Siemens)

**Model.** `Body → Region → Shell → Face → Loop → Fin → Edge → Vertex`. A **fin** is
the oriented use of an edge by a loop: the coedge, or half-edge, of Parasolid. A loop's
direction sets the direction of its fins. A manifold edge on a solid has exactly two
fins.

**Body types.** solid, sheet, wire, acorn (a single vertex), and **general**
(non-manifold, mixed-dimension). A body always has an **infinite void region**, and
solid regions are bounded by shells. Faces separate a region on the front from a region
on the back.

**Non-manifold.** General bodies allow edges with more than two fins, faces with the
same region on both sides (an embedded sheet), wire edges and acorn vertices inside
regions, and several solid regions sharing faces. That last case is the building with
rooms and shared walls.

**Holes.** A face may have any number of loops. Faces on periodic surfaces do **not**
need seam edges: a cylinder face can be bounded by two circular loops. A full sphere
face can have no loops at all *(unverified detail)*.

**Tolerant modeling.** Jackson (SMA 1995) introduced per-entity **local tolerances**
in Parasolid: a tolerance is attached to each face, edge, and vertex. Tolerant edges
may lack an exact 3D curve and use fin-level parameter-space curves (SP-curves)
instead. This was how Parasolid made imported, imprecise data workable.

**Convergent modeling.** Recent Parasolid versions let faces carry *facet* (mesh)
geometry in the same B-rep as analytic and NURBS faces *(unverified detail)*. This is
directly relevant to scanned terrain and buildings.

**Verdict.** The closest industrial match to our needs: explicit regions, one general
body type, local tolerances, mixed facet and exact geometry. Legacy aspects: the
proliferation of body *types* (best derived, not declared), and a pointer-based,
single-version, mutable data model.

## 2. ACIS (Spatial / Dassault)

**Model.** `Body → Lump → Shell → Subshell → Face → Loop → Coedge → Edge → Vertex`,
plus `Wire`. Coedges around a non-manifold edge are linked in a **partner ring**:
ACIS's radial cycle. A face can be single-sided or double-sided. A double-sided face
is flagged as inside or outside (a sheet embedded in a solid versus a free sheet).

**Regions.** ACIS has *lumps* (connected components) but no first-class "region
between lumps". For analysis and meshing, ACIS ships a separate **Cellular Topology**
component that layers 3D cells and 2D cells over the B-rep as attributes
*(unverified detail)*. That a mature kernel had to bolt volumes on afterwards is
evidence in itself.

**Attributes and history.** Every entity is a C++ object (`ENTITY`) with an attribute
chain. History and rollback use **bulletin boards** that record entity create, change,
and delete per operation *(unverified detail on mechanics)*. The idea of journaling
every primitive change is sound. A per-object virtual-class implementation is heavy.

**Verdict.** The coedge partner ring is the right non-manifold primitive. Copy the
journaling idea, but not the per-entity object/attribute-chain implementation.

## 3. Open CASCADE (OCCT) — TopoDS / BRep

**Model.** Entity types: `Vertex, Edge, Wire, Face, Shell, Solid, CompSolid, Compound`.
A `TopoDS_Shape` is a lightweight *reference* made of three parts:

```
TopoDS_Shape = { Handle(TopoDS_TShape) tshape;   // shared, holds sub-shape list + geometry
                 TopLoc_Location       location; // rigid transform (instancing)
                 TopAbs_Orientation    orient; } // FORWARD / REVERSED / INTERNAL / EXTERNAL
```

* `IsPartner` means same TShape. `IsSame` means same TShape and same location.
  `IsEqual` additionally requires the same orientation. Hashing uses TShape and
  location only.
* There is **no coedge object**. The "use" of an edge in a wire is a `TopoDS_Edge`
  value with an orientation and location. Pcurves live *on the edge's TShape*
  (`BRep_TEdge`) as a list of curve representations keyed by (surface, location).
  To find an edge's pcurve on a face, you search that list.
* Only **downward** links are stored. Upward adjacency (edge → faces, vertex → edges)
  requires building maps with `TopExp::MapShapesAndAncestors`, at O(n) per query
  batch.
* `INTERNAL` and `EXTERNAL` orientations allow **embedded** edges and vertices in a
  face, and embedded faces in a solid. These are lower-dimensional inclusions in the
  SGC sense, and a useful idea.
* `CompSolid` is a set of solids sharing faces: a weak cell complex.
* **Seam edges:** faces on periodic surfaces (cylinder, sphere, torus) must have a
  seam edge that appears *twice* in the same wire, with two different pcurves (u = 0
  and u = 2π). Poles have *degenerated* edges. Both artifacts come from forcing
  every face to be a single parameter-space rectangle-like domain.
* **Tolerances:** every vertex, edge, and face has a tolerance. Validity requires
  tol(V) ≥ tol(E) ≥ tol(F) for incident entities. The "same parameter" and "same
  range" properties tie the 3D curve and the pcurves together.

**Verdict.** Widely used (FreeCAD, Topologic, CadQuery, many others). Lessons to take:

* **Avoid:** per-sub-shape `Location` instancing. It makes every entity's identity
  depend on a transform chain, complicates hashing, and forces deep copies
  (`BRepBuilderAPI_Copy`) before any edit to a shared sub-shape. Also avoid the absence
  of upward adjacency, which leads to repeated O(n) ancestor-map builds in nearly
  every algorithm, and seam and degenerated edges. Also avoid the lack of stable
  sub-shape identity, which is the root of FreeCAD's topological naming problem
  (see [06-memory-layout-performance.md §7](06-memory-layout-performance.md)).
* **Copy:** the INTERNAL/EXTERNAL embedding concept, per-entity tolerances, and the
  idea that geometry representations can be stacked on an edge (3D curve plus several
  pcurves plus polygon-on-triangulation).

## 4. Blender BMesh

**Model.** Four element types, all pointer-linked and allocated from memory pools
(mempools):

```c
BMVert { co, no, BMEdge *e; }                  // any one incident edge
BMEdge { BMVert *v1, *v2; BMLoop *l;           // any one loop in the radial cycle
         BMDiskLink v1_disk, v2_disk; }        // next/prev in each vertex's disk cycle
BMLoop { BMVert *v; BMEdge *e; BMFace *f;      // a face corner == coedge
         BMLoop *radial_next, *radial_prev;    // radial cycle around e
         BMLoop *next, *prev; }                // loop cycle around f
BMFace { BMLoop *l_first; int len; no; mat_nr; }
```

* All three cycles are stored: disk (in edges), radial (in loops), and loop. The disk
  cycle is independent of faces, so **wire edges and isolated vertices work**. The
  radial cycle allows any number of faces per edge. The order of edges in the disk
  cycle and of faces in the radial cycle is **undefined**: BMesh does not sort faces
  angularly.
* **No regions or shells.** The BMesh design document explicitly says the radial
  cycle is cheap because BMesh "does not keep track of region or shell information".
* **Holes:** not supported. Early BMesh had a compile-time `USE_BMESH_HOLES` option
  (`BMFace.loops` as a list of `BMLoopList`). It was disabled because it cost memory
  and could not be saved in Blender's file format. A Blender n-gon is a single cycle,
  and holes are represented by bridge edges or by splitting.
* **Euler-style kernel operators:** `split_edge_make_vert`, `join_edge_kill_vert`,
  `split_face_make_edge`, `join_face_kill_edge`, `join_vert_kill_edge`, and so on.
* **Storage versus editing split.** Blender stores and evaluates meshes in a
  *separate* struct-of-arrays (SoA) `Mesh` format: positions, `corner_verts`,
  `corner_edges`, face offsets, and generic attribute arrays. It converts to BMesh only
  for interactive editing. The 2022–2023 SoA refactors gave large speedups, for
  example a ~2× faster mesh build in Cycles and a 28% gain from splitting `MLoop` into
  two int arrays. They also enabled copy-on-write sharing of attribute arrays.

**Verdict.** BMesh shows that a **non-manifold surface with three explicit cycles** is
practical at interactive scale. It is "radial-edge minus regions minus uses-per-side".
Its omissions are exactly what we need: holes, regions, ordered radial cycles. Its
pointer-based layout is what Blender itself is moving away from. The lesson from
Blender's history: **if the editable and the evaluable representations differ, the
conversion becomes a bottleneck.** Design one representation that serves both
(see [06](06-memory-layout-performance.md)).

## 5. CGAL

### 5.1 `Surface_mesh` / `Polyhedron_3`
Half-edge, manifold, orientable. `Surface_mesh` is index-based with dynamic property
maps: a clean SoA design, and a good derived format.

### 5.2 `Nef_polyhedron_3` — Selective Nef Complex (SNC)
* An implementation of Nef polyhedra (closed under Boolean operations, open and closed
  sets, mixed dimensions) by Hachenberger, Kettner & Mehlhorn.
* Each vertex stores a **sphere map**: the intersection of a small sphere with the
  vertex neighbourhood. This is a 2D Nef polygon on the sphere, with a selection mark
  on every element. The SNC links the sphere maps together, and **volumes are
  explicit**, each with a mark.
* Unbounded sets are clipped by an **infimaximal box**: a symbolic box at "infinity".
* **Strengths:** mathematically complete for linear geometry. Regular and non-regular
  Booleans, any non-manifold situation, internal structure.
* **Weaknesses:** linear geometry only, exact rational arithmetic, very slow, and very
  memory hungry. Unusable at city scale.
* **Verdict:** a reference implementation to test our semantics against. Its two key
  ideas are to **resolve non-manifold vertices with a local sphere map** and to use a
  **symbolic bounding box for the unbounded region**.

### 5.3 Combinatorial maps, generalized maps, Linear Cell Complex (LCC)
* d-dimensional darts with βi or αi links, and attributes on i-cells. The LCC adds a
  point to every vertex attribute, so geometry is linear only. A generalized map has
  twice as many darts as the equivalent combinatorial map.
* Validity is combinatorial only. The geometry (planarity, self-intersection) is not
  checked.
* **Verdict:** see [01 §11](01-survey-classical.md) and
  [04](04-surface-vs-volumetric.md). Great for volumetric meshing, a poor master model
  for mixed-dimension data with holes.

## 6. OpenVolumeMesh (Kremer, Bommes, Kobbelt, IMR 2012)

* An **index-based** polytopal complex in 3D. Each edge is a pair of half-edges, and
  each face a pair of **half-faces** (oriented). A cell is a set of half-faces.
* Top-down incidences are stored; bottom-up incidences are cached optionally: vertex →
  outgoing half-edges, half-edge → incident half-faces (radially ordered), half-face →
  cell.
* **Supports non-manifold and mixed-dimensional complexes**, because of the index-based
  arrays. It has generic properties (attributes) on every entity type.
* **Verdict:** shows that a cache-friendly array design for a full 3D complex is
  practical, with optional bottom-up caches. It has no curved geometry and no holes in
  faces (faces are single half-edge cycles). It is a good **derived volumetric
  format** for meshing and FDTD-style acoustic grids. It is also a good layout model
  for our own design.

## 7. Topologic (Jabi et al., Cardiff)

* A non-manifold topology (NMT) library for **architectural space modeling**, built on
  OCCT. Entities: `Vertex, Edge, Wire, Face, Shell, Cell, CellComplex, Cluster`, plus
  **Apertures** (doors and windows as sub-faces of shared faces) and *contents*
  (objects attached to topology).
* A `CellComplex` is a set of cells glued by shared faces: **a wall shared by two
  rooms exists once**. Navigation works both ways: face → adjacent cells. It produces
  a **dual graph** of spaces and apertures for spatial reasoning.
* Used to feed energy simulation (EnergyPlus via Honeybee) directly from the space
  model. Papers argue that this "space-first" model is the inverse of
  element-centric building information modeling (BIM).
* **Verdict:** strong evidence from the building domain that **explicit volumetric
  cells with shared faces** are the right abstraction for simulation. Acoustic
  propagation is the same kind of simulation as building energy. Its weakness is that
  it inherits OCCT's limits: no upward links, slow Booleans on big models, and
  location-instanced shapes.

## 8. Vector Graphics Complex (Dalstein, Ronfard, van de Panne, SIGGRAPH 2014)

* A 2D non-manifold cell complex for illustration: points, curves, and surfaces. Its
  **faces are bounded by any number of cycles**. Holes are first-class, and cells are
  *not* required to be disks. Edges can be closed (no vertices).
* Represents arbitrary *immersions* (overlaps allowed), unlike planar maps.
* **Verdict:** a clean modern design for 2D. It suits road-network plan views and
  parcel and zoning maps. Its "a face is a set of cycles, a cycle is a sequence of
  half-edges *or a single closed edge*" is exactly the face model we want.

## 9. Modern open kernels (Rust)

* **Fornjot:** immutable topological objects in centralized, **append-only stores**,
  referenced by `Handle`s. Identity means the storage location, not data equality.
  Updates create new versions and form a directed acyclic graph (DAG) of shared
  objects, which gives persistence and structural sharing. The project has recently
  been moving to separate *owned* objects from *shared* topological objects to
  simplify the graph. That is a cautionary note: universal handles everywhere became
  unwieldy.
* **truck** (RICOS): `Vertex, Edge, Wire, Face, Shell, Solid`, with geometry generic
  over curve and surface types (B-spline and NURBS in `truck-geometry`). It is a
  manifold-solid orientation, with Booleans in `truck-shapeops`. The fork
  *monstertruck* adds fillets, healing, and STEP output.
* **Manifold** (Elalish; now used by OpenSCAD and Blender) is a mesh library rather
  than a B-rep. It shows that *guaranteed-manifold, index-based, parallel* mesh
  Booleans can be orders of magnitude faster than classic B-rep Booleans. Relevant as
  a derived-mesh Boolean engine.
* **Verdict:** none of these handles non-manifold geometry or regions. Fornjot's
  immutable-store experience is the most relevant data point for our persistence
  design.

---

## Cross-system comparison

| System | Coedge / use | Radial order | Disk cycle | Holes | Regions | Wire / acorn | Layout |
|---|---|---|---|---|---|---|---|
| Parasolid | fin | ✓ | derived | ✓, no seams | ✓ explicit | ✓ | pointer |
| ACIS | coedge, partner ring | ✓ | derived | ✓ | lumps (cells via add-on) | ✓ | pointer, attribute chains |
| OCCT | none (oriented edge refs) | ✗ (maps) | ✗ (maps) | ✓, seams required | Solid / CompSolid (weak) | ✓ (INTERNAL) | ref-counted DAG with locations |
| BMesh | BMLoop | unordered | ✓ stored | ✗ (disabled) | ✗ | ✓ | pointer + mempool |
| CGAL Nef | sphere maps | ✓ | ✓ (sphere map) | ✓ | ✓ | ✓ | pointer, exact |
| CGAL LCC | darts | via α/β | via α/β | ✗ | ✓ (3-cells) | awkward | handle/index |
| OpenVolumeMesh | half-edge, half-face | ✓ (cached) | ✓ (cached) | ✗ | ✓ (cells) | ✓ | index SoA |
| Topologic | (OCCT) | (maps) | (maps) | ✓ | ✓ CellComplex | ✓ | OCCT |
| VGC | half-edge, cycles | n/a (2D) | ✓ | ✓ | n/a (2D) | ✓ | pointer |
| Fornjot | half-edge | n/a | n/a | ✓ | ✗ | partial | immutable store |

## References

* Parasolid entities, lecture notes (SNU): https://ocw.snu.ac.kr/sites/default/files/NOTE/4844.pdf
* D. J. Jackson, "Boundary representation modelling with local tolerances", ACM SMA 1995 — https://ftp1.cs.wisc.edu/pub/users/prem/jackson-SM-95.pdf ; dblp: https://dblp.uni-trier.de/rec/conf/sma/Jackson95.html
* M. Segal, "Using tolerances to guarantee valid polyhedral modeling results", SIGGRAPH 1990 — https://ftp1.cs.wisc.edu/pub/users/prem/segal-repair-siggraph-1990.pdf
* OCCT `TopoDS_Shape` reference: https://dev.opencascade.org/doc/refman/html/classTopoDS__Shape.html ; forum on TShape vs Shape: https://dev.opencascade.org/content/topodstshape-and-topodsshape-0 ; instancing: https://dev.opencascade.org/content/multiple-instances-shapes
* BMesh design doc: https://wiki.blender.org/features/objects/mesh/bmesh/ (old design page mirrors: https://wiki.blender.jp/Dev:Source/Modeling/BMesh/Design , https://julianeisel.github.io/wiki.blender.org-dump/wiki/Source/Modeling/BMesh/Design.html)
* BMesh `bmesh_class.h` (shows `USE_BMESH_HOLES`): https://projects.blender.org/archive/blender-archive/src/commit/5cc793912efe4432b51cc7ddda4005c56fcbc93b/source/blender/bmesh/bmesh_class.h
* Blender Mesh SoA refactor: https://developer.blender.org/T95965 ; corner arrays PR: https://projects.blender.org/blender/blender/pulls/104424 ; vertex positions: https://projects.blender.org/blender/blender/issues/93602
* CGAL Nef_3: https://doc.cgal.org/latest/Nef_3 ; Hachenberger, Kettner, Mehlhorn, "Boolean operations on 3D selective Nef complexes", *CGTA* 2007 — https://www.aminer.org/pub/53909ee020f70186a0e3205d
* CGAL Linear Cell Complex: https://doc.cgal.org/latest/Linear_cell_complex
* M. Kremer, D. Bommes, L. Kobbelt, "OpenVolumeMesh — a versatile index-based data structure for 3D polytopal complexes", IMR 2012 — https://www.graphics.rwth-aachen.de/software/openvolumemesh/ ; https://vci.rwth-aachen.de/publication/03189
* Topologic: W. Jabi et al., AAG 2018 — https://orca.cardiff.ac.uk/113472/1/AAG2018_Topologic_ORCA.pdf ; eCAADe 2019 — https://orca.cardiff.ac.uk/124160/1/eCAADe%202019_SpatialReasoningFramework_asSubmitted_ORCA.pdf ; AEC Magazine — https://aecmag.com/simulation/topologic-modelling-space-the-inverse-of-bim/ ; topologicpy — https://pypi.org/project/topologicpy
* B. Dalstein, R. Ronfard, M. van de Panne, "Vector graphics complexes", *ACM TOG* 33(4), 2014 — https://www.borisdalstein.com/research/vgc/ ; PCS complex TR: https://www.borisdalstein.com/research/vgc/dalstein2014pcs.pdf
* Fornjot `fj-core` objects/storage: https://docs.rs/crate/fj-core/latest/source/src/objects/mod.rs ; https://docs.rs/crate/fj-core/0.49.0/source/src/storage/store.rs
* truck: https://github.com/ricosjp/truck ; monstertruck: https://docs.rs/monstertruck
* Manifold: https://github.com/elalish/manifold
