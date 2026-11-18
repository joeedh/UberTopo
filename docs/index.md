# Docs Index

Index of everything under `docs/`. Update this file whenever a doc is added, moved, renamed or removed (see `CLAUDE.md`).

## Top level

| Doc | Description |
|---|---|
| [requirements.md](requirements.md) | Project requirements |
| [decisions.md](decisions.md) | Project decision log: DEC-001 C++20/litestl, DEC-002 no third-party libs in core, DEC-003 litestl submodule + additions in litestl, DEC-004 allowed third-party (formats/compression/LAZ), DEC-005 Chromium shell + N-API, DEC-006 NW.js / defer N-API migration & Python / SoA B-rep modelled on Sculptcore, DEC-007 coord type as template param / concepts / open attribute system, DEC-008 core data model OIs resolved / litestl push / 40 KB file rule, DEC-009 hash/tuple encoding/RNG layout, DEC-010 P1 attribute core decisions, DEC-011 litestl work merged to master, DEC-012 compile-time attribute schemas; open questions |
| [debugging.md](debugging.md) | Running debugging guide |
| [index.md](index.md) | This index |

## Design (`design/`)

### Core data model (`design/core-data-model/`) — draft v0.2

- [README.md](design/core-data-model/README.md) — Overview, ten core rules, open issues
- [01-cell-complex.md](design/core-data-model/01-cell-complex.md) — Entities, topology columns, faces with holes, radial order and wedge rule, inclusions, shells/regions, primitives, validation, editable/frozen
- [02-coordinates-and-predicates.md](design/core-data-model/02-coordinates-and-predicates.md) — Coord concepts and traits, instantiations (double/float/Lattice/plane-based option), predicates, tile frames, conversions
- [03-attributes.md](design/core-data-model/03-attributes.md) — Open attribute system: paged COW storage, column interface, registry, domains, propagation, schemas
- [04-identity-versions-journal.md](design/core-data-model/04-identity-versions-journal.md) — Indices, handles, persistent IDs, keyed RNG, snapshots, journal, compaction, determinism contract
- [05-geometry-pools.md](design/core-data-model/05-geometry-pools.md) — GeomRef, geometry pools, layered exact/approximation geometry, initial curve and surface kinds
- [06-modules-litestl-and-phasing.md](design/core-data-model/06-modules-litestl-and-phasing.md) — Source layout, litestl work list, phases P0–P5
- [07-topology-api.md](design/core-data-model/07-topology-api.md) — Realised `geom/`, `ident/`, `topo/` API (P2): coordinates and frames, derived predicates, canonical CDT, IDs and refs, domain schemas, Complex, radial order, builder (with opt-in projected faces), regions, three-layer validator, freeze/serialisation/hashes, test generators

## Plans (`plans/`)

Plans must be pressure-tested by a fresh-context agent before implementation.

- [2026-11-17-p0-litestl-foundations.md](plans/2026-11-17-p0-litestl-foundations.md) — P0 (**implemented**; v2 reviewed): wide ints, exact/adaptive predicates, hash128 (SipHash-2-4-128), Philox RNG, deterministic reduce in litestl
- [2026-11-18-p1-attribute-core.md](plans/2026-11-18-p1-attribute-core.md) — P1 (**implemented**; v3 after two reviews; intern pools deferred to P1b): litestl `attr/` — paged COW columns with partial tails, open column interface, simultaneous mixing, groups with builtin slots, locked registry, page-size-independent hashing/serialization, page-size benchmark
- [2026-11-18-p2-cell-complex.md](plans/2026-11-18-p2-cell-complex.md) — P2 (**implemented**: P2a + P2b; v2 reviewed; amendment A1 v3 (opt-in projected faces, canonical CDT) reviewed twice): UberTopo `geom/` (Lattice, derived exact predicates), `ident/` (persistent IDs, typed refs), `topo/` domains and `Complex<C>`, bulk builder, exact radial order, `build_regions()`, validator, freeze/thaw, determinism suite

## Research (`research/`)

- [research/README.md](research/README.md) — **Start here.** Synthesis across all research tracks: answers to the requirements questions (2- vs 3-manifold, holes, attributes, curves, queries), the convergent architecture, cross-track disagreements, consolidated open questions, next steps.

- [research/sculptcore-brep-reference.md](research/sculptcore-brep-reference.md) — Sculptcore's SoA mesh B-rep as a reference: what carries over, what must change, what to add

