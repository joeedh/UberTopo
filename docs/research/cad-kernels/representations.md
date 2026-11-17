# Geometric representations for multi-LOD environment modelling

Part of [CAD kernels, representations and robustness](README.md).

UberTopo is **not mechanical CAD**. The domain differs from MCAD in five ways:

- **Scale.** A test site is 1–50 km across. Detail ranges from mm (window
  mullions for close-range cameras) to tens of metres (terrain for long-range
  acoustics).
- **Generation.** Most content is *procedurally generated* from rules, GIS
  vectors, DEMs and seeds, not drawn by a person.
- **Consumers.**
  - Acoustic propagation needs coarse, watertight geometry with explicit edges
    for diffraction, plus surface impedance.
  - Camera/EO/IR rendering needs visual detail and materials.
  - Line-of-sight and vulnerability analysis needs conservative occluders and
    fast ray queries.
  - Possibly RF/radar later, which needs curvature and edges.
- **Kind of content.** Most of the world is *not solid parts*: terrain is a
  2.5D surface, water is a surface plus a volume, vegetation is statistical and
  volumetric, and power lines are curves with a radius.
- **Accuracy.** Validity and consistency matter more than tight accuracy. A
  building 2 cm too wide is irrelevant. A crack that lets a sound ray leak
  through a wall corrupts the simulation.

This file compares candidate representations against those needs.

---

## 1. Exact NURBS / analytic B-rep

**What it is.** Faces are trimmed parametric surfaces. Edges have 3D curves and
per-face p-curves. This is the industry-standard MCAD model (STEP AP203/214/242).

**Pros.**
- Exact conics and quadrics (cylindrical tanks, domes, pipes).
- Resolution-independent, so it can be tessellated to any LOD.
- Compact for smooth shapes.
- Standard interchange with BIM and infrastructure CAD (IFC also has a
  B-rep/NURBS subset).

**Cons.**
- **Surface–surface intersection is the core difficulty.** Marching SSI can
  miss small loops, branch points and tangential intersections. Its result is
  an approximation (a fitted spline) that does not lie exactly on either
  surface. That is why tolerant edges exist (see [kernel survey](kernel-survey.md)).
- Fillets and offsets are where commercial kernels spend decades.
- Trimming curves in two parameter spaces disagree with the 3D curve. Watertight
  tessellation needs careful shared-edge discretisation.
- Heavy for 10⁶–10⁷ faces. MCAD kernels are tuned for 10²–10⁴ faces per body.

**Fit for UberTopo.** **Low as a core.** The curved surfaces that matter
(roads, terrain, tanks, domes, cooling towers, pipes) are better represented
*procedurally*: a clothoid alignment plus a cross-section, or a surface of
revolution defined by a profile. They should be evaluated directly to the
needed LOD rather than turned into a general NURBS B-rep and then intersected.
Keep exact analytic *carriers* as optional face attributes (see §8). Do not
build a general NURBS SSI engine.

## 2. Polyhedral / mesh B-rep (planar faces, polygons with holes)

**What it is.** Same topology as a B-rep, but every face is planar (a polygon
with holes) or a triangle. Examples: Blender BMesh, CGAL `Surface_mesh`/Nef, and
the plane-based meshes of EMBER and Campen & Kobbelt.

**Pros.**
- Most buildings, walls, roofs, bridges, retaining walls and road
  cross-sections are **piecewise planar**.
- Booleans on polyhedra can be made **exact and fast** (see
  [robustness](robustness-and-exact-arithmetic.md)). That is far beyond what is
  realistically achievable for NURBS.
- A **face with holes** (a wall with window openings) is natural and keeps face
  count low: one face, many inner loops. This matches the requirement for "a
  general non-manifold brep with holes in faces".
- Direct path to GPU, acoustic ray tracers and physics engines.

**Cons.**
- Curved surfaces must be faceted per LOD, so curved-on-curved operations must
  be re-run per LOD, or done once at the finest LOD and simplified.
