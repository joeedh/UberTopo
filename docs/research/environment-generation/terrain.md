# Terrain generation

Part of the [environment-generation research set](README.md). Water is in [water.md](water.md);
DEM sources in [real-world-data-sources.md](real-world-data-sources.md); ground acoustic
properties in [sensing-properties-and-lod.md](sensing-properties-and-lod.md).

Research status: November 2026, web search plus prior knowledge.

---

## 1. What terrain must provide for drone defense

- **Line of sight / masking.** Terrain masking is the main way low-flying drones evade sensors
  (nap-of-the-earth approach along valleys). Accuracy needs: about 1 m vertical in the near field
  (< 2 km), and correct *ridge lines* at longer range. Visibility is dominated by ridges and
  breaklines, not by mean elevation.
- **Acoustic ground effect.** Ground impedance (flow resistivity, porosity, layering) together with
  source/receiver height sets the ground-reflection interference dip, typically in the 200 Hz–1 kHz
  band. That is exactly where drone harmonics sit. So **ground type matters as much as
  shape** for microphone arrays. Terrain shape also creates diffraction over ridges and
  barriers, and focusing/shadowing combined with wind and temperature gradients.
- **Microphone/camera siting.** Slope, aspect, soil and access for sensor placement; hilltops for
  coverage.
- **Non-heightfield features.** Cliffs, overhangs, quarries, cut faces, caves, culverts,
  tunnels and bridges. Drones can hide under overhangs and in caves; tunnels are threats.

---

## 2. Representations

| Representation | Pros | Cons | Use |
|---|---|---|---|
| Regular-grid heightfield (DEM) | Simple, GPU-friendly, all data sources | No overhangs; uniform resolution; staircase on cliffs | Base layer everywhere |
| TIN (triangulated irregular network) | Adaptive; exact breaklines (roads, rivers, building pads) | Harder to edit procedurally | Conformed terrain around features; B-rep embedding |
| Layered materials (Benes & Forsbach 2001; Peytavie et al. 2009 "Arches") | Stacked material layers (bedrock, sand, soil, water) per column; limited overhangs | Not fully 3D | Erosion simulations, soil/ground impedance |
| Implicit / SDF / voxel | True 3D (caves, arches, overhangs) | Memory; meshing; CAD integration | Local volumetric features |
| B-rep surface patches | Exact embedding of road/river imprints; trimmed surfaces | Heavy for km² | Near-field around designed features |

**Recommended hybrid:** a **multi-resolution heightfield (quadtree of tiles)** as the global base;
a **TIN/B-rep "conformed" layer** where features (roads, buildings, river banks) imprint
breaklines; and **local volumetric patches** (SDF/voxel or B-rep solids) for cliffs, overhangs,
caves and tunnels, stitched to the heightfield along a boundary loop. Ground material is an
attribute field (raster or per-face) carrying acoustic impedance class and visual material.
Galin et al. 2019 (below) discuss these trade-offs at length.

---

## 3. Procedural generation methods

### 3.1 Noise and fractals (baseline)
- Fractional Brownian motion with Perlin/simplex/value noise; ridged multifractal and hybrid
  multifractal (Musgrave, in Ebert et al., *Texturing & Modeling: A Procedural Approach*).
- Domain warping for less regular forms. Fast and infinite, but **noise terrains have no drainage
  structure**: no consistent valleys, rivers or watersheds. Hydrology then looks wrong, and
  so does the *masking geometry*, because valley networks are what drones fly along. Use noise
  only for small-scale detail (amplification), or as uplift input to erosion.
- Midpoint displacement / diamond-square: legacy; artifacts.

