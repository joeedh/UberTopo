# Plan P2 — Cell complex core (UberTopo `geom/`, `ident/`, `topo/`)

- **Status:** draft v1, awaiting pressure test.
- **Date:** 2026-11-18
- **Design:** [core-data-model/01](../design/core-data-model/01-cell-complex.md) (cell
  complex), [02](../design/core-data-model/02-coordinates-and-predicates.md) (coordinates
  and predicates), [04](../design/core-data-model/04-identity-versions-journal.md)
  (identity, versions, determinism) and
  [06 §3](../design/core-data-model/06-modules-litestl-and-phasing.md) (phase P2).
- **Builds on:**
  - litestl `master` `26447b2`: P0 numerics, P1 attribute core, compile-time schemas;
  - DEC-007 (templated coordinates and concepts);
  - DEC-008 (rebuilt regions, Lattice plus a later plane table);
  - DEC-010 (attribute API);
  - DEC-012 (schemas, no manual slot indices).
- **Repos:**
  - UberTopo code goes on branch `ccr-34437002-4r9a3r`.
  - Any general-purpose litestl additions go on litestl branch `ubertopo/p2-support`,
    merged to `master` only when the owner asks.

## 1. Goal

The first usable **non-manifold cell complex**: a cell complex with explicit volumes,
built from procedural input in bulk, with exact topology and regions.

1. **`geom/`:**
   - the `Coord` concepts and `CoordTraits` for `double`, `float` and the new `Lattice`
     type;
   - `TileFrame`;
   - the derived exact predicates that P2 needs.
2. **`ident/`:**
   - `PersistentId`;
   - UberTopo `DomainTag`s;
   - ID derivation helpers;
   - `Ref<D>` and `Handle<D>`.
3. **`topo/` domains:** Vertex, Edge, Coedge, Loop, Face, FaceSide, Shell and Region.
   Each is an `attr::TypedGroup` over a compile-time schema (DEC-012), with a slot
   allocator (freelist plus generations), held in `Complex<C>`.
4. **Bulk builder:**
   - emit vertices, wires and faces with holes, plus region hints;
   - deterministic weld and linking into coedges, loops, and disk and radial cycles.
5. **Exact radial order** for planar faces, and the **wedge rule** (invariant W).
6. **`build_regions()`:**
   - shells by wedge flood fill;
   - shell orientation;
   - nesting by exact winding numbers;
   - region assignment that honours hints and reports conflicts.
7. **Validator** covering the combinatorial, embedding (planar or straight) and global
   geometric layers, with a structured report.
8. **Freeze and thaw:**
   - compaction;
   - Morton renumbering, with coedges contiguous within each loop;
   - frozen caches (vertex→edges CSR, shell→face-sides CSR, inclusion CSR);
   - generation bumps.
9. **Serialisation** of a whole `Complex`, and a **content hash** of it.
10. **Determinism:**
    - identical topology, IDs and bytes at 1, 2 and N threads;
    - identical topology across the `double` and `Lattice` instantiations, for inputs
      representable in both.

**Exit criterion** (design 06): generated buildings must produce the correct regions,
pass all validation layers, and be bit-identical at 1 and N threads and in both
instantiations. The buildings have shared walls, window holes, aperture faces, rooms, a
courtyard, a sealed cavity, a fence sheet and a power-line wire.

## 2. Non-goals (later phases)

- **Editing primitives**, the attribute propagator and the journal (P3). P2 provides
  slot allocation and free, which P3 needs, plus builder-only construction.
- **Curved carriers** (P4). Every edge is a straight segment and every face is planar;
  `GeomRef` columns exist but are always NONE. Approximate radial ordering for curved
  faces is deferred.
- **Booleans and the plane table** (P5, OI-1).
- **Tile seams and world-box virtual faces.** The `virtual` flag exists and is honoured
  by the validator, but there is no seam stitching yet.
- **Instanced bodies.**
- **Tolerance-based welding** of imported data. P2 welds by key, or by bitwise-equal
  position.
- **Persistent-ID lookup maps beyond a simple derived hash map**, and handles held
  across versions; P2 provides basic handle resolution only.
- **Incremental region maintenance.** P2 rebuilds regions (DEC-008) and keeps the
  `regions_dirty` state.
- **N-API exposure.**

## 3. Baseline (verified 2026-11-18)

