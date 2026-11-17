# 04 — Identity, Versions and the Journal

Part of the [core data model design](README.md). Research basis:
[brep-topology/06 §5–8](../../research/brep-topology/06-memory-layout-performance.md),
[procedural-systems/determinism-and-identity](../../research/procedural-systems/determinism-and-identity.md),
[attributes-and-queries/recommendations §5](../../research/attributes-and-queries/recommendations.md).
The journal follows Sculptcore's meshlog pattern.

## 1. Three kinds of identity

| Kind | Type | Stable across | Used for |
|---|---|---|---|
| **Storage index** | `Ref<D>` (u32, distinct type per domain) | Edits that don't free the slot. Changed by compaction or freeze | All internal links and hot loops |
| **Generational handle** | `Handle<D>{u32 index, u32 gen}` | Detects use-after-free and use-after-compaction | References held *outside* the complex (UI selection, query results, caches) |
| **Persistent ID** | `PersistentId` (u128) | Re-evaluation, compaction, snapshots, LOD changes (by lineage) | Procedural overrides, cache keys, simulation ground truth, cross-tile references, provenance |

- **Generation counters** are a `u32` column per domain. They are bumped when a slot is
  freed, and bumped for all slots when a compaction permutes a domain.
- **Handle resolution** is `index < capacity && gen[index] == handle.gen && live`. On
  failure, the holder re-resolves through the persistent ID (§3).

## 2. Typed references

```cpp
template <Domain D> struct Ref {             // like Sculptcore's ElemRef<>
  uint32_t i = NONE;
  explicit constexpr Ref(uint32_t i_) : i(i_) {}
  constexpr explicit operator bool() const { return i != NONE; }
};
template <typename T> concept ElementRef = requires { T::domain; } && std::same_as<T, Ref<T::domain>>;
```

`Ref<Edge>` and `Ref<Face>` don't convert into each other, which catches whole classes of
index bugs at compile time. Columns store raw `u32`. Accessors return `Ref<D>`.

## 3. Persistent IDs

**Storage.** A `.id` column of type `PersistentId` exists on Vertex, Edge, Face, Loop
(for openings) and Region. Coedges and shells don't need one: their identity is
derivable from (edge, face) and from region and face-side membership respectively.

**Derivation.** This is the procedural-research scheme, shared by generators and kernel
operators:

```
generator-emitted:   id = H128(parent_id, op_site_id, discriminator)
operator-derived:    id = H128(parent_id, op_kind, role)
     split_face f by curve c  -> children: H128(f.id, SPLIT, side_of(c) ∈ {L,R})
     split_edge e at t        -> H128(e.id, SPLIT, {lo, hi}),  new vertex H128(e.id, SPLIT_V, 0)
     merge a,b                -> survivor keeps min(a.id, b.id); lineage records both
     flip_face                -> id unchanged (sides swap; FaceSide identity = (face id, side))
     imprint e on f           -> e.id unchanged; face children as split_face
```

- **Discriminators** are chosen per operation, from most to least stable:
  1. external ID (OSM, cadastral);
  2. semantic key relative to stable anchors;
  3. world-lattice cell;
  4. quantised local position;
  5. index, flagged as unstable.
- **Geometric roles.** `side_of(c)`, lo/hi and similar roles are defined by **geometry and
  orientation relative to the operating entity**, never by storage order. This makes IDs
  independent of thread scheduling.
- **Collisions.** At 128 bits they are statistically negligible. `freeze()` still checks
  ID uniqueness within a complex in debug builds.
- **Lookup.** The map `PersistentId → Ref` is a derived cache per frozen version, built
  lazily.

**Hash function (OI-4, resolved by DEC-009).** SipHash-2-4 with 128-bit output, in
`litestl::hash::Hasher128`. Every hash begins with a **`DomainTag`**, so that generator
IDs, operator-derived IDs and content hashes never share a space. Every field is
**type-tagged**, so tuples can't collide structurally. `H128(...)` in this document means
a `Hasher128` over the listed fields in order. Identity uses `f64_canonical`; content
hashes use `f64_bits`. Golden values are pinned in litestl tests.

## 4. Keyed randomness

This lives at the procedural level, but the kernel relies on it, so it is specified here:

```
bits = Philox4x32-10( key = H128(world_seed, op_site_id, stream_name)  (lo = key, hi = salt),
               counter = {elem_lo32, elem_hi32, draw, block} )   // DEC-009 layout
```

