# Evaluation engine design for procedural modeling

Part of the [procedural-systems research set](README.md). This file analyzes the core engine
choices — programming model, evaluation strategy, incremental recomputation, caching,
parallelism, language embedding, debugging, undo and persistence — drawing on build systems
and incremental-computation research rather than only on graphics tools. Identity and
randomness are in [determinism-and-identity.md](determinism-and-identity.md); partitioning and
LOD are in [large-world-and-lod.md](large-world-and-lod.md).

---

## 1. Programming models: graph, grammar, script, functional language

| Model | Examples | Strengths | Weaknesses |
|---|---|---|---|
| Dataflow node graph | Houdini SOPs, Blender GN, UE PCG, Grasshopper, Substance | Visual, inspectable intermediates, artist-friendly, explicit dependencies | Poor abstraction (spaghetti), weak control flow, hard to diff/merge, poor for recursion |
| Grammar (rewriting) | CGA, split grammars, L-systems | Natural for hierarchical subdivision; derivation tree gives identity; compact | Context-insensitive by default; global coordination awkward; non-recursive tasks (scattering, terrain) unnatural |
| Imperative script | Houdini Python SOP, Blender bpy, Infinigen, Unity C# | Unlimited power, existing ecosystems | Side effects defeat caching, parallelism, identity and determinism; slow per-element |
| Functional language | VEX/fields (per element), CGA++, Blender 5 closures, Minecraft density functions, APEX | Pure ⇒ cacheable, parallel, lazily evaluable; first-class functions give abstraction | Unfamiliar to artists unless presented as graph; needs a good type system and tooling |

Key observation: **these are front-ends, not engines.** A node graph is a visual syntax for a
pure functional program; a CGA rule is a recursive function `Shape → ShapeTree`; an L-system
is an iterated parallel map. Blender's trajectory (fields → zones → closures → bundles → lists)
and SideFX's APEX (graphs as values, partial evaluation) are both converging on a **typed,
pure, functional intermediate representation (IR)** under a graph UI. CGA++ reached the same
point from the grammar side (first-class shapes and rules,
[academic-foundations.md](academic-foundations.md)).

Recommendation: one **core IR** — a typed, pure, higher-order dataflow language — with several
front-ends: a node editor, a textual DSL (canonical, diffable, LLM-friendly), grammar-style
sugar for subdivision rules, and a builder API in a host language that *traces* to the IR
(the JAX/TensorFlow-graph/Halide approach). Imperative code is allowed only inside *declared
pure ops* (see §7).

### 1.1 Algorithm vs. schedule

