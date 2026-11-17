# Recommendations: Attribute System and Query Architecture for UberTopo

Part of [attributes-and-queries](README.md). This file synthesises the survey files
into a concrete proposal. It is a research recommendation, not a plan. A plan
belongs in `docs/plans/` and should be pressure-tested per `CLAUDE.md`.

## 1. Design principles

1. **Columnar, runtime-typed, paged, copy-on-write.** All per-element data,
   including geometric binding, lives in SoA columns split into pages that are
   shared until written (Houdini pages + Blender implicit sharing, at page
   granularity).
2. **Behaviour belongs to the schema, not the type.** Every attribute name is
   registered with unit, role, frame, quantity kind and propagation policy (ACIS
   attribute classes + BMesh interp callbacks + 3D Tiles metadata schemas).
3. **Identity is designed in from day one.** Generation-checked handles inside a
   body, 128-bit persistent IDs everywhere else. Generators emit semantic keys.
   Kernel operators derive IDs from history (to avoid FreeCAD's topological
   naming problem).
4. **Exact definitions plus certified approximants.** Transcendental road curves,
   analytic surfaces and procedural generators are kept alongside their tessellations,
   which carry error bounds.
5. **One query front-end, many back-ends, batch-first, snapshot-isolated,
   deterministic.** Queries name a scene version and an explicit LOD policy, never
   implicit view state.
6. **Simulation LOD ≠ rendering LOD.** Selection is by physical relevance
   (wavelength, sensor resolution) and fixed per run.
7. **Local precision by construction.** float64 site frames, and integer-translated
   tile frames in which float32 is safe.

## 2. Domains

The attribute domains follow the non-manifold B-rep (topology details belong to the
B-rep research topic; the list below is what the attribute layer needs):

| Domain | Meaning | Face-corner analogue / notes |
|---|---|---|
| `Global` | whole body/scene (Houdini "detail") | metadata, generator params |
| `Body` | a connected modelling object | building, terrain tile part, road |
| `Region` | 3-cell / volume (room, building interior, water body, canopy volume, air) | medium properties, `geom.medium_field` |
| `Shell` | boundary shell of a region | optional; mostly derived |
| `Face` | 2-cell | carrier surface, semantic class |
| **`FaceSide`** | one side (use) of a face; exactly two per face | **per-side materials** (plaster inside, brick outside), per-side region link |
| `Loop` | boundary loop of a face (outer/hole) | opening identity (window hole) |
| `Coedge` | use of an edge by a loop | pcurve, UV/normal "face corner" data |
| `Edge` | 1-cell | 3D curve, diffraction flags, tile-seam flag |
| `Vertex` | 0-cell | position, tolerance |
| `Instance` | placed prototype | transform, prototype/GeneratorRef, per-instance attrs |
| `CurvePoint` / `Curve` | standalone curve sets (power lines, centrelines) | as in Blender `CurvesGeometry` |
| `Point` | standalone point sets (lidar, sensors, samples) | as in a point cloud |

`FaceSide` is the main addition over existing DCC systems. A non-manifold model of
a building has interior walls separating two rooms, and each side needs its own
material, absorption, region link and annotations. Booleans and face reversal swap
sides via the `swap-sides` policy.

## 3. Column storage

```
AttributeColumn {
  schema:   SchemaId            # name@version -> type, shape, unit, role, frame, kind, policy, default
  domain:   Domain
  pages:    COW<PageTable>      # page size 1024 (tunable); each page:
                                #   Absent | Constant(value) | Dense(COW<buffer>)
  version:  u64                 # bumped on any write; drives cache/BVH invalidation
}
```

* **Value types (closed set):** `bool`; `i8…i64`; `u8…u64`; `f16`, `f32`, `f64`;
  `vec2/3/4` and `mat3/4` of f32/f64; `quat`; **fixed arrays** `T[N]` (spectra:
  `f32[8]` octave bands, `f32[31]` third-octave); **enum/dictionary** (u16/u32 index
  into a versioned table); dictionary strings; **references** (`ElemRef<D>`,
  `PersistentId`, `ObjectRef`, `GeomRef`, `MaterialRef`, `FieldRef`,
  `GeneratorRef`); **variable-length lists** (Arrow-style offsets + values, e.g.
  `proc.sources`); **structs** (stored as one sub-column per field, SoA, addressable as
  `name.field`).
* **Sparse/rare attributes** use `Absent` pages. Piecewise-constant attributes use
  `Constant` pages. Order elements spatially or by generation so runs are long.
* **Arrow-compatible buffers** (validity bitmaps from Absent pages, dictionary
  encodings) so a tile's attributes can be exported to Arrow/GeoParquet zero-copy or
  near-zero-copy for analytics.
