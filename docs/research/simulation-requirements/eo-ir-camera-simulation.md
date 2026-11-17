# EO/IR Camera Simulation: What the Environment Model Must Supply

Part of [simulation-requirements](README.md). Covers visible, near/short-wave IR and
thermal (MWIR/LWIR) camera simulation for drone detection, tracking and site surveys,
including synthetic training data (sim-to-real).

Source tags: **[web]** verified via web search in this research; **[K]** background
knowledge, not re-verified online.

---

## 1. Role of cameras in counter-UAS

Cameras are used (a) as primary detectors in wide-field staring arrays, (b) as
slew-to-cue confirmers on pan-tilt-zoom (PTZ) mounts cued by acoustic, radar or RF
sensors, (c) for classification (drone vs bird vs aircraft, payload detection), and
(d) for multi-view triangulation of swarm members. Thermal cameras work at night and
see motors and batteries as hot spots. Simulation is needed both to **generate training
data** for detectors and to **evaluate coverage and performance** at a site
(where can a camera at location X see a drone at altitude h?).

## 2. Pixel footprint and what drives LOD

Instantaneous field of view (IFOV) ≈ HFOV / horizontal pixels; footprint = range × IFOV.

| Camera | HFOV | Pixels | IFOV | Footprint @100 m | @500 m | @2 km |
|---|---|---|---|---|---|---|
| Wide visible (4K) | 90° | 3840 | 409 µrad | 4.1 cm | 20 cm | 82 cm |
| Medium visible (4K) | 30° | 3840 | 136 µrad | 1.4 cm | 6.8 cm | 27 cm |
| PTZ zoomed (1080p) | 3° | 1920 | 27 µrad | 2.7 mm | 1.4 cm | 5.5 cm |
| LWIR uncooled | 24° | 640 | 654 µrad | 6.5 cm | 33 cm | 1.3 m |
| MWIR cooled zoom | 2° | 1280 | 27 µrad | 2.7 mm | 1.4 cm | 5.5 cm |

A 0.35 m quadcopter at 2 km fills < 1 pixel in the wide visible camera and ~0.3 pixel
in the LWIR camera; detection there is a **sub-pixel or few-pixel contrast problem**.
At those scales the drone's detectability depends on **background clutter statistics**
(sky gradient, clouds, tree lines, building edges, wires, birds) and on atmospheric
contrast reduction, not on drone geometry detail.

LOD consequences:

