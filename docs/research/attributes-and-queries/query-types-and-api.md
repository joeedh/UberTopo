# Geometry Query Types and a Unified Query API

Part of [attributes-and-queries](README.md). This file catalogues the geometric
queries UberTopo needs (driven by acoustic/optical drone detection and site-vulnerability
surveys), notes the best known algorithm for each, and proposes a unified,
batch-first query layer over heterogeneous geometry. The underlying structures are
covered in [spatial-acceleration-structures.md](spatial-acceleration-structures.md).

## 1. Query catalogue

### 1.1 Ray queries

| Variant | Returns | UberTopo use |
|---|---|---|
| **closest hit** | t, element, face-side, (u, v), normals | specular reflection paths, camera rendering, lidar simulation |
| **any hit / occlusion** | bool | shadow/LOS tests, image-source path validation |
| **all hits** (sorted, within [tmin, tmax]) | list of intersections | sound transmission through layers (walls, glazing, foliage), penetration depth, "what does this ray cross" |
| **k nearest hits** | first k | partially transparent media, transmission up to k layers |
| **ray march / integrate** | ∫ field ds along the ray (+ hits) | foliage/smoke attenuation via VDB, temperature-dependent travel time |
| **curved ray** | piecewise path | outdoor acoustics with temperature/wind gradients (refraction) |

Every hit record should include:

* `t` and the hit position in the query's frame;
* **B-rep element identity**: the persistent face ID and the face-side hit (front/back
  relative to the face orientation), not just a triangle index (see
  [embedded-geometry-attributes.md](embedded-geometry-attributes.md) §9);
* parametric coordinates on the carrier surface where available, and barycentrics on
  the tessellation;
* geometric normal, and shading normal if requested;
* **requested attribute values** gathered at the hit (material ref, absorption
  spectrum, class). The query should be able to gather attributes so that callers
  avoid a second random-access pass.

**Acoustic multi-bounce** uses these primitives in several algorithm families:

* **Image-source method** (Allen & Berkley 1979; Borish 1984): enumerate mirror images
  and validate each path with segment/occlusion queries. Exact for specular paths but
  exponential in order, so it needs large planar faces. This is a strong argument for
  *planar-simplified acoustic proxies* (see
  [lod-and-simplification.md](lod-and-simplification.md)).
* **Stochastic ray / particle tracing** for diffuse late reverberation: millions of
  incoherent closest-hit queries. This is the hardware-RT sweet spot.