- Visual smoothness needs normals, subdivision or displacement.

**Fit.** **High.** This should be the canonical *evaluated* representation.

## 3. Implicit surfaces / SDF / F-rep

**What it is.** The geometry is the zero set of a function f(x,y,z). With an
SDF, |f| is the distance to the surface. With F-rep (Pasko et al.), functions
are combined with R-functions or min/max for CSG. Systems include nTop,
libfive, Fidget, Curv, and GPU SDF engines (see
[kernel survey §4](kernel-survey.md)).

**Pros.**
- Booleans, smooth blends ("smin"), offsets, shells and morphological
  operations never fail and always give a valid volume.
- Natural for **natural forms**: erosion, rock, cliffs, overhangs, caves, dunes,
  snow and sand accumulation, river beds. It also suits *environmental
  operations* such as "carve a ditch along this curve" or "offset the wall
  footprint by 0.3 m".
- Point queries (inside/outside, distance, gradient) are trivial. That is
  useful for procedural placement and clearance checks, for example "is this
  drone path ≥ 5 m from any obstacle?".
- Hierarchical evaluation with interval arithmetic or Lipschitz bounds allows
  LOD-aware evaluation and culling ([Keeter, Fidget](https://mattkeeter.com/projects/fidget);
  [Barbier et al. 2025, Lipschitz pruning](https://diglib.eg.org/handle/10.1111/cgf70057)).

**Cons.**
- No explicit edges or faces, so no topology for attributes or diffraction
  edges until meshed.
- Sharp features need dual contouring or feature-preserving extraction.
- Thin features below the sampling resolution disappear. This matters for
  power lines, fences and antenna masts, which are drone hazards and visually
  important.
- Exact dimensional control needs exact distance functions, and many
  composed SDFs are only bounds rather than true distances.
- Evaluation cost grows with primitive count unless spatially pruned.

**Fit.** **High as a secondary, generative layer.** Use it for natural terrain
features, morphological edits and coarse proxies. Mesh it into the polyhedral
B-rep at a chosen LOD. Do not use it as the representation for buildings or
roads.

## 4. Voxels and sparse volumes (OpenVDB / NanoVDB)

**What it is.** A sparse hierarchical grid storing scalars or vectors. Typical
contents: SDF narrow bands, densities, material ids.

**Pros.**
- A mature, Apache-licensed, GPU-friendly implementation exists
  ([OpenVDB](https://openvdb.org/about), [NanoVDB](https://research.nvidia.com/labs/prl/nanovdb/nanovdb2021.pdf)).
- Level-set CSG is unconditionally robust.
- Ideal for **volumetric media**:
  - vegetation canopy density (light extinction for cameras, acoustic
    scattering and absorption for microphones),
  - swamp water/mud mixtures,
  - fog, smoke and dust,
  - terrain edits (excavation, craters, rubble).
- Hierarchical DDA ray marching is efficient, which helps both visibility and
  acoustic ray queries.

**Cons.**
- Memory scales with surface area divided by voxel size squared. 1 cm voxels
  across km² are impossible, so voxels are a *local* or *coarse* representation.
- Axis-aligned staircase artifacts unless SDF interpolation is used.
- No explicit topology.

**Fit.** **High for media and coarse proxies. Low for crisp man-made geometry.**

## 5. Heightfields / TINs / DEMs

This is not in the original brief, but it is critical. Terrain over km² is
best represented as a **2.5D heightfield**: raster DEMs, or an adaptive TIN
(triangulated irregular network) with breaklines.

- **Breaklines** are constrained edges in a constrained Delaunay triangulation
  (CDT). Examples: road edges, kerbs, river banks, embankment crests.
- A **2D exact CDT** (Shewchuk predicates; CGAL/Geogram/Triangle) is the right
  robust tool here. It is easier than 3D Booleans.
- Overhangs, caves and bridges break 2.5D. They are handled as separate 3D
  objects (bridges) or local SDF/voxel edits (cliffs, caves) stitched to the
  heightfield.

**Fit.** **Essential** for terrain, with a well-defined stitching contract to
the B-rep (see [tessellation](tessellation-lod.md)).

## 6. Subdivision surfaces

**What it is.** Catmull–Clark or Loop control meshes whose limit is a smooth
surface. Implementations include OpenSubdiv (Pixar, Apache-2.0) and Rhino SubD.

**Pros.**
- Smooth organic shapes from coarse cages.
- Built-in multi-resolution: the cage is the coarse LOD and refinement levels
  are finer LODs.
- Feature-adaptive, crack-free GPU tessellation exists.
- Semi-sharp creases.

**Cons.**
- Booleans on subdivision surfaces are not well defined. They are usually done
  by converting to meshes.
- Little benefit for man-made planar structures.

**Fit.** **Low to medium.** Use it for organic, artist-authored assets (rocks,
sculpture, vehicles) and as a visual smoothing layer. It is not core.

## 7. Point clouds and captured representations

Real sites will be surveyed with LiDAR, photogrammetry and Gaussian splatting.

- Points and splats give excellent **visual** realism for camera simulation and
  instant site capture.
- Points give **no** watertightness or topology. Acoustic simulation and
  line-of-sight need surfaces. Use reconstruction such as screened Poisson or
  **alpha wrapping**, which gives a guaranteed watertight, intersection-free
  2-manifold from soups or points with controllable cavity size
  ([CGAL Alpha_wrap_3](https://doc.cgal.org/latest/Alpha_wrap_3/)).
- Semantic segmentation (building, tree, ground) then feeds the procedural
  layer: replace a noisy building scan with a fitted procedural building, keep
  the scan as visual texture.

**Fit.** An **ingest path**, not a core representation. The core must accept
"mesh soup with semantic labels" as input and produce consistent geometry
(wrap, fit or simplify).

## 8. Hybrid: procedural source + polyhedral B-rep + attached fields

No single representation fits UberTopo. The recommended architecture keeps
*several* representations, with clear authority between them:

```
                 ┌──────────────────────────────────────────────┐
  authority →    │  Procedural graph (rules, GIS vectors, seeds) │  ← source of truth
                 └──────────────┬───────────────────────────────┘
                                │ evaluate(LOD, region, consumer)
         ┌──────────────────────┼─────────────────────────┬───────────────────┐
         ▼                      ▼                         ▼                   ▼
  Analytic carriers       Polyhedral non-manifold   Heightfield / TIN     Sparse volumes
  (planes, clothoid       B-rep (faces w/ holes,    (terrain, CDT with    (VDB: vegetation
   alignments, surfaces   exact plane-based         breaklines)           density, SDF edits,
   of revolution, NURBS   booleans, attributes)                           media)
   via adapter)                │
         │                     │
         └──── embedded as face/edge attributes (carrier id + params) ───┘
                                │
                                ▼
                 Consumer-specific tessellations / proxies (see tessellation-lod.md)
```

Key properties:

1. **The procedural graph is authoritative.** Geometry at any LOD is a *cache*
   of an evaluation. This removes the need for "edit the evaluated B-rep and
   keep history consistent" (the persistent naming problem). It also allows
   LOD to be an evaluation parameter instead of a post-process.
2. **The polyhedral B-rep is the integration layer.** Every consumer ultimately
   needs faces, edges and attributes. Planar faces with holes keep counts low,
   and polyhedral Booleans can be exact.
3. **Curved geometry lives as analytic carriers attached to faces and edges.**
   Each carries an id plus parameters: a clothoid alignment, a cylinder, a
   NURBS patch. Tessellation and normals can then be refined later without
   re-running Booleans for visual purposes (normal maps, displacement, GPU
   tessellation). This also answers the requirement to "embed plane/space
   curves and surface data into the brep" and fits the transcendental-curve
   track.
4. **Fields (VDB grids, SDF expressions) are attached to regions or cells**,
   not converted to B-rep unless a consumer needs a surface.
5. **Vegetation is mostly instanced and statistical.** Trees are instanced
   proxies at the visual LOD. For acoustics and line-of-sight they are a
   *volumetric medium* (density/attenuation field) or a canopy envelope
   surface, never individually Booleaned.

### Representation suitability matrix

| Content | Visual (camera) LOD high | Acoustic | LOS / vulnerability | Best authoring repr. |
|---|---|---|---|---|
| Buildings | polyhedral B-rep + detail instances | coarse watertight polyhedral, explicit edges | polyhedral, conservative | procedural → polyhedral B-rep |
| Terrain | adaptive TIN / displaced heightfield | coarse TIN + impedance attr. | heightfield (fast DDA) | DEM + breaklines (+ SDF edits) |
| Roads | swept polyhedral strips draped on TIN | part of terrain surface + impedance | part of terrain | clothoid alignments + profiles |
| Bridges / walls | polyhedral B-rep | polyhedral | polyhedral | sweeps + Booleans |
| Forests | instanced meshes / impostors / splats | volumetric scattering medium | density field or canopy envelope | point process + species rules |
| Swamps / water | surface mesh + volume | surface (impedance), maybe transmissive | surface | heightfield + water level + VDB |
| Power lines, masts | swept tubes / instanced | usually ignored (thin) or wire scatterers | **critical** thin obstacles | catenary curves + radius attribute |
| Rocks / organic | SubD or SDF-meshed | coarse wrap | wrap | SDF / scans |
| Scanned sites | splats / textured mesh | alpha-wrapped proxy | alpha-wrapped proxy | point cloud ingest |

## 9. Note on non-manifoldness (brief; owned by the B-rep track)

Robust Booleans via **arrangements** naturally produce a *cell complex*. Faces
separate cells, and each cell gets a winding-number or label vector (Zhou et
al. 2016; EMBER; Lévy's Weiler model). That is a **3D cell complex**, which is
exactly what a non-manifold B-rep with regions or volumes stores. Keeping the
arrangement as a first-class structure has several benefits:

- multi-material buildings (wall core vs. cladding vs. air gap) with shared
  faces,
- acoustic partitions (rooms, courtyards),
- water volumes adjacent to terrain,
- consistent LOD without re-Booleaning.

This is an argument for the B-rep track to support **3-cells (regions)**, not
only 2-manifold shells.

## References

- Pasko et al., F-rep (overview, prior knowledge): https://en.wikipedia.org/wiki/Function_representation
- nTop on B-reps vs implicits: https://ntopology.com/resources/blog/understanding-the-basics-of-b-reps-and-implicits/
- Fidget (Keeter): https://mattkeeter.com/projects/fidget ; gradients/Lipschitz: https://www.mattkeeter.com/blog/2025-05-14-gradients/
- Lipschitz Pruning (Barbier et al. 2025): https://diglib.eg.org/handle/10.1111/cgf70057
- OpenVDB: https://openvdb.org/about ; NanoVDB paper: https://research.nvidia.com/labs/prl/nanovdb/nanovdb2021.pdf
- NVIDIA NanoVDB blog: https://developer.nvidia.com/blog/accelerating-openvdb-on-gpus-with-nanovdb
- CGAL 3D Alpha Wrapping: https://doc.cgal.org/latest/Alpha_wrap_3/ ; paper: https://history.siggraph.org/?p=128496
- OpenSubdiv (prior knowledge): https://graphics.pixar.com/opensubdiv/
- Mesh arrangements for solid geometry: https://www.cs.columbia.edu/cg/mesh-arrangements
- Lévy, exact mesh CSG / Weiler model: https://arxiv.org/abs/2405.12949v2
- CityGML 3.0 LoD concept (OGC user guide): https://docs.ogc.org/guides/20-066.pdf
