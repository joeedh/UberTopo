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
| **Counter-based RNG** (Philox-4x32 or Squares) with keyed named streams | `util/rand.h` is a self-described "very bad" LCG with 19-bit output. Procedural determinism needs `rng(seed, node, purpose, element_id)` to be stateless and order-independent. | litestl |
| **128-bit hashing** (stable across platforms, independent of pointers) | Persistent provenance IDs and content-addressed cache keys | litestl |
| **Fixed-width wide integers** (128/256-bit signed, with mul/add/sign) | Exact plane-based predicates (~26-bit inputs, ≤256-bit intermediates) | litestl |
| **Adaptive floating-point predicates** (orient2d/3d, incircle; Shewchuk-style expansions) | Exact 2D arrangements and CDT | litestl |
| **B-spline library** (`math/bspline.h` is an empty stub) | Certified curve and surface caches | **UberTopo** (DEC-003: not litestl) |
| **Deterministic reductions** for `parallel_for` | Floating-point sums must not depend on chunking or thread count | litestl (`task`) |
| **Copy-on-write paged columns** | Versioned topology and attribute storage | litestl (generic container) |

Homes are as decided in DEC-003: everything goes into litestl except B-splines.

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

## DEC-003 — litestl is a git submodule, and additions go into litestl

- **Date:** 2026-11-17
- **Decision:**
  - litestl is included as a **git submodule at `source/litestl`**. This matches
    Sculptcore's layout, so `source/` is the include root and includes read
    `"litestl/util/vector.h"`.
  - **We modify litestl as needed.** General-purpose additions go **into litestl**:
    counter-based RNG, 128-bit hashing, wide integers, adaptive predicates,
    deterministic reductions, copy-on-write paged columns, and the N-API runtime
    (DEC-005).
  - **Exception: B-splines live in UberTopo**, not litestl. litestl's `math/bspline.h`
    stub is left alone.
- **Answers:** Q-B and Q-D.
- **Workflow note:** changes to litestl are made in the submodule, committed and pushed
  to the litestl repo, and then the submodule pointer is bumped in UberTopo. The
  Claude session currently has read-only access to `joeedh/litestl`. Pushing litestl
  changes needs push access to be granted when the first litestl change is made.

---

## DEC-004 — Third-party libraries allowed for formats, compression and LiDAR

- **Date:** 2026-11-17
- **Decision:** these refine DEC-002. The following third-party categories are
  acceptable:
  - **Protobuf** (e.g. for OSM PBF).
  - **Compression and decompression** libraries.
  - **Third-party format converters.**
  - **A third-party compressed-LiDAR (LAZ) library**, "for now".
- **Answers:** Q-C.
- **Candidates.** These are recommendations, not yet decisions; choose when the
  importer is built.

| Need | Candidate | Licence | Notes |
|---|---|---|---|
| Protobuf (OSM PBF) | **protozero** | BSD-2 | Header-only, minimal; the decoder libosmium uses. Google `protobuf` (BSD-3) also works but is much heavier and needs code generation. |
| Deflate / zlib (GeoTIFF, OSM PBF, glTF/PNG) | **libdeflate** or **zlib** | MIT / zlib | libdeflate is faster for whole-buffer decode; zlib is needed for streaming. |
| Other compression | zstd | BSD-3 | Optional, for our own tile/cache blobs alongside litestl's lz4. |
| LAZ (compressed LiDAR) | **laz-perf** | Apache-2.0 | Supports LAS 1.4 point formats 6–8, used by PDAL. LASzip (LGPL-2.1) is the reference alternative. *(Verify current versions and point-format coverage when adopting.)* |
| Format conversion | GDAL/OGR, PDAL (external tools) | MIT / BSD | Run as **offline converters** outside the UberTopo binary, e.g. GeoTIFF → raw tiles, shapefile → GeoJSON. |

The core geometry stays in-house per DEC-002. The DEC-002 consequences on the I/O and
format layer are relaxed accordingly. JSON, XML and the glTF / 3D Tiles / CityJSON /
OpenDRIVE writers are still ours, since they are simple.

---

## DEC-005 — UX in a Chromium shell through litestl's N-API support (not WASM)

- **Date:** 2026-11-17
- **Decision:**
  - The UX may run in a **Chromium shell** (NW.js or Electron).
  - UberTopo is loaded as a **native Node-API (N-API) addon** using litestl's N-API
    binding support. It is not compiled to WASM, so the WASM32 4 GB memory and
    threading limits do not apply.
- **Answers:** Q-A.
- **Known state:** the N-API support is "somewhat hackish" and we may need to improve it.

### Where the N-API code lives today (Sculptcore `d508c0b`, read 2026-11-17)

litestl currently contains **no N-API code**. Its binding system has a TS/WASM runtime
and a Python `.pyi` generator only. The N-API reflection runtime has **bled into
Sculptcore**, under `source/napi/`. Classification:

| Sculptcore file | Generic (move to litestl) | Sculptcore-specific |
|---|---|---|
| `napi_runtime.h/.cc` — C++ port of the TS binding runtime reading `litestl::binding` descriptors; log sink | ✅ (rename `sc_`-prefixed symbols) | |
| `napi_object_model.cc` — JS class construction, instance wrapping, member/array accessors | ✅ | |
| `napi_introspection.cc` — method/constructor invocation, `structInfo`, `construct`, `makeXVector` out-param helpers | ✅ | |
| `napi_vector_bridge.cc` — typed-array views over `util::Vector`, element access, JS→C++ assign | ✅ | |
| `napi_util.h/.cc` — marshalling helpers | ✅ | |
| `napi_log.h` — log sink indirection | ✅ | |
| `napi_debug.cc` — allocator introspection, stdout redirect | partly (allocator dumps) | Crashpad, smoke test |
| `napi_entry.cc`, `napi_c_api.h`, `napi_mesh_vdm.cc`, `napi_gpu_brush.cc`, `napi_spatial_multires.cc` | | ✅ |
| `typescript/api/nativeManager.ts`, `nativeBackend.ts` | likely (TS side of the runtime) | |
| Build: cmake-js configure, clang `/DELAYLOAD` fix, NW.js/Electron runtime selection (`spike/napi/RESULTS.md`) | ✅ as a reusable CMake macro | |

