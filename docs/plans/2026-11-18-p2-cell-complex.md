# Plan P2 — Cell complex core (UberTopo `geom/`, `ident/`, `topo/`)

- **Status:** v2, revised after the fresh-context pressure test (§10). **P2a implemented** (§11); P2b next.
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

About 5.7k LOC of code and 4k LOC of tests. W8 grows because of the exact
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

## 12. Amendment A1 — opt-in projectable faces (2026-11-18)

**Status:** draft. A fresh-context pressure test (§12.6) is required before
implementation.

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

- **Projectable face.** A face `f` is projectable along an axis `k` when all of these
  hold in the projection that drops `k` (`drop_axis`):
  - every loop is simple;
  - no two loops intersect or touch;
  - exactly one loop (the outer loop, the one holding the lexicographic-minimum
    vertex) contains all the others;
  - the holes have the opposite exact orientation to the outer loop;
  - no projected loop is degenerate.

  It follows that the lifted surface is a height field over the projection plane, so
  the face can't intersect itself.
- **Projection axis.** The axis is the dominant component of the approximate Newell
  normal (`project_axis`, ties to the smaller axis). Only that axis is tried; if the
  face isn't projectable along it, the face is rejected.
  - The axis is **stored**, in two bits of `.f.flags`, so later vertex edits (P3) can't
    silently move a face's geometry to a different projection.
- **Canonical triangulation `T(f)`:**
  - **Method.** Ear clipping in the projection, with exact `orient2d_proj` and
    `point_in_poly_proj` tests.
  - **Holes** are joined to the outer loop by deterministic bridges. Each hole, taken
    in order of its lexicographic-minimum vertex, is bridged from that vertex to the
    nearest visible vertex of the current outer boundary. "Nearest" is an exact
    comparison of squared projected distances, with ties broken by lexicographic
    order and then by loop position. Visibility is an exact `segments_intersect_proj`
    test.
  - **Ears** are clipped in a fixed scan order, starting at the outer loop's first
    vertex.
  - **Result.** `T(f)` depends only on the loops as given (vertex positions and the
    start of each loop), so it is identical across thread counts and across `double`
    and `Lattice`.
  - **Bridges and diagonals** are not topology. `T(f)` is a recomputable cache.
- **Planar faces under the option.** A face that is exactly planar is built exactly as
  today, without the flag. Any triangulation of a planar face describes the same
  surface, so nothing downstream changes for it.

### 12.3 API

- **Opting in.** `BuildPolicy::allow_projectable_faces` (default `false`) is the opt-in.
- **Rejection.** Without it, a non-planar face is still rejected with `NonPlanarFace`.
  With it, the face must be projectable, or it is rejected with the new code
  `NotProjectable`.
- **Flags.** The builder sets `face_flags::Projected` on every accepted **non-planar**
  face, and records the axis in `face_flags::AxisMask` (two bits). No other face ever
  carries these bits.
- **Triangulation cache.** `geom::triangulate_proj(PolyView, axis, out_triangles)`
  computes `T(f)`, and `topo::face_triangles(complex, f, out)` returns it, from the
  cache or computed. For an unflagged planar face, `face_triangles` returns the same
  canonical triangulation, so every caller has a single path.
- **Report counts.** `BuildReport` counts projected faces. That makes visible how much
  of a model depends on the weaker form.

### 12.4 Changes to work items

| Item | Change |
|---|---|
| W2 | Add `triangulate_proj`, and `is_projectable_proj` (the exact simplicity, containment and winding checks above). Tests: an oracle comparison against brute-force exact checks on random small grids; triangle-area sum equals polygon area (exact, projected); determinism under rotation of input order of *other* faces; failure cases (self-touching loop, hole outside, hole crossing outer) |
| W5 | For a flagged face, the half-plane at edge `e` is the plane of the **triangle of `T(f)` that contains `e`**: `q` is that triangle's third vertex. An unflagged face keeps today's rule. Both rules agree on planar faces, which is a test |
| W6 | Planarity check becomes "planar, or projectable when the policy allows it". New code `NotProjectable`. Set the flag and axis bits. Count projected faces |
| W7 | Shell orientation volumes and `ray_winding` use `T(f)` for flagged faces, instead of the loop fans. That makes them watertight with neighbouring faces, since boundary edges are shared straight segments |
| W8 | The embedding layer checks that a flagged face is non-planar (otherwise the flag is spurious), that it is projectable along its stored axis, and that the stored axis is the one the rule chooses. An unflagged face must still be exactly planar. The global face–face intersection test uses `T(f)`. The containment check for a vertex included in a flagged face uses the projection only (it is weaker than "on the surface"), and this is documented |
| W9 | Add a `sloped_roof` / `rotated_wall` generator family. It snaps quads to the lattice so that some faces become non-planar, and it builds them with the option enabled. The planar families build with the option disabled |
| W10 | Freeze stores `T(f)` for flagged faces as a `Derived` CSR cache, rebuilt after `read()`. Serialisation carries only the flags |
| W11 | The determinism suite includes the sloped-roof family in both instantiations |

### 12.5 Risks and caveats

| # | Risk | Mitigation |
|---|---|---|
| A1-R1 | **Editing** (P3/P5): on a flagged face, `T(f)` *is* the geometry. Splitting along a chord that isn't an edge of `T(f)`, or moving one vertex, changes the surface | The flag makes such faces identifiable. P3 operators must split flagged faces only along `T(f)` edges, or re-snap them; the rule is recorded for P3. Planar faces are unaffected |
| A1-R2 | A **nearly vertical** projection makes the projected polygon thin, with many exact near-degeneracies | Exactness keeps the results correct, though slow. Generators avoid it. The validator recomputes the axis rule |
| A1-R3 | **Triangulation cost** on large faces (O(n²) ear clipping plus bridging) | Building faces are small. A CDT replaces ear clipping if profiling demands it. The canonical-result contract still holds |
| A1-R4 | **Misuse as a tolerance.** The option hides bad imports | It is opt-in per build, counted in the report, and flagged per face, so it can be audited |

### 12.6 Pressure test

_To be filled in by a fresh-context review agent before implementation._
