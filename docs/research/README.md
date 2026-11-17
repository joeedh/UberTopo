# Research Synthesis: Procedural Modeling Systems for UberTopo

This is the top-level summary of the research done against
[`docs/requirements.md`](../requirements.md). Seven research tracks were run in parallel,
and each produced its own folder of topic files. This document pulls together where they
agree, the decisions that follow, where they still disagree, and the open questions only
the project owner can answer.

Total: about 137k words in 62 files. Each track's `README.md` is the entry point for that
track.

| Track | Folder | Scope |
|---|---|---|
| B-rep topology | [brep-topology/](brep-topology/README.md) | Non-manifold structures, faces with holes, 2- vs 3-manifold, memory layout, naming |
| CAD kernels | [cad-kernels/](cad-kernels/README.md) | Kernel survey, representations, exact arithmetic, Booleans, tessellation/LOD |
| Curves | [curves/](curves/README.md) | Clothoids/spirals, road alignment standards, spline approximation, frames, curve API |
| Procedural systems | [procedural-systems/](procedural-systems/README.md) | Houdini/GeoNodes/CityEngine/PCG/Infinigen etc., engine design, determinism, LOD |
| Environment generation | [environment-generation/](environment-generation/README.md) | Buildings, interiors, roads, terrain, water, vegetation, infrastructure, GIS data |
| Attributes & queries | [attributes-and-queries/](attributes-and-queries/README.md) | Attribute systems, embedded geometry, spatial indices, query API, coordinates, LOD |
| Simulation requirements | [simulation-requirements/](simulation-requirements/README.md) | Acoustics, EO/IR, RF, vulnerability surveys, simulators, requirements matrix |

> **Verification caveat.** Web search worked for all tracks, but direct page fetches were
> blocked for many hosts (arxiv.org, doi.org, blender.org, buildingsmart, ASAM), and the
> shared search budget ran out late in several tracks. Claims taken from memory are
> marked in the files with *(unverified)*, *(verify)*, *(prior knowledge)*, `[K]` or †.
> Check all such marked items before relying on them. This matters most for numeric
> tables: the ISO 9613-1 absorption table, the Nord2000 ground classes, the IFC
> `IfcSpiral` coefficient convention and the LandXML `spiType` spellings.

---

## 1. Answers to the questions in the requirements

### 1.1 Is a 2-manifold enough, or do we need a 3-manifold?

**A 2-manifold is not enough.** All three tracks that looked at this independently
reached the same conclusion:

- [brep-topology/04](brep-topology/04-surface-vs-volumetric.md),
  [cad-kernels/recommendations](cad-kernels/recommendations.md) and
  [simulation-requirements/requirements-matrix](simulation-requirements/requirements-matrix.md)
  all conclude that **explicit volumes (regions / 3-cells) are needed**. The reasons are:
  - Shared walls between rooms and buildings must exist exactly once.
  - Media for acoustics and optics (air, water, soil, forest canopy) must be explicit
    volumes.
  - Each face side must carry its own material.
  - "Which room or medium contains this point" queries depend on regions.
  - Wave solvers need a defined air domain.
  - Arrangement-based Booleans naturally produce cell complexes.
- What is needed is a **non-manifold, mixed-dimensional 3D cell complex**, not a
  volumetric *tessellation*. Tet meshes, voxels and FDTD grids are **derived** per
  simulation; they are not the master model.
- Recommended entities: **Vertex, Edge, Coedge, Loop, Face, Shell, Region**.
  - Lower-dimensional inclusions are allowed: sensors on walls, centerlines on terrain,
    power lines in air.
  - "Building" and "room" are semantic groupings, not topological types.
- Generalized/combinatorial maps (CGAL LCC) are **rejected as the master model**. They
  have no holes in faces, cannot share topology through a single edge or vertex, and use
  2–3× the memory. They remain useful as derived volumetric formats.
- Storage follows the partial-entity approach: one coedge per face–edge incidence plus a
  side bit. This gives radial-edge capability at about half its memory.

### 1.2 Faces with holes

Faces with holes are first-class everywhere (windows, doors, courtyards, islands). A face
is bounded by **zero or more cycles**, with no stored "outer loop" flag, no bridge edges
and no OCCT-style seam edges. Openings are named entities, so an aperture face can fill a
window hole and be shared between facade and interior. See
[brep-topology/03](brep-topology/03-faces-with-holes.md).