### Hacks and limitations seen so far (to fix in litestl)

- **Strings and JS arrays can't cross the generic method binding** (`marshalArg`).
  Sculptcore works around this with dedicated `extern "C"` seams per call
  (`napi_spatial_multires.cc`, `napi_mesh_vdm.cc`). Generic `string`, `Span`, `Vector`
  and typed-array marshalling is needed.
- **Vector out-params** need hand-written `makeXVector` helpers. This matches litestl's
  own TODO (find `Vector` template instantiations, and create and free them
  automatically).
- Raw C N-API only, because node-addon-api's `CallbackInfo` miscompiles under clang on
  Windows (per the Sculptcore spike). Keep it that way.
- Opaque engine objects cross as externals without GC finalizers, by design in
  Sculptcore. UberTopo needs an explicit ownership policy per bound type.

### Plan implication

Before any UberTopo UX work there is a **litestl N-API migration task**: move the generic
runtime from Sculptcore into `litestl/binding/napi/`, generalise the marshalling, and
switch Sculptcore to it. That will be its own plan in `docs/plans/`, pressure-tested
before execution. It touches Sculptcore too, so it needs coordination.

---

## DEC-006 — Follow Sculptcore: NW.js, defer the N-API migration and Python; SoA B-rep modelled on Sculptcore

- **Date:** 2026-11-17
- **Decision:**
  - **Chromium shell = NW.js**, following Sculptcore (Electron kept as a fallback, as
    Sculptcore does).
  - **The N-API migration into litestl is deferred.** It happens later, not as a
    prerequisite of early UberTopo work. Until then, UberTopo has no UX or N-API
    dependency.
  - **Python is not needed yet.** Q-E is deferred.
  - **The B-rep uses a struct-of-arrays approach similar to Sculptcore's**, and
    Sculptcore's mesh (`source/mesh/`) may be used as a reference. See
    [research/sculptcore-brep-reference.md](research/sculptcore-brep-reference.md) for
    what carries over, what must change, and what must be added: regions/shells,
    face sides, inclusions, persistent IDs, angular radial order, COW pages, f64 or
    exact coordinates, geometry pools.
- **Answers:** Q-E (deferred), Q-F (later), Q-G (NW.js).

---

## DEC-007 — Coordinate type as a template parameter; C++20 concepts; open, modular attribute system

- **Date:** 2026-11-17
- **Decisions:**
  1. **The coordinate numeric type is a template parameter** of the topology/geometry
     core rather than a fixed `float`/`double`. **C++20 concepts** constrain template
     parameters throughout the codebase, rather than SFINAE or unconstrained templates.
  2. **Ordering faces around an edge is a solved, local problem**, not a design driver.
     Sort the radial cycle by angle around the edge on insert, or in a frozen-mode
     rebuild.
  3. **The attribute system is modular and open.** Custom attribute classes can be
     added as needed without touching core code. This relaxes Sculptcore's design, where
     attributes come from a closed set fixed at compile time and are dispatched through
     `type_dispatch` switches and specialisation trampolines to avoid virtual calls.
     Core attribute code does **not** need a switch or trampoline covering every valid
     type, and some dynamic dispatch is acceptable.

### Implications

**Coordinate template parameter (resolves the shape of open decision D1):**
- **Instantiations instead of a choice.** Instead of choosing "f64 + tolerances" *or*
  "exact integer lattice", the core is generic over a `Coord` concept. Each tile or
  complex is instantiated with one type: `double` for imported or scanned content, an
  exact or lattice type for generated planar content, and `float` for light tools and
  tests. D1 now means "which instantiations exist, and which is the default for which
  content".
- **Predicates come with the type.** The concept must carry the geometric predicates,
  not just arithmetic. Orientation and in-circle tests are exact for integer and lattice
  types and adaptive-exact for `double`. A traits or concept pair (sketch below) keeps
  algorithms such as arrangements and CDT written once.
- **Wider intermediates.** Exact types need intermediate types: products widen, e.g.
  int64 → int128 → 256-bit (the wide-integer types going into litestl per DEC-003). The
  concept must expose them.
- **Explicit conversion between complexes.** Two complexes with different `Coord` types
  can't share topology directly. Moving between them (snapping imported data onto a
  lattice, exporting doubles) is an explicit, journaled operation. This matches the
  "import/snap operation that records its tolerance" resolution suggested for D1.
- **Attributes are not templated.** Attributes, IDs and topology columns stay
  independent of `Coord`. Only position or plane columns and geometry pools are
  parameterised. This keeps template bloat to the geometric layer.
- **Bindings.** The binding system binds concrete types, so each exported
  instantiation is registered explicitly (e.g. `Complex<double>`). This ties in with
  litestl's TODO about finding template instantiations for `Vector`.

```cpp
// Sketch only: names are illustrative, not decided.
template <typename T>
concept CoordScalar = requires(T a, T b) {
  { a + b } -> std::convertible_to<T>;
  { a - b } -> std::convertible_to<T>;
  { a * b };                                   // may widen
  { CoordTraits<T>::is_exact } -> std::convertible_to<bool>;
  typename CoordTraits<T>::Wide;               // widened type for products
};

template <typename T>
concept ExactPredicates = CoordScalar<T> && requires(const math::Vec<T, 3> &p) {
  { orient3d(p, p, p, p) } -> std::same_as<int>; // sign only; exact or adaptive-exact
  { orient2d(p, p, p) } -> std::same_as<int>;
};

template <ExactPredicates Coord> struct Complex { /* SoA domains, positions: Vec<Coord,3> */ };
```

