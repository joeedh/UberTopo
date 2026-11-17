# Determinism, seeded randomness, stable identity and overrides

Part of the [procedural-systems research set](README.md). This file covers how a procedural
engine makes results reproducible, how generated elements get identities that survive
re-evaluation, and how hand-authored edits can survive regeneration — a known hard problem that
every existing system handles poorly.

Why this matters more for UberTopo than for games or VFX:
- **Test reproducibility.** A drone-detection trial (acoustic array, cameras, swarm trajectory)
  must be re-runnable bit-for-bit months later, on another machine, to compare algorithm
  versions. The environment is part of the test fixture.
- **Controlled variation.** Monte-Carlo studies vary one factor (tree density, building height
  distribution, season) while holding everything else fixed. That requires that changing one
  parameter does not re-randomize unrelated content.
- **Caching and incrementality** (see [evaluation-engine-design.md](evaluation-engine-design.md))
  are only correct if evaluation is a pure function of its inputs.
- **Ground truth.** Labels (which object is where, what material) must be consistent across
  LODs and sensor modalities, so the same element must have the same ID everywhere.

---

## 1. Levels of determinism

| Level | Meaning | Typical breakers |
|---|---|---|
| D0 | Same binary, same machine, same thread count | global RNG state, hash-map iteration order, uninitialized memory |
| D1 | Independent of thread count and scheduling | parallel reductions in completion order, atomic appends, work-stealing order |
| D2 | Across OS / CPU architecture / compiler | `-ffast-math`, FMA contraction, x87 vs SSE, libm `sin/exp/pow` differences, `std::` distribution implementations |
| D3 | Across GPU vendors/drivers | fused ops, denormal flushing, transcendental precision, non-deterministic atomics |
| D4 | Across engine versions | changed op behavior without versioning |

Recommended target: **D1 + D2 for all discrete decisions** (does a tree exist, which species,
which rule fired, topology), **D2 within tolerance for continuous geometry**, D3 only for paths
that are explicitly flagged "approximate/visual", and D4 through op versioning (old documents
pin op versions or are migrated explicitly).

