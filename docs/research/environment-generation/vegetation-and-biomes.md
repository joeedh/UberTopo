# Vegetation and biomes

Part of the [environment-generation research set](README.md). Ground and terrain are in
[terrain.md](terrain.md); wetlands in [water.md](water.md); acoustic/visual property tables in
[sensing-properties-and-lod.md](sensing-properties-and-lod.md).

Research status: November 2026. Core citations were checked by web search where noted. The
search budget ran out partway, so items marked *(verify)* rely on prior knowledge.

---

## 1. Why vegetation matters for drone defense

- **Visual occlusion and clutter.** Tree canopies hide low-flying drones. Branches cause
  intermittent partial occlusion, and moving foliage generates false motion detections. For
  cameras the key quantities are **gap fraction** (probability of a clear line of sight through a
  canopy along a ray) and **clutter statistics**, not leaf-accurate geometry, except at short range.
- **Acoustic attenuation and scattering.** Vegetation affects sound in three ways:
  (1) **ground effect** from soft, porous forest floor (often the dominant effect at low frequency);
  (2) **scattering by trunks and branches** (mid frequencies, wavelength comparable to trunk/branch
  diameter); (3) **absorption by foliage** (high frequencies, > 1–2 kHz). Forests also produce
  **reverberation tails** that smear TDOA estimates (Huisman & Attenborough 1991). Wind in foliage
  is an ambient noise source.
- **Vulnerability surveys.** Treelines are cover for drone operators and launch sites; clearings
  are landing zones; vegetation limits sensor line of sight and needs maintenance (seasonal
  leaf-off changes coverage).

So vegetation needs **three representations of the same plant population**: explicit geometry
(near), statistical/volumetric media (medium/far, for both rays and acoustics), and a placement
model consistent across them.

---

## 2. Individual plant models