* **Beam / frustum / cone tracing** (Funkhouser et al. 1998; Lauterbach, Chandak,
  Manocha 2007): volumetric queries that find all faces in a beam. They are
  efficient for specular paths but more complex
  ([Funkhouser et al. 1998, *A Beam Tracing Approach to Acoustic Modeling*](https://doi.org/10.1145/280814.280818)).
* **Diffraction** (§1.7).

### 1.2 Segment / line-of-sight

LOS between point pairs is the most frequent query in sensor-network planning. It is
an any-hit query on a segment, but:

* For long outdoor ranges (km), include **Earth curvature** and optionally
  atmospheric refraction (k-factor). In a local tangent frame "straight" is not
  geodesic. Above about 2–5 km, use either a curved segment or ECEF-consistent
  geometry (see [large-world-coordinates-and-tiling.md](large-world-coordinates-and-tiling.md)).
* **LOS matrices** (N sensors × M targets, e.g. 50 microphones × 10⁶ air voxels)
  should be a batched API call, not a loop. Rays from a common origin are coherent,
  which the executor should exploit by sorting.
* **Partial LOS**: for acoustic and RF purposes, LOS blocked by foliage ≠ LOS blocked
  by a wall. The query should support "accumulate transmission" rather than
  boolean, i.e. all-hits plus ray-march.

### 1.3 Closest point and distance

* **Point → surface** (unsigned/signed distance, closest element and parametric
  location). BVH branch-and-bound. FCPW is a well-engineered reference with
  vectorised CPU and Slang GPU BVHs ([FCPW](https://github.com/rohan-sawhney/fcpw)).
  For exact B-rep surfaces, refine with Newton projection from the tessellated
  closest point.
* **Point → curve** (station/offset on roads, distance to power lines). Use the curve
  BVH over Bézier spans, then Newton. Same machinery as above.
* **Curve/path → scene** clearance (minimum distance from a drone trajectory to
  obstacles): swept-sphere queries, or an SDF lookup along samples, then exact
  refinement.
* **Hausdorff distance** between LODs (validation): sample one surface and run
  closest-point queries against the other, both ways.

### 1.4 Point containment and region location

Needed for "is this candidate drone position inside a building?", "which room is
this microphone in?" and "which medium (air/water/vegetation) contains this point?".

| Method | Works on | Robustness | Cost |
|---|---|---|---|
| Ray parity (count crossings) | closed, watertight, manifold | fragile on grazing rays, cracks and duplicates | 1 ray (or 3 with voting) |
| **Generalized winding number** (Jacobson, Kavan, Sorkine-Hornung 2013) | soups, open meshes, self-intersections | robust: a smooth harmonic "insideness" that degrades gracefully | O(n) naive |
| **Fast winding numbers** (Barill et al. 2018) | triangle soups and point clouds | as above | O(log n) amortised via tree + dipole/Taylor approximation; up to ~1000× faster |
| GWN for trimmed NURBS (Spainhour, Gunderman, Weiss 2024–2025) | exact B-rep collections | robust to gaps and overlaps | per-patch adaptive |
| **Winding numbers on surfaces** (Feng, Gillespie, Crane 2023) | curves on surfaces (2D-in-surface) | robust to open/non-closed curves | Poisson solve |
| **Cell-complex point location** (explicit regions) | non-manifold B-rep with 3-cells | exact if topology is valid | 1 ray + topology lookup |

References: [Jacobson et al. 2013](https://igl.ethz.ch/projects/winding-number/);
[Barill et al. 2018](https://www.dgp.toronto.edu/projects/fast-winding-numbers/)
(implementation: [SideFX WindingNumber](https://github.com/sideeffects/WindingNumber), libigl);
[Feng et al. 2023](https://nzfeng.github.io/research/WNoDS/index.html);
[Spainhour et al., GWN for trimmed NURBS](https://arxiv.org/abs/2504.11435).

**Recommendation.** Because UberTopo's B-rep explicitly represents regions (rooms,
building interiors, water bodies, vegetation volumes as 3-cells or region-tagged
shells), primary point location should be **topological**: cast one ray (any
direction), take the closest hit face, look at the face-side facing the ray origin,
and return that side's region. This is O(1 ray), exact when the complex is valid,
and directly yields the region's attributes (medium properties). **Winding numbers are
the fallback** for imported or unclean geometry (photogrammetry, CAD soups) and for
*validating* region assignment, where disagreement between the two flags topology
errors.

### 1.5 Frustum, visibility and viewshed

* **Camera visibility** ("which elements does camera C see, and at what pixel
  coverage?"). For dense visibility, **rasterisation into an ID buffer** is far
  faster than rays. GPU raster with a persistent-ID + face-side buffer gives exact
  per-pixel visibility, and a histogram over IDs gives coverage. Rays win only for
  sparse sampling or non-pinhole models (fisheye, rolling shutter), and those can be
  done with ray generation at pixel centres.
* **Frustum culling / range** (elements intersecting a frustum or box): BVH box tests.
* **Viewshed on DEMs**: R3 (exact, O(n^1.5)), R2 (approximate, O(n)), XDraw (fastest,
  least accurate), and Van Kreveld's radial sweep (O(n log n)). GPU variants exist
  ([comparison of I/O-efficient algorithms, arXiv 1810.01946](https://arxiv.org/pdf/1810.01946);
  [MDPI IJGI 8(3):153](https://mdpi.com/2220-9964/8/3/153);
  [total-viewshed on multi-GPU, arXiv 2003.02200](https://arxiv.org/pdf/2003.02200)).
  These ignore buildings and vegetation except as DSM height.
* **3D viewshed with buildings/vegetation**: render cube/shadow maps from the
  observer (raster) or cast ray fans (RT). The output is a per-element or per-voxel
  visibility attribute.
* **Air-volume coverage** for drone defence: for each sensor, which voxels of air
  space within range are visible (camera) or have a clear or partially attenuated
  path (microphone). The union/count/min-range across sensors gives a coverage VDB.
  The **inverse survey query** is "the set of approach corridors to asset A not
  covered by any sensor", i.e. path-finding through uncovered air voxels. This
  combines volumetric coverage with graph search, so the query layer should output
  VDB/raster fields, not only per-ray results.

### 1.6 Range, k-NN and spatial joins

* **Range**: all elements, instances or points within a box, sphere or polygon
  (prism). BVH/R-tree box descent.
* **k-NN** on points (sensors, detections, trees): k-d tree or grid.
* **Spatial joins** (GIS: "buildings within 50 m of road class X"): R-tree × R-tree
  join, or PostGIS/DuckDB-spatial on exported tables. Do not reimplement a GIS
  database in the kernel. Export Arrow/GeoParquet and let analytics tools join
  ([GeoParquet](https://geoparquet.org/)).

### 1.7 Edge queries for diffraction

Diffraction around building corners, roof edges and walls is often the *dominant*
path to a microphone when the direct path is blocked. UberTopo's edge-aware B-rep is
an advantage here, because mesh-only systems must rediscover edges from triangles.

Required queries:

1. **Diffracting edge candidates**: edges with exterior dihedral angle beyond a
   threshold (wedges), length above a fraction of wavelength, and not internal to a
   planar region. This is a *derived edge attribute* (`acoustic.diffracting`, wedge
   angle, wedge face materials), computed once per LOD.
2. **Edges visible from a point** (source or listener): for each candidate edge,
   test visibility of the edge (sample points or the whole segment, i.e. partial
   visibility). Accelerate with an edge BVH (edges as segments/capsules) plus
   occlusion queries. Silhouette determination relative to the point prunes most
   edges.
3. **Edge-to-edge visibility graph** for higher-order diffraction: precomputed per
   scene/LOD, then A* through the graph at runtime. This is Schissler, Mückl & Calamia
   2021's approach, with a silhouette-edge heuristic to keep the edge set small and UTD
   for path intensity ([*Fast Diffraction Pathfinding for Dynamic Sound Propagation*, TOG 40(4) 2021](https://research.facebook.com/file/4521255027934795/Fast-Diffraction-Pathfinding-for-Dynamic-Sound-Propagation.pdf);
   [arXiv 2107.11548](https://arxiv.org/abs/2107.11548)). Earlier: Schissler, Mehra,
   Manocha 2014 for high-order diffraction in large scenes
   ([HIGHDIFF](https://gamma.cs.unc.edu/HIGHDIFF)); Tsingos et al. 2001 for UTD with
   beam tracing ([Tsingos et al. 2001](https://doi.org/10.1145/383259.383323)).
4. **Point → nearest point on edge, apex point** for a source/receiver pair: the
   point on the edge minimising total path length (Fermat). This is a 1D minimisation
   per edge, solved in closed form for straight edges.

Implications for the kernel: edges need **stable identity across LODs** (an LOD2
roofline edge corresponds to an LOD3 roofline edge), and the edge BVH must be
rebuilt when topology changes. Frequency matters too: at 100 Hz (λ ≈ 3.4 m) small
edges are irrelevant, so edge selection should depend on frequency band (an LOD
policy, §2.4).

### 1.8 Topological queries

Served directly by the B-rep, not by spatial structures:

* neighbours (vertex star, edge-adjacent faces in radial order, face-adjacent
  regions);
* boundary (∂ of a face set, of a region, of a selection), loops and holes;
* connected components (union-find over adjacency, filtered by attribute: "connected
  road network", "contiguous forest");
* **region adjacency graph**: regions sharing faces, labelled by the shared faces'
  attributes (an opening/portal vs a wall). This drives room-to-room acoustic coupling,
  intrusion paths for site surveys, and drone ingress through open windows;
* incidence counts and non-manifold classification (needed for validation).

These should be exposed through the same query API (batch over element sets) so that
procedural graphs can mix spatial and topological predicates.

### 1.9 Geodesic and network distance

* **Surface geodesics**: the heat method (Crane, Weischedel, Wardetzky 2013/2017) is
  fast, robust and reuses a Laplacian factorisation
  ([*The Heat Method for Distance Computation*](https://www.cs.cmu.edu/~kmcrane/Projects/HeatMethod/)).
  Exact polyhedral geodesics (MMP) are heavier. Uses here are modest: walking distance
  on terrain, creeping-path approximations, distance-based falloff of attributes.
* **Network distances** on the road/path graph (Dijkstra/A*/contraction hierarchies)
  matter more: response times and access routes. These live on the road graph
  entity, not on the mesh.

### 1.10 Sampling

* **Uniform area sampling** over face sets via alias tables over triangle/face
  areas, optionally attribute-weighted (e.g. sample only `class = RoofSurface`).
* **Blue-noise sampling**: Poisson-disk (Bridson 2007), weighted sample elimination
  (Yuksel 2015) for candidate sensor locations, vegetation placement and Monte Carlo
  acoustics ([Yuksel 2015](http://www.cemyuksel.com/research/sampleelimination/)).
* **Volume sampling** of regions (air space above a site, interior of rooms) with
  rejection via point-location, or stratified over the region's VDB/voxelisation.
* **Low-discrepancy sequences** (Sobol, Owen-scrambled) for ray directions in
  stochastic acoustics. Deterministic seeding is needed for reproducibility.
* **Curve sampling** by arc length (road stations every *d* metres) using the exact
  curve's arc-length parameterisation.

## 2. A unified query layer

### 2.1 Heterogeneous shapes behind one interface

The scene contains structurally different geometry: B-rep bodies (exact surfaces +
tessellation proxies), triangle meshes, heightfield terrain, VDB volumes, curve sets
(power lines, road reference lines), point sets, instances, and procedural
generators that realise geometry lazily. The query layer should treat each as a
**Shape** with optional capabilities:

```
Shape capabilities (each optional, discovered at runtime):
  bounds(lod)                                  -> AABB (+OBB)
  intersect_rays(batch, lod, flags)            -> hits
  closest_points(batch, lod)                   -> projections
  contains(batch)                              -> winding / region
  integrate_rays(batch, field)                 -> ∫ along rays
  sample(n, weights, rng)                      -> samples
  tessellate(tol)                              -> mesh + transfer map
```

Two-level dispatch:

1. **Top level** (per world tile): a TLAS whose leaves are shape instances. Built-in
   triangle shapes go into hardware/Embree BLASes. Exotic shapes become **custom
   primitives** (Embree user geometry, OptiX/Vulkan intersection programs), or are
   *tessellated into a proxy BLAS* with exact refinement in a second pass.
2. **Shape level**: shape-specific structure (triangle BVH, max-mipmap quadtree, VDB
   tree, curve-span BVH).

**Refinement pattern.** For exact B-rep surfaces, intersect the *conservative*
tessellation (enlarged by its certified error ε) in the fast path, then run Newton on
the exact surface from the proxy hit. This gives exact results at mesh speed. The
proxy must be conservative, or hits near silhouettes are missed. Fattening by ε via
inflated bounding boxes in leaves achieves this.

### 2.2 Query specification

Every query carries an explicit specification instead of global state:

```
QuerySpec {
  kind:        ClosestHit | AnyHit | AllHits(k) | Integrate(field) | Closest | Contains | ...
  frame:       FrameId                    # coordinates of inputs/outputs
  lod:         LodPolicy                  # error tolerance ε, or acoustic band (f → ε ≈ λ/8),
                                          #   or "finest available"; never "view-dependent" implicitly
  filter:      MaskBits + AttributePredicate   # e.g. class ∈ {Wall, Roof} ∧ acoustic.opaque
  precision:   Fast32 | Robust64 | Exact
  gather:      [AttributeName]            # attributes to return at hits
  snapshot:    SceneVersion               # which immutable scene version to query
  determinism: Strict | Relaxed
}
```

Design notes:

* **Explicit LOD policy.** Rendering systems select LOD by screen-space error from a
  camera. Simulation must select it by *physical relevance*: wavelength for
  acoustics, sensor resolution for cameras, a user tolerance for surveys. LOD must
  not depend on what happens to be streamed in, or results become irreproducible (see
  [lod-and-simplification.md](lod-and-simplification.md)).
* **Filters at two granularities.** Coarse filtering via instance masks or separate
  TLASes per population (cheap on hardware RT). Fine filtering via attribute predicates,
  evaluated in any-hit/filter callbacks (Embree filter functions; any-hit shaders on
  GPU). Fine filters are expensive on GPUs because they disable some hardware
  shortcuts, so compile common predicates into mask bits or separate BLASes.
* **Precision tiers.** `Fast32` uses hardware RT/Embree in local tile frames.
  `Robust64` uses a CPU fp64 BVH with conservative traversal. `Exact` uses robust
  predicates for classification (orient3d, insphere; Shewchuk's adaptive predicates,
  [Shewchuk 1997](https://www.cs.cmu.edu/~quake/robust.html)). Callers ask for what
  they need. Kernel operations default to `Robust64`/`Exact`, simulation to `Fast32`.
* **Gather.** Returning attributes with hits avoids a second scattered pass and lets
  the backend fetch columns while the data is in cache. GPU backends can bind attribute
  columns as buffers indexed by (instance row, primitive → element map).

### 2.3 Batch-first execution

All queries are **batches** (SoA arrays of origins, directions, tmin/tmax, user IDs).
Single queries are batches of one. The executor:

1. Partitions the batch by tile (frame) and backend.
2. Optionally **sorts for coherence**: Morton order of origins, octahedral binning
   of directions. Coherent rays traverse 2–5× faster on CPU SIMD packets and help
   GPU warps.
3. Runs in **wavefront** style for multi-bounce algorithms: each bounce is a batch,
   with shading and material lookup in separate kernels
   ([Laine, Karras, Aila 2013, *Megakernels Considered Harmful*](https://research.nvidia.com/publication/2013-07_megakernels-considered-harmful-wavefront-path-tracing-gpus)).
   Acoustic ray tracing with frequency-band energy per ray fits this model.
4. Returns results in SoA buffers in the input order (or with an index map).

**GPU API choice.** For simulation use **inline ray queries in compute shaders**
(`VK_KHR_ray_query`, DXR 1.1 `RayQuery`, Metal intersection queries) rather than
full RT pipelines. They are simpler (no shader binding table), fit wavefront compute,
and are portable across Vulkan/DX12/Metal (via a thin abstraction). OptiX remains an
option for NVIDIA-only deployments needing multi-level instancing or curves.

### 2.4 Snapshots, editing and concurrency

Procedural editing and simulation run concurrently: the model regenerates a block
while an acoustic batch runs. The clean solution is **multi-version snapshots**:

* Scene state = (topology, attribute columns, geometry arenas, acceleration
  structures), all immutable once published and shared through COW (see
  [attribute-systems-survey.md](attribute-systems-survey.md)).
* An edit produces a new version that shares unchanged pages, BLASes and tiles.
  Only touched tiles get rebuilt BLASes, and the TLAS/PTLAS is rebuilt or patched.
* Queries name a `SceneVersion`. Long-running simulations pin a version, and old
  versions are reclaimed when unpinned (epoch-based reclamation).

This is MVCC applied to geometry. It removes locking from the query path and makes
simulation runs reproducible against a named model version, which matters for test
and evaluation records.

### 2.5 Determinism

For test and evaluation of defence systems, reproducibility is a requirement:

* Fixed tie-breaking: equal-t hits ordered by (persistent element ID, face-side).
* Seeded RNG streams per query index, not per thread.
* LOD selection independent of streaming state and camera.
* Backend differences (Embree vs GPU fp32) documented as tolerances. Results carry
  their `precision` tier.

### 2.6 Differentiable queries (optional, later)

Differentiable rendering systems (Mitsuba 3 on Dr.Jit) compute gradients through
ray–scene intersection, handling visibility discontinuities with edge sampling (Li et
al. 2018) or reparameterisation (Loubet et al. 2019)
([Mitsuba 3](https://www.mitsuba-renderer.org/);
[Dr.Jit](https://github.com/mitsuba-renderer/drjit);
[Li et al. 2018](https://people.csail.mit.edu/tzumao/diffrt/);
[Loubet et al. 2019](https://rgl.epfl.ch/publications/Loubet2019Reparameterizing)).
Possible uses in UberTopo are sensor-placement optimisation (maximise coverage),
inverse estimation of acoustic material parameters from measured impulse responses,
and calibration of geometry against field recordings.

Recommendation: **not in the core for v1**. Instead (a) make all queries pure
functions of (scene version, parameters, seed) so derivative-free and
finite-difference optimisers work, and (b) keep the query backend abstraction thin
enough to add a Dr.Jit/Slang autodiff backend later. Coverage objectives are
piecewise constant (visibility), so gradients are of limited use without the
edge-sampling machinery anyway.

## 3. Worked examples

**A. Acoustic localisation test case.** Inputs: 30 microphones, a swarm of 20 drones
on trajectories, an urban block at LOD2+ with materials.

1. Per frequency band, choose LOD by λ.
2. Image-source paths up to order 2–3 against the planar acoustic proxy. Validate
   with batched segment occlusion queries (gather: absorption at each reflection
   face-side).
3. Diffraction paths via the precomputed edge visibility graph. Source/receiver-to-edge
   visibility by batched occlusion.
4. Late reverberation by stochastic ray tracing (closest-hit batches, wavefront).
5. Foliage and atmosphere attenuation via ray-march over VDB fields.
6. Output: per-mic impulse responses per drone, with path metadata carrying the
   persistent IDs of the reflecting faces and diffracting edges, so errors in
   localisation can be traced to specific reflectors.

**B. Site vulnerability survey.** Inputs: an asset footprint, candidate sensor
positions, terrain + buildings + vegetation.

1. Sample candidate sensor locations (blue noise on roofs and poles, attribute-filtered).
2. Compute air-volume coverage per candidate: batched LOS into a VDB of air voxels
   within range.
3. Greedy or ILP selection of sensors maximising coverage.
4. Uncovered-corridor search: A* through uncovered voxels toward the asset.
5. Topological query: openings (windows/doors) of the asset's regions reachable from
   uncovered corridors gives the entry-point list.

## References

* Jacobson, Kavan, Sorkine-Hornung, *Robust Inside-Outside Segmentation using Generalized Winding Numbers*, SIGGRAPH 2013: https://igl.ethz.ch/projects/winding-number/
* Barill, Dickson, Schmidt, Levin, Jacobson, *Fast Winding Numbers for Soups and Clouds*, SIGGRAPH 2018: https://www.dgp.toronto.edu/projects/fast-winding-numbers/
* SideFX WindingNumber implementation: https://github.com/sideeffects/WindingNumber
* Feng, Gillespie, Crane, *Winding Numbers on Discrete Surfaces*, SIGGRAPH 2023: https://nzfeng.github.io/research/WNoDS/index.html
* Spainhour et al., *Robust Containment Queries over Collections of Trimmed NURBS Surfaces via Generalized Winding Numbers*: https://arxiv.org/abs/2504.11435
* FCPW: https://github.com/rohan-sawhney/fcpw
* Funkhouser et al., *A Beam Tracing Approach to Acoustic Modeling for Interactive Virtual Environments*, SIGGRAPH 1998: https://doi.org/10.1145/280814.280818
* Schissler, Mückl, Calamia, *Fast Diffraction Pathfinding for Dynamic Sound Propagation*, TOG 2021: https://research.facebook.com/file/4521255027934795/Fast-Diffraction-Pathfinding-for-Dynamic-Sound-Propagation.pdf ; https://arxiv.org/abs/2107.11548
* Schissler, Mehra, Manocha, *High-Order Diffraction and Diffuse Reflections for Interactive Sound Propagation in Large Environments*, SIGGRAPH 2014: https://gamma.cs.unc.edu/HIGHDIFF
* Tsingos, Funkhouser, Ngan, Carlbom, *Modeling Acoustics in Virtual Environments Using the Uniform Theory of Diffraction*, SIGGRAPH 2001: https://doi.org/10.1145/383259.383323
* Viewshed algorithm comparisons: https://arxiv.org/pdf/1810.01946 ; https://mdpi.com/2220-9964/8/3/153 ; https://arxiv.org/pdf/2003.02200
* GeoParquet: https://geoparquet.org/
* Crane et al., *The Heat Method for Distance Computation*: https://www.cs.cmu.edu/~kmcrane/Projects/HeatMethod/
* Yuksel, *Sample Elimination for Generating Poisson Disk Sample Sets*, EG 2015: http://www.cemyuksel.com/research/sampleelimination/
* Shewchuk, *Adaptive Precision Floating-Point Arithmetic and Fast Robust Geometric Predicates*: https://www.cs.cmu.edu/~quake/robust.html
* Laine, Karras, Aila, *Megakernels Considered Harmful*, HPG 2013: https://research.nvidia.com/publication/2013-07_megakernels-considered-harmful-wavefront-path-tracing-gpus
* Mitsuba 3: https://www.mitsuba-renderer.org/ ; Dr.Jit: https://github.com/mitsuba-renderer/drjit
* Li, Aittala, Durand, Lehtinen, *Differentiable Monte Carlo Ray Tracing through Edge Sampling*, 2018: https://people.csail.mit.edu/tzumao/diffrt/
* Loubet, Holzschuch, Jakob, *Reparameterizing Discontinuous Integrands for Differentiable Rendering*, 2019: https://rgl.epfl.ch/publications/Loubet2019Reparameterizing