- **litestl `master` `26447b2`** provides:
  - `math::pred::orient2d/orient3d/incircle/insphere` for `double`, `float` and `int64`;
  - `Int128`/`Int256`;
  - `Hasher128`/`DomainTag`;
  - `rng::Stream`;
  - `task::parallel_for_chunks`/`parallel_reduce`/`ScopedBandCount`;
  - `attr::Column`/`AttrGroup`/`TypedGroup`/`Schema`/`AttrDef`, with snapshots, mix,
    permute, canonical hash and read/write.
- **UberTopo** has a top-level CMake build (C++20, `-ffp-contract=off`, litestl tests
  off), `source/ubertopo/tests` with a `ut_test.h` harness, and two smoke tests.
- **Deferred from P0 into P2:**
  - a deterministic **sort** that doesn't depend on the standard library;
  - a deterministic exclusive **scan**.

  §4 W1 decides them.

## 4. Work items

### W1 — Determinism utilities (litestl `ubertopo/p2-support`)

- **`util/sort.h`:**
  - `sort_unique_keys(span<T>, less)` for keys whose comparator is a strict total order
    with **no ties**. It debug-asserts that adjacent elements differ, because with
    unique keys any correct sort yields the same permutation.
  - `stable_sort(span<T>, less)`, a re-implemented merge sort, for the rare cases with
    genuine ties.
  - P2 uses unique keys everywhere: every builder key ends with the emission index.
- **`task::parallel_exclusive_scan(span<const uint32_t> in, span<uint32_t> out, chunk)`:**
  fixed chunking, with the chunk totals combined serially. The result is bit-identical
  at any band count.
- **Tests:**
  - sorts against `std::stable_sort` on random keys with ties;
  - the unique-key assertion;
  - scan results identical across `ScopedBandCount` values 1–12.

### W2 — `geom/`: coordinates, frames, derived predicates

- **`geom/coord.h`:**
  - the `CoordScalar`, `CoordType` and `ExactCoord` concepts;
  - `CoordTraits<double>`, `<float>` and `<Lattice>`, as in design 02 §2–3.
- **`Lattice`:**
  - a strong `int64_t` wrapper;
  - construction from `double` via `TileFrame::to_lattice` is range-checked against
    ±2^26 quanta and returns `bool` on failure;
  - `to_double`;
  - no arithmetic outside the traits.
- **`TileFrame`:** origin (`f64x3`, a site-grid multiple), quantum and frame ID.
- **`geom/predicates.h`:** written once over `CoordType`, using only
  `orient2d`/`orient3d` from the traits:

  | Predicate | Definition |
  |---|---|
  | `project_axis(face loops)` | The dominant axis of the face's approximate (double) Newell normal. Used only to *choose* a projection; every decision afterwards is exact |
  | `orient2d_proj(a, b, c, axis)` | `orient2d` on the two coordinates that remain after dropping `axis` |
  | `loop_orientation_proj(loop, axis)` | Exact sign of a simple loop's projected orientation: `orient2d_proj` at its lexicographically minimal vertex and that vertex's neighbours |
  | `face_front_sign(face, axis)` | Whether projecting along `axis` preserves the face's front orientation: the orientation of the loop containing the face's lexicographically minimal vertex, which must be the outer loop |
  | `point_in_face_proj(p, face, axis)` | Exact even-odd crossing test against every loop, with Simulation-of-Simplicity tie-breaking for points on edges or vertices; returns a boundary flag |
  | `radial_less(edge_v0, edge_v1, hp_a, hp_b, ref)` | Exact angular comparison of two half-planes about the edge axis (§W5) |
  | `ray_winding(p, shell)` | Exact signed crossing count of the fan triangles of every oriented loop in the shell, along a ray from `p`. Uses only `orient3d` |
  | `segments_intersect_proj(...)` | Exact projected segment intersection classification, for loop self-intersection checks |

- **Tests** (`test_geom_predicates.cc`):
  - **Lattice:** range checks and round trips.
  - **Independent check:** every derived predicate is compared against an evaluation of
    the same formulas with exact rational arithmetic on the test's own big-integer
    oracle. This copies litestl's `bigint_oracle.h` into the UberTopo tests directory
    (test-only).
  - **Inputs:** random cases plus constructed degeneracies (collinear, coplanar, points
    on edges and on vertices).
  - **Ray winding:** the result must equal the analytic winding for unit cubes, nested
    cubes, a cube with a tunnel, an L-shaped prism and a face with a hole, for interior
    and exterior points in both instantiations.

