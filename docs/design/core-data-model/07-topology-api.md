# 07 — Topology API (as implemented)

- **Status:** P2 complete 2026-11-18 (plan [P2](../../plans/2026-11-18-p2-cell-complex.md):
  P2a, then P2b with amendment A1). P3a editing complete 2026-11-18 (§14; plan
  [P3](../../plans/2026-11-18-p3-editing.md)). P4a curves (§16) and P4b surfaces
  (§17) complete 2026-11-21.
- **Scope:** the realised API of `geom/`, `ident/` and `topo/`. The design rationale
  stays in [01](01-cell-complex.md), [02](02-coordinates-and-predicates.md) and
  [04](04-identity-versions-journal.md); this file records names, contracts and the
  places where the implementation differs from the design.

## 1. Modules and targets

| Directory | CMake target | Kind | Contents |
|---|---|---|---|
| `source/ubertopo/geom/` | `ubertopo_geom` | header-only | `coord.h` (`Lattice`, `CoordTraits`, concepts, `TileFrame`), `predicates.h` (derived exact predicates), `cdt/` (canonical exact CDT) |
| `source/ubertopo/ident/` | `ubertopo_ident` | header-only | `id.h` (`PersistentId`, domain tags, derivations), `ref.h` (`Ref`, `Handle`, `ContainerRef`) |
| `source/ubertopo/topo/` | `ubertopo_topo` | static | `types.h`, `schema.h/.cc`, `domain.h`, `complex.h`, `radial.h`, `builder.h/.cc`, `regions.h/.cc`, `validate.h/.cc`, `validate_geom.cc`, `remap.h`, `freeze.h/.cc`, `io.h/.cc` |
| `source/ubertopo/tests/gen/` | (tests) | header-only | `buildings.h`: generators with ground-truth region labels |

- **Includes.** `source/` is the include root: `#include "ubertopo/topo/complex.h"`.
- **Linking tests.** litestl's `platform` is an OBJECT library, so its objects must be
  linked into each executable directly. `ut_test()` does this.

## 2. Coordinates (`geom/coord.h`)

- **`Lattice`** is a strong `int64_t`:
  - it has an `explicit` constructor, `+`, binary `-`, unary `-`, and comparisons;
  - it has no multiplication and no implicit conversions.
- **`CoordTraits<T>`** for `double`, `float` and `Lattice` provides:
  - `Point`, `Point2`;
  - `is_exact` (true only for `Lattice`, whose additions are exact);
  - `to_double`, `from_int`;
  - `orient2d`, `orient3d`, `incircle`, `incircle_sos` (rank tie-break), `compare`;
  - `orient3d_ext`: `Lattice` with a 30-bit range, for constructed points;
  - construction helpers: `from_int_pow2`, `ext_ok`, `add_exact` (`double` checks
    exactness with TwoSum).
- **Concepts.** `CoordType` requires all of the above. `ExactCoord` additionally
  requires `is_exact`.
- **`Point<C>`** is `math::Vec<C, 3>`. litestl's `Vec` zero-fills with `T(0)`, so strong
  scalars work.
- **`TileFrame`:**
  - fields: `origin` (`double3`), `quantum_log2` (the quantum is always a power of two)
    and `frame_id`;
  - `to_lattice` rounds to the nearest quantum, and fails outside ±2²⁶ quanta or on
    non-finite input;
  - `to_local` is exact.

**Difference from design 02.** Design 02 has `Wide` in the traits. It is dropped:
litestl's predicates choose their own accumulator widths.

## 3. Derived predicates (`geom/predicates.h`)

All of these are exact, written once over `CoordType`, and take point spans (never
`topo` types).

| Function | Meaning |
|---|---|
| `drop_axis`, `orient2d_proj` | Projection dropping axis `k`, keeping `((k+1)%3, (k+2)%3)`. The sign equals the sign of the k-th component of `(b−a)×(c−a)` |
| `vol3(a,b,c,d)` | `sign det[b−a, c−a, d−a]`, i.e. `−orient3d`. `vol3_ext` is the same for constructed points |
| `PolyView<C>` | Loops concatenated in `pts`, with offsets `off` |
| `project_axis` | Dominant axis of the approximate Newell normal, or −1 if it is zero |
| `loop_orientation_proj`, `outer_loop_proj`, `face_front_sign` | Exact orientation at the lexicographic-minimum vertex |
| `point_in_poly_proj` | `Outside` / `Inside` / `Boundary` (even-odd) |
| `segments_intersect_proj` | `None` / `Proper` / `Touch` / `Overlap` |
| `HalfPlane`, `radial_class`, `radial_compare` | Exact angular order about an edge axis |
| `interior_point4` | `4·(a+b+2c)/4`, which is exact (fails for `double` off a dyadic grid) |
| `ray_winding(p, tris, scale_log2)` | Signed crossings (see below) |
| `incircle_proj`, `incircle_sos_proj` | Projected `incircle`; the SoS variant breaks cocircular ties by rank (litestl `pred::incircle_sos`) |
| `project_axis_canonical` | `project_axis` with the Newell sum in canonical loop order (independent of loop heads and loop order) |