* **Units:** stored in the schema, never per value, and checked when graphs connect.
  A C++ facade can map schema units to `mp-units` quantity types at compile time
  where code is statically typed.
* **Computed attributes** (`area`, `normal`, `dihedral_angle`, `region_adjacency`,
  `local_up`, `curvature`) are registered like stored ones, with a compute function
  and dependency versions (topology/geometry/columns). They are cached in the same
  page structure and invalidated by version change. Algorithms access stored and
  computed attributes through the same accessor interface (CGAL property-map style).

## 4. Schema registry

Each schema entry:

```
name:          "acoustic.absorption"           # namespaced
version:       2
value_type:    f32[8]                          # octave bands 63 Hz .. 8 kHz
unit:          "1"                             # dimensionless
role:          none
frame:         n/a
kind:          intensive                       # extensive | intensive | categorical | identity | derived
domain_hint:   FaceSide
policy:        { interp: linear, split: copy, merge: area-weighted-mean,
                 lod: area-weighted-mean, mirror: swap-sides }
default:       resolve_via("material.acoustic.absorption")   # lookup chain: element -> material -> default
migrations:    { 1 -> 2: expand_third_octave_to_octave }
doc:           "..."
```

Initial namespaces:

| Namespace | Contents |
|---|---|
| `geom.*` | point, curve, curve_exact, pcurve, surface, embedded, tol, tess |
| `topo.*` | tile_seam, artificial, manifoldness flags |
| `sem.*` | feature (PersistentId), class (enum: CityGML/OSM-derived), usage, name |
| `mat.*` | MaterialRef per FaceSide/Region |
| `acoustic.*` | absorption, scattering, transmission, impedance_model, diffracting (edge), wedge_angle |
| `optical.*`, `ir.*`, `rf.*` | albedo/BRDF ref, emissivity bands, permittivity (if RF in scope) |
| `medium.*` | temperature, humidity, wind (fields), vegetation density/attenuation |
| `road.*` | id, s, t, lane |
| `proc.*` | origin, sources, gen (GeneratorRef) |
| `survey.*` | asset criticality, entry_point, annotations |
| `src.*` | (on tessellations/proxies) back-references to B-rep face/edge/feature |

Material and semantic tables are themselves columnar tables (rows = materials or
features) with their own schemas. Per-element spectra are overrides, not the default
storage.

## 5. Identity

* **Handles:** `{index: u32, generation: u32}` per domain, with slot reuse and lazy
  compaction. Handles are only valid within a body snapshot.
* **Persistent IDs:** 128 bits. 64-bit IDs risk birthday collisions at ~10⁹–10¹⁰
  elements across a large world, so use 128. They are formed as
  `hash(namespace, generator semantic key)` for generator-emitted features, and
  `hash(parent_id, op_kind, discriminator)` for operator-derived elements (split
  child 0/1, intersection edge of (A, B), side ±). They are stored in `sem.feature`
  (features) and an element-level `proc.origin` (elements).
* **Cross-LOD:** features keep IDs across all LODs. Elements map to features via
  `sem.feature`, and coarse elements list sources via `proc.sources`. A **feature
  hierarchy table** records parent and minimum explicit LOD.
* **Cross-tile:** global feature IDs. Tile-local elements reference them, and seam
  pairing records stitch adjacency.

## 6. Propagation engine

The kernel's topological operators **emit events** and do not manipulate attributes
themselves (cf. CGAL corefinement visitors):

```
on_create(new, sources[], weights[], op)     # interpolation (split point, subdivision stencil)
on_split(old, children[], measures[], op)    # face/edge split; measures = areas/lengths
on_merge(olds[], new, measures[], op)
on_reverse(face)                              # face-side swap
on_delete(elem)
on_transform(body, M)
```

A generic **attribute propagator** applies each column's policy (§4) to each event,
vectorised over event batches. Operators may override the policy for a specific
column when they know better (a road imprint sets `road.*` on its new edges
explicitly). The propagator also maintains `proc.origin`/`proc.sources` automatically
and writes events to an append-only **provenance log**.

Required test invariants (see
[attribute-propagation.md](attribute-propagation.md) §7): extensive conservation,
intensive integral conservation under splits, identity stability under identical
regeneration, and provenance closure.

## 7. Embedded geometry

* Geometry binding uses reserved `geom.*` columns. Meshes and exact B-reps share one
  structure, and a planar-faced mesh just lacks `geom.surface`.
* Geometry objects live in **immutable per-type SoA arenas** (lines, arcs, conics,
  B-splines, clothoids/spiros/polynomial clothoids, sweeps, heightfield windows,
  subdivision cages, SDF/expression), referenced by `{type: u8, index: u32}`, and
  shared across splits, copies and LODs.
