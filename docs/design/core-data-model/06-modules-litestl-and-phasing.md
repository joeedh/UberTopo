# 06 — Modules, litestl Work and Phasing

Part of the [core data model design](README.md).

## 1. Source layout (proposed)

```
source/
  litestl/                 submodule (DEC-003); include as "litestl/..."
  ubertopo/
    geom/                  CoordTraits glue, derived predicates, TileFrame, GeomRef, pools
    curves/                curve and surface kinds, B-splines (DEC-003: not in litestl)
    attr/                  Domain, SchemaRegistry, Propagator, UberTopo column classes
    topo/                  Complex<C>, domains, primitives, builders, regions, validator
    ident/                 PersistentId derivation, ID lookup caches, keyed RNG streams
    io/                    Complex serialisation (versioned, canonical; lz4 via litestl)
    tests/                 test_<thing>.cc using litestl's harness
CMakeLists.txt             C++20; clang required (Sculptcore toolchain conventions)
```

**Conventions.**
- Source files over 40 KB are split into subdirectories (`CLAUDE.md`), except
  auto-generated files.
- Namespaces are `ubertopo::{geom,curves,attr,topo,ident,io}`.
- Follow litestl conventions: `.h`/`.cc`, its containers and allocator, `path`, and its
  test harness.
- Temporary comments use the `CLAUDENOTE:` prefix.

**Build.**
- The native build comes first.
- The N-API addon target arrives later, using Sculptcore's cmake-js and clang
  `/DELAYLOAD` setup (DEC-005/006).
- WASM is not a target (DEC-005).

## 2. Work that goes into litestl (DEC-003)

| Item | litestl location (proposed) | Needed by | Notes |
|---|---|---|---|
| `Int128` / `Int256` signed fixed-width integers | `math/wideint.h` | 02 Lattice predicates | Use `__int128` where available; portable fallback; constexpr-friendly |
| Adaptive `double` predicates (orient2d/3d, incircle, insphere) | `math/predicates.h` | 02 | Re-implementation of the published adaptive-expansion algorithms |
| Exact integer determinants | `math/predicates.h` | 02 | Templated on input width |
| `Hash128`/`Hasher128` (SipHash-2-4-128, DEC-009) | `util/hash128.h` | 03, 04, 05 | Golden-value tests |
| Counter-based RNG (Philox4x32-10, `litestl::rng`) | `util/rng.h` | 04 | Replaces the use of `util/rand.h`; leave the old one for Sculptcore compatibility |
| Deterministic exclusive scan; standard-library-independent deterministic sort | `util/task.h`, `util/sort.h` | P2 bulk builder (04 §8.2) | Deferred from P0 by its review |
| Deterministic `parallel_reduce` / ordered `parallel_for` chunks | `util/task.h` | 04 §8 | Chunking independent of worker count |
| Paged copy-on-write store, `ColumnBase`, `Column<T>`, `BoolColumn`, `AttrGroup`, `ColumnTypeRegistry` | `attr/` (new module) | 03 | **Done (P1)**; see litestl `documentation/attributes.md` |
| `InternPool<T>`, `PoolStore`, `Column<PoolRef>` (mark-and-sweep, canonical renumbering) | `attr/pool.h`, `attr/pool_store.h` | 05 | **Done (P1b)** |
| `UniquePtr` | `util/memory.h` | 03 | Missing: litestl has only `shared_ptr`/`weak_ptr` |
| io `Writer`/`Reader` interface for columns | `io/` | 03 | Build on `binfile` |

**Push access.** Granted (DEC-008). Changes are made in the submodule, pushed to a
litestl feature branch, and then the pointer is bumped here.

## 3. Phased delivery