Halide separates *what* is computed (the algorithm) from *how* (tiling, vectorization,
caching, fusion — the schedule) ([Halide](https://halide-lang.org/),
[PLDI 2013 paper](https://people.csail.mit.edu/jrk/halide-pldi13.pdf) †). Minecraft's density
functions do a small version of this: `cache_2d`, `flat_cache`, `interpolated` are schedule
annotations that never change meaning (see the
[domain survey](survey-domain-and-game-systems.md)). Houdini's compiled blocks are a clumsy
version (a schedule hint that also restricts semantics). UberTopo should adopt the separation
explicitly: the procedural graph says what; per-node or per-region **evaluation policies**
(cache tier, granularity, CPU/GPU, LOD/resolution, interpolation) say how, and can be tuned
without changing results beyond declared tolerances.

---

## 2. Evaluation strategy: lazy, eager, demand-driven

- **Eager push** (Grasshopper, Dynamo): any change re-runs everything downstream. Simple, but
  wastes work on outputs nobody is looking at and forces whole-world evaluation.
- **Demand-driven pull with dirty flags** (Houdini, Excel-like): only requested outputs are
  computed; edits mark downstream nodes dirty.
- **Lazy functional evaluation** (Blender lazy-function graphs, APEX partial evaluation): a node
  requests only the inputs it needs for the outputs it was asked for (e.g. *Switch* evaluates
  one branch).
- **Generate-on-query** (Minecraft, HZD, UE runtime PCG): the request is not "the output of
  node N" but "the world in region R at LOD L"; evaluation is a pure function of position and
  local inputs.

For UberTopo the natural request is **(output, region, LOD, layer-set)** — a sensor simulator
asking for "acoustic-relevant surfaces within 500 m of microphone array M at LOD2", or a
viewport asking for "everything visible at screen-space error ε". The engine should therefore
be **demand-driven with region/LOD as part of the query key**, and lazy within nodes where
semantics allow (fields, switches, closures). Pure laziness hurts parallelism and
predictability; the remedy (used by Blender) is demand-driven scheduling that spawns parallel
tasks for independent demanded inputs, with a threshold below which work runs inline.

---

## 3. Incremental recomputation

### 3.1 The build-systems view

Mokhov, Mitchell and Peyton Jones show that every build system is a combination of a
**scheduler** and a **rebuilder**, which are orthogonal
([ICFP 2018](https://www.microsoft.com/en-us/research/uploads/prod/2018/03/build-systems-a-la-carte.pdf),
[JFP 2020](https://www.microsoft.com/en-us/research/uploads/prod/2020/04/build-systems-jfp.pdf)):

- Schedulers: **topological** (static dependencies known up front — Make), **restarting**
  (abort and retry when a missing dependency is discovered — Excel, Bazel), **suspending**
  (suspend the task while its newly discovered dependency is built — Shake, and demand-driven
  query systems).
- Rebuilders: **dirty bit** (Make timestamps, Excel), **verifying traces** (record dependency
  hashes, rebuild only if a hash changed — Shake), **constructive traces** (store results keyed
  by input hashes so they can be fetched from a shared cache — Bazel/CloudBuild), **deep
  constructive traces** (key by terminal inputs only — Nix).

Classifying procedural systems this way is illuminating:

| System | Scheduler | Rebuilder | Early cutoff |
|---|---|---|---|
| Houdini SOP cook | demand-driven (suspending-ish) | dirty bit | no |
| Houdini PDG | topological over work items | file existence/cache mode | partial |
| Blender GN | lazy demand within a tree | none (re-evaluate on change) | n/a |
| Grasshopper | topological push | dirty bit ("expire") | no |
| UE PCG | topological per component/cell | dirty bit + per-cell regen | no *(unverified)* |
| Salsa / rust-analyzer | suspending (demand) | verifying traces | yes |
| Bazel | restarting | constructive traces | yes |

Procedural graphs have **dynamic dependencies** (which tiles a road crosses, which parcels a
river splits, which LOD a sensor demands are only known after computing something), so a
**suspending, demand-driven scheduler** is the right choice. For the rebuilder, **verifying
traces with early cutoff** (if a recomputed value hashes the same as before, downstream is not
invalidated) plus **constructive traces** (results stored by key so they can be reused across
sessions, machines and undo history) give the best of both.

### 3.2 Incremental computation research

- **Self-adjusting computation** (Acar et al.) records a dynamic dependence graph during
  execution and replays only affected parts on input change ([Acar 2005 thesis](https://www.cs.cmu.edu/~umut/papers/thesis.pdf) †).
- **Adapton** (Hammer et al., PLDI 2014) makes it **demand-driven and composable**: only
  computations demanded by observers are recomputed, with hierarchical change tracking
  ([paper](https://people.mpi-sws.org/~hammer/adapton-pldi2014.pdf),
  [Racket docs](https://docs.racket-lang.org/adapton/index.html)).
- **Salsa** (rust-analyzer, rustc's query system): tracked functions, interned values,
  **red-green** verification (walk dependencies to see whether any input actually changed),
  **early cutoff** (stop propagation when a recomputed value is equal), and **durability**
  levels (rarely-changing inputs such as the standard library are skipped during validation)
  ([Salsa book](https://salsa-rs.netlify.app/print),
  [durable incrementality](https://rust-analyzer.github.io/blog/2023/07/24/durable-incrementality.html)).
  Durability maps directly onto procedural worlds: GIS base layers and asset libraries are
  high-durability; a user tweaking a facade parameter is low-durability.
- **Jane Street Incremental**: stabilization of a DAG with cutoffs and observers; a good model
  for UI-facing incremental views ([blog](https://blog.janestreet.com/introducing-incremental/) †).
- **Differential dataflow** (McSherry et al.): incremental computation over **collections** —
  changes are expressed as (record, multiplicity delta) and operators propagate deltas; joins,
  group-by and iteration are incremental ([CIDR 2013](https://www.cidrdb.org/cidr2013/Papers/CIDR13_Paper111.pdf) †,
  [repo](https://github.com/TimelyDataflow/differential-dataflow) †).

### 3.3 Two granularities: node-level and element-level

Every production procedural tool invalidates at **node granularity**: change the facade
parameter of one building and the node recomputes for 100 000 buildings. This is the single
biggest scalability failure of current systems and the reason studios reach for PDG/partitions.

UberTopo needs incrementality at two levels:
1. **Graph level** (salsa-style queries): each node evaluation is a memoized query keyed by
   (op, op-version, parameter hash, input value hashes, region/LOD request).
2. **Collection level** (differential-dataflow-style): values that are *keyed collections* —
   buildings keyed by stable ID, road segments keyed by edge ID, tiles keyed by tile coordinate —
   flow through **keyed map/filter/group/join** operators that memoize *per key*. Editing one
   building's override changes one key; the map re-runs for that key only; downstream
   aggregations (e.g. "merge all buildings in tile T") update only affected tiles.

Collection-level incrementality depends entirely on **stable keys** (see
[determinism-and-identity.md](determinism-and-identity.md)): with index-based identity, an
insertion shifts every key and nothing can be reused. Memo-table overhead must be controlled by
**adaptive granularity** — memoize per key for expensive per-element work (a building), per
batch/chunk for cheap work (grass blades).

### 3.4 Dirty propagation details

- Use **hash-based early cutoff**, not just dirty bits; many edits (renaming, re-ordering
  irrelevant inputs, changing a parameter back) produce identical results.
- Hashing large geometry is not free. Use **recipe (input-addressed) keys** for lookup and
  **content hashes** computed lazily (e.g. while writing the output, or on attribute arrays
  incrementally) for early cutoff; store both.
- Distinguish **structural edits** (graph topology changed) from **parameter edits**; on a
  structural edit, re-derive dependency edges for affected nodes only.
- Time is just another input (Houdini's global time-dependency flag is a known pain point);
  simulation/time-varying content should be explicit nodes with time as a parameter.

---

## 4. Caching and memoization

- **Tiers**: (1) in-memory memo table with cost-aware eviction (keep entries whose
  recompute-cost/size ratio is high — a BVH or boolean result before a cheap transform);
  (2) local on-disk **content-addressed store** (CAS); (3) optional shared/remote CAS (team or
  render-farm cache, à la Bazel remote cache). PDG's file cache is a crude tier 2.
- **Cache key** = hash(op identity, op implementation version, normalized parameters, input
  keys, seed/stream, region, LOD, numeric-mode flags). Op version must be bumped on any
  behavior change; otherwise stale caches silently persist (a common Houdini/PDG failure).
- **Values are immutable with structural sharing.** Blender's implicit sharing of attribute
  arrays (copy-on-write since 3.6 *(knowledge)*) is the right model: a transform op that only
  changes positions shares every other array with its input. This makes caching many
  intermediate geometries affordable.
- **External inputs** (GIS layers, DEMs, OSM extracts, LiDAR tiles, hand-authored files) enter
  the graph through *input nodes* that hash content (or content version IDs) — never by path +
  timestamp.
- **Purity is a precondition.** Any hidden state (global RNG, wall-clock time, iteration order
  of a hash map, thread-count-dependent reductions) silently corrupts caches. Enforce purity
  by construction (ops receive everything via arguments; RNG is keyed, see the determinism file)
  and test it (re-evaluate with shuffled scheduling and compare hashes).

---

## 5. Parallelism

Three levels, all needed:

1. **Task parallelism** over the dependency graph and over keyed collections: a work-stealing
   scheduler (TBB, Taskflow, Rayon) running demanded queries. Blender's "lazy threading" lesson:
   most nodes are cheap, so start inline and fork only when work is large.
2. **Data parallelism** inside ops: structure-of-arrays attributes (Blender, UE 5.6 PCG point
   arrays), per-element kernels compiled from the field/expression language (VEX-like),
   SIMD. Fields fuse chains of per-element expressions into one kernel pass.
3. **GPU**: noise/density evaluation, scattering with rejection, mask rasterization, erosion,
   distance fields, visibility. UE PCG's GPU nodes and HZD's GPU placement show it works for
   scattering; it is a poor fit for topological B-rep operations.

Plus **distribution**: tiles/partitions dispatched to farm workers sharing a remote CAS — the
same engine and cache keys, not a separate PDG-like layer.

**Determinism under parallelism**: results must not depend on thread count or scheduling.
Rules: reductions combine in key order (or use exactly associative/commutative operations such
as integer sums); collections are canonically ordered by stable key, never by completion order;
no atomics-based append without a subsequent sort; RNG is counter-based and keyed, never
sequential. GPU floating point differs across vendors/drivers; decisions that branch (place /
don't place) should be made on quantized or integer values to be robust (see the determinism
file).

---

## 6. Iteration, recursion, simulation

Dataflow DAGs cannot express loops directly. Options seen in practice: Houdini For-Each/
feedback loops, Blender repeat/for-each/simulation zones, CGA recursion, open L-systems'
environment feedback. Recommend **structured iteration constructs** in the IR — `map` over a
keyed collection, `fold`, bounded `repeat(n)`, `iterate_until(converged, max_iter)`, and
recursion over trees (grammar derivation) — each a node whose body is a closure. The outer
graph stays acyclic and cacheable; each iteration's state is a value with its own key, so
incremental re-evaluation can reuse iterations whose inputs are unchanged (Blender already
builds per-iteration lazy-function nodes for repeat zones).

---

## 7. Language embedding vs. DSL

- **DSL only** (CGA, VEX): fast, analyzable, safe; limited ecosystem; separate tooling.
- **General scripting per element** (Python SOP, bpy loops): slow and impure.
- **Embedded DSL traced to IR** (JAX-style): authors write host-language code that *builds* the
  graph; the graph is then analyzed, cached and executed natively. Combines ecosystem and
  analyzability. Requires care so that host-language control flow over *values* is expressed by
  IR constructs (cond/map/repeat) rather than Python `if`.
- **Native/sandboxed custom ops**: extension ops written in the engine language (Rust/C++) or
  compiled to **WebAssembly** and run in a sandbox, with a declared signature, declared purity,
  and a version. WASM gives deterministic, portable execution (with NaN canonicalization and
  without threads/time/IO by default), which suits a cacheable, reproducible engine
  ([WASM determinism notes](https://github.com/WebAssembly/design/blob/main/Nondeterminism.md) †).

Recommendation: core in a systems language; canonical textual IR; Python bindings that trace
to IR (for researchers, notebooks and LLM agents); a compact per-element expression language
(fields) JIT-compiled or vectorized-interpreted; WASM or native plug-in ops for extension.

---

## 8. Debugging and introspection

Procedural systems are notoriously hard to debug ("why is there a tree in the road?").
Requirements distilled from Houdini (geometry spreadsheet, middle-click info, node
profiling), Blender (viewer node, socket inspection, node warnings, 4.3 warning outputs), UE
PCG (debug display per node, inspection), and the Hytale complaints (no noise visualization,
ordering by canvas position):

- **Inspect any intermediate value** in the viewport and in a spreadsheet, for any region/LOD.
- **Per-element provenance**: every generated element carries (producing op/rule path, stable
  ID, seed stream, key parameter values, source input feature IDs). Click a tree → see that it
  came from scatter node S, candidate C, in tile T, with density 0.7 from forest mask F, not
  culled because road buffer R did not reach it.
- **"Why did this recompute?"** — record invalidation causes (Salsa emits events for this).
  Show cache hits/misses and timings per node and per key.
- **Deterministic replay** of any evaluation from (document version, request), and **diff of two
  evaluations by stable ID** (added/removed/changed elements) — invaluable for regression tests
  and for drone-scenario reproducibility.
- **Static checking at edit time**: types, units (metres vs. degrees), domain mismatches,
  unbounded-influence warnings for non-local ops in partitioned graphs.
- Errors and warnings attached to nodes *and* to elements (e.g. "boolean failed for building
  b:…:17").

---

## 9. Undo, persistence, serialization

- **Source of truth = the document**: graph(s), parameters, external input references (by
  content hash/version), override layers, seeds. Generated geometry is **cache**, never
  authoritative — except where the user explicitly **bakes/freezes** an element, which
  promotes it to authored data with recorded provenance.
- Store the document as a **persistent (immutable, structurally shared) data structure**. Undo/
  redo are pointer swaps; because caches are keyed by content, undo instantly reuses earlier
  results (an advantage no current DCC fully exploits; Blender's undo re-evaluates).
- **Serialization**: canonical text for graphs/overrides (stable ordering, stable IDs, diffable,
  mergeable in git, readable by LLMs); binary CAS blobs for caches and baked data; explicit
  **op versioning and migrations** so old documents still evaluate (APEX 20.5 broke graphs;
  Grasshopper 2 cannot open GH1 files — both show the cost of not planning this).
- Collaboration: layered overrides (USD-style) allow multiple users to edit different layers;
  the deterministic engine means collaborators need to exchange only documents, not outputs.

---

## References

Entries marked † were supplied from memory after this session's web-search budget ran out;
verify before citing.

- Mokhov, Mitchell, Peyton Jones, Build Systems à la Carte (ICFP 2018): https://www.microsoft.com/en-us/research/uploads/prod/2018/03/build-systems-a-la-carte.pdf
- Build Systems à la Carte: Theory and Practice (JFP 2020): https://www.microsoft.com/en-us/research/uploads/prod/2020/04/build-systems-jfp.pdf
- Mokhov, blog post: https://blogs.ncl.ac.uk/andreymokhov/build-systems-a-la-carte
- Hammer et al., Adapton (PLDI 2014): https://people.mpi-sws.org/~hammer/adapton-pldi2014.pdf
- Adapton Racket package: https://docs.racket-lang.org/adapton/index.html
- Salsa book: https://salsa-rs.netlify.app/print
- rust-analyzer, Durable Incrementality: https://rust-analyzer.github.io/blog/2023/07/24/durable-incrementality.html
- Acar, Self-Adjusting Computation (thesis, 2005): https://www.cs.cmu.edu/~umut/papers/thesis.pdf †
- Jane Street, Introducing Incremental: https://blog.janestreet.com/introducing-incremental/ †
- McSherry et al., Differential Dataflow (CIDR 2013): https://www.cidrdb.org/cidr2013/Papers/CIDR13_Paper111.pdf †
- Differential dataflow repository: https://github.com/TimelyDataflow/differential-dataflow †
- Halide: https://halide-lang.org/ ; Ragan-Kelley et al. PLDI 2013: https://people.csail.mit.edu/jrk/halide-pldi13.pdf †
- WebAssembly nondeterminism design notes: https://github.com/WebAssembly/design/blob/main/Nondeterminism.md †
- Blender lazy-function evaluator commits: https://developer.blender.org/rB4130f1e674f , https://developer.blender.org/rB5c81d3bd469
- Blender PR #112421 (repeat zone laziness): https://projects.blender.org/blender/blender/pulls/112421
- SideFX, APEX graphs (partial evaluation): https://sidefx.com/docs/houdini/character/kinefx/apexgraphs.html
- SideFX, Compiled blocks: https://www.sidefx.com/docs/houdini/model/compile.html
- Minecraft Wiki, Density function (cache markers): https://minecraft.wiki/w/Density_function