**Open attribute system:**
- **Extension point.** A polymorphic attribute *column* base class (virtual `resize`,
  `swap`, `reorder`, `copy`, `interpolate/merge`, `serialize`, `default`) replaces the
  closed `AttrType` enum plus `type_dispatch`. New attribute classes register a factory
  under a type key, e.g. acoustic band arrays, material handles, geometry-pool slots,
  persistent IDs.
- **Where virtual calls happen.** Dispatch is per column operation (once per column per
  topology op or bulk pass), not per element. Hot loops get the typed column through a
  checked downcast (`group.get<AttrData<T>>("name")`), so the cost is negligible.
- **What stays.** Paged storage, lazy page materialisation and copy-on-write pages stay
  generic in `AttrData<T>`. Packed bools can remain a specialised column class.
  Merge/interpolation policy becomes a per-column hook rather than a central switch.
- **Schema metadata.** Each registered attribute class or instance carries unit, role,
  frame and quantity kind (see
  [attributes-and-queries/recommendations](research/attributes-and-queries/recommendations.md)).
  Registration and metadata use litestl's binding descriptors where possible, so that
  custom attribute classes are also bindable.
- **Q-H, partly answered.** The new system is a **modular redesign modelled on**
  Sculptcore's attributes, not a straight move of them. Whether Sculptcore later adopts
  it is left open.

---

## DEC-008 — Core data model open issues resolved; litestl push access; source file size rule

- **Date:** 2026-11-17
- **Decisions** (on [design/core-data-model](design/core-data-model/README.md)):
  - **OI-1 (exact representation):** accept the lean. Store `Lattice` vertices plus an
    exact plane table, and use plane-based homogeneous points only inside the Boolean
    engine. **Re-review in phase P5**, where the prototype will confirm or overturn it.
  - **OI-2 (regions):** accepted. Shells and regions are derived and rebuilt by
    `build_regions()` after construction batches, with incremental maintenance only in
    trivial operators.
  - **OI-5 (generic attribute machinery):** goes into a new **litestl `attr/`** module
    from day one.
  - **litestl push access** granted. Work is pushed to litestl feature branches, and the
    submodule pointer is bumped in UberTopo.
  - **New rule:** source files over **40 KB** are split, with an intelligent
    subdirectory layout where needed. Auto-generated files are exempt. Added to
    `CLAUDE.md`.

---

## DEC-009 — Hash, canonical tuple encoding and RNG layout (resolves OI-4)

- **Date:** 2026-11-17, decided while pressure-testing the
  [P0 plan](plans/2026-11-17-p0-litestl-foundations.md).
- **Hash:**
  - **SipHash-2-4 with 128-bit output**, as defined by the reference implementation, is
    re-implemented in litestl as `litestl::hash::Hash128` / `Hasher128`.
  - It uses a public fixed key, so it provides no adversarial collision resistance.
    That is acceptable for this project.
  - P1 may add a faster function for page content behind the same `Hasher128`
    interface if measurements require it.
- **Canonical tuple encoding:**
  - Every `Hasher128` starts with a mandatory **`DomainTag`**, which separates
    generator IDs, operator-derived IDs and content hashes.
  - Every encoder writes a **1-byte type tag** followed by a little-endian payload.
    Strings are u64-length-prefixed.
  - `f64_canonical` (−0 → +0, NaN → `0x7ff8000000000000`) is used **for identity**.
    `f64_bits` and `raw_bytes` are used **for content hashes**.
- **RNG:**
  - The generator is **Philox4x32-10** in `litestl::rng`.
  - The counter layout is pinned as `{elem_lo32, elem_hi32, draw, block}`.
  - `range()` rejection advances `block`, never `draw`.
  - The Philox key is `stream_key.lo ^ mixHash(stream_key.hi)`, so all 128 bits of
    the stream key matter. The plan's original "high half salts `ctr[3]`" was
    replaced during implementation, so that `ctr[3]` stays a full 32-bit block
    index.
  - Persistent IDs fold to 64-bit elements; the fold is documented.
- **Pinned goldens.** ID-derivation, RNG-stream and determinism-digest goldens are
  pinned in litestl tests. **Changing any of them requires a new DEC entry**, because
  it changes all procedural output.

---

## DEC-010 — P1 attribute core: API decisions, shared-code fixes, pools to P1b, page size

- **Date:** 2026-11-18, from the [P1 plan](plans/2026-11-18-p1-attribute-core.md)
  (v3 with §4b amendments) and its implementation.
- **API decisions** (see litestl `documentation/attributes.md`):
  - The page size is a runtime per-store setting. **The default is 4096 elements**: in
    the W8 benchmark, 12 and 14 tied on read speed and 12 copied about 40% fewer bytes
    per scattered copy-on-write edit. This closes OI-3.
  - Equality is bitwise.
  - Defaults are fixed at construction.
  - The last page has partial capacity.
  - Builtin columns are reached by slot index.
  - Mixing is simultaneous, with validated batches, `mix_from`, `Custom` with a
    pre-call snapshot, and claimed slots. Topo columns are never group-mixed.
  - Version stamps come from a process-wide counter.
  - The registry is explicit and locked.
  - The canonical hash and run-encoded bytes are independent of page size,
    representation and `ensure`/`remove` history. Temp and Derived columns are excluded.
  - `BoolColumn` uses one byte per element; bit packing is a later optimisation.
- **Fixes to shared litestl code**, which Sculptcore should rebuild against once when it
  bumps litestl:
  - `math::Vec` has a defaulted copy constructor and is now trivially copyable.
  - `BindingBase` has a virtual destructor.
  - An out-of-bounds read inherited from Shewchuk's `fast_expansion_sum_zeroelim` is
    bounded; ASan found it.
  - An LSan suppression file covers intentional process-lifetime allocations.
  - **`util::Vector::remove_at`** no longer move-assigns into a destroyed element. That
    was undefined behaviour and double-freed owning element types; TSan found it.
  - **`alloc::NewArray`** now constructs every element, from unmoved arguments
    (owner-requested fix; there are no existing callers).
