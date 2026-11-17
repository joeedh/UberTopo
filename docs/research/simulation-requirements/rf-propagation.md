# RF Sensing and Propagation: Radar, RF Detection, GNSS

Part of [simulation-requirements](README.md). A brief survey of what radio-frequency
simulations need from the environment model: counter-UAS radar, passive RF detection
and direction finding of drone control/video links, and GNSS multipath/denial affecting
drone navigation.

Source tags: **[web]** verified via web search in this research; **[K]** background
knowledge, not re-verified online.

---

## 1. Frequencies and the overlap with acoustics

| System | Frequency | λ |
|---|---|---|
| Legacy control links (LoRa-class, 433/868/915 MHz) | 0.43-0.92 GHz | 69-33 cm |
| Video/telemetry (1.2-1.3 GHz) | 1.3 GHz | 23 cm |
| GNSS L1 / L5 | 1.575 / 1.176 GHz | 19 / 25.5 cm |
| Wi-Fi-style C2/video | 2.4 / 5.8 GHz | 12.5 / 5.2 cm |
| Cellular drones (LTE/5G FR1) | 0.7-6 GHz | 43-5 cm |
| S-band / X-band / Ku-band radar | 3 / 10 / 15 GHz | 10 / 3 / 2 cm |
| Ka / W-band radar | 35 / 94 GHz | 8.6 / 3.2 mm |

**Key observation:** drone acoustics (≈ 100 Hz-16 kHz, λ ≈ 3.4 m - 2 cm) and drone RF
(≈ 0.4-15 GHz, λ ≈ 70 cm - 2 cm) span **almost the same wavelength range**. The same
"λ-relative LOD" machinery (simplify to λ/8, convert sub-λ detail into roughness/
scattering, keep diffracting edges, sensor-centric refinement) serves both. That is a
strong argument for building the LOD system around a **tolerance parameter in metres**
plus edge preservation, not around visual criteria.

## 2. Propagation methods and their geometry needs

