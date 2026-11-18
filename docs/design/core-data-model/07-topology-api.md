# 07 — Topology API (as implemented)

- **Status:** P2a landed 2026-11-18 (plan
  [P2](../../plans/2026-11-18-p2-cell-complex.md)). The P2b sections (regions, embedding
  and global validation, freeze, serialisation) are added as they land.
- **Scope:** the realised API of `geom/`, `ident/` and `topo/`. The design rationale
  stays in [01](01-cell-complex.md), [02](02-coordinates-and-predicates.md) and
  [04](04-identity-versions-journal.md); this file records names, contracts and the
  places where the implementation differs from the design.

## 1. Modules and targets

| Directory | CMake target | Kind | Contents |
|---|---|---|---|
| `source/ubertopo/geom/` | `ubertopo_geom` | header-only | `coord.h` (`Lattice`, `CoordTraits`, concepts, `TileFrame`), `predicates.h` (derived exact predicates) |
| `source/ubertopo/ident/` | `ubertopo_ident` | header-only | `id.h` (`PersistentId`, domain tags, derivations), `ref.h` (`Ref`, `Handle`, `ContainerRef`) |
| `source/ubertopo/topo/` | `ubertopo_topo` | static | `types.h`, `schema.h/.cc`, `domain.h`, `complex.h`, `radial.h`, `builder.h/.cc`, `validate.h/.cc` |

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
  - `orient2d`, `orient3d`, `incircle`, `compare`;
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

**`ray_winding`:**
- **Method.** It counts signed crossings along a segment from `p` to a far point
  `p + D·2^L`. `D` comes from a fixed table of 8 generic integer directions.
- **Degeneracies.** Any degenerate contact moves on to the next direction, and the
  number of retries is reported.
- **Boundary.** If `p` lies on a triangle the result is `Boundary`.

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

## 7. Radial order (`topo/radial.h`)

- **`FaceFrame{axis, sigma}`**, computed per face.
- **`coedge_half_plane`.** `q` is the first vertex of the coedge's loop after the edge
  that isn't on the edge line. `s` is +1 if `q` is on the interior side.
- **`radial_order`:**
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

**Containers.** `ContainerHint`:
- face hints are resolved immediately;
- region hints are stored in `*.container_hint` until regions exist.

**Determinism.** Parallel phases use fixed chunks and write into buffers. Columns are
then filled with page-parallel `parallel_write`. The output is identical at 1, 2, 4
and 12 bands.

## 9. Validator (`topo/validate.h`)

- **API.** `validate(complex)` returns a `ValidationReport`: issues of
  `{ValidateCode, ElemKind, index}`, capped at 256, with a running total.
- **Combinatorial layer:**
  - dead references;
  - edge endpoints;
  - coedge links, chain and start vertex;
  - loops: closure, size, ownership;
  - loopless faces, and a face using an edge twice;
  - radial and disk cycles: closure, inverse links, membership;
  - containers, and containers on face vertices;
  - duplicate IDs;
  - when regions are current: invariant W, shell partition, shell/region consistency,
    and the unbounded region.
- **P2b adds** the embedding and global layers, the Euler–Poincaré cross-check, and
  the freeze tests.