- **Intern pools are deferred to P1b.** Their requirements are recorded in design 05.
- **New pinned golden:** `test_attr_digest` (`eb4c52c53316afaf23dbdc3cd35d0630`). It
  is separate from the P0 digest, which is unchanged.

---

## DEC-011 — litestl P0/P1 work merged to `master`

- **Date:** 2026-11-18. The owner asked for the merge.
- **What happened:** litestl `ubertopo/p1-attr`, which contains all of
  `ubertopo/p0-foundations`, was **fast-forward merged into litestl `master`** at
  `b11fa31` and pushed. `master` hadn't moved since the branches were cut (`765241e`).
  The UberTopo submodule link (`source/litestl`) records `b11fa31`, which is now
  reachable from `master`, and `.gitmodules` tracks `branch = master`.
- **Effect on Sculptcore:** it picks up these changes when it next bumps its litestl
  submodule:
  - the P0 numerics;
  - the attr module;
  - the shared-code fixes: `Vec` trivially copyable, `~BindingBase`,
    `Vector::remove_at`, `NewArray`, and the predicate bounds fix;
  - the test infrastructure.

  Rebuild and test once at that point, especially on the Windows clang toolchain,
  which couldn't be tested here.
- **Future litestl work** continues on feature branches (CLAUDE.md rule) and is merged
  when the owner asks.

---

## DEC-012 — Compile-time attribute schemas; no manual slot indexing

- **Date:** 2026-11-18. The owner required that named attributes be associated with
  their names at compile time, so that errors in attribute shapes known at compile time
  become compile errors. Sculptcore does this with a struct of `BuiltinAttr` members.
- **Decision.** litestl `attr/schema.h`:
  - **`AttrDef<"name", Column<T>, Flags>`** declares an attribute once: name, column
    type, flags and default. A custom default comes from shadowing `default_value()` in
    a derived struct.
  - **`Schema<Defs...>`** is an ordered list of definitions, with `extend<...>` to
    append more.
  - **`TypedGroup<Schema>`** registers the definitions as builtins. Access is
    `get<"name">()`, `get<Def>()` and `slot<"name">()`, all resolved at compile time.
  - **Optional attributes** reuse definitions through `attr::ensure<Def>(group)` and
    `attr::find<Def>(group)`.
- **Compile errors:** an unknown name, duplicate names, a definition not in the schema,
  a non-column type, and a wrong column type. litestl ctests check each with a
  compile-fail snippet that must produce the expected diagnostic.
- **Rule:** UberTopo code declares builtin attributes through schemas and never
  indexes builtin slots by hand (`CLAUDE.md`).
- **Merged:** at the owner's request, litestl `master` was fast-forwarded to `26447b2`
  (from `ubertopo/attr-schema`) and UberTopo `master` to match.

## DEC-013 — P2a: cell-complex core API decisions and goldens

- **Date:** 2026-11-18. Implements P2a of the
  [P2 plan](plans/2026-11-18-p2-cell-complex.md). The realised API is
  [design 07](design/core-data-model/07-topology-api.md).
- **Decisions:**
  - **Link flags.** Every topology link column is `Topo`. litestl's `Derived` flag is
    reserved for freeze caches, which are excluded from hashes and bytes.
  - **Bookkeeping columns.** `.id`, `.gen` and `.live` have the same names in every
    domain.
  - **Freeing a slot** resets every column except `.gen`, then bumps `.gen`.
  - **Region hints.** Region containers named by generator hints are stored as
    `*.container_hint` IDs until `build_regions()` resolves them.
  - **`ElemKind`** replaces the design's `Domain` enum name.
  - **`Lattice`** is a strong `int64` with `+`, `-` and negation only. `Wide` is dropped
    from `CoordTraits`. `orient3d_ext` (30-bit `Lattice` range) serves constructed
    points: ray far points, and interior test points scaled by 4.
  - **The builder is all-or-nothing on errors.** Its output is a function of the
    emitted set: sorted-key dense indexing, with `vs[0]` the smaller vertex ID and disk
    cycles in edge order. The radial cycle starts at the smallest face ID.
- **litestl additions** (branch `ubertopo/p2-support`):
  - `util/sort.h` (`sort_unique_keys`, which checks for ties in every build, and
    `stable_sort`);
  - `task::parallel_exclusive_scan`;
  - `AttrGroup::hash_into(h, filter)` and `AttrGroup::version()`;
  - `Vec` zero-fill with `T(0)`;
  - `Vector::data() const`.
- **Goldens.** The persistent-ID derivation goldens in `test_ident.cc` join the
  determinism contract.

## DEC-014 — P2b: regions, validation, freeze, opt-in projected faces with a canonical CDT

- **Date:** 2026-11-18. Implements P2b and amendment A1 of the
  [P2 plan](plans/2026-11-18-p2-cell-complex.md). The realised API is
  [design 07](design/core-data-model/07-topology-api.md).
- **Owner decisions:**
  - **Opt-in projectable faces.** Non-planar faces are accepted only when client code
    sets `BuildPolicy::allow_projectable_faces`. Such a face must project to a simple
    polygon with holes along an axis-aligned plane, and its geometry is the
    triangulation of that projection, not a best-fit plane.
  - **The triangulation is a canonical exact CDT**, chosen over ear clipping and built
    in P2b. It is also the intended render triangulator.
  - **Degenerate `double` shells are reported**, not computed exactly (§9.1 of the
    plan).
  - **litestl is merged after P2b.**
