# Spatial Acceleration Structures

Part of [attributes-and-queries](README.md). This file surveys acceleration
structures for geometric queries over environment-scale scenes: BVHs, k-d trees,
octrees, R-trees, grids/hashes, terrain quadtrees and sparse volumes. It also covers
how they handle dynamic procedural edits. Query *types* and the API layer are in
[query-types-and-api.md](query-types-and-api.md).

## 1. Summary: which structure for which job

| Workload in UberTopo | Recommended structure | Why |
|---|---|---|
| Ray casting against surfaces (acoustic, optical, LOS) | **Two-level BVH** (TLAS over instances/tiles, BLAS per object/tile), wide (4/8-ary), SAH-built | Industry standard. Hardware RT available. Instancing for vegetation and repeated buildings. |
| Closest point / distance to surfaces | Same BVH, or a distance-specialised BVH (FCPW) | Branch-and-bound on box distance. |
| Ray marching through media (foliage density, smoke, temperature) | **VDB / NanoVDB** with HDDA | Sparse, hierarchical empty-space skipping. |
| Terrain ray casting, LOS, viewshed | **Heightfield + min/max quadtree** (max-mipmaps) | Orders of magnitude less memory than a triangulated BVH and exact on the raster. |
| 2D GIS vector layers (roads, parcels, footprints, OSM) | **Packed R-tree** (STR/Hilbert) for static data, R* for dynamic | Mature, disk/page friendly, matches PostGIS/GEOS/Boost. |
| Moving points (drone swarm tracks, sound sources, particles), k-NN | **Uniform grid / spatial hash**, rebuilt per step; **k-d tree** (nanoflann) for static clouds | O(n) rebuild, cache-friendly, trivially parallel. |
| Dynamic object broadphase during procedural editing | **Loose octree** or incremental BVH | Cheap insert/remove without global rebuild. |
| Embedded curves on faces (2D) | Per-face **2D R-tree / BVH** over curve-span boxes | Small, local. |
| Volumetric occupancy (air-space voxels for coverage maps) | **VDB** or sparse voxel octree / SVDAG | Sparse, compressible. |

The main architectural point is that **no single structure serves everything**, but
**one query front-end can dispatch to all of them** (see
[query-types-and-api.md](query-types-and-api.md)). BVHs dominate, and the rest are
specialised companions.

## 2. Bounding volume hierarchies

### 2.1 Quality metric: SAH

