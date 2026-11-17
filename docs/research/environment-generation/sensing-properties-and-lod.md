# Cross-cutting: LOD ladders and sensing-relevant properties

Part of the [environment-generation research set](README.md). This file collects, across all
environment types, (a) what level-of-detail variants generators must produce, and (b) which
geometric and material properties matter for **acoustic** simulation (microphone arrays locating
drones, including reflections), **camera** simulation, and **vulnerability surveys**. Simulation
methods themselves are covered by another research thread. This file states what the *environment
generators* must output.

Research status: November 2026, mostly from prior knowledge. Numeric values are typical
engineering ranges for plausibility, not design data. Verify against the cited standards before
any quantitative use.

---

## 1. Scale argument: acoustic LOD can be much coarser than visual LOD

Drone acoustic signatures: blade-passing frequency (BPF) = rotor RPM/60 × blade count, typically
about 100–600 Hz for small multirotors, with strong harmonics up to several kHz and broadband
noise above. Wavelength λ = c/f ≈ 343/f:

| f | 100 Hz | 250 Hz | 500 Hz | 1 kHz | 2 kHz | 4 kHz |
|---|---|---|---|---|---|---|
| λ | 3.4 m | 1.4 m | 0.69 m | 0.34 m | 0.17 m | 0.09 m |

Consequences for generators:
- Geometric features **much smaller than λ/4 to λ/2** do not need to be explicit geometry for
  acoustics. They act as **surface roughness → scattering coefficient**. For drone bands that
  means features under roughly 0.1–0.3 m (window mullions, brick relief, cornices, small
  furniture) are attributes, not faces.
- Features **comparable to λ** (balconies, window recesses 0.2–0.5 m deep, parapets, fences, car
  bodies, tree trunks) cause **diffraction and scattering**. Represent explicitly at near-field
  LODs, or as scattering with frequency dependence.
- **Large planar faces** (facades, roofs, ground, water) give specular reflections. Their *position
  and orientation accuracy* matters more than detail. Moving a reflector 0.5 m along its normal
  changes a near-normal reflection path by up to about 1 m, i.e. about 3 ms of delay. That is
  significant for TDOA localisation that uses or rejects reflections.
- **Edges** (roof eaves, building corners, barrier tops, ridges) are diffraction sources. The
  B-rep edge graph is directly useful here: acoustic solvers (image-source + UTD/BTM edge
  diffraction) need the list of **convex wedge edges with their wedge angles**. Generators should
  produce clean, merged coplanar faces so that spurious edges do not appear. Tessellation
  creases are not acoustic edges, so the B-rep must distinguish true geometric edges from
  tessellation edges.
- The Fresnel zone of a reflection path, about √(λ·d) for distance d (e.g. ≈ 6 m at 500 Hz,
  50 m), tells how large a reflector must be to reflect fully. It also says **a reflector
  needs detail only within its Fresnel zones for the active sensor–source pairs**. That is a
  natural driver for query-dependent LOD.

For **cameras**, the relevant scale is pixel footprint: at 1 km with a 1 mrad/pixel sensor, one
pixel is about 1 m; at 100 m it is about 0.1 m. Silhouettes (skyline, treeline, wires against sky)
matter most for detection, because drones are detected against sky or clutter backgrounds.

---

## 2. Unified LOD ladder (all domains)

| Level | Purpose | Buildings | Terrain | Roads | Vegetation | Water | Infrastructure |
|---|---|---|---|---|---|---|---|
| **S (statistical)** | Regional propagation, probabilistic LOS | Urban canopy stats (coverage, height distribution, H/W) | ≥ 30 m grid | Density, class | Canopy height, cover, LAI, stem density | Mask + level | Obstacle points with heights |
| **0** | 2D / footprints | LOD0 footprints | 10–30 m | Graph | Stand polygons | Polygons, centrelines | Lines/points |
| **1** | Coarse 3D | LOD1 blocks | 2–10 m + breaklines | Ribbons | Crown envelopes (medium) | Flat surfaces | Envelopes |
| **2** | Default urban sim | LOD2.x roofs, superstructures | 0.5–2 m conformed | Lane surfaces, kerbs | Envelopes + trunks | Channels, banks | Catenaries, lattices as wireframes |
| **3** | Near-field camera & acoustics | LOD3 openings, balconies | ≤ 0.5 m, micro-relief | Markings, furniture | Mesh trees | Waves, rapids | Full assets |
| **4 / interior** | Indoor | Rooms, stairs | — | — | — | — | Interior plant |

