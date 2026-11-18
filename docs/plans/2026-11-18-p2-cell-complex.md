# Plan P2 — Cell complex core (UberTopo `geom/`, `ident/`, `topo/`)

- **Status:** v2, revised after the fresh-context pressure test (§10); amendment A1 v3 (§12). **Implemented**: P2a and P2b (§11).
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
   - shells by union-find over wedges;
   - shell orientation;
   - sheet shells, and nesting by exact ray crossings;
   - region assignment that honours hints and reports conflicts.
7. **Validator** covering the combinatorial, embedding (planar or straight) and global
   geometric layers, with a structured report.
8. **Freeze and thaw:**
   - compaction with link remapping;
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
- **Facts the plan relies on** (checked during review):
  - `Derived` and `Temp` columns are excluded from group hashes and bytes;
  - `AttrGroup::permute` moves rows but doesn't remap the indices stored in them;
  - `set_default_list` resets every column;
  - `Vec` zero-fills with a float literal;
  - integer `orient3d` accepts Bits ≤ 30.

## 4. Work items

### W1 — Determinism and attribute utilities (litestl `ubertopo/p2-support`)

- **`util/sort.h`:**
  - `sort_unique_keys(span<T>, less)` for keys whose comparator is a strict total order
    with **no ties**. Any correct sort gives the same permutation then, so it may wrap
    `std::sort`. The adjacent-difference check stays on in **Release** too: a tie is an
    error that's reported to the caller (`bool` result), not silently tie-broken.
  - `stable_sort(span<T>, less)`, a re-implemented merge sort, for the rare cases with
    genuine ties.
  - Builder keys never include the emission index (review 15). Equal keys mean
    duplicate IDs, which is a build error.
- **`task::parallel_exclusive_scan(span<const uint32_t> in, span<uint32_t> out, chunk)`:**
  fixed chunking, with the chunk totals combined serially. The result is bit-identical
  at any band count.
- **`attr` additions** (review 18):
  - `AttrGroup::hash_into(Hasher128&, filter)`, where the filter is a predicate over
    `(name, flags)`. Used for `topology_hash`, which excludes geometry columns.
  - `AttrGroup::version()`: a stamp combining the column versions and
    `schema_version()`. It changes after any mutation of any column.
- **`math::Vec`** (review 13): zero-fill with `T{}` / `T(0)` instead of `0.0f`, so `Vec`
  works with strong scalar types such as `Lattice`.
- **Tests:**
  - sorts against `std::stable_sort` on random keys with ties;
  - the unique-key tie report;
  - scan results identical across `ScopedBandCount` values 1–12;
  - a filtered hash equals the hash of a group without the filtered columns;
  - `version()` changes after every kind of mutation.

### W2 — `geom/`: coordinates, frames, derived predicates

- **`geom/coord.h`:**
  - the `CoordScalar`, `CoordType` and `ExactCoord` concepts;
  - `CoordTraits<double>`, `<float>` and `<Lattice>`, as in design 02 §2–3.
- **`Lattice`** (review 13):
  - a strong `int64_t` wrapper with an `explicit` constructor from `int64_t`;
  - `+`, binary `-` and unary `-` (as `CoordScalar` requires) and comparisons;
  - no multiplication, and no implicit conversions;
  - construction from `double` via `TileFrame::to_lattice` is range-checked against
    ±2^26 quanta and returns `bool` on failure;
  - `to_double`.
- **`LatticePoint`:** `math::Vec<Lattice, 3>`, which works after the W1 `Vec` fix.
  `struct{int64_t}` already has unique object representations, so no padding
  specialisation is needed.
- **Column types:** the type keys `lat`, `latx3`, `u32[2]`, `u32[4]`, `cref`
  (`ContainerRef`) and `gref` (`GeomRef`). They're registered by
  `topo::register_column_types()`.
- **`TileFrame`:**
  - origin (`f64x3`, a site-grid multiple), quantum and frame ID;
  - **the quantum must be 2^-k.** That makes `double` = integer × quantum exact, which
    the cross-instantiation tests rely on (review 17).
- **`geom/predicates.h`** (written once over `CoordType`, using only
  `orient2d`/`orient3d` from the traits):
  - **Inputs are spans of points and index lists,** never `topo` types (review 23).
    `topo/` adapts its faces and shells to these spans.

  | Predicate | Definition |
  |---|---|
  | `project_axis(loops)` | The dominant axis of the approximate (double) Newell normal. Used only to *choose* a projection; every decision afterwards is exact |
  | `orient2d_proj(a, b, c, axis)` | `orient2d` on the two coordinates that remain after dropping `axis` |
  | `loop_orientation_proj(loop, axis)` | Exact sign of a simple loop's projected orientation: `orient2d_proj` at its lexicographically minimal vertex and that vertex's neighbours |
  | `face_front_sign(loops, axis)` | The orientation of the loop containing the face's lexicographically minimal vertex. That loop is the outer loop, because the minimum is an extreme point and holes lie strictly inside |
  | `point_in_face_proj(p, loops, axis)` | Exact even-odd crossing test against every loop, with Simulation-of-Simplicity tie-breaking for points on edges or vertices; returns a boundary flag |
  | `radial_less(edge_v0, edge_v1, hp_a, hp_b, ref)` | Exact angular comparison of two half-planes about the edge axis (§W5) |
  | `ray_crossings(p, triangles, dir_index)` | Signed crossing count of a segment from `p` to a far point `q = p + D_k·L`, for the triangle fans of oriented loops (see below) |
  | `segments_intersect_proj(...)` | Exact projected segment intersection classification, for loop self-intersection checks |

- **Ray crossings** (review 7):
  - **Directions.** `D_k` comes from a fixed table of *generic* integer directions, such
    as (1, 2^10+1, 2^20+3) and permuted variants. Axis-aligned directions would be
    degenerate on almost every vertex of grid-built geometry.
  - **Far point.** `L` is the smallest power of two that takes `q` beyond the bounding
    box of the triangles, so a *segment* test is equivalent to the ray test.
  - **Segment test.** The segment crosses triangle `abc` when `orient3d(a,b,c,p)` and
    `orient3d(a,b,c,q)` have opposite signs and the three
    `orient3d(p, q, edge)` signs agree.
  - **Degeneracies.** Any zero moves to the next direction `k`, and the retry count is
    reported.
  - **Bit budget for `Lattice`:**
    - points are scaled by 3, for the face-barycentre test points of W7.3, giving
      |3x| < 2^27.6;
    - `|q| < 2^30`;
    - so the test uses `orient3d<Bits=30>`, which passes litestl's limits
      (2·30+3 ≤ 63 and 3·30+7 ≤ 127), with a `static_assert` and a range check.
  - **For `double`,** `q` is computed by exact power-of-two scaling, which stays exact
    while the inputs are on a 2^-k grid; otherwise the result is still a valid segment
    endpoint.
- **Tests** (`test_geom_predicates.cc`):
  - **Lattice:** range checks and round trips.
  - **Independent check:** every derived predicate is compared against an evaluation of
    the same formulas with exact rational arithmetic on the test's own big-integer
    oracle. This copies litestl's `bigint_oracle.h` into the UberTopo tests directory
    (test-only).
  - **Inputs:** random cases plus constructed degeneracies (collinear, coplanar, points
    on edges and on vertices).
  - **Ray crossings:** the winding must equal the analytic value for unit cubes, nested
    cubes, a cube with a tunnel, an L-shaped prism and a face with a hole, for interior
    and exterior points in both instantiations. Grid-aligned test points force the
    direction retries.

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
    - `region_id(hint, min face-side ID)`;
    - `region_id_from_sides(min face-side ID)`;
    - the fixed `kUnboundedRegionId`.
- **`ident/ref.h`:**
  - `enum class ElemKind`, renamed from `Domain` to avoid clashing with
    `topo::Domain<Schema>` (review 23);
  - `Ref<K>`: explicit construction, no cross-kind conversion, `NONE`;
  - `Handle<K>{index, gen}`;
  - the `ElementRef` concept;
  - `ContainerRef`: a 2-bit kind (NONE, FACE, REGION) plus a 30-bit index, with
    range-checked packing.