- **Decisions:**
  - **Projected faces**: the `ProjAxis` flag bits (`(axis + 1) << 4`) are set only by
    the builder. The opt-in is recorded on the complex (`.g.allows_projectable`, a
    global builtin) and checked by the validator.
  - **CDT uniqueness** comes from Simulation of Simplicity with lexicographic ranks.
    litestl gains `pred::incircle_sos`.
  - **Regions:** union-find shells; sheet shells; exact or certified signed volumes;
    the shared-face rule plus exact ray winding for nesting; the innermost container
    by volume; region IDs from hints or the minimum face side; region attributes
    carried over by ID.
  - **The Euler–Poincaré cross-check** is a per-vertex planarity test of the link
    rotation system, so it needs no generator ground truth.
  - **Freeze:** Morton order over the tile frame's fixed extent; a remap table with
    compile-time coverage; the generation rule `max(own, slot's old) + 1` for moved
    elements; frozen caches that aren't serialised.
  - **Hashes.** `topology_hash` leaves out positions and the coordinate key, so it is
    equal across `double` and `Lattice`.
- **Goldens.** The topology hash of the fixed model in `test_topo_determinism`
  (`{0x98c378924d246f78, 0x28007637b0e58ee8}`) joins the determinism contract.
- **Merged:** after P2b, at the owner's request, litestl `master` was fast-forwarded to
  `ccf0386` (from `ubertopo/p2-support`). The full matrix passed first. UberTopo
  `master` was then fast-forwarded to the P2 branch.
- **Known limits:**
  - **Handle resolution** across versions: slots that are dropped and later
    re-allocated restart at generation 0.
  - **Edit rules** for projected faces wait for P3. On those faces the triangulation is
    the geometry, so any loop edit changes it.

## DEC-015 — P1b: value pools with mark-and-sweep liveness

- **Date:** 2026-11-18. Implements the [P1b plan](plans/2026-11-18-p1b-value-pools.md)
  (v3, after two fresh-context reviews). The code is in litestl `attr/`: `pool_ref.h`,
  `pool_slots.h`, `pool.h` and `pool_store.{h,cc}`. The guide is litestl
  `documentation/attributes.md` ("Value pools").
- **Owner decision: mark-and-sweep, not refcounts.** This reverses design 05's
  refcount funnel. The first review showed that per-snapshot refcounts plus column→store
  binding caused three blockers and six majors: read into an owner, group assignment,
  snapshots inheriting journal counts, unbound columns, and parallel permute.
- **Decisions:**
  - **Refs are plain values.** A ref column is an ordinary `Column<PoolRef>`
    (8 bytes, `pool == 0` means none, key `"ref"`). The `ColumnBase` structural API is
    unchanged. Custom columns holding refs override six ref hooks, a per-column virtual
    as DEC-007 requires.
  - **`collect(roots)`.** Liveness is decided only by `collect(roots)`. The roots are
    the ref columns of the given groups, the `RefRoot` extras (the P3 journal is one)
    and the inner refs of reachable entries. Owners collect at deterministic points.
    Validation (dangling, malformed or refused refs; non-`kNoRef` defaults) runs while
    marking, and a failure changes nothing.
  - **Every add interns.** Ref equality is value equality within a pool.
  - **Snapshots.** `PoolStore` is a value type. Copying it is O(slot pages): slot pages
    and a two-level (base plus delta) dedup index are copy-on-write, and entry nodes are
    shared through an atomic `holders` count. Owners keep defaulted copy and move.
  - **Canonical form.** Three-phase first-reference renumbering: hashed columns, then
    Temp and Derived, then extras, each with its own inner-ref closure. Bytes and hashes
    cover only the hashed closure. `write` and `hash_into` require
    `canonical(roots)`, meaning neither the store nor any root group changed since
    `canonicalize`.
  - **Owner I/O.** Owner write canonicalises a copy. Owner read is strict: a fresh
    owner, then audit, and `canonicalize` must report no change.
  - **Pool ids** are the owner's registration order, and new kinds are appended.
    Remapping by key is deferred until a plugin system exists. P4's `GeomRef.kind` is
    the pool id.
  - **Between stores.** `import` and `import_rows` re-intern deeply between stores. A
    raw ref copied across stores is meaningless, and that rule is documented.
  - **Weak roots.** `RefRoot::prune` runs between mark and sweep. `PoolStore::version()`
    changes whenever slots may have been reused.
- **Goldens:** `test_attr_pool_digest` =
  `{0x73283dc9b10b45d0, 0x7a0914cd7544d6fa}`, identical at slot page shifts 2 and 10.
  The P0, P1 and P2 goldens are unchanged.
- **Merged:** on 2026-11-18, at the owner's request, litestl `master` was fast-forwarded
  to `608afca` (from `ubertopo/p1b-pools`) and UberTopo `master` to `cf999cf` (from
  `session/p1b-value-pools`), after the full matrix passed.
- **Also in litestl:** `util/memory.h` gained its missing `#pragma once`;
  `hash::DomainTag::PoolEntry`; `AttrGroup::canonical_order()` is public; and the
  test-only `ReadLimits::verify_hash`.
- **Known limits:**
  - **Forgotten roots** are detectable only before their slot is reused, or under the
    test quarantine.
  - **Snapshot copy cost.** Copying is linear in pages, but at 10⁶ entries cache and
    TLB misses make it about 17–20× the cost at 10⁵ (plan §11).

## DEC-016 — P3a editing: physical undo, derived regions, local validation

- **Date:** 2026-11-18
- **Status:** accepted (implemented in P3a; plan
  [2026-11-18-p3-editing.md](plans/2026-11-18-p3-editing.md) v2, fresh-context review
  recorded there).
- **Decisions:**
  - **Physical undo.** Undo is physical (row pre-images, allocator events, group sizes);
    redo re-executes recorded operations. A failed or rolled-back edit is a
    bit-identical no-op. Region rebuilds are barrier transactions with copy-on-write
    snapshots.
  - **Regions stay derived.** `set_region`, `make_region` and `kill_region` are
    dropped. Face edits set `regions_dirty`, and `rebuild_regions()` is the only writer
    of shells and regions. Side hints name regions; embedding into a region writes its
    hint.
  - **Canonical allocation.** `Domain::alloc` takes the lowest free slot instead of the
    last freed one, so allocation sequences don't depend on whether a complex was
    re-read.
  - **Projected faces (DEC-014, A1-R1 settled).** T(f) stays derived. Edits keep the
    projection axis and re-derive the radial cycles of every edge of an edited
    projected face. A projected face that becomes planar drops its axis. A planar face
    never becomes projected through an edit.
  - **Validation.** Every primitive validates: `Full` (the oracle) or `Local` (the whole
    combinatorial layer plus the geometric layers restricted to the touched
    elements). The random test checks that both agree.
  - **The journal is a pool root** (`JournalRoots`, DEC-015). Ref columns are found
    again by recorded name and type.
