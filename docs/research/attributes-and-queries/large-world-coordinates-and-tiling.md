# Large-World Coordinates, Precision, Tiling and Streaming

Part of [attributes-and-queries](README.md). This file covers geospatial reference
frames, float/double precision strategy, tiling schemes (quadtree tiles, 3D Tiles,
S2, H3, CDB) and out-of-core streaming for city-to-region-scale environments. LOD
selection and simplification are in [lod-and-simplification.md](lod-and-simplification.md).

## 1. Scale requirements

The drone-defence use cases span:

* **Site scale:** a facility, campus or stadium, 0.5–5 km across. Sensor networks
  live here, and acoustic and optical localisation needs sub-decimetre consistency.
* **Area scale:** a city district, a border section or a forest, 10–100 km. This
  is the scale for surveys and approach-corridor analysis.
* **Regional/global context:** imported GIS data (OSM, national DEMs, cadastral data)
  in many CRSs, plus GNSS-referenced sensor positions and drone tracks.

Precision requirement, using acoustics as the driver: sound travels 343 m/s, so
1 cm ≈ 29 µs of travel time, while one sample at 48 kHz is ≈ 20.8 µs. TDOA
localisation therefore needs **source–sensor geometry consistent to ~1 cm or
better**. Camera localisation needs angular consistency: 1 mrad at 1 km is 1 m.

## 2. Geodetic frames

* **Geodetic coordinates** (φ, λ, h) on the **WGS 84 ellipsoid** (a = 6 378 137 m,
  1/f = 298.257223563). They are good for interchange and terrible for geometry
  (non-Euclidean, singular at the poles).
* **ECEF** (Earth-centred, Earth-fixed, X/Y/Z in metres) is a global Cartesian frame.
  Coordinates are ~6.4×10⁶ m, so float32 resolution is ~0.5 m (see §3), and double is
  required.
* **Local tangent planes, ENU/NED**, are a rigid transform of ECEF (rotation +
  translation) at an origin. Distances and angles are exact (no projection
  distortion). The plane departs from the ellipsoid by d²/2R: about 7.8 cm at 1 km and
  7.8 m at 10 km. Geometry remains exact, because ENU is just a rotated ECEF, but
  "up" at a point 10 km away is tilted ~0.09° from the frame's +Z. Physics that
  depends on gravity direction (refracting atmosphere, water surfaces, "level"
  roofs) should use **local up = ellipsoid normal at the point**, not frame +Z, at
  area scale.
