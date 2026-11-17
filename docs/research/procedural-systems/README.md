# Procedural modeling systems: research overview

This folder surveys procedural modeling *systems* and their core engines: architecture,
evaluation models, determinism, identity, scale and LOD. It ends with a critique and a
recommended architecture for UberTopo's procedural core. Specific generation algorithms
(roads, buildings, terrain, vegetation), B-rep data structures, CAD kernels, curves, and
attributes/spatial queries are covered by other research sets under `docs/research/`.

Research method: web search (late 2026) plus prior knowledge. Some primary pages
(`developer.blender.org`, `code.blender.org`) could not be fetched from this session, and the
web-search budget ran out partway through, so a few references were added from memory. Each
file marks these with † or *(unverified)*. Check them before citing.

## Files

| File | Contents |
|---|---|
| [survey-dcc-and-engine-systems.md](survey-dcc-and-engine-systems.md) | Houdini (SOPs, VEX, cooking, compiled blocks, PDG/TOPs, APEX, Solaris, Engine), Blender Geometry Nodes (fields, lazy-function evaluation, anonymous attributes, instances, zones, bundles/closures/lists), CityEngine/CGA, Unreal PCG (partitioned/hierarchical/runtime/GPU), Unity, Grasshopper/Dynamo/Sverchok, Substance Designer, OpenUSD/OpenExec/Omniverse; comparison table |
| [survey-domain-and-game-systems.md](survey-domain-and-game-systems.md) | World Machine/Gaea/World Creator, SpeedTree, **Infinigen** (+Indoors, OcMesher), WFC/Townscaper/editable WFC, MagicaVoxel, Minecraft density functions and noise router, Hytale V2, No Man's Sky, Horizon Zero Dawn GPU placement, Far Cry 5 |
| [academic-foundations.md](academic-foundations.md) | Shape grammars, split grammars, CGA, CGA++, Lipp locators and layered edits, G²/interconnected structures, GML on B-reps, L-systems and open L-systems, Parish & Müller, surveys, inverse procedural modeling, neural shape-program synthesis, LLM-driven procedural generation through 2026 |
| [evaluation-engine-design.md](evaluation-engine-design.md) | Graph vs grammar vs script vs functional IR; lazy/eager/demand-driven; build systems à la carte; Adapton, Salsa, differential dataflow; element-level incrementality; caching; parallelism; iteration; embedding vs DSL; debugging; undo; persistence |
| [determinism-and-identity.md](determinism-and-identity.md) | Determinism levels; floating-point rules; counter-based RNGs (Philox, Squares) with named streams; stable hierarchical IDs; link to CAD topological naming; override taxonomy and override layers; instancing identity |
| [large-world-and-lod.md](large-world-and-lod.md) | Op locality classes; hierarchical coarse-to-fine generation; ownership by anchor; tile-size independence; generate-on-query with lazy proxies; streaming; CityGML 2.0/3.0 and refined LODs; LOD consistency rules; design records and materializers; sensor-driven LOD metrics |
| [critique-and-recommendations.md](critique-and-recommendations.md) | Legacy limitations per system; cross-cutting failures; 13 design principles; proposed layered architecture; IR sketch; edit-evaluation walkthrough; phased plan; risks |

## Key findings

1. **Every production system invalidates per node and identifies elements by index.** Editing
   one building recomputes all buildings, and inserting one element reshuffles randomness and
   breaks downstream edits. Systems that reach large worlds add a second, coarser system on top
   (Houdini PDG, Unreal PCG partitions, CityEngine per-lot runs), each with its own caching and
   identity.
2. **The field is converging on a typed, pure, functional IR under a graph UI.** Blender went
   from fields to zones to closures, bundles and lists. Houdini added APEX (graphs as values,
   partial evaluation). CGA++ made shapes and rules first-class. UberTopo should start from
   there instead of arriving at it by accident.
3. **Build-system and incremental-computation research applies directly.** Useful sources are
   Build Systems à la Carte, Salsa (red-green, early cutoff, durability), Adapton and
   differential dataflow. Procedural graphs need demand-driven scheduling with dynamic
   dependencies, early cutoff, and memoization per key over collections, not just per node.