* **Ray tracing (shooting-and-bouncing rays + image method)** with UTD edge diffraction
  and diffuse scattering is the standard for site-specific propagation. Commercial:
  Remcom Wireless InSite [K: https://www.remcom.com/wireless-insite-em-propagation-software],
  Altair WinProp/Feko [K], MATLAB `raytrace` [K]. Open-source/differentiable:
  **NVIDIA Sionna RT**, built on Mitsuba 3 (scene XML format, Blender export), with
  differentiable path computation, radio maps, and gradient-based optimization of
  materials, antenna patterns and positions ([web] Sionna RT paper and docs).
* **Materials.** Sionna implements all ITU-R P.2040-3 materials (concrete, brick,
  plasterboard, wood, glass, ceiling board, chipboard, floorboard, metal, and
  very-dry/medium-dry/wet ground) with permittivity and conductivity updated
  automatically with frequency; custom materials specify real relative permittivity
  (≥ 1), conductivity and permeability ([web] Sionna radio-materials docs). Newer work
  cites P.2040-4 for 77 GHz ([web] Sim2Radar). Scattering coefficient, cross-polarization
  discrimination and scattering pattern (Lambertian / directive / backscattering) are
  further per-material parameters [K, Sionna docs].
* **Empirical / terrain models** for site surveys over km: ITU-R P.1812 (terrain path
  profile + clutter), P.526 (diffraction), P.833 (vegetation attenuation per metre)
  [K]. CloudRF's C-UAS guidance recommends P.1812 modelling for detector siting
  ([web], vendor material). Inputs: DTM/DSM, land-cover clutter class and clutter height.
* **Full-wave** (FDTD/MoM/FEM) only for the drone itself (RCS, antenna patterns) or
  small regions.

Geometry requirements match acoustics closely:

1. Planar facets with **consistent normals**, polygons with holes for windows (glass is a
   different material; **low-E metallic-coated glass** is a strong reflector, so window
   material type matters).
2. **Explicit diffracting edges** with wedge angle and both face materials — the same
   edge list as acoustics (UTD was originally an EM theory).
3. **Layered wall constructions** (multi-layer slabs with thickness per layer) for
   penetration loss; ITU-R P.2040 gives formulas for single and multi-layer slabs [K].
4. **Thickness** of thin structures (walls, roofs) because transmission through them
   matters for indoor-to-outdoor links and radar penetration.
5. **Vegetation** as volumes with specific attenuation (dB/m), and foliage motion (wind)
   which produces Doppler-spread clutter for radar.
6. **Terrain DTM + DSM + clutter classes** for empirical models over km.

## 3. Counter-UAS radar

* Small drones have RCS ~0.001-0.1 m², similar to birds; detection relies on Doppler,
  micro-Doppler (rotor blade flashes) and tracking. Physics-based micro-Doppler
  simulation using detailed drone component models matches measured spectra for several
  drone types ([web] Cranfield/St Andrews, IET RSN 2024); flight-dynamics-aware rotor rates
  improve realism ([web] EuRAD 2022); full-wave X-band RCS of a DJI S900 was compared
  with measurements ([web] IRS 2021); RAPID-SIM simulates staring FMCW radar for swarms
  ([web]).
* The environment contributes **clutter** (buildings, terrain, moving foliage, vehicles,
  water waves), **multipath** (ground/water lobing that creates nulls at certain target
  elevations), and **masking** (radar shadow behind buildings and terrain). MATLAB
  Radar Toolbox covers clutter/multipath statistically ([web]); site-specific clutter
  from ray tracing against city and tree geometry is less mature (search found little).
* **Requirements:** per-material RCS-relevant properties (permittivity, conductivity,
  roughness), facet geometry at ≤ λ/8 of the radar band near the radar's line of sight
  (≈ 4 mm at X-band — impractical everywhere, so use **roughness/scattering statistics
  for sub-λ detail** and explicit geometry for large flat surfaces, edges and dihedral/
  trihedral corners, which are strong radar reflectors); dynamic foliage parameters
  (sway amplitude, frequency) for Doppler clutter; water surface state.
* **Bird models** (for false-alarm studies) belong to the scenario layer but need habitat
  attributes in the environment (water bodies, trees, landfill) to place realistically.

## 4. Passive RF detection and direction finding

Passive sensors detect drone C2 and video emissions, classify protocol, estimate angle
of arrival with antenna arrays, and locate by triangulation or TDOA across nodes. The
multipath problems mirror acoustics: reflections from facades produce false bearings;
NLOS reception at corners. Needs the same ray-traced channel model (Sionna-class) with
antenna patterns and node mount geometry; again **sensor-centric detail** near the
receiver matters most. Typical detection ranges are 1-5 km in open terrain, less in
cities [K].

## 5. GNSS multipath and denial (drone navigation in urban canyons)

* Building geometry causes NLOS reception and multipath, giving pseudorange errors of
  tens of metres in urban canyons. Simulation uses ray tracing on 3D city models with
  satellite ephemerides; examples include the Hannover multipath simulator on triangle
  meshes, ISAE-SUPAERO's SE-NAV tracer on a 3D Toulouse model feeding an EKF, Ollander
  et al.'s 12 synthetic urban environments with single-reflection tracing, and DLR's
  SNACKS ([web] survey results).
* **Shadow matching** (Groves, UCL) compares observed LOS/NLOS patterns to predictions
  from a 3D model; reported < 2 m horizontal error vs ~25 m for conventional GNSS in
  London ([web]). Again a **run-time consumer** of the environment model.
* GNSS jamming/spoofing studies need jammer-to-receiver propagation over terrain and
  buildings (empirical models or ray tracing), which is how a defended site would
  degrade hostile drones' navigation — and how a swarm simulation must model degraded
  navigation of the attacker.
* **Requirements:** building models at CityGML LoD1-LoD2 level with ~0.5-1 m
  accuracy (L1 λ = 19 cm, but GNSS LOS/NLOS classification only needs building edges to
  about a metre); roof edges and heights are the critical parameters; material class
  (glass/metal vs concrete) affects reflection strength; geo-referencing must be exact
  (satellite geometry is in ECEF).

## 6. Derived requirements (RF)

| # | Requirement |
|---|---|
| RF-1 | Same λ-tolerance LOD and edge machinery as acoustics; tolerance as a numeric input |
| RF-2 | Per-material EM properties: ITU-R P.2040 material class or (ε′, σ, μr) with frequency model; roughness; scattering coefficient & pattern |
| RF-3 | Layered construction (thickness per layer) shared with thermal and acoustic transmission |
| RF-4 | Window glazing type (plain vs metallized low-E) as an attribute |
| RF-5 | Vegetation: volume with specific attenuation model inputs (density, LAD, water content) + sway dynamics |
| RF-6 | DTM/DSM/clutter-class raster exports for ITU-R P.1812-style km-scale models |
| RF-7 | Export to Mitsuba XML (Sionna RT) with material names; export to common CAD/mesh formats for Wireless InSite |
| RF-8 | ECEF/geodetic exactness; satellite-visibility (sky mask) queries at points |
| RF-9 | Dynamic objects (vehicles, foliage, water waves) for Doppler clutter |

## References

* [web] Hoydis et al., "Sionna RT: Differentiable Ray Tracing for Radio Propagation
  Modeling", 2023. https://arxiv.org/pdf/2303.11103
* [web] Sionna radio materials API. https://nvlabs.github.io/sionna/rt/api/radio_materials.html
* [web] Sionna ITU material source. https://nvlabs.github.io/sionna/_modules/sionna/rt/radio_materials/itu_material.html
* [web] Sim2Radar, VLM-guided scene reconstruction for radar sim-to-real. https://arxiv.org/pdf/2602.13314
* [K] ITU-R P.2040, effects of building materials and structures on radiowave propagation. https://www.itu.int/rec/R-REC-P.2040
* [K] ITU-R P.833, attenuation in vegetation. https://www.itu.int/rec/R-REC-P.833
* [K] ITU-R P.1812, path-specific propagation prediction. https://www.itu.int/rec/R-REC-P.1812
* [K] ITU-R P.526, propagation by diffraction. https://www.itu.int/rec/R-REC-P.526
* [web] CloudRF, optimising drone detection with RF simulation. https://cloudrf.com/optimising-drone-detection-with-rf-simulation/
* [web] Realistic simulation of drone micro-Doppler signatures (Cranfield). https://dspace.lib.cranfield.ac.uk/items/3165f899-9a5c-4ebb-a305-e9deb606f4ba
* [web] New simulation methodology for drone micro-Doppler (St Andrews). https://research-portal.st-andrews.ac.uk/en/publications/a-new-simulation-methodology-for-generating-accurate-drone-micro-/
* [web] NATO STO MP-MSG-SET-183 (C-UAS modelling and simulation). https://www.sto.nato.int/publications/STO%20Meeting%20Proceedings/STO-MP-MSG-SET-183/MP-MSG-SET-183-01.pdf
* [web] MathWorks radar data synthesis. https://au.mathworks.com/help/radar/radar-data-synthesis.html
* [web] Pisova & Chod, GNSS signal reception in urban canyons with 3D models (AEEE). https://advances.vsb.cz/index.php/AEEE/article/view/1291
* [web] Bourdeau et al., 3D-model-aided GNSS (ION GNSS 2012). https://www.tesa.prd.fr/documents/26/bourdeau_sahmoudi_ion_gnss_2012.pdf
* [web] Ollander et al., synthetic urban environments for multipath. https://www.preprints.org/manuscript/202012.0647/v1/download
* [web] Groves, shadow matching. https://discovery-pp.ucl.ac.uk/1394444/1/0681.pdf
* [K] Remcom Wireless InSite. https://www.remcom.com/wireless-insite-em-propagation-software
