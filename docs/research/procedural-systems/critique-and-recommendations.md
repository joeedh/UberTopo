# Critique of existing procedural systems and recommended architecture for UberTopo

Part of the [procedural-systems research set](README.md). This file condenses the surveys
([DCC/engines](survey-dcc-and-engine-systems.md), [domain/games](survey-domain-and-game-systems.md),
[academic](academic-foundations.md)) and the design analyses
([evaluation](evaluation-engine-design.md), [determinism/identity](determinism-and-identity.md),
[large worlds/LOD](large-world-and-lod.md)) into a critique and a proposed architecture.

---

## 1. Concrete legacy limitations, system by system

### Houdini
1. Geometry is a polygon/primitive soup with attributes; no persistent topology, no faces with
   holes, no CAD-grade B-rep. Topological operations recompute adjacency each time.
2. Identity is the element index (`@ptnum`, `@primnum`); groups and edit SOPs store indices.
   Any upstream insertion breaks downstream edits and randomness.
3. Invalidation is per node over all elements, with dirty bits and no early cutoff.
4. Hidden dependencies through HScript/Python parameter expressions and channel references;
   global time dependency.
5. At least four evaluation models (SOP cook + compiled blocks, PDG, APEX, LOPs/USD, plus DOPs)
   with separate caching and identity. PDG's cache is file-existence based.
6. Scale beyond memory requires PDG and files; the engine itself is single-scene, in-memory.
7. Closed source, licence-gated embedding (Houdini Engine).

### Blender Geometry Nodes
1. No incremental recomputation across edits; whole node tree re-evaluates when a dependency
   changes (bakes are manual).
2. Mesh faces cannot have holes; no B-rep; curves are evaluated to polylines for most ops.
3. Index identity by default; `id` attribute is opt-in and easy to lose.
4. Tied to Blender's object/ID model and depsgraph; not embeddable (GPL, process model).
5. No partitioned or streaming generation.
6. Type inference for closures is incomplete (multi-call-site ambiguity).
Positive: fields, anonymous attributes, implicit sharing, lazy-function graphs and the 5.x
functional features are the best design ideas in the DCC world.

### CityEngine / CGA
1. Rules see one shape at a time; context only via occlusion queries; CGA++'s first-class
   shapes and synchronization are largely absent from the product.
2. Box-scope-centric operations; arbitrary polyhedra and curved geometry are second-class.
3. Exterior-centric; interiors and non-building environments (vegetation, water) are weak.
4. Output is meshes with tags, not a topological model with shared boundaries.
5. Edits to generated output do not survive regeneration except through attributes on inputs.
6. Proprietary runtime (PRT); CGA is a closed language.
Positive: GIS-attribute-driven rules on real footprints; rule packages as portable artifacts.

### Unreal PCG
1. Point-centric data model; topology and CAD operations are not its domain.
2. Output identity is engine actors / ISM indices; hand edits to generated actors are lost on
   regeneration (remedy: input-side exclusion and tags).
3. Node/component/cell-granular invalidation.
4. Cross-partition coherence must be engineered manually via HiGen levels.
5. Tied to the Unreal object model; data-format churn (5.6 SoA points broke Blueprint graphs).
Positive: partitioned + hierarchical + runtime generation with budgets, GPU nodes,
position-derived seeds — the best production model for large-world streaming.

### Grasshopper / Dynamo / Sverchok
1. Eager push evaluation; downstream recomputed wholesale.
2. Data trees/list replication: powerful but error-prone implicit iteration semantics.
3. Index/trace identity; baking severs the link.
4. Largely single-threaded.
5. Rhino's OpenNURBS geometry is robust but its API design dates from the 1990s.
Positive: Dynamo's element binding is an early, practical answer to identity across re-runs.

### Terrain tools (World Machine, Gaea, World Creator)
1. 2.5D heightfields; no overhangs, caves, structures.
2. Non-local erosion makes tiled results depend on tile size.
3. Whole-tile granularity; no element identity.