### 3.2 Erosion simulation
- **Thermal erosion**: material slips downslope wherever slope exceeds the talus angle. Cheap; it
  produces scree slopes (Musgrave, Kolb, Mace 1989, "The synthesis and rendering of eroded fractal
  terrains", https://doi.org/10.1145/74334.74337).
- **Hydraulic erosion (shallow-water / virtual pipes)**: water, sediment capacity, erosion and
  deposition on a grid. Mei, Decaudin, Hu 2007, "Fast hydraulic erosion simulation and
  visualization on GPU" (https://doi.org/10.1109/PG.2007.15); Št'ava et al. 2008, "Interactive
  terrain modeling using hydraulic erosion" (https://doi.org/10.2312/SCA/SCA08/201-210). Particle
  ("droplet") erosion is a popular cheap variant. These give gullies and alluvial fans at
  hill scale, but not mountain-range structure.
- **Stream power law + uplift (geomorphology)**: dz/dt = U − K·A^m·S^n, where A is drainage area
  and S is slope. Cordonnier, Braun, Cani, Benes, Galin, Peytavie, Guérin 2016, "Large scale
  terrain generation from tectonic uplift and fluvial erosion" (CGF,
  https://doi.org/10.1111/cgf.12820), was the first graphics method to combine uplift and fluvial
  erosion. It produces realistic dendritic drainage, ridges and peaks at 10–100 km scale. It
  uses an implicit solver over a stream graph (Braun & Willett 2013, FastScape,
  https://doi.org/10.1016/j.geomorph.2012.10.008).
- **Schott, Paris, Fournier, Guérin, Galin 2023, "Large-scale terrain authoring through
  interactive erosion simulation"** (ACM TOG, https://doi.org/10.1145/3592787): the user paints
  the **uplift** field, and a fast approximation of drainage area and flow routing makes stream
  power erosion interactive. This is the best current "author at the mountain-range scale" method.
- **Cordonnier et al. 2018**, "Sculpting mountains: interactive terrain modeling based on
  subsurface geology" (TVCG): volumetric uplift with rock layers of different erodibility.
- **Cordonnier et al. 2017**, "Authoring landscapes by combining ecosystem and terrain erosion
  simulation" (ACM TOG, https://doi.org/10.1145/3072959.3073667): couples vegetation and erosion.
- **Cordonnier et al. 2023, "Forming terrains by glacial erosion"** (ACM TOG,
  https://doi.org/10.1145/3592422): glacial valleys (U-shaped valleys, cirques), which matter for
  alpine terrain.

### 3.3 Hydrology-first (rivers define terrain)
- **Génevaux, Galin, Guérin, Peytavie, Beneš 2013, "Terrain generation using procedural models
  based on hydrology"** (ACM TOG, https://doi.org/10.1145/2461912.2461996): generate the river
  network first, from a sketch of the coastline and river mouths with geomorphology rules
  (Horton–Strahler ordering, Rosgen river types). Then build terrain as a **construction tree
  of primitives** around the river graph: river valleys, hills, mountains, blended into a
  continuous **implicit/primitive-based elevation function**. The terrain is consistent with
  drainage by construction, and it can be evaluated at any resolution (good for LOD). See
  [water.md](water.md).
- Related: "Dendry" (Gaillard, Guérin, Peytavie, Galin 2019, https://doi.org/10.1145/3306131.3317020),
  a procedural, locally evaluable dendritic function (infinite terrains with
  drainage-like ridges/valleys). A good cheap middle ground between noise and erosion.

### 3.4 Example-based / data-driven
- Zhou, Sun, Turk, Rehg 2007, "Terrain synthesis from digital elevation models" (TVCG,
  https://doi.org/10.1109/TVCG.2007.1027): patch-based synthesis from real DEMs along a
  user-sketched feature map.
- Argudo et al. 2019, "Orometry-based terrain analysis and synthesis" (ACM TOG,
  https://doi.org/10.1145/3355089.3356535): peak/saddle "divide trees" as a structural descriptor;
  synthesis that matches real peak statistics.
- Guérin et al. 2017, "Interactive example-based terrain authoring with conditional generative
  adversarial networks" (https://doi.org/10.1145/3130800.3130804): sketch → DEM (cGAN).
- Diffusion models (2023–2025): Lochner et al. 2023 "Interactive authoring of terrain using
  diffusion models" (CGF, https://doi.org/10.1111/cgf.14941); **MESA** (text-to-terrain trained on
  Copernicus DEM, CVPRW 2025, https://arxiv.org/abs/2504.07210); **Terrain Diffusion** (Goslin,
  Dec 2025, https://arxiv.org/abs/2512.08309): a hierarchical diffusion stack with "InfiniteDiffusion"
  for unbounded, real-time terrain. See [ml-generation.md](ml-generation.md) for the critique.
  In short, these are good at *appearance-level realism* but give **no hydrological
  consistency guarantee**, and resolution is usually ≥ 30 m.

### 3.5 Terrain amplification (adding detail to coarse real DEMs)
This is critical for UberTopo. Real data is 30 m globally (Copernicus/FABDEM), while sites need
0.5–1 m.
- Paris et al. 2019, "Terrain amplification with implicit 3D features" (ACM TOG,
  https://doi.org/10.1145/3342765), adds 3D features (arches, overhangs) to heightfields.
- Simpler and robust: **constrained fractal amplification**. Interpolate the coarse DEM, add
  band-limited noise whose spectrum continues the measured DEM's power-spectral slope (terrain
  roughness spectra are roughly power law), and **pin** the amplified surface to known breaklines
  (roads, rivers, building pads) and to any high-resolution data (3DEP 1 m, local LiDAR). Then run
  short hydraulic/thermal erosion passes for plausible micro-drainage.
- Use land cover to modulate micro-relief: agricultural fields smooth, forest floors with hummocks
  and deadfall, urban flat pads.

---

## 4. Non-heightfield features

- **Cliffs / overhangs**: Peytavie et al. 2009, "Arches: a framework for modeling complex
  terrains" (CGF, https://doi.org/10.1111/j.1467-8659.2009.01390.x), uses layered material stacks
  with stability simulation. Becher et al. 2019, "Feature-based volumetric terrain generation and
  decoration" (TVCG), and Paris et al. 2019 (implicit 3D features).
- **Caves**: Paris, Guérin, Peytavie, Collon, Galin 2021, "Synthesizing geologically coherent cave
  networks" (CGF/PG 2021, https://doi.org/10.1111/cgf.14420). Builds cave graphs from geological
  constraints (horizons, fractures, inception), then implicit tunnel geometry. Real caves:
  survey data (Therion format) is sparse.
- **Man-made cuts**: quarries, open-pit mines (benched spiral profiles: use the same sweep-
  with-profile machinery as roofs and roads), embankments, berms, earth-covered magazines
  and revetments (military), landfill mounds.
- **Representation**: local SDF/voxel or B-rep solids, with the heightfield "holed" where they
  attach. The B-rep needs **faces with holes**, plus a boundary loop shared between the
  heightfield face and the volumetric patch.

---

## 5. Terrain LOD and streaming

- **Geometry clipmaps** (Losasso & Hoppe 2004, https://doi.org/10.1145/1015706.1015799): nested
  grids centred on the viewer. Very fast, but view-dependent. Our *simulation* consumers (acoustic
  ray tracers, LOS queries) are not camera-centric.
- **Chunked LOD / CDLOD / quadtree tiles** with skirts or stitched borders. **Cesium
  quantized-mesh** and **3D Tiles** use TIN tiles at successive error levels
  (https://github.com/CesiumGS/quantized-mesh).
- **RTIN / Martini** (right-triangulated irregular networks, Mapbox) for fast adaptive TINs from
  grids.
- **Error-bounded simplification**: Garland & Heckbert 1995 greedy insertion TIN.
- **Query-driven LOD (important for UberTopo):** acoustic and LOS queries need *query-dependent*
  accuracy, e.g. full resolution near sensors and along the Fresnel zone of a propagation path,
  coarse elsewhere. Store the terrain as a **hierarchy with per-level max-error bounds** and
  **min/max elevation per tile** (an implicit BVH for ray/segment LOS queries). Maximum-mipmaps
  (min/max pyramid) make heightfield ray casting O(log n).
- Procedural terrain must be **deterministic per tile and level** (seeded by tile key), and
  amplification must be **consistent across levels**: a coarse tile is the low-pass of the fine
  tile. Otherwise LOS changes with LOD. Hydrology-first (Génevaux 2013) and Dendry are locally
  evaluable, which helps here.

---

## 6. Ground materials (attribute layer)

Each terrain face/cell carries a ground type: asphalt, concrete, compacted soil, gravel, bare
soil (ploughed/compacted), short grass, long grass, forest floor (litter), sand, snow (fresh/old),
water, wetland, rock. This gives:
- **Acoustic**: effective flow resistivity σ (kPa·s/m²) and optionally porosity/layer depth for
  a Delany–Bazley/Miki or two-parameter model, or the Nord2000 impedance classes A–H. See
  [sensing-properties-and-lod.md](sensing-properties-and-lod.md).
- **Visual**: PBR material, seasonal state, moisture.
- **Trafficability / access** (for vulnerability analysis): who can reach which point.

Derive ground type from land cover (ESA WorldCover, Dynamic World), soil maps (SoilGrids,
https://soilgrids.org) and OSM `surface=*`, refined procedurally (paths across grass, worn areas).

---

## 7. Recommendations

1. **Base**: real DEM where available (3DEP/LiDAR > national DTMs > FABDEM > Copernicus GLO-30).
   Prefer bare-earth DTMs. Copernicus GLO-30 is a *DSM*; FABDEM removes trees and buildings
   (Hawker et al. 2022, https://doi.org/10.1088/1748-9326/ac4d4f).
2. **Synthetic**: uplift + stream-power erosion (Cordonnier 2016 / Schott 2023) for mountains and
   hills; a hydrology-first primitive tree (Génevaux 2013) for controllable river valleys; noise
   only for micro detail.
3. **Amplification**: spectral-continuation noise pinned to breaklines and measured data, plus
   short erosion passes.
4. **Representation**: tiled multi-resolution heightfield + conforming TIN/B-rep imprints
   + local volumetric patches. Store min/max pyramids for fast LOS.
5. **Ground-type attribute layer** as a first-class output with acoustic impedance.
6. Treat ML terrain generators as optional style sources at ≥ 30 m. Always post-process with
   hydrological correction (depression filling, breaching; see [water.md](water.md)).

---

## References

- Galin et al., "A Review of Digital Terrain Modeling", CGF (Eurographics STAR) 2019 — https://doi.org/10.1111/cgf.13657
- Musgrave, Kolb, Mace, "The synthesis and rendering of eroded fractal terrains", SIGGRAPH 1989 — https://doi.org/10.1145/74334.74337
- Mei, Decaudin, Hu, "Fast hydraulic erosion simulation and visualization on GPU", PG 2007 — https://doi.org/10.1109/PG.2007.15
- Št'ava et al., "Interactive terrain modeling using hydraulic erosion", SCA 2008 — https://doi.org/10.2312/SCA/SCA08/201-210
- Génevaux et al., "Terrain generation using procedural models based on hydrology", SIGGRAPH 2013 — https://doi.org/10.1145/2461912.2461996
- Cordonnier et al., "Large scale terrain generation from tectonic uplift and fluvial erosion", CGF 2016 — https://doi.org/10.1111/cgf.12820 (PDF: https://www.cs.purdue.edu/homes/bbenes/papers/Cordonier16CGF.pdf)
- Braun & Willett, "A very efficient O(n), implicit and parallel method to solve the stream power equation", Geomorphology 2013 — https://doi.org/10.1016/j.geomorph.2012.10.008
- Cordonnier et al., "Authoring landscapes by combining ecosystem and terrain erosion simulation", SIGGRAPH 2017 — https://doi.org/10.1145/3072959.3073667
- Cordonnier et al., "Sculpting mountains", TVCG 2018 — https://www.cs.purdue.edu/homes/bbenes/papers/Cordonnier18TVCG.pdf
- Schott et al., "Large-scale terrain authoring through interactive erosion simulation", ACM TOG 2023 — https://doi.org/10.1145/3592787
- Cordonnier et al., "Forming terrains by glacial erosion", ACM TOG 2023 — https://doi.org/10.1145/3592422
- Gaillard et al., "Dendry", I3D 2019 — https://doi.org/10.1145/3306131.3317020
- Zhou et al., "Terrain synthesis from digital elevation models", TVCG 2007 — https://doi.org/10.1109/TVCG.2007.1027
- Argudo et al., "Orometry-based terrain analysis and synthesis", SIGGRAPH Asia 2019 — https://doi.org/10.1145/3355089.3356535
- Guérin et al., "Interactive example-based terrain authoring with cGANs", SIGGRAPH Asia 2017 — https://doi.org/10.1145/3130800.3130804
- Lochner et al., "Interactive authoring of terrain using diffusion models", CGF 2023 — https://doi.org/10.1111/cgf.14941
- Borne-Pons et al., "MESA", CVPRW 2025 — https://arxiv.org/abs/2504.07210
- Goslin, "Terrain Diffusion", 2025 — https://arxiv.org/abs/2512.08309
- Paris et al., "Terrain amplification with implicit 3D features", ACM TOG 2019 — https://doi.org/10.1145/3342765
- Peytavie et al., "Arches: a framework for modeling complex terrains", CGF 2009 — https://doi.org/10.1111/j.1467-8659.2009.01390.x
- Paris et al., "Synthesizing geologically coherent cave networks", CGF 2021 — https://doi.org/10.1111/cgf.14420
- Losasso & Hoppe, "Geometry clipmaps", SIGGRAPH 2004 — https://doi.org/10.1145/1015706.1015799
- Cesium quantized-mesh — https://github.com/CesiumGS/quantized-mesh
- Hawker et al., "A 30 m global map of elevation with forests and buildings removed" (FABDEM), ERL 2022 — https://doi.org/10.1088/1748-9326/ac4d4f
- SoilGrids — https://soilgrids.org
