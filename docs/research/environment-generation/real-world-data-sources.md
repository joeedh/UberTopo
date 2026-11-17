# Real-world data ingestion and procedural fill-in

Part of the [environment-generation research set](README.md). Domain-specific sources also appear
in each topic file; this file covers the main datasets, formats and the conflation and fill-in
strategy.

Research status: November 2026. Overture, Google Open Buildings, GlobalBuildingAtlas, FABDEM,
City3D, Building3D, OpenUSD, CGAL and CityGML/CityJSON LOD facts were checked by web search in
this session. Other items are from prior knowledge and are marked *(verify)* where uncertain.
**Licensing details change; check each licence before shipping data.**

---

## 1. Principle: real data is the skeleton, procedural generation is the flesh

For a site survey, real geometry must win wherever it exists and is accurate. Procedural
generation fills **missing attributes** (roof shape, facade, openings, interiors, species) and
**missing resolution** (amplifying a 30 m DEM to 1 m), while staying **consistent with the
constraints** the data imposes (footprint, height, road centreline, canopy height). Every entity
records its provenance and confidence, so the simulation can estimate how much its results depend
on invented detail. That *sensitivity analysis* (re-run with different procedural seeds) is a
cheap and important feature for vulnerability surveys.

---

## 2. Vector map data

