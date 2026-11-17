# Survey: general-purpose procedural systems (DCC tools and game engines)

Part of the [procedural-systems research set](README.md). This file covers the general-purpose
procedural *systems*: their data models, evaluation models and architectural strengths and
weaknesses. Domain-specific generators (terrain tools, SpeedTree, Infinigen, game world
generators) are in [survey-domain-and-game-systems.md](survey-domain-and-game-systems.md).

Research note: web search was available, but direct page fetches to some hosts (e.g.
`developer.blender.org`, `code.blender.org`) were blocked in this session, so some details come
from search-result excerpts plus prior knowledge. Claims that could not be verified against a
primary source are marked *(unverified)*.

---

## 1. SideFX Houdini

Houdini is the de-facto reference for node-based procedural modeling in film and games. It is
also the clearest example of a system whose layered history shows through.

### 1.1 Geometry model (SOPs)

- A geometry ("detail", `GU_Detail`) holds **points**, **vertices**, **primitives** and a single
  **detail** record. Points are positions that may be shared; vertices are per-primitive
  references to points (the "corner" of a face); primitives are polygons, polylines, NURBS/Bézier
  patches and curves, volumes, VDBs, packed primitives, etc.
  ([attributes docs](https://www.sidefx.com/docs/houdini/model/attributes.html),
  [HDK geometry intro](https://sidefx.com/docs/hdk/_h_d_k__geometry__intro.html)).
- **Attributes** are named, typed arrays on each of the four classes. Lookup precedence is
  vertex → point → primitive → detail. Intrinsic attributes (e.g. `intrinsic:pointcount`,
  packed transforms) are computed on demand.
- **Groups** are named index sets on points/primitives/edges/vertices.
- There is **no persistent half-edge or boundary-representation topology**: adjacency is
  recomputed by individual nodes when they need it. There is no first-class concept of a face
  with holes; holes are faked with "hole" flags on polygons or with bridged cuts. NURBS
  primitives exist but trimmed-surface modelling is weak and nothing resembles a CAD-grade B-rep.
- **Packed primitives** (and packed disk primitives / Alembic / USD packed prims) are the
  instancing mechanism: one primitive references shared geometry plus a transform.

### 1.2 Evaluation: cooking

- A network of nodes is **cooked** on demand when a viewer or output requests a node
  ([cooking basics](https://www.sidefx.com/docs/houdini/basics/cooking.html)). Each SOP's output
  is cached in memory. Changing a parameter marks the node and everything downstream **dirty**;
  the next demand recooks dirty nodes. This is demand-driven, node-granular invalidation with
  no early cutoff on unchanged outputs (a recooked node that produces identical geometry still
  dirties downstream nodes) *(standard behavior; no official doc found stating it)*.
- Parameters are evaluated through an expression layer: legacy **HScript** expressions,
  **Python** expressions, and channel references (`ch()`, `chs()`, `op:` paths). These create
  *implicit* dependencies between arbitrary nodes in the scene. Time dependency is a global flag
  that propagates through these references; forum threads show recurring confusion about nodes
  recooking every frame unnecessarily
  ([forum example](https://www.sidefx.com/forum/topic/75482/)).
- Granularity is **one node over all elements**. Editing a parameter that affects one building
  recooks the node for all buildings. Per-piece work uses **For-Each loops**, which re-execute
  their body per piece.
- **Compiled blocks** wrap a sub-network so it can run as a single verb chain without the
  per-node overhead; their main purpose is multithreaded For-Each loops. Restrictions: parameters
  are "left behind", stop conditions are unsupported, and blocks must be cleanly nested at
  Begin/End boundaries ([compiled blocks](https://www.sidefx.com/docs/houdini/model/compile.html),
  [looping](https://www.sidefx.com/docs/houdini/model/looping.html)). This is effectively a
  second evaluation mode bolted onto the first.
- **SOP verbs** (the node algorithm separated from the node instance) exist since H16 and make
  nodes callable from Python and compiled blocks.

### 1.3 VEX

VEX is a C-like, SIMD-friendly shading/geometry language that runs a kernel per element
(point, primitive, vertex, detail, or volume voxel) inside Attribute Wrangle / VOP nodes. It is
the main reason Houdini is fast in practice: the node graph is coarse, the kernels are tight.
Its random functions (`rand`, `random`) are hash-based and are usually seeded with `@ptnum`,
which ties randomness to element *index*, so inserting or deleting an element re-randomizes
everything after it (see [determinism-and-identity.md](determinism-and-identity.md)).

### 1.4 PDG / TOPs

PDG ("procedural dependency graph") is a separate evaluation library exposed as the TOP
network context ([TOPs intro](https://www.sidefx.com/docs/houdini/tops/intro.html),
[HDK TOP intro](https://www.sidefx.com/docs/hdk/_h_d_k__op_basics__top_intro.html)). TOP nodes
generate **work items** (each a task with attributes and dependencies), schedule them locally
or on a farm, and track results as **files**. Caching is file-based: a work item is "clean" if
its expected outputs exist and nothing upstream changed; Cache Mode settings control whether
existing files are trusted ([cache state](https://www.sidefx.com/docs/houdini/tops/pdg/workItemCacheState.html),
[forum on cache semantics](https://www.sidefx.com/forum/post/316144)). PDG is how studios do
"wedging" (parameter sweeps) and large-world tiling (e.g. per-tile terrain, per-building
generation).

Observation: PDG is a coarse **build system** (cf. Make/Bazel) layered *on top of* the SOP
cook model, with its own identity (work-item index/name), its own caching (files on disk) and
its own scheduling. The two systems do not share an invalidation model.

### 1.5 APEX and KineFX

APEX (Houdini 20+) is a newer **graph evaluation framework**: graphs are *data* (stored as
geometry), assembled by SOPs, and evaluated separately and on demand by an invoke node.
It supports **partial evaluation** — only requested outputs are computed. Houdini 20.5 tightened
graph rules so evaluation is "well-defined, self-contained and non-ambiguous", breaking older
graphs ([APEX graphs](https://sidefx.com/docs/houdini/character/kinefx/apexgraphs.html),
[20.5 updates](https://www.sidefx.com/docs/houdini/character/kinefx/apexupdates_H20.5.html)).
APEX is aimed at rigging, but it is architecturally interesting: it is SideFX's own admission
that a graph-as-value, pure, partially evaluable IR is the modern way to do it.

### 1.6 Solaris / LOPs

LOPs build an **OpenUSD stage** through a node network (each LOP adds or edits layers).
Houdini therefore contains at least four evaluation models: SOP cooking (with compiled blocks),
DOP simulation, PDG work items, APEX graphs, plus LOP/USD composition.

### 1.7 Houdini Engine

Houdini Engine exposes HDAs (digital assets) to host apps (Unreal, Unity, Maya, 3ds Max,
proprietary editors) through a C API ([HAPI](https://www.sidefx.com/docs/hengine/)). The host
sends inputs, the Engine session cooks, the host receives geometry. Used heavily in games
(e.g. Far Cry 5, see the domain survey). Limitations: session-based (out-of-process or in-process
Houdini), licence-gated, coarse-grained (whole HDA recook), and the host must re-import outputs,
which breaks any identity or hand edits on the host side unless the integration adds its own
bookkeeping.

### 1.8 Assessment

Strengths: enormous operator library, VEX, attribute-centric data model, artists' trust,
mature farm tooling.
Legacy limitations: no topology/B-rep kernel; index-based identity; node-granular invalidation
with no early cutoff; implicit expression dependencies; four or five evaluation models that
don't share caching or identity; in-memory single-scene model; file-based caching for scale.

---

## 2. Blender Geometry Nodes

### 2.1 Data model

- Geometry is a **GeometrySet**: a bag of components (mesh, point cloud, curves, instances,
  volume, Grease Pencil in 4.3+), each with named attributes on its domains (mesh: point, edge,
  face, face corner; curves: point, spline; instances: instance).
- **Instances** are a component holding references (to geometry, objects or collections) plus
  transforms and per-instance attributes; "Realize Instances" flattens them. Instances can nest.
- Mesh faces are n-gons **without holes**; topology is Blender's `Mesh` arrays (not BMesh,
  which is used only in edit mode).

### 2.2 Fields

Fields (Blender 3.0) are **lazily evaluated functions over a geometry domain** — a node graph
fragment that computes a value per element given a context (geometry + domain). Field inputs
like *Position*, *Index*, *Normal* read the context; a field is only evaluated when a node that
consumes geometry asks for it (e.g. *Set Position* evaluates its *Offset* field on the points
it is processing) ([fields manual](https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/fields.html)).
Internally, fields are compiled to **multi-functions**, which evaluate in batches over index
masks (vectorized, multithreaded). *Capture Attribute* materializes a field into an
**anonymous attribute** — a reference-counted, unnamed attribute that propagates through
downstream operations and is deleted when no longer referenced. This is a genuinely good idea:
it is a typed, garbage-collected way to pass per-element data through topology-changing
operations without polluting the attribute namespace.

### 2.3 Evaluation: lazy-function graphs

Since 2022, each node group is converted into a **lazy-function graph** before evaluation
([new evaluation system commit](https://developer.blender.org/rB4130f1e674f)). Lazy functions
request only the inputs they need (e.g. *Switch* evaluates one branch), and the executor
starts single-threaded and switches to multithreading only when it judges the work large enough
("lazy threading", [commit](https://developer.blender.org/rB5c81d3bd469)). Zones (repeat,
simulation, for-each) each get their own lazy-function graph
([PR #109029](https://projects.blender.org/blender/blender/pulls/109029),
[PR #112421](https://projects.blender.org/blender/blender/pulls/112421)).

Important limitation: the evaluator is **not incremental across edits**. Geometry Nodes is a
modifier; when anything the modifier depends on changes, the dependency graph (depsgraph, at
object/ID granularity) re-runs the modifier and the node tree is re-evaluated from its inputs
(laziness prunes unused branches but there is no persistent per-node memoization between
evaluations; caching is explicit via **Bake** nodes and simulation caches) *(based on knowledge
of 3.x–4.x; no official statement found for 5.x)*.

### 2.4 2023–2026 redesign milestones

- **Repeat zone** (4.0) and **simulation zone** (3.6): structured loops instead of node-graph
  recursion.
- **For Each Element zone** (4.3): parallel per-element iteration
  ([4.3 manual](https://docs.blender.org/manual/de/4.3/modeling/geometry_nodes/utilities/for_each_geometry_zone.html),
  [80.lv](https://80.lv/articles/blender-4-3-s-for-each-element-node-zone-upgrade/)). Gizmos on
  node groups and packed bakes also landed in 4.3.
- **Bundles and closures** (5.0): bundles are struct-like values that can hold any socket type
  including geometry and fields; a **Closure zone** creates a function value and *Evaluate
  Closure* calls it, matching sockets by name. Captured values are read-only; closures cannot
  reach attributes outside their evaluation context; when a closure is evaluated in multiple
  places, socket-type inference currently picks one "basically at random"
  ([5.0 notes](https://developer.blender.org/docs/release_notes/5.0/geometry_nodes/),
  [bundles & closures blog](https://code.blender.org/2025/08/bundles-and-closures/),
  [Evaluate Closure manual](https://docs.blender.org/manual/en/5.0/render/shader_nodes/utilities/evaluate_closure.html)).
- **New socket shapes** (5.0) distinguishing single values, fields, volume grids and (planned)
  lists ([socket shapes blog](https://code.blender.org/2025/08/new-socket-shapes/)).
- **Lists** and Geometry Bundles appear in 5.2 LTS (July 2026), along with an experimental XPBD
  physics solver node ([CG Channel on 5.2](https://www.cgchannel.com/2026/07/blender-5-2-lts-is-here-discover-its-5-key-features/))
  *(list semantics unverified against official notes)*.

The trajectory is clear: Geometry Nodes is evolving from a "visual modifier stack" into a
**typed functional language** (first-class functions, structs, lists, structured loops). That
is the right direction and validates a functional IR for UberTopo.

### 2.5 Assessment

Strengths: fields + anonymous attributes are the best per-element abstraction among DCC tools;
lazy evaluation; clean functional trajectory; open source (GPL) with readable code
(`source/blender/functions`, `source/blender/nodes`).
Limitations: no incremental recomputation; tied to Blender's object/ID model and depsgraph;
mesh without holes or B-rep; index-based identity unless the user maintains an `id` attribute;
single-process, in-memory; no partitioned/streaming generation; GPL makes embedding
impossible for proprietary integration.

---

## 3. Esri CityEngine (CGA)

- **CGA** (Computer Generated Architecture) is the shape grammar from Müller et al. 2006
  ([paper](https://dl.acm.org/doi/10.1145/1141911.1141931)), productized by Procedural Inc. and
  acquired by Esri in 2011. A **shape** has a scope (oriented box), geometry, and attributes;
  rules rewrite shapes using operations such as `extrude`, `comp` (component split into faces),
  `split` (along an axis with absolute/relative/repeat sizes), `offset`, `roofGable`, `i()`
  (insert asset), `setback`, and occlusion queries (`inside`, `overlaps`, `touches`). Rules may
  be stochastic (`p` percentages) and conditional.
- The result of a derivation is a **shape tree**; leaves are output geometry. Attributes can be
  linked to GIS feature attributes (height, usage, roof type), which is CityEngine's killer
  feature: city models driven by real parcel/footprint data.
- Rules compile to **rule packages (RPK)** run by the **Procedural Runtime (PRT)**, embeddable in
  ArcGIS Pro, Unreal (Vitruvio), Rhino, Houdini (Palladio), and Python (PyPRT)
  ([PyPRT 1.12](https://community.esri.com/t5/arcgis-cityengine-blog/new-release-of-pyprt-1-12-with-support-for-arcgis/ba-p/1684666),
  [rule package doc](https://doc.arcgis.com/en/cityengine/2024.1/help/help-rule-package.htm)).
  2025.1 added facade components to **Visual CGA** (node editor)
  ([what's new 2025.1](https://www.esri.com/arcgis-blog/products/city-engine/3d-gis/whats-new-in-arcgis-cityengine-2025-1)).
- Street networks are generated by a growth algorithm descended from Parish & Müller 2001, or
  imported from OSM; blocks and lots are subdivided; rules run per lot.

Assessment. CGA is still the best existing *language* for building massing and facades, and
its "rules attached to GIS features" workflow is precisely what UberTopo needs for real sites.
Limitations: rules see essentially one shape at a time (context-sensitivity is limited to
occlusion queries — CGA++ addressed this in research, see
[academic-foundations.md](academic-foundations.md)); no global constraints; poor for natural
environments; exterior-centric (interiors are awkward); the generated model is a mesh soup with
semantic tags rather than a topological B-rep; local edits to generated buildings are lost on
regeneration unless done through attribute overrides on the input shape; proprietary runtime.

---

## 4. Unreal Engine PCG framework

- **Data model**: PCG graphs pass collections of typed **PCG data**: *point data* (each point has
  transform, bounds, density, color, steepness, **seed**, plus **metadata** attributes), *spatial
  data* (landscape, splines, volumes, primitives, textures) that can be sampled into points,
  *attribute sets* (tables), and since 5.4–5.5 dynamic meshes and more. In 5.6 the default point
  container became a **structure-of-arrays** (`UPCGPointArrayData`) for memory and speed
  ([API](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/PCG/UPCGPointArrayData));
  users reported regressions in Blueprint-heavy graphs that triggered conversions
  ([forum](https://forums.unrealengine.com/t/pcg-on-ue-5-6-is-slow/2689091)).
- **Execution**: a graph is attached to a **PCG component** on an actor; the component's
  bounds define the generation domain. Generation can be on-demand in editor, at load, or at
  runtime. **Partitioned generation** splits the component's domain on a grid aligned with
  World Partition; **hierarchical generation (HiGen)** lets different parts of one graph run on
  different grid sizes (coarse grids for big structures, fine for grass), passing data down
  ([generation modes](https://dev.epicgames.com/documentation/unreal-engine/using-pcg-generation-modes-in-unreal-engine)).
  **Runtime generation** generates and cleans up content around "generation sources" (players,
  cameras) within frame-time budgets.
- **GPU**: compute-graph nodes (5.5+) execute on GPU; 5.6 added GPU sampling of landscape RVT,
  GPU hierarchical generation passing data to smaller grids, and lazy readbacks
  ([GPU processing](https://dev.epicgames.com/documentation/unreal-engine/using-pcg-with-gpu-processing-in-unreal-engine)).
- **Status**: declared Production-Ready in 5.7 (Nov 2025) with a PCG Editor Mode and an
  experimental PCG-based Procedural Vegetation Editor
  ([Epic 5.7 announcement](https://unrealengine.com/en-US/news/unreal-engine-5-7-is-now-available),
  [Digital Production](https://digitalproduction.com/2025/11/12/unreal-engine-5-7-foliage-pcg-and-in-editor-ai/)).
- **Determinism**: each point carries a seed derived from its position when sampled
  *(knowledge; the helper is `ComputeSeedFromPosition` in the PCG plugin)*, combined with
  component and node seeds. Position-based seeding is stable under insertion/deletion of other
  points but changes when the point itself moves.
- Cross-partition coherence is an open user complaint: content that spans partitions needs
  to be generated at a coarser grid and passed down
  ([forum](https://forums.unrealengine.com/t/cross-partition-coherent-generation-in-pcg-with-hierarchical-runtime-generation/2728364)).

Assessment. PCG is the most modern *production* design for **large-world, partitioned,
streaming, budgeted** generation, and HiGen is a direct precedent for UberTopo's tiled
hierarchical generation. Limitations: point-cloud-centric data model (great for scattering,
weak for topology and CAD); outputs are engine actors/ISM instances, so identity and edits are
UObject-centric; node-granular invalidation; hand edits to generated actors are lost on
regeneration (the remedy is exclusion volumes, tags, and data-driven overrides); tied to Unreal.

---

## 5. Unity

Unity has no first-party general procedural geometry framework. **Graph Toolkit**
(experimental, Unity 6.2+) is a framework for *building* graph editors, explicitly without a
runtime execution backend ([docs](https://docs.unity3d.com/Packages/com.unity.graphtoolkit@0.1/manual/introduction.html),
[announcement](https://discussions.unity.com/t/unity-s-graph-toolkit-experimental-available-today-in-unity-6-2/1664909)).
Procedural work relies on Houdini Engine for Unity, ProBuilder, terrain tools, and third-party
packages (MapMagic 2, Gaia, PCG Graph). Lesson: an engine without a native procedural core
ends up with a fragmented ecosystem; nothing to copy architecturally.

---

## 6. Grasshopper (Rhino), Dynamo, Sverchok

### Grasshopper
- **Eager, push-based dataflow**: changing an input *expires* all downstream components, which
  then recompute in full when the solution runs. Most components run single-threaded.
- **Data trees**: every wire carries a tree (paths → lists). Matching rules (longest list,
  cross reference) and path manipulation (graft, flatten, simplify) are powerful but a notorious
  usability problem. Grasshopper 2 (beta in Rhino 9 WIP) keeps the tree topology but adds
  per-item metadata ([GH2 beta](https://discourse.mcneel.com/t/rhino-beta-feature-grasshopper-2-beta/221336)).
- Geometry is Rhino's OpenNURBS (NURBS/B-rep, robust but rooted in 1990s Rhino 1.0 design).
- "Baking" copies results into the Rhino document; the link is broken and edits do not flow
  back. Rhino.Inside.Revit adds element tracking.
- A community thread "If Grasshopper was rebuilt for 2026, what should change?" is a useful
  catalogue of pain points
  ([thread](https://discourse.mcneel.com/t/if-grasshopper-was-rebuilt-for-2026-what-should-change-or-stay/211626)).

### Dynamo
- Dataflow front-end over **DesignScript** (an associative language with replication guides).
- **Element binding / trace**: Revit-creating nodes remember which elements they created, so a
  re-run *updates* those elements instead of creating duplicates
  ([primer](https://primer2.dynamobim.org/7_dynamo_for_revit/4-creating),
  [AU class](https://www.autodesk.com/autodesk-university/class/Getting-Good-Stuff-Out-Pandoras-Box-Element-Binding-Dynamo-Revit-2019)).
  This is an important precedent for **stable identity across re-evaluation**: identity is
  keyed by node + replication index (callsite trace), stored with the host document. It breaks
  when list order changes — the same index-identity weakness as Houdini.

### Sverchok
Blender add-on ([GitHub](https://github.com/nortikin/sverchok)) predating Geometry Nodes:
Python, eager list-based dataflow, rich CAD-ish nodes (including FreeCAD integration). Shows
that list-of-lists dataflow in Python scales poorly; mostly of historical interest.

---

## 7. Substance 3D Designer

A node graph for 2D textures (heightmaps, PBR channels) evaluated on GPU or CPU. Relevant
ideas:
- **Function graphs** are a typed, pure per-pixel expression language embedded inside nodes
  (Pixel Processor, FX-Map) — analogous to VEX inside SOPs, or fields inside Geometry Nodes.
- **Relative output size**: nodes inherit resolution from parent/input, so a graph is
  resolution-independent and can be re-evaluated at a different "LOD". This is a cheap, robust
  form of LOD-aware procedural content.
- Global and per-node **random seed** parameters; graphs are deterministic given a seed.
- `.sbsar` packaged graphs run in host engines via the Substance engine (analogous to RPK/HDA).
Limitations: 2D only; no incremental per-tile recomputation; whole-image granularity.

---

## 8. OpenUSD and NVIDIA Omniverse (composition layer)

- **USD** is a scene description with a **composition engine**: layers hold *opinions*;
  composition arcs (sublayers, references, payloads, inherits, variants, specializes) are
  resolved by strength order (LIVRPS) into a composed stage
  ([glossary](https://openusd.org/release/glossary.html)). **Payloads** allow deferred
  loading; **variant sets** encode switchable alternatives (often LODs); **native instancing**
  shares prototypes.
- USD's **layered, non-destructive override model** is the best existing solution to "hand edits
  that survive regeneration": a generator writes a weak layer, users write a stronger layer,
  and overrides are addressed by **prim path** (a stable, human-meaningful name, not an index).
- USD is *not* a procedural engine. **OpenExec** (preview in OpenUSD 25.08) adds a computation
  framework: it builds a computation graph from a stage, evaluates requested outputs on demand,
  caches results and invalidates them on input change; first use is rigging
  ([25.08 release](https://forum.aousd.org/t/openusd-v25-08-released/2619),
  [26.03](https://aousd.org/blog/openusd-v26-03/)).
- **Omniverse** builds on USD with live multi-client layer sync (Nucleus), RTX rendering,
  and sensor simulation (Isaac Sim — relevant to drone/sensor simulation pipelines)
  ([Omniverse](https://developer.nvidia.com/omniverse)).

Assessment. USD should be an **interchange and composition target** for UberTopo (sensor
simulators and renderers consume it), and its opinion/layer model is the right *conceptual*
model for overrides. It should not be the internal geometry or evaluation model: USD's mesh
schema has no topology, no B-rep, no non-manifold structure, and composition cost grows with
prim count (millions of generated elements as prims is expensive).

---

## 9. Cross-cutting comparison

| System | Paradigm | Granularity of recompute | Identity | Scale model | Geometry |
|---|---|---|---|---|---|
| Houdini SOPs | demand-driven DAG, dirty flags | node × all elements | point/prim index | in-memory; PDG for tiles | polygon soup + attrs |
| Houdini PDG | build system, work items | work item (file) | work-item index/name | farm, files | n/a |
| Blender GN | lazy functional DAG + fields | whole tree per object update | index, optional `id` attr | in-memory | mesh (no holes) |
| CityEngine CGA | shape grammar | per initial shape (lot) | shape-tree path (internal) | per-lot parallel | mesh + semantics |
| Unreal PCG | dataflow over point sets | component / partition cell | position-derived seed; actors | partitioned, hierarchical, runtime | points + spatial data |
| Grasshopper | eager push dataflow | downstream of change, whole | tree path + index | in-memory | NURBS/B-rep |
| Dynamo | associative dataflow | downstream of change | trace (node + index) | in-memory | Revit/ASM |
| Substance | GPU image DAG | node × whole image | n/a | single texture | 2D |
| USD/OpenExec | composition + demand-driven compute | prim / computation | prim path | payloads, instancing | scene graph |

The recurring pattern: **every production system uses node-level invalidation and index-based
identity**, and every one that scales to big worlds bolted a second, coarser system on top
(PDG, PCG partitions, CityEngine per-lot runs). See
[critique-and-recommendations.md](critique-and-recommendations.md).

---

## References

- SideFX, Houdini geometry attributes: https://www.sidefx.com/docs/houdini/model/attributes.html
- SideFX, HDK geometry introduction: https://sidefx.com/docs/hdk/_h_d_k__geometry__intro.html
- SideFX, Cooking: https://www.sidefx.com/docs/houdini/basics/cooking.html
- SideFX, Compiled blocks: https://www.sidefx.com/docs/houdini/model/compile.html
- SideFX, Looping: https://www.sidefx.com/docs/houdini/model/looping.html
- SideFX, TOPs intro: https://www.sidefx.com/docs/houdini/tops/intro.html
- SideFX, Work item cache state: https://www.sidefx.com/docs/houdini/tops/pdg/workItemCacheState.html
- SideFX, APEX graphs: https://sidefx.com/docs/houdini/character/kinefx/apexgraphs.html
- SideFX, APEX updates in 20.5: https://www.sidefx.com/docs/houdini/character/kinefx/apexupdates_H20.5.html
- SideFX, Houdini Engine: https://www.sidefx.com/docs/hengine/
- Blender manual, Fields: https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/fields.html
- Blender, new GN evaluation system commit: https://developer.blender.org/rB4130f1e674f
- Blender, lazy threading commit: https://developer.blender.org/rB5c81d3bd469
- Blender PR #109029 (zones in evaluator): https://projects.blender.org/blender/blender/pulls/109029
- Blender PR #112421 (lazy repeat zone): https://projects.blender.org/blender/blender/pulls/112421
- Blender 5.0 Geometry Nodes release notes: https://developer.blender.org/docs/release_notes/5.0/geometry_nodes/
- Blender, Bundles and Closures (2025): https://code.blender.org/2025/08/bundles-and-closures/
- Blender, New Socket Shapes (2025): https://code.blender.org/2025/08/new-socket-shapes/
- CG Channel, Blender 5.2 LTS: https://www.cgchannel.com/2026/07/blender-5-2-lts-is-here-discover-its-5-key-features/
- Müller et al., Procedural Modeling of Buildings (2006): https://dl.acm.org/doi/10.1145/1141911.1141931
- Esri, What's new in CityEngine 2025.1: https://www.esri.com/arcgis-blog/products/city-engine/3d-gis/whats-new-in-arcgis-cityengine-2025-1
- Esri, Rule package: https://doc.arcgis.com/en/cityengine/2024.1/help/help-rule-package.htm
- Esri, PyPRT: https://github.com/Esri/pyprt
- Epic, PCG generation modes: https://dev.epicgames.com/documentation/unreal-engine/using-pcg-generation-modes-in-unreal-engine
- Epic, PCG with GPU processing: https://dev.epicgames.com/documentation/unreal-engine/using-pcg-with-gpu-processing-in-unreal-engine
- Epic, UPCGPointArrayData API: https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/PCG/UPCGPointArrayData
- Epic, UE 5.7 announcement: https://unrealengine.com/en-US/news/unreal-engine-5-7-is-now-available
- Unreal forum, cross-partition coherence: https://forums.unrealengine.com/t/cross-partition-coherent-generation-in-pcg-with-hierarchical-runtime-generation/2728364
- Unity Graph Toolkit: https://docs.unity3d.com/Packages/com.unity.graphtoolkit@0.1/manual/introduction.html
- McNeel, Grasshopper 2 beta: https://discourse.mcneel.com/t/rhino-beta-feature-grasshopper-2-beta/221336
- Dynamo primer, creating elements: https://primer2.dynamobim.org/7_dynamo_for_revit/4-creating
- Sverchok: https://github.com/nortikin/sverchok
- OpenUSD glossary: https://openusd.org/release/glossary.html
- OpenUSD 25.08 (OpenExec preview): https://forum.aousd.org/t/openusd-v25-08-released/2619
- AOUSD, OpenUSD 26.03: https://aousd.org/blog/openusd-v26-03/
- NVIDIA Omniverse: https://developer.nvidia.com/omniverse
