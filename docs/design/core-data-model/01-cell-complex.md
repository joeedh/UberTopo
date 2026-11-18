# 01 — The Cell Complex

Part of the [core data model design](README.md). The research basis is
[brep-topology/04](../../research/brep-topology/04-surface-vs-volumetric.md),
[06](../../research/brep-topology/06-memory-layout-performance.md) and
[08](../../research/brep-topology/08-critique-and-recommendation.md). The storage is
modelled on Sculptcore's `source/mesh/`
([reference](../../research/sculptcore-brep-reference.md)).

## 1. Entities

```
Region ──1..n──► Shell ──set of──► FaceSide = (Face, side)
                                        │
Face ──0..n──► Loop ──cycle──► Coedge ──uses──► Edge ──2──► Vertex
                                 │ radial cycle (angular order around Edge)
Vertex.container / Edge.container ──► Face | Region     (inclusions)
```

| Entity | Dim | What it is |
|---|---|---|
| **Vertex** | 0 | A point. It may bound edges, or be *included* in a face or region (sensor, benchmark). |
| **Edge** | 1 | A curve segment between two vertices, which may coincide for a closed edge. It may bound faces through coedges, or be a **wire** included in a face or region (power line, centreline). |
| **Coedge** | — | One use of an edge by one loop of one face. There is one coedge per face–edge incidence, *not* per face side. Same role as Sculptcore's `CORNER` and BMesh's loop. |
| **Loop** | — | A closed cycle of coedges bounding a face, either an outer boundary or a hole. Same role as Sculptcore's `LIST`. |
| **Face** | 2 | A bounded piece of a surface with **0..n loops**. It has two **sides**, and each side names a Region. |
| **FaceSide** | — | `(face, side)` with `side ∈ {FRONT=0, BACK=1}`. Implicit index `2·face + side`. It has no table of its own, only an attribute domain. |
| **Shell** | — | A connected set of face sides that bound one region (§6). |
| **Region** | 3 | A volume cell: a room, a wall solid, water, soil, air, or a vegetation medium. Index 0 is always the **unbounded region**. |

There are no body, solid, sheet or wire *types*. Those are derived classifications of a
set of cells. "Building", "room" and "road segment" are semantic groupings expressed as
attributes (`sem.feature`, see [03](03-attributes.md)), so cells can change across LODs
without changing semantics.

## 2. Topology columns

All links are `u32` storage indices with `NONE = 0xFFFFFFFF`. They are **builtin
columns** in each domain's attribute group, the same as in Sculptcore, so reorder, swap,
serialise and copy-on-write all treat topology exactly like user data. Flags: `T` means
topology (authoritative); `D` means derived (rebuildable from the `T` columns).

| Domain | Column | Type | Flag | Meaning |
|---|---|---|---|---|
| Vertex | `pos` | `CoordTraits<C>::Point` | — | Position. This is the **only** column that depends on `Coord` ([02](02-coordinates-and-predicates.md)) |
| | `.v.e` | u32 | D | Any incident edge (disk entry) |
| | `.v.container` | `ContainerRef` | T | NONE, or the face or region the vertex is included in |
| Edge | `.e.vs` | u32×2 | T | Endpoints `vs[0]→vs[1]` define the edge direction |
| | `.e.disk` | u32×4 | D | Disk-cycle prev/next at each endpoint, side-bit packed `(edge<<1)\|slot` (Sculptcore `diskPack`) |
| | `.e.c` | u32 | D | Any coedge of the radial cycle. NONE means a wire edge |
| | `.e.container` | `ContainerRef` | T | For wire or embedded edges |
| | `.e.curve` | `GeomRef` | T | Carrier curve plus parameter range ([05](05-geometry-pools.md)). NONE means a straight segment |
| Coedge | `.c.e` | u32 | T | Edge used |
| | `.c.v` | u32 | D | Start vertex in loop direction, cached for speed as in Sculptcore |
| | `.c.l` | u32 | D | Owning loop |
| | `.c.next` | u32 | T | Next coedge in the loop |
| | `.c.prev` | u32 | D | Previous coedge in the loop |
| | `.c.radial_next` / `_prev` | u32 | D | Radial cycle in **angular order** (§4) |
| | `.c.flags` | u8 | T | bit0 `sense`: 1 if the coedge runs `vs[0]→vs[1]`. Needed explicitly because closed edges have `vs[0]==vs[1]` |
| | `.c.pcurve` | `GeomRef` | T | Optional curve in the face's parameter space |
| Loop | `.l.c` | u32 | T | First coedge |
| | `.l.f` | u32 | D | Owning face |
| | `.l.next` | u32 | T | Next loop of the same face |
| | `.l.size` | u32 | D | Coedge count |
| Face | `.f.l` | u32 | T | First loop, or NONE for a loopless face (closed periodic surface) |
| | `.f.region` | u32×2 | T | Region on the FRONT and BACK side |
| | `.f.shell` | u32×2 | D | Shell of each side |
| | `.f.surface` | `GeomRef` | T | Carrier surface. NONE means planar, with the plane derived from the loop; or, for a face flagged `ProjAxis` (opt-in, amendment A1), the canonical CDT of its projection along the stored axis |
| | `.f.flags` | u8 | T | `virtual` (tile seam or world box), `aperture` (fills a hole), `reversed` (surface normal opposes the front side) |
| FaceSide | `.fs.shell_next` | u32 | D | Next face side in the same shell (editable mode). Frozen mode uses a CSR list instead |
| Shell | `.s.region` | u32 | D | Region bounded |
| | `.s.first` | u32 | D | First face side |
| | `.s.next` | u32 | D | Next shell of the same region |
| Region | `.r.shell` | u32 | D | First shell |
| | `.r.flags` | u8 | T | `unbounded` (index 0 only), `virtual` (outside the tile, beyond the world box) |

