# Embedding Curves and Surfaces as Attribute Data

Part of [attributes-and-queries](README.md). This file covers how to attach geometry
objects (3D curves, curves-on-surface, carrier surfaces, procedural generators) to
topological elements, and how to store heterogeneous geometry objects in attribute
columns. Curve mathematics (clothoids, spiros, B-spline fitting) is covered by the
separate curves research topic. This file covers the *data-model* side.

## 1. The classical B-rep geometry binding

A classical B-rep binds geometry to topology like this:

| Topology | Geometry | Notes |
|---|---|---|
| Vertex | point | plus a tolerance in tolerant modellers |
| Edge | 3D curve + parameter range | one carrier curve per edge |
| Coedge (edge-use in a face) | **pcurve**: 2D curve in the face's (u, v) domain | one per face the edge bounds, two on a seam |
| Face | carrier surface + trimming loops | loops made of coedges |

OCCT is representative. `BRep_TEdge` holds a list of `BRep_CurveRepresentation`
objects: a 3D curve, curves-on-surface (pcurves keyed by (surface, location)),
polygon-on-triangulation, and so on. It also holds a tolerance, and `SameParameter` /
`SameRange` flags assert that the 3D curve and the pcurves are parameterised
consistently ([OCCT Modeling Data user guide](https://dev.opencascade.org/doc/overview/html/occt_user_guides__modeling_data.html);
[OCCT BRep format description](https://dev.opencascade.org/doc/overview/html/specification__brep_format.html)).

Observations relevant to a new design:

1. **This is already an attribute system,** just a hard-coded one. The edge's "3D
   curve" and the coedge's "pcurve" are attributes on the edge and coedge domains,
   and the per-edge representation list is an untyped multi-valued attribute.
2. **Polymorphism through class hierarchies.** `Handle(Geom_Curve)` points to a
   heap-allocated object of one of ~10 subclasses (`Geom_Line`, `Geom_Circle`,
   `Geom_BSplineCurve`, `Geom_TrimmedCurve`, `Geom_OffsetCurve`, ...). Each evaluation
   is a virtual call through a pointer, which is cache-hostile. Shared ownership by
   intrusive refcount is reasonable. This is the 90s design to replace with
   data-oriented storage.
3. **Tolerance is per element** and grows during modelling operations ("tolerance
   creep"). It is in effect a scalar attribute with a `max` merge rule.
4. **Approximations are second-class.** A transcendental curve (e.g. a clothoid) has
   to be approximated by a B-spline at construction time, and the exact definition is
   lost. The kernel only knows the B-spline.

## 2. Requirements from UberTopo

* **Exact definition + approximant.** Road reference lines are numerically integrated
  transcendental curves: clothoids/Euler spirals, Levien's spiros, polynomial
  clothoids. Levien describes spiro splines in his thesis
  ([Levien 2009, *From Spiral to Spline*](https://www2.eecs.berkeley.edu/Pubs/TechRpts/2009/EECS-2009-162.html)).
  OpenDRIVE geometry elements are `line`, `spiral`, `arc`, `poly3` (deprecated) and
  `paramPoly3`
  ([ASAM OpenDRIVE 1.8](https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/latest/specification/index.html)).
  The requirement says these are tessellated into polynomial B-splines. The kernel
  should keep **both**: the *defining* curve (exact parameters, used for station
  computation, regeneration and LOD) and the *approximant* (B-spline with a certified
  error bound ε, used for intersection, imprinting and meshing).
* **Curves on faces that are not edges.** Lane markings, kerb lines, contour lines,
  property boundaries, planned paths and drone corridors live *on* a terrain face but
  should not necessarily split it. Splitting a large terrain face by every lane
  marking explodes the topology.
* **Structured road geometry.** A road is a plan-view curve plus an elevation
  profile z(s), a superelevation/crossfall profile, lane width functions w_i(s) and
  offsets. That is a curvilinear coordinate system (s, t), not just a curve.
* **Raster-backed and procedural carrier surfaces.** Terrain faces carried by a
  heightfield/DEM tile, vegetation canopies carried by implicit or volumetric proxies,
  and building faces carried by planes.
* **Many LODs from one definition.** Embedded geometry should be re-tessellatable on
  demand at a requested tolerance.

## 3. Geometry as attribute columns

### 3.1 The binding as reserved attributes

Treat geometric binding as **reserved, schema-registered attributes** in the general
attribute system rather than as fields of the topology structs:

| Domain | Reserved attribute | Value type |
|---|---|---|
| vertex | `geom.point` | `dvec3` (frame-relative, see [large-world-coordinates-and-tiling.md](large-world-coordinates-and-tiling.md)) |
| vertex | `geom.tol` | `f64` |
| edge | `geom.curve` | `CurveRef` + `Interval` |
| edge | `geom.tol` | `f64` |
| coedge | `geom.pcurve` | `Curve2Ref` + `Interval` (+ sense) |
| face | `geom.surface` | `SurfaceRef` (+ sense) |
| region | `geom.medium_field` | optional `FieldRef` (VDB/raster/analytic) |
| face | `geom.embedded` | `EmbeddedCurveSet` (see §4) |

Benefits:

* One mechanism for COW, serialisation, propagation policies and introspection.
* A pure mesh is the same structure with `geom.curve`/`geom.surface` absent (straight
  edges, planar facets), so meshes and exact B-reps share a data structure. That is
  the "BMesh with holes" idea from the requirements, extended to exact geometry.
* Alternate representations become additional columns: `geom.curve_exact` (the
  clothoid) next to `geom.curve` (its B-spline approximant), and `geom.tess` for a
  cached polyline/triangulation at a tolerance.

The topology core (incidence arrays, radial and disk cycles) stays hard-coded for
speed. Only geometry moves into attributes.

### 3.2 Storing heterogeneous geometry objects

A `CurveRef` column must refer to lines, arcs, NURBS, clothoids, offset curves and
so on. The options:

| Option | Layout | Pros | Cons |
|---|---|---|---|
| A. Polymorphic pointer (OCCT `Handle`) | `vector<shared_ptr<Curve>>` | simple, extensible | pointer chasing, virtual dispatch, no SIMD, hard to serialise or upload to GPU |
| B. `std::variant` column | AoS of variants sized to the largest type | no heap | NURBS are variable-length and can't sit inline; wasted space |
| C. **Tag + index into per-type arenas** | column of `{u8 type, u32 index}`; one SoA arena per curve type (lines: 2×dvec3; arcs: center/normal/radius/angles; NURBS: offsets into knot/CP pools; clothoids: start pose, κ₀, κ', length) | cache-friendly batched evaluation per type, GPU-uploadable, trivially serialisable, sharing by index | needs a registry and type-dispatch tables; arena GC |
| D. Bytecode/expression | small programs (CSG/SDF style) | ultimate flexibility for procedural geometry | slow without a JIT; hard to intersect analytically |

**Recommendation: C**, with D as one "type" (procedural/expression curves) for rare
cases. Batched evaluation sorts queries by type tag and dispatches to a
per-type SIMD/GPU kernel. Data-oriented and ECS-style engines use the same pattern,
and it maps onto GPU "geometry pools" such as Embree's user geometry or OptiX custom
primitives with SBT records indexed by type.

**Immutability.** Geometry objects in arenas should be immutable once published
(value semantics). Editing a curve creates a new arena entry and updates the
reference. This gives free sharing across copies, LODs and threads, and makes the
references themselves the only COW-managed data. Unreferenced entries are reclaimed
by refcount or by epoch-based compaction. Many edges reference the same carrier: all
sub-edges of a split share one curve with different `Interval`s.

### 3.3 Exact and approximate pairs

For each transcendental curve store:

```
CurveExact   { kind: Clothoid|Spiro|PolyClothoid|..., params..., length }
CurveApprox  { BSpline (degree, knots, control points), max_error: f64,
               param_map: Reparam (exact s ↔ spline u), }
```

The `param_map` is important. Roads are parameterised by **arc length s**, while a
B-spline approximant has its own parameter u. Station queries, lane offsets and
OpenDRIVE (s, t) coordinates need s. Options: (1) fit the spline so u ≈ s (arc-length
parameterised fitting, possible to tight tolerance for clothoids since their curvature
is linear in s), or (2) store an explicit monotone 1D spline s(u) alongside. The
mapping's error must be included in the certified bound.

## 4. Embedded curve sets: curves on faces without imprinting

**Imprinting** a curve into a face (splitting it into sub-faces bounded by new edges)
is what classical kernels do for every curve that must be related to a face. For
large terrain with dense road networks and markings this is wasteful and
fragile. A better design is **lazy embedding**:

```
geom.embedded on face F:
  list of EmbeddedCurve {
      curve: CurveRef | Curve2Ref    # 3D curve, or pcurve in F's (u,v)
      range: Interval
      role:  breakline | marking | boundary | annotation | path
      payload: AttrRowRef            # row in a per-role attribute table
      imprint: never | on_mesh | always
  }
```

* **Breaklines** (kerbs, road edges, ditch lines) must appear as edges in any
  tessellation of F. They are *constraints* to a constrained Delaunay triangulation
  (CDT) of F's parameter domain. This is the standard GIS approach for TINs with
  breaklines ([Shewchuk, *Triangle*](https://www.cs.cmu.edu/~quake/triangle.html);
  [CGAL 2D Triangulations, constrained](https://doc.cgal.org/latest/Triangulation_2/index.html)).
  Tessellation inserts them on demand, and the B-rep topology need not change.
* **Markings and annotations** never split topology. Renderers draw them as
  decals/textures, and queries can find them through a per-face 2D index.
* **Boundaries** that carry semantics (lane boundaries, land-parcel boundaries) can
  be promoted to real edges by an explicit `imprint` operation when an algorithm needs
  sub-face attributes (e.g. a different material per lane).

The embedded curve set gives each face a small **2D spatial index** (an R-tree over
pcurve bounding boxes in (u, v)). The query "which embedded curves are near this
point on the face?" is then a per-face 2D query.

This is a **two-tier topology**: coarse B-rep topology for modelling and queries,
and fine embedded features resolved during tessellation. It matches how GIS
separates TIN breaklines from polygon topology, and how OpenDRIVE separates road
reference geometry from the surface mesh produced by tools such as RoadRunner or
esmini.

## 5. Curvilinear coordinate attributes (s, t) for roads

For road surfaces, the most useful per-vertex attribute after tessellation is the
**curvilinear coordinate** relative to the road's reference line:

* `road.id` (categorical, per face or face-side)
* `road.s` (station along the reference line), `road.t` (lateral offset), per vertex
  or face-corner
* `road.lane` (derived from t via the lane-width functions at s)

Computing (s, t) for a world point is a **closest-point-on-curve query** against the
exact curve, with a Newton iteration seeded from the approximant. For a whole mesh it
is a batched query. Keeping s and t as attributes enables:

* lane-marking rendering and surface-material lookup by (lane, s) without imprinting;
* acoustic tyre/road noise or surface roughness per lane;
* placing objects along the road (signs, poles, barriers, which are acoustic
  reflectors) by (s, t) and regenerating them when the road changes;
* drone flight corridors along roads, defined in (s, t, h) coordinates.

Vertical profile and crossfall are 1D functions of s (OpenDRIVE `elevationProfile`
and `lateralProfile` polynomials). Store them as **1D curve attributes on the road
entity** (a separate `Road` object/domain), not on mesh elements. The road surface's
carrier is then a **procedural sweep surface**:

```
S(s, t) = C(s) + t * N(s) rotated by crossfall(s,t) + z(s) * Up
```

This is a *surface on face* whose definition references the road object.
Tessellation evaluates S at whatever density the requested tolerance needs.

## 6. Surface-on-face carriers beyond NURBS

Carrier surface kinds for an environment kernel, all stored in per-type arenas
(§3.2):

| Kind | Use | Evaluate | Intersect / closest-point |
|---|---|---|---|
| Plane | buildings, most man-made | trivial | trivial |
| Quadrics (cylinder, cone, sphere, torus) | tanks, pipes, domes | closed form | closed form / low-degree solve |
| B-spline / NURBS | free-form | de Boor | Bézier clipping, subdivision + Newton |
| **Heightfield / raster** (DEM tile, bilinear or bicubic) | terrain | texel lookup | 2D DDA / max-mipmap traversal (see [spatial-acceleration-structures.md](spatial-acceleration-structures.md)) |
| **Sweep / procedural** (road surface S(s,t), extrusions, lofts) | roads, kerbs, walls along paths | via defining curves | tessellate-and-refine |
| Subdivision surface | organic shapes, rocks | OpenSubdiv | via tessellation |
| Triangulated (TIN) | imported surveys, photogrammetry | barycentric | triangle BVH |
| Implicit/SDF reference | vegetation volumes, smooth blends | field eval | sphere tracing / Harnack tracing |

Raster-backed carrier surfaces matter for scale. A 10 km × 10 km terrain at 0.5 m is
4×10⁸ samples. As a raster it is ~1.6 GB of `f32`; as a triangle mesh with explicit
topology it is several times larger. A terrain face whose carrier is "DEM tile k,
window [i0,i1]×[j0,j1]" keeps B-rep topology tiny while queries run directly on the
raster.

## 7. Procedural references as attributes

Some elements should store **how to make** their geometry rather than the geometry:

* **Vegetation instances:** `(species, age, seed, health)` plus a transform. Mesh,
  billboard, volumetric density proxy or acoustic scattering proxy are generated per
  requested LOD and representation.
* **Building components:** "window type W3, 1.2 × 1.5 m" placed on a facade face at
  (u, v). Instanced geometry at high LOD, a texture/material change at low LOD.
* **Infrastructure:** "lattice pylon type P, height 40 m": a wireframe of beams at
  high LOD, an equivalent scattering cylinder at low LOD.

Store this as a `GeneratorRef = (generator id, version, parameter blob hash,
parameter row)`, where the parameter row lives in a typed per-generator attribute
table. Determinism matters: generators must be pure functions of
(parameters, seed, requested LOD), so that any process (simulation node, renderer,
tile server) regenerates bit-identical geometry. This replaces storing geometry for
billions of trees.

The instance domain (Houdini packed primitives, Blender instances, USD
PointInstancer) is the natural home: per-instance attributes plus a prototype
reference that can be either concrete geometry or a `GeneratorRef`. It maps directly
onto TLAS/BLAS acceleration (see
[spatial-acceleration-structures.md](spatial-acceleration-structures.md)).

## 8. References between elements and objects

Attributes that *point* to other things need typed reference kinds with clear
lifetime semantics:

| Reference kind | Target | Storage | On target deletion |
|---|---|---|---|
| `ElemRef<D>` (same body) | element of domain D | generation-checked handle | becomes null (checked) |
| `PersistentRef` | element by persistent ID, possibly in another body, LOD or revision | 64/128-bit ID | resolves lazily, may fail, can re-resolve |
| `ObjectRef` | another body/object/asset | asset ID / URI | dangling-tolerant |
| `GeomRef` | geometry arena entry | tag + index | refcounted, cannot dangle |
| `MaterialRef` / `EnumRef` | row in a table | dictionary index | table is versioned with the schema |
| `FieldRef` | VDB/raster/analytic field | asset ID + grid name | dangling-tolerant |

Rules:

* Within a single mutable body, use handles (fast). Across bodies, LODs or
  revisions, use persistent IDs (robust).
* Never store raw indices across an operation that may compact.
* References are attributes like any other, with propagation policies. On split, a
  "door → room it opens into" reference copies. On merge it unions.

**Region adjacency** ("which room is next to which") does not need stored references.
It is derived from the non-manifold topology: two regions are adjacent iff they share
a face (as opposite face-sides). Store derived adjacency graphs as cached computed
attributes, invalidated on topology change.

## 9. Interaction with tessellation and queries

* **Tessellation** is a function `(body, tolerance, options) → mesh + attribute
  transfer map`. The output mesh's vertices carry evaluated field attributes (§5,
  [attribute-propagation.md](attribute-propagation.md) §5) and back-references to the
  B-rep elements they came from (`src.face`, `src.edge`, persistent IDs). The query
  layer always reports *B-rep* element IDs, even when it intersects tessellated
  geometry.
* **Embedded curves in queries.** "Distance from point to nearest road centreline" is
  a closest-point query against a curve set, independent of the surface mesh. Curve
  sets get their own BVH of curve-segment bounding boxes (or of the approximants'
  Bézier spans, whose control polygons bound the curve via the convex-hull property).
* **Certified bounds.** Since approximants carry `max_error`, a query on the
  approximant can return an interval-valued result: distance ± ε. A robust kernel
  should propagate this.

## 10. Summary

1. Make geometry binding (point, curve, pcurve, surface) **reserved attributes** in
   the general system, so meshes and exact B-reps share one structure.
2. Store geometry objects **in immutable per-type SoA arenas**, referenced by
   tag+index. Avoid polymorphic heap objects.
3. Keep **exact defining curves alongside certified approximants**, with explicit
   reparameterisation maps (arc length).
4. Support **embedded curve sets on faces** with lazy imprinting. Breaklines are
   resolved at tessellation time via CDT.
5. Represent roads as **curvilinear coordinate systems** (OpenDRIVE-style (s, t))
   with sweep-surface carriers, and emit (s, t, lane) as tessellation attributes.
6. Include **raster/heightfield** and **procedural sweep** carriers as first-class
   surface types.
7. Use **GeneratorRefs** for vegetation, building components and infrastructure, with
   deterministic regeneration per LOD.
8. Use a small set of **typed reference kinds** with defined deletion and propagation
   semantics.

## References

* OCCT, Modeling Data user guide: https://dev.opencascade.org/doc/overview/html/occt_user_guides__modeling_data.html
* OCCT, BRep format specification: https://dev.opencascade.org/doc/overview/html/specification__brep_format.html
* Levien, *From Spiral to Spline: Optimal Techniques in Interactive Curve Design*, PhD thesis 2009: https://www2.eecs.berkeley.edu/Pubs/TechRpts/2009/EECS-2009-162.html
* ASAM OpenDRIVE specification: https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/latest/specification/index.html
* Shewchuk, *Triangle* (constrained Delaunay): https://www.cs.cmu.edu/~quake/triangle.html
* CGAL 2D Triangulations (constrained): https://doc.cgal.org/latest/Triangulation_2/index.html
* OpenSubdiv: https://graphics.pixar.com/opensubdiv/docs/intro.html
* Embree user geometry API: https://www.embree.org/api.html
* NVIDIA OptiX programming guide (custom primitives, SBT): https://raytracing-docs.nvidia.com/optix8/guide/index.html
* OpenUSD PointInstancer: https://openusd.org/release/api/class_usd_geom_point_instancer.html