* **Exact + approximant pairs** with certified `max_error` and an explicit arc-length
  reparameterisation map.
* **Embedded curve sets** on faces with lazy imprinting. Breaklines are resolved by
  CDT at tessellation time, and markings are never imprinted.
* **Road surfaces** are sweep carriers referencing a `Road` entity (plan curve,
  elevation and crossfall profiles, lane widths). Tessellation emits `road.s/t/lane`.
* Tessellation returns `mesh + src.*` back-references, so every query hit maps to
  B-rep elements and semantic features.

## 8. Query architecture

```
L4  Algorithms     acoustic paths (image-source, stochastic, diffraction graph), viewshed/coverage,
                   sensor placement, corridor search, sampling, validation (Hausdorff, conservation)
L3  Query API      batch-first; QuerySpec {kind, frame, lod, filter, precision, gather, snapshot, determinism}
L2  Scene snapshot SceneVersion = tiles × LOD cut × (topology, attribute pages, arenas, accel structs);
                   MVCC via COW; pinned by long runs; epoch reclamation
L1  Shape adapters TriMesh/BRep-proxy(+exact refine)/Heightfield/Volume/Curves/Points/Instances/Procedural;
                   capabilities: bounds, rays, closest, contains, integrate, sample, tessellate
L0  Back-ends      Embree 4 (CPU fp32) | CPU fp64 BVH (tinybvh-derived or custom) + Shewchuk predicates |
                   Vulkan ray query (+DXR/Metal via thin HAL) | max-mipmap heightfield | NanoVDB/HDDA |
                   R-tree (Boost.Geometry/flatbush-style, 2D source data) | uniform grid/hash | nanoflann
```

Key decisions:

* **TLAS per tile, BLAS per object/chunk.** Instancing for vegetation and repeated
  components. Moving drones and vehicles are instances. Plan the instance-mask bit
  budget, and use separate TLASes per query population when masks run out.
* **Inline ray queries in compute** for GPU simulation (portable, wavefront-friendly).
  Use full RT pipelines only for rendering.
* **Point location** is topological first (ray → face-side → region) and uses fast
  winding numbers as the fallback and validator.
* **Diffraction** uses derived edge attributes, an edge BVH, and a precomputed
  edge-to-edge visibility graph per LOD cut (Schissler et al. 2021 style).
* **Coverage and viewshed** output VDB/raster fields. Terrain uses max-mipmap
  heightfields, not triangulated BLASes, in the CPU path. On GPU, tessellate terrain
  tiles to BLASes (hardware RT) or use a compute DDA.
* **Precision tiers** are explicit: `Fast32` in tile frames, `Robust64`, `Exact`. Kernel
  ops default to `Robust64`/`Exact`.
* **Determinism:** fixed tie-breaks, per-query RNG streams, LOD cut fixed per run.
* **Dynamic edits:** version counters invalidate BLASes per tile. Rigid motion only
  touches the TLAS, deformation refits, and topology edits rebuild the affected tile's
  BLAS (cluster-level later with CLAS/DGF/PTLAS).

## 9. Large world

* float64 **site ENU frame**. **Tile frames** are integer translations (tile size a
  power of two, ≤ 8 km for mm-level float32). ECEF bridges sites. PROJ handles every
  CRS conversion. Vertical datum, CRS and epoch are recorded on imported data.
* Site-frame **quadtree tiling** internally, **S2 cell IDs** for global keys, **H3**
  for analytics outputs only.
* Simulation streaming is **footprint-driven and blocking**, with immutable
  **content-addressed tile blobs**.
* **Tile-seam edges are flagged** (excluded from diffraction, locked in
  simplification), and seams are bitwise watertight within an LOD.
* Export **3D Tiles 1.1** (`EXT_mesh_features` + `EXT_structural_metadata`) for
  viewers. Treat **CDB/One World Terrain** as a probable simulator interchange target
  (confirm with the owner).

## 10. LOD

* Representational LOD comes **from generators** (CityGML-like LOD x.y levels).
  Geometric LOD comes from attribute-aware simplification (meshoptimizer for meshes,
  VSA/planar extraction for acoustic proxies).
* **Acoustic proxies per frequency band**, honouring watertightness, per-region volume,
  per-material area and absorption area, openings as portals, diffracting edges above
  a λ-scaled threshold, and planarity. Removed detail is folded into the scattering
  coefficient.
* **Vegetation** gets volumetric proxies (VDB density + per-band attenuation) plus
  trunk cylinders. **Thin structures** (wires, fences, poles) are curve or instance
  primitives that never get simplified away.
* A **cluster DAG** (Nanite-like) is a derived representation for visualisation and
  bulk stochastic ray queries at a world-space ε-cut. It is never the source of truth,
  and is not used for image-source acoustics.