Techniques:
- Compile the deterministic core without fast-math; make FMA usage explicit; avoid platform
  `libm` for decisions (use own polynomial approximations or correctly-rounded libraries such
  as CORE-MATH, [project](https://core-math.gitlabpages.inria.fr/) †).
- Never use `std::uniform_real_distribution` and friends (implementation-defined); map raw
  integer bits to floats with explicit formulas.
- **Quantize before branching**: decide placement by comparing integer hashes against
  integer-scaled thresholds, or quantize positions to a fixed grid before hashing.
- Canonical ordering of every collection by stable key; reductions in key order.
- Test: run each evaluation twice with different thread counts and shuffled task order, compare
  output hashes. Run the cross-platform CI comparison regularly.

---

## 2. Randomness

### 2.1 What legacy systems do

- **Sequential stateful generators** (`rand()`, Mersenne Twister, one global stream): the value
  you get depends on how many draws happened before. Adding one random draw anywhere shifts
  everything after it. This is the 1990s default and still common in scripts (Infinigen relies
  on Python/NumPy RNG state, scoped with a `FixedSeed` context manager plus hashing to limit the
  blast radius *(knowledge)*).
- **Hash of index** (Houdini `rand(@ptnum + seed)`, Blender *Random Value* defaulting to index
  when no `id` attribute exists): deterministic and parallel, but **index-keyed**, so deleting
  point 5 re-randomizes points 6..N.
- **Hash of position** (Unreal PCG point seeds, HZD placement): stable under unrelated insertions
  and deletions; changes if the element itself moves; coincident points collide.
- **Positional random factories** (Minecraft): world seed hashed with block/chunk coordinates
  ([density functions](https://minecraft.wiki/w/Density_function)).

### 2.2 Counter-based RNGs and hashes

A **counter-based RNG** is a keyed bijection `f_key(counter) → random bits`: no state, any value
computable independently and in parallel
([Wikipedia overview](https://en.wikipedia.org/wiki/Counter-based_random_number_generator)).

- **Philox** and **Threefry** (Salmon et al., SC11 "Parallel Random Numbers: As Easy as 1, 2, 3",
  Random123 library) — crush-resistant, used in CUDA cuRAND, JAX, NumPy
  ([Random123](https://www.thesalmons.org/john/random123/releases/latest/docs/)).
- **Squares** (Widynski 2020): middle-square based counter RNG, four rounds, roughly 2× Philox
  speed in the author's benchmarks ([arXiv 2004.06278](https://arxiv.org/abs/2004.06278)).
- Fast **integer hashes** for GPU/procedural use: PCG-hash, xxHash-style mixers, SplitMix64
  finalizer; Jarzynski & Olano evaluate quality/speed trade-offs
  ([JCGT 2020](https://jcgt.org/published/0009/03/02/) †).

### 2.3 Recommended scheme: keyed, named streams

```
bits = CBRNG( key = H(world_seed, generator_node_id, stream_name),
              counter = (element_stable_id, draw_index) )
```

- `generator_node_id` is the stable identity of the node/rule in the document (not its
  position in the graph), so rewiring the graph doesn't reseed everything.
- `stream_name` is a **named purpose** (`"species"`, `"height"`, `"lean"`). Adding a new random
  attribute uses a new stream and **does not disturb existing ones** — the key property for
  controlled variation ("add random moss without changing which trees exist").
- `element_stable_id` comes from the identity scheme below.
- `draw_index` only for variable-count draws (e.g. number of branches), and then each branch's
  own randomness should use the branch's stable ID, not the draw index.
- The user-facing "seed" parameter of a node is folded into the key; changing it reseeds only
  that node's streams.
- Scenario variation = changing `world_seed` or a node seed; reproducibility = recording them.

---

## 3. Stable identity of generated elements

### 3.1 Schemes in existing systems

| Scheme | Used by | Stable under | Breaks under |
|---|---|---|---|
| Element index | Houdini ptnum/primnum, Blender index, Grasshopper path+index | nothing structural | any insertion/deletion/reorder upstream |
| Call-site trace (node + replication index) | Dynamo element binding ([primer](https://primer2.dynamobim.org/7_dynamo_for_revit/4-creating)) | re-runs with same list shapes | list reordering, length change |
| Position hash | UE PCG seeds, HZD | unrelated edits elsewhere | moving the element; coincident elements |
| Derivation path (rule path) | CGA shape tree, Lipp "exact locators" | parameter changes that preserve tree shape | structural changes (an extra floor renumbers floors) |
| Semantic locator | Lipp 2008/2019 semantic/meta-locators | structural changes | ambiguity ("leftmost window" when two tie) |
| External feature ID | CityEngine + GIS attributes, OSM IDs | everything procedural | source data re-issued with new IDs |
| Persisted UUID | Revit UniqueId, USD prim paths authored by hand | everything | requires storage — not purely procedural |

### 3.2 Recommended scheme: hierarchical, discriminator-chosen IDs

Every generated element gets

```
id = H128(parent_id, op_site_id, discriminator)
```

with a human-readable **path** kept alongside for debugging (`site:A/parcel:osm-w123/bldg/
facade:south/floor:ground/bay:c+2/window`).

- `parent_id`: the element this one was derived from (building from parcel, window from facade).
  This makes IDs a derivation tree/DAG like CGA's shape tree.
- `op_site_id`: a persistent ID of the node/rule *in the document*, assigned at creation and
  stored, so it survives renames, moves and rewiring.
- `discriminator`: chosen per op as the **most stable available** key:
  1. an **external ID** (OSM way ID, cadastral parcel ID, survey point ID) when the element
     corresponds to input data;
  2. a **semantic key** relative to stable anchors (facade orientation, floor label from ground,
     bay index counted from a named anchor such as the facade centre or the entrance — so adding
     a bay at the far end does not renumber all bays);
  3. a **world-anchored lattice coordinate** for scattering: candidates are defined on a fixed
     world lattice (jittered grid / Poisson tile set), so a candidate's ID is its lattice cell and
     sub-index; changing a density mask then only adds/removes candidates, never renames
     surviving ones — HZD's "locally stable" property;
  4. **quantized local position** in the parent's frame;
  5. index — only as a last resort, and flagged in the UI as "unstable identity".

Ops must declare which discriminator they use; the engine can then warn when a downstream
override or cache depends on an unstable ID.

### 3.3 Identity across LODs

The same building must have the same ID at LOD0 (footprint) and LOD3 (detailed facade). With
the scheme above, the building ID derives from the parcel/footprint and the building rule's
site, and *not* from LOD-specific ops. LOD-specific sub-elements (windows exist only at LOD2+)
derive from the building ID. See [large-world-and-lod.md](large-world-and-lod.md).

### 3.4 Relation to the CAD topological naming problem

History-based parametric CAD has the same problem under another name: features reference
faces/edges of earlier results, and those references break when upstream topology changes
("topological naming problem"). Literature: Kripac 1997 persistent naming
([CAD journal](https://doi.org/10.1016/S0010-4485(96)00094-6) †), Capoyleas, Chen & Hoffmann 1996
generic naming ([CAD journal](https://doi.org/10.1016/0010-4485(95)00065-8) †); FreeCAD 1.0
(2024) shipped a mitigation after years of community pain *(knowledge)*. The B-rep kernel and
the procedural core must share **one naming scheme**: kernel ops propagate face/edge IDs from
inputs to outputs (split face → children IDs derived from parent face ID + split key; boolean →
faces keep provenance from operands), and procedural IDs are just the top of that hierarchy.
This should be coordinated with the B-rep research set.

---

## 4. Overrides that survive regeneration

### 4.1 Edit taxonomy

1. **Parameter override** on a generated element ("this building has 5 floors, flat roof").
2. **Transform edit** ("move this tree 2 m east").
3. **Deletion** ("remove this tree / this lamp post").
4. **Insertion** of hand-made content, which procedural content must react to ("a hand-placed
   radar mast; clear vegetation within 10 m").
5. **Replacement** of a generated element by authored content ("swap generated building for the
   surveyed BIM model of the real building").
6. **Topological edit** of generated geometry ("cut a doorway in this generated wall").

### 4.2 Approaches seen in practice

- **Bake and freeze** (Houdini/Blender bake, Grasshopper bake, generated Minecraft chunks): edits
  are easy but proceduralism is lost for that region.
- **Edit the inputs, not the outputs** (CityEngine lot attributes, UE PCG exclusion volumes and
  tags, Far Cry 5's designer-placed roads as inputs): robust; covers edits 1, 3, 4 when the
  input exists at the right granularity.
- **Override layers keyed by identity** (USD layers/opinions; Lipp et al.'s anchored assignments
  stored separately and applied during layer merging,
  [EG 2011](https://diglib.eg.org/handle/10.1111/v30i2pp345-354); No Man's Sky's terrain edit
  deltas): general, but only as good as the identity scheme; NMS updates moving terrain under
  player bases show what happens when anchors are not stable.
- **Edit nodes in the graph** (Houdini *Edit* SOP storing deltas by point number): fragile —
  index identity.
- **Re-targeting by matching** (Lipp et al. 2019 meta-locators analyze local context to re-find
  the edited element, [EG](https://diglib.eg.org:443/handle/10.1111/cgf13615)): robust to
  structural change, needs a confidence measure.
- **Element binding** (Dynamo): the host keeps the created element and updates it in place —
  preserves host-side edits only if the binding survives.

### 4.3 Recommended design

- Overrides live in **override layers** (separate from the graph and from generated data,
  serialized as text, ordered by strength like USD). Multiple layers allow per-user, per-scenario
  and per-test variations ("scenario B: building 17 has a rooftop antenna").
- Each override targets an element by **stable ID** and stores a **fallback locator**
  (semantic path + geometric signature: centroid in parent frame, size, class).
- Overrides are applied at the element's **override point** — the op that creates the element —
  so a parameter override feeds the downstream derivation (a 5-floor override regenerates that
  building's facade consistently at every LOD). Transform overrides apply at the same point,
  and downstream ops see the moved element (e.g. tree-clearing follows the moved tree).
- Deletions are **tombstones** (ID-keyed). Replacements substitute an authored asset for the
  element while keeping its ID, semantics and links (address, parcel).
- Insertions are ordinary **inputs** to the graph (authored layers) that downstream ops consume
  (exclusion masks, snapping targets).
- Topological edits on generated geometry are recorded as kernel operations against stable
  face/edge IDs (see §3.4) and replayed after regeneration.
- On every evaluation, each override is classified **resolved / re-anchored (with confidence) /
  orphaned**. Orphans are shown in a review UI and never silently dropped; re-anchored ones can
  be confirmed by the user, which rewrites their target ID.
- Overrides participate in caching like any other input (keyed by element), so editing one
  override re-evaluates one element (collection-level incrementality).

---

## 5. Instancing and identity

- Instances share a **prototype**; each instance has its own ID, transform and per-instance
  attributes. Sub-elements inside an instance have IDs `(instance_id, prototype_local_id)`,
  computed lazily (no need to materialize millions of IDs).
- Prototypes should be **content-addressed** (hash-consed): two trees generated with the same
  species/parameters/seed-class resolve to the same prototype automatically. Generators can
  deliberately quantize variation (e.g. 32 variants per species) to increase sharing.
- Overriding a sub-part of one instance **specializes** it (a new prototype derived from the old
  plus the override), similar to USD's instanceable flag being turned off for that prim.
- Instancing is a **representation choice** (schedule), not a semantic one: semantics, IDs and
  ground-truth labels must be identical whether an element is instanced or realized.

---

## References

Entries marked † were supplied from memory after this session's web-search budget ran out;
verify before citing.

- Salmon et al., Random123 / Parallel Random Numbers: As Easy as 1, 2, 3 (SC11): https://www.thesalmons.org/john/random123/releases/latest/docs/
- Widynski, Squares: A Fast Counter-Based RNG: https://arxiv.org/abs/2004.06278
- Counter-based random number generator (overview): https://en.wikipedia.org/wiki/Counter-based_random_number_generator
- Jarzynski & Olano, Hash Functions for GPU Rendering (JCGT 2020): https://jcgt.org/published/0009/03/02/ †
- CORE-MATH correctly rounded math library: https://core-math.gitlabpages.inria.fr/ †
- Minecraft Wiki, Density function: https://minecraft.wiki/w/Density_function
- Van Muijden, GPU-Based Procedural Placement in Horizon Zero Dawn (GDC 2017): https://www.guerrilla-games.com/media/News/Files/GDC2017_VanMuijden_GPUBasedProceduralPlacementInHorizonZeroDawn.pdf
- Dynamo primer, element creation and binding: https://primer2.dynamobim.org/7_dynamo_for_revit/4-creating
- Lipp, Wonka, Wimmer 2008, Interactive Visual Editing of Grammars for Procedural Architecture: https://www.cg.tuwien.ac.at/research/publications/2008/LIPP-2008-IEV/LIPP-2008-IEV-paper.pdf
- Lipp et al. 2011, Interactive Modeling of City Layouts using Layers of Procedural Content: https://diglib.eg.org/handle/10.1111/v30i2pp345-354
- Lipp et al. 2019, Local Editing of Procedural Models: https://diglib.eg.org:443/handle/10.1111/cgf13615
- Boris the Brave, Editable WFC: https://www.boristhebrave.com/2022/04/25/editable-wfc/
- OpenUSD glossary (layers, opinions, instancing): https://openusd.org/release/glossary.html
- Kripac 1997, A mechanism for persistently naming topological entities in history-based parametric solid models: https://doi.org/10.1016/S0010-4485(96)00094-6 †
- Capoyleas, Chen, Hoffmann 1996, Generic naming in generative, constraint-based design: https://doi.org/10.1016/0010-4485(95)00065-8 †