The **surface area heuristic** estimates the expected cost of a random ray traversing
a node from the ratio of child to parent surface area
([MacDonald & Booth 1990](https://doi.org/10.1007/BF01911006);
[Wald 2007, *On fast construction of SAH-based BVHs*](https://www.sci.utah.edu/~wald/Publications/2007/ParallelBVHBuild/fastbuild.pdf)).
Caveats for UberTopo:

* SAH assumes rays uniformly distributed in direction and origin. Acoustic and
  sensor rays from a few sources, and viewshed rays from a few observers, are far from
  uniform. Ray-distribution-aware heuristics exist (Bittner & Havran's RDH, Fabianowski
  et al. 2009 "inside" metric), but SAH is "good enough" in practice
  ([Meister et al. 2021, survey](https://diglib.eg.org/handle/10.1111/cgf142662)).
* Closest-point and distance queries are better served by **compact boxes**
  (minimise volume or diagonal). SAH trees work acceptably.
* "Long thin" geometry such as power lines, fences, rails and tree branches makes
  axis-aligned boxes inefficient. **Spatial splits** (SBVH, Stich et al. 2009) or
  **oriented bounding boxes** at the BLAS root (Embree supports OBB nodes for hair)
  help. Power lines and wire fences are acoustically and optically relevant to drone
  defence, since drones hit wires, and are the kind of geometry that breaks naive
  BVHs.

### 2.2 Builders

| Builder | Where | Quality | Speed | Notes |
|---|---|---|---|---|
| Binned SAH top-down | CPU (Embree), GPU | high | fast | workhorse |
| SBVH (spatial splits) | CPU | highest | slower | for static, long/overlapping triangles ([Stich et al. 2009](https://www.nvidia.com/docs/IO/77714/sbvh.pdf)) |
| LBVH (Morton codes) | GPU | low–medium | fastest | [Lauterbach et al. 2009](https://doi.org/10.1111/j.1467-8659.2009.01377.x); [Karras 2012](https://research.nvidia.com/publication/2012-06_maximizing-parallelism-construction-bvhs-octrees-and-k-d-trees) |
| Treelet restructuring (TRBVH/ATRBVH) | GPU | high | fast | [Karras & Aila 2013](https://research.nvidia.com/publication/2013-07_fast-parallel-construction-high-quality-bounding-volume-hierarchies) |
| PLOC / PLOC++ | GPU | high | fast | [Meister & Bittner 2018](https://dcgi.felk.cvut.cz/projects/ploc/); [Benthin et al. 2022 (PLOC++)](https://www.intel.com/content/www/us/en/developer/articles/technical/ploc-for-bounding-volume.html) |
| **H-PLOC** | GPU | ≈PLOC++ | 1.1–3.6× faster than PLOC++/ATRBVH, single kernel, includes direct binary→wide conversion | [Benthin, Meister et al. 2024, HPG/PACMCGIT](https://gpuopen.com/download/publications/HPLOC.pdf) |
| Insertion-based / incremental | CPU | medium–high | per-insert O(log n) | [Bittner et al. 2015, *Incremental BVH construction*](https://doi.org/10.1111/cgf.12581) |

Practical take for UberTopo:

* **CPU:** use **Embree 4** (Apache-2.0) for the CPU path: production-grade builders,
  SIMD traversal, user geometry callbacks, curves, points, instancing including
  multi-level, and robust mode
  ([Embree](https://www.embree.org/); [Embree API](https://www.embree.org/api.html)).
  Alternatively, **tinybvh** (Bikker, 2024–2025, MIT, single header) has many builders
  (binned SAH, SBVH, wide BVH conversion, CWBVH) and is easy to extend to double
  precision ([tinybvh](https://github.com/jbikker/tinybvh)).
* **GPU:** for hardware RT, use the driver's builder through Vulkan
  `VK_KHR_acceleration_structure` + `VK_KHR_ray_query` (portable), DXR, Metal ray
  tracing or OptiX. For compute-only GPU paths (custom primitives, double precision,
  non-standard queries), use H-PLOC-style builders.

### 2.3 Wide and compressed BVHs

Modern traversal uses 4-, 8- or 16-wide nodes with quantised child boxes:

* CWBVH (Ylitie, Karras, Laine 2017) is an 8-wide BVH with 8-bit quantised child
  bounds, at 80 bytes/node
  ([Ylitie et al. 2017](https://research.nvidia.com/publication/2017-07_efficient-incoherent-ray-traversal-gpus-through-compressed-wide-bvhs)).
* Hardware RT formats are opaque, but wide and compressed internally.
* For memory-bound environment-scale scenes (10⁹+ triangles), **compressed leaf
  geometry** matters as much as nodes. AMD's **Dense Geometry Format (DGF)** stores
  meshlets of ≤64 vertices/≤64 triangles in 128-byte blocks, designed for hardware RT
  consumption ([AMD DGF paper](https://gpuopen.com/download/DGF.pdf);
  [Animating geometry with DGF](https://gpuopen.com/learn/animating-geometry-with-amd-dgf/)).
  NVIDIA's **cluster acceleration structures** (CLAS, ≤256 triangles/vertices per
  cluster) and **partitioned TLAS** ship as `VK_NV_cluster_acceleration_structure` /
  `VK_NV_partitioned_acceleration_structure` ("RTX Mega Geometry"). A DXR preview of
  *Clustered Geometry* and *PTLAS* followed
  ([NVIDIA RTX Mega Geometry Vulkan samples](https://developer.nvidia.com/blog/nvidia-rtx-mega-geometry-now-available-with-new-vulkan-samples);
  [Microsoft DirectX blog, CLAS/PTLAS preview](https://devblogs.microsoft.com/directx/clas-ptlas-preview/)).
  Clusters cannot be refitted, so edits rebuild the affected clusters.

These developments matter for UberTopo because cluster-granularity BLAS fits
**tile-local procedural edits**: regenerate one building, rebuild its clusters,
update its PTLAS partition. See [lod-and-simplification.md](lod-and-simplification.md)
for the meshlet/Nanite discussion.

### 2.4 Two-level (and multi-level) structures and instancing

* **TLAS** over instances (transform + BLAS pointer + per-instance ID/mask/SBT
  offset). **BLAS** over primitives in object space.
* Moving drones, vehicles and rotating radar dishes are instance transform updates.
  Only the TLAS is rebuilt, which takes milliseconds for 10⁵–10⁶ instances on the GPU.
* **Forests:** a few dozen species/age prototypes × millions of instances. Two levels
  are enough if every tree is an instance of a prototype. Multi-level instancing
  (forest patch → trees → leaves) is supported by Embree and OptiX but **not** by
  DXR/Vulkan, which allow only two levels. Flatten to two levels for hardware RT.
* **Per-instance attributes** (species, health, seasonal leaf-on/off for acoustic and
  optical properties) are read in hit shaders via instance ID → attribute row. The
  attribute system's instance domain (see
  [attribute-systems-survey.md](attribute-systems-survey.md)) should be laid out so
  that `instanceCustomIndex` is a direct row index.
* **Instance masks** (8 bits in DXR/Vulkan) let one TLAS serve several query
  populations ("acoustic-relevant", "optical-opaque", "vegetation", "terrain"). Eight
  bits is tight, so plan the bit allocation, or use several TLASes.
* **Precision:** instance transforms are float32 3×4 matrices in all hardware APIs.
  Place TLAS origins per tile in a local frame (see
  [large-world-coordinates-and-tiling.md](large-world-coordinates-and-tiling.md)).

### 2.5 Refit vs rebuild vs update

| Edit | Action | Notes |
|---|---|---|
| rigid motion of an object | update instance transform, rebuild TLAS | cheap |
| deformation without topology change (wind-blown trees, water surface) | **refit** BLAS bounds bottom-up | quality degrades as motion grows; rebuild after N refits or when SAH cost rises by X% |
| topology edit to one object (procedural regeneration) | rebuild that BLAS | tile-sized BLAS bounds the cost |
| insertion/removal of objects | TLAS rebuild, PTLAS partition update, or incremental insertion | PTLAS rebuilds only changed partitions |
| large terrain edit | rebuild terrain tile BLAS / min-max quadtree region | localised |

Dirty tracking should come from the attribute and topology system: each column and
each body has a **version counter**. Acceleration structures record the versions
they were built from and become stale when the counters change.

### 2.6 Robustness and precision

* **Watertight ray–triangle intersection** (Woop, Benthin, Wald 2013) prevents rays
  from slipping through shared edges
  ([JCGT paper](https://jcgt.org/published/0002/01/05/)). For acoustics a leak through
  a wall crack is a large error: energy that should be reflected is transmitted and
  can produce false sources. CPU paths should use watertight tests. Hardware RT
  implementations are specified to avoid double hits and gaps on shared edges of
  triangles in the same BLAS (DXR "watertightness" requirements), but only in fp32
  and only within a BLAS. Seams *between* separately built tiles or objects are the
  modeller's responsibility. This argues for conforming tile boundaries (shared
  vertices) or overlap skirts for terrain.
* **Conservative traversal.** Ize 2013 gives robust BVH traversal with conservative
  box tests ([Ize, *Robust BVH Ray Traversal*, JCGT 2013](https://jcgt.org/published/0002/02/02/)).
* **Double precision.** No hardware RT path supports fp64. A useful hybrid is fp32
  conservative BVH traversal (boxes rounded outward) with fp64 or exact predicates in
  leaves. Kernel-level queries (Boolean preprocessing, point classification) take the
  CPU fp64 path. Bulk simulation queries take the fp32 hardware path in local tile
  frames, where fp32 gives ~1 mm resolution at 8 km from the origin.

## 3. k-d trees

* Historically the fastest structure for static-scene ray tracing (SAH k-d trees,
  Havran 2000), now displaced by BVHs because BVHs refit, bound memory (no primitive
  duplication) and build faster.
* Still the best general tool for **point** queries: k-NN, radius search on static
  point sets. Examples include sensor arrays, lidar clouds, vegetation stems and
  sample points. **nanoflann** (header-only, BSD) is the de facto C++ choice
  ([nanoflann](https://github.com/jlblancoc/nanoflann)).
* For GPU k-NN on moving points, grids/hashing or BVH-over-points (with RT cores used
  for neighbour search, as in RTNN, Zhu 2022) are better
  ([Zhu, *RTNN: Accelerating Neighbor Search Using Hardware Ray Tracing*, PPoPP 2022](https://doi.org/10.1145/3503221.3508409)).

## 4. Octrees, loose octrees, sparse voxel octrees

* **Linear octrees** with Morton codes are easy to build in parallel (Karras 2012
  builds octrees from the same radix tree as LBVH). They suit adaptive sampling,
  meshing and spatial partitioning of work.
* **Loose octrees** (Ulrich 2000, *Game Programming Gems*): each cell's bounds are
  enlarged (typically ×2), so an object is placed in one cell chosen by its size and
  centre. Insertion and removal are O(1)–O(depth) with no splitting of objects. This
  fits the **broadphase for interactive procedural editing**, e.g. "which existing
  objects intersect the new building's footprint?".
* **Sparse voxel octrees** (Laine & Karras 2010) and **SVDAGs** (Kämpe, Sintorn,
  Assarsson 2013) compress binary occupancy extremely well, which matters for
  city-scale "free air space" maps used in coverage analysis
  ([Laine & Karras, *Efficient Sparse Voxel Octrees*](https://research.nvidia.com/publication/2010-02_efficient-sparse-voxel-octrees);
  [Kämpe et al., *High Resolution Sparse Voxel DAGs*](https://doi.org/10.1145/2461912.2462024)).

## 5. R-trees (GIS)

R-trees are BVHs designed for disk pages: high fan-out (tens to hundreds), with node
size equal to a page.

* **Guttman 1984** is the original; its quadratic split is mostly obsolete
  ([Guttman 1984](https://doi.org/10.1145/602259.602266)).
* **R\*-tree** (Beckmann et al. 1990) minimises overlap, margin and area, and uses
  forced reinsertion. It is the default for **dynamic** GIS indexes
  ([Beckmann et al. 1990](https://doi.org/10.1145/93597.98741)).
* **Hilbert R-tree** (Kamel & Faloutsos 1994) orders by the Hilbert value of the
  centre. Packed Hilbert R-trees give near-optimal static indices; **flatbush** is a
  fast JS/C++/Rust implementation
  ([flatbush](https://github.com/mourner/flatbush)).
* **STR (Sort-Tile-Recursive) packing** (Leutenegger et al. 1997) bulk-loads static
  data. GEOS `STRtree`, Shapely and JTS use it
  ([GEOS](https://libgeos.org/)).
* **Boost.Geometry `rtree`** supports linear/quadratic/R* insertion and packing, plus
  k-NN and spatial predicates
  ([Boost.Geometry index](https://www.boost.org/doc/libs/release/libs/geometry/doc/html/geometry/spatial_indexes.html)).
* **PostGIS** uses GiST (an R-tree generalisation) for server-side spatial joins
  ([PostGIS](https://postgis.net/docs/using_postgis_dbmanagement.html#gist_indexes)).

For UberTopo, R-trees index **2D/2.5D source data** before procedural generation: road
centrelines, building footprints, land-cover polygons, parcels and utilities. This is
the input side of the pipeline. The 3D generated geometry uses BVHs. Do not use R-trees
for ray tracing.

## 6. Uniform grids and spatial hashing

* **Uniform grid** with counting-sort construction (cell index → sorted particle
  ranges) is the standard GPU structure for particles (SPH, boids). It is ideal for
  drone-swarm simulation: neighbour queries for collision avoidance and formation, and
  the inverse problem of clustering detections.
* **Spatial hashing** (Teschner et al. 2003) handles unbounded domains with hash
  buckets ([Teschner et al. 2003](https://matthias-research.github.io/pages/publications/tetraederCollision.pdf)).
  **Perfect spatial hashing** (Lefebvre & Hoppe 2006) gives static sparse data with
  O(1) lookups ([Lefebvre & Hoppe 2006](https://hhoppe.com/proj/perfecthash/)).
* **Hierarchical grids** (2–3 levels) adapt to density variation, e.g. a dense swarm
  over a sparse area.
* Grids also suit **sensor-coverage rasters**: per-cell "number of sensors with LOS"
  or "min detection range". Store these as VDB or 2D rasters.

## 7. Terrain: heightfields and quadtrees

Terrain dominates the area of natural environments, and treating it as a generic
triangle mesh wastes memory and accuracy.

* **Min/max (maximum) mipmaps** store, for each quadtree node, the max (and min)
  height over its footprint. Ray traversal descends the quadtree and skips nodes whose
  max is below the ray segment. The result is exact on the bilinear/triangulated
  heightfield, uses tiny memory (+33%) and supports dynamic updates
  ([Tevs, Ihrke, Seidel 2008, *Maximum Mipmaps for Fast, Accurate, and Scalable Dynamic Height Field Rendering*](https://doi.org/10.1145/1342250.1342279)).
* **LOS on DEM.** Bresenham/DDA along the profile with Earth curvature and refraction
  correction for long ranges. Viewshed algorithms (R3, R2, XDraw, Van Kreveld's sweep)
  are covered in [query-types-and-api.md](query-types-and-api.md).
* **Rendering LOD** (CDLOD, Strugar 2009; geometry clipmaps, Losasso & Hoppe 2004) is
  separate from query structures. Queries should hit the raster at a chosen resolution,
  not the rendering mesh.
* **Overhangs, tunnels, bridges, cliffs** cannot be represented by a heightfield.
  Represent them as B-rep bodies stitched into the terrain (with terrain faces
  trimmed), or as VDB SDF patches. The query layer combines both: hit = min(terrain
  hit, BLAS hits).

## 8. Sparse volumes: VDB and HDDA

* **OpenVDB / NanoVDB** (see [attribute-systems-survey.md](attribute-systems-survey.md)
  §7) store region fields: canopy density, temperature, wind, humidity, smoke,
  signed distance.
* **HDDA** (hierarchical DDA, Museth 2014) marches rays through the VDB tree, skipping
  inactive tiles at each level
  ([Museth, *Hierarchical Digital Differential Analyzer for Efficient Ray-Marching in OpenVDB*, SIGGRAPH 2014 talk](https://doi.org/10.1145/2614106.2614136)).
  NanoVDB implements it on the GPU.
* Acoustic uses: ray marching through **vegetation density** to accumulate
  excess attenuation (ISO 9613-2 foliage term, or physically based scattering), and
  through **temperature/wind fields** for curved-ray propagation, where the
  ray-tracer integrates ray equations through the field. Optical: smoke/fog
  extinction for camera detection range.
* **SDF grids** give fast approximate closest-distance queries (one lookup and a
  gradient) for clearance checks, e.g. "minimum distance from drone path to any
  obstacle".

## 9. Non-triangle primitives in a BVH

The BVH is agnostic: leaves can hold anything with a bounding box and an intersect
routine.

* **Curves** (power lines, cables, hair-like vegetation): Embree and OptiX have native
  curve primitives (round/flat linear, Bézier, B-spline, Catmull-Rom). Power lines as
  swept circles of tiny radius are a natural fit.
* **Analytic surfaces:** bound Bézier patches by control-point hulls. Ray intersection
  uses Bézier clipping or Newton iteration seeded by a coarse tessellation. In practice
  most systems tessellate for bulk queries and refine with Newton on the exact surface
  where needed (closest point, exact contact).
* **Implicits/SDFs:** bound with boxes and intersect via sphere tracing (needs a
  Lipschitz bound) or **Harnack tracing** for harmonic functions such as winding
  numbers ([Gillespie, Yang, Botsch, Crane 2024, *Ray Tracing Harmonic Functions*](https://www.markjgillespie.com/Research/harnack-tracing/index.html)).
* **Points/discs/spheres:** native in Embree and OptiX. Useful for point-cloud
  scenes and sensor visibility checks.
* **Hardware RT custom primitives** use intersection shaders (DXR/Vulkan) or custom
  primitive programs (OptiX). They are much slower than built-in triangles, so
  tessellate where possible.

## 10. Choosing the architecture

1. **Primary:** a two-level BVH with one TLAS per *world tile* (local frame) and
   BLASes per object/tile-chunk, with CPU (Embree or tinybvh with fp64 extension) and
   GPU (Vulkan RT) backends behind one interface.
2. **Terrain:** heightfield + max-mipmap quadtree per tile, treated as a custom
   primitive in the TLAS (or tessellated into a BLAS for hardware RT).
3. **Volumes:** NanoVDB grids per tile, ray-marched by HDDA, referenced from region
   attributes.
4. **2D source data:** packed R-trees (STR/Hilbert) and R* for editable layers.
5. **Moving points:** uniform grid/hash rebuilt per step. k-d trees for static
   point sets.
6. **Editing broadphase:** loose octree, or reuse the TLAS with incremental updates.
7. **Versioned invalidation** driven by attribute/topology version counters.

## References

* Meister, Ogaki, Benthin, Doyle, Guthe, Bittner, *A Survey on Bounding Volume Hierarchies for Ray Tracing*, CGF 2021: https://diglib.eg.org/handle/10.1111/cgf142662
* MacDonald & Booth, *Heuristics for ray tracing using space subdivision*, 1990: https://doi.org/10.1007/BF01911006
* Wald, *On fast Construction of SAH-based Bounding Volume Hierarchies*, 2007: https://www.sci.utah.edu/~wald/Publications/2007/ParallelBVHBuild/fastbuild.pdf
* Stich, Friedrich, Dietrich, *Spatial Splits in Bounding Volume Hierarchies*, HPG 2009: https://www.nvidia.com/docs/IO/77714/sbvh.pdf
* Lauterbach et al., *Fast BVH Construction on GPUs*, 2009: https://doi.org/10.1111/j.1467-8659.2009.01377.x
* Karras, *Maximizing Parallelism in the Construction of BVHs, Octrees, and k-d Trees*, HPG 2012: https://research.nvidia.com/publication/2012-06_maximizing-parallelism-construction-bvhs-octrees-and-k-d-trees
* Karras & Aila, *Fast Parallel Construction of High-Quality BVHs*, HPG 2013: https://research.nvidia.com/publication/2013-07_fast-parallel-construction-high-quality-bounding-volume-hierarchies
* Meister & Bittner, *PLOC*, 2018: https://dcgi.felk.cvut.cz/projects/ploc/
* Intel, *PLOC++*: https://www.intel.com/content/www/us/en/developer/articles/technical/ploc-for-bounding-volume.html
* Benthin, Meister, et al., *H-PLOC*, 2024: https://gpuopen.com/download/publications/HPLOC.pdf
* Bittner, Hapala, Havran, *Incremental BVH construction for ray tracing*, 2015: https://doi.org/10.1111/cgf.12581
* Ylitie, Karras, Laine, *Efficient Incoherent Ray Traversal on GPUs Through Compressed Wide BVHs*, HPG 2017: https://research.nvidia.com/publication/2017-07_efficient-incoherent-ray-traversal-gpus-through-compressed-wide-bvhs
* Embree: https://www.embree.org/ (API: https://www.embree.org/api.html)
* tinybvh: https://github.com/jbikker/tinybvh
* AMD Dense Geometry Format: https://gpuopen.com/download/DGF.pdf ; https://gpuopen.com/learn/animating-geometry-with-amd-dgf/
* NVIDIA RTX Mega Geometry Vulkan samples: https://developer.nvidia.com/blog/nvidia-rtx-mega-geometry-now-available-with-new-vulkan-samples
* Microsoft DirectX, Clustered Geometry & PTLAS preview: https://devblogs.microsoft.com/directx/clas-ptlas-preview/
* Woop, Benthin, Wald, *Watertight Ray/Triangle Intersection*, JCGT 2013: https://jcgt.org/published/0002/01/05/
* Ize, *Robust BVH Ray Traversal*, JCGT 2013: https://jcgt.org/published/0002/02/02/
* nanoflann: https://github.com/jlblancoc/nanoflann
* Zhu, *RTNN*, PPoPP 2022: https://doi.org/10.1145/3503221.3508409
* Laine & Karras, *Efficient Sparse Voxel Octrees*, 2010: https://research.nvidia.com/publication/2010-02_efficient-sparse-voxel-octrees
* Kämpe, Sintorn, Assarsson, *High Resolution Sparse Voxel DAGs*, 2013: https://doi.org/10.1145/2461912.2462024
* Guttman, *R-trees*, SIGMOD 1984: https://doi.org/10.1145/602259.602266
* Beckmann et al., *The R*-tree*, SIGMOD 1990: https://doi.org/10.1145/93597.98741
* flatbush: https://github.com/mourner/flatbush
* GEOS: https://libgeos.org/
* Boost.Geometry spatial indexes: https://www.boost.org/doc/libs/release/libs/geometry/doc/html/geometry/spatial_indexes.html
* PostGIS GiST indexes: https://postgis.net/docs/using_postgis_dbmanagement.html#gist_indexes
* Teschner et al., *Optimized Spatial Hashing for Collision Detection of Deformable Objects*, 2003: https://matthias-research.github.io/pages/publications/tetraederCollision.pdf
* Lefebvre & Hoppe, *Perfect Spatial Hashing*, 2006: https://hhoppe.com/proj/perfecthash/
* Tevs, Ihrke, Seidel, *Maximum Mipmaps*, I3D 2008: https://doi.org/10.1145/1342250.1342279
* Museth, *Hierarchical DDA (HDDA)*, SIGGRAPH 2014 talk: https://doi.org/10.1145/2614106.2614136
* Gillespie et al., *Ray Tracing Harmonic Functions* (Harnack tracing), SIGGRAPH 2024: https://www.markjgillespie.com/Research/harnack-tracing/index.html

Note: some DOIs and URLs above were cited from memory without being fetched during
this research pass. The H-PLOC, DGF, RTX Mega Geometry/PTLAS, BVH-survey and FCPW
facts were checked by web search on 2026-11-17.