* **Projected CRSs** (UTM, national grids, Web Mercator) map the ellipsoid onto a
  plane with distortion. UTM's scale factor is 0.9996 on the central meridian and
  rises to ≈1.0010 near zone edges. That is −0.4 to +1 m per km, i.e. 0.04–0.1% length
  error, which is *larger than the acoustic budget over a 1 km baseline*. Web Mercator
  is far worse away from the equator. **Never do 3D geometry or simulation in a
  projected CRS.** Use projected CRSs only for import/export and 2D map display, and
  transform to ENU/ECEF with **PROJ** ([PROJ](https://proj.org/)).
* **Heights.** Ellipsoidal height h (GNSS) differs from orthometric height H (above
  the geoid, used by most DEMs and building heights) by the geoid undulation N
  (EGM2008, roughly −107 m to +86 m globally). Mixing them puts sensors metres above or
  below the terrain. Every height attribute must declare its vertical datum
  ([NGA EGM2008](https://earth-info.nga.mil/index.php?dir=wgs84&action=wgs84);
  [PROJ geoid grids](https://cdn.proj.org/)).
* **Datum realisations and epochs.** WGS 84 realisations, ITRF2020, ETRS89 and NAD83
  differ by up to ~1–2 m, and plate motion is cm/yr. This is irrelevant for synthetic
  environments but relevant when importing surveyed sensor positions next to older
  GIS data. Store CRS + epoch metadata on imported datasets.

## 3. Floating-point precision

Unit in the last place (ulp) for float32 (24-bit significand) at a coordinate of
magnitude x is ≈ x · 2⁻²³:

| Distance from origin | float32 ulp | float64 ulp |
|---|---|---|
| 100 m | 7.6 µm | ~1.4×10⁻¹⁴ m |
| 1 km | 61 µm | ~1.1×10⁻¹³ m |
| 8 km | 0.98 mm | ~9×10⁻¹³ m |
| 65 km | 7.8 mm | ~7×10⁻¹² m |
| 1 000 km | 0.12 m | ~1.1×10⁻¹⁰ m |
| 6 400 km (ECEF) | 0.5 m | ~9×10⁻¹⁰ m |

Arithmetic loses several ulps (ray–triangle intersection, transforms), so the
working rule is: **float32 is adequate (≤ mm) within ~8 km of the local origin**. This
holds for GPU ray tracing, rendering and per-tile simulation. Kernel geometry
(B-rep construction, Booleans, tolerance handling) uses float64 everywhere. Positions
are stored as float64 relative to a *site frame*, even though float64 ECEF would also
suffice. A site frame keeps magnitudes small, so float32 conversions are cheap and
safe.

### 3.1 Established techniques

* **Relative-to-center (RTC)**: geometry stored as float32 offsets from a per-tile
  float64 centre (glTF `CESIUM_RTC`, 3D Tiles tile transforms)
  ([CESIUM_RTC](https://github.com/CesiumGS/glTF/tree/main/extensions/1.0/Vendor/CESIUM_RTC)).
* **Relative-to-eye (RTE) / GPU RTE**: subtract the camera position in double on the
  CPU, or in shaders using high/low float pairs (double emulation), so vertices near
  the camera have small magnitudes
  ([Cesium, *Graphics Tech in Cesium – Stack*](https://cesium.com/blog/2015/05/26/graphics-tech-in-cesium-stack/);
  [`czm_translateRelativeToEye`](https://cesium.com/downloads/cesiumjs/releases/b28/Documentation/czm_translateRelativeToEye.html);
  Ohlarik 2008, *Precisions, Precisions*).
* **Floating origin**: periodically re-centre the world on the camera or player
  (Unity community practice, Kerbal Space Program, Godot double builds).
* **Engine-level double precision**: Unreal Engine 5 *Large World Coordinates* uses
  doubles for transforms and positions engine-wide, with float GPU data made
  tile-relative ([UE5 Large World Coordinates](https://dev.epicgames.com/documentation/en-us/unreal-engine/large-world-coordinates-in-unreal-engine-5)).
  Godot supports a `precision=double` build.

For UberTopo, rendering-side tricks (RTE) matter only for the viewer. The **kernel and
simulation** need the frame hierarchy in §4, so that each query runs in a frame where
float32 is safe and conversions are exact translations.

## 4. Recommended frame hierarchy

```
ECEF (WGS 84, float64)                     # global; for import/export and inter-site
 └─ Site frame S (ENU at site origin, float64 rigid transform to ECEF)
     └─ Tile frame T(i,j[,k]) = S translated by (i·L, j·L, k·L)   # integer tile key, pure translation
         └─ Object/instance frame (float64 rigid transform within tile; float32 OK for BLAS)
```

Properties:

* **Tile frames are pure translations of the site frame** by integer multiples of the
  tile size L (a power of two in metres, e.g. 256 m–2 km). Cross-tile transforms are
  exact (integer offsets, exactly representable), so queries crossing tile
  boundaries just offset rays. Rotations between neighbouring ENU frames, which a
  naive per-tile-ENU scheme would introduce, are avoided.
* **Site extent**: one ENU site frame is suitable up to ~50–100 km, beyond which the
  tilt of "up" and the ellipsoid drop (≈200–800 m) make a tangent plane awkward,
  though still exact. For larger areas, use several sites, each with its own ENU frame,
  with ECEF as the bridge, or an ECEF-aligned tile scheme for the whole area.
* **Gravity-aware attributes**: "up" for physics comes from a computed attribute,
  the ellipsoid normal at a point, so terrain and water stay physically level across
  a large site.
* **Every positional attribute declares its frame** in the schema (`frame: tile`,
  `frame: object`, `frame: ecef`). Vectors and normals transform by role (see
  [attribute-systems-survey.md](attribute-systems-survey.md) §10).
* **Curves spanning tiles** (roads, power lines, rivers) are stored once in the site
  frame (float64) and *clipped/instanced per tile* at tessellation time. Their
  persistent IDs are global.
* **Time** is also a coordinate: drone tracks and sensor data need a time scale
  (GPS/TAI/UTC, with leap seconds) declared in metadata. This matters for TDOA and is
  easily forgotten.

## 5. Tiling schemes

| Scheme | Shape | Hierarchy | Exact containment | Distortion | Good for |
|---|---|---|---|---|---|
| **Site-frame quadtree/octree** (power-of-two L) | squares/cubes in ENU | 4/8-ary | yes | none locally | **internal storage, LOD, queries** |
| Geodetic quadtree (EPSG:4326, TMS) | lat/lon rectangles | 4-ary (2 roots) | yes | tiles shrink with latitude | global terrain (quantized-mesh) |
| Web Mercator XYZ (EPSG:3857) | squares in Mercator | 4-ary | yes | severe at high latitudes | 2D map display only |
| OGC 2D Tile Matrix Set | any CRS grid | per-matrix | yes | CRS-dependent | standard 2D tiling metadata ([OGC TMS](https://docs.ogc.org/is/17-083r4/17-083r4.html)) |
| **S2 cells** | spherical quads (cube projection) | 4-ary, 30 levels, 64-bit Hilbert-ordered IDs | yes | area ratio within a level ≈2× | global indexing, catalogs, cross-site lookup ([S2 Geometry](https://s2geometry.io/)) |
| **H3** | hexagons (+12 pentagons) | aperture 7, 16 resolutions | **no** (children only approximately cover the parent) | low | analytics, aggregation, heatmaps ([H3](https://h3geo.org/)) |
| Geohash | lat/lon Z-order rectangles | 32-ary strings | yes | lat-dependent; poor locality at cell edges | legacy keys |
| OGC DGGS | various | various | various | equal-area options | standards-based analytics ([OGC DGGS](https://docs.ogc.org/as/20-040r3/20-040r3.html)) |

Recommendations:

* **Internal tiling**: site-frame quadtree (2D in plan, with an optional vertical
  octree split for tall/underground content). It is exact, translation-only, and
  matches heightfield rasters and the TLAS-per-tile design.
* **Global keys**: tag each site and each tile with the **S2 cell ID** covering it for
  catalogues and cross-dataset lookup. S2 has exact hierarchical containment and
  64-bit IDs.
* **Analytics outputs** (coverage statistics, risk heatmaps for reports): **H3** is
  fine, because its approximate containment does not matter for aggregation. Never use
  H3 for geometry LOD.

## 6. 3D Tiles, I3S and CDB

### 3D Tiles (OGC Community Standard, 1.1)

* A tileset is a tree of tiles with **bounding volumes** (`box`, `region` in
  lat/lon/h radians, `sphere`, plus S2 via `3DTILES_bounding_volume_S2`),
  **geometricError** (metres of error if this tile is rendered instead of its
  children), **refine** `REPLACE` (HLOD) or `ADD` (progressive), and tile transforms
  (RTC).
* **Implicit tiling** (core in 1.1): uniform quadtree/octree subdivision with
  **subtree files** holding Morton-ordered availability bitstreams for tiles,
  content and child subtrees. It avoids huge explicit JSON trees.
* Content is glTF 2.0, with `EXT_mesh_features` (feature IDs per vertex, texel or
  instance) and `EXT_structural_metadata` (typed property tables and textures),
  replacing 1.0's b3dm/i3dm/pnts batch tables.
* References: [OGC 3D Tiles 1.1](https://docs.ogc.org/cs/22-025r4/22-025r4.html);
  [3d-tiles GitHub](https://github.com/CesiumGS/3d-tiles);
  [implicit tiling explainer (Geodan)](https://research.geodan.nl/3d-tiles-1-1-implicit-tiling/);
  [S2 bounding volume extension](https://github.com/CesiumGS/3d-tiles/tree/main/extensions/3DTILES_bounding_volume_S2).

3D Tiles is a **visualisation streaming** format. Its metadata model is excellent, but
it has no topology, no exact geometry and only view-dependent LOD semantics
(`geometricError` is a screen-space-error driver). UberTopo should **export** to 3D
Tiles for viewers (CesiumJS, Cesium for Unreal/Unity/Omniverse) and for interoperability
with government/defence data such as Google Photorealistic 3D Tiles or national
mesh datasets. It should **import** 3D Tiles as reference/context geometry, but not
use it as its internal store.

### I3S

Esri's Indexed 3D Scene Layers (OGC Community Standard) is similar in purpose: a node
tree with LOD, attribute buffers per feature, and mesh/point/building-scene-layer
profiles. The Building Scene Layer carries BIM-like discipline/category structure
([OGC I3S](https://www.ogc.org/standards/i3s/)). It is an alternative export target.

### OGC CDB

**CDB** originated as a modelling-and-simulation synthetic environment database
(flight simulators, military training). It is a tiled, LOD-structured, file-based
repository of terrain elevation, imagery, vector features and 3D models, organised by
geodetic tiles and LOD levels, designed for **runtime simulation consumption** rather
than web visualisation. CDB 2.0 work modernises the encodings, e.g. GeoPackage and
glTF ([OGC CDB](https://www.ogc.org/standards/cdb/)). Since UberTopo targets defence
test and evaluation, CDB (and related synthetic-environment efforts such as the US
Army's One World Terrain, which uses 3D Tiles/CDB-derived formats) is a likely
**interchange requirement** with simulators. Worth confirming with the project owner.

## 7. Streaming and out-of-core

Rendering streams by camera. **Simulation streams by query footprint**, and that
changes the design:

* **Residency is query-driven.** A batch of 10⁶ LOS rays from 50 sensors defines a
  footprint (union of segments). The scheduler computes the tiles and LOD levels
  needed (from the batch's LOD policy) and **blocks or defers** until they are
  resident. Rendering degrades gracefully. Simulation must not silently use a coarser
  tile, or results become irreproducible.
* **Prefetch** using the simulation's temporal structure: drone trajectories are
  known or predicted, so tiles along them can be prefetched.
* **Memory budget per representation**: exact B-rep tiles (CPU, for editing), BLAS
  tiles (GPU, for RT), raster tiles (terrain), VDB tiles (volumes). Each has an LRU
  with priorities.
* **Immutable, content-addressed tile blobs**: each tile version is a hash-named
  blob (topology + attribute pages + geometry arena chunks). This fits the COW/MVCC
  design ([query-types-and-api.md](query-types-and-api.md) §2.4): edits produce new
  blobs, old versions remain for pinned simulations, and blobs can be cached and
  deduplicated across machines and runs. It is the Git/IPFS model applied to tiles.
* **Compression**: meshoptimizer encoders / Draco for transport and storage; DGF or
  vendor cluster formats for in-memory RT; zstd for attribute pages; NanoVDB for
  volumes. Attribute pages compress very well with dictionary/RLE (see
  [attribute-systems-survey.md](attribute-systems-survey.md) §9).
* **Memory-mapped tile files** with page-aligned attribute columns allow zero-copy
  loading. Arrow IPC / Feather layouts are a good model.

## 8. Features spanning tiles and tile seams

Tiling cuts geometry. Consequences:

* **Global feature IDs vs tile-local elements.** A road, river, forest or large
  building spanning tiles has one **global persistent feature ID**. Each tile holds
  *parts* with tile-local element handles and a `feature_id` attribute. Queries report
  both. Analytics aggregate by feature ID.
* **Artificial edges.** Faces cut at tile boundaries get edges that do not exist in
  reality. These edges must be **flagged** (`topo.tile_seam = true`) so that:
  * diffraction-edge detection ignores them, otherwise every tile boundary across a
    flat field becomes a false diffracting wedge;
  * simplification does not move them (border locking, so adjacent tiles stay
    watertight);
  * region/face adjacency across tiles is stitched via **seam pairing records**
    (edge in tile A ↔ edge in tile B).
* **Watertight seams for ray queries.** Adjacent tiles at the *same* LOD must share
  identical seam vertices (bitwise, in site coordinates; tile-frame offsets are exact
  integers, so translated coordinates stay bitwise equal if L is a power of two and
  coordinates are within range). Across LOD transitions, use skirts or stitching
  strips, or simply require queries to use one LOD per footprint.
* **Alternatively, overlap regions**: store each tile with a small apron of
  neighbour geometry (a "halo") so that local queries near borders do not need the
  neighbour tile. This is a common approach in distributed simulation and terrain
  processing.

## 9. Summary recommendations

1. Kernel coordinates are float64 in a **site ENU frame**. GPU/simulation coordinates
   are float32 in **tile frames that are integer translations** of the site frame
   (tile size a power of two ≤ ~8 km for mm precision).
2. Never compute in projected CRSs. Use PROJ at import/export. Store CRS, vertical
   datum and epoch metadata on all imported data.
3. Use an **ellipsoid-normal "up"** attribute for gravity-dependent physics at area
   scale, and curved LOS for long ranges.
4. Use a **site-frame quadtree** for internal tiling, **S2 IDs** for global keys, and
   **H3** only for analytics output.
5. Export **3D Tiles 1.1** (with `EXT_structural_metadata`) for visualisation. Treat
   **CDB/OWT** as a likely simulator interchange target (to confirm).
6. Make streaming **query-footprint-driven and blocking** for simulation, with
   content-addressed immutable tile blobs that fit the snapshot/COW model.
7. Give cross-tile features **global persistent IDs**, flag **tile-seam edges**, and
   keep **seams bitwise watertight** within an LOD.

## References

* PROJ: https://proj.org/ ; PROJ data/geoid grids: https://cdn.proj.org/
* NGA, WGS 84 and EGM2008: https://earth-info.nga.mil/index.php?dir=wgs84&action=wgs84
* CESIUM_RTC glTF extension: https://github.com/CesiumGS/glTF/tree/main/extensions/1.0/Vendor/CESIUM_RTC
* Cesium, *Graphics Tech in Cesium – Stack* (GPU RTE): https://cesium.com/blog/2015/05/26/graphics-tech-in-cesium-stack/
* Cesium `czm_translateRelativeToEye`: https://cesium.com/downloads/cesiumjs/releases/b28/Documentation/czm_translateRelativeToEye.html
* Unreal Engine 5, Large World Coordinates: https://dev.epicgames.com/documentation/en-us/unreal-engine/large-world-coordinates-in-unreal-engine-5
* OGC Two Dimensional Tile Matrix Set: https://docs.ogc.org/is/17-083r4/17-083r4.html
* S2 Geometry: https://s2geometry.io/
* H3: https://h3geo.org/
* OGC DGGS Abstract Specification: https://docs.ogc.org/as/20-040r3/20-040r3.html
* OGC 3D Tiles 1.1: https://docs.ogc.org/cs/22-025r4/22-025r4.html ; https://github.com/CesiumGS/3d-tiles
* Geodan, *3D Tiles 1.1 implicit tiling*: https://research.geodan.nl/3d-tiles-1-1-implicit-tiling/
* 3DTILES_bounding_volume_S2: https://github.com/CesiumGS/3d-tiles/tree/main/extensions/3DTILES_bounding_volume_S2
* OGC I3S: https://www.ogc.org/standards/i3s/
* OGC CDB: https://www.ogc.org/standards/cdb/
* Apache Arrow IPC format: https://arrow.apache.org/docs/format/Columnar.html#serialization-and-interprocess-communication-ipc