- **Goldens:** `test_edit_random` (seed 1, 600 steps, `double`) =
  `{0x09421988ba58a022, 0x96f79940201fa554}`. The P0–P2 and P1b goldens are unchanged.
- **Deferred to P3b:**
  - compaction and a generation floor (the DEC-014 limit stands);
  - `imprint`, `add_hole`, `merge_vertices`, `connect_loops` (DEC-021);
  - incremental region maintenance (OI-P3-1);
  - container hints outvoted by a region merge (OI-P3-2);
  - wire–face crossing checks.

---

## DEC-017 — P3b editing: stable hinted region IDs, generation floor, filtered validation

- **Date:** 2026-11-18
- **Status:** accepted (implemented in P3b; plan
  [2026-11-18-p3b-editing.md](plans/2026-11-18-p3b-editing.md) v3, two fresh-context
  reviews recorded there).
- **Decisions:**
  - **Hinted region IDs come from the hint.** A region whose winning hint no other
    region wins gets `region_id_from_hint(h)`. Splitting its faces therefore doesn't
    rename it, and a rebuild keeps its attributes. Regions that share a winning hint
    keep `region_id(h, min side)`. Stability holds for single-hint regions only (plan
    C8).
  - **Container hints resolve through carriers (OI-P3-2 closed).** A hint is resolved
    in this order:
    1. the unique region that wins it;
    2. if several regions win it: ambiguous;
    3. if none does: the unique minority carrier other than region 0.
  - **Generation floor (closes the DEC-014 limit).**
    - Slots dropped by compaction, freeze or a region rebuild never revive an old
      handle.
    - Appended slots start at the floor.
    - UTCX format 2 serialises the seven floors. Format 1 reads with floors 0.
  - **`compact` is a journaled barrier**, refused inside a transaction, like
    `rebuild_regions`.
  - **Derived editor state is kept by dirty rows.** The ID and inclusion index and a
    persistent face-box grid are both maintained this way. `touch`, `alloc` and
    `restore` mark rows, and barriers rebuild both eagerly.
  - **Validation:**
    - `Local` validates the touched rows and their one-ring in all three layers.
    - The new `OnCommit` mode validates everything an outermost transaction touched,
      once, at commit.
    - Redo never validates. `verify_redo` (on by default in debug builds) compares a
      content hash stored at commit.
    - The validator reports wires crossing faces (`WireCrossesFace`).
  - **New primitives:**
    - `imprint` / `unimprint`;
    - `add_hole` / `remove_hole`;
    - `weld`, which merges vertex pairs and fuses the parallel edges that result.
- **Goldens:**
  - `test_edit_random` (600 steps, `double`):
    - seed 1: `{0xd232bb3c173ca918, 0xf365153588a08e2c}`;
    - seed 2: `{0x75bccfc9fd30c4fb, 0x0f75f4b7fd6a4b1c}`;
    - seed 3: `{0x7c4ccbaea916883a, 0xacddf65e683a064a}`.
  - Seeds 2 and 3 end with hinted rooms, so they cover region IDs.
  - P2 determinism: `{0x824b09e6bbd23a72, 0xf96f12ef6fe48699}`.
- **Open:**
  - OI-P3-1 (incremental regions), narrowed;
  - OI-P3b-1: a cycle around existing holes is refused;
  - OI-P3b-2: unhinted regions are renamed when their minimum side changes.

---

## DEC-018 — P4a: curves, the geometry store in `Complex`, curved edges

- **Date:** 2026-11-18
- **Status:** accepted (implemented in P4a; plan
  [2026-11-18-p4a-curves.md](plans/2026-11-18-p4a-curves.md) v2, fresh-context review
  recorded there).
- **Decisions:**
  - **The curves library** is `source/ubertopo/curves/`, namespace `ubertopo::curves`.
    - Curves evaluate in `double`, in the tile frame's local coordinates (the values of
      `geom::to_double3`: metres for `double` and `float`, quanta for `Lattice`).
    - Nothing in it is templated on the coordinate type.
    - Kinds: `cesaro2` (κ laws of degree ≤ 3), `offset2`, `alignment3`, `catenary3`,
      `polyline3`, `bspline`. Dispatch is virtual per kind.
  - **No libm on curve paths.** litestl's `math/detmath.h` provides sin, cos, exp,
    log, sinh, cosh and asinh. It gives the same bits under clang, gcc and MSVC, and
    each function has a stated ulp bound.
  - **Certified bounds.** Every kind reports `eval_error`, `deriv_eval_error(k)` and
    `deriv_bound(t0, t1, k)`, proved for exact arithmetic plus an explicit allowance for
    every rounding, and checked against a quad-precision oracle.
    - Box bounds, flattening and the G2 quintic Hermite approximation are generic
      algorithms over these.
    - The approximation's knot multiplicity follows the source's continuity.
  - **The geometry store.** `GeomRef` is `attr::PoolRef`. `Complex` holds a `PoolStore`
    with the kinds registered in a fixed order.
    - `.e.curve` takes 3D kinds only. Pcurves and surfaces are refused until P4b.
    - New Topo columns: `.e.range`, `.e.geom_flags` and `.g.curve_tol` (default 2⁻³⁰
      frame units).
  - **One hash path.** `content_hash` and `topology_hash` see refs through the store's
    read-only canonical map, so they equal the hashes of the canonicalised complex
    that `write` emits. Unreferenced entries never change a hash.
  - **UTCX format 3** writes the canonical store after the floors, from a canonicalised
    copy. `read` audits, canonicalises (which must change nothing) and checks the
    trailing hash. Formats 1 and 2 still read.
  - **Edge carriers.** `.e.curve` = ref and `.e.range` = (t₀, t₁) put `vs[0]` at
    carrier(t₀) and `vs[1]` at carrier(t₁).
    - An end vertex must lie within eval_error + √3·rounding_C(p) + curve_tol of the
      carrier.
    - The skeleton validator keeps checking straight chords (OI-P4a-1).
  - **The editor never renumbers the live store.**
    - `intern_curve` is not journaled.
    - `set_edge_curve` and `split_edge_at` are journaled.
    - Barrier undo and rollback restore the groups and keep the store.
    - `JournalRoots` roots every ref the journal holds.
    - Collects are explicit: `collect_geometry()` outside transactions, and every
      barrier after it commits.