### B-rep topology (`research/brep-topology/`)

- [README.md](research/brep-topology/README.md) — B-rep topology data structures — research
- [01-survey-classical.md](research/brep-topology/01-survey-classical.md) — 01 — Survey: classical B-rep topology structures (literature)
- [02-survey-systems.md](research/brep-topology/02-survey-systems.md) — 02 — Survey: topology in industrial kernels and open libraries
- [03-faces-with-holes.md](research/brep-topology/03-faces-with-holes.md) — 03 — Faces with holes (inner loops)
- [04-surface-vs-volumetric.md](research/brep-topology/04-surface-vs-volumetric.md) — 04 — Is a 2-manifold enough? Surfaces vs. volumetric cell complexes
- [05-invariants-operators-validity.md](research/brep-topology/05-invariants-operators-validity.md) — 05 — Topological invariants, Euler operators, validity, robust editing
- [06-memory-layout-performance.md](research/brep-topology/06-memory-layout-performance.md) — 06 — Memory layout, performance, parallelism, persistence, and naming
- [07-geometry-embedding.md](research/brep-topology/07-geometry-embedding.md) — 07 — Embedding geometry in topology
- [08-critique-and-recommendation.md](research/brep-topology/08-critique-and-recommendation.md) — 08 — Critique of existing designs, and the recommended UberTopo topology

### CAD kernels, representations & robustness (`research/cad-kernels/`)

- [README.md](research/cad-kernels/README.md) — CAD kernels, geometric representations and numerical robustness
- [booleans-and-operations.md](research/cad-kernels/booleans-and-operations.md) — Modelling operations: Booleans, offsets, fillets, sweeps, intersection, trimming
- [kernel-survey.md](research/cad-kernels/kernel-survey.md) — Survey and critique of geometry kernels
- [recommendations.md](research/cad-kernels/recommendations.md) — Recommendations: representation, robustness, borrow vs build
- [representations.md](research/cad-kernels/representations.md) — Geometric representations for multi-LOD environment modelling
- [robustness-and-exact-arithmetic.md](research/cad-kernels/robustness-and-exact-arithmetic.md) — Robustness: tolerances, exact arithmetic and large coordinates
- [tessellation-lod.md](research/cad-kernels/tessellation-lod.md) — Tessellation and multi-LOD output

### Curves & surfaces (`research/curves/`)

- [README.md](research/curves/README.md) — Curves and Surfaces for Procedural Environment Modelling — Research Index
- [clothoids-and-spirals.md](research/curves/clothoids-and-spirals.md) — Clothoids, Spirals and Curvature-Defined Curves
- [curve-library-design.md](research/curves/curve-library-design.md) — Curve Library Design for UberTopo (proposal)
- [numerical-evaluation.md](research/curves/numerical-evaluation.md) — Numerical Evaluation of Curvature-Defined (Transcendental) Curves
- [road-alignment-standards.md](research/curves/road-alignment-standards.md) — Road and Rail Alignment Geometry and Interchange Standards
- [space-curves-and-frames.md](research/curves/space-curves-and-frames.md) — Space Curves and Moving Frames
- [spline-approximation-and-offsets.md](research/curves/spline-approximation-and-offsets.md) — Polynomial Approximation of Transcendental Curves, and Offsets
- [surfaces-from-curves.md](research/curves/surfaces-from-curves.md) — Surfaces Built from Curves, and Curve/Surface Data in the B-rep

### Procedural modeling systems (`research/procedural-systems/`)

- [README.md](research/procedural-systems/README.md) — Procedural modeling systems: research overview
- [academic-foundations.md](research/procedural-systems/academic-foundations.md) — Academic foundations of procedural modeling
- [critique-and-recommendations.md](research/procedural-systems/critique-and-recommendations.md) — Critique of existing procedural systems and recommended architecture for UberTopo
- [determinism-and-identity.md](research/procedural-systems/determinism-and-identity.md) — Determinism, seeded randomness, stable identity and overrides
- [evaluation-engine-design.md](research/procedural-systems/evaluation-engine-design.md) — Evaluation engine design for procedural modeling
- [large-world-and-lod.md](research/procedural-systems/large-world-and-lod.md) — Large worlds, partitioned generation and multi-LOD semantics
- [survey-dcc-and-engine-systems.md](research/procedural-systems/survey-dcc-and-engine-systems.md) — Survey: general-purpose procedural systems (DCC tools and game engines)
- [survey-domain-and-game-systems.md](research/procedural-systems/survey-domain-and-game-systems.md) — Survey: domain-specific generators and game world generation