**`ray_winding`:**
- **Method.** It counts signed crossings along a segment from `p` to a far point
  `p + D·2^L`. `D` comes from a fixed table of 8 generic integer directions.
- **Degeneracies.** Any degenerate contact moves on to the next direction, and the
  number of retries is reported.
- **Boundary.** If `p` lies on a triangle the result is `Boundary`.

### 3.1 Canonical CDT (`geom/cdt/`, amendment A1)

`cdt_proj(PolyView, axis, out)` returns `Ok`, `NotProjectable` or `HoleOrientation`.
On success it outputs the exact constrained Delaunay triangulation of the face in the
projection along `axis`.

- **Uniqueness.** Cocircular ties are broken by Simulation of Simplicity with
  lexicographic ranks. The result depends only on the projected points and the loop
  edges, never on insertion order, loop heads or loop order, and it is the same in
  `double` and `Lattice`.
- **Algorithm:**
  - lexicographic incremental Bowyer–Watson with ghost triangles and a 1-D collinear
    start;
  - constraint insertion by Anglada cavity retriangulation, using the same perturbed
    `incircle`;
  - an orientation-rule inside test, which is also the projectability check
    (crossing, touching, nested or outside loops, duplicate projected points).
- **Output:** stored corner indices, in front winding, canonically ordered.
- **Test hook.** `cdt::detail::cdt_proj_ordered` takes vertex and constraint
  insertion orders.

## 4. Identity (`ident/`)

- **`PersistentId`** is `Hash128`.
- **Domain tags** (`ident::tag`): `GeneratorEmit` (256), `BuilderEdge`, `BuilderLoop`,
  `Region`, `OpDerived`, `TestOnly`, `FaceSide` (262).
- **Derivations:**
  - `derive(tag, fields…)`, where the fields are typed (integers by width and
    signedness, strings, IDs, canonical doubles);
  - `derive_generator`;
  - `edge_id` (unordered);
  - `loop_id`, `face_side_id`;
  - `unbounded_region_id`, `region_id(hint, min side)`, `region_id_from_sides`.
- **Goldens** are pinned in `test_ident.cc`. They are part of the determinism contract.
- **`ElemKind`** is the design's `Domain` enum, renamed so it doesn't clash with
  `topo::Domain`.
- **References:**
  - `Ref<K>`, with an explicit constructor and no cross-kind conversion (compile-fail
    tests);
  - `Handle<K>{index, gen}`;
  - `ContainerRef`, with a 2-bit kind plus a 30-bit index. Zero means none, and
    construction is range-checked.

## 5. Domains and schemas (`topo/schema.h`, `topo/domain.h`)

**Schemas.** Every domain is a compile-time `attr::Schema`. The column names follow
design 01 §2, plus:
- **Shared bookkeeping**, with the same name in every domain:
  - `.id` (`h128`), on Vertex, Edge, Loop, Face and Region;
  - `.gen` (u32);
  - `.live` (bool).
- **`.v.container_hint` and `.e.container_hint`** (`h128`): region hints from the
  builder, which `build_regions()` resolves (P2b).
- **`.fs.hint`** (`h128`): the generator region hint of each face side.
- **`GlobalSchema`** (a group of size 1): `.g.allows_projectable` records the A1
  opt-in.
- **Projected faces** carry `face_flags::ProjAxisMask` (`(axis + 1) << 4`). Use
  `proj_axis(flags)` and `with_proj_axis`.

**Flags.** Every link column is flagged **`Topo`**. Design 01's "D" marker means
*recomputable*, not litestl's `Derived` flag: litestl excludes `Derived` columns from
hashes and bytes, so that flag is reserved for freeze caches.

**`Domain<S>`:**
- `alloc` (LIFO reuse);
- `alloc_n` (dense append);
- `free` resets every column **except `.gen`**, then bumps `.gen`;
- `live`, `for_each_live`, `handle`, `resolve`;
- `rebuild_allocator`;
- copying a domain is a snapshot.

**`FaceDomain`** owns the face-side rows (`2f + side`), so faces and sides are only ever
allocated, freed or permuted together.

## 6. Complex (`topo/complex.h`)

- **`ComplexBase`** (non-template) holds the edge, coedge, loop, face, shell and region
  domains, plus `global`, `frame` and `regions_dirty`.
- **Navigation:**
  - `edge_of`, `start_vert`, `end_vert`, `next`/`prev`, `radial_next`/`prev`;
  - `sense`, `side_toward_next`;
  - `face_of`, `first_loop`/`next_loop`, `region`/`shell(f, side)`.
- **Guarded walks.** `for_each_loop`, `for_each_coedge`, `for_each_radial` and
  `for_each_disk_edge` return `false` on a cycle that doesn't close, instead of
  looping.
- **`Complex<C>`** adds the vertex domain (`pos` is the only `Coord`-typed column) and
  the accessors `pos`, `vert_id`, `vert_edge`, `vert_container` and
  `for_each_edge_at`.
- **Versions.** `version()` is the maximum group version, and changes after any
  mutation. `frozen()` and `caches_valid()` compare stored stamps against it.
