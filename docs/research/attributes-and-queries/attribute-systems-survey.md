# Attribute Systems: A Survey of Existing Designs

Part of [attributes-and-queries](README.md). This file surveys how existing geometry
systems attach data to geometric elements and how they store it. The other files cover
[propagation through edits](attribute-propagation.md),
[embedded curve and surface data](embedded-geometry-attributes.md) and
[the recommended design](recommendations.md).

## 1. Why attributes are central for UberTopo

In a CAD kernel the attribute system is usually an afterthought: OCCT's `TDF`/OCAF label
tree, Parasolid attributes and ACIS `ATTRIB` chains were bolted onto a topology core
designed in the late 80s and early 90s. They were meant for a few tags per face, such as
colour, name or tolerance. UberTopo needs much more:

* **Simulation inputs per element.** Each face needs acoustic absorption and scattering
  per frequency band, optical/IR properties and surface roughness. Each region needs
  medium properties such as air temperature and humidity (which set the speed of
  sound), wind, vegetation density, or water.
* **Semantics.** Classification (CityGML-style `WallSurface`, `RoofSurface`; OSM tags;
  "road lane 2, northbound"), ownership, and hazard or vulnerability labels for site
  surveys.
* **Embedded geometry.** Curves on edges, curves on faces (pcurves), surfaces on faces,
  road reference lines with station/offset coordinates. See
  [embedded-geometry-attributes.md](embedded-geometry-attributes.md).
* **Procedural provenance.** Which generator and parameters produced this element, so
  it can be regenerated at another level of detail (LOD) and named stably.
* **Bulk numeric data.** Per-vertex normals, UVs and per-point lidar intensities, at
  millions to billions of elements.

The systems that scale best to the last item are the ones from DCC tools and
renderers (Houdini, Blender, USD), not the CAD kernels. The useful work is to combine
the DCC-style columnar model with CAD-style topology and B-rep geometry.

## 2. Houdini: point / vertex / primitive / detail

