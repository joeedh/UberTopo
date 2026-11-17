# CAD kernels, geometric representations and numerical robustness

This research track covers geometry kernels as whole systems, the choice of
representation for multi-LOD environment modelling, numerical robustness, the
modelling operations UberTopo needs, and tessellation for simulation
consumers. Two other tracks cover related ground: B-rep topology data
structures, and transcendental curves. This track only refers to them where
needed.

The research was done in November 2026 using web search plus prior knowledge.
Claims taken from prior knowledge without a current source are marked
*(prior knowledge)* or *(unverified)*. One site (develop3d.com) was blocked by
the session's network policy.

## Files

| File | Contents |
|---|---|
| [kernel-survey.md](kernel-survey.md) | Parasolid, ACIS, CGM, Granite, C3D, SMLib/USD BRep, OCCT, Rhino/openNURBS, Onshape/Plasticity/Zoo, Fornjot, Truck, SolveSpace, OpenSCAD/CadQuery/build123d, CGAL, libigl, Manifold, Blender, research Boolean codes, implicit/SDF systems, OpenVDB. What is 90s legacy and where each fails. |
| [representations.md](representations.md) | Exact NURBS B-rep, polyhedral B-rep, implicit/SDF/F-rep, sparse voxels, heightfields/TINs, subdivision, point clouds; a hybrid architecture and a suitability matrix per content type and consumer. |
| [robustness-and-exact-arithmetic.md](robustness-and-exact-arithmetic.md) | ε-tolerances vs EGC; Shewchuk predicates; CGAL kernels; mesh arrangements; indirect predicates; plane-based integer geometry (with a bit-width budget); Manifold's topology-first approach; symbolic perturbation; snap rounding; intervals; fixed-point; large-coordinate and geospatial frame strategy. |
| [booleans-and-operations.md](booleans-and-operations.md) | 3D and 2D Booleans, arrangements, offsets, fillets/bevels, sweeps along transcendental curves (roads, walls, rails, power lines), SSI, trimming and faces with holes, Minkowski sums, operation contracts. |
| [tessellation-lod.md](tessellation-lod.md) | Consumer requirements (acoustic, camera, LOS, collision, RF); crack-free edge-first tessellation; semantic vs geometric LOD; LOD consistency guarantees; simplification; consumer profiles. |
| [recommendations.md](recommendations.md) | Proposed representation stack and robustness strategy; borrow-vs-build table with licences; C++ vs Rust; suggested phasing; risks; open questions. |

## Summary of findings

1. **Industrial kernels share a late-1980s architecture.** Parasolid, ACIS,
   CGM, Granite, C3D, OCCT and SMLib are all exact NURBS B-reps with
   float + ε-tolerance robustness, tolerant edges, sewing and healing.
   Parasolid is designed for a **1000-unit box at 1e-8 precision**. Tolerances
   are non-transitive and grow over modelling histories. OCCT exposes the
   problem as user-tuned "fuzzy" and "glue" options. None of this suits
   procedurally generated, km-scale scenes with 10⁷ faces.
2. **Modern open-source B-rep attempts have struggled with scope.** Fornjot
   (Rust) has shut down. Truck and SolveSpace keep tolerance-based designs.
   Zoo built its own GPU B-rep engine but keeps it proprietary. NVIDIA
   open-sourced SMLib (as USD BRep, Apache-2.0) in October 2026. That is
   notable as a NURBS adapter, but it is still a 90s-style design.
3. **Exact polyhedral Booleans are now fast.** Plane-based integer geometry
   (Campen & Kobbelt 2010 → Nehring-Wirxel 2021 → EMBER 2022), indirect
   predicates (Attene 2020 → Cherchi et al. 2022) and exact constructions
   (Lévy 2024) all reach interactive or better speeds with guaranteed
   robustness. EMBER handles million-triangle meshes interactively and beats
   inexact methods.
4. **Manifold** (Apache-2.0) is the best drop-in float Boolean: always-manifold
   output, symbolic perturbation, provenance ids. It has been adopted by
   OpenSCAD and Blender 4.5. It requires manifold inputs and is not exact.
5. **Environment geometry is mostly planar, 2.5D, swept or statistical.** It
   is not free-form NURBS. Curved content (roads, terrain, tanks) is better
   kept as procedural/analytic carriers and tessellated per LOD than
   intersected as NURBS.
6. **Large coordinates.** Float is unusable beyond a few km: about 1 mm ulp at
   10 km and 0.5 m at Earth-centred coordinates. The answer is geodetic
   anchor, then a site frame (double), then **tile-local integer lattices**
   for exact work, then camera-relative float on the GPU. Plane-based
   arithmetic with about 26-bit inputs fits in 256-bit fixed-width integers,
   which is about 6.7 km at 0.1 mm or 67 km at 1 mm per tile.
7. **Acoustic consumers need watertight, coarse, coplanar-merged polygons with
   explicit diffracting edges.** That argues for deriving acoustic LOD from a
   polyhedral B-rep, not from render meshes. LOD levels must declare
   consistency guarantees (conservative, volume-preserving, best-fit).

## Headline recommendation

Build a **procedural evaluator** whose integration layer is an **exact,
plane-based, polyhedral, non-manifold B-rep** (faces with holes, 3-cells,
attributes, provenance) in **tile-local integer frames**. Peer it with:

- **exact 2D arrangements** for roads, parcels and footprints,
- **heightfield/TIN** terrain with exact CDT breaklines,
- **OpenVDB** sparse volumes for vegetation and media,
- analytic curve and surface carriers as attributes.

Borrow predicates, Clipper2, Manifold, OpenVDB and meshoptimizer. Build the
plane-based Boolean/arrangement core and the 2D arrangement. Keep NURBS
(OCCT or SMLib) as an import/export adapter only. See
[recommendations.md](recommendations.md).

## Open questions (details in recommendations.md §8)

- Distribution and licensing posture (GPL/LGPL acceptability), and export
  control.
- C++ vs Rust.
- Largest site extent and smallest relevant feature, which set the lattice
  resolution and tile size.
- Which acoustic simulation methods will consume the geometry.
- How much CAD/BIM and scan data must be ingested.
- Whether cross-platform determinism is a hard requirement.
- Whether RF/radar is in future scope.
