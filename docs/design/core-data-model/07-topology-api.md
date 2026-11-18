# 07 — Topology API (as implemented)

- **Status:** P2 complete 2026-11-18 (plan [P2](../../plans/2026-11-18-p2-cell-complex.md):
  P2a, then P2b with amendment A1).
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
and it clears `regions_dirty`.

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