### 1.3 Arbitrary attributes, including embedded curves and surfaces

- **Storage.** Typed, schema-registered columns stored in pages that are copied only when
  written.
  - Each attribute's schema holds its unit, role, frame, quantity kind and edit
    behaviour.
  - Two domains that DCC tools lack: **FaceSide** (different material on each side of an
    interior wall) and **Region**.
  - See [attributes-and-queries/recommendations](attributes-and-queries/recommendations.md).
- **Embedded geometry.** Curves and surfaces are attributes that point into immutable
  per-type geometry pools: 3D curves on edges, parameter-space curves on coedges,
  surfaces on faces.
- **Layered geometry.** Each curve or surface is stored as
  **procedural/transcendental definition → B-spline with certified error bound →
  tessellation**.
  - Road surfaces are exact surfaces parameterised by station (distance along the road)
    and lateral offset.
  - Lane borders and kerbs are exact curves *t = b(s)* in that parameter space, so
    adjacent pieces meet by sharing rather than by tolerance.
  - See [curves/surfaces-from-curves](curves/surfaces-from-curves.md) and
    [brep-topology/07](brep-topology/07-geometry-embedding.md).
- **Propagation.** Merges must be weighted correctly (absorption by area) or acoustic
  results become silently wrong. See
  [attributes-and-queries/attribute-propagation](attributes-and-queries/attribute-propagation.md).

### 1.4 Transcendental curves

- **Core primitive.** One primitive covers clothoids, the IFC polynomial spirals, Bloss
  curves, Viennese bends, log-aesthetic curves and river meanders: **start pose + length
  + a pluggable curvature law κ(s)**.
- **Evaluation.** Integrate in short pieces (turning ≲ 0.5 rad) with Gauss–Legendre
  quadrature or series; this yields certified polynomial caches.
- **Approximation.** The default is quintic Hermite B-splines with knots at every
  segment join.
- **Offsets.** Lane edges are offsets of the *exact* centreline, never of its
  approximation.
- **Prior art.** Bertolazzi & Frego (`Clothoids`) and Levien's kurbo are the references
  to reuse or port.
- See [curves/README](curves/README.md).

### 1.5 Geometry queries

- **Front end.** One batch-first query front end over several back ends: Embree or
  hardware ray tracing, a double-precision CPU BVH, height-field trees, NanoVDB, R-trees
  and grids. Each query names a scene version, an LOD policy and a precision tier.
- **Point location.** Done topologically: cast one ray, read the region on the side of
  the face it hits. Fast winding numbers are the fallback and validator.
- **Diffraction.** Needs real feature edges, kept separate from tessellation and
  tile-cut edges, with wedge angle and per-side materials, plus a precomputed
  edge-to-edge visibility graph.
- See [attributes-and-queries/query-types-and-api](attributes-and-queries/query-types-and-api.md).

---

## 2. The architecture the tracks converge on

```
 L0  Procedural document (source of truth)
     typed functional IR · params · seeds · content-hashed GIS inputs · override layers
       │  demand-driven incremental evaluation, stable hierarchical IDs, keyed RNG
       ▼
 L1  Design records (per entity decisions, LOD-independent)
       │  materializers (per LOD vector / per consumer)
       ▼
 L2  Exact non-manifold 3D cell complex  ◄── analytic carriers as attributes
     (faces w/ holes, regions, wires,          (clothoids, catenaries, sweeps,
      persistent IDs, columnar attributes)      road (s,t) surfaces)
     peers: 2D exact arrangements · terrain TIN/heightfield · sparse volumes (VDB)
       │
       ▼
 L3  Consumer products (caches): acoustic polygon model + diffracting edges,
     render cluster DAG, LOS BVH, collision/ESDF proxies, wave-solver voxels,
     3D Tiles / USD / CityJSON / OpenDRIVE exports, ground-truth labels
```

The consensus points, with the tracks that support each:

1. **The procedural graph is authoritative.** Geometry is derived and cached. LOD means
   *re-materializing from the design record*, not decimating.
   - Sources: procedural-systems, cad-kernels, environment-generation.
2. **Do not build or adopt a general NURBS CAD kernel.** Environment geometry is mostly
   planar, 2.5D, swept or statistical. There is no rolling-ball filleting and no general
   surface–surface intersection; curved content stays analytic and is tessellated per
   LOD.
   - Source: cad-kernels.
