# Large worlds, partitioned generation and multi-LOD semantics

Part of the [procedural-systems research set](README.md). This file covers how procedural
engines scale to large areas (tiling, hierarchy, streaming, generate-on-query) and how one
entity can be generated consistently at many levels of detail (LOD), including CityGML's LOD
concepts and LOD-aware procedural rules.

---

## 1. Scale and purpose

Plausible UberTopo workloads:
- A **site survey** area of 1–25 km²: real terrain (DEM/LiDAR), real road network and building
  footprints (OSM, cadastre), procedurally completed facades, interiors where needed,
  vegetation and street furniture. 10⁴–10⁵ buildings, 10⁶–10⁸ trees/shrubs, 10⁵ road segments.
- A **sensor trial** localized around sensor arrays (microphones, cameras) and drone
  trajectories: high detail within hundreds of metres of sensors and flight paths, coarse detail
  elsewhere for occlusion and far-field reflections.
- **Monte-Carlo batches**: hundreds of environment variants, each generated, simulated and
  discarded — throughput and cache reuse between variants dominate.

The required detail depends on the **consumer**, not on the object: acoustic propagation cares
about surfaces large relative to the wavelength (drone rotor noise spans roughly 100 Hz–several
kHz, i.e. wavelengths ~3.4 m down to a few cm), material absorption/impedance, diffracting edges
and ground cover; optical cameras care about screen-space error and appearance; line-of-sight
vulnerability analysis cares about occluder silhouettes and openings. LOD must therefore be
**selected per query by an error metric**, not baked per object.

---

## 2. Partitioning strategies in existing systems