### Infinigen
1. Batch, per-scene, slow; no incremental editing.
2. Built on Blender Python; RNG partially sequential (scoped by `FixedSeed`).
3. Scenes bounded around camera trajectories, not persistent geo-referenced worlds.
Positive: breadth and realism of seeded asset factories; constraint DSL + solver for indoor
arrangement; first-class ground-truth outputs; camera-aware meshing (OcMesher).

### Game generators (Minecraft, NMS, HZD)
1. Domain-limited (voxels, heightfields, scatter); not CAD.
2. Edits are stored as generated-chunk state or position deltas; rule changes move terrain
   under edits.
Positive: pure positional functions, chunk stages with declared neighbour radii, world-anchored
seeds, "locally stable" placement, schedule annotations (cache markers) in the function graph.

### USD / Omniverse
1. Not a procedural engine (OpenExec is early and rig-focused).
2. Mesh schema without topology; prim-count scalability limits for millions of generated
   elements.
Positive: layered opinions with strength ordering, path-addressed overrides, variants,
payloads, instancing — the right conceptual model for overrides and interchange.

---

## 2. Cross-cutting failures (what "90s foundations" means here)

1. **Index-based identity** everywhere → randomness, edits and caches are unstable.
2. **Node-granular invalidation** with no early cutoff → edits cost O(world).
3. **Mutable, side-effectful evaluation** (scripts, global RNG, expression references) →
   caching, parallelism and determinism are best-effort.
4. **Bolt-on scale** (PDG, partitions, tiled builds) → two or more evaluation models with
   different identity and caching.
5. **Geometry model below CAD grade** (soups, meshes without holes) or above procedural
   convenience (OpenNURBS APIs) — no system has both a robust B-rep and a fast attribute-centric
   procedural data model.
6. **LOD as an afterthought** (decimation, separate assets) rather than a property of the
   generator; decisions re-made per LOD.
7. **Overrides as an unsolved problem** — bake or lose edits.
8. **Weak debugging** — little provenance, no "why recomputed", no diff of evaluations.
9. **No first-class real-world data fusion** beyond CityEngine's attribute linking.
10. **Determinism not specified** (cross-platform, cross-thread-count, GPU), which is
    unacceptable for a test-and-evaluation tool.

---

## 3. Design principles for a 2026 green-field procedural core

P1. **One pure, typed, higher-order IR**; node graph, textual DSL, grammar sugar and host-language
    builders are front-ends to it.
P2. **Demand-driven, incremental evaluation** with verifying + constructive traces and early
    cutoff (Salsa/Shake/Bazel lineage), at graph *and* keyed-collection granularity.
P3. **Stable hierarchical identities** for every generated element, composed from parent ID,
    persistent op-site ID and the most stable available discriminator; shared with the B-rep
    kernel's topological naming.
P4. **Keyed counter-based randomness** with named streams; specified determinism levels (D1+D2
    for decisions).
P5. **Content-addressed, tiered caching** with immutable, structurally shared values.
P6. **Declared locality** for every op; hierarchical coarse-to-fine generation; ownership by
    anchor; tile-size-independent results.
P7. **Generate-on-query** through lazy proxies in the spatial index; LOD selected by per-purpose
    error metrics.
P8. **Design records separate decisions from geometry**; materializers produce any LOD vector
    from the same record.
P9. **Override layers** keyed by stable IDs with fallback locators, resolved/re-anchored/orphaned
    reporting; edits on inputs preferred where possible.
P10. **Provenance and introspection** are first-class (per-element provenance, recompute
    explanations, evaluation diffs, deterministic replay).
P11. **Real-world data fusion** as a first-class layer (precedence surveyed > inferred >
    procedural), with inputs content-hashed.