### W3 — `ident/`: IDs, domain tags, references

- **`ident/id.h`:**
  - `using PersistentId = hash::Hash128;`
  - UberTopo `DomainTag` constants (≥ 256), registered in one header: `GeneratorEmit`,
    `BuilderEdge`, `BuilderLoop`, `Region`, `OpDerived`, `TestOnly`.
  - Derivation helpers, all of them `Hasher128` tuples with typed fields:
    - `derive_generator(parent, op_site, discriminator…)`;
    - `edge_id(va, vb)`, computed over the *sorted* endpoint IDs, so it is independent
      of direction and index;
    - `loop_id(face, ordinal)`;
    - `region_id_from_hint(hint)`;
    - `region_id_from_sides(min face-side ID)`.
- **`ident/ref.h`:**
  - `enum class Domain`;
  - `Ref<D>`: explicit construction, no cross-domain conversion, `NONE`;
  - `Handle<D>{index, gen}`;
  - the `ElementRef` concept;
  - `ContainerRef`: a 2-bit kind (NONE, FACE, REGION) plus a 30-bit index, with
    range-checked packing.
- **Tests:**
  - goldens for the derivation helpers, joining the determinism contract;
  - **compile-fail snippets**: `Ref<Edge>` used where `Ref<Face>` is expected, and
    implicit construction from `uint32_t`;
  - `ContainerRef` round trips and the overflow check.

### W4 — `topo/` domains and `Complex<C>`

- **`topo/schema.h`:** one `attr::Schema` per domain, using the column names and flags of
  design 01 §2.
  - Link columns are `Topo` (or `Topo | Derived` for derived links), with defaults of
    `NONE`.
  - Every domain has `.gen` (u32 generation) and, where design 04 §3 says so,
    `.id` (`h128`).
  - Positions are `Column<CoordTraits<C>::Point>`. `Lattice` vectors need `ValueKey` and
    `is_padding_free_custom` specialisations, plus a registration call in
    `topo::register_column_types()`.
- **`topo/domain.h`:** `Domain<Schema>` wraps a `TypedGroup` plus a slot allocator, after
  Sculptcore's `ElemData`:
  - `alloc()`, `alloc_n(n)` (a dense range), `free(i)` (bumps the generation; LIFO
    freelist), `live(i)`, iteration over live slots, `live_count()` and `capacity()`;
  - a freed slot's columns are reset with `set_default_list`;
  - **FaceSide** is a `TypedGroup` sized 2 × face capacity, resized and permuted *only*
    together with the Face domain, through the induced map `2π(f)+s` (P1 dry run).
- **`topo/complex.h`:** `Complex<C>`:
  - members: the domain fields, plus `global`, `TileFrame frame`, the `regions_dirty`
    flag, and topology and geometry version stamps derived from the group versions;
  - **value semantics:** copying is a snapshot;
  - **accessors:** typed navigation over `Ref<D>`, such as `next(Ref<Coedge>)`,
    `radial_next`, `face_of`, `loops_of(face)` (an iterator), `coedges_of(loop)` and
    `edges_at(vertex)` (the disk walk);
  - **Rule:** all link access goes through `get<"name">()` or these accessors, never
    through raw slots (DEC-012).
- **Tests:**
  - allocation and free, with generation bumps and LIFO reuse;
  - a snapshot is independent of its source;
  - FaceSide sizing follows faces;
  - accessor round trips on a hand-built tetrahedron.

### W5 — Radial order (exact, planar)

- **Half-plane of a coedge.** For coedge `c` of planar face `f`, around edge `e = (v0→v1)`:
  - Choose `axis = project_axis(f)` and `σ = face_front_sign(f, axis)`.
  - The face's interior lies to the **left** of `c`, viewed from the front.
  - In the projection, "left" is `orient2d_proj > 0` when `σ = +1` and `< 0` otherwise.
  - Pick the first vertex `q` of `f` (in loop order, starting after the edge) that isn't
    collinear with the edge in projection.
  - The half-plane is `(q, s)`, where `s = +1` if `q` is on the interior side and `−1`
    otherwise. `s = −1` means "the half-plane opposite `q`".
  - This avoids constructing any point, so everything stays exact.
