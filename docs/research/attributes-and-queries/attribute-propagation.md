# Attribute Propagation Through Topology Edits

Part of [attributes-and-queries](README.md). This file covers what happens to
attribute values, element identity and provenance when topology changes through
split, merge, Boolean, subdivision, simplification and procedural regeneration.

## 1. The problem

Every topological operator creates, destroys or re-parents elements. For each
attribute on each affected element the system must decide:

1. **Value.** What is the new element's value: copy, interpolate, recompute, or
   mark as unknown?
2. **Identity.** Is the new element "the same" as an old one (keeps its persistent
   ID), a child of one (derived ID), or new?
3. **Provenance.** Which source elements and which operation produced it?

Most systems answer only (1) and only per *type*. CAD kernels that let users
reference faces ("fillet this edge", "put this road on that face") eventually
discovered they also need (2) and (3). FreeCAD's topological naming problem is the
best-known failure: features referenced faces by index (`Face6`), and indices changed
when upstream parameters changed. The fix, ported from the RealThunder branch into
FreeCAD 1.0 (2024), is an element-mapping/history scheme that generates names from
operation history
([FreeCAD wiki, Topological naming problem](https://wiki.freecad.org/Topological_naming_problem);
[FreeCAD 1.0 release notes](https://wiki.freecad.org/Release_notes_1.0)).

For UberTopo the same issue covers more than modelling. A sensor is placed "on the
north facade of building B17". An acoustic measurement is associated with "the face
whose material is wet asphalt at lane 2, station 120 m". A survey finding says "this
window is a drone entry point". All of these references must survive regeneration,
re-tessellation and LOD changes.

## 2. Taxonomy of per-attribute propagation policies

The prior art agrees that **behaviour per attribute class** is the right
granularity:

* ACIS `ATTRIB` subclasses override `split_owner`, `merge_owner`, `copy_owner`,
  `trans_owner`, `replace_owner` and `lose`
  ([Spatial ACIS attribute docs](https://doc.spatial.com/get_doc_page/articles/a/t/t/Attributes_d0ce.html)).
* Parasolid attribute definitions declare a class (1–7) with fixed behaviour on
  split/merge/transform. Typical choices: *transform with owner*, *delete on
  split/merge*, *copy to both pieces*.
* BMesh `CustomData` gives each layer *type* an `interp(sources[], weights[], n, dest)`
  callback used by every Euler operator
  ([Blender `customdata.cc`](https://projects.blender.org/blender/blender/src/branch/main/source/blender/blenkernel/intern/customdata.cc)).
* Blender geometry nodes use type-driven implicit domain interpolation
  ([Blender manual](https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/attributes_reference.html)).
  Houdini SOPs each document their own attribute behaviour, usually with options such
  as "Keep attributes" or "Attribute promotion method: average / min / max / mode /
  first / last / sum".

Synthesising these, an attribute schema needs these orthogonal policy fields:

| Policy field | Choices | Example attribute |
|---|---|---|
| **interp** (continuous blend from N weighted sources) | `linear`, `normalized-linear` (unit vectors), `slerp` (quaternions), `log-linear` (positive scales), `nearest` (largest weight), `categorical-mode`, `none` | position: linear; normal: normalized; material: nearest |
| **on_split** (one element becomes k) | `copy`, `interp-at-split-point`, `divide-extensive` (split areas/masses proportionally), `recompute`, `clear` | material: copy; `lane_station` on vertices: interp; "area" if stored: divide |
| **on_merge** (k elements become one) | `require-equal` (else conflict), `first`, `area-weighted-mean`, `sum-extensive`, `mode`, `union` (set-valued), `mark-mixed`, `clear` | material: require-equal or mark-mixed; class labels: union; absorption override: area-weighted |
| **on_transform** | role-driven: `point`, `vector`, `normal`, `covector`, `frame`, `none` | normals: inverse transpose; wind vector: vector |
| **on_mirror / orientation flip** | `none`, `negate`, `swap-sides` | face-side materials swap when a face is reversed |
| **on_copy / instance** | `copy`, `share`, `new-id` | persistent IDs: new derived ID |
| **on_lod** (simplification/aggregation) | as `on_merge`, plus `preserve-boundary` (forbid collapses across value changes) | semantic class, material |
| **validity** | `geometry-dependent` (invalidate on any geometry edit), `topology-dependent`, `independent` | cached curvature: geometry-dependent |

Extensive and intensive quantities need different treatment. **Extensive** quantities
(area, mass, count, absorption *area* in sabins) add on merge and divide on split.
**Intensive** quantities (absorption *coefficient*, temperature, density) are averaged
with the right weight: area for faces, length for edges, volume for regions. Getting
this wrong is a silent physics bug. Absorption coefficients merged by plain mean
instead of area-weighted mean give wrong reverberation times. The schema should record
`extensive | intensive | categorical | identity | derived` and pick sensible defaults
for the policies from that.

## 3. Operation-by-operation rules

Domains below use the B-rep vocabulary: vertex, edge, coedge (edge-use), loop, face,
face-side, region. See [recommendations.md](recommendations.md).

### 3.1 Edge split (insert vertex at parameter t)

* New vertex: continuous vertex attributes are interpolated from the edge endpoints
  with weights (1−t, t). The geometric position is *not* interpolated but evaluated
  on the edge's carrier curve (a B-rep edge is curved). Attributes defined in curve
  parameter space (road station s) must be evaluated from the embedded curve, not
  linearly interpolated: station along a clothoid is arc length, and linear
  interpolation of endpoint stations is exact only if the edge was parameterised by
  arc length. See [embedded-geometry-attributes.md](embedded-geometry-attributes.md).
* Two child edges: copy categorical attributes. Divide extensive ones by length.
  Child edges get derived persistent IDs (`parent_id ⊕ {0,1}`).
* Coedges: each coedge splits too. Face-corner attributes (UV, per-corner normal)
  at the new corner are interpolated *within each face separately*, which preserves
  UV seams.
* Carrier curve: shared (same curve, restricted parameter range). No copy.

### 3.2 Face split (by a new edge, or by imprinting a curve)

* Child faces copy all categorical and intensive attributes. Extensive ones divide by
  area.
* Face-sides copy their side-specific attributes (material per side).
* The new edge gets attributes from its *origin*. For a split by an imprinted road
  edge, the new edge carries the road's attributes (curve reference, road ID, station
  range), not interpolated face data. This is the most important case for road
  tessellation and is why embedded curves need to carry their own attribute payload.
* Persistent IDs: if the split was caused by imprinting entity *X*, children are
  named `(parent, X, side)`, with side determined by the curve's left/right. This is
  stable under regeneration as long as *X* is stable. It is the core of history-based
  topological naming.

### 3.3 Face/edge merge (dissolve, coplanar merge, simplification)

* Categorical: `require-equal` is the safe default. A merge across a material
  boundary is either forbidden (simplifier constraint) or produces `mixed` with a
  provenance set.
* Intensive: area-weighted mean.
* Set-valued (feature IDs, class labels): union. Keep a dominant one for display.
* Persistent ID: the merged element takes the ID of the dominant (largest) parent and
  records the others in its provenance. Never invent a new ID when one parent clearly
  dominates, so that references survive.

### 3.4 Booleans (union, difference, intersection, imprint, non-manifold merge)

Booleans are the hardest case. In a non-manifold B-rep a "Boolean" is usually
implemented as **general merge** (compute the arrangement of all operands into a
cellular complex) followed by **selection** (keep the cells satisfying the predicate).
Radial-edge / SGC-style systems take this approach
([Weiler 1986; Rossignac & O'Connor 1989, *SGC*](https://doi.org/10.1016/B978-0-444-87423-5.50021-8)).
The approach is good for attributes:

* Every output face is a sub-face of exactly one input face (or of a coplanar overlap
  of several). Its attributes come from that source face (`origin = (operand, source
  face ID)`). Faces from the subtracted operand of a difference are reversed, so their
  face-sides swap (`on_mirror = swap-sides`).
* **Coplanar overlaps** (two walls touching) need a rule: the left operand wins, a
  priority attribute decides, or both are kept as a set. For a non-manifold model of a
  building, a shared interior wall naturally has two sides with different materials,
  so the radial structure already holds both. The merge result is one face with two
  face-sides, each inheriting from its own operand. This is a strong argument for a
  face-side domain.
* New edges (intersection curves) get `origin = (face_a_id, face_b_id)`. Their
  persistent name is the pair, which is stable.
* Regions (3-cells) inherit region attributes from the operand(s) containing them. A
  region inside two operands needs a combination rule (e.g. medium priority).

Mesh Booleans (exact/robust: Cherchi et al. 2020/2022, Zhou et al. 2016 *Mesh
arrangements*, Blender's exact Boolean, CGAL corefinement) all compute "which input
triangle does this output triangle come from". Exposing that map is enough to
implement the rules above
([Zhou, Grinspun, Zorin, Jacobson 2016, *Mesh Arrangements for Solid Geometry*](https://www.cs.columbia.edu/cg/mesh-arrangements/);
[Cherchi et al. 2022, *Interactive and Robust Mesh Booleans*](https://arxiv.org/abs/2205.14151);
[CGAL Polygon Mesh Processing corefinement visitor](https://doc.cgal.org/latest/Polygon_mesh_processing/index.html#title14)).
CGAL's corefinement **visitor** (callbacks `before_subface_creations`,
`after_subface_created`, `after_face_copy`, `new_vertex_added`, ...) is a good API
pattern for this: the kernel announces topological events, and the attribute layer
listens.

### 3.5 Subdivision and refinement

* Vertex attributes follow the subdivision stencils (Catmull–Clark, Loop) when they
  are `vertex`-interpolated in USD terms, or bilinear (`varying`).
* Face-varying attributes (UVs) need boundary/corner rules. OpenSubdiv's
  `FVarLinearInterpolation` modes (`none`, `corners only`, `corners plus1/2`,
  `boundaries`, `all`) are the established vocabulary
  ([OpenSubdiv docs](https://graphics.pixar.com/opensubdiv/docs/subdivision_surfaces.html#face-varying-interpolation-rules)).
* Categorical face attributes copy to all child faces.
* For a B-rep face that is re-tessellated (not subdivided), per-vertex values of the
  tessellation are **evaluated** from the face's parametric attribute fields (§5),
  not interpolated from coarse data.

### 3.6 Simplification (edge collapse, vertex clustering)

* The collapse target position comes from the QEM optimum. Attributes come either
  from the same optimisation (attribute-augmented quadrics: Garland & Heckbert 1998,
  Hoppe 1999) or by barycentric projection onto the original surface
  ([Hoppe 1999](https://hhoppe.com/proj/newqem/)).
* Categorical boundaries become constraints: no collapse that moves a vertex off a
  material/semantic boundary curve. meshoptimizer exposes this as border locking and a
  per-vertex lock array ([meshoptimizer](https://github.com/zeux/meshoptimizer)).
* Details are in [lod-and-simplification.md](lod-and-simplification.md).

### 3.7 Procedural regeneration

Regenerating a building after a parameter change replaces its whole subgraph.
Attributes that were **authored after generation**, such as a hand-placed sensor or a
survey annotation on a face, must be re-attached. Options:

1. **History-based naming** (FreeCAD 1.0 element maps, Onshape's internal naming,
   Kripac 1997 *A mechanism for persistently naming topological entities*,
   [doi:10.1016/S0010-4485(96)00057-0](https://doi.org/10.1016/S0010-4485(96)00057-0)).
   Names are derived from the generating operation and its inputs, so a regenerated
   face gets the same name if it is generated "the same way".
2. **Semantic naming.** The generator emits semantic keys explicitly ("facade N,
   floor 3, window 2"). Procedural systems can do this far more reliably than
   feature-based CAD, because the generator knows the *meaning* of what it emits.
   CGA-style shape grammars (CityEngine) naturally produce hierarchical shape names
   ([Müller et al. 2006, *Procedural Modeling of Buildings*](https://doi.org/10.1145/1141911.1141931)).
3. **Geometric re-matching** as a fallback: closest face with the same class, same
   normal within tolerance, overlapping footprint. It is fragile but needed for
   imported data with no history.

**Recommendation:** generators must emit **semantic persistent keys** (option 2) as
a required output, with history-based names (option 1) derived automatically for
elements created by generic operators such as Booleans and splits. Geometric
re-matching (option 3) is only a repair tool. See
[recommendations.md](recommendations.md) for the ID scheme.

## 4. Provenance as a first-class attribute

Provenance should be maintained automatically by the kernel, not by each operator
author:

```
proc.origin   : (op_id: u32, source: PersistentId)   # dominant source
proc.sources  : small-set<PersistentId>              # all contributing sources (merge/LOD)
proc.gen      : GeneratorRef                         # generator node + parameter hash
```

Uses:

* **Debugging and verification.** Answers "why does this face have this material?".
* **Cross-LOD identity.** A simplified LOD2 face's `proc.sources` lists the LOD3 faces
  it represents. Queries at coarse LOD can report fine-LOD semantic IDs (see
  [lod-and-simplification.md](lod-and-simplification.md)).
* **Incremental recompute.** Changing generator *G* invalidates exactly the elements
  with `proc.gen = G`, and dirty tracking flows to acceleration structures.
* **Simulation traceability.** An acoustic reflection path reports the persistent IDs
  of the faces it hit, so results are meaningful across model revisions.

Costs: `proc.sources` can grow without bound during repeated merging. Cap it, or store
it as a reference into a provenance DAG table (an append-only log of operations)
instead of inline sets. The DAG form is better. It is a lineage graph (cf. data
provenance in databases, W3C PROV) that can be pruned or compacted between sessions
([W3C PROV-DM](https://www.w3.org/TR/prov-dm/)).

## 5. Parametric attribute fields on B-rep faces

Triangle-mesh attribute systems interpolate between vertices. A B-rep face with an
analytic or spline carrier surface has no vertices in its interior. Attributes that
vary over a face (wetness, wear, road-marking paint coverage, a temperature map)
need a *field over the face's parameter domain*:

* **Constant per face** (the common case).
* **Parametric field**: a function of (u, v) on the face's surface, such as a
  low-order polynomial, a B-spline scalar field sharing the surface's knot vectors, or
  a texture in (u, v) (like a property texture in `EXT_structural_metadata`).
* **Curvilinear field**: a function of (s, t) relative to an embedded curve, e.g. lane
  index as a function of lateral offset t from the road reference line. This is
  OpenDRIVE's lane model
  ([ASAM OpenDRIVE 1.8](https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/latest/specification/index.html)).
* **Spatial field**: a reference into a VDB or raster in world/tile space.

Tessellation then *evaluates* fields at generated vertices rather than interpolating,
and propagation through splits is trivial: children share the parent's field and
just restrict the domain. This is a significant advantage of keeping exact geometry
plus fields over baking per-vertex values early. Fields compose with the
"computed attribute" mechanism: a field is an attribute whose storage is a function
object, not an array.

## 6. Interaction with copy-on-write

Propagation and COW interact as follows:

* A split that changes 10 faces in a 10M-face mesh should not copy every attribute
  column. Chunked COW (pages of 1–4k elements) bounds the copy to the touched pages.
* Appending new elements (most splits append) should not copy existing pages. Use
  append-only paged storage with a page table that is itself COW.
* Deleting elements creates holes. Either keep tombstones (generation-checked handles
  stay valid and iteration skips dead slots) and compact lazily, or compact eagerly and
  remap. For persistent IDs it does not matter. For handles it does. Lazy compaction
  with a compaction epoch is the usual answer (cf. slot maps / generational arenas).

## 7. Testing propagation

Propagation bugs are silent, so automated invariants are essential:

* **Conservation tests.** The sum of an extensive attribute is unchanged by
  split/merge. The area-weighted integral of an intensive attribute is unchanged by
  splits.
* **Boundary preservation.** After simplification, the set of material-boundary
  curves is within ε of the original (Hausdorff).
* **Round-trip identity.** Regenerate with identical parameters: all persistent IDs
  are identical. Regenerate with a parameter perturbation: IDs of unaffected elements
  are unchanged.
* **Provenance closure.** Every element's `proc.origin` resolves to an existing
  operation in the log.

## References

* FreeCAD, Topological naming problem: https://wiki.freecad.org/Topological_naming_problem
* FreeCAD 1.0 release notes: https://wiki.freecad.org/Release_notes_1.0
* Spatial ACIS attributes: https://doc.spatial.com/get_doc_page/articles/a/t/t/Attributes_d0ce.html (from memory; URL may have moved)
* Blender `customdata.cc`: https://projects.blender.org/blender/blender/src/branch/main/source/blender/blenkernel/intern/customdata.cc
* Blender Manual, Attributes Reference: https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/attributes_reference.html
* Rossignac & O'Connor, *SGC: A dimension-independent model for pointsets with internal structures and incomplete boundaries* (1989): https://doi.org/10.1016/B978-0-444-87423-5.50021-8
* Zhou, Grinspun, Zorin, Jacobson, *Mesh Arrangements for Solid Geometry*, SIGGRAPH 2016: https://www.cs.columbia.edu/cg/mesh-arrangements/
* Cherchi, Pellacini, Attene, Livesu, *Interactive and Robust Mesh Booleans*, SIGGRAPH Asia 2022: https://arxiv.org/abs/2205.14151
* CGAL Polygon Mesh Processing (corefinement, visitors): https://doc.cgal.org/latest/Polygon_mesh_processing/index.html
* OpenSubdiv face-varying interpolation: https://graphics.pixar.com/opensubdiv/docs/subdivision_surfaces.html
* Hoppe, *New Quadric Metric for Simplifying Meshes with Appearance Attributes*, IEEE Vis 1999: https://hhoppe.com/proj/newqem/
* meshoptimizer: https://github.com/zeux/meshoptimizer
* Kripac, *A mechanism for persistently naming topological entities in history-based parametric solid models*, CAD 1997: https://doi.org/10.1016/S0010-4485(96)00057-0
* Müller, Wonka, Haegler, Ulmer, Van Gool, *Procedural Modeling of Buildings*, SIGGRAPH 2006: https://doi.org/10.1145/1141911.1141931
* ASAM OpenDRIVE specification: https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/latest/specification/index.html
* W3C PROV-DM: https://www.w3.org/TR/prov-dm/