- **Counter-based and stateless.** It is order-independent and thread-count-independent.
  Philox4x32-10 is the proposal (well studied, simple); Squares is the alternative.
  It is re-implemented in litestl (`litestl::rng`, DEC-009). The weak LCG in `util/rand.h`
  is left in place for existing users.
- **Named streams** mean that adding a new random attribute never shifts existing ones.

## 5. Versions: editable and frozen

**A `Complex` is a value type.** Copying it makes a **snapshot**: every column calls
`share()` ([03 §4](03-attributes.md#4-the-column-interface)), at a cost of O(pages)
refcount increments.

- **The procedural cache** stores snapshots as node outputs. A downstream node copies its
  input and edits the copy, touching only the pages it writes.
- **Undo** keeps snapshots and/or the journal (§6).
- **Concurrent readers** hold a snapshot. Readers never block writers, because writers
  copy pages on write.
- **Version stamps.** Each complex carries
  `topology_version`, `geometry_version` and `attr_version[column]`, which are `u64`
  counters bumped on mutation. Derived caches (CSR adjacency, inclusion lists, BVH,
  computed attributes) record the versions they were built from.

**Freeze** ([01 §9](01-cell-complex.md#9-editable-and-frozen-modes)) runs these steps:
1. Compact live slots and remove tombstones.
2. Compute the Morton order of element centroids within the tile.
3. Make coedges of the same loop contiguous.
4. Build the `old → new` remap.
5. `permute` every column in parallel.
6. Bump generations.
7. Build the frozen caches.

Freezing always produces a **new** snapshot; old readers are unaffected.

**Thaw** is just copying a frozen snapshot and editing it. Columns are the same, so there
is no conversion. Freelists start empty.

## 6. Journal

An optional journal per editable complex records the **primitive operations**
([01 §7](01-cell-complex.md#7-primitive-operations)) as they execute. This is the
Sculptcore `MeshCallbacks` → meshlog pattern.

```cpp
struct JournalEntry {
  PrimitiveOp op;                       // make_edge, split_face, ...
  util::Vector<uint32_t> refs;          // touched element indices (pre/post)
  util::Vector<PersistentId> lineage;   // created-from / split-from / merged-from
  ColumnDelta deltas;                   // sparse prior values for touched rows (undo)
};
```

**Uses:**
- **Fine-grained undo/redo** by applying inverse primitives.
- **Lineage**, which lets external references resolve forward across versions. For
  example, "face X was split into X.L and X.R; the reference policy is follow both, or
  pick the larger".
- **Incremental validation** (check only touched elements).
- **Diffing** between versions, for incremental re-tessellation and simulation
  re-export of dirty tiles.
- **Deterministic replay** of a failing run.

**When it's off.** Bulk builders and procedural evaluation usually run with the journal
**off**, relying on snapshots for undo and caching. Lineage for procedurally generated
elements comes from the ID derivation itself. The journal is mainly for interactive
editing and debugging.

## 7. Compaction

Compaction runs at freeze, or when tombstones exceed about 25% of a domain:

- **Links:** produce `old → new` per domain and rewrite internal links in one parallel
  pass.
- **Attribute columns:** permute.
- **FaceSide:** permuted by the induced map.
- **Handles:** invalidated by the generation bump.
- **Persistent IDs:** unaffected.

## 8. Determinism contract

These are testable requirements for every operation in the core:

1. **Same inputs, same outputs.** The same inputs (including seeds and parameters) give
   bit-identical topology columns, IDs, attribute pages and serialised bytes. This holds
   for any thread count and on any supported platform, for `Lattice`, and for `double`
   *decisions*.
2. **Free-slot reuse is deterministic.** It is LIFO within single-threaded editing.
   Parallel builders never use freelists: they allocate dense ranges, then **sort by
   canonical keys** (persistent ID, then lexicographic position) before assigning
   indices.
3. **Parallel reductions affecting output use fixed-chunk ordering.** This means a
   deterministic `parallel_reduce` added to `litestl::task` with chunking that doesn't
   depend on worker count.
4. **Interned pools assign slots in canonical order.** They do this either
   single-threaded or with a renumbering pass before freeze or serialise. This
   addresses Sculptcore's sharded-pool nondeterminism ([05](05-geometry-pools.md)).
5. **Serialisation is canonical:** insertion-ordered columns, canonical encodings and
   sorted maps.
6. **Floating-point discipline:** no `-ffast-math`, `-ffp-contract=off` in geometry
   translation units, and no `std::` transcendental functions on decision paths. The
   curve library uses its own evaluation ([05](05-geometry-pools.md)).

CI runs determinism tests at 1, 2 and N threads and compares hashes across the compilers
in use.