| System | Partitioning | Cross-partition dependencies |
|---|---|---|
| World Machine / Gaea | fixed tiles with overlap, blended seams | erosion is non-local; results depend on tile size |
| Houdini PDG | user-defined work items (tiles, buildings) | user wires inter-tile data via files |
| UE PCG | grid partitions aligned to World Partition; HiGen grid levels; runtime gen around sources | coarse grids pass data down to finer grids; cross-partition coherence is a known user pain point ([forum](https://forums.unrealengine.com/t/cross-partition-coherent-generation-in-pcg-with-hierarchical-runtime-generation/2728364)) |
| Minecraft | 16×16 chunks advancing through status stages | each stage declares a neighbour radius; structures placed on a coarse seed grid computable from anywhere |
| No Man's Sky | continuous voxel regions around player | pure functions of position + seed |
| HZD | GPU placement in tiles around player | placement is a pure function of position and 2D maps (locally stable) |
| Infinigen + OcMesher | per camera trajectory; camera-aware octree | whole scene is batch-generated |

### 2.1 A taxonomy of operations by locality

1. **Pointwise** — value at x depends only on inputs at x (density/noise functions, mask
   lookups, Minecraft density functions). Trivially partitionable, generate-on-query friendly.
2. **Bounded radius r** — depends on inputs within distance r (scatter with minimum spacing,
   blur, local erosion steps, clearing vegetation near a road, feature placement in a 3×3 chunk
   neighbourhood). Partitionable with a **halo/apron of width r**; results identical regardless
   of tile size *if* defined on world-anchored lattices and keyed RNG.
3. **Global / unbounded** — hydrology and flow accumulation, road-network growth, city block
   layout, constraint solving (WFC, annealing), long-range erosion. Not partitionable as-is.

The engine should require every op to **declare its locality class and radius**, enforce it
(an op only receives the halo it declared), and refuse to run unbounded ops inside a tile
context.

### 2.2 Hierarchical (coarse-to-fine) generation

Global ops are made tractable by running them at a **coarse level of a spatial hierarchy** and
passing results down — UE PCG's HiGen, Minecraft's structure sets, and every studio pipeline
("hand-authored skeleton, procedural flesh"; see the Far Cry 5 notes in the
[domain survey](survey-domain-and-game-systems.md)) follow this pattern:

```
Level 0 (region, ~10 km):   macro terrain, river network, arterial roads, land-use zones
Level 1 (district, ~1 km):  local roads, blocks, parcels, forest stands, water bodies
Level 2 (tile, ~100 m):     buildings (design records), individual trees, street furniture
Level 3 (on demand):        facade/interior geometry, branch/leaf geometry, fine terrain
```

Rules:
- **Data flows down only.** A level-k result may depend on level ≤k results within declared
  halos, never on finer levels. (If a fine-level result must influence a coarse decision —
  e.g. building heights affecting a skyline constraint — compute a summary at the coarse level
  from coarse data, or iterate explicitly.)
- **Ownership by anchor**: each element belongs to exactly one cell (the cell containing its
  anchor point, e.g. footprint centroid or tree base), even if its geometry crosses cell
  borders. Neighbouring cells see it through the halo. No duplicates, no seams.
- **Tile-size independence**: cells are a *scheduling* device. Element existence and properties
  are defined with world-anchored lattices and keyed RNG (see
  [determinism-and-identity.md](determinism-and-identity.md)), so changing cell size changes
  performance, not output. Global ops are bound to a hierarchy *level*, not to a cell size.
- **Real-world data** enters at the level matching its scale: national DEM and OSM roads at
  level 0–1, cadastral parcels and footprints at level 1–2, LiDAR-derived tree positions at
  level 2. Procedural completion fills only what data does not provide (fusion rules decide
  precedence: surveyed > inferred > procedural).

### 2.3 Generate-on-query and lazy spatial structure

For huge areas the world should never be fully generated. Requests are
`(region or ray/frustum, error metric/LOD, layers, purpose)`.

- Each level produces, cheaply, **conservative bounding proxies** for content not yet refined
  (building envelope from footprint and max height; tree crown bounds from species and height
  distribution; forest stand volume). These go into the spatial index.
- A spatial query (ray cast for acoustic/optical propagation, visibility, frustum) that hits a
  proxy whose error exceeds the query's tolerance **triggers refinement** of that element only —
  the same idea as RenderMan's deferred "procedural primitives" with declared bounds and ray
  tracing APIs' procedural/intersection-shader geometry, applied at the level of the procedural
  engine.
- Refined results are cached by (element ID, LOD, op versions), so subsequent queries and other
  simulation runs reuse them.
- The bounds contract is strict: a generator must never produce geometry outside its declared
  proxy (checked in debug builds), or queries will miss content.

This ties the procedural core to the geometry query layer covered by the attributes and spatial
queries research set: the spatial index must support **lazy nodes** that expand on demand.

### 2.4 Streaming, budgets and memory

- Caches are partitioned by (cell, level, LOD) with cost-aware eviction to a disk CAS.
- Prefetch along known trajectories (scripted drone paths, sensor placements, cameras).
- Interactive editing uses time budgets per frame (UE PCG budgets of a few ms at runtime
  [forum](https://forums.unrealengine.com/t/pcg-on-5-6-is-slow/2689091)) with progressive
  refinement; batch scenario generation uses all cores and the farm.
- Camera/sensor-aware meshing of implicit content (OcMesher's multi-view octree,
  [arXiv 2312.08364](https://arxiv.org/abs/2312.08364)) generalizes to **sensor-aware**
  refinement: resolution proportional to the strictest consumer's error metric at each location,
  over the whole trial, producing one consistent mesh for all frames (no temporal popping that
  would corrupt detection-algorithm tests).

---

## 3. Multi-LOD semantics

### 3.1 CityGML LOD concepts

- **CityGML 2.0** (2012): LOD0 (footprint/roof-edge polygons, 2.5D), LOD1 (block model, flat
  roofs), LOD2 (roof shapes, thematic surfaces: wall/roof/ground), LOD3 (architectural detail:
  openings, balconies), LOD4 (interiors).
- **CityGML 3.0** (OGC standard 2021): **LOD4 removed**; LOD0–3 remain; **interior and exterior
  can be modelled at different LODs** (e.g. LOD1 shell with LOD2 interior); **thematic surface
  semantics allowed at every LOD**; core model reorganized around **Space** and **Space
  Boundary**, easing mapping to IndoorGML
  ([OGC CityGML 3.0 conceptual model](https://docs.ogc.org/is/20-010/20-010.html) †,
  [Kutzner, Chaturvedi, Kolbe 2020](https://doi.org/10.1007/s41064-020-00095-z) †,
  [GIM International](https://www.gim-international.com/content/news/citygml-3-0-conceptual-model-approved-as-official-ogc-standard)).
- **Refined LODs** (Biljecki, Ledoux, Stoter 2016): each LOD split into four sub-levels LODx.0–
  x.3 (e.g. LOD1.2 = block with smaller building parts; LOD2.2 = roof superstructures such as
  dormers), because the coarse LODs leave too much room for interpretation; supported by CityJSON
  ([Biljecki et al. 2016](https://doi.org/10.1016/j.compenvurbsys.2016.04.005) †,
  [CityJSON spec](https://www.cityjson.org/specs/) †).

Lessons:
1. A single scalar LOD is insufficient. Use a **LOD vector**: exterior geometry, interior
   geometry, semantic granularity, appearance/material detail — each independently selectable
   (CityGML 3.0 moved this way for interior/exterior).
2. **Semantics must exist at every LOD** (a wall at LOD1 is still a wall with a material and
   acoustic properties) — CityGML 3.0 agrees.
3. **Height references matter**: an LOD1 block's height can be eave, ridge, median roof, etc.;
   Biljecki's work shows large differences in derived quantities. The design record should store
   all reference heights so that LOD1 can be produced for each convention.
4. Use **Space / Space Boundary** as the semantic backbone for both buildings and other
   environments (a forest stand, a culvert, a tunnel are spaces with boundaries) — this aligns
   with the non-manifold B-rep requirement (cells and shared boundary faces).

### 3.2 Consistency requirements across LODs

1. **Identity**: the same element ID at every LOD (see the determinism file).
2. **Decision consistency**: every discrete decision (roof type, number of floors, use, window
   rhythm, species, lane count) is made **once**, at the coarsest level where it is needed, and
   inherited by finer LODs. Never re-decided per LOD — that is how LOD popping and inconsistent
   ground truth arise.
3. **Geometric conformance**: LOD n+1 approximates LOD n within a declared tolerance (same
   footprint; same reference heights; roof volume within tolerance), and coarser LODs are valid
   low-passes of finer ones (for terrain: finer LODs add band-limited detail octaves on top of
   the coarse surface).
4. **Monotone refinement**: finer LODs add, never contradict (windows at LOD3 sit where the LOD2
   facade texture/attributes say the bays are).
5. **Interface consistency between neighbours at different LODs**: building ground-floor
   elevation vs terrain, road surface vs terrain, bridge decks vs abutments — anchored by
   shared design values decided at coarse level, so mixed-LOD scenes have no gaps or
   interpenetrations beyond tolerance.

### 3.3 Design records and LOD materialization

Recommended pattern: separate **decisions** from **geometry**.

- A generator first produces a compact **design record** per entity — all discrete choices and
  key parameters, with stable sub-element IDs — e.g. building: footprint ref, storeys, storey
  heights, roof type and pitch, facade layouts (bay widths, opening types per bay per floor),
  materials, interior program. Design records are cheap (bytes to kilobytes), so they can be
  generated for every entity in a region at once and cached.
- **Materializers** turn a design record into geometry at a requested LOD vector:
  `materialize(record, lod) → B-rep + attributes`. LOD1 extrudes the footprint to the chosen
  reference height; LOD2 builds the roof; LOD3 cuts openings per the facade layout; interior LODs
  build rooms from the program. All read the *same* record.
- Grammar-style rules become **LOD-aware** by declaring where they materialize: a facade split
  rule *always* runs (it is cheap and decides bays), but only emits opening geometry when
  `lod.exterior ≥ 3`; below that, its decisions are recorded as attributes (bay positions,
  window-to-wall ratio) that LOD2 uses for appearance and acoustic surface properties (glass vs
  masonry fractions).
- The same pattern applies beyond buildings:

| Entity | Design record | LOD0 | LOD1 | LOD2 | LOD3 |
|---|---|---|---|---|---|
| Building | storeys, roof, facade layout, materials, program | footprint + attrs | block | roof + typed surfaces | openings, balconies; interiors per interior-LOD |
| Tree | species, height, crown dims, trunk dia., health | point + attrs | crown proxy (ellipsoid/cylinder) | branch skeleton | leaves/needles |
| Forest stand | boundary, species mix, density, age classes | polygon + stats | canopy volume | individual tree records | per-tree LODs |
| Road | centerline curve, cross-section template, lanes | centerline | carriageway polygon | lanes, curbs, sidewalks, profile | markings, drains, furniture |
| Water body | boundary, depth profile, flow | polygon/line | surface | banks, bed | vegetation, structures |
| Terrain | coarse DEM + detail generator params | coarse grid | refined grid | local detail, cut/fill for roads | micro-relief, rocks |

- Road cross-sections and centerlines are curve-based (clothoids/spiros per the requirements),
  so road LODs are naturally parameterized by curve tessellation tolerance plus cross-section
  detail — consistent across LODs by construction.

### 3.4 LOD selection by error metric

LOD requests should come as an **error metric per purpose**, evaluated per element:
- optical: projected geometric error in pixels for a given camera set;
- acoustic: feature size relative to the wavelength band of interest and distance to
  sources/receivers; whether an element contributes first-order reflections or diffraction
  edges within the trial volume;
- LOS/vulnerability: silhouette error relative to the drone-detection geometry.

The engine maps the metric to an LOD vector per element and caches materializations per vector.
Different simulations can thus request different LODs of the same world in the same session,
while sharing design records and IDs (consistent ground truth across modalities).

---

## References

Entries marked † were supplied from memory after this session's web-search budget ran out;
verify before citing.

- Unreal forum, cross-partition coherent generation: https://forums.unrealengine.com/t/cross-partition-coherent-generation-in-pcg-with-hierarchical-runtime-generation/2728364
- Unreal forum, PCG 5.6 performance and frame budgets: https://forums.unrealengine.com/t/pcg-on-5-6-is-slow/2689091
- Epic, PCG generation modes (partitioned, hierarchical, runtime): https://dev.epicgames.com/documentation/unreal-engine/using-pcg-generation-modes-in-unreal-engine
- Minecraft Wiki, Noise router: https://minecraft.wiki/w/Noise_router
- Van Muijden, GPU-Based Procedural Placement in Horizon Zero Dawn: https://www.guerrilla-games.com/media/News/Files/GDC2017_VanMuijden_GPUBasedProceduralPlacementInHorizonZeroDawn.pdf
- Ma et al., View-Dependent Octree-based Mesh Extraction (OcMesher): https://arxiv.org/abs/2312.08364
- GIM International, CityGML 3.0 conceptual model approved: https://www.gim-international.com/content/news/citygml-3-0-conceptual-model-approved-as-official-ogc-standard
- OGC City Geography Markup Language (CityGML) 3.0 Conceptual Model: https://docs.ogc.org/is/20-010/20-010.html †
- Kutzner, Chaturvedi, Kolbe 2020, CityGML 3.0: New Functions Open Up New Applications: https://doi.org/10.1007/s41064-020-00095-z †
- Biljecki, Ledoux, Stoter 2016, An improved LOD specification for 3D building models: https://doi.org/10.1016/j.compenvurbsys.2016.04.005 †
- CityJSON specifications: https://www.cityjson.org/specs/ †
- MDPI ISPRS IJGI article on CityGML LOD (2025): https://www.mdpi.com/2220-9964/14/12/465