- **Tests:**
  - goldens for the derivation helpers, joining the determinism contract;
  - **compile-fail snippets**: `Ref<Edge>` used where `Ref<Face>` is expected, and
    implicit construction from `uint32_t`;
  - `ContainerRef` round trips and the overflow check.

### W4 — `topo/` domains and `Complex<C>`

- **`topo/schema.h`:** one `attr::Schema` per domain, using the column names of
  design 01 §2.
  - **Link flags** (review 3):
    - **Every link column**, stored or derived-in-the-design, is flagged `Topo` only, so
      it is hashed, serialised and survives `read()`.
    - **`Derived`** is reserved for **freeze caches** (W10), which litestl excludes from
      hashing and I/O, and which are rebuilt after `read()`.
    - Design 01 §2's "D" marker is documented as "recomputable", not as the litestl flag.
  - **Defaults** are `NONE`.
  - **`.gen` and `.id`.** Every domain has `.gen` (u32 generation) and, where design 04
    §3 says so, `.id` (`h128`). Shells have no `.id`; they are keyed by their minimal
    face-side ID.
  - **Positions** are `Column<CoordTraits<C>::Point>`.
- **`topo/domain.h`:** `Domain<Schema>` wraps a `TypedGroup` plus a slot allocator, after
  Sculptcore's `ElemData`:
  - `alloc()`, `alloc_n(n)` (a dense range), `free(i)`, `live(i)`, iteration over live
    slots, `live_count()` and `capacity()`;
  - **`free(i)`** (review 9) resets **every column except `.gen`** to its default
    (per-column, through `ColumnBase`), then bumps `.gen` and pushes `i` onto a LIFO
    freelist. Generations never go back to 0.
  - **FaceSide** is a `TypedGroup` sized 2 × face capacity, resized and permuted *only*
    together with the Face domain, through the induced map `2π(f)+s` (P1 dry run).
    Freeing face `f` resets rows `2f` and `2f+1`.
- **`topo/complex.h`:** `Complex<C>`:
  - members: the domain fields, plus `global`, `TileFrame frame` and the
    `regions_dirty` flag;
  - **`topology_version()` and `geometry_version()`** are computed from
    `AttrGroup::version()` of the relevant groups (W1);
  - **value semantics:** copying is a snapshot;
  - **accessors:** typed navigation over `Ref<K>`, such as `next(Ref<Coedge>)`,
    `radial_next`, `face_of`, `loops_of(face)` (an iterator), `coedges_of(loop)` and
    `edges_at(vertex)` (the disk walk);
  - **Rule:** all link access goes through `get<"name">()` or these accessors, never
    through raw slots (DEC-012).
- **Tests:**
  - allocation and free, with generation bumps (never reset) and LIFO reuse;
  - a stale handle fails to resolve after reuse;
  - a snapshot is independent of its source;
  - FaceSide sizing follows faces;
  - `read()` restores every link column;
  - accessor round trips on a hand-built tetrahedron.

### W5 — Radial order (exact, planar)

**Conventions** (review 14):
- An edge runs from `vs[0]` to `vs[1]`, ordered by persistent vertex ID, smaller first.
- Disk order around a vertex is by edge index.

**Precondition** (enforced by W6, review 6): every face is planar and non-degenerate.
The outer loop is counter-clockwise viewed from the front, and holes are clockwise.

- **Half-plane of a coedge.** For coedge `c` of planar face `f`, around edge `e = (v0→v1)`:
  - Choose `axis = project_axis(f)` and `σ = face_front_sign(f, axis)`.
  - The face's interior lies to the **left** of `c`, viewed from the front.
  - In the projection, "left" is `orient2d_proj > 0` when `σ = +1` and `< 0` otherwise.
  - Pick the first vertex `q` of `f` (in loop order, starting after the edge) that isn't
    collinear with the edge in projection. Any such coplanar vertex works, inside the
    face or not.
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
- **Construction** (review 11):
  - the sort runs in parallel over edges, writing `radial_next/prev` into **temporary
    `util::Vector`s** indexed by coedge;
  - those are copied into the columns with per-page `Column::parallel_write`;
  - no column `set()` runs concurrently. The same pattern is used for every parallel
    link write in W6, W7 and W10;
  - cycles of size 1–2 are trivial.