- **`FrozenCaches`** (built by `freeze` and `read`; not serialised):
  - vertex → edges;
  - shell → face sides;
  - face and region inclusions;
  - `T(f)` of projected faces.

## 7. Radial order (`topo/radial.h`)

- **`FaceFrame{axis, sigma}`**, computed per face.
- **`coedge_half_plane`.** `q` is the first vertex of the coedge's loop after the edge
  that isn't on the edge line. `s` is +1 if `q` is on the interior side.
- **`radial_order(k, e, coedges, half_plane_of, out, tie)`:**
  - takes a per-coedge half-plane provider: the planar rule, or for projected faces the
    third vertex of the coedge's `T(f)` triangle with `s = +1`
    (`projected_third_vertices`, `face_triangles`);
  - sorts counter-clockwise about `vs[0]→vs[1]`, starting at the coedge whose face has
    the smallest ID;
  - exactly equal angles are tie-broken by face ID and reported.
- **Verification.** The wedge rule and this order are checked against each other by
  invariant W on real models (`test_topo_validate`).

## 8. Builder (`topo/builder.h`)

**API.** `Builder<C>::vertex(id, p, ContainerHint)`, `face(id, loops, front_hint,
back_hint, flags)`, `wire(id, a, b, ContainerHint)`, then
`build(out, report, policy)`.

**Output** is a function of the emitted *set* only:
- **Welding.** Vertices weld by ID. `weld_equal_positions` also welds bitwise-equal
  positions, keeping the smaller ID.
- **Ordering.** Every domain is dense and in sorted-key order:
  - vertices by ID;
  - edges by vertex pair, with `vs[0]` the smaller ID;
  - faces by ID;
  - loops by (face, ordinal);
  - coedges by (face, loop, position).
- **Cycles.** Disk cycles are in edge-index order.

**Errors and warnings.** The build is all-or-nothing on errors; the error codes are
listed in `BuildCode`. `RadialTie` is the only warning.

**Projectable faces** (amendment A1; `BuildPolicy::allow_projectable_faces`, off by
default):
- **Acceptance.** A non-planar face is accepted if `cdt_proj` succeeds. The dominant
  canonical Newell axis is tried first, then the other two axes in index order.
- **Output.** The builder sets `ProjAxis` on the face and `.g.allows_projectable` on
  the complex, and counts the face in `BuildReport::projected_faces`.
- **Codes:** `NotProjectable`, `ReservedFaceFlags` (the client set the bits) and
  `WireOnProjectedFace`.

**Containers.** `ContainerHint`:
- face hints are resolved immediately;
- region hints are stored in `*.container_hint` until regions exist.

**Determinism.** Parallel phases use fixed chunks and write into buffers. Columns are
then filled with page-parallel `parallel_write`. The output is identical at 1, 2, 4
and 12 bands.

## 9. Validator (`topo/validate.h`, `validate_geom.cc`)

**API.** `validate(complex, level)` returns a `ValidationReport`: issues of
`{ValidateCode, ElemKind, index}`, capped at 256, with a running total. A layer runs only
if the previous ones are clean.

| Level | Checks |
|---|---|
| `Combinatorial` | Dead references; edge endpoints; coedge links, chain and start vertex; loops; loopless faces; a face using an edge twice; radial and disk cycles; containers; duplicate IDs; **Euler–Poincaré per vertex** (`VertexLink`); when regions are current, invariant W, shell partition, shell/region consistency and the unbounded region |
| `Embedding` | Exact planarity of unflagged faces; projected-face flag, axis, opt-in and projectability; hole winding; radial order re-derived from geometry; container geometry: region winding over the region's shells, or on the face or on `T(f)` |
| `Global` | Loop simplicity and holes inside; exact face–face contact over grid-binned bounding-box pairs (contact allowed only at shared vertices and along shared edges); `build_regions()` on a copy matches the stored regions |

**Vertex link (Euler–Poincaré).** At each vertex, the radial orders define a rotation
system on the link graph:
- **Nodes** are the face edges at the vertex.
- **Arcs** are the face corners there.
- **The check.** The number of sectors (orbits of the wedge relation on corner sides)
  must equal *corners − nodes + 2 × link components*, the count for a planar embedding
  of each component.
- **What it catches.** It needs no ground truth, and it catches radial orders that no
  planar link admits, such as a reversed three-face cycle.

## 10. Regions (`topo/regions.h`)

`build_regions(k, report)` is all-or-nothing. On success it writes `.f.region`,
`.f.shell`, the face-side chains, the shells and regions, and the resolved containers,
and it clears `regions_dirty`. The engine is `RegionBuilder` in `topo/regions/`. Face,
side and container columns are compared first, so a rebuild that changes nothing leaves
their pages and versions alone.

1. **Shells.** Union-find over wedges. A sheet boundary (a radial cycle of one coedge)
   joins both sides of its face with no special case. Shells are indexed by their
   minimal face-side ID.
