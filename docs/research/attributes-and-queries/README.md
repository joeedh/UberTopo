# Attributes, Geometry Queries and Large-World Data Management

This folder holds research for UberTopo (see `docs/requirements.md`) on:

* **attribute systems**: arbitrary per-element data, including embedded curves and
  surfaces;
* **geometry queries**: spatial acceleration structures, query types and a unified
  query API;
* **large-world data management**: coordinate frames, precision, tiling, streaming
  and LOD.

Other research topics (B-rep topology data structures, kernel robustness, curve
mathematics, procedural engine architecture, environment generation, simulation
requirements) are covered elsewhere in `docs/research/`.

## Files

| File | Contents |
|---|---|
| [attribute-systems-survey.md](attribute-systems-survey.md) | Houdini, Blender (domains, implicit interpolation, ImplicitSharing, BMesh CustomData), OpenMesh, CGAL property maps, USD primvars, OpenVDB, OCAF/ACIS, CityGML/CityJSON, 3D Tiles metadata, Arrow. Storage design space (SoA, typing, sparse, COW, dictionary encoding), units/frames/roles, schema versioning, semantic attributes for acoustic/optical/IR/survey use. |
| [attribute-propagation.md](attribute-propagation.md) | Per-attribute policies for split/merge/Boolean/subdivision/simplification/regeneration; extensive vs intensive quantities; face-side swapping; persistent naming (FreeCAD's topological naming problem, history-based vs semantic keys); provenance; parametric fields on B-rep faces; COW interaction; test invariants. |
| [embedded-geometry-attributes.md](embedded-geometry-attributes.md) | Curve-on-edge, pcurve-on-coedge, surface-on-face as reserved attributes; heterogeneous geometry in tag+index per-type arenas; exact + certified approximant curves; embedded curve sets with lazy imprinting; OpenDRIVE-style (s, t) road coordinates and sweep surfaces; raster/procedural carriers; GeneratorRefs; typed reference kinds. |
| [spatial-acceleration-structures.md](spatial-acceleration-structures.md) | BVH (SAH, SBVH, LBVH, PLOC/H-PLOC, wide/compressed, TLAS/BLAS, refit vs rebuild, watertightness, fp64), hardware RT (Vulkan/DXR/Metal/OptiX, CLAS/PTLAS, DGF), k-d trees, (loose/sparse voxel) octrees, R-trees (R*, Hilbert, STR), grids/hashing, terrain max-mipmaps, VDB/HDDA, non-triangle primitives. |
| [query-types-and-api.md](query-types-and-api.md) | Ray variants (first/any/all/integrate) for acoustics, LOS, closest point, point containment (generalized and fast winding numbers vs topological point location), visibility/viewshed/air-volume coverage, range/k-NN, diffraction edge queries, topological queries, geodesics, sampling; unified batch-first, snapshot-isolated, deterministic query layer; differentiable queries. |
| [large-world-coordinates-and-tiling.md](large-world-coordinates-and-tiling.md) | WGS 84/ECEF/ENU/UTM, vertical datums, float32 vs float64 precision table, RTC/RTE/floating origin/UE5 LWC, recommended frame hierarchy, tiling schemes (quadtree, S2, H3, TMS, DGGS), 3D Tiles 1.1, I3S, CDB, query-driven streaming, tile seams and global feature IDs. |
| [lod-and-simplification.md](lod-and-simplification.md) | Representational vs geometric LOD, cross-LOD identity and attribute aggregation, HLOD, simplification algorithms (QEM, Hoppe, meshoptimizer, VSA, structure-aware), acoustic/optical/vegetation proxies, simulation LOD policy, Nanite/meshlets assessment, LOD validation. |
| [recommendations.md](recommendations.md) | Concrete proposal: domains (incl. FaceSide and Region), paged COW column store, schema registry, identity scheme, propagation engine, geometry arenas, layered query architecture, large-world and LOD decisions, phasing, risks, open questions. |

## Key findings

1. **DCC tools have the best attribute storage; CAD kernels have the best attribute
   semantics.** Houdini (paged SoA, constant pages) and Blender (SoA generic attributes,
   ImplicitSharing COW) show how to store billions of values. ACIS/Parasolid attribute
   classes and BMesh `interp` callbacks show how values should behave through topology
   edits. UberTopo should combine them, with behaviour declared per *attribute schema*,
   not per value type.
2. **A B-rep needs more domains than DCC tools have.** In particular it needs a
   **FaceSide** domain (per-side materials on shared interior walls, which matters for
   acoustics) and a **Region** domain (media: air temperature, water, vegetation
   density).
3. **Persistent identity must be built in from the start.** FreeCAD's topological
   naming problem shows the cost of retrofitting it. Generators should emit semantic
   keys, and operators should derive IDs from history (128-bit).
4. **Geometry binding is itself an attribute system.** Making curves, pcurves and
   surfaces reserved columns over immutable per-type arenas unifies meshes and exact
   B-reps and replaces the 90s polymorphic `Handle(Geom_Curve)` design.
5. **Roads should be embedded as curvilinear (s, t) systems** with exact
   transcendental reference lines plus certified B-spline approximants. Markings and
   breaklines go in embedded curve sets on faces and are imprinted lazily.
6. **Queries need a unified, batch-first, snapshot-isolated front-end.** It sits over
   several back-ends: two-level BVH (Embree / Vulkan ray query), heightfield
   max-mipmaps, NanoVDB, R-trees, grids. Each query has an explicit LOD policy and
   precision tier.
7. **Point location should be topological**: ray → face-side → region. Fast winding
   numbers are the fallback and validator for unclean data.
8. **Diffraction needs edge-level queries.** The B-rep's explicit edges are an
   advantage here. Tile-seam edges must be flagged so they are not treated as
   diffracting wedges.
9. **Precision:** float64 in site ENU frames, and float32 only in tile frames that are
   integer translations (≤ 8 km for ~mm). Never compute in UTM. Acoustic TDOA needs
   ~1 cm consistency.
10. **Simulation LOD must be deterministic and physically driven** (by wavelength and
    sensor resolution), not camera-driven. Acoustic proxies must conserve per-material
    area and region volume and fold removed detail into scattering coefficients.
11. **Nanite-style cluster DAGs** are a good *derived* representation for visualisation
    and bulk stochastic rays at a world-space error cut, especially with cluster
    acceleration structures (RTX Mega Geometry/DXR preview, AMD DGF). They are not a
    source of truth and are wrong for image-source acoustics.

## Research method and caveats

Web searches (2026-11-17) were used to verify key current facts: Blender
ImplicitSharing and Bundles, USD primvar interpolation, H-PLOC, fast/discrete-surface
winding numbers, 3D Tiles 1.1 metadata, RTX Mega Geometry/PTLAS and the DXR preview,
AMD DGF, Nanite fallback-mesh behaviour, diffraction pathfinding (Schissler et al.
2021), the PTB absorption database, Cesium RTE, OpenMesh/CGAL properties, Houdini
attribute precedence, meshoptimizer attribute simplification, FCPW, viewshed
algorithm comparisons, and Hoppe's quadric metric. Other citations (classic papers,
some DOIs, ACIS documentation URL, Pelzer & Vorländer 2010) are from background
knowledge and are marked where uncertain. Verify them before quoting in formal
documents.
