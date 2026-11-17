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

* **C++20 on [litestl](https://github.com/joeedh/litestl)** (DEC-001). Follow litestl's
  conventions: its containers (`util::Vector/Map/Set/...`) over the STL in hot paths,
  allocate via `litestl::alloc`, hash via `litestl::hash`, `.h`/`.cc` files, its test
  harness, and `path` instead of `std::filesystem`.
* **TypeScript/Python glue goes through litestl's binding system** (`defineBindings()`),
  not pybind11/embind/etc.
* **Avoid third-party libraries** (DEC-002). litestl and what it vendors (Eigen, lz4) are
  fine. The Manifold library is the only candidate exception and needs an explicit
  decision before it is linked. Third-party/GPL code (e.g. CGAL, Clothoids) may be used
  only as an offline test oracle, never linked. Re-implementing published algorithms is
  the default.