4. **Stable identity is the foundation.** Caching, controlled variation, overrides, LOD
   consistency and ground truth all depend on it. Use hierarchical IDs: parent ID, persistent
   op-site ID, and the most stable discriminator available (external GIS ID, semantic key,
   world-anchored lattice cell, quantized position, and index only as a last resort). Share this
   naming with the B-rep kernel, because the CAD topological naming problem is the same problem.
5. **Randomness must be keyed, not sequential.** Use a counter-based RNG (Philox, Squares, or a
   strong hash) keyed by world seed, node, named stream and element ID. Adding a new random
   attribute then does not disturb existing content.
6. **Overrides belong in USD-style layers keyed by stable IDs.** Each override keeps a fallback
   locator (Lipp et al.'s anchored assignments and meta-locators) and is reported as resolved,
   re-anchored or orphaned. Where an edit can be made on the inputs instead (exclusion zones,
   attributes), do that.
7. **For large worlds, every op declares how far its effects reach.** Ops are pointwise,
   bounded-radius or global. Global ops run at coarse hierarchy levels and pass results down.
   Each element belongs to one tile, chosen by its anchor point, and results must not depend on
   tile size. Unreal HiGen, Minecraft chunk stages and Horizon Zero Dawn's locally stable
   placement are the precedents.
8. **Generate-on-query through lazy proxies in the spatial index.** A ray or visibility query
   only refines what it hits, at the LOD its error metric requires. Acoustic, optical and
   line-of-sight consumers need different LODs of the same world.
9. **Multi-LOD needs design records separate from geometry.** Each decision is made once, at
   the coarsest level that needs it, and materializers produce any LOD from the same record.
   CityGML 3.0 (LOD4 removed, interior and exterior LODs independent, semantics at every LOD)
   and Biljecki's refined LODs support using a vector of LODs rather than one number.
10. **Infinigen is the closest open precedent for realistic natural and indoor content.** It
    combines seeded asset factories, a constraint DSL with a solver, and ground-truth outputs.
    Its architecture (batch, Blender-bound) is not one to copy, but its asset coverage and
    constraint layer are worth studying or interoperating with.
11. **LLMs work best emitting a compact, typed, validated procedural IR,** combined with
    solvers (Holodeck, Proc3D, Procedura). Raw host scripting such as bpy works poorly. A
    well-designed canonical text IR makes UberTopo agent-friendly at little extra cost.

## Recommendation (summary)

Build the procedural core as: a canonical typed functional IR, a demand-driven incremental
query engine, stable IDs, keyed RNG, a tiered content-addressed cache, a hierarchical locality
scheduler, design records with LOD materializers, override layers, and per-element provenance.
It runs on top of the non-manifold B-rep kernel and a spatial index with lazy nodes, and exports
to OpenUSD and CityGML/CityJSON. Details and a phased plan are in
[critique-and-recommendations.md](critique-and-recommendations.md).

## Open questions for the project owner

- **Determinism contract:** is bit-exact reproducibility across machines and OSes required for
  test fixtures, or is reproducibility within tolerance enough? This choice drives
  floating-point policy and GPU use.
- **Interactive vs batch priority:** will the system mostly generate scenario batches (favouring
  throughput, farm execution and a remote cache) or support interactive editing of a site
  (favouring latency, incrementality and editor UX)?
- **Real-data fidelity:** for surveyed sites, how much must match reality (footprints only, or
  facades and interiors from surveys or BIM)? This sets the priority of inverse procedural
  modeling and BIM import.
- **Consumers:** which sensor simulators (acoustic propagation, optical rendering, possibly
  RF/radar) will consume the output, and in which formats (USD, glTF, a custom format)? Their
  error metrics define the LOD system.
- **Implementation language and licensing:** Rust or C++ core? Are GPL components (for example
  Blender or Infinigen code) acceptable for reuse or interoperability?
- **Authoring audience:** will procedural rules be written mainly by engineers (text DSL or
  Python), artists (node editor) or LLM agents? This decides which front-end comes first.