- **Scope of this rule.** It applies only to topology construction, where the target of
  each write is *scattered*: the coedges of an edge aren't contiguous.
  - **Per-element derived data,** such as face planes and vertex or face normals, is
    written in place, in parallel, with `Column::parallel_write`. Each thread owns a
    contiguous range of target elements. That's already supported by litestl P1 and is
    the intended path for later geometry passes.
  - **Scattered accumulations,** such as face normals summed into vertices, are written
    as **gathers over a CSR** (vertex→faces, from the frozen caches) and summed in a
    fixed order. They don't use atomics or float scatter-adds, which would be
    nondeterministic.
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
  VertexKey vertex(PersistentId id, Point p, ContainerHint c = {});
  FaceKey   face(PersistentId id, span<const span<const VertexKey>> loops,
                 RegionHint front, RegionHint back, FaceFlags flags = {});
  EdgeKey   wire(PersistentId id, VertexKey a, VertexKey b, ContainerHint c = {});
  bool build(Complex<C> &out, const BuildPolicy &policy, BuildReport &report);
};
```

- **Weld.** Vertices weld by **key**: equal IDs must have bitwise-equal positions,
  otherwise the build reports an error. `BuildPolicy::weld_equal_positions` optionally
  welds distinct keys with bitwise-equal positions too, keeping the smaller ID.
  Tolerance welding is out of scope.
- **Face edges** are created from consecutive loop vertices, deduplicated by unordered
  endpoint pair, and given `edge_id(va, vb)`. There is one edge per pair. P2 has straight
  edges only, so parallel edges can't arise.
- **Wires** (review 14):
  - a wire keeps its **given ID**;
  - a wire whose endpoint pair is already a face edge, or another wire, is rejected.
- **Containers** (review 5). Region indices don't exist at build time, so `vertex` and
  `wire` take a **`ContainerHint`**:
  - none;
  - a `FaceKey` (an inclusion in a face);
  - a region hint (a generator region ID).

  The builder resolves face hints directly. `build_regions()` resolves region hints into
  `ContainerRef`s, and reports a hint whose region doesn't geometrically contain the
  element (W8).
- **Deterministic indexing.** Every domain is filled densely in sorted order:
  - vertices by ID;
  - edges by `(min vertex ID, max vertex ID)`;
  - faces by ID;
  - loops by `(face ID, ordinal)`;
  - coedges by `(face, loop ordinal, position)`.

  Keys are unique, and duplicates are build errors, so `sort_unique_keys` applies.
  Parallel phases (edge dedup with per-chunk maps merged in chunk order, radial sorting,
  link writing) use `parallel_for_chunks` with constant chunks.
- **Face checks** (review 6). The builder rejects, with a report entry:
  - a duplicate face ID;
  - a loop shorter than 3, or a repeated vertex within a loop;
  - a vertex shared between two loops of one face;
  - a face that uses the same edge twice;
  - a non-planar face (exact `orient3d` against the first non-collinear triple);
  - a collinear or zero-area loop, which would leave W5 with no `q`;
  - a face whose hole loops don't have the opposite projected orientation to its outer
    loop. The outer loop is the one with the lexicographically minimal vertex.
  - A loopless face is also rejected (only curved surfaces may have one, and P2 has
    none).

  Holes that are geometrically outside the outer loop are a global-layer check (W8).
- **Region hints** are generator-level region IDs per face side (`RegionHint` = a
  `PersistentId`, none, or `kUnbounded`). They are stored as a builtin FaceSide column,
  `.fs.hint`.
- **Tests:**
  - building a tetrahedron, a box, two boxes sharing a face, a box with a window hole
    plus an aperture face, and a fence sheet gives exact, known link tables;
  - every rejection path, including a hole wound like its outer loop;
  - **permutation invariance:** emitting the same model in a shuffled order gives
    identical columns, IDs and bytes.

### W7 — `build_regions()`

1. **Shells by union-find over wedges** (review 21).
   - For every coedge `c` with `cn = radial_next(c)`, the wedge between them joins face
     sides `(face(c), side_toward_next(c))` and `(face(cn), 1 − side_toward_next(cn))`.
   - A radial cycle of size 1 (a sheet boundary) joins `(f, 0)` with `(f, 1)`.
   - Union-find runs over face-side indices `2f+s`, in a parallel-safe form: per-chunk
     union lists are merged serially in chunk order.
   - Each class is labelled by its minimal face-side persistent ID, which makes the
     labelling independent of traversal order. Shells are indexed in label order.
2. **Shell kind and orientation.**
   - **Sheet shell** (review 1): the shell contains *both* sides of every face it
     touches, such as a fence in air or a thin-walled shell open to the outside through
     a window. Its volume isn't computed, and it nests like a cavity.

     The old "edge used an even number of times" closure test is dropped: every shell
     passes it.
   - **Other shells:** the orientation is the sign of `V = Σ det(a, b, c)` over the fan
     triangles of each face side, oriented with the **outward** normal of the side, i.e.
     pointing away from the side's region (review 19). With that convention, an
     enclosing shell has `V > 0`:
     - `Lattice`: exact in `Int128` (budget in R8);
     - `double`: a filtered sum with a rigorous error bound. If `|V| ≤ bound`, the shell
       is reported as a **degenerate shell** and not guessed (owner decision §9.1).
   - Mixed shells, such as a fin attached to a solid, have a non-zero `V` and need no
     special case.
   - `V > 0` means the shell **encloses** its region (an **outer** shell). `V < 0` means
     its region is outside it (a **cavity** shell).
3. **Nesting.**
   - For each cavity shell and each sheet shell `A`, find the **innermost outer shell
     `B`** whose enclosed volume contains `A`. Outer shells nest the same way, which
     gives the containment tree.
   - **(a) Shared face** (review 2). If `A` and `B` share a face, on opposite sides, `A`
     is **not** inside `B`. This covers a thin-wall room against the outside, and a room
     against its thick wall's cavity shell, which are the common touching cases.
   - **(b) Otherwise, test a point** of `A` against `B` with `ray_crossings`. The point is
     a vertex of `A` that isn't on `B` if there is one; failing that, the barycentre
     (scaled by 3) of a fan triangle of a face of `A` that isn't a face of `B`.
     Faces of a valid complex meet only at shared edges and vertices, so that point
     is not on `B`.
   - **Prefilter.** Candidates are prefiltered by bounding box (R5).
4. **Regions.**
   - Each outer shell plus the cavity and sheet shells directly inside it form one
     region.
   - Top-level cavity and sheet shells belong to region 0, the unbounded region, which
     always has index 0.
5. **IDs and hints** (review 8).
   - A region whose face sides all carry hint `h` gets `region_id(h, min face-side ID)`.
     That ID is unique even when two disconnected regions carry the same hint; the
     duplicate hint is reported, as a warning.
   - Mixed hints produce a **hint conflict** report naming the face sides, and the
     majority hint wins, with ties broken by smallest ID.
   - Without hints, the ID is `region_id_from_sides`.
   - Region 0 is always `kUnboundedRegionId`. Its face sides should carry `kUnbounded`
     or no hint; any other hint is reported as a conflict.
6. **Region attributes** (review 24). Non-`Topo` region columns are carried over from
   the previous region set by persistent ID, with CopyFirst through `mix_from`. Regions
   with new IDs get defaults.
7. **Output.**
   - It writes `.f.region`, `.f.shell`, the shell chains (`.fs.shell_next`) and the region
     chains, resolves region `ContainerHint`s (W6), and clears `regions_dirty`.
   - It returns a `RegionReport`: counts, degenerate shells, ray retries and hint
     conflicts.
   - It is deterministic: shells and regions are indexed by sorted IDs, the parallel
     phases are chunked, and volume sums go through `parallel_reduce`.
- **Tests** (`test_topo_regions.cc`), with ground truth from the generators (W9):
  - room grids with thin shared walls, where each room is a region and the outside is
    region 0;
  - a *single* thin-walled room (touching shells);
  - thick walls, where the wall solids are regions and rooms are cavities of the
    walls' regions;
  - a window hole with a glazing aperture face, which separates the room from the
    outside;
  - an open window with no aperture face, which joins the room to the outside region
    as a sheet shell;
  - a courtyard (a floor face with a hole);
  - a sealed cavity inside a solid;
  - a pillar inside a room, a cavity of the room region;
  - a fence sheet in air, a sheet shell in region 0;
  - a sensor vertex included in a room;
  - a wire edge in air;
  - two disconnected regions with the same hint;
  - region attributes surviving a rebuild.

  Each case checks region count, membership and hint reports against the generator's
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
  - exactly one unbounded region, at index 0;
  - container validity, including face containers (design 01 §5), and no `container`
    on vertices with non-embedded edges;
  - the persistent-ID uniqueness check;
  - the generalised **Euler–Poincaré** cross-check from research brep-topology/05. Its
    expected Betti numbers come from generator ground truth (W9; review 20).
- **Embedding** (P2: straight and planar):
  - face planarity: `orient3d` of each loop vertex against the face's first
    non-collinear triple must be zero;
  - hole orientation is opposite to the outer loop's;
  - the stored radial order must match a fresh W5 sort;
  - each included vertex or wire lies inside its container: inside its region
    (`ray_crossings`) or on its face (`point_in_face_proj`) (review 5).
- **Global** (review 12; tests and import; quadratic work avoided):
  - no loop self-intersects (`segments_intersect_proj`, with per-face sweep);
  - holes lie inside their face's outer loop (`point_in_face_proj`);
  - no two faces intersect away from shared edges and vertices: **uniform-grid
    binning** of face bounding boxes, then exact triangle–triangle tests on each
    candidate pair, including coplanar overlap;
  - `build_regions()` run on a copy matches the stored regions.

  The global layer runs on every small generated instance, and on `random_city` only in
  `long` tests at reduced scale under the sanitizers.
- **Freeze contract.** Generation monotonicity is a check *between* two complexes, so
  it's a W10 freeze test, not a validator code (review 20).
- **Tests:**
  - every code is triggered by a hand-corrupted complex (one corruption per code);
  - valid generated models give empty reports;
  - **mutation check:** a planted bug in the radial comparator or the wedge rule is
    caught.

### W9 — Test generators (`source/ubertopo/tests/gen/`)

These are deterministic procedural generators with **ground truth**:
- the expected region of every face side;
- the expected counts;
- the expected Betti numbers or genus for the Euler–Poincaré check.

- **Families:**
  - `room_grid(nx, ny, floors, wall: thin|thick, windows: k, glazing: bool)`;
  - `single_room`;
  - `courtyard`;
  - `sealed_cavity`;
  - `pillar_room`;
  - `fence_in_air`;
  - `sensor_and_wire`;
  - `random_city(seed, n_buildings)`, which composes these with random parameters using
    `rng::Stream`.
- **Construction rules** (review 16):
  - shared vertices are keyed by **grid position** (`derive_generator(building,
    "v", ix, iy, iz)`), not by room path, so neighbouring rooms weld;
  - faces are split wherever another face's edge meets them (T-junctions, such as an
    interior wall meeting an exterior wall), so edges really are shared.
- **Coordinates:**
  - Generated on an integer grid that scales to both instantiations: `Lattice` values
    directly, and `double` = integer × 2^-k, which is exact.
  - The `double` and `Lattice` models are therefore *the same geometry*.
- **IDs:** persistent IDs derived from generator paths
  (`derive_generator(building, "face", …)`).

### W10 — Freeze, thaw, serialisation, hashing

- **`freeze(Complex<C>&) -> FreezeReport`:**
  - **Precondition.** It refuses to run while `regions_dirty` is set (review 24).
  - **No-op case.** If the complex is already frozen and unchanged since then
    (`topology_version()` and `geometry_version()` match the stamps recorded at freeze),
    `freeze` does nothing (review 10).
  - **Order:**
    - vertices, edges and faces are renumbered by the **Morton code of their
      centroid**, quantised to 21 bits per axis over the **`TileFrame`'s fixed extent**,
      never the data's bounding box (review 17);
    - the centroid is computed in `double` in the tile frame, which is exact for
      2^-k-grid inputs, so both instantiations agree;
    - ties are broken by persistent ID;
    - loops follow faces;
    - coedges are contiguous per loop, in loop order;
    - shells follow their minimal face-side ID;
    - regions follow persistent ID, with region 0 pinned at index 0.
  - **Permute.** Every domain, including the induced FaceSide map, is permuted through
    `AttrGroup::permute`.
  - **Remap** (review 4). `permute` moves rows but doesn't rewrite the index values
    stored in them. A **remap pass** rewrites every link column through the old-to-new
    maps. The pass lives in `topo/remap.h`, with one entry per link column, and
    `static_assert`s that every `Topo` column of each schema is listed. It covers:
    - `.c.next`/`prev`, `.c.radial_*`, `.c.edge`, `.c.loop`;
    - `.e.vs` and the packed `(edge<<1)|slot` disk links;
    - `.v.e`, `.l.*`, `.f.loop`, `.f.region`, `.f.shell`;
    - `.fs.shell_next` (`2f+s`);
    - the shell and region chains;
    - the `ContainerRef` index, by its kind.
  - **Generations.** Generations of moved slots are bumped.
  - **Frozen caches.** These are built as `Derived` columns or side CSR structures,
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
  - a trailing content hash;
  - frozen caches are rebuilt after `read()`.
- **Hashing:**
  - `content_hash(const Complex&)` is the hash of every group in a fixed order, plus
    the frame.
  - `topology_hash` is the same, through the W1 filtered `hash_into`, excluding `pos`
    and geometry columns. It is used for cross-instantiation comparison.
- **Tests:**
  - freeze, then validate;
  - a non-identity freeze (after random frees) passes validation, i.e. the remap is
    correct, and leaves regions valid;
  - generations after a freeze are ≥ the generations before it, for every surviving
    element;
  - freezing a frozen, unchanged complex is a no-op (identical bytes);
  - `freeze` refuses while `regions_dirty` is set;
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
- Notes in design 01 (the "D" marker means recomputable; sheet shells; flags), 02
  (Lattice arithmetic; ray crossings) and 04, where the implementation differs.
- A DEC entry for P2 decisions and goldens.
- `docs/index.md` and `docs/debugging.md`.
- CMake: `ubertopo_geom`, `ubertopo_ident` and `ubertopo_topo` targets, with tests in
  `source/ubertopo/tests/` (unit and long labels) and compile-fail tests, using
  litestl's pattern.

## 5. Order and dependencies

```
W1 (litestl sort/scan/attr/Vec) ─┐
W2 geom ─────────────────────────┼─► W4 domains/Complex ─► W5 radial ─► W6 builder ─► W7 regions ─► W8 validator
W3 ident ────────────────────────┘                                          ▲             ▲
W9 generators (after W6 API) ───────────────────────────────────────────────┴─────────────┘
W10 freeze/remap/io/hash (after W7) ─► W11 determinism ─► W12 docs
```

Commits are in that order, and each commit builds and passes. The work lands in two
parts (owner decision, §9):

- **P2a:** W1–W6, the combinatorial layer of W8, and simple hand-built fixtures.
- **P2b:** W7, the rest of W8, and W9–W12.

Each part is pushed as a separate commit series.

## 6. Acceptance criteria

1. **Generated models pass.**
   - Every generator family, in both instantiations, builds, gets its regions built,
     passes the combinatorial and embedding layers with an empty report, and matches its
     region ground truth exactly.
   - The global layer passes on every small instance, and on `random_city` at the
     reduced `long` scale.
2. **Determinism:** content hash and bytes are identical across band counts 1, 2, 4 and
   12. Topology hashes are identical across `double` and `Lattice`. The pinned golden
   matches under clang Release, gcc Release and clang `-march=x86-64-v3`.
3. **Exactness checks pass:**
   - derived predicates agree with the big-integer oracle;
   - radial order agrees with the long-double reference on well-separated cases and is
     correct on constructed degeneracies;
   - ray crossings give the analytic winding on the shape set, including grid-aligned
     points.
4. **The validator catches** every hand-corrupted case and both planted mutations.
5. **Compile-time safety:**
   - cross-kind `Ref` misuse and mistyped link-column names fail to compile
     (compile-fail tests);
   - a `Topo` column missing from the remap table fails a `static_assert`.
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
| R1 | The half-plane construction is wrong for some concave or holed face configurations | The review confirmed the construction, given the W6 orientation checks. Independent long-double reference on random fans, constructed concave and hole cases, and a validator re-sort check |
| R2 | `double` shell orientation can't be certified (degenerate shells) | Report it, never guess (§9.1). Generators produce well-separated geometry. `Lattice` is exact |
| R3 | Ray degeneracies (the segment hits an edge or vertex) | Generic integer directions, a fixed retry table, and the retry count in the report. Tests use grid-aligned points that force retries |
| R4 | Touching shells confuse nesting | The shared-face rule (W7.3a) plus face-interior test points (W7.3b). Explicit single-room and thick-wall tests |
| R5 | Scale: O(shells²) nesting on a 10⁵-face city | Bounding-box prefilter. Buildings give many small shells, and containment is local. The W11 runtime is measured, and a BVH follows in a later phase if needed |
| R6 | The builder's determinism leaks through hash-map iteration order | Edge dedup maps are only *queried*; output order always comes from sorted unique keys. Tests shuffle the emission order |
| R7 | Template bloat from `Complex<C>` | Only vertex positions and geometric algorithms are templated (design 01 §10). Region, union-find and remap logic is non-template, over domains |
| R8 | The Lattice bit budget for signed-volume sums | Absolute coordinates ≤ 2^26. With entries in [−1, 1], a 3×3 determinant is at most 4 in absolute value, so each term is ≤ 2^80. A sum of ≤ 2^30 **fan triangles** is ≤ 2^110, which fits `Int128`. A `static_assert`, plus a range check on the fan-triangle count |
| R9 | A link column is missed by the freeze remap | The remap table `static_assert`s schema coverage. A non-identity freeze is followed by full validation |
| R10 | Thread-safety of parallel link writes | Temporary vectors plus per-page `parallel_write` only (W5). Runs under TSan |

## 8. Estimated size

| Item | Code (LOC) | Tests (LOC) |
|---|---|---|
| W1 | ~300 | ~200 |
| W2 | ~700 | ~550 |
| W3 | ~250 | ~150 |
| W4 | ~650 | ~300 |
| W5 | ~350 | ~350 |
| W6 | ~800 | ~400 |
| W7 | ~750 | ~450 |
| W8 | ~1100 | ~450 |
| W9 | — | ~650 |
| W10 | ~750 | ~300 |
| W11 | — | ~200 |

About 5.7k LOC of code and 4k LOC of tests, plus amendment A1 (§12): about 1.2k LOC of
code (the CDT is about 700 of it) and about 0.9k LOC of tests, mostly in W2. W8 grows because of the exact
triangle–triangle tests (review 12), and W10 because of the remap pass.

## 9. Owner decisions (2026-11-18)

1. **Uncertifiable `double` shell orientation: report it.** P2 reports the shell as
   degenerate and doesn't guess. An exact evaluation (a big-integer or expansion sum) is
   written later, when an operation needs it, and only where computing it exactly is
   the right answer rather than cleaning up the geometry to remove the offending face.
2. **Two landings.** P2 lands as **P2a** and **P2b** (§5), each as its own pushed commit
   series.

## 10. Pressure-test review (fresh-context agent, 2026-11-18)

**What the review checked:** design 01/02/04/06, DEC-008/012 and the litestl sources
(`attr/schema.h`, `group.h/.cc`, `column_base.h`, `paged_store.h`, `value_ops.h`,
`type_key.h`, `math/vector.h`, `math/predicates.h`, `util/task.h`, `util/hash128.h`).

**What it confirmed:**
- **The W5 half-plane construction** is sound for concave faces and hole loops, given a
  consistent loop orientation.
- **The angular classification** is sound: 0, upper, π, lower, and the within-class
  `s_a·s_b·orient3d` test.
- **`face_front_sign`** via the lexicographic minimum is valid, because that minimum is
  an extreme point.
- **The R8 bit budget** holds, although it was misstated.

**Verified before revising** (2026-11-18):
- `group.cc` `hashed()` excludes `Temp` and `Derived` columns from hashes and bytes;
- `Vec`'s three-argument constructor zero-fills with `0.0f`;
- `AttrGroup` has `set_default_list` (all columns) and no `version()`;
- integer `orient3d` allows Bits ≤ 30.

**Findings and dispositions:**

| # | Sev. | Finding | Disposition |
|---|---|---|---|
| 1 | Blocker | The closure test ("edges used an even number of times") never fails, so sheets are never classed as open | W7.2: sheet shells are those that hold both sides of every face they touch. They skip the volume calculation and nest like cavities |
| 2 | Blocker | Touching shells are the common case, and the fallback was undefined | W7.3: (a) a shared face on opposite sides means "not inside"; (b) otherwise a face-barycentre test point. Bit budget in W2 |
| 3 | Blocker | `Topo\|Derived` links would be dropped from hashes, bytes and `read()` | W4: links are `Topo` only, and `Derived` is reserved for freeze caches |
| 4 | Blocker | Freeze moves rows but never remaps the indices stored in them | W10: remap pass with `static_assert` coverage; non-identity freeze test; R9 |
| 5 | Major | `ContainerRef` REGION indices can't be known at build time | W6: `ContainerHint`, resolved in `build_regions`; containment checked in the W8 embedding layer |
| 6 | Major | No hole-orientation or degenerate-loop checks (a counter-clockwise hole gives false ties) | W6 face checks; W5 precondition; W8 embedding check |
| 7 | Major | `ray_winding` can't be built for Lattice; axis-aligned retries are degenerate on grids | W2: segment to a far point along generic integer directions, `orient3d<Bits=30>`, budget stated |
| 8 | Major | Region IDs from hints collide; region 0 hint conflicts | W3/W7.5: `region_id(hint, min side ID)`, duplicate warning, `kUnbounded` hint |
| 9 | Major | `free` via `set_default_list` wipes `.gen` (ABA) | W4: reset all columns except `.gen`; FaceSide rows freed with the face |
| 10 | Major | "Freeze twice gives identical bytes" contradicts the generation bump | W10: freezing a frozen, unchanged complex is a no-op |
| 11 | Major | Parallel scattered link writes break litestl's write rules | W5: temporary vectors plus per-page `parallel_write`; R10 |
| 12 | Major | The O(n²) global layer can't run at 10⁵ faces; W8 was underestimated | W8: grid binning, global layer at reduced scale for the city; W8 re-estimated |
| 13 | Major | `Lattice` won't compile with `Vec`; `CoordScalar` needs `+ - neg`; missing type keys | W1 `Vec` fix; W2 `Lattice` operators and type keys; padding specialisation dropped |
| 14 | Minor | Edge direction, disk order and wire rules unspecified | W5 conventions; W6 wire rules |
| 15 | Minor | The emission-index tie-break undermines permutation invariance | W1/W6: ties are errors, and the check stays on in Release |
| 16 | Minor | Generators need grid-keyed vertices and T-junction splitting | W9 construction rules |
| 17 | Minor | Morton order across instantiations; region 0 index; shell keys | W2 quantum = 2^-k; W10 frame-extent Morton, region 0 pinned, shells by min side ID |
| 18 | Minor | `topology_hash` needs filtered hashing; no `AttrGroup::version()` | W1 litestl additions |
| 19 | Minor | R8 bound misstated; volume sign convention backwards | R8 corrected; W7.2 uses outward normals |
| 20 | Minor | Euler–Poincaré needs ground truth; generation monotonicity isn't a single-complex check | W9 ground truth; monotonicity moved to a W10 test |
| 21 | Minor | The flood fill's `radial_prev` side was unspecified | W7.1: replaced by canonical union-find over wedge pairs |
| 22 | Minor | Suggests answering §9.1 with an exact expansion sum | **Declined** for P2 by owner decision §9.1; noted there as the future approach |
| 23 | Minor | `geom` shouldn't depend on `topo` types; `Domain` name clash | W2 span inputs; W3 `ElemKind` |
| 24 | Minor | Rebuilding regions drops region attributes; freeze while `regions_dirty` | W7.6 carry-over by ID; W10 precondition |

## 11. Implementation notes

### P2a (2026-11-18)

**Landed:**
- W1–W6;
- the combinatorial layer of W8;
- builder band-count determinism, taken early from W11.

The realised API is [design 07](../design/core-data-model/07-topology-api.md), and the
decisions are recorded in DEC-013.

**Deviations and additions:**
- **`ray_winding`** takes triangle spans and an integer pre-scale (`scale_log2`). The
  W7.3b interior points use `interior_point4`, i.e. `(a+b+2c)/4` scaled by 4, rather
  than a barycentre scaled by 3: division by 4 is exact in `double` on dyadic grids.
- **Point-in-polygon** reports a boundary point explicitly. It needs no
  Simulation-of-Simplicity tie-breaking.
- **The builder** stores region container hints in `.v.container_hint` and
  `.e.container_hint`. Its output is all-or-nothing on errors.
- **The validator** runs its region checks only when `regions_dirty` is clear. In P2a
  they are tested on regions assigned by hand from geometry. Because those regions
  come from geometry, a clean invariant W also verifies that the radial order and the
  wedge rule agree on real models.
- **Deferred to P2b:**
  - the Euler–Poincaré cross-check, which needs generator ground truth;
  - the generation-monotonicity test, which belongs with freeze.
- **litestl additions beyond W1:** `Vector::data() const`.

**Verification:**
- **Mutation checks:**
  - geom: radial sign, point-in-polygon rule, segment overlap, winding sign;
  - radial: half-plane sign, reference sign;
  - validator: wedge rule flipped, radial order reversed.
- **Configuration matrix:**
  - UberTopo (11 suites, including 3 compile-fail and compile-ok checks): clang Release,
    gcc Release, clang `-march=x86-64-v3`, gcc Debug + ASan, gcc Debug + UBSan and gcc
    TSan (with `LITESTL_ATTR_CHECKS`) all pass.
  - litestl (39 tests, on `ubertopo/p2-support`): clang-rel, gcc-rel, clang-v3,
    clang-noi128, gcc-tsan, gcc-ubsan and gcc-asan all pass.

### P2b (2026-11-18)

**Landed:**
- amendment A1, with litestl `incircle_sos`, `geom/cdt/` and the builder's
  projectable faces;
- W7, the rest of W8, W9, W10 and W11.

The decisions are recorded in DEC-014, and design 07 is complete.

**Deviations and additions:**
- **CDT.**
  - **Insertion.** Bowyer–Watson cavities are used for vertex insertion, with ghost
    triangles and a 1-D start. The plan's Lawson-flip wording describes an equivalent
    construction.
  - **The orientation rule** is the projectability check.
  - **API.** The `cdt_proj` signature has no `sigma` argument; the front winding is
    derived from the outer loop.
- **Regions:**
  - union-find runs serially;
  - volumes and nesting are parallel per shell;
  - there is no stored containment tree of outer shells (the innermost container is
    chosen by volume);
  - a `nested_solids` family was added to make the innermost choice observable, after
    a mutation survived without it.
- **Euler–Poincaré** is checked per vertex, as a planarity test of the link rotation
  system, instead of from generator Betti numbers. It caught a reversed radial cycle
  before the embedding layer did.
- **The face–face test** uses each face's canonical CDT. Planar faces use it too,
  because loop fans of non-convex faces aren't surfaces.
- **The global layer** runs at full scale on every generator family, at 25 buildings.
  The 200-building city runs the embedding layer.
- **Freeze:**
  - shells and regions keep `build_regions()`'s canonical order (identity
    compaction);
  - generation rule: `max(own, slot's old) + 1` for moved elements;
  - the frozen caches are side structures stamped with `Complex::version()`, not
    `Derived` columns.
- **The test frame quantum is 2^-8**, so `double` and `Lattice` produce the same
  Morton orders.

**Acceptance (§6):**
1. Every generator family, in both instantiations, matches its ground truth exactly
   and passes all three validation layers.
2. Bytes and content hashes are identical at 1, 2, 4 and 12 bands; topology hashes are
   identical across `double` and `Lattice`; the pinned golden holds (matrix below).
3. The oracles agree:
   - big-integer oracles for the predicates;
   - the formal-polynomial oracle for `incircle_sos`;
   - global Delaunay, validity and area oracles for the CDT;
   - `atan2` for radial order;
   - analytic ray winding.
4. The validator catches one corruption per code, plus the planted wedge and radial
   mutations.
5. Compile-fail tests cover cross-kind refs, implicit indices and a missing remap rule.
6. The full matrix passes (listed below).
7. Hygiene: no file over 40 KB and no `CLAUDENOTE:`.

**Configuration matrix:**
- UberTopo (17 suites, including compile-fail checks): clang Release, gcc Release,
  clang `-march=x86-64-v3`, gcc Debug + ASan, gcc Debug + UBSan and gcc TSan (with
  `LITESTL_ATTR_CHECKS`) all pass. The sanitizer runs use `LITESTL_TEST_SCALE_DIV=4`.
- litestl (40 tests): clang-rel, gcc-rel, clang-v3, clang-noi128, gcc-tsan, gcc-ubsan
  and gcc-asan all pass.
- **Fix found by the matrix:** ASan's leak checker flagged litestl's per-thread tracker
  lists, which now exist because P2b allocates in worker threads. The UberTopo tests
  now use litestl's `lsan.supp` (see `docs/debugging.md`).

**Merge.** At the owner's request, litestl `master` was fast-forwarded to `ccf0386`
(from `ubertopo/p2-support`), which the UberTopo submodule pointer already references.

**Mutation checks:**
- CDT: tie-break ranks, cavity choice, winding;
- projected half-plane sign;
- `build_regions`: volume sign, shared-face rule, wedge pairing, innermost container;
- remap: a wrong rule fails tests, and a missing rule fails the `static_assert`.

## 12. Amendment A1 — opt-in projectable faces (2026-11-18)

**Status:** v3, final. v2 was revised after the first fresh-context pressure test
(§12.6). v3 swaps ear clipping for a canonical CDT at the owner's request, and was
revised after its own pressure test (§12.7). It is ready for P2b.

### 12.1 Motivation and owner decision

The P2a builder requires every face to be **exactly planar**. That's the right default:
everything the builder computes treats a face as a single plane, and there is no
tolerance. But sloped roofs and rotated walls whose corners are rounded to the
`Lattice` grid, or rounded in `double`, are generally not exactly coplanar, so they are
rejected.

The owner chose this relaxation:
- **The rule.** A face may be non-planar if it is **projectable**: its projection onto
  one axis-aligned plane is a simple polygon with holes.
- **The geometry** is the **canonical exact triangulation** of that projection, lifted
  back to 3D. It is not a best-fit plane.
- **Opt-in.** Client code must **explicitly enable** this weaker form. Planar faces stay
  the default, and a complex built without the option is unchanged.

### 12.2 Definitions

**Canonical loop order.** Everything below walks each loop starting at its
lexicographic-minimum vertex, which is a projected lexicographic comparison where a
projection is involved. Loops are taken in the order of their lexicographic-minimum
vertices. A change of loop head therefore never changes the result (review 4).

**Projectable face.** A face is projectable along axis `k` when all of these hold
exactly in the projection that drops `k`:
- every loop is simple and non-degenerate;
- no two loops intersect or touch;
- the outer loop (the one holding the lexicographic-minimum vertex) contains every
  other loop;
- **no hole contains another hole** (review 3);
- every hole has the opposite orientation to the outer loop.

The lifted surface is then a height field over the projection plane, so the face can't
intersect itself.

**Axis choice** (review 7). The builder tries the dominant axis of the approximate
Newell normal first, then the other two axes in index order, and uses the first along
which the face is projectable.
- The Newell sum follows the canonical loop order, so it doesn't depend on loop heads.
- Only the *ranking* of the axes is approximate. The test of each axis is exact.
- The Newell axis is identical in `double` and `Lattice` (verified by the review).

**Stored axis.** The chosen axis is stored on the face (§12.3), and is the face's
geometry from then on.
- The validator requires the face to be projectable along its **stored** axis.
- It does **not** require the stored axis to equal the choice rule's result. That
  equality is checked only on freshly built complexes, in tests (review 4).

**Canonical triangulation `T(f)`: an exact constrained Delaunay triangulation**
(owner decision, v3). This replaces v2's ear clipping and bridges.

**Problem statement.** `T(f)` is the CDT, in the projection along the stored axis, of
the face's loop vertices, constrained by every loop edge. Only triangles inside the
face are kept: inside the outer loop and outside every hole. Included vertices and
edges (sensors, slits) are **not** part of the geometry. A future tessellator may add
them as Steiner constraints of the *render* mesh.

**Uniqueness.** `T(f)` depends only on the projected point set and the constraint
edges, not on insertion order, loop heads or loop order.
- **Ties.** A CDT is unique once cocircular ties are broken. Ties are broken by
  **symbolic perturbation of the lifting**: vertex `i` is lifted to
  `x² + y² + ε^(rank(i))`, where `rank` is the projected lexicographic order of the
  vertex.
  - The perturbation is `ε^(rank+1)`, so it is infinitesimal for every rank,
    rank 0 included.
  - An `incircle(a,b,c,d)` that evaluates to exactly 0, with `a,b,c` counter-clockwise,
    is resolved by the **vertex of smallest rank among the four**. That vertex has the
    largest perturbation. The result is the sign of its coefficient (review v3, derived
    and checked exhaustively on a 5×5 grid):

    | Smallest rank | Sign |
    |---|---|
    | `a` | `+orient2d(b,c,d)` |
    | `b` | `−orient2d(a,c,d)` |
    | `c` | `+orient2d(a,b,d)` |
    | `d` | `−orient2d(a,b,c)` |

    The coefficient is never zero on a tie, because three distinct cocircular points
    are never collinear.
  - Ranks are lexicographic comparisons, so they are identical in `double` and
    `Lattice`.
- **Collinearity is not perturbed.** `orient2d` stays exact, the algorithm never forms
  zero-area triangles, and collinear runs (T-junction vertices) come out as fans of
  proper triangles.

**Algorithm** (`geom/cdt.h`, re-implemented per DEC-002):
1. **Vertices.** Insert the vertices in projected lexicographic order into an
   incremental Delaunay triangulation, using Lawson flips with the perturbed
   `incircle`.
   - **Duplicate points.** Two vertices with the same projected position
     (`lex_compare == 0`) make the face `NotProjectable`. This is reported, never
     asserted: the perturbation needs distinct ranks.
   - **Degenerate start** (review v3, 2). While every point inserted so far is
     collinear, the state is a 1-D chain of ghost-only edges. The first non-collinear
     point fans to the whole chain.
   - **Ghost rules:**
     - a point `d` is in the ghost triangle `(a, b, ∞)` iff `orient2d(a,b,d) > 0`, or
       `orient2d(a,b,d) = 0` with `d` strictly inside the segment `ab`;
     - new triangles are created only on hull edges with strict `orient2d > 0`;
     - a point on a hull edge's collinear extension extends the hull.
   - The convex hull is closed with **ghost triangles** on a symbolic vertex at
     infinity, so no bounding triangle is constructed. A large bounding triangle would
     leave the `Lattice` range.
   - **Point location** (review v3, 6). In lexicographic order every new point lies
     strictly outside the current hull, or on its collinear extension. Production
     location is therefore a hull walk from the previous vertex, which is always a hull
     vertex.
   - **General location.** The uniqueness test hook, and the later BRIO ordering,
     insert in arbitrary order. They need general location, including inside a
     triangle and on an interior or hull edge (edge splits). That path is implemented
     and tested separately.
2. **Constraints.** Insert the constraint edges in sorted (min rank, max rank) order.
   Each one removes the triangles it crosses, and the two cavity polygons are
   retriangulated (review v3, 3).
   - **Cavity algorithm.** Each cavity is retriangulated as its **own CDT, with the
     same perturbed `incircle`**, using Anglada's recursive method: for the base edge
     `(a, b)`, pick the cavity vertex `c` with strict `orient2d(a,b,c) > 0` whose
     perturbed circle `(a,b,c)` contains no other cavity vertex, then recurse on the two
     sides.
   - **Why perturbed.** An unperturbed choice breaks uniqueness on cocircular grid
     cavities.
   - **Failure.** A constraint that crosses another constraint, or passes through a
     vertex, can't occur on a projectable face. If it does, insertion fails and the
     face is `NotProjectable`. A partial `T(f)` is never returned.
3. **Inside test** (review v3, 8). Normalise so the outer loop is counter-clockwise.
   - **Kept triangles.** A triangle is kept when it lies to the left of the directed
     loop edges, found by flood fill from the left triangle of each directed loop edge
     without crossing constraints.
   - **Orientation rule.** For every directed loop edge, the triangle on its left must
     be kept and the one on its right must not.
   - **Rejections.** A violation is a precondition failure. A nested hole, a hole
     outside the outer loop and a crossing are `NotProjectable`, and a wrong hole
     winding alone is `HoleOrientation`.
4. **Output:**
   - **Winding** (review v3, 1). Each triangle is emitted in the face's **front
     winding**. CDT triangles are counter-clockwise in projection, so they are reversed
     when σ = −1.
   - **Order.** Each triangle is rotated to start at its smallest index, and the
     triangles are sorted by index triple.
   - **Indices** (review v3, 9) are **stored corner indices**: positions in the face's
     stored loop order, with the loops in storage order. The invariance tests compare
     triangles by vertex ID, because loop heads change the indices.
5. **Predicates** (review v3, 10). The algorithm uses `orient2d_proj`, plus new
   `incircle_proj` and `incircle_perturbed_proj` in `geom/predicates.h`, in `drop_axis`
   order.
   - **litestl.** The generic rank-perturbed predicate goes into litestl
     `math/predicates.h` as `incircle_sos(a,b,c,d, ra,rb,rc,rd)`, for
     `double`/`float`/`int64`, because CLAUDE.md puts general predicates in litestl.
     The CDT itself, templated on UberTopo's `CoordType`, stays in UberTopo `geom/`.
   - **Budgets.** `Lattice` uses `incircle<26>` (Int128, ≤ 3·2^110), and `double` uses
     the adaptive predicate. `T(f)` uses only stored vertices, never `ext_bits` points.
   - **File layout.** The CDT is split under `geom/cdt/`, into `triangulation.h`,
     `insert.h` and `constrain.h`, to stay under 40 KB per file. It uses litestl
     containers.

**Planar faces.** P2 computes `T(f)` only for flagged faces. The same CDT is the
intended triangulator for planar faces' render meshes later, so one triangulator
serves both geometry and tessellation.

**Planar faces** are never flagged. They keep the P2a rules, including fans for
volumes and ray tests, which are correct on planar faces (review 11).

### 12.3 API and persistence

- **Opting in.** `BuildPolicy::allow_projectable_faces` (default `false`) enables the
  option for one build.
- **Error codes:**
  - without the option, a non-planar face is `NonPlanarFace`, as in P2a;
  - with it, a non-planar face must be projectable along some axis;
  - a projection that fails only on hole winding reports `HoleOrientation`, and any
    other failure reports the new code `NotProjectable` (review 15).
- **Flags** (review 9). `.f.flags` gets a two-bit field `ProjAxis` (mask `0x30`) that
  holds `axis + 1`. `0` means "not projected", so there are no inconsistent states.
- **Reserved bits** (review 6). The builder rejects a face whose client-supplied flags
  set `ProjAxis` bits, with the new code `ReservedFaceFlags`.
- **Persistent opt-in** (review 6). The complex records `allows_projectable` as a
  global builtin. It is serialised, so it survives `read()` and is visible to P3. The
  validator reports any face with `ProjAxis ≠ 0` when that bit is clear.
- **Wires** (review 5). In P2, a wire included in a flagged face is rejected with the
  new code `WireOnProjectedFace`. A straight wire leaves the surface where it crosses a
  fold.
- **Triangulation entry points:**
  - `geom::cdt_proj(PolyView, axis, sigma, out)` returns a status (`Ok`,
    `NotProjectable`, `HoleOrientation`), and outputs stored corner indices in front
    winding. **`is_projectable_proj` is this call**: the CDT succeeding plus the
    orientation rule, with no separate O(E²) crossing test (review v3, 8);
  - `topo::face_triangles(complex, f, out)` works only for flagged faces, and returns
    `false` on failure (review 11).
- **Radial half-planes** (review 10). `radial_order` takes a **per-coedge half-plane
  provider** instead of a `FaceFrame` lookup. For a flagged face, the builder computes
  `T(f)` once and records each boundary coedge's third vertex. For a `T(f)` triangle,
  `s = +1` always, and this is asserted.
- **Report counts.** `BuildReport` counts projected faces.

### 12.4 Changes to work items

| Item | Change |
|---|---|
| W2 | Add `geom/cdt/` (`cdt_proj`, which is also the projectability check), the projected `incircle` predicates, and litestl `incircle_sos`. **Tests** (review v3, 4): <ul><li>**global** constrained-Delaunay property: for every kept triangle and every vertex strictly inside its perturbed circumcircle, that vertex is not visible from the triangle's interior, by segment tests against the constraints;</li><li>**validity**: every triangle has `orient2d > 0` in the CDT frame and `vol3` agrees with the face front for every emitted triangle (mirrored σ = −1 family); each interior edge is shared by exactly two triangles with opposite directions; each loop edge lies in exactly one triangle;</li><li>the triangle-area sum equals the polygon area, compared exactly with the big-integer oracle;</li><li>**uniqueness**: the test hook shuffles both vertex and constraint insertion order, and the output must be identical;</li><li>loop-head and loop-order invariance, compared by vertex ID;</li><li>identical triangles across the two instantiations;</li><li>`incircle_sos` against a formal-polynomial oracle: the coefficient vector in ε, compared lexicographically, so no ε value has to be chosen (review v3, 5);</li><li>cocircular grids, collinear and T-junction runs, and degenerate starts (the first k points collinear);</li><li>duplicate projected points, nested holes, holes outside or crossing the outer loop, wrong winding;</li><li>**performance**: a 10⁴-vertex random polygon, and a convex-arc worst case (a finely tessellated round roof outline), each with a stated time budget</li></ul> |
| W5 | A flagged face's half-plane at `e` is the plane of the `T(f)` triangle that has `e` as an edge: `q` is its third vertex and `s = +1`. Unflagged faces keep the P2a rule. **Tests:** <ul><li>both rules agree on planar faces;</li><li>a mirrored (σ = −1) flagged face;</li><li>a mixed flagged/unflagged fan, checked against `atan2`</li></ul> |
| W6 | Planarity becomes "planar, or projectable when the policy allows it", tried on axes in the §12.2 order. Adds `NotProjectable`, `ReservedFaceFlags` and `WireOnProjectedFace`; sets `ProjAxis` and `allows_projectable`; counts projected faces |
| W7 | Volumes and `ray_winding` use `T(f)` for flagged faces and fans for planar ones. The nesting test points of W7.3b come from `T(f)` triangles on flagged faces (review 8) |
| W8 | **Embedding layer:** <ul><li>a flagged face must be non-planar, or the flag is spurious;</li><li>it must be projectable along its stored axis;</li><li>flagged faces are allowed only when `allows_projectable` is set;</li><li>a vertex included in a flagged face must lie exactly on `T(f)`: the projected point is located in a **closed** triangle, then `vol3 == 0` against it. A point that projects onto a `T(f)` edge or vertex is accepted against any closed triangle containing it (review 5; review v3, 12);</li><li>mutation tests on the axis bits.</li></ul>**Global layer:** face–face intersection uses `T(f)` |
| W9 | A `sloped_roof` / `rotated_wall` family, built with the option enabled. Snapping moves roof corners vertically, keeping them within the wall tops' footprint, so there are no face–face intersections and the ground truth holds (review 15). Planar families build without the option |
| W10 | Freeze stores `T(f)` for flagged faces as a `Derived` CSR of **per-face local corner indices**, built after permute and remap, so it never enters the remap table (review 13). `read()` rebuilds it, and serialisation carries only flags and `allows_projectable` |
| W11 | The determinism suite includes the sloped-roof family in both instantiations |
| W12 | Docs: <ul><li>design 01 §2: the `ProjAxis` flag, and the meaning of `.f.surface` NONE becomes "planar, or projected when flagged";</li><li>design 01 §4, §5 and §8;</li><li>07-topology-api;</li><li>a DEC entry for the owner rule;</li><li>`docs/index.md` (review 14)</li></ul> |

### 12.5 Risks and caveats

| # | Risk | Mitigation |
|---|---|---|
| A1-R1 | **Editing** (P3/P5). On a flagged face, `T(f)` *is* the geometry, and it isn't local: **any** loop edit changes it. That includes moving a vertex, splitting an edge (inserting a 3D-collinear vertex moves the fold) or adding a hole (review 4) | Flagged faces are identifiable. P3 must either store `T(f)` as persistent data for edited faces, or treat every loop edit on a flagged face as a geometry edit that re-snaps or re-validates. The decision is recorded for P3. Planar faces are unaffected. With a CDT, one moved vertex can flip triangles far away, so locality is worse than with ear clipping (review v3, 12) |
| A1-R2 | A **nearly vertical** projection gives thin polygons with many exact near-degeneracies | The results stay correct, though slower. The axis order prefers the dominant axis. Generators avoid the case |
| A1-R3 | **CDT cost and correctness** | Sorted insertion can cost Θ(n²) flips when the points lie on a convex arc (review v3, 7). Building faces are small, and a convex-arc performance test with a budget tracks the worst case. Terrain-scale use needs a deterministic BRIO order (keyed RNG plus lexicographic order within each round) on the general-location path. Correctness is covered by the global Delaunay, validity and uniqueness tests in both instantiations |
| A1-R5 | **Sign rule of the perturbed `incircle`** | The rule is in §12.2. The review derived it and checked it exhaustively on a 5×5 grid. Tests compare against a formal-polynomial oracle |
| A1-R4 | **Misuse as a tolerance** | Opt-in per build, persisted on the complex, counted in the report, and flagged per face. A per-face request bit (a stricter audit trail) can be added if wanted |

### 12.6 Pressure-test review (fresh-context agent, 2026-11-18)

**What the review verified:**
- the `T(f)` half-plane rule (`s = +1` whatever σ is);
- that FRONT and the wedge rule hold locally on a height field, hole edges included;
- slivers;
- mixing flagged and unflagged faces about an edge;
- that each boundary edge belongs to exactly one triangle, given the §12.2 conditions;
- that a height field doesn't intersect itself;
- that `T(f)` is needed for volumes;
- that the Newell axis is identical in both instantiations;
- the `Lattice` budget for squared distances, which v2 no longer uses;
- that apertures need nothing extra;
- that the flag bits fit;
- that bridges exist in ascending lexicographic-minimum order.

**Findings and dispositions:**

| # | Sev. | Finding | Disposition |
|---|---|---|---|
| 1 | Major | The ear test was underspecified, because `point_in_poly_proj` isn't an ear test. Collinear and T-junction vertices gave zero-area triangles and gaps | §12.2: exact diagonal test with strict convexity, touch-blocking and InCone; no zero-area triangles; resume at `p`; failure means `NotProjectable` |
| 2 | Major | Bridges: visibility didn't cover all loops; repeated targets were ambiguous; squared distance isn't exact in `double`, which breaks cross-instantiation equality | §12.2: lexicographically greatest visible `v < h`, with visibility against every loop, a wedge test for copies, and no arithmetic |
| 3 | Major | Nested holes weren't excluded | §12.2: no hole contains another |
| 4 | Major | `T(f)` depended on loop heads and was non-local under edits; the validator's axis check conflicted with the stored axis | §12.2: canonical loop order; validator checks the stored axis (the rule match only on fresh builds); A1-R1 extended |
| 5 | Major | Projection-only containment let sensors float off the surface; included wires weren't addressed | W8: exact on-`T(f)` check; §12.3: `WireOnProjectedFace` in P2 |
| 6 | Major | Clients could set the flag bits by hand; the opt-in didn't persist | §12.3: `ReservedFaceFlags`; a persisted `allows_projectable` global, checked by the validator |
| 7 | Minor | Dominant axis only, versus the owner's "one axis-aligned plane" | §12.2: dominant axis first, then the others in index order |
| 8 | Minor | The W7.3b test point came from a fan triangle | W7: `T(f)` triangles on flagged faces |
| 9 | Minor | The flag encoding allowed inconsistent states | §12.3: `axis + 1` in two bits |
| 10 | Minor | The `radial_order` API, and recomputing `T(f)` per coedge | §12.3: per-coedge provider, precomputed per face; assert `s = +1` |
| 11 | Minor | `face_triangles` on planar faces could fail and was expensive | §12.2/§12.3: flagged faces only, with a `bool` return; planar faces keep fans |
| 12 | Minor | Exact area needs Int128 or an expansion; missing test cases | W2/W5/W8 test lists |
| 13 | Minor | Freeze cache layout | W10: local corner indices, built after remap |
| 14 | Minor | Docs to update | W12 row |
| 15 | Minor | Error-code precedence; generator snapping must avoid face–face intersections | §12.3 precedence; W9 snapping rule |

### 12.7 Pressure-test review of v3 (CDT, fresh-context agent, 2026-11-18)

**What the review verified:**
- **Uniqueness and insertion-order independence** of the rank-perturbed CDT, with
  constraints too: constrained diagonals are forced, the other ties are generic, and no
  vertex disappears.
- **The domain-restricted CDT** equals the hull CDT.
- **The sign rule** (now in §12.2), checked exhaustively on a 5×5 grid against an exact
  evaluation at ε = 2^-70. It needs only `orient2d`.
- **Bit budgets:** `Lattice` `incircle<26>` sums to ≤ 3·2^110, and the `double` domain
  is fine.
- **Cross-instantiation determinism.**
- **Each boundary edge is in exactly one kept triangle.**

**Findings and dispositions:**

| # | Sev. | Finding | Disposition |
|---|---|---|---|
| 1 | Major | CDT triangles are counter-clockwise in projection, so on σ = −1 faces they are the opposite of the front winding | Output step: emit in front winding; W2 test of `vol3` against the face front on the mirrored family |
| 2 | Major | The degenerate start (collinear first points) and the ghost rules weren't specified | Step 1: 1-D chain state; ghost containment, creation and extension rules; tests |
| 3 | Major | The cavity method was underspecified; uniqueness needs the perturbed `incircle` in cavities | Step 2: Anglada's recursive cavity CDT with the perturbed `incircle` and strict `orient2d` |
| 4 | Major | The local-Delaunay oracle isn't independent; an area sum can hide an overlap cancelled by a gap | W2: global visibility-based Delaunay test, validity checks, shuffled constraint order |
| 5 | Minor | `ε^0 = 1` isn't infinitesimal; the sign rule wasn't written down | `ε^(rank+1)`; rule written into §12.2; formal-polynomial oracle |
| 6 | Minor | Production location is only a hull walk; the shuffled hook needs general location | Step 1: general location implemented and tested separately |
| 7 | Minor | "Near-linear" cost was unproven (Θ(n²) on convex arcs); the projectability check could be O(E²) | A1-R3; convex-arc performance test; projectability is the CDT call itself |
| 8 | Minor | Checks were redundant with what the CDT detects; duplicate projected points | Step 3: orientation rule replaces parity and the separate checks; duplicates are `NotProjectable` |
| 9 | Minor | The output index convention was ambiguous | Stored corner indices; invariance compared by vertex ID |
| 10 | Minor | `incircle_proj` was missing; where the predicates live | `geom/predicates.h` additions; `incircle_sos` in litestl; the CDT in UberTopo `geom/cdt/` |
| 11 | Minor | §8 sizing and the 40 KB file split | `geom/cdt/` split; §8 updated |
| 12 | Minor | Included vertices on `T(f)` edges; P3 non-locality is worse with a CDT | W8 closed-triangle rule; A1-R1 note |