P12. **Algorithm/schedule separation**: evaluation policies (CPU/GPU, cache tier, granularity,
    interpolation, budget) never change semantics beyond declared tolerance.
P13. **Interchange, not lock-in**: export to OpenUSD/CityGML/CityJSON/glTF; consume GIS formats;
    keep the internal model free of any host application's object model.

---

## 4. Proposed architecture

```
+------------------------------------------------------------------------------------+
|  Front-ends: node editor | textual DSL (canonical) | grammar sugar | Python builder |
|              LLM/agent tooling (same textual IR + validator)                        |
+------------------------------------------------------------------------------------+
|  Document (source of truth, persistent data structure, text-serialized)             |
|    graphs (IR modules, versioned ops) | parameters & seeds | input-data refs (hashes)|
|    override layers (ordered) | scenario definitions | baked/frozen promotions        |
+------------------------------------------------------------------------------------+
|  Compiler: type/unit check, locality check, op-version pinning, fusion of field      |
|            expressions into kernels, schedule/policy application                     |
+------------------------------------------------------------------------------------+
|  Query engine (demand-driven, incremental)                                          |
|    queries = (output, region|ray|frustum, LOD-metric, layers, scenario)              |
|    memo: verifying traces + early cutoff; keyed-collection incrementality            |
|    hierarchy scheduler: levels, cells, halos, ownership-by-anchor                   |
|    identity service: stable IDs, keyed RNG streams                                  |
|    override resolver: apply / re-anchor / orphan report                             |
+------------------------------------------------------------------------------------+
|  Executors: task graph (work stealing) | SoA kernels (SIMD/JIT) | GPU | farm workers |
+------------------------------------------------------------------------------------+
|  Value store: immutable values w/ structural sharing | tiered CAS (RAM/disk/remote)  |
+------------------------------------------------------------------------------------+
|  Libraries: B-rep kernel ops (non-manifold, holes, embedded curves) | curve library  |
|    (clothoids/spiros → B-splines) | fields/noise | scattering | grammars | solvers   |
|    | asset factories (buildings, vegetation, roads, water, infrastructure)          |
+------------------------------------------------------------------------------------+
|  Spatial index with lazy proxy nodes (refine-on-hit) | attribute & semantic queries  |
+------------------------------------------------------------------------------------+
|  Outputs: sensor-sim adapters (acoustic, optical), OpenUSD, CityGML/CityJSON, glTF,  |
|           ground-truth labels, provenance reports, evaluation diffs                  |
+------------------------------------------------------------------------------------+
```

### 4.1 IR sketch

- **Types**: scalars with units, vectors, curves, B-rep bodies/sheets/wires, fields
  (`Field<T>` = function over a domain), keyed collections (`Coll<K, V>`), design records
  (struct/bundle types), closures, region/LOD descriptors.
- **Core constructs**: let/apply, `map/filter/group_by/join` over keyed collections, `fold`,
  `repeat(n)`, `iterate_until(pred, max)`, recursive rule application (grammar derivation over
  trees), `cond/switch` (lazy), `with_seed_stream(name)`, `override_point(collection)`,
  `materialize(record, lod)`.
- **Op signature metadata**: purity, locality class + radius, identity discriminator, LOD
  behaviour, op version, cost hints, GPU eligibility.
- **Canonical text form**: compact, deterministic ordering, stable node IDs, line-diffable;
  designed so that both humans and LLM agents can write it and get precise validation errors.

### 4.2 Evaluation flow for an edit

1. User changes the storey override of building `B` in override layer `L2`.
2. Document version increments; only the override input for key `B` changed (low durability).
3. Query engine re-verifies queries depending on `B`'s override: `design_record[B]` recomputes;
   its hash changes; materializations of `B` at LODs in use recompute; the tile aggregate for
   `B`'s cell updates; the spatial-index node for `B` is replaced; dependent consumers (e.g. an
   acoustic reflection set near `B`) are invalidated.
