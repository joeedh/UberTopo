# Project Decisions

This is a log of decisions made by the project owner, with what each one implies. Each
entry is permanent. If a decision is reversed, add a new entry that supersedes it rather
than editing the old one.

---

## DEC-001 — Implementation language: C++20 on litestl

- **Date:** 2026-11-17
- **Decision:**
  - UberTopo is written in **C++** on top of the project owner's
    [`litestl`](https://github.com/joeedh/litestl) foundation library.
  - TypeScript and Python glue goes through **litestl's binding system** (`binding/`,
    with its TypeScript and Python generators).
- **Answers:** open question 1 in [research/README.md](research/README.md#4-consolidated-open-questions-for-the-project-owner).

### What litestl provides (as of commit `765241e`)

| Area | litestl component | Notes for UberTopo |
|---|---|---|
| Containers | `util::Vector`, `Map`, `Set`, `OrderedSet`, `BoolVector`, `BinaryHeap`, `Span`, `Array`, `Pool` | Small-buffer-optimised and non-throwing. Use these, not the STL, in hot paths. |
| Memory | `alloc::alloc/release/New/Delete` with a leak tracker; `shared_ptr`/`weak_ptr` | Columnar attribute pages and copy-on-write chunks should allocate through `alloc`. |
| Parallelism | `task::parallel_for(IndexRange, cb, grain)`, thread pool | Determinism rule: results must not depend on how the range is chunked (see gaps). |
| Math | `math::Vec<T,N>` (templated on scalar, so `double` works), matrix, quat, AABB, geom, triangle/AABB intersection | Kernel code uses `Vec<double,3>` or exact types; `float` only at the GPU and render boundary. |
| Linear algebra | Vendored Eigen (header-only `eigen` target) | Available for solvers (fitting, least squares) without adding a dependency. |
| Hashing | `hash::hash`, `mixHash` (splitmix64), Swiss-table control plane | 64-bit. Persistent IDs need 128 bits (see gaps). |
| Serialisation | `io/binfile.h`, `io/serial`, lz4 compression | Base for the content-addressed cache and tile blobs. |
| Strings, paths, platform | `util::string`, `path`, `platform` (stack traces, `debugBreak`, CPU, time) | Use `path` rather than `std::filesystem`. |
| Reflection and bindings | `binding/` with `Bind<T>()`, `defineBindings()`, TS generator and runtime, Python `.pyi` generator | All public API types opt in with `defineBindings()`. |
| Tests | Custom harness (`test_init/test_assert/test_end`) plus DTL | UberTopo tests follow the same convention. |
| WASM | First-class Emscripten build | Opens a browser/Node target for UberTopo (see open questions). |

### Gaps in litestl that UberTopo needs filled

| Gap | Why it is needed | Proposed home |
|---|---|---|
| **Counter-based RNG** (Philox-4x32 or Squares) with keyed named streams | `util/rand.h` is a self-described "very bad" LCG with 19-bit output. Procedural determinism needs `rng(seed, node, purpose, element_id)` to be stateless and order-independent. | litestl (general purpose) |
| **128-bit hashing** (stable across platforms, independent of pointers) | Persistent provenance IDs and content-addressed cache keys | litestl |
| **Fixed-width wide integers** (128/256-bit signed, with mul/add/sign) | Exact plane-based predicates (~26-bit inputs, ≤256-bit intermediates) | UberTopo kernel, or litestl if wanted |
| **Adaptive floating-point predicates** (orient2d/3d, incircle; Shewchuk-style expansions) | Exact 2D arrangements and CDT | UberTopo kernel |
| **B-spline library** (`math/bspline.h` is an empty stub) | Certified curve and surface caches | UberTopo curves module, or finish litestl's stub |
| **Deterministic reductions** for `parallel_for` | Floating-point sums must not depend on chunking or thread count | UberTopo, as a fixed-chunk reduce helper |
| **Copy-on-write paged columns** | Versioned topology and attribute storage | UberTopo core |

The proposed homes are suggestions. See open question Q-B below.

---

## DEC-002 — Avoid third-party libraries; Manifold is a possible exception

- **Date:** 2026-11-17
- **Decision:**
  - Avoid third-party libraries as much as possible. litestl and its vendored Eigen and
    lz4 count as the foundation, not as third-party additions.
  - The **Manifold** library (Apache-2.0) *may* be an exception.
- **Effect:** this overturns most "borrow" rows in
  [cad-kernels/recommendations.md §4](research/cad-kernels/recommendations.md) and
  [attributes-and-queries/recommendations.md](research/attributes-and-queries/recommendations.md).

### Revised borrow-vs-build

| Component | Research said | Now | Approach |
|---|---|---|---|
| Exact predicates | Borrow Shewchuk / Geogram PCK | **Build** | Re-implement the published algorithms (adaptive expansions; plane-based integer predicates). These are small, well-documented, and the core of correctness anyway. |
| 2D clipping and offsetting | Borrow Clipper2 | **Build** | It falls out of the exact 2D arrangement we were building anyway: offset = arrangement of offset segments + winding selection. |
| Constrained Delaunay (terrain, faces with holes) | Borrow | **Build** | Incremental CDT on the same exact predicates. |
| 3D Booleans | Build plane-based exact; Manifold as fallback | **Build**; Manifold optional | See "Role of Manifold" below. |
| Ray tracing and BVH | Embree / OptiX / hardware RT | **Build** | Own binned-SAH BVH (f64 option, TLAS/BLAS instancing), batch query API. A GPU path can come later through our own compute layer. |
| Sparse volumes (vegetation media, ESDF, fields) | OpenVDB / NanoVDB | **Build** | A simple two-level sparse brick grid (hash of 8³ bricks) covers the needs. VDB's full generality isn't required. |
| Mesh simplification and clustering | meshoptimizer | **Build** | Quadric simplification with attribute and feature-edge constraints. Acoustic proxies need custom rules anyway. |
| Clothoid fitting | Wrap or port Bertolazzi–Frego `Clothoids` | **Port algorithms** | Re-implement from the papers. The original library may be used **only as an offline test oracle**, never linked. |
| Curve API ideas | kurbo (Rust) | **Design reference only** | No code reuse is possible from Rust anyway. |
| NURBS / STEP import | OCCT or SMLib adapters | **Defer** | Only needed if exact CAD/BIM import is required (open question 16). |
| CGAL | Test oracle | **Offline oracle only** | GPL, so never linked into UberTopo. Used in separate test tooling, if at all. |
| Linear algebra | — | **Use Eigen** | Already vendored in litestl. |
| Compression | — | **Use lz4** | Already in litestl. |

### Role of Manifold

Manifold is the best candidate exception, but it cannot be the core Boolean engine:

- It requires **manifold (closed, oriented) input** and produces manifold output. UberTopo's
  master model is a **non-manifold cell complex** (shared walls, laminae, wires, regions;
  see [research/README.md §1.1](research/README.md#11-is-a-2-manifold-enough-or-do-we-need-a-3-manifold)).
- It is not exact; it breaks ties symbolically rather than with tolerances.
- Its dependencies need checking before adoption. The core is self-contained; TBB and
  Clipper2 are optional, behind build flags. *(Verify the current build options before
  vendoring.)*

**Recommended use:**
1. As a **differential-testing oracle** for our own exact Boolean on closed solids.
2. Optionally, as a **fallback back end** for closed visual assets (props, vehicles) behind
   the same Boolean interface.

Decide whether to link it once the in-house exact arrangement exists (Spike 1). Until then,
it costs nothing to keep it test-only.

### Consequences beyond geometry

"No third-party libraries" also affects the **I/O and format layer**, which the research
assumed would use common libraries. Each of these is a parser or writer that has to be
built:

- **DEM rasters:** GeoTIFF needs a TIFF reader with Deflate and LZW. Deflate (inflate)
  must be written, because litestl has only lz4.
- **OpenStreetMap:** OSM PBF needs Protobuf decoding and zlib. Alternatives are ingesting
  `.osm` XML or GeoJSON exports instead.
- **LiDAR:** LAS is simple. LAZ uses its own arithmetic-coding compressor, which is
  substantial work. Prefer pre-decompressed LAS or a conversion step outside UberTopo.
- **Exports:** glTF / 3D Tiles, CityJSON and OpenDRIVE are JSON or XML plus binary
  buffers. A small JSON and XML reader and writer is needed.
- **Projections:** WGS84 ↔ ECEF ↔ ENU are formulas we own. UTM and other projections
  (normally PROJ) are a bounded set of formulas we would implement as needed.

A pragmatic option is to keep heavyweight format conversion **outside** UberTopo, as
separate offline tools that may use third-party code. UberTopo itself would then ingest
only simple, documented formats. See Q-C.

---

## Open questions raised by these decisions

- **Q-A — WASM target.** Must UberTopo run in WASM (browser or Node), given litestl's
  first-class WASM build and the TS glue? WASM32 limits memory to 4 GB (memory64 is
  needed beyond that), threading needs SharedArrayBuffer, and wide-integer exact
  arithmetic is slower. This affects tile sizes and the design of the exact core.
- **Q-B — Where should general-purpose additions live?** Should counter-based RNG,
  128-bit hashing, wide integers and B-splines go into **litestl** (shared with
  Sculptcore) or stay in UberTopo?
- **Q-C — Offline converters.** Can format conversion (GeoTIFF, OSM PBF, LAZ, STEP) live
  in separate offline tools that may use third-party libraries, or must the whole
  toolchain be in-house?
- **Q-D — How litestl is included.** The proposal is a git submodule (e.g. `extern/litestl`
  or `source/litestl`, as in Sculptcore), pinned per commit.
- **Q-E — Python binding runtime.** litestl's Python generator emits `.pyi` stubs. What is
  the intended native Python runtime path (CPython extension module, ctypes or cffi over
  the C ABI, or Python via WASM)?
