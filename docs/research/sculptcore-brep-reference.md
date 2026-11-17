# Sculptcore's mesh B-rep as a reference for UberTopo

**Source:** [joeedh/sculptcore](https://github.com/joeedh/sculptcore) at commit `d508c0b`
(read 2026-11-17). Files studied: `source/mesh/` and `documentation/mesh.md`, plus the
headers listed in the table below. Sculptcore is MIT-licensed and by the same author, so
code may be lifted, not just studied.

UberTopo will use a struct-of-arrays approach similar to Sculptcore's (see
[DEC-006](../decisions.md)). This doc compares Sculptcore's design with the UberTopo
topology recommendation in [brep-topology/08](brep-topology/08-critique-and-recommendation.md)
and lists what carries over unchanged, what must change, and what must be added.

## 1. What Sculptcore has

Sculptcore's mesh is an index-based, BMesh-like non-manifold B-rep. It has five element
domains, each a struct of paged attribute columns:

| Domain | Topology columns (all `int`, `ELEM_NONE = -1`) | Notes |
|---|---|---|
| `VERTEX` | `v.e` (disk entry) | `co` is `float3` |
| `EDGE` | `e.vs` (int2, authoritative), `e.disk` (int4, side-bit packed `(edge<<1)\|side`), `e.c` (radial entry) | The disk cycle is derived from `vs` on load |
| `CORNER` | `c.v`, `c.e`, `c.l`, `c.next/prev` (boundary cycle), `c.radial_next/prev` (radial cycle) | A coedge / "loop" in BMesh terms |
| `LIST` | `l.c` (head corner), `l.f`, `l.next` (next list of the same face), `l.size` | **One list per boundary cycle; holes are additional lists** |
| `FACE` | `f.l` (first list), `f.list_count`, `f.no` | Faces with any number of holes |

Supporting machinery:

| Mechanism | File | What it does |
|---|---|---|
| `ElemData` | `elem_data.h` | Per-domain `AttrGroup` + `freemap`/freelist slot recycling. **Indices are stable until freed.** `alloc_near(hint)` gives locality, and `on_swap` callbacks support reorders. |
| `AttrData<T>` | `attribute.h` | Columns paged at `ATTR_PAGESIZE = 4096`. Pages are **lazily materialised**: a page with `exists=false` reports one default value for every slot. |
| `PackedBoolAttrs` | `attribute_bool.h` | All of a domain's bool attributes share bit-packed storage. |
| `BuiltinAttr<T, "name", Flag>` | `attribute_builtin.h` | Typed named columns that auto-register; topology is just attributes flagged `TOPO`/`DERIVED`. |
| `AttrFlag` | `attribute_enums.h` | `TOPO`, `TEMP`, `NOCOPY`, `NOINTERP`, `TOPO_KEEP_FROZEN`, `DERIVED`. |
| `AttrMerge` | `attr_merge.{h,cc}` | Per-layer policy for values created by split or collapse (`DEFAULT` lerp, `COPY_SRC0`, `NONE`, `CUSTOM`). |
| `DeformPool` | `deform_pool.{h,cc}` | **Interned, refcounted, sharded side table.** The column stores only a 32-bit slot index; variable-size data lives in the pool. |
| `MeshCallbacks` + `meshlog/` | `mesh_callbacks.h` | Every topology operation fires create/kill/change hooks. The meshlog records them as topo chunks for undo/redo. It also has reorder chunks and sparse attribute-swap chunks. |
| `FrozenTopo`, `VertNbrCSR` | `mesh_topo_cache.h` | CSR snapshots of the disk, radial, face→lists and list→corners relations, and rebuild of the link columns from a snapshot. |
| `mesh_validate.h` | `utils/` | Structured health report (census, manifoldness, Euler). |
| Serialisation | `mesh_serialize.{h,cc}` | Versioned, lz4hc-compressed blobs with `migrate()`. |
| Spatial tree | `source/spatial/` | A BVH over the mesh. Ownership lives on mesh attributes (`.spatial.f.node`), and ray casts happen per leaf. |
| Bindings | `defineBindings()` everywhere | The TS API is generated through litestl. |

## 2. Fit against the UberTopo recommendation

### Carries over as-is (or nearly)

- **Struct-of-arrays domains with topology stored as ordinary attribute columns.** This
  is exactly the recommended SoA-arena layout (R3).
- **Faces with holes as multiple lists**, no bridge edges, and no stored "outer" flag
  needed. Matches [brep-topology/03](brep-topology/03-faces-with-holes.md).
- **Corner = coedge** (oriented edge use in a face), with a **radial cycle**, so edges can
  have any number of faces (non-manifold). **Wire edges** (`e.c = NONE`) and isolated
  vertices are naturally representable.
- **Side-bit-packed disk links.** This is the same economy the research recommends.
- **Paged columns with lazily materialised pages.** This is the natural base for
  copy-on-write paging (see §3).
- **Flag-driven attribute behaviour** (`TOPO`, `DERIVED`, `NOINTERP`) plus **per-layer
  merge policies.** This is the start of the schema-registered attribute behaviour
  recommended in [attributes-and-queries/recommendations](attributes-and-queries/recommendations.md).
- **Interned side-table pool behind a slot-index column (`DeformPool`).** This is the
  right template for UberTopo's **geometry pools**: curves on edges, pcurves on corners,
  surfaces on faces, plane tables, material records. The column stays a plain int, so
  reorder, swap and serialise keep working.