2. **Shell kind and volume.**
   - **Sheet shells** hold both sides of every face they touch.
   - **Other shells** get an exact signed volume (`Int128`) in `Lattice`, or a filtered
     sum with an error bound in `double`. Normals point away from the side's region.
     `V > 0` is an outer shell, `V < 0` a cavity shell, and an uncertified value is a
     `DegenerateShell` error.
3. **Nesting.**
   - Cavity and sheet shells join the region of the innermost outer shell that
     contains them, the one with the smallest volume.
   - Sharing a face on opposite sides means "not inside".
   - Otherwise a test point is checked with an exact ray winding: a vertex that isn't
     on the candidate shell, or an `interior_point4` of a face the candidate doesn't
     share.
4. **IDs.**
   - Region 0 is `unbounded_region_id()`.
   - A hinted region gets `region_id(hint, min side ID)`; an unhinted one gets
     `region_id_from_sides`.
   - Warnings: `HintConflict` (the majority hint wins) and `DuplicateHint`.
5. **Other output:**
   - non-`Topo` region columns are carried over by persistent ID (CopyFirst);
   - region container hints are resolved;
   - errors: `UnresolvedContainerHint`, `AmbiguousContainerHint`, `TouchingShell`,
     `NestingDegenerate`, `NoTestPoint`.

**Precondition.** The complex passes the combinatorial validator.

## 11. Freeze, thaw, serialisation, hashes

**`freeze(k)`** returns `Ok`, `AlreadyFrozen` (a no-op) or `RegionsDirty`.

- **Order:**
  - vertices, edges and faces are renumbered by the Morton code of their position,
    edge midpoint or corner centroid, in lattice quanta quantised to 21 bits per axis
    over the tile frame's fixed extent, ties by persistent ID;
  - loops follow faces, coedges are contiguous per loop, and face sides follow faces;
  - shells and regions keep `build_regions()`'s order.
- **Remap.** Every index column is remapped through `topo/remap.h`. Each schema column
  has a `RemapRule`, and `schema_covered<S>` is checked by `static_assert`
  (compile-fail test).
- **Generations.** An element that moves gets `max(own gen, slot's old gen) + 1`;
  unmoved elements keep their generation and handles.
- **Caches** are rebuilt.

**`thaw(k)`** returns a copy without caches.

**`write` / `read`:**
- **Stream:** magic `UTCX`, format 1, coordinate key, tile frame, the `regions_dirty`
  and frozen flags, then the groups (vertices, edges, coedges, loops, faces, face
  sides, shells, regions, global), then the trailing `content_hash`.
- **`read`** checks layouts and the hash, rebuilds the allocators and caches, and
  replaces the target only on success.

**Hashes:**
- `content_hash` covers everything serialised.
- `topology_hash` leaves out vertex positions, the coordinate key and the frame's
  geometry. It is equal across `double` and `Lattice` for the same model, and is
  pinned in `test_topo_determinism`.

## 12. Test generators (`tests/gen/buildings.h`)

**Families:**
- room grids, with thin or thick walls and glazed or open windows;
- courtyard;
- pillar room;
- sealed cavity;
- room > pillar > void;
- fence;
- sensor and wire;
- sloped, projected roof;
- random city.

**Ground truth.** Every face side carries its ground-truth region label as its hint,
and the tests require the computed partition to equal the labelling exactly.

**Construction:**
- vertices are keyed by grid position, and shared walls are emitted once;
- coordinates are integers: `Lattice` uses them directly, and `double` uses them times
  2^-8, with a tile frame quantum of 2^-8.

## 13. Pool owner contract (for P4)

P1b (DEC-015) delivered litestl's value pools. When P4 adds a `GeometryStore` to
`Complex`, the complex must follow this contract, which `test_smoke_pools` exercises:

**Holding the store and refs:**
- **Store by value.** `Complex` holds a `PoolStore` by value. Kinds are registered in
  the constructor in a fixed order, and new kinds are appended. Copy and move stay
  defaulted, because nothing points into the store.
- **Ref columns** are `AttrDef<..., attr::Column<attr::PoolRef>>` schema entries, for
  example `.e.curve`, `.c.pcurve` and `.f.surface`. A custom column holding refs must
  override the six `ColumnBase` ref hooks.

**Roots and collection:**
- **Roots.** All domain groups go in schema order, plus the journal and any weak cache
  maps (`RefRoot`, with `prune`). `Roots::accept` enforces kinds per column, for
  example curve kinds only on `.e.curve`.
- **Collect points** must be deterministic: the end of a primitive or a batch, a
  threshold on `taken_since_collect()`, and before freeze, canonicalisation or write.
  Never hold a `PoolRef` outside a root across a collect.
- **Approximation caches** live in Derived columns, in entries that reference their
  source through an inner ref, or in a pruned weak map. An external memo keyed by
  `PoolRef` must check `PoolStore::version()`.

**Freeze and I/O:**
- **Freeze** canonicalises the store after the permutes, as design 04 §8.4 requires.
- **Write and hashes.** `write` canonicalises a copy and then writes the store before
  the groups. `content_hash` includes the canonical store hash, because a group hash
  alone hashes slot numbers.
