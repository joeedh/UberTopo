# Water: rivers, lakes, wetlands, coasts, canals, bathymetry

Part of the [environment-generation research set](README.md). Terrain coupling is in
[terrain.md](terrain.md); hydrography data sources in
[real-world-data-sources.md](real-world-data-sources.md).

Research status: November 2026. The web-search budget ran out while this file was being written,
so citations marked *(verify)* come from prior knowledge and were not re-checked online.

---

## 1. Why water matters for drone defense

- **Acoustics:** a calm water surface is an almost perfect acoustic reflector (impedance far above
  that of air), so sound over water carries far. Low-level temperature inversions over cool water
  bend sound downward, adding range. Moving water (rivers, weirs, surf) is a **broadband noise
  source** that masks drones. Shorelines are impedance discontinuities.
- **Cameras:** specular reflections (a drone's mirror image on water, false detections), sun
  glint, and low-contrast backgrounds. Water lacks texture, which hurts stereo/optical-flow methods.
- **Vulnerability:** waterways are approach corridors (low, open, unobstructed, often unwatched),
  ports and dams are targets, and sensor placement is constrained (no mounting on water without
  buoys or platforms).
- **Topology:** water bodies are **volumes** bounded by terrain (bed) and a free surface. Bridges,
  piers and dams sit across them. This is a case for a cellular / 3-manifold-capable B-rep:
  the water cell shares the bed faces with the ground solid and the surface face with the air.

---

## 2. Representation

- **Water body = 3-cell** bounded by (a) bed faces, which are terrain faces below the water level
  (bathymetry); (b) a **free-surface face**: planar for lakes and reservoirs (single water-level
  attribute), sloped/curved for rivers (water surface profile along the channel); (c) structure
  faces (dam faces, quay walls, lock gates).
- **Shoreline = edge loop** where the free surface meets terrain. It must be computed by
  intersection, never stored independently, or the two drift apart under edits/LOD changes.
  The free surface is a face with holes (islands).
- **Attributes**: water level (with seasonal/tidal range as a distribution), flow velocity field
  (for surface roughness/noise), depth, turbidity (visual), salinity (acoustic underwater, not our
  focus), ice cover (seasonal: changes acoustics completely), wave state (sea state → surface
  roughness → scattering and sound source).
- **Rivers at low LOD**: a centreline curve (graph edge) + width/depth functions along the curve
  (like a road cross-section). At high LOD, an explicit channel cut into terrain with banks.

---

## 3. Rivers and river networks

### 3.1 Network generation
- **Hydrology-first terrain (Génevaux et al. 2013**, https://doi.org/10.1145/2461912.2461996):
  river mouths on the coastline, growth inland by expansion rules (asymmetric/symmetric branching,
  continuation) weighted by elevation and slope priority; **Horton–Strahler** ordering assigns
  stream orders; **Rosgen classification** (A–G types: steep step-pool to meandering
  low-gradient) gives each segment a cross-section and planform. Terrain is then built around
  the network. This is the most controllable method.
- **Emergent from erosion** (Cordonnier 2016, Schott 2023, see [terrain.md](terrain.md)): the
  drainage network appears from stream-power erosion. Extract it with flow routing (D8/D∞/MFD)
  and an accumulation threshold.
- **River networks on planets**: Derzapf, Ganster, Guthe, Klein 2011, "River networks for instant
  procedural planets" (CGF, https://doi.org/10.1111/j.1467-8659.2011.02053.x).
- **Classic**: Kelley, Malin, Nielson 1988, "Terrain simulation using a model of stream erosion"
  (SIGGRAPH, https://doi.org/10.1145/378456.378519); Prusinkiewicz & Hammel 1993, fractal
  mountains with rivers.
- **Procedural Riverscapes** (Peytavie, Dupont, Guérin, Cortial, Benes, Gain, Galin; CGF/Pacific
  Graphics 2019, *verify*): detailed river *surfaces and bed features*, including rapids,
  pools, and flow-dependent surface appearance, generated procedurally from a river graph.

### 3.2 Planform: meanders, braids
- Meandering: sinuosity, wavelength ≈ 10–14 × channel width, radius of curvature ≈ 2–3 × width
  (Leopold & Wolman empirical relations). Generate from the centreline with a *sine-generated
  curve* (Langbein & Leopold 1966: direction angle θ(s) = ω·sin(2πs/M)), which is a natural fit for
  the project's curve machinery since it is a curvature-defined (intrinsic) curve like a clothoid.
- Meander migration simulation (Howard & Knutson 1984) gives oxbow lakes and scroll bars.
  Graphics: Paris et al., "Authoring and simulating meandering rivers" (ACM TOG / SIGGRAPH Asia
  2023, *verify*).
- Braided rivers: multiple channels around bars; generate as a channel graph within a braid
  belt.

### 3.3 Cross-section and banks
- Width and depth from hydraulic geometry: w ∝ Q^0.5, d ∝ Q^0.4 (Leopold & Maddock 1953), where
  discharge Q comes from the upstream drainage area.
- Asymmetric cross-section at bends (cut bank deep and steep, point bar shallow).
- Bank material and vegetation (riparian strip: see [vegetation-and-biomes.md](vegetation-and-biomes.md)).
- Engineered channels: trapezoidal/rectangular concrete (urban flood channels, e.g. LA River),
  levees, rip-rap, weirs, culverts (a culvert is a tunnel under a road: non-heightfield).

### 3.4 Water surface
Water surface elevation along the river comes from a 1D backwater/normal-depth computation
(Manning's equation) or, more simply, from monotonic smoothing of bed elevation + depth.
Hydrological DEM conditioning is required so the water never flows uphill: use **priority-flood**
depression filling (Barnes, Lehman, Mulla 2014, https://doi.org/10.1016/j.cageo.2013.04.024) or
breaching.

---

## 4. Lakes, reservoirs, ponds

- **Natural lakes**: depressions of the terrain that fill to a spill level. Compute with
  priority-flood / **fill-spill-merge** (Barnes, Callaghan, Wickert 2021, "Computing water flow
  through complex landscapes — Part 3: Fill–Spill–Merge", Earth Surface Dynamics, *verify DOI*)
  given a water budget. Not every depression is full (endorheic basins, seasonal lakes).
- **Reservoirs**: a dam across a valley, with the water level at the dam's normal pool elevation.
  Flood the terrain below that level upstream of the dam (connected-component flood fill). The
  shoreline is the contour of the terrain at pool level: an exact iso-contour on the B-rep or
  heightfield.
- **Ponds**: farm ponds, stormwater retention basins (urban, often with designed side slopes),
  quarry lakes (steep sides).
- **Bathymetry of lakes**: if unknown, synthesise by a distance-to-shore transform with a
  depth-shape exponent (glacial lakes deep and steep, floodplain lakes shallow), or a max-depth
  prior from HydroLAKES statistics.

---

## 5. Wetlands, swamps, marshes, mangroves

- **Definition for generation**: areas with water table at or above the surface for part of the
  year. Generate from low slope + high flow accumulation + low elevation above the nearest
  drainage (HAND, "height above nearest drainage", Rennó et al. 2008) + land cover.
- **Geometry**: a mosaic of open-water patches, emergent vegetation (reeds, cattails), hummocks
  and channels. Generate the water/land mosaic by thresholding a noise field modulated by HAND,
  then place vegetation per zone ([vegetation-and-biomes.md](vegetation-and-biomes.md)).
- **Swamp (forested wetland)**: trees (cypress, mangrove) standing in shallow water. The B-rep
  representation needs tree trunks piercing the water surface face, which means many holes in the
  surface face. At LOD this becomes statistical ("stem density per m²").
- **Mangroves**: intertidal; prop roots form dense lattices (strong visual occlusion, acoustic
  scattering). Tidal water level changes everything twice a day.
- **Acoustics**: wet ground behaves like a hard reflector when flooded and like porous soft ground
  when vegetated or saturated soil. The impedance is seasonal and time-dependent.
- **Data**: Global Mangrove Watch (https://www.globalmangrovewatch.org), ESA WorldCover classes
  "herbaceous wetland" and "mangroves", US National Wetlands Inventory
  (https://www.fws.gov/program/national-wetlands-inventory), GLWD (Global Lakes and Wetlands
  Database, v2 released 2024/2025 *verify*).

---

## 6. Coastlines and the sea

- **Coastline shape**: fractal (Mandelbrot), but real coasts have structure: headlands and bays
  from differential erosion, spits, barrier islands, deltas, cliffs (non-heightfield), beaches with
  a characteristic profile (Dean equilibrium profile h = A·x^(2/3), with A set by grain size).
- **Generation**: start from a land mask (noise threshold or real), apply coastal erosion
  heuristics (soft-rock bays, hard-rock headlands), then generate the nearshore bathymetry with the
  Dean profile, then dunes behind beaches ([vegetation-and-biomes.md](vegetation-and-biomes.md)
  desert/dune section).
- **Tides**: tidal range turns a wide intertidal zone into land or water. Model water level as a
  scenario parameter.
- **Sea state**: Pierson–Moskowitz/JONSWAP wave spectra for surface geometry at camera LOD
  (Tessendorf FFT ocean); for acoustics, sea state → surface roughness and ambient noise (Wenz
  curves).
- **Coastal structures**: breakwaters, groynes, seawalls, piers, quays, harbours. Ports are
  high-value drone targets.
- **Data**: OSM coastline (`natural=coastline`; processed land polygons at
  https://osmdata.openstreetmap.de), GSHHG, GEBCO bathymetry (https://www.gebco.net, about 450 m
  global grid), NOAA ETOPO 2022 (https://www.ncei.noaa.gov/products/etopo-global-relief-model),
  NOAA BlueTopo/CUDEM for US coastal high-res, EMODnet Bathymetry (Europe,
  https://emodnet.ec.europa.eu/en/bathymetry), national LiDAR bathymetry (topobathy).

---

## 7. Canals, docks, locks, dams

- **Canals**: engineered alignment (straight reaches, gentle curves), trapezoidal or rectangular
  prism, towpaths, locks (chamber + gates; stepped water levels), aqueducts (a canal on a bridge,
  so water sits above air: a clear non-heightfield / non-manifold case). OSM: `waterway=canal`,
  `lock=yes`.
- **Irrigation canals and ditches**: dense networks in agricultural areas; generated along field
  boundaries (see the agriculture section in [vegetation-and-biomes.md](vegetation-and-biomes.md)).
- **Dams**: gravity (concrete, triangular profile), arch (curved in plan, thin), embankment
  (earth/rock fill, trapezoidal), buttress. Spillways, intake towers, powerhouses. Dams are
  critical infrastructure (see [infrastructure.md](infrastructure.md)). Data: GRanD/GOODD (Global
  Dam Watch, https://www.globaldamwatch.org), OSM `waterway=dam` / `man_made=dyke`.
- **Docks and quays**: vertical walls, so water depth changes abruptly there; mooring structures.

---

## 8. Hydrography data sources

| Dataset | Content | Resolution / scope |
|---|---|---|
| HydroSHEDS / HydroRIVERS / HydroLAKES (https://www.hydrosheds.org) | Conditioned DEM, river network, lake polygons with depth/volume estimates | ~90 m (15 arcsec), global |
| MERIT Hydro (https://hydro.iis.u-tokyo.ac.jp/~yamadai/MERIT_Hydro/) | Flow direction, upstream area, river width | 90 m, global |
| JRC Global Surface Water (https://global-surface-water.appspot.com) | Water occurrence 1984–present from Landsat | 30 m |
| USGS NHDPlus HR / 3DHP (https://www.usgs.gov/national-hydrography) | US high-resolution hydrography, moving to the 3D Hydrography Program | 1:24k and better |
| OSM `waterway=*`, `natural=water`, `water=*` | Centrelines and polygons | Variable; excellent in Europe |
| SWOT mission (https://swot.jpl.nasa.gov) | Satellite water-surface elevation and slope of rivers/lakes | Since 2023 |

---

## 9. LOD variants

| LOD | Representation |
|---|---|
| W0 | Graph (river centrelines with width/order), lake polygons with a level |
| W1 | Flat water surfaces draped/clipped to terrain; channels burnt into DEM |
| W2 | Explicit channel geometry with banks; bathymetry; free-surface profile; structures |
| W3 | Wave/ripple surface detail (camera), rapids, debris, emergent vegetation in water |

---

## 10. Recommendations

1. Treat water bodies as **cells** of the environment complex (bed faces shared with terrain,
   free surface shared with air). Shorelines are derived by intersection, never authored
   independently.
2. Generate rivers **with** terrain (hydrology-first or erosion-emergent), never by carving
   independent splines into noise terrain. Always run hydrological conditioning (priority-flood)
   on any DEM, real or synthetic.
3. River centrelines are intrinsic curves (sine-generated / curvature-defined), handled by the
   same curve machinery as roads: width/depth functions along arc length.
4. Water level, tide, ice and sea state are **scenario parameters**, not baked geometry. The free
   surface face is regenerated from the level.
5. Attach acoustic (hard reflector; ambient noise source spectra for rivers/surf) and visual
   (specular, wave spectrum) properties.

---

## References

- Génevaux et al., "Terrain generation using procedural models based on hydrology", SIGGRAPH 2013 — https://doi.org/10.1145/2461912.2461996
- Derzapf et al., "River networks for instant procedural planets", CGF 2011 — https://doi.org/10.1111/j.1467-8659.2011.02053.x
- Kelley, Malin, Nielson, "Terrain simulation using a model of stream erosion", SIGGRAPH 1988 — https://doi.org/10.1145/378456.378519
- Peytavie et al., "Procedural Riverscapes", CGF (PG) 2019 — *verify*: https://diglib.eg.org (search title)
- Paris et al., "Authoring and simulating meandering rivers", ACM TOG 2023 — *verify*
- Langbein & Leopold, "River meanders — theory of minimum variance", USGS Prof. Paper 422-H, 1966 — https://pubs.usgs.gov/pp/0422h/report.pdf (*verify URL*)
- Leopold & Maddock, "The hydraulic geometry of stream channels", USGS Prof. Paper 252, 1953 — https://pubs.usgs.gov/publication/pp252
- Barnes, Lehman, Mulla, "Priority-flood", Computers & Geosciences 2014 — https://doi.org/10.1016/j.cageo.2013.04.024
- Rennó et al., "HAND, a new terrain descriptor using SRTM-DEM", RSE 2008 — https://doi.org/10.1016/j.rse.2008.03.018
- HydroSHEDS — https://www.hydrosheds.org ; MERIT Hydro — https://hydro.iis.u-tokyo.ac.jp/~yamadai/MERIT_Hydro/
- JRC Global Surface Water — https://global-surface-water.appspot.com
- USGS National Hydrography — https://www.usgs.gov/national-hydrography
- GEBCO — https://www.gebco.net ; ETOPO 2022 — https://www.ncei.noaa.gov/products/etopo-global-relief-model ; EMODnet Bathymetry — https://emodnet.ec.europa.eu/en/bathymetry
- OSM coastline data — https://osmdata.openstreetmap.de
- Global Dam Watch (GRanD/GOODD) — https://www.globaldamwatch.org
- Global Mangrove Watch — https://www.globalmangrovewatch.org ; US NWI — https://www.fws.gov/program/national-wetlands-inventory
- SWOT — https://swot.jpl.nasa.gov