**Implementation note (P2a, [07 §5](07-topology-api.md#5-domains-and-schemas-toposchemah-topodomainh)).**
- **Flags.** "D" means *recomputable from the T columns*. Every link column is stored
  with litestl's `Topo` flag. litestl's `Derived` flag excludes a column from hashes
  and bytes, so it is reserved for freeze caches.
- **Bookkeeping columns.** Each domain also has `.gen` and `.live`, and `.id` where
  design 04 §3 says so.
- **Container hints.** Vertices and edges carry a `*.container_hint` column for region
  hints that are resolved by `build_regions()`.

`ContainerRef` is a u32 with a 2-bit kind tag (NONE, FACE, REGION) and a 30-bit index.
Thirty bits gives about 10⁹ elements per tile, well beyond any tile budget.

**Orientation conventions:**
- A face's **FRONT** side is the side its (sense-corrected) surface normal points into.
- Every loop is oriented so that the face interior lies to the **left** of each coedge
  when viewed from the front. Outer loops therefore run counter-clockwise and holes
  clockwise when seen from the front.
- No "outer loop" flag is stored. When an algorithm needs one, it is derived from
  orientation on planar faces or from parameter-space winding on curved ones.

**Rejected Sculptcore and BMesh fields:**
- `f.no` is replaced by a derived attribute, `geom.normal`.
- `MESH_FACE_VS_LIMIT` becomes a debug-only cycle guard sized from `.l.size`.

## 3. Faces with holes

A face holds a singly linked list of loops (`.f.l`, `.l.next`).

- **Holes** are additional loops. There are no bridge edges, seam edges or outer flag.
- **Apertures** such as windows and doors are holes in the wall face, *plus* an optional
  separate face carrying the `aperture` flag that fills the hole. The two share the hole
  loop's edges through the radial cycle. That aperture face can be glazing (a physical
  face with materials) or a virtual partition (an acoustic or visibility portal between
  regions).
- **Loopless faces** (`.f.l == NONE`) are allowed only on closed surfaces (sphere,
  torus), never on planes.
- A face must not use the same edge twice. Slits and cracks inside a face are
  **included edges** (§5), not loop members.

## 4. Radial order and the wedge rule

Coedges around an edge are kept in **counter-clockwise order about the edge direction**
`vs[0]→vs[1]` (right-hand rule).

**Ordering.**
- **Planar faces:** for each coedge, take the face's inward tangent direction
  `t = n × d`, where `n` is the front normal and `d` the coedge direction. Sort the `t`
  vectors by angle about the edge axis.
- **Comparisons are exact:** two directions are compared about the axis using
  orientation-predicate sign tests from `CoordTraits` ([02 §4](02-coordinates-and-predicates.md#4-predicates)).
  There is no epsilon.
- **Curved faces:** use the surface normal at the edge midpoint, in double precision,
  with curvature as the tie-break. The ordering is cached in `radial_next/prev` and
  flagged approximate.
- **Small cycles:** most radial cycles have 1–2 coedges. Insertion sorts into an existing
  cycle with at most a handful of comparisons. The frozen rebuild sorts all of them in
  parallel.

**The wedge rule.** This is what makes regions work. For a coedge `c` of face `f`, the
side of `f` that faces the wedge between `c` and `radial_next(c)` is:

```
side_toward_next(c) = sense(c) ? FRONT : BACK
```

This follows from the loop convention: with axis `a`, rotating `t = n × a` by +90° about
`a` gives `n`. So a coedge parallel to the edge has its front facing the
counter-clockwise neighbour, and an antiparallel coedge has its back facing it.

**Invariant W (wedge consistency).** Writing `cn = radial_next(c)`:

```
face(c).region[side_toward_next(c)] == face(cn).region[1 - side_toward_next(cn)]
```

Every wedge around every edge holds exactly one region. Validation (§8) checks W on
every edge. Shell and region construction (§6) are flood fills over the wedge relation.

## 5. Mixed dimension and inclusions

| Element | Representation |
|---|---|
| Isolated vertex in air (sensor, waypoint) | Vertex with `.v.e == NONE` and `container = REGION r` |
| Vertex on a wall (wall-mounted microphone) | Vertex with `container = FACE f`. Its position must lie on `f`, which the embedding check verifies |
| Wire edge in air (power-line span) | Edge with `.e.c == NONE` and `container = REGION r`. Its curve is a catenary ([05](05-geometry-pools.md)). Its endpoints are on poles (face vertices) or included vertices |
| Edge embedded in a face (lane marking, centreline before imprint) | Edge with `.e.c == NONE`, `container = FACE f`, and a pcurve on `f` stored as an attribute because there is no coedge to carry it |
| Sheet (fence, terrain patch, canopy top) | Face whose boundary edges have radial degree 1. Both sides may name the same region |
| Solid (building mass, slab, pole) | Region bounded by shells, with medium ≠ air |

**Inclusion lists** (face → included vertices and edges; region → included vertices and
edges) are **derived**. They are built lazily as CSR from the container columns and
invalidated by the topology version. Nothing on the edit path maintains them.

**Instanced bodies** (trees, vehicles, street furniture) are *not* cells. They are
separate prototype complexes placed by reference plus transform inside a region, at the
scene/tile level (a later design). This avoids a forest of 10⁶ trees creating 10⁶
cavities in the air region. Where an analysis needs one instance merged, it imprints a
copy.

## 6. Shells and regions: maintenance model

**Definitions.**
- A **shell** is a maximal set of face sides connected across edges by the wedge
  relation: from `(f, s)`, cross each boundary edge to the radially adjacent face side
  facing the same wedge.
- Face sides that touch only at a vertex belong to different shells. This is simpler
  than Parasolid's vertex-connected shells and is enough for every use case found.
- A **region** is a set of shells: one outer shell plus any cavity shells, except the
  unbounded region, which has no outer shell.
- A sheet forms a shell whose face sides all bound the same region (both sides of every
  sheet face). That is Parasolid's "sheet in a void" without a separate body type.

**Maintenance model (OI-2, proposed).** Shells and regions are **derived**, with a
`regions_dirty` state on the complex.

- **Construction:** generators and bulk builders emit faces with *region hints*, then
  call `build_regions()`. That pass:
  1. flood-fills shells over wedges;
  2. classifies shell nesting with exact point-in-shell tests (ray parity on the
     `Coord` predicates, or the generalised winding number for unclean data);
  3. assigns or merges regions, keeping hinted region IDs where consistent and
     reporting conflicts.
- **Fast-path operators** keep regions valid incrementally where it's trivial: splitting
  a face (both children inherit both regions), splitting an edge, imprinting a curve on
  a face, inflating a face into a slab (creates a known region).
- **Other operators** set `regions_dirty`. Queries that need regions (`locate`,
  region-adjacency, simulation export) assert that regions are clean or trigger a
  rebuild of the **dirty tiles only**.

Why: the research's main risk with explicit regions is region bookkeeping in every edit
operator. Most UberTopo geometry comes from construction, not interactive editing, and a
rebuild pass doubles as the test oracle for incremental maintenance.

**The world box.** Terrain is an open sheet. To make "below ground" a bounded region,
the tile can be closed with virtual faces (`flags.virtual`) on the tile boundary and
world box. These are never tessellated or exported as reflectors. Simulation exports
replace them with their own truncation boundaries.

**Implementation note (P2b; [07 §10](07-topology-api.md#10-regions-toporegionsh)).**
- **Sheet shells.** A shell holding both sides of every face it touches is a sheet
  shell, such as a fence, or a thin-walled shell open through a window. It has no
  volume and nests like a cavity.
- **Touching shells.** Two shells that share a face on opposite sides are never nested
  in each other. This covers the common case of a thin-walled room against the outside
  and a room against its thick wall.
- **Containment tree.** Cavity and sheet shells join the innermost outer shell that
  contains them, chosen by enclosed volume. P2 doesn't store an explicit containment
  tree of outer shells.

## 7. Primitive operations

**Public construction** goes through **bulk builders**:

```
builder.emit_face(span<span<VertexKey>> loops, surface, region_hint[2])
builder.emit_wire(VertexKey a, VertexKey b, curve, container)
builder.emit_vertex(point, container)
builder.build(policy)   // weld by key, then link coedges, sort radial cycles,
                        // build regions; parallel and deterministic
```

**Underneath** sit about 14 **journaled, invertible primitives**. They are the same
family as Sculptcore's `make_*`/`kill_*` plus non-manifold extensions. Each emits
propagation events for attributes ([03 §6](03-attributes.md#6-propagation)) and journal
records ([04 §6](04-identity-versions-journal.md#6-journal)).

| Primitive | Inverse | Effect |
|---|---|---|
| `make_vertex(p, container)` | `kill_vertex` | Isolated vertex |
| `make_edge(v0, v1, curve, container)` | `kill_edge` | Wire edge |
| `make_face(loops, surface, regions)` | `kill_face` | Face from existing edges, inserting coedges into radial cycles in order |
| `split_edge(e, t)` → `v` | `join_edge(v)` | Edge split at a parameter value. Coedges split along with it |
| `split_face(f, c_a, c_b, curve)` → `e` | `join_faces(e)` | Chord between two vertices of one loop |
| `connect_loops(f, c_outer, c_hole)` | `disconnect_loops` | Used **only** inside operators that need it transiently, never left in stable output (no bridge edges) |
| `add_hole(f, loop)` | `remove_hole` | Attach an existing loop as a hole |
| `embed(v\|e, container)` | `unembed` | Set the inclusion container |
| `imprint(f, e_included)` | `unimprint` | Turn an included edge into loop topology, splitting the face if it separates it |
| `flip_face(f)` | itself | Swap sides, regions and FaceSide attributes, and reverse loops |
| `set_region(f, side, r)` | `set_region(old)` | Region assignment |
| `make_region()` / `kill_region(r)` | each other | Region allocation |
| `merge_vertices(a, b)` | `split_vertex` | Weld, used by builders and import |
| `transfer_ids` (journal only) | — | Records persistent-ID lineage ([04](04-identity-versions-journal.md)) |

**As realised in P3a** ([07 §14](07-topology-api.md#14-editing-topoedit-p3a), plan
[P3](../../plans/2026-11-18-p3-editing.md)):
- **Region primitives dropped.** `set_region`, `make_region` and `kill_region` are
  gone: regions stay derived (§6), and side hints (`set_side_hint`) name them.
- **Different arguments.** `split_edge` takes an exact point rather than a parameter,
  and `split_face` takes two vertices of the outer loop.
- **Fast paths deferred (OI-P3-1).** The fast paths of §6 are deferred to P3b,
  because region IDs derive from minimum face-side IDs. Every face edit sets
  `regions_dirty`.
- **Still to come.** `connect_loops`, `add_hole`, `imprint` and `merge_vertices`
  are P3b.

**Booleans** are *not* primitives. They are arrangement-and-select passes (merge every
operand into one arrangement, compute the cells, select by membership). Spike 1 covers
them in a later design.

Euler operators are the internal vocabulary of the journal, not the public API.

## 8. Validation

Validation runs in three layers. The validator returns a structured report, following
Sculptcore's `mesh_validate.h` census and Euler output. It does not assert.

1. **Combinatorial** (cheap, always available in debug builds):
   - `next`/`prev` inverse; loops close; `c.l.f` consistent; `c.v`/`c.e`/`sense`
     consistent with `.e.vs`.
   - Radial cycle closed and all members share `c.e`; disk cycle closed.
   - No face uses an edge twice; loopless faces only on closed surfaces.
   - **Invariant W** on every edge.
   - Shells partition face sides; `region[side] == shell.region`; exactly one
     unbounded region.
   - Container references are valid. A vertex with incident non-embedded edges has
     `container == NONE`.
   - Generation counters monotonic ([04](04-identity-versions-journal.md)).
   - Generalised Euler–Poincaré check per connected component, from
     [brep-topology/05](../../research/brep-topology/05-invariants-operators-validity.md),
     as a cross-check only.
2. **Embedding** (geometry on topology):
   - Edge endpoints lie on the edge curve, and loop vertices on the face surface, within
     each carrier's *certified* error ([05](05-geometry-pools.md)).
   - Radial order matches geometry.
   - Pcurves agree with 3D curves.
3. **Global geometric** (expensive, used in tests and on import):
   - Loops don't self-intersect; holes lie inside the face; faces meet only along
     shared edges and vertices (no unrecorded intersections).
   - Shell closure and nesting agree with `build_regions()`.

**Implementation note (P2b; [07 §9](07-topology-api.md#9-validator-topovalidateh-validate_geomcc)).**
- **Euler–Poincaré.** The cross-check is implemented per vertex, as a planarity test of
  the link rotation system that the radial orders define. It needs no ground truth.
- **Generation monotonicity** is a freeze test, not a validator code, because it
  compares two complexes.

## 9. Editable and frozen modes

Both modes use the **same columns**. Freezing is compaction plus renumbering plus
cache-building. It is not a format conversion; Blender's BMesh/Mesh split is the
anti-pattern here.

| | Editable | Frozen |
|---|---|---|
| Free slots | Freelist + freemap (Sculptcore `ElemData`); indices stable until freed | None: dense and compacted |
| Element order | Allocation order (`alloc_near` locality hint) | Morton order on the centroid within the tile; coedges of a loop contiguous |
| Disk and radial links | Maintained | Maintained. The disk links are also kept in frozen mode for simplicity (Sculptcore keeps them always). The memory cost is accepted |
| Derived caches | Built lazily, invalidated by topology version | Built eagerly: vertex → edges CSR, shell → face-side CSR, inclusion CSR (Sculptcore `FrozenTopo` pattern) |
| Mutation | Allowed, with journal | Not allowed. A copy-on-write snapshot is thawed into an editable copy |

Compaction yields an `old → new` remap per domain. Links and attribute columns are
permuted in one parallel pass, and generation counters are bumped
([04 §5](04-identity-versions-journal.md#5-versions-editable-and-frozen)).

## 10. Template shape

```cpp
namespace ubertopo::topo {

template <geom::CoordType C>            // concept from 02
class Complex {
public:
  using Coord  = C;
  using Point  = typename geom::CoordTraits<C>::Point;

  VertexDomain<C> v;   // pos, .v.e, .v.container, user attrs
  EdgeDomain      e;   // not templated
  CoedgeDomain    c;
  LoopDomain      l;
  FaceDomain      f;
  FaceSideDomain  fs;  // implicit size 2*f.capacity()
  ShellDomain     s;
  RegionDomain    r;
  attr::AttrGroup global;  // complex-level attributes

  // copying a Complex shares all pages (COW snapshot, 04)
};

}  // namespace ubertopo::topo
```

Each domain's builtin topology columns are declared as an `attr::Schema` of
`attr::AttrDef`s, for example `.c.next` as
`AttrDef<".c.next", Column<uint32_t>, ColumnFlags::Topo>`, and stored in an
`attr::TypedGroup`. Topology code therefore writes `c.get<".c.next">()`, and a
misspelled or mistyped link column is a compile error (DEC-012).

Only `VertexDomain<C>` is templated. Every algorithm that doesn't touch positions takes
the topology domains as non-template types, which keeps template bloat in check. Typed
element references follow Sculptcore's `ElemRef<Domain>`, as a concept-constrained
`Ref<D>` that is a distinct type per domain.