- **Angular order about the axis `v0→v1`** (counter-clockwise, right-hand rule):
  - **Reference.** A reference half-plane `h₀` (the coedge with the smallest persistent
    face ID) gives angle 0.
  - **Upper or lower.** Each half-plane `h` is classified as upper (0 < θ < π) or lower
    (π < θ < 2π) by `s₀·s_h·sign(orient3d(v0, v1, q₀, q_h))`, with the sign convention
    fixed by tests.
  - **On-axis-plane cases.** A zero result means `h` lies in `h₀`'s plane, at θ = 0 or
    π. The two are told apart exactly with `orient2d_proj` in that plane's projection,
    by checking whether `q_h` is on the same side of the edge as `q₀`, adjusted by
    `s₀·s_h`.
  - **Within a class,** order by `s_a·s_b·sign(orient3d(v0, v1, q_a, q_b))`.
  - **Ties.** Exactly equal angles (overlapping coplanar faces on the same side) are
    invalid geometry. The validator reports them, and the order breaks the tie by face
    persistent ID so that the result stays deterministic.
- **Construction:**
  - the builder sorts each radial cycle once, in parallel over edges;
  - cycles of size 1–2 are trivial;
  - `radial_next/prev` are written in sorted order.
- **Wedge rule.** `side_toward_next(c) = sense(c) ? FRONT : BACK`. Invariant W is checked
  by the validator.
- **Tests** (`test_topo_radial.cc`):
  - Fans of k planes around an axis (k = 1–8) with random angles, compared against an
    independent `long double` atan2 ordering wherever the angles differ by more than
    10⁻⁶.
  - Exactly degenerate fans in Lattice: coplanar opposite faces (θ = π, wall plus
    aperture) and perpendicular walls.
  - Concave faces whose first non-collinear vertex lies on the *exterior* side, to
    exercise `s = −1`.
  - Faces with holes, where the edge belongs to a hole loop.
  - Both instantiations, and both edge directions: reversing `v0/v1` reverses the cycle.

### W6 — Bulk builder

```cpp
template <CoordType C> class Builder {
public:
  explicit Builder(const TileFrame &f);
  VertexKey vertex(PersistentId id, Point p, ContainerRef c = {});   // key = id
  FaceKey   face(PersistentId id, span<const span<const VertexKey>> loops,
                 RegionHint front, RegionHint back, FaceFlags flags = {});
  EdgeKey   wire(PersistentId id, VertexKey a, VertexKey b, ContainerRef c);
  bool build(Complex<C> &out, const BuildPolicy &policy, BuildReport &report);
};
```

- **Weld.** Vertices weld by **key**: equal IDs must have bitwise-equal positions,
  otherwise the build reports an error. `BuildPolicy::weld_equal_positions` optionally
  welds distinct keys with bitwise-equal positions too, keeping the smaller ID.
  Tolerance welding is out of scope.
- **Edges** are created from consecutive loop vertices, deduplicated by unordered
  endpoint pair, and given `edge_id(va, vb)`. There is one edge per pair. P2 has straight
  edges only, so parallel edges can't arise.
- **Deterministic indexing.** Every domain is filled densely in sorted order:
  - vertices by ID;
  - edges by `(min vertex ID, max vertex ID)`;
  - faces by ID;
  - loops by `(face ID, ordinal)`;
  - coedges by `(face, loop ordinal, position)`.

  Keys are unique, so `sort_unique_keys` applies. Parallel phases (edge dedup with
  per-chunk maps merged in chunk order, radial sorting, link writing) use
  `parallel_for_chunks` with constant chunks.