### 2.1 Grammar-based
- **L-systems** (Prusinkiewicz & Lindenmayer, *The Algorithmic Beauty of Plants*, 1990,
  http://algorithmicbotany.org/papers/#abop): parametric, stochastic, context-sensitive and
  environmentally sensitive (open) L-systems. Great for herbaceous plants, flowers and grasses,
  less intuitive for mature trees.
- **Weber & Penn 1995, "Creation and rendering of realistic trees"** (SIGGRAPH,
  https://doi.org/10.1145/218380.218427): a parametric (not grammar) recursive branching model with
  about 80 parameters per species (levels, curvature, splits, taper, pruning envelope). It is the
  basis of Blender's "Sapling Tree Gen" and many game tools. Simple and deterministic, and good for
  generating many species variants at fixed cost.

### 2.2 Self-organising / environment-driven
- **Space colonization** (Runions, Lane, Prusinkiewicz 2007, "Modeling trees with a space
  colonization algorithm", Eurographics NPH, https://doi.org/10.2312/NPH/NPH07/063-070): attraction
  points fill a crown envelope, and branches grow toward the nearest points, removing them on
  arrival. It produces natural branch spacing and responds to envelope shape and obstacles (e.g.
  trees against buildings, under power lines: utility pruning creates V-shaped crowns).
- **Palubicki et al. 2009, "Self-organizing tree models for image synthesis"** (SIGGRAPH,
  https://doi.org/10.1145/1531326.1531364): buds compete for light/space through a shadow
  propagation grid; resources are allocated by the Borchert–Honda model. Captures crown
  shyness, light-seeking asymmetry and forest-edge effects. **Recommended for forest edges and
  urban trees next to buildings**, because neighbour-aware form matters for occlusion.
- **Inverse procedural modelling of trees** (Stava et al. 2014, CGF,
  https://doi.org/10.1111/cgf.12282): fits developmental model parameters to example trees.
  Useful for calibrating species to LiDAR-measured trees.
- **Plastic trees / interaction** (Pirk et al. 2012) adapt trees to their surroundings after
  generation.

### 2.3 Commercial and production tools
- **SpeedTree** (https://speedtree.com): industry standard, hybrid procedural + hand-editing, with
  LOD and wind built in. Proprietary format; usable via export (FBX/USD).
- **The Grove** (https://www.thegrove3d.com): a botanically-grounded growth simulator (Blender).
- **Infinigen** (Raistrick et al., CVPR 2023, https://arxiv.org/abs/2306.09310): procedural trees,
  bushes, grasses, cacti, corals, rocks, and terrain, all from code, BSD licence. A good open
  reference corpus.

### 2.4 Data-driven plant reconstruction
Terrestrial and airborne LiDAR → quantitative structure models (QSM, e.g. TreeQSM) for trunk and
branch cylinders. These give real scattering-relevant geometry for acoustic validation plots.

---

## 3. Placement: distributions and ecosystems

### 3.1 Point processes
- **Poisson-disk sampling** (Bridson 2007, "Fast Poisson disk sampling in arbitrary dimensions",
  https://doi.org/10.1145/1278780.1278807) for minimum spacing; variable radius by species/size.
- **Multi-class disk distributions** reproduce inter-species pair correlations: Ecormier-Nocca et
  al. 2019, "Accurate synthesis of multi-class disk distributions" (CGF,
  https://doi.org/10.1111/cgf.13631). Learn pair-correlation functions from real plots and
  synthesise matching distributions.
- **Clustered processes** (Thomas/Matérn cluster) for regeneration patches; **inhibition**
  (Strauss) for mature canopy spacing.
- **Tiling**: Wang tiles / Poisson-disk tiles with stable per-tile seeds for infinite, streamable
  placement (essential for LOD consistency).

### 3.2 Ecosystem simulation
- **Deussen et al. 1998, "Realistic modeling and rendering of plant ecosystems"** (SIGGRAPH,
  https://doi.org/10.1145/280814.280898): a multi-level pipeline. Terrain + ecological
  simulation (competition for space, self-thinning: plants modelled as circles that grow and
  kill dominated neighbours) → plant distribution → instancing of approximate geometry. Still
  the reference architecture.
- **Lane & Prusinkiewicz 2002**, "Generating spatial distributions for multilevel models of plant
  communities" (Graphics Interface): local-to-global plant interaction rules.
- **Makowski et al. 2019, "Synthetic silviculture: multi-scale modeling of plant ecosystems"**
  (SIGGRAPH, https://doi.org/10.1145/3306346.3323039): couples individual developmental tree models
  (branch-level, light-competition) with stand-level dynamics over centuries. Produces forests where
  each tree's form reflects its competition history (suppressed understorey, edge trees). This is
  the highest-fidelity approach for forest *structure*.
- **EcoBrush** (Gain et al. 2017, CGF, https://doi.org/10.1111/cgf.13116): brushes that paint
  ecosystem distributions learned from simulation.
- **EcoLearn** (Kapp, Gain, Guérin, Galin, Peytavie 2020, "Data-driven authoring of large-scale
  ecosystems", ACM TOG, https://doi.org/10.1145/3414685.3417848; code
  https://github.com/jgain/EcoLearn): learns a **canopy height model** for given terrain from real
  data, fits canopy trees to it to match a species distribution, then synthesises understorey from
  learned distributions. It captures self-thinning and gap dynamics *without* hours of simulation.
  **Recommended model for UberTopo forests:** it pairs naturally with real canopy height data.
- **Cordonnier et al. 2017** (ecosystem + erosion coupling) and Ecormier-Nocca et al. 2021,
  "Authoring consistent landscapes with flora and fauna" (ACM TOG,
  https://doi.org/10.1145/3450626.3459952).

### 3.3 Driving data
- **Canopy height**: Meta/WRI 1 m global canopy height (Tolan et al. 2024, RSE,
  https://doi.org/10.1016/j.rse.2023.113888, *verify DOI*); ETH 10 m global canopy height (Lang et
  al. 2023, Nature Ecol. Evol., https://doi.org/10.1038/s41559-023-02206-7); GEDI LiDAR footprints
  (https://gedi.umd.edu); national ALS canopy height models.
- **Land cover**: ESA WorldCover 10 m (https://esa-worldcover.org), Dynamic World
  (https://dynamicworld.app), CORINE, NLCD.
- **Biome/climate**: Köppen–Geiger maps (Beck et al.; https://www.gloh2o.org/koppen/), RESOLVE
  Ecoregions 2017 (https://ecoregions.appspot.com), Whittaker biome diagram
  (temperature × precipitation) for synthetic worlds.
- **Street trees**: municipal tree inventories (many cities publish species, DBH, height); OSM
  `natural=tree`, `natural=tree_row`, `denotation=avenue`.

---

## 4. Biome-specific generation

| Biome | Key structure | Generation notes | Sensing notes |
|---|---|---|---|
| **Temperate broadleaf forest** | Canopy 20–35 m, understorey, leaf-off in winter | EcoLearn / silviculture; seasonal leaf state as parameter | Leaf-off changes gap fraction drastically; acoustic attenuation drops |
| **Boreal / conifer forest** | Dense, conical crowns, moss/litter floor | Weber–Penn conifer templates; Poisson + clustering | Very soft forest floor (low σ), strong ground effect |
| **Tropical rainforest** | Multi-layered, emergents to 50+ m, lianas | Layered placement; lianas as space-colonization between crowns | Near-total occlusion; high humidity; loud ambient (insects, birds) |
| **Savanna / open woodland** | Scattered trees on grass | Inhibition point process; fire-scar patches | LOS mostly clear at low level; isolated tree occluders |
| **Grassland / steppe / prairie** | Grass 0.1–2 m | Not geometry: instanced cards near camera, a volumetric layer for rays, ground impedance for acoustics | Tall grass hides ground operators |
| **Desert** | Sand dunes, rock, sparse shrubs/cacti | Dune simulation (below); sparse inhibition processes | Hot ground → strong upward refraction (acoustic shadow zones at day); mirage for cameras |
| **Tundra / alpine** | Low shrubs, lichen, permafrost patterns, snow | Patterned ground (polygons) via Voronoi; snow cover as a layer | Fresh snow is extremely absorptive (very low σ) |
| **Wetland / swamp / mangrove** | See [water.md](water.md) | Zonation by HAND/inundation; prop-root lattices procedurally | Dense lattices: occlusion + scattering |
| **Mediterranean scrub (maquis/chaparral)** | Dense 1–4 m shrubs | Cluster processes; shrub geometry from space colonization | Near-impenetrable for ground; cover for operators |
| **Agricultural** | Fields, crops, hedgerows, orchards, vineyards, greenhouses | See §5 | Seasonal crop height; greenhouse plastic is a strong reflector/glinter |
| **Urban green** | Street trees, parks, lawns, hedges, green roofs | Rule-based from the street cross-section (tree every 6–12 m), park planting templates | Trees in street canyons scatter and absorb |

### 4.1 Desert dunes
- **Werner 1995**, "Eolian dunes: computer simulations and attractor interpretation" (Geology,
  https://doi.org/10.1130/0091-7613(1995)023%3C1107:EDCSAA%3E2.3.CO;2): a cellular automaton of
  sand slabs saltating downwind with a "shadow zone" and avalanching at the angle of repose (about
  34°). Depending on sand supply and wind variability it reproduces barchan, transverse, linear
  (seif) and star dunes. Cheap, GPU-friendly and well understood.
- **Paris, Peytavie, Guérin, Argudo, Galin 2019, "Desertscapes simulation"** (CGF/PG 2019,
  https://doi.org/10.1111/cgf.13815): extends Werner-style transport with a terrain-aware surface
  wind field, layered sand/bedrock, abrasion (yardangs, ventifacts), vegetation stabilisation
  (nabkha), and interactive authoring. Generates barchan, longitudinal and anchored dunes. A 2024
  GPU follow-up gives real-time simulation.
- **Sand ripples**: centimetre-to-decimetre wavelength; procedural (normal/displacement maps) or
  a ripple CA (Nishimori & Ouchi 1993, PRL, https://doi.org/10.1103/PhysRevLett.71.197; Anderson
  1987). Relevant visually only.
- Other desert elements: rocky hamada/reg (desert pavement), wadis (ephemeral channels, generated
  by the river methods but dry), playas (flat, hard, very reflective acoustically), mesas and buttes
  (layered erosion with hard caprock, a non-heightfield cliff case).

---

## 5. Agriculture

- **Field layout**: field boundaries follow cadastral/parcel patterns, roads and topography. Types
  include rectangular (US PLSS grid, 1 mile sections), irregular old-world, strip fields,
  terraces on slopes (contour-following, generated as offset contour bands, a sweep-with-
  profile case again), and **centre-pivot circles** (400–800 m diameter). Generate by subdividing
  agricultural land-use zones with the parcel methods ([urban-layout.md](urban-layout.md)),
  with size priors by region.
- **Data**: Fields of The World (benchmark/models for field boundaries, *verify URL*
  https://fieldsofthe.world), EuroCrops (*verify*), USDA Cropland Data Layer
  (https://nassgeodata.gmu.edu/CropScape/).
- **Crops**: row direction (along the long field axis or contours), row spacing (0.15–1 m),
  height by crop and season (maize 2–3 m late summer gives strong cover; wheat about 1 m;
  bare soil after harvest). Represent at medium LOD as a homogeneous volumetric layer with
  anisotropy (rows).
- **Orchards / vineyards / plantations**: regular grids (rectangular, hexagonal/quincunx)
  with row spacing 3–7 m; trellis wires (vineyards, hop gardens) are thin-wire hazards like power
  lines; oil palm plantations in the tropics.
- **Hedgerows and shelterbelts**: linear vegetation along field edges. Important cover and
  acoustic barriers. Generate as linear features with space-colonization shrubs.
- **Farm infrastructure**: barns, silos, greenhouses/polytunnels (large reflective arrays, e.g. the
  Almería "sea of plastic"), irrigation pivots, fences, farm tracks.

---

## 6. Vegetation LOD

| LOD | Representation | Consumers |
|---|---|---|
| V3 (near, < 50–100 m) | Full mesh: trunk/branch generalised cylinders + leaf cards or leaf meshes | Camera, close acoustic scattering |
| V2 | Simplified mesh: trunk + major branches; foliage as clustered cards / billboard clouds (Décoret et al. 2003, https://doi.org/10.1145/882262.882326) | Camera mid-range |
| V1 | Impostors (octahedral impostors) for rendering; **crown envelope solids with a participating-medium attribute** (leaf area density, gap fraction) for rays | Camera far, LOS queries |
| V0 | Statistical canopy layer: canopy height raster + cover fraction + LAI + stem density + species mix | Regional acoustics and probabilistic LOS |

Key points:
- **Volumetric/statistical vegetation is the right model for simulation queries.** A ray through
  a crown envelope gets transmission T = exp(−G·LAD·ℓ/cosθ) (Beer–Lambert with leaf angle
  distribution G ≈ 0.5 for spherical leaf angles) instead of intersecting thousands of leaves.
  The same envelope carries acoustic **extinction/scattering per octave band**. Bruneton & Neyret
  2012 (https://doi.org/10.1111/j.1467-8659.2012.03036.x) show the rendering side of this.
- **Determinism across LODs**: a tree's position, species, height, crown radius and seed must be
  identical in all LODs (generated from the placement layer, not from the LOD mesh), so that a
  LOS test at V1 agrees statistically with V3.
- Modern renderers (UE5 Nanite-based foliage, *verify version*) reduce the need for impostors in
  *rendering*. Simulation queries still benefit from volumetric proxies.
- **Thin structure aliasing**: branches and wires are sub-pixel at distance. Camera simulation
  needs coverage-correct rendering (alpha-to-coverage, supersampling), or detection models will be
  wrong.

---

## 7. Acoustic modelling of vegetation (what geometry must provide)

- **ISO 9613-2** (2024 revision) has a simple foliage attenuation term (dB/m along the path through
  foliage, up to a maximum) and a newer detailed forest method based on forest parameters
  (https://www.iso.org/standard/74047.html; e.g. the CadnaA manual:
  https://doku.datakustik.com/CadnaA/en/BebaungBewuchs.html). Critiques: it ignores
  vegetation type and density, predicts zero effect for 1–2 m hedges, and underestimates at
  long distance (Inter-Noise 2023; Van Renterghem et al., White Rose eprint
  https://eprints.whiterose.ac.uk/id/eprint/86043/1/5697336.pdf).
- **Nord2000** and **Harmonoise** support forest scattering via stem density and trunk radius.
- Physics-based models need **stem density (stems/ha), mean trunk diameter (DBH), branch volume,
  leaf area density, ground flow resistivity** and canopy height. These are exactly the V0/V1
  layer parameters. Tarrero et al. 2008 (Applied Acoustics, *verify DOI*
  https://doi.org/10.1016/j.apacoust.2007.05.006) compare forest measurements with Nord2000;
  Swearingen & White 2007 (JASA, https://doi.org/10.1121/1.2392829) study scattering, refraction and
  ground effect in a pine forest; Huisman & Attenborough 1991 (JASA,
  https://doi.org/10.1121/1.400922) cover reverberation in a pine forest.
- Rule of thumb: dense vegetation gives about 1–2 dB(A) per 10 m in the road-noise band (Austrian
  LAUB study, https://projekte.ffg.at/anhang/5fd0a43a4199f_20201118_LAUB_v1.2.pdf), more at high
  frequency, and the ground effect of soft forest floor often dominates.

**So:** the vegetation generator must emit, per forest stand or cell, a parameter set
`{stem_density, dbh_mean, dbh_sd, height_mean, crown_base, LAD_profile(z), leaf_state, species_mix,
ground_type}`. This is consistent with explicit trees when those are instantiated.

---

## 8. Recommendations

1. **Placement layer first**: canopy height model (real or EcoLearn-style synthetic) → species
   and size distribution (multi-class point process / ecosystem simulation) → stable per-plant
   records (ID, position, species, height, DBH, crown dims, seed).
2. **Plant geometry on demand** from a species library (Weber–Penn parameters for speed;
   self-organising/space colonization for edge and urban trees), at V3/V2 only near consumers.
3. **Volumetric crown envelopes and statistical stand parameters** for LOS and acoustics,
   always available.
4. **Season** (leaf on/off, crop height, snow) is a scenario parameter that changes attributes,
   not placement.
5. Dunes from a Werner/Desertscapes CA on the terrain heightfield layer; grassland and crops as
   volumetric layers plus ground impedance.

---

## References

- Prusinkiewicz & Lindenmayer, *The Algorithmic Beauty of Plants*, 1990 — http://algorithmicbotany.org/papers/#abop
- Weber & Penn, "Creation and rendering of realistic trees", SIGGRAPH 1995 — https://doi.org/10.1145/218380.218427
- Runions, Lane, Prusinkiewicz, "Modeling trees with a space colonization algorithm", NPH 2007 — https://doi.org/10.2312/NPH/NPH07/063-070
- Palubicki et al., "Self-organizing tree models for image synthesis", SIGGRAPH 2009 — https://doi.org/10.1145/1531326.1531364
- Stava et al., "Inverse procedural modelling of trees", CGF 2014 — https://doi.org/10.1111/cgf.12282
- Deussen et al., "Realistic modeling and rendering of plant ecosystems", SIGGRAPH 1998 — https://doi.org/10.1145/280814.280898
- Makowski et al., "Synthetic silviculture", SIGGRAPH 2019 — https://doi.org/10.1145/3306346.3323039
- Gain et al., "EcoBrush", CGF 2017 — https://doi.org/10.1111/cgf.13116
- Kapp et al., "Data-driven authoring of large-scale ecosystems" (EcoLearn), SIGGRAPH Asia 2020 — https://doi.org/10.1145/3414685.3417848 ; https://hal.archives-ouvertes.fr/hal-03007259
- Ecormier-Nocca et al., "Accurate synthesis of multi-class disk distributions", CGF 2019 — https://doi.org/10.1111/cgf.13631
- Ecormier-Nocca et al., "Authoring consistent landscapes with flora and fauna", SIGGRAPH 2021 — https://doi.org/10.1145/3450626.3459952
- Bridson, "Fast Poisson disk sampling in arbitrary dimensions", SIGGRAPH sketches 2007 — https://doi.org/10.1145/1278780.1278807
- Raistrick et al., "Infinite Photorealistic Worlds using Procedural Generation" (Infinigen), CVPR 2023 — https://arxiv.org/abs/2306.09310
- Werner, "Eolian dunes: computer simulations and attractor interpretation", Geology 1995 — https://doi.org/10.1130/0091-7613(1995)023%3C1107:EDCSAA%3E2.3.CO;2
- Paris et al., "Desertscapes simulation", CGF 2019 — https://doi.org/10.1111/cgf.13815 ; project page https://perso.liris.cnrs.fr/aparis/public_html/projects/paris2019_Deserts.html
- Nishimori & Ouchi, "Formation of ripple patterns and dunes by wind-blown sand", PRL 1993 — https://doi.org/10.1103/PhysRevLett.71.197
- Décoret et al., "Billboard clouds for extreme model simplification", SIGGRAPH 2003 — https://doi.org/10.1145/882262.882326
- Bruneton & Neyret, "Real-time realistic rendering and lighting of forests", CGF 2012 — https://doi.org/10.1111/j.1467-8659.2012.03036.x
- Lang et al., "A high-resolution canopy height model of the Earth", Nat. Ecol. Evol. 2023 — https://doi.org/10.1038/s41559-023-02206-7
- Tolan et al., "Very high resolution canopy height maps from RGB imagery...", RSE 2024 — https://doi.org/10.1016/j.rse.2023.113888 (*verify*)
- ESA WorldCover — https://esa-worldcover.org ; Dynamic World — https://dynamicworld.app ; RESOLVE Ecoregions — https://ecoregions.appspot.com ; Köppen–Geiger — https://www.gloh2o.org/koppen/
- ISO 9613-2:2024 — https://www.iso.org/standard/74047.html ; CadnaA foliage docs — https://doku.datakustik.com/CadnaA/en/BebaungBewuchs.html
- Swearingen & White, JASA 2007 — https://doi.org/10.1121/1.2392829 ; Huisman & Attenborough, JASA 1991 — https://doi.org/10.1121/1.400922 ; Tarrero et al., Applied Acoustics 2008 — https://doi.org/10.1016/j.apacoust.2007.05.006 (*verify*)
- Van Renterghem et al., vegetation/noise (White Rose eprint) — https://eprints.whiterose.ac.uk/id/eprint/86043/1/5697336.pdf
- LAUB study (FFG) — https://projekte.ffg.at/anhang/5fd0a43a4199f_20201118_LAUB_v1.2.pdf
- USDA CropScape — https://nassgeodata.gmu.edu/CropScape/