- **Read** is strict: a fresh complex, then `store.read`, the group reads,
  `audit(require_reachable)`, and a `canonicalize` that must report no change; then
  move-assign. Once pools join the complex, its stream gets a new format version.

**Other owners and projected faces:**
- **Merging or importing from another complex** goes through `import` and
  `import_rows`, never through raw ref copies.
- **Projected faces** (DEC-014) whose triangulation becomes a pool entry follow the same
  rules. Their edit rules are P3's.

## 14. Editing (`topo/edit/`, P3a)

**The editor.** `Editor<C>(complex, journal = nullptr, rules = nullptr, options)` is
the only writer of a complex during editing.
- **Transactions:**
  - `begin(label)` / `commit()` / `rollback()` group primitives into one undo unit.
    Nested `begin` is a savepoint.
  - Every primitive is its own savepoint, so a failure rolls back exactly that
    primitive.
  - A failed or rolled-back edit leaves a **bit-identical content hash**.
- **Results.** Primitives return an `EditCode`. `report()` gives the element
  concerned, a detail string and, for `Invalid`, the validator's report.
- **Validation modes** (`EditorOptions::validate`):
  - `Full`: `validate(k, Global)` after every primitive;
  - `Local`: `validate_local` over the touched elements and their one-ring (P3b, §15);
  - `OnCommit` (P3b): `validate_local` once, at the outermost commit, over everything
    the transaction touched;
  - `None`.
- **Frozen complexes are refused.** Columns may not be added or removed inside a
  transaction.

**Primitives:**