- **Validation during build.** The builder rejects, with a report entry, any face
  that:
  - uses the same edge twice;
  - has a loop shorter than 3;
  - repeats a vertex within a loop;
  - is loopless (only allowed on curved surfaces, which P2 doesn't have).
- **Region hints** are generator-level region IDs per face side (`RegionHint` = a
  `PersistentId` or none). They are stored as a builtin FaceSide column, `.fs.hint`.
- **Containers** (inclusions) are written directly. Inclusion CSR caches are built at
  freeze time.
- **Tests:**
  - building a tetrahedron, a box, two boxes sharing a face, a box with a window hole
    plus an aperture face, and a fence sheet gives exact, known link tables;
  - every rejection path;
  - **permutation invariance:** emitting the same model in a shuffled order gives
    identical columns, IDs and bytes.

### W7 — `build_regions()`

1. **Shells by flood fill over wedges.**
   - From face side `(f, s)`, for each coedge `c` of `f`: if `s == side_toward_next(c)`,
     cross to the counter-clockwise neighbour
     `(face(radial_next(c)), 1 − side_toward_next(radial_next(c)))`; otherwise cross to
     the clockwise one via `radial_prev`.
   - A boundary edge of a sheet, with radial degree 1, maps `(f, s)` to `(f, 1 − s)`.
   - Shells get indices in order of their minimal face-side persistent ID.
2. **Closure and orientation.**
   - A shell is **closed** if every edge it uses is used by an even number of its
     coedge sides. Sheets fail this, and are classed as **open**.
   - For a closed shell, the orientation is the sign of `Σ det(a, b, c)` over the fan
     triangles of its face sides, with normals oriented to point into the side's
     region:
     - for `Lattice`, the sum is exact in `Int128`, and the bit budget is documented;
     - for `double`, it's a filtered sum with a rigorous error bound, and |V| ≤ bound is
       reported as a **degenerate shell** rather than guessed.
   - A positive sign means the shell **encloses** its region (an outer shell); a
     negative sign means its region is **outside** it (a cavity boundary). The sign
     convention is fixed by tests.
3. **Nesting.**
   - For each cavity shell and each open shell `A`, find the **innermost outer shell
     `B`** whose enclosed volume contains `A`. The test is `ray_winding(p, B) ≠ 0` for a
     vertex `p` of `A` that isn't a vertex of `B`. If every vertex of `A` is on `B`, a
     touching shell, a second vertex-based test runs over `A`'s other faces, and if that
     fails too the case is reported.
   - Bounding boxes prefilter candidates. The cost is O(shells² × faces) worst case,
     which is acceptable for P2; a BVH comes later.
   - Outer shells nest the same way, which gives the containment tree.
4. **Regions.**
   - Each outer shell plus the cavity and open shells directly inside it form one region.
   - Top-level cavity and open shells belong to region 0, the unbounded region.
5. **Hints.**
   - A computed region takes the hint shared by all its face sides, and its ID is
     `region_id_from_hint`.
   - Mixed hints produce a **hint conflict** report naming the face sides, and the
     majority hint wins, with ties broken by smallest ID.
   - Without hints, the ID is `region_id_from_sides`.
   - Region 0 always has the fixed unbounded ID.
6. **Output.**
   - It writes `.f.region`, `.f.shell`, the shell chains (`.fs.shell_next`) and the region
     chains, and clears `regions_dirty`.
   - It returns a `RegionReport`: counts, degenerate shells, touching-shell fallbacks and
     hint conflicts.
   - It is deterministic: shells and regions are sorted by ID before indexing, and the
     parallel phases (flood fill per seed in a fixed order, volume sums through
     `parallel_reduce`) are chunked.
- **Tests** (`test_topo_regions.cc`), with ground truth from the generators (W9):
  - room grids with thin shared walls, where each room is a region and the outside is
    region 0;
  - thick walls, where the wall solids are regions;
  - a window hole with a glazing aperture face, which separates the room from the
    outside;
  - an open window with no aperture face, which joins the room to the outside region;
  - a courtyard (a floor face with a hole);
  - a sealed cavity inside a solid;
  - a pillar inside a room, a cavity of the room region;
  - a fence sheet in air, an open shell in region 0;
  - a sensor vertex included in a room;
  - a wire edge in air.

  Each case checks region count, membership and hint conflicts against the generator's
  ground truth.

### W8 — Validator

`validate(const Complex<C>&, ValidateLevel) -> ValidationReport` returns a list of
`{code, domain, element IDs, message}`. It never asserts.

- **Combinatorial:**
  - the inverse and closure checks of design 01 §8;
  - radial and disk cycle closure;
  - no face uses an edge twice;
  - **invariant W**;
  - shells partition face sides, and `region[side] == shell.region`;
  - exactly one unbounded region;
  - container validity, and no `container` on vertices with non-embedded edges;
  - generation monotonicity across a freeze;
  - the persistent-ID uniqueness check;
  - the generalised **Euler–Poincaré** cross-check from research brep-topology/05.
- **Embedding** (P2: straight and planar):
  - face planarity: `orient3d` of each loop vertex against the face's first
    non-collinear triple must be zero;
  - the stored radial order must match a fresh W5 sort.
- **Global** (tests and import):
  - no loop self-intersects (`segments_intersect_proj`);
  - holes lie inside their face's outer loop (`point_in_face_proj`);
  - no two faces intersect away from shared edges and vertices (O(n²) with bounding-box
    prefilter, test-only);
  - `build_regions()` run on a copy matches the stored regions.
- **Tests:**
  - every code is triggered by a hand-corrupted complex (one corruption per code);
  - valid generated models give empty reports;
  - **mutation check:** a planted bug in the radial comparator or the wedge rule is
    caught.

### W9 — Test generators (`source/ubertopo/tests/gen/`)

These are deterministic procedural generators with **ground truth**: the expected region
of every face side, and the expected counts.

- **Families:**
  - `room_grid(nx, ny, floors, wall: thin|thick, windows: k, glazing: bool)`;
  - `courtyard`;
  - `sealed_cavity`;
  - `pillar_room`;
  - `fence_in_air`;
  - `sensor_and_wire`;
  - `random_city(seed, n_buildings)`, which composes these with random parameters using
    `rng::Stream`.
- **Coordinates:**
  - Generated on an integer grid that scales to both instantiations: `Lattice` values
    directly, and `double` = integer × 2^-k, which is exact.
  - The `double` and `Lattice` models are therefore *the same geometry*.
- **IDs:** persistent IDs derived from generator paths
  (`derive_generator(building, "room", ix, iy, floor)`).

### W10 — Freeze, thaw, serialisation, hashing

- **`freeze(Complex<C>&) -> FreezeReport`:**
  - compacts every domain and renumbers vertices, edges and faces by the **Morton code
    of their centroid**, computed in `double` in the tile frame and quantised to 21
    bits per axis. Ties are broken by persistent ID.
  - Loops follow faces. Coedges are contiguous per loop, in loop order. Shells and
    regions follow persistent ID.
  - Every domain, including the induced FaceSide map, is permuted through
    `AttrGroup::permute`.
  - Generations are bumped.
  - The frozen caches are built and stored as `Derived` columns or side CSR structures
    stamped with group versions:
    - vertex→edges CSR;
    - shell→face-sides CSR;
    - face and region inclusion CSR.
- **`thaw`** is a copy. Freelists start empty and caches are dropped on the first
  mutation (version mismatch).
- **`write(const Complex&, ByteWriter&)` / `read(Complex&, ByteReader&)`:**
  - a header holding the format version, `Coord` key and tile frame;
  - each domain group in a fixed order, through `AttrGroup::write`/`read` with layout
    checks;
  - the allocator state (the freemap) for editable complexes;
  - a trailing content hash.
- **`content_hash(const Complex&)`:** the hash of every group in a fixed order, plus
  the frame. **`topology_hash`** is the same with `pos` and geometry columns excluded;
  it is used for cross-instantiation comparison.
- **Tests:**
  - freeze, then validate;
  - freezing twice is idempotent (identical bytes);
  - a freeze after random frees removes the tombstones and leaves regions valid;
  - round trips;
  - a corrupted stream is rejected.

### W11 — Determinism and cross-instantiation suite

`test_topo_determinism.cc`:

- **Thread counts:** for `random_city` (≥ 200 buildings, ≥ 10⁵ faces at scale 1),
  `build`, `build_regions` and `freeze` under `ScopedBandCount` 1, 2, 4 and 12 must give
  identical `content_hash` and identical bytes.
- **Instantiations:** the `Lattice` and `double` builds of the same generator must give
  identical **`topology_hash`**, region membership and IDs.
- **Pinned goldens:** a pinned `topology_hash` for a fixed `room_grid`, which joins the
  determinism contract (DEC entry).
- **Compilers:** the same digest under clang and gcc, and with `-march=x86-64-v3`.

### W12 — Docs and integration

- A new design-level reference, **`docs/design/core-data-model/07-topology-api.md`**,
  covering the realised API: domains and schemas, accessors, builder, regions,
  validator codes and freeze.
- Notes in design 01, 02 and 04 where the implementation differs.
- A DEC entry for P2 decisions and goldens.
- `docs/index.md` and `docs/debugging.md`.
- CMake: `ubertopo_geom`, `ubertopo_ident` and `ubertopo_topo` targets, with tests in
  `source/ubertopo/tests/` (unit and long labels) and compile-fail tests, using
  litestl's pattern.

## 5. Order and dependencies

```
W1 (litestl sort/scan) ─┐
W2 geom ────────────────┼─► W4 domains/Complex ─► W5 radial ─► W6 builder ─► W7 regions ─► W8 validator
W3 ident ───────────────┘                                          ▲             ▲
W9 generators (after W6 API) ──────────────────────────────────────┴─────────────┘
W10 freeze/io/hash (after W7) ─► W11 determinism ─► W12 docs
```

Commits are in that order, and each commit builds and passes. The work may be split into
**P2a** (W1–W6, W8 combinatorial) and **P2b** (W7, W8 rest, W9–W11) if it is reviewed as
two pushes.

## 6. Acceptance criteria

1. **Generated models pass.** Every generator family, in both instantiations, builds,
   gets its regions built, passes all three validation layers with an empty report, and
   matches its region ground truth exactly.
2. **Determinism:** content hash and bytes are identical across band counts 1, 2, 4 and
   12. Topology hashes are identical across `double` and `Lattice`. The pinned golden
   matches under clang Release, gcc Release and clang `-march=x86-64-v3`.
3. **Exactness checks pass:**
   - derived predicates agree with the big-integer oracle;
   - radial order agrees with the long-double reference on well-separated cases and is
     correct on constructed degeneracies;
   - ray winding equals the analytic value on the shape set.
4. **The validator catches** every hand-corrupted case and both planted mutations.
5. **Compile-time safety:** cross-domain `Ref` misuse and mistyped link-column names
   fail to compile (compile-fail tests).
6. **The full UberTopo suite passes** under clang Release, gcc Release, gcc Debug + ASan,
   gcc Debug + UBSan and gcc TSan.
7. **Hygiene:**
   - no file over 40 KB;
   - no `CLAUDENOTE:`;
   - docs and index updated;
   - litestl changes on a feature branch, merged only on request.

## 7. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | The half-plane construction is wrong for some concave or holed face configurations | Independent long-double reference on random fans, constructed concave and hole cases, and a validator re-sort check. The rule is documented so reviewers can check it |
| R2 | `double` shell orientation can't be certified (degenerate shells) | Report it, never guess. Generators produce well-separated geometry. `Lattice` is exact |
| R3 | Ray-winding degeneracies (the ray hits an edge or vertex) | A fixed deterministic sequence of ray directions, with retry on any zero `orient3d`. The retry count goes into the report, and tests force degenerate first directions |
| R4 | Touching shells (vertex-only contact) confuse nesting | Test vertices are taken from non-shared vertices. The fallback is reported. Explicit tests |
| R5 | Scale: O(shells²) nesting on a 10⁵-face city | Bounding-box prefilter. Buildings give many small shells, and containment is local. The W11 runtime is measured, and a BVH follows in a later phase if needed |
| R6 | The builder's determinism leaks through hash-map iteration order | Edge dedup maps are only *queried*; output order always comes from sorted keys. Tests shuffle the emission order |
| R7 | Template bloat from `Complex<C>` | Only vertex positions and geometric algorithms are templated (design 01 §10). Region and flood logic is non-template, over domains |
| R8 | The Lattice bit budget for signed-volume sums | Absolute coordinates ≤ 2^26. Each triangle determinant is < 2^80 and the sum of ≤ 2^30 terms is < 2^110, so `Int128` suffices. A `static_assert` plus a range check on face counts |

## 8. Estimated size

| Item | Code (LOC) | Tests (LOC) |
|---|---|---|
| W1 | ~200 | ~150 |
| W2 | ~600 | ~500 |
| W3 | ~250 | ~150 |
| W4 | ~600 | ~250 |
| W5 | ~350 | ~350 |
| W6 | ~700 | ~350 |
| W7 | ~650 | ~400 |
| W8 | ~650 | ~350 |
| W9 | — | ~500 |
| W10 | ~550 | ~250 |
| W11 | — | ~200 |

About 4.5k LOC of code and 3.5k LOC of tests. These are rough figures.

## 9. Open questions for the owner

1. **Fallback when a `double` shell's orientation can't be certified.** Should P2 also
   evaluate it exactly by converting to big integers in production code (a new litestl
   exact-sum utility), or is "report as degenerate" acceptable for P2?
2. **Commit structure.** Should P2 land as one sequence of commits, or as P2a and P2b
   with a pause between them?

## 10. Pressure-test review

_To be filled in by a fresh-context review agent before implementation._
