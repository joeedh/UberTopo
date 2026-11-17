# CLAUDE.md

Project: UberTopo — a CAD kernel + procedural modeling core for modeling human and
natural environments at multiple levels of detail, used to develop and test drone
defense systems. See `docs/requirements.md`.

## Rules

* **Temporary comments** must be prefixed with `CLAUDENOTE:` so they can be stripped
  later (e.g. `// CLAUDENOTE: remove once the BVH refit lands`).
* **Debugging guide:** keep a running debugging guide in `docs/debugging.md`. Whenever
  you diagnose a non-obvious problem, add the symptom, cause and fix/technique there.
* **Design docs** go in `docs/`.
* **Research docs** go in `docs/research/`. If a research doc grows large, split it
  into smaller, topic-focused files.
* **Plans** must be saved to the repo in `docs/plans/`. Before implementing a plan,
  pressure test it with an agent that has a fresh context (one that has not seen
  the discussion that produced the plan); record the review's findings and any plan
  changes in the plan file.
* **Docs index:** maintain `docs/index.md`. Every time a doc is added, moved, renamed
  or removed under `docs/`, update the index in the same change.

## Technology (see `docs/decisions.md`)

* **C++20 on [litestl](https://github.com/joeedh/litestl)** (DEC-001), included as a git
  submodule at `source/litestl` (DEC-003); `source/` is the include root. Follow
  litestl's conventions: its containers (`util::Vector/Map/Set/...`) over the STL in hot
  paths, allocate via `litestl::alloc`, hash via `litestl::hash`, `.h`/`.cc` files, its
  test harness, and `path` instead of `std::filesystem`.
* **litestl is ours to modify.** General-purpose additions (RNG, hashing, wide ints,
  predicates, containers, N-API runtime) go into litestl, committed in the submodule
  and pushed there, then the submodule pointer is bumped. **B-splines stay in
  UberTopo**, not litestl.
* **TypeScript/Python glue goes through litestl's binding system** (`defineBindings()`).
  The UX runs in an NW.js shell (DEC-006; N-API migration and Python deferred) and loads UberTopo as a **native
  N-API addon**, not WASM (DEC-005). Use raw C N-API (`node_api.h`), never
  node-addon-api.
* **Avoid third-party libraries in the core** (DEC-002). Allowed: litestl and what it
  vendors (Eigen, lz4); protobuf, compression/decompression libraries, a LAZ library,
  and third-party format converters (DEC-004). Manifold is a candidate exception that
  needs an explicit decision before linking. GPL or other third-party code (CGAL,
  Clothoids) may be used only as an offline test oracle, never linked. Re-implementing
  published algorithms is the default for geometry.
* **B-rep storage is struct-of-arrays modelled on Sculptcore's mesh** (DEC-006). Use
  `docs/research/sculptcore-brep-reference.md` and Sculptcore's `source/mesh/` (MIT,
  same author) as reference.