| Primitive | Notes |
|---|---|
| `make_vertex(p, id)` / `kill_vertex(v)` | Kill needs an isolated vertex |
| `make_edge(a, b, id = edge_id)` / `kill_edge(e)` | A wire. Runs from the smaller vertex ID; kill needs a wire |
| `set_position(v, p)` | Planar faces must stay planar. Projected faces that become planar drop their axis. Every edge of every face at `v` is re-sorted; regions go dirty |
| `embed_vertex` / `embed_edge(x, EmbedTarget)` | Face, region (writes the region's hint; needs current regions) or none |
| `split_edge(e, p)` / `join_edge(v)` | `p` strictly inside `e`, exactly. Halves get `derive(OpDerived, e.id, "split_edge", r)`; the vertex gets `"split_edge.v"`. The join survivor is the smaller ID |
| `make_face(loops, id, flags, hints)` / `kill_face(f)` | Over existing edges, with no embedded vertices or wires. Classified like the builder (`face_geom.h`). Coedges are inserted by re-sorting radial cycles; a tie fails. Kill refuses faces with inclusions or the last carrier of a used hint |
| `split_face(f, a, b)` / `join_faces(e)` | A chord between non-adjacent vertices of the outer loop. The child holding the chord coedge `a → b` keeps `f`'s slot. Children get `derive(OpDerived, f.id, "split_face", 0 or 1)`. Holes and inclusions follow by exact point tests. A join needs two coplanar faces sharing only `e`, with consistent fronts and equal flags and hints |
| `flip_face(f)` | Reverses loops, swaps side rows and hints, toggles `Reversed`; regions go dirty |
| `set_side_hint(f, side, h)` | Regions go dirty; refuses removing the last carrier of a used hint |
| `set_attr(group, row, column, bytes)` | Journaled write of a non-`Topo` column (not `pos`) |
| `rebuild_regions()` | A barrier: `build_regions`, then validation; it snapshots the complex for undo. `Full` mode validates to Embedding. `Local` and `OnCommit` run `validate_regions` (the combinatorial checks that read regions: containers, region IDs, shells, regions, invariant W) and `ContainerGeometry` on region-contained elements, the one geometric check that reads regions (plan [2026-11-22](../../plans/2026-11-22-region-barrier-cost.md)). Earlier commits validated the topology and geometry the barrier leaves unchanged. An editor never validates the complex it is given: validate a built or loaded complex before editing it in `Local` or `OnCommit` |

**Regions are derived.** Only `rebuild_regions` writes region and shell columns.
- Face edits (make, kill, split, join, flip, hints) set `regions_dirty`.
- Wire, vertex and planar `split_edge` / `join_edge` edits keep regions.

**Journal.** A `Journal<C>` holds the applied and undone transactions.
- **Undo** is physical: the row captures, then allocator events in reverse, then group
  sizes.
- **Redo** re-executes the recorded ops.
- **Lineage** is queried with `forward(id)` / `backward(id)`.
- **Truncation.** A write outside the editor truncates the journal at the next
  `begin`.
- **Pool roots.** `JournalRoots<C>` registers captured bytes, barrier snapshots and
  `SetAttr` payloads as a `PoolStore` root.

**Propagation.** Per-event policies come from `propagate.h`; overrides come from
`PropagationRules` (`conserved(group, name)` = split by measure, sum on merge).
- `pos` is never mixed.
- Temp and Derived columns reset.
- Corners (coedge columns) follow loop changes.

**Allocation.** `Domain::alloc` takes the **lowest free slot** (a min-heap). The
allocation sequence therefore depends only on `.live`, both in memory and after
`read` / `permute`.

**Not in P3a:**
- `add_hole`, `imprint`, `merge_vertices` (§15), `connect_loops` (§15.1);
- compaction and the generation floor;
- incremental regions;
- wire–face crossing checks (a gap the P2 validator has too).

## 15. Editing additions (P3b)

Plan [2026-11-18-p3b-editing.md](../../plans/2026-11-18-p3b-editing.md); DEC-017.

**Generation floor** (`topo/domain.h`):
- Each domain keeps `gen_floor()`. `permute` raises it over the indices it drops, and
  appended slots start at it, so a handle into a dropped slot never resolves again.
- `replace_generations(old, same)` (region rebuilds) keeps the generations of rows
  that are unchanged: shells with the same first side, regions with the same ID.
- UTCX format 2 serialises the seven floors after the header flags. Format 1 still
  reads, with floors 0. Floors enter `content_hash` only when one is non-zero.
- Journaled history is not covered: rollback and undo restore generations exactly.

**Barriers.** `compact()` (`topo/compact.h`) and `rebuild_regions()` are snapshot
transactions of their own. Both are refused inside a transaction (`OpenTransaction`)
and both are redone by running them again.

**Regions** (`topo/regions.cc`):
- **Region IDs.** A hinted region whose winning hint no other region wins gets
  `region_id_from_hint(h)`. Regions that share a winner keep `region_id(h, min side)`.
- **Container hints** resolve in this order:
  1. the unique region that wins the hint;
  2. if several regions win it: `AmbiguousContainerHint`;
  3. if none does: the unique minority carrier other than region 0, else ambiguous or
     unresolved.

**Primitives:**
- `imprint(f, chain)`: the chain is a path of wires embedded in `f`, given in order.
  - An open path between two outer-loop vertices splits `f`, with the same IDs and
    slot rule as `split_face`, which is now its one-edge case.
  - A closed cycle becomes a hole of `f`, filled by a new face
    `derive(f, "imprint.island", min chain edge ID)`.
  - Inclusions inside the cycle move to the new face.
- `unimprint(e)` walks the maximal two-face chain through `e` and merges the two faces.
  The chain becomes wires embedded in the survivor (the smaller face ID).
- `add_hole(f, cycle)` and `remove_hole(f, loop)`. A removed hole's edges and vertices
  become inclusions of `f`.
- `weld(pairs)`:
  - each pair of coincident vertices merges into the smaller ID;
  - edges that become parallel fuse into the smaller edge ID, with coedge senses
    recomputed and the radial cycle re-sorted;
  - a merged vertex's container is none if it is a face vertex, else the face
    container, else the region container (conflicts are refused).
- `make_edge(a, b, id, &e, in)` can create the wire already embedded. A wire lying in a
  face must be made that way, or the validator reports `WireCrossesFace`.
- `connect_loops(f, path)` and `disconnect_loops(e)`: §15.1.
- New codes: `ChainNotSimple`, `NotEmbedded`, `HoleInside` (OI-P3b-1: cycles around
  existing holes are refused), `EdgeShared`, `Coincident`.

**Derived editor state** (`edit/editor_index.h`, `edit/editor_grid.h`,
`edit/editor_region_hints.h`):
- **What is kept:** the ID, hint and face-inclusion index, a face-box grid
  (`topo/face_grid.h`), and a cache of regions' winning hints. Nothing but that cache
  depends on region indices.
- **How:** the index and the grid are maintained by dirty rows. `touch`, `alloc` and `restore` mark
  rows; a flush re-reads them from the current state.
- **Barriers and outside writes:**
  - `compact` and its undo rebuild both eagerly, since they renumber rows.
  - `rebuild_regions` and its undo re-key only hinted elements whose container was or
    becomes a face (`EditIndex::refresh_containers`), and leave the grid, which holds
    geometry only. A failed `rebuild_regions` rolls back before touching either.
  - A write outside the editor invalidates both, and they are rebuilt on next use.
- **Winning hints** are rebuilt, in O(F), by the first embed into a region after
  `.fs.hint` or `.f.region` changed (the cache remembers their version stamps).
- **Grid cells** are the smallest power of two at least the median face-box extent.
- **Large faces** spanning more than 64 cells are entered by their T(f) triangles.
- **Wires** are entered along their segment.

**Filtered validation:**
- **Combinatorial** (`validate_local.cc`): the listed rows and their one-ring. The
  scope is listed in the file header. IDs go through the index (`IdOracle`).
- **Embedding:** iterates the filter lists. Region containment costs the region's
  boundary, except region 0, which uses one ray through the grid.
- **Global** (`validate_grid.cc`): contact tests against grid neighbours only, with
  sparse per-call storage.
- **The oracle:** `validate(k, Global)` stays unfiltered.

**Redo** never validates. `EditorOptions::verify_redo` (on by default in debug builds)
compares the content hash stored at commit and fails with `JournalStale` on a
mismatch.

### 15.1 Cuts through holes

Plan [2026-11-24-connect-loops.md](../../plans/2026-11-24-connect-loops.md); DEC-021.

- **`connect_loops(f, path)`** splits `f` along chords that run through its holes.
  - **The path:** a vertex of the outer loop, then two vertices of each hole the cut meets
    (where it enters and where it leaves), then another vertex of the outer loop.
  - **The result:** one chord per gap. Each cut hole's loop is shared out between the two
    children's outer loops, and its row is freed. Other holes and inclusions go to the
    child that contains them.
  - **IDs and slots:** `derive(f, "connect_loops", 0 | 1)`. The child with the chord
    coedges in the path's direction keeps `f`'s slot.
  - **Refusals:** `BadArgument`, `SurfacedFace`, `NotInLoop`, `NotOuterLoop` (an end on a
    hole, or a middle on the outer loop), `ChainNotSimple` (a hole entered and left at one
    vertex, or on two holes, or cut twice), `EdgeExists`, `HoleOnChord`,
    `InclusionOnChord`, `DegenerateLoop`. Crossing chords are `Invalid`.
- **`disconnect_loops(e)`** joins two faces that share m >= 2 separate edges, from any of
  those edges, and removes them.
  - The two loops become m: the new outer loop first, then the holes.
  - It also joins a strip across a hole to the face around it.
  - Refused with `NotJoinable`: shared edges that meet (`unimprint`'s case), one shared
    edge (`join_faces`' case), a third face on a shared edge, a shared vertex elsewhere,
    or a union that isn't planar.
  - Not an exact inverse: IDs differ. Undo is exact.
- **Both are in `edit/editor_impl_connect.h`.** The cut itself is
  `split_core` (`edit/editor_impl_split.h`), shared with `split_face` and `imprint`.

## 16. Curves and the geometry store (P4a)

Plan [2026-11-18-p4a-curves.md](../../plans/2026-11-18-p4a-curves.md); DEC-018.

**The store** (`topo/complex.h`, `topo/geom_store.h`):
- `ComplexBase::geom` is an `attr::PoolStore` with the curve kinds registered in plan
  order (`curves::register_pools`).
- `GeomRoots` / `GeomMutRoots` give the store its roots: the domain groups in stream
  order, plus extra roots such as `JournalRoots` and `ApproxCache`.
- The accept rule (`accept_geom_ref`):
  - `.e.curve` takes `alignment3`, `catenary3`, `polyline3` and 3D `bspline`;
  - `.c.pcurve` and `.f.surface` take nothing yet;
  - an `offset2` names a `cesaro2`, and an `alignment3` names a `cesaro2` or an
    `offset2`.
- `frozen()` also requires the store's version to be the one freeze left. Interning a
  new entry thaws.

**Columns:**
- `.e.curve` (`PoolRef`);
- `.e.range` (`double2`, (0, 0) for a straight edge);
- `.e.geom_flags` (must be 0 in P4a);
- `.g.curve_tol` (default 2⁻³⁰ frame units).

All four are Topo.

**Hashes and I/O** (`topo/io.cc`, `topo/io_legacy.cc`):
- Both hashes map refs through `PoolStore::canonical_map`. `content_hash` adds the
  store's reachable entries in canonical order.
- **UTCX format 3:** header, floors, canonical store, groups, trailing hash. `write`
  works on a canonicalised copy.
- **`read`:** audit with `require_reachable`, then canonicalize (which must report no
  change), then the hash check.
- **Formats 1 and 2** read through the legacy schemas. Their geometry refs must all be
  NONE, and `AttrGroup::migrate_from` moves the rest into a fresh complex.
- `freeze` audits the store first (`FreezeResult::GeometryInvalid`) and canonicalises it
  after the permutes.

**Carriers** (`topo/curve_geom.h`):
- **`edge_carrier(store, ref, t0, t1)`** checks:
  - the kind;
  - the range: inside the domain, distinct ends, no offset fold.
- **`on_carrier`** applies the endpoint rule: |c(t) − p| ≤ eval_error +
  √3·rounding_C(p) + curve_tol, with an allowance for the distance computation.
- **`round_point`** is round_C, the rounding from frame-local doubles to a stored point.

**Editor** (`edit/editor_impl_curves.h`):
- **`intern_curve(kind, def, &ref)`** validates the definition through the kind's
  write/read round trip. Its inner refs must be live and accepted, and its evaluator
  must build. It is not journaled.
- **`set_edge_curve(e, ref, t0, t1)`:** NONE clears the carrier, with range (0, 0). The
  endpoint rule is checked by validation.
- **`split_edge_at(e, t)`:**
  - the new vertex goes at round_C(c(t)), and the ranges split at t;
  - incident planar faces must stay planar;
  - regions become dirty.

  `split_edge` refuses curved edges (`CurvedEdge`).
- **`join_edge`** joins curved edges on one carrier whose ranges meet bit for bit at the
  vertex and run on. Their propagation weights are parameter lengths.
- **`weld`** fuses only straight edges, or the same carrier between the same vertices.
- **`collect_geometry(extra)`** is refused inside a transaction. Barriers collect after
  they commit. Barrier undo keeps the live store.

**Validation** (`validate_curves.cc`, at the start of the embedding layer):
- `CurveKind`;
- `CurveRange`;
- `CurveEndpoint`;
- `GeometryUnsupported` (pcurves, surfaces, non-zero `.e.geom_flags`).

## 17. Surfaces, pcurves and the refined skeleton (P4b)

Plan [2026-11-18-p4b-surfaces.md](../../plans/2026-11-18-p4b-surfaces.md); DEC-019.

**Kinds** (`curves/`):
- **`road3`** (`road.h`): a ruled road surface over a `cesaro2` reference. S(s, t) =
  r(s) + t·N(s) + Z(s, t)·e_z, with t a horizontal offset and Z linear in t between
  border heights. Each link (the strip between two borders) is one piece.
- **`graph2`** (t = f(s)) and **`segment2`** (`pcurve.h`): pcurves in a surface's
  (s, t) space. Their pieces are `Fn1D`s (`fn1d.h`): polynomials in σ = s − break.
- **`SurfaceEval`** (`surface.h`): `eval(u, v, n, out)` gives S and its partials to
  order 2, with one-sided limits at breaks. `eval_piece` evaluates on a given link.
  It also reports `eval_error`, `deriv_eval_error(k)` and box derivative bounds.
- **`build_road`** (`road_build.h`), a generator helper and not a kind: from a
  reference, a centre profile and a cross-section of links, it interns the `road3`
  and each border's `offset2`, `alignment3` carrier and `graph2` pcurve.

**Accept rules and columns:**
- `.f.surface` takes `road3`. The face flag `Reversed` gives its orientation.
- `.c.pcurve` takes `graph2` on a curved edge's coedge and `segment2` on a straight
  edge's, only where the coedge's face has a surface. Every coedge of a surfaced face
  has one.
- `road3` names a `cesaro2`. Nothing names a `road3`, `graph2` or `segment2`.
- **Same parameter:** a curved edge's pcurve takes the edge's carrier parameter over
  `.e.range`; a straight edge's takes λ ∈ [0, 1] from `vs[0]` to `vs[1]`.
- `.g.skeleton_tol`: Topo, `double`, default 2⁻⁶ frame units. It is left out of the
  topology hash (it is in frame units).

**The refined skeleton** (`topo/face_skeleton.h`):
- **`Skeleton<C>`** holds each curved edge's flattening at `skeleton_tol`, rounded to
  `C`. Its points are numbered from the vertex count, in one table per validation pass
  or editor operation. `warm()` fills it up front for parallel readers.
- **`FacePoints::gather(k, f, sk)`** expands a face's loops to skeleton points (`ids`,
  with the `owner` coedge of each). `gather(k, f)` stays vertices-only.
- **Readers:** projection and T(f), radial order, contacts (`skel_point_on_face`,
  `skel_segment_on_face`), wire–face tests, containers, regions, face boxes and the
  editor's grid.
- **`face_planar_rule`** (`face_geom.h`): the stored vertices decide planarity, unless
  they are degenerate (a loop of arcs), when the skeleton decides.
- With no curved edges, everything reads exactly the stored vertices.

**Editor** (`edit/editor_impl_curves.h`):
- **`intern_geom(kind, def, &ref)`** takes every kind (`intern_curve` is its P4a
  name).
- **`set_face_surface(f, ref, reversed, pcurves)`** sets a face's surface, its
  `Reversed` flag and every coedge's pcurve in one journaled step, so every validation
  mode sees a whole surfaced face. NONE clears all three.
- **`set_coedge_pcurve(c, ref)`** is journaled.
- `split_edge_at` and curved `join_edge` keep pcurve refs.
- **`SurfacedFace`** refuses the edits that would need pcurve rules on a surfaced face:
  `split_edge` and `join_edge` on straight edges, `split_face`, `join_faces`,
  `imprint`, `unimprint`, `add_hole`, `remove_hole` and `weld` (OI-P4b-2).

**Validation** (`validate_surfaces.cc`, after the curve checks), per surfaced face:
- `SurfaceKind`, `PcurveKind`, `PcurveRange`: refs, kinds and domains;
- `SurfaceLink`: one link holds every pcurve;
- `PcurveVertex`: S(pcurve) at each coedge end against its vertex;
- `PcurveDeviation`: the edge against S(pcurve) along its range, structurally for road
  borders and by a second-order bound otherwise;
- `SurfaceOrientation`: `Reversed` against the surface normal.

`SkeletonDegenerate` reports a refined skeleton that touches its own loop or another,
and a curved edge that doesn't flatten at `skeleton_tol`. `GeometryUnsupported` now
means only a non-zero `.e.geom_flags`.

**Format and I/O:** UTCX format 4 (`kComplexFormat`). The new kinds are appended to
the store. Format 3 reads through a migration that checks its stored hash over the
first `kFormat3Pools` (6) pools.

**The acceptance generator** (`tests/gen/road_terrain.h`): a 2 km terrain sheet of
40 × 40 projected quads, and a 1.2 km road placed into it in one transaction. The
transaction kills the cells under the road, adds road faces with their surface and
pcurves, and makes a fill face from the hole's boundary to the road's outline. It
builds in `double` and `Lattice`, whose definitions differ only by an exact power-of-two
scale.