### Environment generation (`research/environment-generation/`)

- [README.md](research/environment-generation/README.md) — Environment generation research
- [buildings.md](research/environment-generation/buildings.md) — Building generation: massing, roofs, facades, openings, typologies
- [infrastructure.md](research/environment-generation/infrastructure.md) — Physical infrastructure
- [interiors.md](research/environment-generation/interiors.md) — Interiors and floor plans
- [ml-generation.md](research/environment-generation/ml-generation.md) — ML-based environment generation (state as of late 2026): a skeptical review
- [real-world-data-sources.md](research/environment-generation/real-world-data-sources.md) — Real-world data ingestion and procedural fill-in
- [road-networks.md](research/environment-generation/road-networks.md) — Road networks, junctions, rail, and transport structures
- [sensing-properties-and-lod.md](research/environment-generation/sensing-properties-and-lod.md) — Cross-cutting: LOD ladders and sensing-relevant properties
- [terrain.md](research/environment-generation/terrain.md) — Terrain generation
- [urban-layout.md](research/environment-generation/urban-layout.md) — City layout, land use, blocks and parcels
- [vegetation-and-biomes.md](research/environment-generation/vegetation-and-biomes.md) — Vegetation and biomes
- [water.md](research/environment-generation/water.md) — Water: rivers, lakes, wetlands, coasts, canals, bathymetry

### Attributes, queries & large-world data (`research/attributes-and-queries/`)

- [README.md](research/attributes-and-queries/README.md) — Attributes, Geometry Queries and Large-World Data Management
- [attribute-propagation.md](research/attributes-and-queries/attribute-propagation.md) — Attribute Propagation Through Topology Edits
- [attribute-systems-survey.md](research/attributes-and-queries/attribute-systems-survey.md) — Attribute Systems: A Survey of Existing Designs
- [embedded-geometry-attributes.md](research/attributes-and-queries/embedded-geometry-attributes.md) — Embedding Curves and Surfaces as Attribute Data
- [large-world-coordinates-and-tiling.md](research/attributes-and-queries/large-world-coordinates-and-tiling.md) — Large-World Coordinates, Precision, Tiling and Streaming
- [lod-and-simplification.md](research/attributes-and-queries/lod-and-simplification.md) — Level of Detail, HLOD and Semantics-Preserving Simplification
- [query-types-and-api.md](research/attributes-and-queries/query-types-and-api.md) — Geometry Query Types and a Unified Query API
- [recommendations.md](research/attributes-and-queries/recommendations.md) — Recommendations: Attribute System and Query Architecture for UberTopo
- [spatial-acceleration-structures.md](research/attributes-and-queries/spatial-acceleration-structures.md) — Spatial Acceleration Structures

### Simulation requirements (drone defense) (`research/simulation-requirements/`)

- [README.md](research/simulation-requirements/README.md) — Simulation Requirements Research
- [acoustic-drone-localization.md](research/simulation-requirements/acoustic-drone-localization.md) — Acoustic Drone Detection and Localization: Simulation Chain Requirements
- [eo-ir-camera-simulation.md](research/simulation-requirements/eo-ir-camera-simulation.md) — EO/IR Camera Simulation: What the Environment Model Must Supply
- [outdoor-acoustics.md](research/simulation-requirements/outdoor-acoustics.md) — Outdoor and Urban Acoustic Propagation: What the Simulations Need
- [requirements-matrix.md](research/simulation-requirements/requirements-matrix.md) — Derived Requirements Matrix
- [rf-propagation.md](research/simulation-requirements/rf-propagation.md) — RF Sensing and Propagation: Radar, RF Detection, GNSS
- [simulators-and-engines.md](research/simulation-requirements/simulators-and-engines.md) — Simulators, Engines and Interchange Formats
- [validation-and-variation.md](research/simulation-requirements/validation-and-variation.md) — Validation, Uncertainty Quantification and Scenario Variation
- [vulnerability-surveys.md](research/simulation-requirements/vulnerability-surveys.md) — Site Vulnerability Surveys and Drone-Swarm Simulation