* **Validation suite** per LOD: Hausdorff, semantic closure, acoustic conservation,
  topological consistency, behavioural regression.

## 11. Suggested phasing

| Phase | Deliverables | Rationale |
|---|---|---|
| 1 | Column store (pages, COW, constant/absent pages), schema registry with units/roles/policies, handles + 128-bit persistent IDs, propagation engine with event API, provenance log; CPU query layer on Embree (closest hit, any hit, all hits, closest point, fast winding number), topological point location; site/tile frames; Arrow export | Everything else depends on identity and attribute semantics. CPU-only keeps it debuggable. |
| 2 | Geometry arenas (exact + approximant curves, sweep and heightfield carriers), embedded curve sets + CDT tessellation with `src.*`/`road.*` attributes; fp64 robust BVH path; heightfield max-mipmap; NanoVDB fields and HDDA ray-march; snapshot/MVCC | Road networks and terrain are core requirements. |
| 3 | Vulkan ray-query backend (batched, wavefront); LOD cut selection by ε(x) field; acoustic proxies (planar, per band); diffraction edge graph; coverage/viewshed fields | Simulation-scale throughput. |
| 4 | Cluster DAG + cluster AS/DGF for massive scenes; 3D Tiles export; CDB/OWT interchange; optional differentiable backend | Scale-out and interop. |

## 12. Risks

* **Complexity of policies.** Too many policy knobs means nobody sets them right.
  Mitigation: derive defaults from `kind` (extensive/intensive/categorical/identity),
  so most schemas need only a kind.
* **Persistent-ID discipline in generators.** If generators emit unstable semantic
  keys (e.g. based on iteration order), cross-revision identity fails. Mitigation:
  lint and tests (regenerate with perturbed parameters and diff IDs).
* **GPU/CPU result divergence.** fp32 hardware RT vs fp64 CPU gives different
  near-grazing results. Mitigation: documented tolerances, precision tier carried in
  results, and cross-backend regression tests.
* **Hardware RT vendor features** (CLAS, PTLAS, DGF) are recent and vendor-specific,
  and the DXR clustered geometry spec is still in preview as of late 2026. Keep them
  behind the L0 HAL, with a portable fallback.
* **Acoustic proxy generation** is research-grade. Automatic planar proxies of
  arbitrary procedural/photogrammetric geometry with all conservation constraints is
  not a solved off-the-shelf problem. Procedural generation of acoustic LODs
  *directly from generators* sidesteps most of it for synthetic content.

## 13. Open questions for the project owner

1. **Sensor modalities in scope:** acoustic and optical (visible/IR) are explicit. Is
   RF/radar in scope? It adds permittivity attributes, different LOD rules (λ in cm)
   and possibly wave-based solvers.
2. **Acoustic solver family:** geometric acoustics only (image-source, ray, UTD/BTM
   diffraction), or also wave-based (FDTD/BEM) for low frequencies? Wave-based solvers
   need volumetric meshes or voxelisations and complex impedance attributes.
3. **Area extent and precision:** typical and maximum site extent, and the required
   absolute georeferencing accuracy versus relative consistency.
4. **Interchange requirements:** must the system import/export CDB, One World
   Terrain, CityGML/CityJSON, OpenDRIVE, IFC or 3D Tiles? Which are mandatory?
5. **Data sources:** synthetic only, or real-world imports (OSM, national DEMs,
   photogrammetric meshes, lidar)? Imports raise robustness and re-identification
   requirements considerably.
6. **Real-time vs batch:** do simulations need interactive rates (hardware-in-the-loop
   with live sensors), or are offline batch runs acceptable? This decides how early
   the GPU backend is needed.
7. **Determinism standard:** is bitwise reproducibility across machines and GPUs
   required for test and evaluation records, or is statistical reproducibility
   enough?
8. **Dynamic environment:** do scenarios include moving or changing geometry beyond
   drones and vehicles (doors opening, vegetation in wind, water surfaces, weather
   changing media fields over time)?
9. **Security and classification:** are some attributes (asset criticality,
   vulnerabilities) sensitive, needing per-attribute access control or separable
   storage?
10. **Scripting and analytics surface:** is a Python/Arrow interface for analysts
    expected? This affects the column layout decision.

## References

See the reference lists in each topic file:
[attribute-systems-survey.md](attribute-systems-survey.md),
[attribute-propagation.md](attribute-propagation.md),
[embedded-geometry-attributes.md](embedded-geometry-attributes.md),
[spatial-acceleration-structures.md](spatial-acceleration-structures.md),
[query-types-and-api.md](query-types-and-api.md),
[large-world-coordinates-and-tiling.md](large-world-coordinates-and-tiling.md),
[lod-and-simplification.md](lod-and-simplification.md).