Rules:
1. **Identity persists across levels.** The LOD3 window belongs to the LOD2 wall face. The V3 tree
   is the same record as the V0 stand member. Use stable IDs from (source ID, rule path, index).
2. **Coarser levels are conservative or unbiased summaries of finer ones.** The LOD1 block
   height should be the mean roof height (unbiased for acoustics) or the max (conservative for
   LOS). Store both as attributes and let the consumer choose.
3. **LOD is chosen per query and per region** (Fresnel-zone and pixel-footprint driven), not per
   camera. Generators must be *lazy* and *local*.

---

## 3. Acoustic properties generators must attach

### 3.1 Surfaces (per face or region)
- **Absorption coefficient α per octave band** (63 Hz–8 kHz), per ISO 354 random-incidence values.
  Typical outdoor values:

| Surface | α (250 Hz – 2 kHz, typical) |
|---|---|
| Concrete, asphalt, stone, glass (thick) | 0.01–0.05 |
| Brick (unpainted) | 0.03–0.07 |
| Metal cladding (sheet) | 0.05–0.1 plus panel resonance; corrugated scatters |
| Glazing (double, low frequency) | 0.1–0.3 at < 250 Hz (mass-spring), ~0.03 above |
| Timber cladding | 0.05–0.15 |
| Absorptive noise barrier | 0.6–0.95 |
| Open window / opening | Treat as coupling to the interior (≈ 1 for a large room at high frequency) |

- **Scattering coefficient s** (ISO 17497-1) from `roughness_scale` (amplitude, correlation
  length) as a function of frequency.
- **Transmission loss** for façades and windows when interior coupling is modelled.

### 3.2 Ground
Ground impedance is critical for microphone arrays near the ground. Use effective flow resistivity
σ (kPa·s/m²) with the Delany–Bazley/Miki one-parameter model, or the two-parameter variable-porosity
model (Attenborough). Nord2000 classes are a useful discrete vocabulary:

| Nord2000 class | σ (kPa·s/m²) | Typical ground |
|---|---|---|
| A | 12.5 | Very soft: fresh snow, moss-like forest floor |
| B | 31.5 | Soft forest floor, short dense heather |
| C | 80 | Uncompacted loose ground: turf, grass, loose soil |
| D | 200 | Normal uncompacted ground: forest floors, pasture |
| E | 500 | Compacted field and gravel |
| F | 2,000 | Compacted dense ground: gravel road, car park |
| G | 20,000 | Hard surfaces: asphalt, concrete |
| H | 200,000 | Very hard and dense: dense asphalt, concrete, water |

(Class values per the Nord2000 method as commonly tabulated *(verify against the Nord2000
documentation)*.) Ground type comes from land cover + OSM `surface` + season (snow, frozen,
wet). **Season and moisture change σ by orders of magnitude.**

### 3.3 Volumes and media
- **Vegetation**: stem density, DBH distribution, LAD profile, canopy height (see
  [vegetation-and-biomes.md](vegetation-and-biomes.md)).
- **Atmosphere** (not generated geometry, but scenario state): wind and temperature profiles.
  These dominate long-range outdoor propagation (refraction, shadow zones). Terrain and land
  cover affect the near-surface profiles (urban heat island, cold air pooling in valleys, water
  bodies).
- **Interiors**: room volume, surface area, mean α per band.

### 3.4 Sources (ambient noise and confusers)
Environment generators should emit **sound sources**: road traffic (by road class and traffic
volume; CNOSSOS-EU source model), rail, aircraft at airports, industrial plant, HVAC on roofs,
transformers (100/120 Hz tonal), wind turbines, rivers/weirs/surf, wind in vegetation, birds,
people/crowds at venues. Each source has position or line/area geometry, a spectrum (octave or
1/3-octave), directivity and a time profile. The 2D/3D geometry of a line source (road lane
centreline) comes straight from the road generator.

---

## 4. Camera properties

- **Materials**: PBR (base colour, roughness, metalness, normal), transmission and IOR for glazing
  and water, emissive for lights (night scenes), retroreflective for signs. Optional
  thermal-infrared properties (emissivity, thermal mass) if IR cameras are in scope (open
  question).