### 2.1 OpenStreetMap
- Licence ODbL (share-alike on derived *databases*; produced works need attribution).
- **Buildings**: `building=*` (type: house, apartments, commercial, industrial, warehouse, church,
  mosque, military…), `height`, `min_height`, `building:levels`, `roof:shape` (flat, gabled,
  hipped, pyramidal, skillion, gambrel, mansard, dome, onion, round, half-hipped…), `roof:height`,
  `roof:direction`, `roof:orientation`, `roof:levels`, `building:material`, `roof:material`,
  `building:colour`; **Simple 3D Buildings** with `building:part` for multi-part buildings
  (https://wiki.openstreetmap.org/wiki/Simple_3D_Buildings). Coverage of footprints is good in
  Europe/N. America and patchy elsewhere; 3D tags are sparse except in well-mapped cities.
- **Roads**: `highway=*` classification, `lanes`, `width`, `surface`, `oneway`, `turn:lanes`,
  `sidewalk*`, `cycleway*`, `parking:*`, `bridge`, `tunnel`, `layer`, `maxspeed`.
  Centrelines only, no explicit junction geometry. See [road-networks.md](road-networks.md).
- **Other**: `landuse`, `natural`, `waterway`, `power`, `barrier`, `man_made`, `aeroway`,
  `railway`, `amenity`; trees as nodes.
- Tooling: osmium (https://osmcode.org/osmium-tool/), Geofabrik extracts
  (https://download.geofabrik.de), Overpass API, osm2streets, OSMnx.

### 2.2 Overture Maps Foundation
- Released monthly as GeoParquet on S3/Azure. Themes: addresses, base (land, water, land use,
  infrastructure), buildings, divisions, places, transportation. Schema at v1.15 by Dec 2025;
  2026 releases continue (https://docs.overturemaps.org).
- **Buildings**: conflation of OSM + Microsoft ML + Google Open Buildings + Esri community +
  national sources (e.g. Spain IGN added ~13M buildings in 2025). `building` and `building_part`
  types with height, num_floors, roof shape where known. Many are ML-derived with lower footprint
  precision (per the Overture buildings guide, https://docs.overturemaps.org/guides/buildings).
- **GERS** (Global Entity Reference System, GA June 2025): stable IDs across releases, plus bridge
  files to sources. **Very useful for UberTopo**: use GERS IDs as the seed for deterministic
  procedural detail, so the same building gets the same facade across data updates.
- Transportation theme: segments + connectors (a topologically cleaner graph than raw OSM).
- Licences per theme (buildings ODbL, as derived from OSM; others CDLA-Permissive) *(verify per
  release)*.

### 2.3 ML building footprints and heights
| Dataset | Content | Notes |
|---|---|---|
| Microsoft Global ML Building Footprints (https://github.com/microsoft/GlobalMLBuildingFootprints) | ~1.4B footprints, some with heights | ODbL; regularised polygons |
| Google Open Buildings v3 (https://sites.research.google/gr/open-buildings/) | ~1.8B footprints, Africa/S&SE Asia/LatAm | Confidence score per polygon |
| Google Open Buildings 2.5D Temporal | Presence/count/height rasters 2016–2023, effective ~4 m, height MAE ~1.5 m | Global South; uncalibrated confidence |
| GlobalBuildingAtlas (TUM, ESSD 2025; https://arxiv.org/abs/2506.04106) | 2.75B polygons; heights from PlanetScope at 3 m; 2.68B LoD1 models | Continental RMSE 1.5–8.9 m |
| EUBUCCO (https://eubucco.com) | ~200M EU buildings with height/age/type | Harmonised national sources |
| 3DBAG (https://3dbag.nl) | All Dutch buildings at LoD1.2/1.3/2.2 from national LiDAR | Reference-quality open LoD2.2 |
| PLATEAU (Japan, https://www.mlit.go.jp/plateau/) | CityGML LOD1–LOD3 for many Japanese cities | Some LOD3 with openings |
| National LoD2 programmes (e.g. German states' LoD2 open data, Swiss swissBUILDINGS3D) | Roof-shaped models | Open in many EU countries |

### 2.4 Parcels, land use, population
See [urban-layout.md](urban-layout.md): Regrid (US parcels), Urban Atlas, GHSL, WorldPop.

---

## 3. 3D city model standards

- **CityGML 3.0** (OGC 2021, https://www.ogc.org/standards/citygml/): semantic city model
  (Building, BuildingPart, thematic surfaces, openings, Transportation with lanes/sections,
  Vegetation, WaterBody, Relief, CityFurniture, Tunnel, Bridge, Construction). 3.0 reworked the
  LOD concept (interiors at any LOD, LOD4 removed) and added Space/Boundary concepts,
  versioning, dynamizers (time series) and point-cloud references. Heavy XML encoding.
- **CityJSON 2.0** (https://www.cityjson.org): compact JSON encoding of the CityGML data model,
  with CityJSONSeq for streaming. It supports the refined LODs (e.g. "2.2"). Its geometry model —
  indexed vertex list, MultiSurface/Solid/CompositeSolid, *semantic surfaces per face* — is a good
  **import/export target and a model of the minimum semantics we need**. Tools: cjio, CityJSON
  loader for QGIS, val3dity validator (https://github.com/tudelft3d/val3dity), which checks ISO
  19107 solid validity and is useful for validating our own output.
- **Refined LODs**: Biljecki, Ledoux & Stoter 2016 (16 LODs, 0.0–3.3;
  https://3d.bk.tudelft.nl/hledoux/pdfs/16_ceus_lod_specs.pdf). Adopt this vocabulary.

## 4. BIM: IFC
- **IFC 4.3** (ISO 16739-1:2024, https://technical.buildingsmart.org/standards/ifc/) adds
  infrastructure: IfcAlignment (with clothoid and other transition curves, relevant to the curve
  work), IfcRoad, IfcRailway, IfcBridge, IfcMarineFacility.
- Geometry: B-rep, swept solids, CSG, tessellations. The semantic richness (IfcWall, IfcWindow,
  IfcSpace, IfcStair, materials, opening relationships IfcRelVoidsElement/IfcRelFillsElement)
  maps directly onto our attributes. **The IfcOpeningElement → void → filling pattern is a
  useful model** for how windows should relate to walls in our B-rep.
- Tools: IfcOpenShell (https://ifcopenshell.org, LGPL; uses OpenCASCADE for geometry), web-ifc.
- Reality: IFC is available only for specific (new, large, government) sites. Model quality varies
  widely; georeferencing (IfcMapConversion) is often missing or wrong.

---

## 5. Point clouds (LiDAR, photogrammetry)

### 5.1 Formats and tools
- **LAS 1.4** (ASPRS, https://www.asprs.org/divisions-committees/lidar-division/laser-las-file-format-exchange-activities)
  with standard classification codes: 1 unclassified, 2 ground, 3/4/5 low/medium/high vegetation,
  6 building, 7 low noise, 9 water, 10 rail, 11 road surface, 13 wire-guard, 14 wire-conductor,
  15 transmission tower, 17 bridge deck, 18 high noise. **Wire classes 13–15 are directly useful
  for power-line extraction.**
- **LAZ** (lossless compression), **COPC** (Cloud-Optimized Point Cloud: LAZ with an octree for
  HTTP range reads, https://copc.io), EPT (Entwine Point Tiles).
- **PDAL** (https://pdal.io): pipelines for filtering, ground classification (SMRF, CSF), DTM/DSM
  rasterisation, HAG (height above ground). **lastools**, CloudCompare, Open3D.
- Sources: USGS 3DEP (QL2 or better, ≥ 2 pts/m², much of CONUS, https://www.usgs.gov/3d-elevation-program),
  national ALS programmes (NL AHN 4/5, England EA LiDAR, Denmark, Switzerland swissSURFACE3D,
  France LiDAR HD, Spain PNOA LiDAR, Poland…), OpenTopography (https://opentopography.org).

### 5.2 Building reconstruction from LiDAR
- **PolyFit** (Nan & Wonka, ICCV 2017, https://github.com/LiangliangNan/PolyFit): detect planes →
  intersect into candidate faces → select a watertight manifold subset by binary optimisation
  (fitting + coverage + complexity). Robust and compact. Needs good plane coverage (walls are
  missing in ALS).
- **City3D** (Huang, Stoter, Peters, Nan; Remote Sensing 2022, https://arxiv.org/abs/2201.10276):
  extends PolyFit to airborne data by **inferring vertical walls** from roof boundaries (optimal
  transport polylines) and adding roof-preference energy and topology constraints. Releases
  a dataset of about 20k buildings. Code at github.com/yidahuang/City3D *(verify)*.
- **Kinetic Shape Reconstruction** (Bauchet & Lafarge, ACM TOG 2020,
  https://doi.org/10.1145/3376918): kinetic polygon propagation for compact polyhedral
  partitioning. In CGAL as "Kinetic Space Partition / Kinetic Surface Reconstruction" (CGAL 6.0)
  *(verify)*.
- **3DBAG pipeline** (geoflow / *roofer*, https://github.com/3DBAG/roofer *(verify)*): production
  LoD1.2–2.2 from footprints + AHN LiDAR; open source. **The most production-proven pipeline;
  study it first.**
- **Learned**: Building3D benchmark (ICCV 2023, ~160k buildings with roof wireframes, Tallinn;
  https://openaccess.thecvf.com/content/ICCV2023/papers/Wang_Building3D_A_Urban-Scale_Dataset_and_Benchmarks_for_Learning_Roof_Structures_ICCV_2023_paper.pdf);
  Point2Roof, Points2Model (IJDE 2025), LOD2-Former (ISPRS Annals 2026, LiDAR + imagery →
  roof wireframes). They are better at messy roofs but still miss edges. Treat them as
  *roof-structure proposers* feeding a PolyFit-style selection that guarantees validity.
- **Hybrid with procedural**: fit **roof-type templates** (straight-skeleton gable/hip parameters
  per part) to LiDAR. This gives clean, semantic LOD2 with few parameters, and falls back to
  plane arrangement for complex roofs. BigSUR (Kelly 2017) is the reference for fitting
  procedural models to noisy data.

### 5.3 Other LiDAR uses
Canopy height models and individual tree detection (vegetation placement); power-line extraction
(classes 14/15, catenary fitting); terrain DTM; road surface and kerb extraction (mobile LiDAR).

---

## 6. Elevation (DEMs)

| Dataset | Type | Resolution | Notes |
|---|---|---|---|
| SRTM v3 / NASADEM | DSM (C-band, 2000) | 30 m | 60°N–56°S; voids filled |
| **Copernicus DEM GLO-30/GLO-90** | **DSM** (X-band TanDEM-X, 2011–2015) | 30/90 m | Best global DSM; free; includes trees/buildings |
| **FABDEM** (Hawker et al. 2022, https://doi.org/10.1088/1748-9326/ac4d4f) | Bare-earth DTM from GLO-30 (random forest removal of trees/buildings) | 30 m | MAE forest 5.15→2.88 m, urban 1.61→1.12 m; non-commercial licence *(verify)* |
| ALOS AW3D30 | DSM (optical) | 30 m | Alternative |
| **USGS 3DEP** | DTM from LiDAR | 1 m (1/3″ seamless 10 m) | US |
| National LiDAR DTMs | DTM | 0.5–2 m | Many EU countries, open |
| GEBCO / ETOPO / EMODnet | Bathymetry + topography | 15″ / ~100 m | See [water.md](water.md) |

**Important:** a DSM includes canopy and roofs. Using GLO-30 as ground puts the ground on top of
forests, which is fatal for acoustic ground-effect and LOS work. Always use a DTM (FABDEM or
national) for ground, and model canopy/buildings separately.

---

## 7. Land cover and imagery

- **ESA WorldCover** 10 m, 2020 and 2021, 11 classes (tree cover, shrubland, grassland, cropland,
  built-up, bare/sparse, snow/ice, permanent water, herbaceous wetland, mangroves, moss/lichen),
  CC-BY 4.0 (https://esa-worldcover.org).
- **Dynamic World** (Google/WRI, near-real-time 10 m class probabilities, https://dynamicworld.app).
- CORINE (EU, 100 m/MMU 25 ha), NLCD/Annual NLCD (US 30 m), Copernicus HRL (imperviousness, tree
  cover density, grassland, water & wetness).
- Imagery: Sentinel-2 (10 m), NAIP (US, 0.6 m), national orthophotos (often 0.1–0.25 m, open in
  many EU countries). Use them for texturing, facade colour priors, and as input to ML extraction.

---

## 8. 3D delivery formats

- **glTF 2.0** (Khronos; ISO/IEC 12113:2022, https://www.khronos.org/gltf/): runtime mesh/material
  format; EXT_mesh_features / EXT_structural_metadata (from Cesium) carry per-feature attributes.
  Good *output* format for visualisation; not a CAD/B-rep format.
- **3D Tiles 1.1** (OGC Community Standard, https://github.com/CesiumGS/3d-tiles): hierarchical
  LOD (bounding volume tree with geometric error), implicit tiling (quadtree/octree), glTF
  content, metadata. **Our LOD streaming to viewers should emit 3D Tiles.** The geometric-error
  concept is a good model for our own LOD bookkeeping.
- **Google Photorealistic 3D Tiles** (https://developers.google.com/maps/documentation/tile/3d-tiles):
  photogrammetric meshes of many cities, visually excellent. But the meshes are unsegmented,
  non-watertight and texture-baked, **and the Map Tiles API terms restrict caching, offline use
  and extraction/derivative geometry** *(verify current terms)*. Usable as a live visual
  backdrop in a viewer. Likely **not usable as a geometry source** for simulation.
- **Cesium ion / Bing / other photogrammetry**: similar caveats.
- **I3S** (Esri / OGC Community Standard): scene layers, similar to 3D Tiles.
- **OpenUSD**: AOUSD released **Core Specification 1.0 on 17 Dec 2025**
  (https://aousd.org; Linux Foundation press release
  https://www.linuxfoundation.org/press/alliance-for-openusd-announces-core-specification-1.0-the-universal-language-for-building-3d-worlds).
  It is the composition and scene-description standard across NVIDIA Omniverse, Apple, Pixar and
  Autodesk. Its layering/composition (references, payloads, variants for LOD and seasons) is an
  excellent **interchange and scenario-composition** layer, e.g. "base city + scenario overrides".
  The geometry schemas are mesh/subdiv/curves/points (with NURBS patches and curves), not B-rep
  topology. Good to *export* to; not a kernel representation.

---

## 9. Conflation and fill-in pipeline (recommended)

1. **Area of interest** → fetch tiles: DTM (best available), DSM, land cover, OSM/Overture
   vectors, building footprints/heights (best available), canopy height, hydrography, LiDAR if
   present.
2. **Georeferencing**: one local projected CRS per scene (e.g. UTM or a local transverse Mercator
   / ENU tangent plane at the site). Kernel coordinates in metres, double precision with a scene
   origin. Keep the transform as metadata. Vertical datum (EGM2008 geoid vs. ellipsoid) must be
   explicit: DEMs mix datums.
3. **Conflate**: deduplicate building sources (Overture already does this), snap road centrelines
   to imagery/LiDAR if available, reconcile water polygons with DTM (hydro-flatten, then burn).
4. **Attribute inference** (*procedural priors*): height from levels ← region typology prior;
   roof shape from region/typology/footprint shape priors (or from LiDAR fitting); facade style
   from region/era; WWR from typology; tree species from biome + canopy height; lanes from road
   class + width.
5. **Generate geometry** at requested LODs using the topic generators. Seed every entity from its
   stable source ID (GERS, OSM ID + version, or a tile-local hash).
6. **Consistency checks**: buildings on terrain (foundations/plinths), roads conformed to terrain,
   trees not inside buildings or roads, water surfaces level, no wire through solids.
7. **Provenance and confidence** on every face/entity; uncertainty-aware outputs (e.g. building
   height ± σ) to support Monte-Carlo sensitivity runs.

---

## References

- OSM Simple 3D Buildings — https://wiki.openstreetmap.org/wiki/Simple_3D_Buildings ; roof:shape — https://wiki.openstreetmap.org/wiki/Key:roof:shape
- Overture docs & releases — https://docs.overturemaps.org ; buildings guide — https://docs.overturemaps.org/guides/buildings ; 2025-12-17 release notes — https://docs.overturemaps.org/blog/2025/12/17/release-notes/
- Microsoft Global ML Building Footprints — https://github.com/microsoft/GlobalMLBuildingFootprints
- Google Open Buildings — https://sites.research.google/gr/open-buildings/ ; 2.5D Temporal — https://developers.google.com/earth-engine/datasets/catalog/GOOGLE_Research_open-buildings-temporal_v1
- GlobalBuildingAtlas — https://arxiv.org/abs/2506.04106 ; ESSD — https://essd.copernicus.org/articles/17/6647/2025/
- 3DBAG — https://3dbag.nl ; PLATEAU — https://www.mlit.go.jp/plateau/ ; EUBUCCO — https://eubucco.com
- OGC CityGML 3.0 — https://www.ogc.org/standards/citygml/ ; CityJSON — https://www.cityjson.org ; val3dity — https://github.com/tudelft3d/val3dity
- Biljecki et al. refined LODs — https://3d.bk.tudelft.nl/hledoux/pdfs/16_ceus_lod_specs.pdf
- IFC — https://technical.buildingsmart.org/standards/ifc/ ; IfcOpenShell — https://ifcopenshell.org
- ASPRS LAS — https://www.asprs.org/divisions-committees/lidar-division/laser-las-file-format-exchange-activities ; COPC — https://copc.io ; PDAL — https://pdal.io ; OpenTopography — https://opentopography.org
- Nan & Wonka, "PolyFit", ICCV 2017 — https://github.com/LiangliangNan/PolyFit
- Huang et al., "City3D", Remote Sensing 2022 — https://arxiv.org/abs/2201.10276 ; https://www.mdpi.com/2072-4292/14/9/2254
- Bauchet & Lafarge, "Kinetic Shape Reconstruction", ACM TOG 2020 — https://doi.org/10.1145/3376918
- Wang et al., "Building3D", ICCV 2023 — https://openaccess.thecvf.com/content/ICCV2023/papers/Wang_Building3D_A_Urban-Scale_Dataset_and_Benchmarks_for_Learning_Roof_Structures_ICCV_2023_paper.pdf
- LOD2-Former, ISPRS Annals 2026 — https://isprs-annals.copernicus.org/articles/XI-2-2026/187/2026/
- USGS 3DEP — https://www.usgs.gov/3d-elevation-program
- FABDEM — https://doi.org/10.1088/1748-9326/ac4d4f
- ESA WorldCover — https://esa-worldcover.org ; Dynamic World — https://dynamicworld.app
- glTF — https://www.khronos.org/gltf/ ; 3D Tiles — https://github.com/CesiumGS/3d-tiles ; Google Photorealistic 3D Tiles — https://developers.google.com/maps/documentation/tile/3d-tiles
- AOUSD Core Spec 1.0 — https://www.linuxfoundation.org/press/alliance-for-openusd-announces-core-specification-1.0-the-universal-language-for-building-3d-worlds ; https://aousd.org