* **Screen-space geometric error.** The rendered geometry for a sensor should have a
  geometric error ≤ ~0.5 pixel footprint at each point's range. This is the standard
  view-dependent LOD metric (as in OGC 3D Tiles "geometricError"), but evaluated **per
  sensor**, and many sensors (dozens of cameras plus a drone's own camera) render the same
  scene concurrently. LOD selection therefore must be per query/per ray, not one global
  cut of the scene.
* **Zoom.** A PTZ or long-focal-length camera at 2 km needs the same detail as a wide
  camera at 30 m. Detail must be **synthesised on demand** where a narrow field of view
  looks — a natural fit for procedural generation (generate higher LOD only in the frustum).
* **Silhouettes matter most.** Up-looking cameras see drones against the sky; the
  horizon line (roof lines, tree crowns, wires, antennas, cranes, street lights) is where
  false alarms and occlusion occur. Tree crown silhouettes need leaf-cluster-scale detail
  where they meet the sky; flat textured "billboard" trees produce unrealistic edges.
* **Thin structures.** Power lines, telephone wires, guy wires, fences, antennas and
  railings are high-contrast thin features that cause false alarms and are flight hazards.
  They are **curves** (catenaries) not surfaces: the environment model must represent
  wires as 1-D curve primitives with radius, rendered with sub-pixel coverage
  anti-aliasing. This links to the project's curve requirements.
* **Texture/material LOD.** Below one pixel, sub-pixel mixtures of materials matter
  (radiometrically correct mip-mapping / material aggregation), especially in IR where
  mixing temperatures is non-linear (radiance, not temperature, must be averaged).

## 3. Material and radiometric attributes

### 3.1 Visible (game-engine realism)

Physically based rendering (PBR, metallic-roughness as in glTF 2.0 / OpenUSD
UsdPreviewSurface / MaterialX): base colour, metallic, roughness, normal map, occlusion,
emissive, transmission, clear-coat, plus subsurface for foliage. Sufficient for
visible-band training data when combined with domain randomization; does not give
absolute radiometry.

### 3.2 Physics-based EO (DIRSIG-class)

DIRSIG (RIT) builds scenes from facets, path-traces radiometrically across wavelengths
with thermal self-emission, and outputs per-pixel truth ([web] DIRSIG overview). Materials
are **spectral**: BRDF models (Phong, Ward, Beard-Maxwell, Priest-Germer, measured), with
polarimetric variants as 4×4 Mueller matrices; emissivity derived from the hemispherical
reflectance via Kirchhoff's law ([web] DIRSIG material docs; Gartley dissertation).
Requirements:

* **Spectral reflectance** 0.4-2.5 µm (and to 14 µm for thermal) per material, ideally
  from measured libraries (e.g. ECOSTRESS/ASTER spectral library [K]).
* **BRDF parameters** per material; optional polarization.
* **Texture-driven material maps**: DIRSIG maps image textures to material mixtures, so
  each surface needs a material ID map, not just colours.

### 3.3 Thermal (MWIR 3-5 µm, LWIR 8-14 µm)

Apparent temperature depends on heat transfer history, so a thermal solver must run
over the diurnal cycle (24-72 h spin-up) before rendering. Inputs per surface/part:

* **Emissivity** (spectral or band-averaged) and **solar absorptivity**.
* **Thermal properties of layered constructions**: conductivity, specific heat, density
  and thickness per layer (wall assemblies, roofs, road pavement layers, soil).
  DIRSIG does not derive thermal models automatically; these properties must be supplied
  ([web] DIRSIG material reference).
* **Internal boundary conditions**: heated interiors, HVAC exhausts, windows, vehicle
  engines, people (≈ 30-35 °C skin/clothing surface), animals, drone motors/batteries.
* **Geometry for radiative exchange**: shadowing over time, sky-view factor, mutual
  radiation between facing surfaces (street canyons), which requires watertight
  enough geometry to compute view factors and solar shadows reliably.
* **Water** (high emissivity ~0.98, high thermal inertia), **vegetation** (transpiration,
  near-air temperature, low thermal inertia), **soil moisture**.

MuSES (ThermoAnalytics) computes target and terrain temperatures and at-sensor radiance
from visible to mm-wave, and has been coupled to CameoSim (terrain geometry exported into
MuSES) and to DIRSIG for vehicle signatures validated against calibrated imagery ([web]).
**Requirement:** UberTopo should export a **thermal mesh**: surfaces with layered
construction attributes and a coarse-enough tessellation (thermal nodes typically
0.1-1 m) for a finite-difference/finite-volume heat solver, plus the fine render
geometry that references the thermal nodes for temperature lookup.

### 3.4 Atmosphere and illumination

* Path transmittance and path radiance (MODTRAN/LibRadtran class models [K]); haze,
  fog, rain, snow; visibility parameter.
* Sky radiance (physically based sky models), clouds (as drone backgrounds and shadow
  casters), sun/moon position from date, time and geodetic location.
* Optical turbulence (heat shimmer) for long-range zoom cameras.
* **Requirement:** environment model must be **geo-referenced** (lat/lon/height, datum)
  with date/time so sun position and atmospheric profiles are consistent with the
  acoustic and RF simulations.

## 4. Sensor model effects (not UberTopo's job, but needs inputs)

MTF/PSF, noise (shot, read, fixed-pattern; NETD for thermal), motion blur, rolling
shutter, auto-exposure, lens flare from sun, compression artefacts, platform vibration.
The environment model's role is to supply correct radiance and per-pixel truth.

## 5. Synthetic data for drone detection (sim-to-real)

* DLR (Electronics 2023): Unreal + AirSim pipeline; detectors trained only on synthetic
  data dropped significantly on real data; adding a small share of real data helped;
  physically realistic 3D simulation is suitable for training ([web]).
* Cranfield (arXiv 2411.09077): **structured domain randomization**; Faster R-CNN trained
  on pure synthetic data reached AP50 97.0 % on the real MAV-Vid set vs 97.8 % for real
  training data ([web]).
* University of Ottawa A2A-SDD: multi-view air-to-air drone dataset from AirSim with
  detection and XYZ annotations ([web]).
* SimD3: synthetic drone detection dataset on Cosys-AirSim / UE5 ([web], summary only).
* Real benchmarks for validation: Anti-UAV (RGB+IR tracking) [K: https://arxiv.org/abs/2101.08466],
  MAV-Vid, Drone-vs-Bird challenge [K], Svanström multi-sensor dataset [K].
* Photorealistic 3D Tiles (Google/Cesium) streamed into Isaac Sim are blocky and blurry
  at 10-500 ft drone altitudes, and users fall back on local photogrammetry ([web]
  Cesium forum). **Mesh-from-photogrammetry is not enough for low-altitude work:** it
  lacks semantics, materials, thin structures, vegetation structure, and interiors of
  occluders. This is UberTopo's opportunity: procedural detail with semantics.

Lessons for the environment model:

1. **Variety beats fidelity** for visible-band detector training, as long as the
   variation spans reality (structured domain randomization). Procedural generation must
   expose **randomizable parameters with controllable distributions** (building styles,
   materials, vegetation species/density, clutter objects, weather, time of day).
2. **Context matters**: drones near tree lines, against buildings, among birds; scenes
   must be semantically plausible (structured DR), not random-object soup.
3. **Thermal sim-to-real** requires physics; randomizing temperatures independently per
   object creates unrealistic contrast. Thermal attributes should be physically coupled.

## 6. Ground truth and labels

From the environment model the renderer needs, per pixel: **semantic class**
(hierarchical: building/roof/facade/window; vegetation/tree/crown/trunk; road/lane;
water; wire; sky), **instance ID** (persistent across frames and LODs), **material ID**,
depth/range, surface normal, motion vectors. Per object: 2-D and 3-D boxes, occlusion
and truncation fraction, pixel area, distance, and for drones, rotor/attitude state.
Labels must come from the **same IDs** used by acoustic and RF ground truth so
multi-modal fusion can be scored.

## 7. Derived requirements (EO/IR)

| # | Requirement | Rationale |
|---|---|---|
| EO-1 | View-dependent LOD evaluated per sensor/per ray with error ≤ 0.5 px footprint | many sensors, wide zoom range |
| EO-2 | On-demand procedural refinement inside narrow frusta | PTZ/zoom cameras at km range |
| EO-3 | Curve primitives for wires/cables (catenary + radius), poles, antennas | false alarms, flight hazards |
| EO-4 | Vegetation with crown structure at leaf-cluster scale near silhouettes; instances + volumes | horizon clutter, partial occlusion |
| EO-5 | PBR material set for game engines AND spectral/BRDF material IDs for physics-based renderers, from one material library | two fidelity tiers |
| EO-6 | Thermal construction attributes (layers: k, cp, ρ, thickness; emissivity; absorptivity; internal sources) | thermal solver |
| EO-7 | Thermal-node mesh export plus fine render mesh mapped to it | MuSES/DIRSIG-style workflows |
| EO-8 | Geo-referencing (datum, lat/lon/h) + date/time for sun/sky; atmospheric profile hooks | consistent illumination |
| EO-9 | Per-pixel semantic class, instance ID, material ID export; persistent IDs | training labels, fusion scoring |
| EO-10 | Exposed, seedable randomization parameters for structured domain randomization | sim-to-real |
| EO-11 | Dynamic elements: vehicles, people, birds, foliage sway, water surface, clouds | clutter and false alarms |
| EO-12 | Export to OpenUSD and glTF (+ 3D Tiles for streaming) with semantics preserved | Isaac Sim / Unreal / Cesium |

## References

* [web] DIRSIG overview (RIT). https://dirsig.cis.rit.edu/docs/reveal.js/overview.html
* [web] DIRSIG reflectance/emissivity properties. https://dirsig.cis.rit.edu/docs/d4-refl-props.pdf
* [web] Gartley, polarimetric BRDF modelling for DIRSIG. https://dirsig.cis.rit.edu/docs/gartley.pdf
* [web] Polarimetric modeling of scenes in the thermal IR. https://core.ac.uk/works/7960933
* [web] MuSES EO/IR sensor simulation. https://www.thermoanalytics.com/application/eo-ir-scene-simulation-for-ai-applications/
* [web] ThermoAnalytics EO-IR services. https://www.thermoanalytics.com/thermoanalytics-eo-ir-services-overview/
* [web] Radiometrically accurate thermal vehicle targets (MuSES+DIRSIG).
  https://ersearch2.cvtisr.sk/vufind/SummonRecord/FETCH-LOGICAL-b575-9cc2b8b7ba6f4959dd3896405e8ce924ea6743e25b1b6b5efcb1e1560ec7ca603
* [web] Dieter et al. (DLR), quantifying the sim-reality gap for drone detection, 2023. https://elib.dlr.de/195082/
* [web] Wisniewski et al., drone detection trained on pure synthetic data. https://arxiv.org/html/2411.09077v1
* [web] A2A-SDD (Ottawa), DASC 2023. https://carg-uottawa.github.io/files/HA_dasc2023.pdf
* [web] Synthetic payload detection (Ottawa), SAS 2023. https://carg-uottawa.github.io/files/HA_sas2023.pdf
* [web] TUM thesis on synthetic drone data. https://mediatum.ub.tum.de/doc/1721356/erncmqmyemcpiuubk811mgqcc.pdf
* [web] Cesium + Isaac Sim low-altitude tile fidelity thread.
  https://community.cesium.com/t/cesium-isaac-sim-terrain-goes-black-at-drone-scale-10-500ft-google-photorealistic-3d-tiles-not-loading-close-up/45997
* [web] Cesium for Omniverse. https://cesium.com/platform/cesium-for-omniverse/
* [K] Jiang et al., Anti-UAV benchmark. https://arxiv.org/abs/2101.08466
* [K] Tobin et al., domain randomization, 2017. https://arxiv.org/abs/1703.06907
* [K] Prakash et al., structured domain randomization, 2019. https://arxiv.org/abs/1810.10093
* [K] OGC 3D Tiles (geometricError / screen-space error). https://www.ogc.org/standard/3dtiles/
* [K] glTF 2.0 PBR. https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
* [K] MODTRAN. https://www.spectral.com/our-software/modtran/