4. Nothing else recomputes. Undo restores the prior document version; all prior results are
   still in the CAS and are reused instantly.

### 4.3 Language and implementation choices (recommendation, not decision)

- Core engine in a systems language with strong safety and parallelism (Rust or modern C++);
  Python bindings for authoring, scripting, notebooks and agents.
- Extension ops: native plug-ins or WebAssembly modules with declared purity and version.
- GPU via a portable compute layer (e.g. Vulkan/WebGPU compute or CUDA where available),
  restricted to ops flagged as GPU-eligible with documented determinism level.

### 4.4 Phased plan (for a future `docs/plans/` document)

1. **IR + query engine prototype** on simple values (scalars, polylines, point sets): demand-
   driven memoization, early cutoff, keyed collections, stable IDs, keyed RNG, determinism tests
   (thread-count and platform invariance).
2. **Hierarchy scheduler** with locality declarations, halos, ownership-by-anchor; prove tile-
   size independence on a scatter + road-clearing example.
3. **Integrate the B-rep kernel**: ops on non-manifold B-rep with persistent face/edge naming
   shared with procedural IDs.
4. **Design records + materializers** for buildings (LOD0–3, interiors later), trees, roads;
   LOD-consistency tests.
5. **Override layers** with resolution/re-anchoring/orphan reporting; editing UI hooks.
6. **GIS ingestion and fusion** (DEM, OSM, cadastre, LiDAR) as content-hashed inputs.
7. **Generate-on-query** via lazy proxies in the spatial index; sensor-driven LOD metrics;
   acoustic/optical adapter prototypes.
8. Front-ends: textual DSL, node editor, Python builder, LLM-agent tooling.

---

## 5. Risks and open problems

- **Memo overhead** at element granularity (millions of keys): needs adaptive batching and
  careful memory layout; measure early.
- **Hashing cost** for large geometry: rely on input-addressed keys plus lazily computed content
  hashes; benchmark.
- **Non-local solvers** (road growth, WFC, layout annealing) conflict with locality and stable
  identity; require warm-starting from previous solutions plus stability penalties, and bounded
  re-solve regions.
- **Cross-platform floating-point determinism** for B-rep operations (booleans, intersections)
  is hard; may need exact/robust predicates and tolerance-aware canonicalization of results.
- **Override re-anchoring** heuristics can mis-target; must be reviewable and conservative.
- **Usability** of a functional IR for artists: the node editor and grammar sugar must hide
  closures/collections behind familiar idioms (Blender's experience is relevant).

---

## References

See the per-topic files for full references. Key sources:
- Build Systems à la Carte: https://www.microsoft.com/en-us/research/uploads/prod/2020/04/build-systems-jfp.pdf
- Salsa / durable incrementality: https://rust-analyzer.github.io/blog/2023/07/24/durable-incrementality.html
- Adapton: https://people.mpi-sws.org/~hammer/adapton-pldi2014.pdf
- Blender bundles and closures: https://code.blender.org/2025/08/bundles-and-closures/
- SideFX APEX: https://sidefx.com/docs/houdini/character/kinefx/apexgraphs.html
- Epic PCG generation modes: https://dev.epicgames.com/documentation/unreal-engine/using-pcg-generation-modes-in-unreal-engine
- Schwarz & Müller 2015 (CGA++): https://history.siggraph.org/?p=112748
- Lipp et al. 2011 (layers, anchored assignments): https://diglib.eg.org/handle/10.1111/v30i2pp345-354
- Infinigen Indoors: https://arxiv.org/abs/2406.11824
- HZD GPU placement: https://www.guerrilla-games.com/media/News/Files/GDC2017_VanMuijden_GPUBasedProceduralPlacementInHorizonZeroDawn.pdf
- Random123: https://www.thesalmons.org/john/random123/releases/latest/docs/
- OpenUSD glossary: https://openusd.org/release/glossary.html
