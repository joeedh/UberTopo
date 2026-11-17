# Survey: domain-specific generators and game world generation

Part of the [procedural-systems research set](README.md). This file looks at terrain tools,
vegetation tools, research generators (Infinigen), constraint-based tile systems (WFC,
Townscaper), and game world generators (Minecraft, Hytale, No Man's Sky, Horizon Zero Dawn,
Far Cry 5). The focus is on their *system architecture* — evaluation, partitioning,
determinism, identity, edits — not the generation algorithms themselves (those are covered by
the environment-generation research set).

---

## 1. Terrain tools: World Machine, Gaea, World Creator

- **World Machine** ([site](https://www.world-machine.com/)): node graph of heightfield
  operators (noise, erosion, selectors, filters). Devices evaluate on a 2D grid; **tiled builds**
  render a large world as overlapping tiles and blend at seams.
- **Gaea** (QuadSpinner; Gaea 2 since 2024, [site](https://quadspinner.com/)): similar graph
  model with stronger erosion and simulation nodes, GPU acceleration, and a tiled "build swarm".
- **World Creator** ([site](https://www.world-creator.com/)): GPU-real-time terrain with layered
  (stack-based rather than graph-based) editing and GIS heightmap import.

Architectural lessons:
1. **2.5D heightfields cannot represent overhangs, caves, bridges, or cliffs with undercuts**.
   UberTopo needs volumetric/B-rep representations for those; heightfields remain an excellent
   *coarse layer* and acceleration structure.
2. **Erosion is non-local**. A hydraulic erosion pass at tile *i* depends on water flowing in
   from neighbours, so tiling requires overlaps (aprons) and the result *depends on tile size*.
   This is a general lesson: any global operator breaks naive partitioning. Either compute it at
   a coarse global level and refine locally, or accept tile-size-dependent results and pin the
   tile scheme as part of the "seed".
3. Layer-stack UIs (World Creator) are more approachable than graphs for terrain but are a
   restricted graph (a linear chain with masks).

---

## 2. SpeedTree

SpeedTree ([site](https://speedtree.com/)) models a plant as a **generator hierarchy**: trunk
generator → branch generators → leaf/frond generators, each with parameters as curves along
the parent (e.g. branch length vs. position on trunk). LODs are first-class (continuous LOD
from full geometry to billboard), and wind is authored per level. Users can hand-edit
individual branches; *(unverified)* those edits are kept as per-node overrides on the
generated structure, so regeneration with new parameters preserves hand edits as long as the
parent structure exists — a domain-specific answer to the override problem.

Lessons: hierarchical generators with parameters expressed *relative to the parent* produce
stable, LOD-friendly structure; LOD is designed in, not decimated after the fact. Unreal 5.7's
experimental Procedural Vegetation Editor re-implements this idea on the PCG graph
([Digital Production](https://digitalproduction.com/2025/11/12/unreal-engine-5-7-foliage-pcg-and-in-editor-ai/)).

---

## 3. Infinigen (Princeton) — highly relevant

### 3.1 What it is
- **Infinigen** (CVPR 2023) generates photorealistic *natural* scenes — terrain, plants,
  creatures, rocks, water, weather — entirely from randomized mathematical rules, with no
  external assets, for synthetic computer-vision training data
  ([paper](https://openaccess.thecvf.com/content/CVPR2023/html/Raistrick_Infinite_Photorealistic_Worlds_Using_Procedural_Generation_CVPR_2023_paper.html),
  [arXiv 2306.09310](https://arxiv.org/abs/2306.09310)). BSD licensed. Code at infinigen.org.
- **Infinigen Indoors** (CVPR 2024) adds procedural architecture, furniture, appliances and
  clutter, and a **constraint language + solver**: users declare scene-composition constraints
  in a Python DSL; the solver runs simulated annealing hierarchically — floor plan, then large
  furniture, then small objects
  ([paper](https://openaccess.thecvf.com/content/CVPR2024/html/Raistrick_Infinigen_Indoors_Photorealistic_Indoor_Scenes_using_Procedural_Generation_CVPR_2024_paper.html),
  [arXiv 2406.11824](https://arxiv.org/abs/2406.11824)).
- **OcMesher** (3DV 2025): terrain is a procedural SDF; meshing uses a **camera-aware octree**
  built from a whole range of camera views (high resolution near surfaces visible from any
  camera, low elsewhere), then dual contouring — one mesh, view-consistent, no flicker,
  exportable to external simulators ([arXiv 2312.08364](https://arxiv.org/abs/2312.08364),
  [GitHub](https://github.com/princeton-vl/OcMesher)). Earlier Infinigen re-meshed per camera.

### 3.2 Architecture (from paper + code knowledge)
- Python on top of Blender. Assets are **factories** (`AssetFactory` subclasses) constructed with
  a `factory_seed`; `create_asset(i)` makes instance *i*. Seeds are derived by hashing
  (factory seed, instance index, names) and scoped with a `FixedSeed` context manager — a
  pragmatic, hash-based stable-seed scheme.
- Shapes and materials are mostly **Blender Geometry Nodes and shader node trees built from
  Python**; a "node transpiler" converts hand-built node trees into Python code so they can be
  versioned and parameterized *(knowledge; see repository docs)*.
- Scene generation is a **staged batch pipeline** (coarse layout → populate → fine terrain →
  render → ground truth), each stage a separate process run per scene, often on a cluster.
- Ground truth (depth, normals, segmentation, instance IDs, flow, occlusion) is a first-class
  output — directly analogous to what drone-detection testing needs (ground-truth positions,
  material labels, visibility).

### 3.3 Critique
- Slow (minutes to hours per scene), single-scene batch model; no incremental update and no
  interactive editing loop.
- Depends on Blender's Python API and data model; geometry is meshes without CAD topology.
- Scenes are bounded around a camera trajectory; unbounded, multi-km worlds with streaming
  and stable geography are not the goal (the OcMesher follow-up notes mesh-size limits for
  long trajectories, [arXiv 2509.13306](https://arxiv.org/pdf/2509.13306v3)).
- But: it is the strongest open-source evidence that a **library of seeded, parameterized asset
  factories + a declarative constraint layer for arrangement** is a productive architecture
  for realistic natural and indoor content. UberTopo should study its asset coverage and
  constraint DSL closely and consider interoperability (e.g. import Infinigen assets as
  leaf-level generators).

---

## 4. Wave Function Collapse, model synthesis, Townscaper

- **Model synthesis** (Merrell 2007, [site](https://paulmerrell.org/model-synthesis/)) and
  **WaveFunctionCollapse** (Gumin 2016, [GitHub](https://github.com/mxgmn/WaveFunctionCollapse))
  generate tile/voxel arrangements satisfying local adjacency constraints learned from an example
  or authored, via constraint propagation with random choice (lowest entropy first) and
  backtracking/restarts.
- **Townscaper** (Oskar Stålberg, 2020): irregular quad grid (hexagonal grid with random edge
  removal, then subdivided into quads), marching-cubes-like module selection on corners, and
  WFC-style constraint solving with **deterministic** choices so that a click only changes the
  local neighbourhood ([Game Developer](https://www.gamedeveloper.com/game-platforms/how-townscaper-works-a-story-four-games-in-the-making)).
- **Editable WFC** (Boris the Brave): standard WFC is *not* edit-stable — changing one cell can
  change distant cells, because choices are made in a global order. Making it editable requires
  locality (solve in chunks, keep previous solution as a soft constraint, re-solve only a
  neighbourhood) ([article](https://www.boristhebrave.com/2022/04/25/editable-wfc/),
  [tips](https://boristhebrave.com/2020/02/08/wave-function-collapse-tips-and-tricks)).

Lesson for UberTopo: **constraint solvers are inherently non-local**, which is at odds with
incremental re-evaluation and stable identity. A solver node must declare a bounded region of
influence, or be run with the previous solution as warm start and a stability penalty, or both.
The same applies to Infinigen Indoors' annealing and to any layout optimization (road graph
relaxation, parcel subdivision).

---

## 5. MagicaVoxel

MagicaVoxel ([site](https://ephtracy.github.io/)) is a voxel editor whose procedural tools are
GPU shaders run over the voxel volume. Relevant only as a reminder that a voxel-shader model
gives trivially parallel, deterministic generation at the cost of resolution; not a candidate
architecture for multi-LOD CAD-grade environments.

---

## 6. Minecraft world generation (1.18+)

- Terrain shape is defined by **density functions**: a data-driven DAG of pure functions of
  `(x, y, z)` (noise samplers, arithmetic, splines, clamps, `y_clamped_gradient`, etc.), authored
  as JSON in data packs ([density function](https://minecraft.wiki/w/Density_function),
  [custom world generation tutorial](https://minecraft.wiki/w/Tutorial%3ACustom_world_generation)).
- The **noise router** names the top-level functions that drive generation:
  `final_density`, `initial_density_without_jaggedness`, aquifer and ore-vein functions,
  biome-selection climate parameters (temperature, humidity, continentalness, erosion,
  weirdness, depth) ([noise router](https://minecraft.wiki/w/Noise_router)).
- **Explicit caching markers inside the function graph**: `flat_cache` and `cache_2d` (evaluate
  once per column), `cache_once` (once per position per evaluation), `interpolated` (evaluate
  on a coarse grid and trilinearly interpolate), `cache_all_in_cell`. This is a clean idea:
  *the author annotates evaluation strategy without changing semantics*.
- **Chunk status pipeline**: each 16×16 chunk advances through stages (structure starts,
  structure references, biomes, noise, surface, carvers, features, light, spawn, full). Each
  stage has a declared **dependency radius** on neighbouring chunks' earlier stages, e.g.
  features ("decoration") may write into a 3×3 chunk neighbourhood, which is why features are
  limited in size. Structures (villages, strongholds) are placed by a coarser, seed-driven grid
  (`structure_sets` with spacing/separation) so that their existence can be computed from any
  chunk without generating neighbours *(knowledge; see vanilla data and wiki)*.
- **Determinism**: world seed + positional random factories (Xoroshiro128++ seeded from
  hashed coordinates) make every chunk reproducible independent of generation order.
- **Edits**: player modifications are stored as the chunk data itself once generated; regen of
  a modified chunk is not supported (generated chunks become authoritative).

Lessons: (1) a pure-function DAG evaluated at arbitrary positions is the simplest possible
"generate-on-query" model and parallelizes perfectly; (2) staged generation with declared
neighbour radii is a battle-tested solution for cross-tile dependencies; (3) "coarse placement
grid computable from anywhere" solves large-structure placement without global passes.

## 7. Hytale World Gen V2

Hytale (early access January 2026) ships World Gen V2, a node-editor-based, designer-facing
generator with **live reload in-game**, density-based terrain and material concepts, and a
modding API; community plugins add WFC nodes
([GamesRadar](https://www.gamesradar.com/games/survival/this-new-approach-to-world-generation-will-redefine-the-block-game-genre-hytale-lead-declares-fundamental-shift-in-how-artists-and-game-designers-tackle-minecraft-style-worlds),
[official worldgen tutorial mirror](https://hytalemodding.dev/en/docs/official-documentation/worldgen/worldgen-tutorial/density-generation-concepts)).
Noted limitations include no noise visualization in the editor and node priority determined by
canvas position — i.e. *debuggability and explicit ordering* are the weak points, as usual.

## 8. No Man's Sky

GDC 2017 "Continuous World Generation in No Man's Sky" (Innes McKendrick) describes a pipeline
from **voxel generation → polygonization → texturing → population/simulation**, run
continuously around the player so that space-to-ground transitions never load
([GDC Vault](https://gdcvault.com/play/1024265/)). Everything derives from seeds; the universe
is never stored. Player terrain edits are stored as **deltas** and re-applied on top of the
regenerated terrain (with a cap on stored edits) *(knowledge)*. Sean Murray's "Building Worlds
Using Math(s)" covers the noise design ([GDC Vault](https://gdcvault.com/play/1024514/)).

Lesson: "source of truth = seed + rules + edit deltas" scales without bound, but edit deltas
must be addressed in a coordinate system that survives rule changes — NMS's game updates have
famously shifted terrain under player bases, a vivid demonstration of the
override-anchoring problem.

## 9. Horizon Zero Dawn — GPU run-time placement

Guerrilla's system (GDC 2017, Jaap van Muijden) moved from CPU placement to **GPU, density-based,
real-time placement** around the player: artists author placement logic in a graph editor;
world data lives as 2D maps (~4 MB/km²); ~250 instances per frame are placed while moving,
across 500+ asset types. Design goals: **fully data-driven, deterministic, locally stable**
([slides PDF](https://www.guerrilla-games.com/media/News/Files/GDC2017_VanMuijden_GPUBasedProceduralPlacementInHorizonZeroDawn.pdf),
[article](https://www.guerrilla-games.com/read/gpu-based-procedural-placement-in-horizon-zero-dawn)).

"Locally stable" is exactly the property UberTopo needs: changing a map in one area must not
change placements elsewhere. They achieve it by making placement a pure function of position
and local map values (dithered density thresholds evaluated per candidate position).

## 10. Far Cry 5 — Houdini-driven world pipeline

Ubisoft Montreal (GDC 2018, Etienne Carrier) generated biomes, terrain texturing, freshwater
networks, cliffs, power lines and fog maps for ~100 km² using Houdini Engine inside the Dunia
editor, re-running tools as designers edited the world daily
([GDC Vault](https://gdcvault.com/play/1025557/Procedural-World-Generation-of-Far),
[SideFX talk page](https://www.sidefx.com/learn/talks/procedural-world-generation-far-cry-5/)).
Lessons: procedural tools in production are run **incrementally per region in response to
hand edits** (roads, POIs placed by designers are *inputs*, and procedural layers react to
them); this "hand-authored skeleton, procedural flesh" layering is the dominant successful
pattern in industry.

---

## 11. Synthesis for UberTopo

| Pattern | Seen in | Use in UberTopo |
|---|---|---|
| Pure positional function DAG + cache annotations | Minecraft, HZD, Substance | Fields / density layers; generate-on-query |
| Staged chunk pipeline with dependency radii | Minecraft, NMS | Tile/LOD scheduler for partitioned generation |
| Coarse-to-fine hierarchical grids | UE PCG HiGen, Minecraft structure sets | Global → regional → local generation |
| Seeded asset factories | Infinigen, SpeedTree | Leaf-level asset generators keyed by stable IDs |
| Declarative constraints + solver | Infinigen Indoors, WFC | Interior layout, site layout; must be locality-bounded |
| Camera/sensor-aware meshing | OcMesher | Sensor-driven LOD for acoustic/optical simulation |
| Hand skeleton + procedural flesh | Far Cry 5, CityEngine + GIS | GIS/hand-authored inputs drive procedural layers |
| Edit deltas over regenerated content | NMS, Lipp et al. | Override layer anchored by stable semantic IDs |

---

## References

- World Machine: https://www.world-machine.com/
- QuadSpinner Gaea: https://quadspinner.com/
- World Creator: https://www.world-creator.com/
- SpeedTree: https://speedtree.com/
- Digital Production, UE 5.7 (Procedural Vegetation Editor): https://digitalproduction.com/2025/11/12/unreal-engine-5-7-foliage-pcg-and-in-editor-ai/
- Raistrick et al., Infinite Photorealistic Worlds Using Procedural Generation (CVPR 2023): https://openaccess.thecvf.com/content/CVPR2023/html/Raistrick_Infinite_Photorealistic_Worlds_Using_Procedural_Generation_CVPR_2023_paper.html
- arXiv 2306.09310: https://arxiv.org/abs/2306.09310
- Raistrick et al., Infinigen Indoors (CVPR 2024): https://openaccess.thecvf.com/content/CVPR2024/html/Raistrick_Infinigen_Indoors_Photorealistic_Indoor_Scenes_using_Procedural_Generation_CVPR_2024_paper.html
- arXiv 2406.11824: https://arxiv.org/abs/2406.11824
- Ma et al., OcMesher, arXiv 2312.08364: https://arxiv.org/abs/2312.08364 ; code: https://github.com/princeton-vl/OcMesher
- Follow-up on unbounded meshing limits, arXiv 2509.13306: https://arxiv.org/pdf/2509.13306v3
- Merrell, Model synthesis: https://paulmerrell.org/model-synthesis/
- Gumin, WaveFunctionCollapse: https://github.com/mxgmn/WaveFunctionCollapse
- Game Developer, How Townscaper works: https://www.gamedeveloper.com/game-platforms/how-townscaper-works-a-story-four-games-in-the-making
- Boris the Brave, Editable WFC: https://www.boristhebrave.com/2022/04/25/editable-wfc/
- Boris the Brave, WFC tips and tricks: https://boristhebrave.com/2020/02/08/wave-function-collapse-tips-and-tricks
- MagicaVoxel: https://ephtracy.github.io/
- Minecraft Wiki, Density function: https://minecraft.wiki/w/Density_function
- Minecraft Wiki, Noise router: https://minecraft.wiki/w/Noise_router
- Minecraft Wiki, Custom world generation tutorial: https://minecraft.wiki/w/Tutorial%3ACustom_world_generation
- GamesRadar on Hytale World Gen V2: https://www.gamesradar.com/games/survival/this-new-approach-to-world-generation-will-redefine-the-block-game-genre-hytale-lead-declares-fundamental-shift-in-how-artists-and-game-designers-tackle-minecraft-style-worlds
- Hytale worldgen docs (mirror): https://hytalemodding.dev/en/docs/official-documentation/worldgen/worldgen-tutorial/density-generation-concepts
- GDC Vault, Continuous World Generation in No Man's Sky: https://gdcvault.com/play/1024265/
- GDC Vault, Building Worlds Using Math(s): https://gdcvault.com/play/1024514/
- Van Muijden, GPU-Based Procedural Placement in Horizon Zero Dawn (slides): https://www.guerrilla-games.com/media/News/Files/GDC2017_VanMuijden_GPUBasedProceduralPlacementInHorizonZeroDawn.pdf
- GDC Vault, Procedural World Generation of Far Cry 5: https://gdcvault.com/play/1025557/Procedural-World-Generation-of-Far
- SideFX, Far Cry 5 talk: https://www.sidefx.com/learn/talks/procedural-world-generation-far-cry-5/
