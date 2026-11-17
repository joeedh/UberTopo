# Core Data Model — Design

**Status:** draft v0.2 (2026-11-17). OI-1, OI-2 and OI-5 resolved by [DEC-008](../../decisions.md). Not yet reviewed. Implementation plans derived from
it go in `docs/plans/` and are pressure-tested first (see `CLAUDE.md`).

This design covers UberTopo's central data structure:

- the non-manifold cell complex (topology);
- the coordinate type and the geometric predicates that come with it;
- the open attribute system;
- identity, versions and the edit journal;
- the geometry pools that attach curves and surfaces to topology.

It does **not** cover:
- the procedural evaluation engine;
- spatial queries and the BVH;
- tessellation;
- simulation exports.

Those are later designs that build on this one.

## Files

| File | Contents |
|---|---|
| [01-cell-complex.md](01-cell-complex.md) | Entities, topology columns, face sides and regions, radial order, inclusions, invariants, primitive operations, validation |
| [02-coordinates-and-predicates.md](02-coordinates-and-predicates.md) | `Coord` concepts, traits, instantiations (`double`, `float`, lattice, plane-based option), predicates, tile frames, conversion between instantiations |
| [03-attributes.md](03-attributes.md) | Open attribute system: column interface, paged copy-on-write storage, type registry, domains (including FaceSide), schemas, propagation policies |
| [04-identity-versions-journal.md](04-identity-versions-journal.md) | Indices, handles, 128-bit persistent IDs, keyed RNG, versioned snapshots, journal, editable/frozen modes, compaction, determinism contract |
| [05-geometry-pools.md](05-geometry-pools.md) | Geometry references on edges, coedges and faces; pool pattern; curve and surface kinds; exact/approximation layering |
| [06-modules-litestl-and-phasing.md](06-modules-litestl-and-phasing.md) | Source layout, namespaces, the work list for litestl, testing, phased delivery, open issues |

## Inputs

**Decisions:** [DEC-001 … DEC-007](../../decisions.md). Summary:
- C++20 with concepts, on litestl.
- Avoid third-party libraries in the core.
- General-purpose additions go into litestl; B-splines stay in UberTopo.
- Struct-of-arrays (SoA) layout modelled on Sculptcore.
- The coordinate type is a template parameter.
- The attribute system is open.

**Research:**
- [Research synthesis](../../research/README.md).
- [brep-topology/08](../../research/brep-topology/08-critique-and-recommendation.md),
  [attributes-and-queries/recommendations](../../research/attributes-and-queries/recommendations.md),
  [procedural-systems/determinism-and-identity](../../research/procedural-systems/determinism-and-identity.md),
  [cad-kernels/recommendations](../../research/cad-kernels/recommendations.md).
- [Sculptcore B-rep reference](../../research/sculptcore-brep-reference.md).

## Design at a glance

```
                     ┌────────────────────────────────────────────────┐
                     │ Complex<Coord>   (one tile / one object)       │
                     │                                                │
  topology domains   │ Vertex  Edge  Coedge  Loop  Face  Shell Region │  SoA, u32 indices,
  (01)               │   │      │      │      │     │ ╲      │     │  │  freelists (editable)
                     │   │      │      │      │     │  FaceSide=(f,s) │  or dense (frozen)
                     ├───┼──────┼──────┼──────┼─────┼──────────┼─────┤
  attributes (03)    │ AttrGroup per domain: open set of Column classes │  paged, COW, typed
                     │ (topology links are builtin columns)            │
                     ├─────────────────────────────────────────────────┤
  positions (02)     │ Vertex.pos : CoordTraits<Coord>::Point          │  only Coord-templated column
                     ├─────────────────────────────────────────────────┤
  geometry refs (05) │ Edge.curve, Coedge.pcurve, Face.surface :       │  GeomRef{kind,index}
                     │ GeomRef ──► shared GeometryPools (double, tile) │
                     ├─────────────────────────────────────────────────┤
  identity (04)      │ PersistentId (u128) columns · generation cols   │
                     │ journal (optional) · version = COW snapshot     │
                     └─────────────────────────────────────────────────┘
```

**The ten core rules:**

1. **The topology is a stratified, non-manifold, mixed-dimensional 3D cell complex.**
   Its entities are Vertex, Edge, Coedge, Loop, Face, Shell and Region. Every face has two
   sides, and each side names a Region. Shared walls exist once.
2. **Faces have zero or more boundary loops.** Holes are just additional loops, with no
   outer flag and no bridge or seam edges.
3. **Everything is struct-of-arrays.** Topology links are ordinary `u32` columns in the
   same attribute groups as user data, as in Sculptcore.
4. **`Complex<Coord>` is templated on the coordinate type**, constrained by concepts.
   Only vertex positions (and, in the plane-based option, a plane table) depend on
   `Coord`. Attributes, IDs, topology and the analytic geometry pools do not.
5. **Topological decisions use exact or adaptive-exact predicates** supplied by
   `CoordTraits`. There is no global epsilon.
6. **The attribute system is open.** Custom column classes plug in through a small
   virtual interface. Dispatch happens per column, never per element, and there is no
   central switch over all types.
7. **Storage is copy-on-write paged.** Copying a `Complex` produces a cheap immutable
   snapshot. The procedural cache, undo and concurrent readers all use snapshots.
8. **There are three kinds of identity.**
   - Storage index: fast, internal, unstable across compaction.
   - Generational handle: external, detects stale references.
   - 128-bit persistent ID: derived from provenance and stable across re-evaluation.
9. **Analytic geometry lives in shared pools**, referenced by `GeomRef` columns. This is
   the Sculptcore `DeformPool` pattern. Exact and transcendental definitions sit above
   certified B-spline caches, which sit above tessellation.
10. **Determinism is part of the contract.** The same inputs give bit-identical topology,
    IDs and serialisation, regardless of thread count.

## Open issues (tracked here until resolved)

| # | Issue | Where |
|---|---|---|
| OI-1 | ~~Plane-based exact representation~~ **Resolved (DEC-008):** `Lattice` vertices plus an exact plane table, with homogeneous points inside the Boolean engine only. **Re-review in P5** | [02 §5](02-coordinates-and-predicates.md#5-the-plane-based-option-d2) |
| OI-2 | ~~Region maintenance~~ **Resolved (DEC-008):** rebuilt by `build_regions()`, with a `regions_dirty` state | [01 §6](01-cell-complex.md#6-shells-and-regions-maintenance-model) |
| OI-3 | Page size for copy-on-write columns (Sculptcore uses 4096; the research suggests 1–16K). Measure | [03 §3](03-attributes.md#3-paged-copy-on-write-storage) |
| OI-4 | ~~Choice of 128-bit hash function~~ **Resolved (DEC-009):** SipHash-2-4-128 with type-tagged, domain-separated tuple encoding | [04 §3](04-identity-versions-journal.md#3-persistent-ids) |
| OI-5 | ~~Location of the attribute machinery~~ **Resolved (DEC-008):** litestl `attr/` from day one | [06](06-modules-litestl-and-phasing.md) |
| OI-6 | Sculptcore adoption of the new attribute system (Q-H) | [decisions.md](../../decisions.md) |