3. **No ε in topological decisions for generated content.** Use exact or certified
   predicates. Booleans work by "merge everything into one arrangement, then select
   cells".
   - Lineage: Masuda, SGC, mesh arrangements, EMBER and Cherchi et al.
   - Sources: cad-kernels, brep-topology.
4. **Stable identity is foundational and shared.** It covers 128-bit persistent IDs
   derived from provenance (parent ID + rule-site ID + most stable discriminator) and
   counter-based keyed RNG streams. The same scheme names B-rep faces and edges, which
   solves the topological naming problem by construction.
   - Sources: procedural-systems, brep-topology, attributes-and-queries.
5. **Columnar, index-based, copy-on-write storage** with versioned immutable snapshots.
   This gives caching, undo, concurrent readers, diffing and lineage.
   - Sources: brep-topology, attributes-and-queries, procedural-systems.
6. **LOD is a vector and a tolerance, selected per query and per consumer.** It is not
   one scalar per scene.
   - Acoustics ≈ λ/8: 43 cm at 100 Hz, 4 cm at 1 kHz.
   - Cameras: at most half the pixel footprint.
   - Collision: an outer-conservative proxy.
   - Occlusion: an inner-conservative proxy.
   - Detail below tolerance becomes **scattering, roughness or porosity attributes**; it
     is not dropped.
   - Sources: simulation-requirements, environment-generation, attributes-and-queries.
7. **Generate-on-query.** Cheap proxies sit in the spatial index and are refined only
   where a query needs the detail.
   - Sources: procedural-systems, environment-generation.
8. **Vegetation comes in three consistent forms from one description**: per-plant
   records, meshes near sensors, and volumetric media (leaf area density, stem density,
   gap fraction).
   - Sources: environment-generation, simulation-requirements.
9. **Wires, fences and lattices stay as exact curves and wire edges** with attributes.
   They are drone hazards and camera confusers.
   - Sources: environment-generation, simulation-requirements, curves.
10. **One material library serves all modalities**: acoustic band coefficients and
    ground impedance, PBR, thermal layers, and RF permittivity. It supports layered
    assemblies and records the uncertainty and source of every value.
    - Source: simulation-requirements.
11. **Real data is the skeleton; procedural generation fills in detail**, with
    provenance and confidence recorded so sensitivity runs can re-seed only the inferred
    parts.
    - Real data: OSM/Overture (GERS IDs as seeds), ML footprints and heights, bare-earth
      DEMs.
    - Sources: environment-generation, procedural-systems.
12. **Determinism is a tested contract.** Discrete decisions must not depend on thread
    count or platform. CI hashes the outputs.
    - Sources: all tracks.
13. **Procedural generation's main value for this project** is structured scenario
    variation: domain randomization, worst-case search, and perturbing surveyed sites.
    - Source: simulation-requirements/validation-and-variation.

---

## 3. Where the tracks disagree (decisions needed)

| # | Topic | Position A | Position B | Suggested resolution |
|---|---|---|---|---|
| D1 | **Coordinates and exactness** | cad-kernels: integer homogeneous **plane-based** coordinates on a per-tile lattice (~26-bit inputs, ≤256-bit predicates), with tolerances only as explicit modelling ops | brep-topology / attributes: **f64** tile-local frames with **local journaled tolerances** (Parasolid-style) for mixed GIS/scan/CAD inputs | Use **both, by provenance**. Generated planar content uses the exact lattice. Imported, scanned or curved content enters through an explicit "import/snap" operation that records its tolerance. Prototype both before committing. |
| D2 | **Vertex representation** | cad-kernels: vertices may be *implicit* (plane triples) and planes are first-class shared entities | brep-topology: explicit vertex points with curves and surfaces as geometry | Depends on D1. If plane-based, planes become a first-class geometry pool referenced by faces, and vertex points are cached roundings. |
| D3 | **Float tile frames** | attributes: f32 is allowed in tile frames up to about 8 km for mm precision | cad-kernels: f32 only at the GPU boundary (camera-relative) | Keep the f32 boundary at GPU and render only. All kernel computation uses f64 or exact arithmetic. |
| D4 | **Boolean back end** | cad-kernels: build our own plane-based exact arrangement, with Manifold as fallback | brep-topology: arrangement-and-select on our complex | These are compatible. The open risk is that no permissively licensed production implementation exists. Mitigate with differential testing against Manifold and CGAL Nef_3. |