- **Goldens:**
  - `test_edit_random` (600 steps, `double`, with curve steps):
    - seed 1: `{0x7ce5ffd466d9054b, 0x670cb9e60e3d5bb2}`;
    - seed 2: `{0x6c72c409e108ce3b, 0x65dcb1c54131ecf4}`;
    - seed 3: `{0xaa60a8a7e5aa9661, 0xe6a3903c669fef9a}`.
  - P2 determinism: `{0x474f0ef45988d192, 0x4e944de540b23ca0}`.
  - The W5 switch was checked with a structure hash that leaves out geometry columns,
    pinned with the format-2 code and unchanged by the switch.
- **Open:**
  - OI-P4a-1: curved geometry is validated on its chord skeleton, and there are no
    curved digons;
  - OI-P4a-2: no automatic collect;
  - OI-P4a-3: κ laws of degree ≤ 3 only;
  - OI-P4a-4: `detmath` speed is reported, not gated;
  - ~~OI-P4a-5~~ (closed): the `cesaro2` bound's terms are now tested one by one
    against long-double recomputations. The tests found NaN piece bounds on long pieces,
    which are fixed (plan §11).

---

## DEC-019 — P4b: road surfaces, pcurves, the refined skeleton, a road on terrain

- **Date:** 2026-11-21
- **Status:** accepted (implemented in P4b; plan
  [2026-11-18-p4b-surfaces.md](plans/2026-11-18-p4b-surfaces.md), fresh-context review
  recorded there). The rest of P4a's "P4b" list moved to P4c (plan §1).