- **Callback-driven topology journal (meshlog).** This matches the "journaled primitive
  ops" recommendation (R4) for undo, lineage and incremental validation.
- **`FrozenTopo` CSR snapshots.** This is the "frozen mode" of R3, already prototyped.
- **Validation report and versioned serialisation with migration.**

### Must change

| Sculptcore | Why it doesn't fit UberTopo | Change |
|---|---|---|
| `co` is `float3` | Site-scale coordinates need f64, or exact lattice coordinates (open decision D1 in [README](README.md#3-where-the-tracks-disagree-decisions-needed)) | DEC-007: the coordinate type is a concept-constrained template parameter (`double`, exact lattice, ...) |
| **Indices used as identity** (`IDMap` deprecated in favour of "mesh indices work as ids") | Procedural caching, overrides and simulation ground truth need IDs stable across *re-evaluation*, not just across edits | Add a **128-bit persistent-ID column** per domain, derived from provenance. Indices stay the fast internal handle |
| Radial cycle is **unordered** | Assigning regions and finding diffraction-edge wedges need faces **sorted by angle** around each edge | Keep `radial_next/prev`, but maintain angular order on insert, or rebuild it in frozen mode |
| Pages are copied, not shared | Versioned immutable snapshots for the procedural cache and concurrent readers | **Refcounted copy-on-write pages.** Per DEC-003, the paged column moves into litestl |
| `AttrType` is a fixed bitmask (float/int vectors, bool, byte, short, weights) with `type_dispatch` switches (closed set, DEC-007 relaxes this) | Needs `double`, `int64`, `u128` IDs, fixed-length band arrays (e.g. 8 or 24 acoustic bands), enum/material handles, and references to other elements or pools | Extensible type registry with schema metadata (unit, role, frame, quantity kind, merge policy) |
| Bools bit-packed per domain | Fine | Keep |
| Fan triangulation in `utils/triangulate.h` | Not valid for concave faces or faces with holes | Constrained Delaunay on exact predicates (DEC-002) |
| `MESH_FACE_VS_LIMIT = 10000` iteration guard | Terrain and footprint faces can be larger | Raise it, or make it a debug-only check |

### Must be added (not in Sculptcore)

1. **Shell and Region domains**, plus a **FaceSide** notion (`f.region[2]`). Every face
   names the region on each side. Shared walls exist once. There is always an unbounded
   region.
2. **Inclusions**: vertices, edges or faces lying in the open interior of a face or
   region (sensors on walls, centrelines on terrain, power lines in air). These go
   either in container columns (`v.container`, `e.container`) or in CSR inclusion lists.
3. **Geometry pools** on the `DeformPool` pattern:
   - transcendental and B-spline curves (on edges);
   - pcurves (on corners);
   - surfaces, including road (s, t) surfaces (on faces);
   - planes (if plane-based, D1/D2).
4. **Feature-edge classification** (feature / smooth / tessellation / tile-cut), wedge
   angle and per-side materials. These are required by acoustics and diffraction.
5. **Tile partitioning**: independent complexes per tile, with shared virtual boundary
   faces.
6. **Determinism contract**: Sculptcore's sharded pool and `parallel_capture` are tuned
   for interactive throughput. UberTopo needs results independent of thread count, so
   interning order, slot assignment and reductions must be deterministic, or slots must
   be renumbered canonically before hashing or serialising.

## 3. Proposed reuse path (input to the core data-model design doc)

Per DEC-003, general-purpose pieces go into litestl. Sculptcore can adopt them later.

| Piece | Destination | Notes |
|---|---|---|
| Paged column `AttrData<T>` + `AttrGroup` + `BuiltinAttr` + packed bools | **litestl** (generalised, with COW pages and an extensible type registry) | Shared foundation. Sculptcore can migrate when convenient |
| Interned refcounted pool (`DeformPool` pattern) | **litestl** (generic `InternPool<T>`) | Used for UberTopo geometry pools |
| `ElemData` slot allocator (freemap/freelist, `alloc_near`, `on_swap`) | **litestl** or UberTopo | Generic, but tightly coupled to `AttrGroup` |
| V/E/Corner/List/Face topology + Euler ops | **UberTopo** (new code modelled on Sculptcore, extended with Shell/Region/inclusions) | Lift `disk_*`/`radial_*` helpers and the validation report |
| `FrozenTopo` CSR snapshot | UberTopo | Becomes the frozen mode, with Morton ordering and GPU upload |
| Meshlog callback journal | UberTopo (pattern) | Becomes the journal of primitive ops for versions, lineage and undo |
| Spatial tree | Reference only | UberTopo needs instanced TLAS/BLAS, f64, batch queries, and generate-on-query proxies |

Whether to **move** Sculptcore's attribute system into litestl, rather than write a new
generalised one there, is an open question in [decisions.md](../decisions.md). Moving it
would also change Sculptcore.

## 4. Open questions arising

- Should Sculptcore's `AttrData`/`AttrGroup` be moved into litestl and generalised, which
  would make Sculptcore a client, or should litestl get a new implementation modelled on
  it?
- Should UberTopo keep Sculptcore's "corner per face-vertex" granularity (BMesh loops),
  or use partial-entity coedges (one per face–edge incidence with a side bit)? For
  polygonal faces these are the same count. The difference only matters for faces with
  boundary-less loops (isolated vertex loops), which BMesh-style lists can't express.