- **Thin-structure flags**: wires, fences, lattices, branches. The renderer must anti-alias them
  correctly or use coverage-based models.
- **Dynamic elements**: foliage wind animation, water waves, moving cranes, turbines, traffic,
  flags. These are false-alarm sources.
- **Sky/terrain boundary** accuracy (skyline) and **background class** per view direction (sky,
  vegetation, building, terrain) for detection-probability models.

---

## 5. Vulnerability-survey annotations

Generators should emit these as semantic annotations (points, polylines, polygons, volumes),
not leave them to post-hoc analysis:

| Annotation | Source generator |
|---|---|
| Rooftop access points (stair penthouse, hatch, ladder, fire escape) | Buildings, interiors |
| Landing/launch zones (flat, clear, ≥ 2×2 m; roofs, parking lots, clearings, fields) | Buildings, roads, vegetation, agriculture |
| Cover and concealment for operators (treelines, hedges, walls, vehicles, parapets) | Vegetation, infrastructure, buildings |
| Approach corridors (valleys, rivers, roads, power-line rights-of-way, rail) | Terrain, water, roads, infrastructure |
| Obstacles/hazards (wires, guy-wires, masts, cranes) with clearance volumes | Infrastructure |
| Sensor mounting candidates (poles, towers, roof edges, facades with power) | Infrastructure, buildings |
| Restricted airspace volumes (airport OLS, sensitive sites) | Infrastructure, urban layout |
| Perimeters (fences, walls, gates) | Infrastructure, urban layout |
| Occluded volumes (under bridges, overhangs, canopies, courtyards) | Roads, terrain, buildings, vegetation |

---

## 6. Implications for the kernel (summary for the B-rep/attribute researchers)

1. **Faces with holes** everywhere: wall openings, water surfaces with islands and tree stems,
   terrain with building pads and tunnel portals, roofs with skylights.
2. **Non-manifold elements**: zero-thickness sheets (fences, signs, panels, leaf cards), wire edges
   (power lines, guy-wires, lattice members) with no faces, dangling faces (canopies,
   awnings).
3. **Cellular volumes**: rooms, water bodies, soil layers and building interiors share faces.
   This is evidence for an explicit 3-cell (volumetric) layer on top of the surface topology.
4. **Exact embedded curves**: catenaries, clothoids, sine-generated river curves, offset curves
   (lane edges), profile curves (procedural extrusions). Tessellate on demand.
5. **Attributes** at all topological levels and as fields (rasters over faces): material,
   acoustic class, ground σ, roughness, provenance, confidence, stable IDs, LOD membership.
6. **Edge classification**: true geometric/feature edges vs. tessellation edges, with wedge angle,
   for diffraction.
7. **Query-dependent LOD** (Fresnel-zone, pixel-footprint) needs hierarchical bounding volumes
   with per-level error bounds (as 3D Tiles' geometric error).

---

## References

- ISO 9613-2:2024, Attenuation of sound during propagation outdoors — https://www.iso.org/standard/74047.html
- ISO 354 (absorption, reverberation room) — https://www.iso.org/standard/34545.html
- ISO 17497-1 (scattering coefficient) — https://www.iso.org/standard/31397.html
- Attenborough, Li, Horoshenkov, *Predicting Outdoor Sound*, Taylor & Francis 2007 — https://doi.org/10.1201/9781482295030 (*verify DOI*)
- Delany & Bazley, "Acoustical properties of fibrous absorbent materials", Applied Acoustics 1970 — https://doi.org/10.1016/0003-682X(70)90031-9
- Embleton, Piercy, Daigle, "Effective flow resistivity of ground surfaces determined by acoustical measurements", JASA 1983 — https://doi.org/10.1121/1.389747
- Nord2000 — DELTA/FORCE Technology reports (*verify current host*); CNOSSOS-EU, Commission Directive (EU) 2015/996 — https://eur-lex.europa.eu/eli/dir/2015/996/oj
- Svensson, Fred, Vanderkooy, "An analytic secondary source model of edge diffraction impulse responses" (BTM), JASA 1999 — https://doi.org/10.1121/1.428071
- 3D Tiles (geometric error) — https://github.com/CesiumGS/3d-tiles