- **Decisions:**
  - **The acceptance case is hole and fill (owner).** The terrain cells a buffer around
    the road overlaps are removed. The road's faces carry a `road3` surface and
    pcurves, and one projected fill face joins them to the terrain: its outer loop is
    the hole's boundary and its hole loop is the road's outline.
    - Literal imprinting would need vertices on both a straight terrain edge and a road
      border, which only exist if the road follows the terrain exactly.
    - Daylight lines found by slope are P4c.
  - **`road3`**, a ruled road surface: S(s, t) = r(s) + t·N(s) + Z(s, t)·e_z.
    - **t is a horizontal offset (owner).** r and N are the `cesaro2` reference's point
      and left normal, so border k's footprint is `offset2(reference, b_k)` exactly.
    - Z interpolates linearly in t between the border heights Z_k(s). Crossfall, crown
      and superelevation are expressed through the Z_k.
    - Border k's edges carry `alignment3(offset2(reference, b_k), Z_k)`. The
      evaluation is pinned so that the border lies on S bit for bit.
    - Each link (the strip between two borders) is one piece.
  - **Pcurves** are `graph2` (t = f(s)) on a curved edge's coedge and `segment2` on a
    straight edge's.
    - **Same parameter:** a curved edge's pcurve takes the edge's carrier parameter; a
      straight edge's takes λ ∈ [0, 1] from `vs[0]` to `vs[1]`. So `split_edge_at` and
      `join_edge` keep pcurve refs unchanged on curved edges.
    - Every coedge of a surfaced face has a pcurve, and no other coedge does.
  - **Validation is certified, not exact.** New checks: `SurfaceKind`, `PcurveKind`,
    `PcurveRange`, `SurfaceLink`, `PcurveVertex`, `PcurveDeviation` (structural for
    road borders, by a second-order bound otherwise) and `SurfaceOrientation`.
    Tolerances follow P4a's endpoint rule.
  - **Surfaced faces may be planar or projected.** The surface is the face's geometry;
    the skeleton classification only drives the topological tests.
  - **The refined skeleton.** A curved edge contributes its deterministic flattening
    at `.g.skeleton_tol` (default 2⁻⁶ frame units) to every test that reads face loop
    points: projection and T(f), radial order, contacts, wire–face tests, containers,
    regions, face boxes and the editor's grid.
    - Flattening points are rounded to `C`, numbered after the stored vertices in one
      table per pass (`Skeleton<C>`), and never stored or hashed.
    - Planarity is still decided on the stored vertices (OI-P4b-7).
  - **The editor.** `intern_geom` (P4a's `intern_curve`) takes every kind;
    `set_face_surface` and `set_coedge_pcurve` are journaled.
    - Edits that would need pcurve rules on surfaced faces are refused with
      `SurfacedFace`: `split_edge` and `join_edge` on straight edges, `split_face`,
      `join_faces`, `imprint`, `unimprint`, `add_hole`, `remove_hole` and `weld`.
    - `split_edge_at` and curved `join_edge` keep the pcurves.
  - **UTCX format 4.** The new kinds are appended to the store and `.g.skeleton_tol`
    joins the global schema. Format 3 reads through a migration that checks its hash
    over the first 6 pools (litestl `PoolStore::hash_into` with a pool limit).
  - **The topology hash leaves out `.g.skeleton_tol`**, which is in frame units, as it
    leaves out positions.
- **Goldens:**
  - `test_edit_random` (600 steps, `double`):
    - seed 1: `{0x43da9c3224b285d1, 0x11a35c20a48e069f}`;
    - seed 2: `{0x339f00b89b89cc36, 0x14bd7d328afdb162}`;
    - seed 3: `{0x0071f444ac2078f3, 0xdde9a7286c002ad9}`.

    Re-pinned in W1 (three pools and a global; structure goldens unchanged) and in W2
    (behaviourally, the refined skeleton).
  - P2 determinism topology: `{0x2a499b7b7287dea1, 0x2505140161e20813}`, its structure
    golden unchanged.
  - Road on terrain, content after placement:
    - `double`: `{0xb1593a8a3b173e70, 0xb2acc08bad884e5c}`;
    - `Lattice`: `{0xa5b9cd7c66cd53cb, 0x0764de8ea7e76939}`.

    The two builds have equal coordinate-free structure hashes.
- **Open:** OI-P4b-1 to OI-P4b-7 (plan §10):
  - OI-P4b-1: contacts and regions are decided on the skeleton, within skeleton_tol of
    the true geometry;
  - OI-P4b-2: few edits on surfaced faces;
  - OI-P4b-3: lane widths are horizontal, not along the tilted surface as in
    OpenDRIVE;
  - OI-P4b-4: the fill face is the CDT of its boundary, not an embankment design;
  - OI-P4b-5: skeleton_tol is in frame units, so `Lattice` generators must set it;
  - OI-P4b-6: no inversion queries;
  - OI-P4b-7: a planar face's curved carriers aren't checked against its plane.

---

## DEC-021 — `connect_loops`: a cut through holes is one primitive

- **Date:** 2026-11-24. Plan:
  [2026-11-24-connect-loops.md](plans/2026-11-24-connect-loops.md). (DEC-020 is the BVH
  plan's.)
- **Changed from design 01 §7.** That listed an edge from a hole to the outer loop, with
  one face on both sides, to be used only transiently. The validator, the radial order and
  the skeleton all rule that out, and nothing needs it. Instead
  `connect_loops(f, path)` makes the whole cut at once, so the complex is valid before and
  after.
- **`disconnect_loops(e)`** joins faces that share several separate edges, from any of
  them. `join_faces` keeps its one-edge case and `unimprint` its chain case.
- **Two fixes came with it:**
  - an embedded wire beside a cut is placed by the loops its ends are on, so the answer no
    longer depends on the cut's direction;
  - `unimprint` finds a face's outer loop geometrically rather than taking its first
    loop, and `remove_hole` can remove a hole that comes first.
- **Open:** OI-CL-1 to OI-CL-3 (plan §6): cuts between holes only, cuts along wires or
  curves, and surfaced faces.

## DEC-022 — Exact Booleans: a CSG tree, exact arrangement, rounding

[Plan](plans/2026-11-25-exact-booleans.md) §7; API: [08](design/core-data-model/08-booleans.md).

- **OI-1 (DEC-008) holds:** integer vertices and the engine's own plane table, rebuilt per
  evaluation; homogeneous integer points in the engine only. No stored plane pool.
- **Modelling frame:** a CSG tree is modelled in its object's frame, and one rotation is applied when
  the result is rounded, so flush contact survives any orientation.
- **Rounding** fails visibly (`RoundingFailed`), never silently, and a finer output quantum is the
  first remedy. Buildings: none of 3000 models fails at 2^-13 m, shift 0, in any of three orientations.
  Random tetrahedra: 4.5% at shift 0, 0.4% at shift 4.
- **Repair after rounding** (OI-BOOL-2, S7; [plan](plans/2026-11-25-boolean-repair.md)): the rounded faces
  are arranged again exactly, and cells get the label with the largest winding number. On by default,
  and reported. 5710 of 5711 failures of the benchmark are repaired. The result is valid, but it isn't
  the exact one's topology: features under a quantum can vanish.
- **The CSG tree is the stored model** (owner's request). Solids are derived, and never operands.
- **Names** are made of source elements, and sorted by ID before hashing, so the order of children
  matters at coincidences only. Regions are named by labels.
- **Open:** S5 (speed) waits for the face BVH, and the comparison with Manifold or CGAL (OI-BOOL-1) is put off
  (owner, 2026-11-25).

---

## Open questions

- ~~Q-A — WASM target~~ → DEC-005 (native N-API in a Chromium shell).
- ~~Q-B — where general-purpose additions live~~ → DEC-003 (litestl; B-splines in
  UberTopo).
- ~~Q-C — offline converters~~ → DEC-004 (third-party converters allowed).
- ~~Q-D — how litestl is included~~ → DEC-003 (submodule at `source/litestl`).
- ~~Q-E~~ deferred by DEC-006 (Python not needed yet). Original text: **Python binding runtime**. litestl's Python generator emits
  `.pyi` stubs only. Should native Python go through a CPython extension module
  generated from the same descriptors as the N-API runtime, or ctypes/cffi over a
  C ABI? Is Python needed early at all?
- ~~Q-F~~ → DEC-006 (migrate N-API later). Original text: **Sculptcore coordination.** Can the N-API migration modify Sculptcore in the
  same effort, or should litestl gain the runtime first and Sculptcore switch over
  later?
- ~~Q-G~~ → DEC-006 (NW.js). Original text: **Chromium shell.** NW.js (Sculptcore's default) or Electron? Sculptcore keeps
  both, so we could follow it.
- **Q-H — Sculptcore's attribute system** *(partly answered by DEC-007: new modular design modelled on Sculptcore; Sculptcore adoption open)*. Should Sculptcore's paged attribute system
  (`AttrData`/`AttrGroup`/`BuiltinAttr`, plus the `DeformPool` intern pattern) be
  **moved** into litestl and generalised, with Sculptcore becoming a client? Or should
  litestl get a new implementation modelled on it, leaving Sculptcore untouched for now?
  See [research/sculptcore-brep-reference.md §3](research/sculptcore-brep-reference.md#3-proposed-reuse-path-input-to-the-core-data-model-design-doc).