Houdini's geometry (GA library, HDK) defines four **attribute classes**
([SideFX docs, *Geometry attributes*](https://www.sidefx.com/docs/houdini/model/attributes.html);
[HDK geometry intro](https://www.sidefx.com/docs/hdk/_h_d_k__geometry__intro.html)):

| Class | Lives on | Typical use |
|---|---|---|
| point | shared positions | `P`, `v`, `pscale`, `id` |
| vertex | a primitive's reference to a point (a "face corner") | `uv`, split normals |
| primitive | polygon, curve, volume, packed prim, ... | `material`, `name`, `Cd` per face |
| detail | the whole geometry | global metadata |

Key design properties:

* **Columnar storage.** Each attribute is an array indexed by element offset. Pages of
  1024 elements give paged copy-on-write and "constant page" compression: a page whose
  values are all equal is stored once. This is an early and effective form of sparse
  storage.
* **Precedence.** If the same name exists on several classes, the lower level wins
  (vertex > point > primitive > detail). Values are resolved at lookup, not merged.
* **Type info.** Attributes carry a storage type (int, float, string, dict, arrays) and
  a *type info* tag (`point`, `vector`, `normal`, `color`, `quaternion`, `matrix`,
  `texturecoord`). Transforms use the tag: vectors rotate, points translate, normals
  use the inverse transpose. UberTopo needs this too, generalised to units and frames
  (see §10).
* **Intrinsic attributes.** Computed read-only (sometimes writable) pseudo-attributes
  such as `intrinsic:bounds`, `intrinsic:measuredarea` and packed-prim transforms,
  exposed through the same API. This is a useful pattern: derived quantities look like
  attributes but are computed lazily.
* **Dictionaries and arrays.** Since Houdini 18, dict attributes give schemaless
  structured data per element. They are flexible but slow and untyped, and Houdini
  users treat them as an escape hatch.
* **Groups** are boolean attributes, conceptually and largely in implementation.
* **Primitive heterogeneity.** One `GU_Detail` mixes polygons, NURBS, Bezier curves,
  volumes, VDBs, packed primitives (instances) and agents. Primitive attributes span
  all of these. This is close to the "heterogeneous geometry in one container" that
  UberTopo needs.
* **The `id` convention.** Persistent identity across frames is a user-maintained
  integer point attribute, not a system feature. Many Houdini SOPs preserve or generate
  it, but identity is not automatic. This is a weakness to avoid.

Lesson: four domains are not enough for a B-rep. Houdini has no edge domain (edges
are implicit, and edge groups are stored as point pairs), no face-side domain and no
region (volume cell) domain.

## 3. Blender: attribute domains and implicit interpolation

Blender's modern geometry (mesh, curves, point cloud, instances, Grease Pencil v3)
uses a generic attribute API with **domains**
([Blender manual, *Attributes Reference*](https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/attributes_reference.html)):

| Domain | Geometry |
|---|---|
| Point | mesh vertices, curve control points, point cloud points |
| Edge | mesh edges |
| Face | mesh polygons |
| Face Corner | per-face vertex use (UVs, split normals) |
| Spline / Curve | each curve in a `CurvesGeometry` |
| Instance | each instance in an `Instances` component |
| Layer | Grease Pencil layers |

Notable design points:

* **Implicit domain interpolation.** Reading a point attribute in a face context
  averages it automatically, and reading a face attribute on points averages the
  adjacent faces. The manual's example: Position (point) used as a Set Material
  selection (face) is interpolated point→face. Booleans use "any/all" style rules,
  ints and floats average. The rules are per type, not per attribute. This removes a
  lot of boilerplate but silently loses information, because a categorical integer
  such as a material index gets *averaged*. UberTopo should make interpolation a
  property of the attribute's schema, not just its type.
* **Fields.** Geometry Nodes evaluates *fields* (lazy functions of element context)
  and only materialises them into attributes on capture. This separates "attribute
  as stored data" from "attribute as expression". Derived values (normals, area,
  curvature) are fields, which is like Houdini's intrinsics but composable.
* **SoA everywhere.** `Mesh` stores `vert_positions`, `edge_verts`, `corner_verts`,
  `corner_edges` and `face_offsets` as flat arrays. Since 3.x/4.0, Blender has removed
  the old AoS `MVert`/`MPoly`/`MLoop` structs in favour of named generic attributes.
  Even topology is "just attributes" with reserved names (`.edge_verts`,
  `.corner_vert`). Curves use an `offsets` array (CSR layout) so per-curve point
  ranges are implicit.
* **Implicit sharing (copy-on-write).** Since 3.6, attribute arrays are reference
  counted through `ImplicitSharingInfo`. Copying a geometry is O(number of arrays):
  arrays are shared until someone asks for write access, and only then is that array
  copied ([Blender developer docs, *Implicit Sharing*](https://developer.blender.org/docs/features/core/implicit_sharing/);
  [PR #105994](https://projects.blender.org/blender/blender/pulls/105994);
  [3.6 release notes](https://developer.blender.org/docs/release_notes/3.6/nodes_physics/)).
  Conversion nodes became ≥10× faster in some cases. The granularity is a whole
  array: changing one position copies the entire position array. That is fine for
  node evaluation but heavy for interactive local edits on huge meshes. A chunked COW
  would fix it.
* **Bundles (Blender 5.0)** carry structured, heterogeneous values (nested bundles,
  geometry, fields) and can be attached to a geometry as "geometry-wide attributes"
  ([Bundles and Closures, code.blender.org, 2025](https://code.blender.org/2025/08/bundles-and-closures/);
  [Blender 5.0 GN release notes](https://developer.blender.org/docs/release_notes/5.0/geometry_nodes/)).
  These are effectively detail-level compound attributes.

### BMesh CustomData

BMesh (Blender's editable topological mesh with disk and radial cycles) stores
attributes in `CustomData` layers. Each element (`BMVert`, `BMEdge`, `BMLoop`,
`BMFace`) has a `void* data` pointer to a per-element block that packs all layers
(AoS per element), allocated from a mempool. Layers have typed descriptors
(`LayerTypeInfo`) with callbacks: `copy`, `free`, `interp` (weighted interpolation
from N sources), `swap`, `set_default`, `equal`, `copyminmax`. Euler operators call
`CustomData_bmesh_interp` when they split or join elements
([Blender source, `BKE_customdata.hh`/`customdata.cc`](https://projects.blender.org/blender/blender/src/branch/main/source/blender/blenkernel/intern/customdata.cc)).

Strengths: the interpolation callback per layer type is the right abstraction for
propagation through topology edits, and BMesh "loops" (face corners) carry UVs
correctly across splits. Weaknesses: AoS blocks per element mean adding a layer
reallocates every element block. They are cache-hostile for bulk numeric work. And
the callbacks are per *type*, not per *semantic attribute*. Blender itself converts
BMesh ↔ Mesh (SoA) at edit-mode boundaries, which shows the AoS design does not scale
to the evaluated pipeline.

## 4. OpenMesh properties

OpenMesh (RWTH Aachen) attaches **properties** to vertices, halfedges, edges, faces and
the mesh. Each property is a `PropertyT<T>` holding a `std::vector<T>` (SoA). It is
added by handle (`VPropHandleT<T>`) or by name. The newer `PropertyManager` /
`makeTemporaryProperty` API gives RAII scoping for temporary per-algorithm data
([OpenMesh docs](https://graphics.rwth-aachen.de/media/openmesh_static/Daily-Builds/Doc/a06330.html);
[tutorial 03](https://sources.debian.org/src/openmesh/11.0.0-2/Doc/tutorial_03.docu)).

* Having **halfedge properties** separate from edge properties is the right idea for
  B-reps: per-side edge data (a coedge's pcurve, "which face is to my left") belongs
  on the halfedge.
* Garbage collection compacts all property vectors when elements are deleted, which
  invalidates handles. Handles are plain indices with no generation counters, so stale
  handles fail silently. UberTopo should use generation-checked handles.
* There are no interpolation semantics. Algorithms such as the decimater handle
  specific properties ad hoc.

## 5. CGAL property maps

CGAL abstracts attribute access with the Boost Property Map concept: a key→value map
that algorithms take as a *named parameter* (`vertex_point_map`, `face_index_map`, ...).
`Surface_mesh` stores dynamic properties via
`add_property_map<Index, T>(name, default)`, which returns `std::optional` in CGAL 6.0
and `std::pair<map,bool>` in 5.x
([CGAL Surface_mesh property example](https://doc.cgal.org/5.5/Surface_mesh/Surface_mesh_2sm_properties_8cpp-example.html);
[dynamic properties example](https://doc.cgal.org/5.0/BGL/Property_map_2dynamic_properties_8cpp-example.html)).

The strength is decoupling: algorithms do not care whether a value is stored, computed
or borrowed from another structure, so a property map can be a lambda. This is the
C++-generic equivalent of Blender fields and Houdini intrinsics, and UberTopo's query
and algorithm layer should use the same idea: algorithms consume *accessors*, not
storage. The weakness is that the template-heavy API gives poor compile times and no
runtime introspection. A procedural node graph needs runtime-typed attributes.

## 6. OpenUSD primvars

USD separates **primvars** (geometric attributes that interpolate over a surface) from
ordinary attributes. Each primvar has an **interpolation** token
([USD `UsdGeomPrimvar` docs](https://openusd.org/release/api/class_usd_geom_primvar.html);
[schema text via Houdini `usd_primvarinterpolation`](https://www.sidefx.com/docs/houdini/vex/functions/usd_primvarinterpolation.html);
[Katana UsdPrimvarSet](https://learn.foundry.com/katana/Content/rg/usd_nodes/usdprimvarset.html)):

| Interpolation | Count on a mesh | Meaning |
|---|---|---|
| `constant` | 1 | one value for the whole prim |
| `uniform` | #faces | one value per face (per patch) |
| `varying` | #points | per point, bilinear interpolation (ignores subdivision basis) |
| `vertex` | #points | per point, interpolated with the surface basis (subdiv/NURBS) |
| `faceVarying` | #face-vertices | per face corner, allows seams |

Further properties:

* **Indexed primvars.** A primvar may carry an `indices` array, so values are stored
  once and referenced. This is dictionary encoding, ideal for categorical attributes
  such as material names and class labels.
* **`elementSize`** lets a primvar hold N values per element, e.g. an 8-band
  absorption spectrum stored as `float[]` with `elementSize = 8`.
* **Role types** (`point3f`, `normal3f`, `vector3f`, `color3f`, `texCoord2f`) play the
  same role as Houdini type info.
* **Subdivision-aware.** `varying` vs `vertex` distinguishes "linear across the cage"
  from "follows the limit surface". `faceVaryingLinearInterpolation` (from OpenSubdiv)
  controls how face-varying data behaves at boundaries
  ([OpenSubdiv fvar interpolation](https://graphics.pixar.com/opensubdiv/docs/subdivision_surfaces.html)).
* **Composition and schemas.** USD's strengths are layering, overrides and applied API
  schemas (e.g. `UsdPhysicsMaterialAPI`), a model for attaching *typed semantic
  bundles* to prims. Its weakness for UberTopo is that primvars exist only per prim for
  render geometry. USD has no topology-edit semantics or B-rep, and it does not handle
  billions of fine-grained mutating elements.

USD is a reasonable **interchange and scene-assembly** target. For instancing at scale
its `PointInstancer` model (per-instance primvars, prototypes) maps directly onto the
TLAS/BLAS split in [spatial-acceleration-structures.md](spatial-acceleration-structures.md).

## 7. OpenVDB grids

OpenVDB stores sparse volumetric data in a shallow B+-tree-like hierarchy (root hash
map → 32³ internal → 16³ internal → 8³ leaf) with **tile values** for uniform regions
and bitmasks for active voxels
([Museth 2013, *VDB*, ACM TOG](https://www.museth.org/Ken/Publications_files/Museth_TOG13.pdf);
[OpenVDB docs](https://www.openvdb.org/documentation/doxygen/)). As an attribute system:

* A **grid is an attribute over space** (a volumetric domain), typed (`FloatGrid`,
  `Vec3SGrid`, `PointDataGrid`), with metadata (name, class: level set / fog volume /
  staggered, transform, units by convention).
* **PointDataGrid** stores point attributes in voxel-local SoA arrays, which gives
  spatially coherent point clouds with arbitrary attributes. It is a good model for
  lidar and vegetation-instance point sets.
* **NanoVDB** is a linearised, read-only, GPU-friendly version
  ([NanoVDB](https://developer.nvidia.com/nanovdb)).

For UberTopo, VDB-like grids are the natural storage for **region/volume attributes
that vary in space**: temperature and wind fields (acoustic refraction), vegetation
density and canopy attenuation, fog/smoke for optical sensors, SDFs for distance
queries. A region in the B-rep can *reference* a grid as its medium-property field.

## 8. Other notable systems (brief)

* **OCCT OCAF / XDE.** A label tree (`TDF_Label`) with attributes keyed by GUID, plus
  undo/redo transactions. It is associative only by naming (`TNaming`), which is the
  origin of FreeCAD's long-running *topological naming problem*: element indices
  change after recompute, so references break
  ([FreeCAD wiki, Topological naming problem](https://wiki.freecad.org/Topological_naming_problem)).
  Lesson: persistent identity must be designed in from the start.
* **Parasolid / ACIS attributes** carry owner-type rules and per-attribute-class
  behaviour on split, merge, copy and transform (e.g. "on split: copy to both"; "on
  merge: keep if equal, else delete"). These **attribute class callbacks** are the
  most transferable idea from 90s CAD kernels. ACIS `ATTRIB` has virtual `split_owner`,
  `merge_owner`, `copy_owner` and `trans_owner` hooks
  ([ACIS attribute docs, Spatial](https://doc.spatial.com/get_doc_page/articles/a/t/t/Attributes_d0ce.html)).
* **CityGML / CityJSON.** Semantic surfaces (`WallSurface`, `RoofSurface`,
  `GroundSurface`, ...) attached per boundary surface, generic attributes per city
  object. CityGML 3.0 rebuilt its core around *Space* and *Space Boundary* and
  revised the LOD concept, dropping LOD4 as a separate level by letting interiors
  exist at LOD0–3
  ([OGC CityGML 3.0 Conceptual Model](https://docs.ogc.org/is/20-010/20-010.html);
  [Kutzner et al. 2020, *CityGML 3.0: New functions open up new applications*](https://link.springer.com/10.1007/s41064-020-00095-z);
  [CityJSON spec](https://www.cityjson.org/specs/)). This matches the B-rep picture
  closely: spaces = regions (3-cells), space boundaries = faces with sides. CityJSON's
  "semantics" per surface, stored as a values array indexing a dictionary of semantic
  objects, is indexed-primvar encoding again.
* **3D Tiles 1.1 / glTF `EXT_mesh_features` + `EXT_structural_metadata`.** Feature IDs
  per vertex, per texel or implicit, plus *property tables* (SoA, typed via a schema
  with classes, enums, arrays, normalized ints, units-by-semantic) and *property
  textures*. They replace the 3D Tiles 1.0 batch table
  ([Cesium blog, fine-grained metadata](https://cesium.com/blog/2022/05/31/fine-grained-metadata-in-3d-tiles-next);
  [EXT_structural_metadata spec](https://github.com/CesiumGS/glTF/tree/3d-tiles-next/extensions/2.0/Vendor/EXT_structural_metadata);
  [OGC 3D Tiles 1.1](https://docs.ogc.org/cs/22-025r4/22-025r4.html)). This is the best
  current *schema* model for streamed semantic attributes: explicit classes, typed
  properties, enums, `noData`, offset/scale, and separation of feature identity from
  feature properties.
* **Apache Arrow.** Not geometry-specific, but it is the de facto standard columnar
  in-memory format, with typed columns, validity bitmaps (sparse/nullable),
  dictionary encoding, list/struct/union types, zero-copy sharing and a C data
  interface ([Arrow columnar format](https://arrow.apache.org/docs/format/Columnar.html)).
  GeoArrow builds geometry on it
  ([GeoArrow](https://geoarrow.org/)). Making UberTopo's column layout Arrow-compatible
  at the boundary would give Python/pandas/DuckDB analytics for free, which helps for
  survey reports.

## 9. Storage design space

| Axis | Options | Notes |
|---|---|---|
| Layout | AoS per element (BMesh), SoA columns (Blender Mesh, Houdini, OpenMesh, Arrow) | SoA wins for bulk ops, SIMD, GPU upload and adding/removing attributes. AoS wins only for tiny local edits on few elements. |
| Typing | static templates (CGAL), runtime-typed columns (Houdini, Blender), schemaless dicts | A procedural graph needs runtime typing with a closed set of POD element types plus a few structured kinds. |
| Density | dense, paged with constant pages (Houdini), validity bitmap (Arrow), hash map (sparse) | Most semantic attributes are piecewise-constant over large element ranges. Paged constant compression or run-length is the sweet spot. |
| Sharing | deep copy, whole-array COW (Blender), paged COW (Houdini), persistent data structures | Procedural pipelines copy geometry constantly, so COW is mandatory. Paged/chunked COW suits local edits on big data. |
| Categorical | inline strings, indexed/dictionary (USD indices, Arrow dictionary, CityJSON semantics) | Use dictionaries for labels and material references. |
| Identity | indices (OpenMesh), indices + user `id` (Houdini), persistent naming (OCAF) | UberTopo needs generation-checked handles internally plus persistent IDs for cross-edit and cross-LOD identity. |

### Sparse attributes in practice

Three distinct "sparse" cases need different encodings:

1. **Rare attribute** (5 of 10⁶ faces have `door_id`). Use a sorted (element id →
   value) map or a validity bitmap plus a compact value array.
2. **Piecewise-constant attribute** (all faces of a wall share a material). Use paged
   constant compression or run-length encoding over element order. Spatially coherent
   element ordering (Morton/Hilbert, or generation order) makes runs long.
3. **Default-valued attribute** (most faces use the default). Store a default in the
   schema and materialise lazily.

Hash-map sparse storage is slow for bulk iteration. A two-level structure (pages,
each either *absent / constant / dense*) covers all three cases with one code path. It
is essentially Houdini's page design plus an "absent" state, and the same idea as a
VDB leaf with tiles.

## 10. Units, frames and type roles

Attribute values are rarely dimensionless. Mistakes from mixing units and frames are a
known failure mode in simulation tools (the Mars Climate Orbiter is the canonical
example). Relevant designs:

* **Role tags** (Houdini type info, USD roles) tell transforms how to act: `point`
  (affine), `vector` (linear), `normal` (inverse transpose), `covector`, `quaternion`,
  `bivector`, `none`.
* **Units.** glTF metadata and 3D Tiles allow `semantic` strings but no units system.
  CityGML uses `gml:MeasureType` with a `uom` attribute. In C++, `mp-units` (proposed
  for standardisation) provides compile-time quantity types
  ([mp-units](https://mpusz.github.io/mp-units/)). For a runtime-typed system,
  storing a unit in the **schema** of each attribute (not per value) costs nothing and
  catches errors at graph-construction time.
* **Frames.** Positions in a large world need an explicit frame (ECEF, local ENU of
  tile *k*, object-local). See
  [large-world-coordinates-and-tiling.md](large-world-coordinates-and-tiling.md).
  Directional attributes (normals, wind vectors) must name their frame too, or at
  least inherit their geometry's.

## 11. Schema and versioning

Long-lived simulation datasets (site surveys, test-range models) will outlive code
versions. Prior art:

* **USD schemas** are versioned by name (`...API_1`, family/version in newer USD) and
  apply to prims as API schemas.
* **3D Tiles metadata schemas** have `id`, `version`, `classes` and `enums`, are
  referenced by URI and embedded or external.
* **Protobuf/FlatBuffers/Cap'n Proto** give forward/backward compatible evolution
  rules: field numbers, optional fields, never reuse IDs.
* **CityGML ADEs** (Application Domain Extensions) extend the base semantic model with
  domain-specific attributes (e.g. the Energy ADE), which is a namespaced extension
  mechanism.

The takeaway is to use **namespaced attribute names** (`acoustic.absorption`,
`sem.citygml.class`, `proc.origin`), a **registered schema** per name (type, element
shape, unit, role, interpolation policy, default, version), and **migration
functions** registered per version step, so files from older versions load.

## 12. Semantic attributes for drone-defense simulation

Concrete attribute sets that will be needed. Detailed simulation requirements belong
to another research topic; listed here are the storage implications.

**Acoustic (per face-side, usually via material reference):**

* Random-incidence absorption coefficient α per octave band (typically 63 Hz–8 kHz,
  8 bands, or 1/3-octave with ~24–31 bands). Measured per ISO 354. Datasets include
  the legacy **PTB absorption database**, >2000 materials
  ([PTB](https://www.ptb.de/cms/en/ptb/fachabteilungen/abt1/fb-16/ag-163/faq.html)),
  and the open **openMat** format
  ([Schröder et al. 2013](https://infoscience.epfl.ch/record/188603/files/Schroder_et_al_openMat_DAGA2013.pdf)).
* Scattering coefficient per band (ISO 17497-1) and diffusion.
* Transmission loss / sound reduction index per band for walls, glazing and foliage
  layers.
* For wave-based solvers: complex surface impedance Z(f), or a reflection-coefficient
  model. That is a *function-valued* attribute, best stored as a reference to a
  material record holding a parametric model (e.g. Delany–Bazley/Miki for porous
  ground) rather than per-element samples
  (Delany & Bazley 1970, *Applied Acoustics* 3(2), [doi:10.1016/0003-682X(70)90031-9](https://doi.org/10.1016/0003-682X(70)90031-9)).
  Ground impedance matters a lot for outdoor propagation of drone noise.
* Edge attributes: dihedral angle (derived), "diffracting edge" flag, wedge material.

**Optical / IR / RF:**

* Albedo/BRDF (material reference), roughness, emissivity per IR band (MWIR/LWIR),
  thermal mass/conductivity for thermal simulation, and possibly RF permittivity if
  radar is in scope.

**Classification and survey:**

* Class labels (CityGML class, OSM tags, land cover), building usage, height
  above ground, ownership/jurisdiction, "line-of-sight sensitive asset", hardening
  level, access points.

Storage implication: **per-element numeric spectra should almost always be indirect**.
A face-side stores a `MaterialRef` (dictionary-encoded), and the spectra live in a
material table, itself a columnar table with one row per material. Per-element spectra
appear only for *measured* or *spatially varying* data (wet vs dry ground patches,
weathering). The schema must support both, e.g. `acoustic.absorption` resolved by a
lookup rule: element override → material → default.

## 13. Summary of lessons

1. Use SoA columns with runtime types, chunked/paged so that COW and constant
   compression work at page granularity (Houdini pages + Blender implicit sharing).
2. Need more domains than DCC tools: the B-rep's vertex, edge, coedge (edge-use),
   loop, face, **face-side**, **region**, shell/body, plus instance and global. Curves
   and point sets need their own domains.
3. Interpolation and propagation behaviour belong to the *attribute schema* (as in
   ACIS attribute classes and BMesh `interp` callbacks), not to the value type
   (Blender's implicit conversion).
4. Derived values (normals, area, dihedral angle, curvature) are *computed attributes*
   (fields, intrinsics, property maps): cached and invalidated by topology/geometry
   version counters, never stored as truth.
5. Categorical data uses dictionary encoding. Spectral material data uses indirection
   through material tables.
6. Persistent identity must be built in from the start (FreeCAD's naming problem shows
   the cost of retrofitting it).
7. Schemas are namespaced, versioned, unit- and role-annotated, and modelled on 3D
   Tiles metadata and USD API schemas. The boundary format is Arrow-compatible.

## References

* SideFX, *Houdini Geometry Attributes*: https://www.sidefx.com/docs/houdini/model/attributes.html
* SideFX, *HDK Geometry Introduction*: https://www.sidefx.com/docs/hdk/_h_d_k__geometry__intro.html
* Blender Manual, *Attributes Reference*: https://docs.blender.org/manual/en/latest/modeling/geometry_nodes/attributes_reference.html
* Blender Developer Docs, *Implicit Sharing*: https://developer.blender.org/docs/features/core/implicit_sharing/
* Blender PR #105994, implicit sharing library: https://projects.blender.org/blender/blender/pulls/105994
* Blender 3.6 release notes (nodes & physics): https://developer.blender.org/docs/release_notes/3.6/nodes_physics/
* Blender, *Bundles and Closures* (2025): https://code.blender.org/2025/08/bundles-and-closures/
* Blender 5.0 Geometry Nodes release notes: https://developer.blender.org/docs/release_notes/5.0/geometry_nodes/
* Blender source, `customdata.cc`: https://projects.blender.org/blender/blender/src/branch/main/source/blender/blenkernel/intern/customdata.cc
* OpenMesh documentation: https://graphics.rwth-aachen.de/media/openmesh_static/Daily-Builds/Doc/a06330.html
* CGAL Surface_mesh properties example: https://doc.cgal.org/5.5/Surface_mesh/Surface_mesh_2sm_properties_8cpp-example.html
* CGAL dynamic properties example: https://doc.cgal.org/5.0/BGL/Property_map_2dynamic_properties_8cpp-example.html
* OpenUSD `UsdGeomPrimvar`: https://openusd.org/release/api/class_usd_geom_primvar.html
* Foundry Katana, UsdPrimvarSet: https://learn.foundry.com/katana/Content/rg/usd_nodes/usdprimvarset.html
* OpenSubdiv, subdivision surfaces / fvar interpolation: https://graphics.pixar.com/opensubdiv/docs/subdivision_surfaces.html
* Museth, *VDB: High-Resolution Sparse Volumes with Dynamic Topology*, TOG 2013: https://www.museth.org/Ken/Publications_files/Museth_TOG13.pdf
* NanoVDB: https://developer.nvidia.com/nanovdb
* FreeCAD, Topological naming problem: https://wiki.freecad.org/Topological_naming_problem
* Spatial ACIS attributes documentation (from memory; URL may have moved): https://doc.spatial.com/get_doc_page/articles/a/t/t/Attributes_d0ce.html
* Delany & Bazley 1970, acoustical properties of fibrous absorbent materials: https://doi.org/10.1016/0003-682X(70)90031-9
* OGC CityGML 3.0 Conceptual Model: https://docs.ogc.org/is/20-010/20-010.html
* Kutzner, Chaturvedi, Kolbe 2020, *CityGML 3.0*: https://link.springer.com/10.1007/s41064-020-00095-z
* CityJSON specification: https://www.cityjson.org/specs/
* Cesium, *Fine-grained metadata in 3D Tiles Next*: https://cesium.com/blog/2022/05/31/fine-grained-metadata-in-3d-tiles-next
* EXT_structural_metadata: https://github.com/CesiumGS/glTF/tree/3d-tiles-next/extensions/2.0/Vendor/EXT_structural_metadata
* OGC 3D Tiles 1.1: https://docs.ogc.org/cs/22-025r4/22-025r4.html
* Apache Arrow columnar format: https://arrow.apache.org/docs/format/Columnar.html
* GeoArrow: https://geoarrow.org/
* mp-units: https://mpusz.github.io/mp-units/
* PTB room acoustics absorption coefficient database: https://www.ptb.de/cms/en/ptb/fachabteilungen/abt1/fb-16/ag-163/faq.html
* Schröder et al., *openMat* (DAGA 2013): https://infoscience.epfl.ch/record/188603/files/Schroder_et_al_openMat_DAGA2013.pdf