Each phase becomes a plan in `docs/plans/` and is pressure-tested before work starts.
They replace the four spikes listed in the [research synthesis](../../research/README.md#5-recommended-next-steps).

| Phase | Scope | Exit criteria |
|---|---|---|
| **P0 — Foundations (litestl)** — *done 2026-11-17* | Wide ints, predicates, hash128, Philox RNG, deterministic reduce | Predicate test suite against an independent big-rational oracle; golden hashes; thread-count invariance of the reduce |
| **P1 — Attribute core (litestl `attr/`)** — *done 2026-11-18* | Paged copy-on-write store, column interface, registry, AttrGroup, BoolColumn (byte per element; InternPool moved to P1b) | Met: model-based suite, open-extension test, canonical round trips, page size 4096 (OI-3), 7-configuration matrix |
| **P1b — Value pools (litestl `attr/`)** — *done 2026-11-18* ([plan](../../plans/2026-11-18-p1b-value-pools.md), DEC-015) | InternPool / `Column<PoolRef>` / variable-size entries / store-level ownership; mark-and-sweep liveness instead of refcounts | Met: pool model test with diverging snapshots, audit with explicit roots, canonical bytes independent of history and page size, ≥ 10⁵-entry index benchmark |
| **P2 — Complex core (UberTopo `topo/`)** — *done 2026-11-18* (P2a + P2b, with opt-in projected faces and a canonical CDT; [07](07-topology-api.md)) | `Complex<C>` for `double` and `Lattice`; domains and builtin columns; bulk builder; radial sort; `build_regions()`; validator; freeze and thaw; persistent IDs | Generated buildings with shared walls, window holes, aperture faces and rooms produce the correct regions and pass all validation layers, bit-identically at 1 and N threads and in both instantiations |
| **P3 — Primitives, propagation and journal** — *P3a done 2026-11-18* ([plan](../../plans/2026-11-18-p3-editing.md), DEC-016; [07 §14](07-topology-api.md#14-editing-topoedit-p3a)); *P3b done 2026-11-18* ([plan](../../plans/2026-11-18-p3b-editing.md), DEC-017; [07 §15](07-topology-api.md#15-editing-additions-p3b): compaction, generation floor, stable hinted region IDs, filtered validation and OnCommit, `imprint`/`add_hole`/`weld`); `connect_loops` done 2026-11-24 ([plan](../../plans/2026-11-24-connect-loops.md), DEC-021); incremental regions: W0, W1 and W4 done, the rest deferred ([plan](../../plans/2026-11-24-incremental-regions.md)) | The primitives from 01 §7; Propagator with schema policies; journal; compaction | Met for P3a: randomised edit sequences validate after every commit; failed edits and undo restore bit-identical hashes; propagation invariants hold; Local validation agrees with Full |
| **P4 — Curves and pools** — *P4a done 2026-11-18* ([plan](../../plans/2026-11-18-p4a-curves.md), DEC-018; [07 §16](07-topology-api.md#16-curves-and-the-geometry-store-p4a): the store in `Complex`, `cesaro2`/`offset2`/`alignment3`/`catenary3`/`polyline3`/`bspline`, certified bounds, curved edges in the editor, UTCX format 3); *P4b done 2026-11-21* ([plan](../../plans/2026-11-18-p4b-surfaces.md), DEC-019; [07 §17](07-topology-api.md#17-surfaces-pcurves-and-the-refined-skeleton-p4b): `road3`, `graph2`/`segment2` pcurves, the refined skeleton, UTCX format 4); P4c remains: `Plane`, `Heightfield`, `Sweep`, `BSplineSurface`, queries, daylighting, junctions, edits on surfaced faces | GeometryStore, CurvatureLawCurve, OffsetCurve, Catenary, B-splines, RoadSurface | Met for P4a: certified-bound tests and oracle comparison for every kind; curved edges survive edits, undo/redo, barriers, freeze and I/O. Met for P4b: a road placed into a terrain sheet by hole and fill, validated, edited, undone, frozen and round-tripped bit-identically in `double` and `Lattice` |
| **P5 — Exact Booleans** (former Spike 1) | Arrangement-and-select on `Lattice`; resolve OI-1 | Differential tests against Manifold (closed solids) and CGAL Nef (offline oracle); determinism |

**Dependencies.** P0 → P1 → P2 → (P3, P4 in parallel) → P5. P2 can start on a minimal
version of P1, a typed paged column without copy-on-write, while P1 completes.

## 4. Open issues

These are summarised in the [README](README.md#open-issues-tracked-here-until-resolved).
In addition, from the decisions log: **Q-H**, whether Sculptcore adopts the new
`litestl::attr`.
