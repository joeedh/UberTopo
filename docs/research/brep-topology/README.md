# B-rep topology data structures — research

This folder researches the topological core of UberTopo's CAD kernel. It covers which
boundary-representation (B-rep) topology structure to use, whether a surface
(2-manifold) model suffices or explicit volumes are needed, how to handle faces with
holes and mixed-dimensional elements, and how to lay the structure out for
performance, parallelism, persistence, and stable naming. The basis is
`docs/requirements.md`.

## Files

| File | Contents |
|---|---|
| [01-survey-classical.md](01-survey-classical.md) | Literature survey: winged-edge, half-edge/GWB, quad-edge, radial-edge (Weiler), vertex-based (Gursoz/Choi/Prinz), Masuda, SGC (Rossignac & O'Connor), Yamaguchi–Kimura, partial-entity (Lee & Lee), generalized/combinatorial maps |
| [02-survey-systems.md](02-survey-systems.md) | Industrial and open systems: Parasolid, ACIS, Open CASCADE TopoDS, Blender BMesh, CGAL (Surface_mesh, Nef_3, LCC), OpenVolumeMesh, Topologic, VGC, Fornjot, truck, Manifold |
| [03-faces-with-holes.md](03-faces-with-holes.md) | Inner loops vs. bridge edges vs. decomposition; periodic faces, pinched loops, inclusions, apertures; parameter-space loop classification |
| [04-surface-vs-volumetric.md](04-surface-vs-volumetric.md) | **The core question.** 2-manifold vs. regions/3-complex, analysed against buildings, terrain, water, forests, atmosphere, acoustic methods, vulnerability surveys, and infrastructure. Mixed-dimensional elements. The recommended entity model |
| [05-invariants-operators-validity.md](05-invariants-operators-validity.md) | Generalized Euler–Poincaré (derived via χ_c), local invariants, Euler operators and their modern role, three-layer validity checking, robust editing and arrangement Booleans |
| [06-memory-layout-performance.md](06-memory-layout-performance.md) | Index vs. pointer, SoA layout and sizes, frozen/editable modes, chunked copy-on-write persistence, parallel/GPU build, tiling, topological naming and persistent provenance IDs, compaction |
| [07-geometry-embedding.md](07-geometry-embedding.md) | Curves and surfaces on topology, pcurves vs. 3D curves, roads as "pcurve-primary", clothoids/spiros → certified B-splines, mesh-backed faces, precision and coordinates, tolerant modeling |
| [08-critique-and-recommendation.md](08-critique-and-recommendation.md) | Legacy cruft vs. essential ideas, decision summary (R1–R8), API sketch, phased adoption, open questions |

## Bottom line

1. **A 2-manifold is not enough. A volumetric tessellation is not needed as the master
   model either.** Use a **non-manifold, mixed-dimensional cell complex in 3D with
   explicit regions (volumes)**. Each shared wall is one face with a region on each
   side. Rooms, water bodies, soil layers, and air volumes are regions with media.
   Tets, voxels, and FDTD grids are *derived*.
   ([04](04-surface-vs-volumetric.md))
2. **Entities:** Vertex, Edge, Coedge, Loop, Face, Shell, Region, plus *inclusions*
   (sensors on walls, centerlines on terrain, fences in air, instanced trees in a
   region). "Body/building/room" are semantic groupings, not topology types.
3. **Faces are bounded by any number of cycles.** There is no outer-loop flag, no
   bridge edges, and no seam edges. Holes are first-class, so they can be named and
   filled with aperture faces (windows and doors).
   ([03](03-faces-with-holes.md))
4. **Use partial-entity-style economy, not radial-edge duplication.** One coedge per
   face-edge incidence, a side bit instead of face-uses, and an angularly ordered
   radial cycle. The disk cycle is derived.
5. **Reject generalized maps (3-G-maps/LCC) as the master model.** They are
   quasi-manifold only, have no holes in faces, handle mixed dimension awkwardly, and
   cost 2–3× memory. They remain useful as derived volumetric formats.
6. **Use index-based SoA arenas** with `u32` links, generational external handles,
   and separate *persistent provenance IDs*. Frozen and editable are *modes of one
   layout*, not two formats. That avoids Blender's BMesh↔Mesh conversion lesson.
   ([06](06-memory-layout-performance.md))
7. **Chunked copy-on-write versions plus journaled primitive ops** give procedural
   caching, undo, concurrent readers, lineage, and incremental validation.
8. **Solve topological naming by construction.** Procedural generators know the
   semantics, so they assign IDs at creation, and every primitive derives child IDs
   deterministically.
9. **Geometry:** pcurves on coedges, 3D curves on edges, either one primary. Layered
   procedural → certified B-spline → tessellation. Road alignments are
   pcurve-primary on terrain. Faces can be mesh-backed for scan data. Use f64 tile
   frames and local tolerances. ([07](07-geometry-embedding.md))
10. **Construction is bulk + arrangement-and-select** (Masuda/SGC/mesh-arrangements
    lineage). Euler operators survive as the internal journal vocabulary, not as
    the API. ([05](05-invariants-operators-validity.md))

## Research method and caveats

* Sources were located with web search (Nov 2026). Most full texts could not be
  fetched from this environment (outbound fetches to most hosts were blocked), and the
  web-search budget ran out before every claim could be double-checked. Details that
  rely on background knowledge rather than a reachable source are marked
  *(unverified detail)* in the files. Most of these concern closed-source kernels
  (Parasolid, ACIS) and exact formula notation (Masuda).
* Memory and performance figures are back-of-envelope estimates, labelled as such.
  They should be replaced by measurements once a prototype exists.
* Related sibling research folders (curves, attributes and queries, CAD kernels,
  simulation requirements, procedural systems, environment generation) may overlap.
  This folder deliberately limits itself to topology and its interfaces.