---

## 4. Consolidated open questions for the project owner

The questions are de-duplicated across all tracks and grouped by how much they affect
the design.

**Shape the core architecture**

1. **Language:** Rust or C++? This decides reuse of kurbo (Rust) versus Clothoids, OCCT,
   Manifold and OpenVDB (C++).
2. **Reproducibility:** is bit-identical output across machines, thread counts and GPUs
   required, or is statistical reproducibility enough? The designs assume bit-identical
   for discrete decisions.
3. **Scale:** what is the largest contiguous site, and the finest detail that matters?
   This sets tile size, lattice resolution and tolerance defaults.
4. **Workload:** batch scenario generation (farm throughput) or interactive editing
   (latency, UI)? Who writes procedural rules: engineers, artists or LLM agents?
5. **Licensing and distribution:** will UberTopo be distributed outside the
   organisation? Are GPL, LGPL and ODbL share-alike acceptable? Do export-control, ITAR
   or classification rules apply to the software, site models, materials or
   vulnerability annotations? Classified sources would require air-gapped pipelines and
   access control.

**Shape the simulation interface**

6. **Acoustic fidelity:** are signals needed that are accurate enough to run
   localization algorithms on (time-domain, multichannel), or only band levels for
   coverage maps? Are wave-based solvers needed at low frequency (which needs volume
   meshes, impedance, and soil and water regions)?
7. **Sensor scope:** acoustic, visible camera, thermal IR, radar, RF, GNSS? This decides
   which material properties every generator must emit.
8. **Target simulators and formats:** for example Isaac Sim, Unreal/Colosseum, Gazebo,
   Sionna, DIRSIG; and which of CDB/OWT, 3D Tiles, OpenUSD, CityJSON/CityGML, OpenDRIVE,
   IFC 4.3, LandXML.
9. **Real time:** must simulations run in real time with hardware in the loop, or is
   offline batch enough?
10. **Deployment:** will the deployed counter-UAS system load UberTopo exports at run
    time (echo-aware localization, GNSS shadow matching)? If so, the exports must be
    compact and stable.

**Shape content priorities**

11. **Real data vs synthetic:** imported only (OSM, DEMs, LiDAR, photogrammetry, BIM),
    synthetic only, or fused? Will real road data need fitting to smooth alignments?
12. **Regions and site types first:** EU cities, US suburbs, military bases, critical
    infrastructure, informal settlements?
13. **Interiors:** in scope at high detail (atria, stadium concourses, warehouses), or
    are openings coupled to simple room volumes enough?
14. **Time variation:** vehicles, doors, water levels, tide, season, foliage in wind,
    crowds, cranes?
15. **Rail:** is it in scope, and does it need third-order-continuous transitions?
16. **Exact NURBS:** is export (STEP/IGES) or import of exact CAD/BIM needed in phase 1?
17. **Validation site:** is a real test site with survey data, weather instruments and
    controlled sources available for validating the simulation chain?

---

## 5. Recommended next steps

Per [CLAUDE.md](../../CLAUDE.md), each of these becomes a plan in `docs/plans/` and is
pressure-tested by a fresh-context agent before implementation.

1. **Get answers to questions 1–5 above.** They change the plans materially.
2. **Write a design doc in `docs/`** for the core data model:
   - cell complex entities and storage modes;
   - identity scheme;
   - attribute schema and domains;
   - the geometry pools;
   - the resolution of D1–D3.
3. **Spike 1 — exactness (D1):** a plane-based exact arrangement on procedural buildings
   (shared walls, window holes, room regions), differential-tested against Manifold and
   CGAL Nef_3.
4. **Spike 2 — procedural core:** an IR and demand-driven query engine on simple values,
   with stable IDs, keyed RNG and thread-count invariance tests.
5. **Spike 3 — curves:** a curvature-law curve with certified quintic B-spline caches
   and exact offsets, validated against Bertolazzi–Frego `Clothoids`.
6. **Spike 4 — vertical slice:** a small site with terrain, roads, buildings with
   openings and a tree volume, exported as an acoustic polygon model with diffracting
   edges and per-side materials. This proves the simulation contract end to end.
