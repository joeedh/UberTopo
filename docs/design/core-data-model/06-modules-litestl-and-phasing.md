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
| `hash128` (stable, seedable) | `util/hash.h` | 03, 04, 05 | OI-4. Golden-value tests |
| Counter-based RNG (Philox4x32-10) | `util/rng.h` | 04 | Replaces the use of `util/rand.h`; leave the old one for Sculptcore compatibility |
| Deterministic `parallel_reduce` / ordered `parallel_for` chunks | `util/task.h` | 04 §8 | Chunking independent of worker count |
| Paged copy-on-write store, `ColumnBase`, `Column<T>`, `BoolColumn`, `AttrGroup`, `ColumnTypeRegistry` | `attr/` (new module) | 03 | OI-5; new design modelled on Sculptcore (DEC-007) |
| `InternPool<T>` (refcounted, canonical renumbering) | `attr/intern_pool.h` | 05 | Generalised `DeformPool` |
| `UniquePtr` | `util/memory.h` | 03 | Missing: litestl has only `shared_ptr`/`weak_ptr` |
| io `Writer`/`Reader` interface for columns | `io/` | 03 | Build on `binfile` |

**Push access.** Granted (DEC-008). Changes are made in the submodule, pushed to a
litestl feature branch, and then the pointer is bumped here.

## 3. Phased delivery

Each phase becomes a plan in `docs/plans/` and is pressure-tested before work starts.
They replace the four spikes listed in the [research synthesis](../../research/README.md#5-recommended-next-steps).

| Phase | Scope | Exit criteria |
|---|---|---|
| **P0 — Foundations (litestl)** | Wide ints, predicates, hash128, Philox RNG, deterministic reduce | Predicate test suite against an independent big-rational oracle; golden hashes; thread-count invariance of the reduce |
| **P1 — Attribute core (litestl `attr/`)** | Paged copy-on-write store, column interface, registry, AttrGroup, packed bools, InternPool | Copy-on-write snapshot tests; open-extension test with a test-only column class; serialise round trip; page-size measurements (OI-3) |
| **P2 — Complex core (UberTopo `topo/`)** | `Complex<C>` for `double` and `Lattice`; domains and builtin columns; bulk builder; radial sort; `build_regions()`; validator; freeze and thaw; persistent IDs | Generated buildings with shared walls, window holes, aperture faces and rooms produce the correct regions and pass all validation layers, bit-identically at 1 and N threads and in both instantiations |
| **P3 — Primitives, propagation and journal** | The primitives from 01 §7; Propagator with schema policies; journal; compaction | Randomised edit sequences: validation passes; undo through the journal restores page-hash equality; propagation invariants hold |
| **P4 — Curves and pools** | GeometryStore, CurvatureLawCurve, OffsetCurve, Catenary, B-splines, RoadSurface | Certified-bound tests; oracle comparison; a road imprinted on terrain sheets |
| **P5 — Exact Booleans** (former Spike 1) | Arrangement-and-select on `Lattice`; resolve OI-1 | Differential tests against Manifold (closed solids) and CGAL Nef (offline oracle); determinism |

**Dependencies.** P0 → P1 → P2 → (P3, P4 in parallel) → P5. P2 can start on a minimal
version of P1, a typed paged column without copy-on-write, while P1 completes.

## 4. Open issues

These are summarised in the [README](README.md#open-issues-tracked-here-until-resolved).
In addition, from the decisions log: **Q-H**, whether Sculptcore adopts the new
`litestl::attr`.
