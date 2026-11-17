# Outdoor and Urban Acoustic Propagation: What the Simulations Need

Part of [simulation-requirements](README.md). This file covers the physics and numerical
methods for sound travelling from a drone to a microphone outdoors, and turns them into
requirements on the UberTopo environment model. The localization side (arrays, TDOA,
datasets) is in [acoustic-drone-localization.md](acoustic-drone-localization.md).

Source tags: **[web]** = found and checked via web search during this research;
**[K]** = from background knowledge, citation not re-verified online (direct page fetches
were blocked in this session; only search snippets were available).

---

## 1. Why acoustics drives the hardest geometry requirements

Acoustic drone detection works at short-to-medium range, roughly 100-500 m for small
multirotors in real conditions ([web] practitioner and peer-reviewed reports, see
[acoustic-drone-localization.md](acoustic-drone-localization.md)). In that range the sound
reaching a microphone is a **sum of a direct path and many delayed copies**: ground
reflection, facade reflections, edge-diffracted paths around corners and roof lines,
diffuse scattering from rough facades and vegetation, all distorted by wind, temperature
gradients and turbulence. The project's own requirements call out "dealing with sonic
reflections". Unlike a camera, a microphone cannot ignore geometry that it does not "see":
a building behind the sensor causes an echo, and a roof edge that blocks the direct path
creates a diffracted arrival whose apparent direction is the edge, not the drone.

So the acoustic simulation needs:

* geometry that is correct to a fraction of a wavelength **near sensors**,
* explicit **diffracting edges**,
* **frequency-dependent surface attributes** (absorption, scattering, impedance, transmission),
* **volumetric attributes** for vegetation and the atmosphere,
* and a **time-domain** treatment of moving sources (Doppler, retarded time).

## 2. The frequency range and wavelength scale

Drone noise is tonal at the blade-passing frequency (BPF) and its harmonics, on top of
broadband noise. BPF = B·Ω/60 (B blades, Ω RPM). Measured quadcopter BPFs are around
150-250 Hz in hover, and tones at BPF, shaft rate and their harmonics dominate the
spectrum up to about 6 kHz, with a broadband hump in the mid range ([web] Intaratep et
al., AIAA 2016-2873). Larger drones and fixed-wing (propeller, engine) types go lower
(tens of Hz); small FPV racers with high RPM and 3-blade props go higher. Microphones and
analysis bands of interest therefore span about **50 Hz - 16 kHz**, with most detection
energy at **100 Hz - 4 kHz**.

With c ≈ 343 m/s (20 °C):

| f | λ | λ/8 (specular-surface tolerance) | Typical feature that matters |
|---|---|---|---|
| 50 Hz | 6.9 m | 0.86 m | building masses, terrain berms |
| 100 Hz | 3.4 m | 0.43 m | building blocks, walls, large roofs |
| 250 Hz | 1.4 m | 0.17 m | facade setbacks, balconies, parapets |
| 500 Hz | 0.69 m | 0.086 m | balconies, window bays, cars |
| 1 kHz | 0.34 m | 0.043 m | window recesses, cornices, tree trunks |
| 2 kHz | 0.17 m | 0.021 m | mullions, gutters, branches |
| 4 kHz | 8.6 cm | 1.1 cm | brick relief, foliage, sensor mounts |
| 8 kHz | 4.3 cm | 5 mm | leaves, small clutter, mic housing |
| 16 kHz | 2.1 cm | 2.7 mm | microphone mesh, windscreens |

Three regimes follow from ka (k = 2π/λ, a = feature size):

* **ka ≪ 1 (feature ≲ λ/6):** the feature barely scatters; omit it or fold it into an
  effective surface property (impedance, small porosity).
* **ka ≈ 1 (λ/6 to ~2λ):** resonant/diffuse scattering. Geometric acoustics cannot model
  this geometrically; represent it statistically with a **scattering coefficient** s(f)
  (ISO 17497-1 [K]) on the parent surface, or use a wave solver.
* **ka ≫ 1 (feature ≳ 2-4λ):** geometric acoustics valid; feature must be **explicit
  geometry**, with its edges available for diffraction.

The same physical facade therefore needs **different representations per frequency
band**: at 100 Hz a whole terrace of houses is a flat box with one impedance; at 2 kHz
balconies are explicit and brick texture is a scattering coefficient. This is the main
LOD finding for acoustics: **LOD must be selectable by wavelength, not by viewing
distance**, and the generator must be able to emit the "acoustic proxy" at a requested
tolerance (λ/8 for flatness of specular surfaces is the Rayleigh-roughness rule of thumb,
roughness h < λ/(8 cos θ)).

## 3. Physical mechanisms and the inputs each needs

| Mechanism | Magnitude (order) | Environment-model inputs |
|---|---|---|
| Spherical spreading | −6 dB per distance doubling | none (positions) |
| Air absorption (ISO 9613-1) | at 20 °C/70 % RH: 0.3 dB/km @125 Hz, 2.8 @500 Hz, 5.0 @1 kHz, 9.0 @2 kHz, 22.9 @4 kHz, 76.6 @8 kHz [K, ISO 9613-2 Table 2] | temperature, humidity, pressure fields |
| Ground effect | interference dip of 10-20 dB at 200-800 Hz over soft ground; +3 to +6 dB over hard ground | ground impedance per patch (flow resistivity σ, layer depth), roughness, terrain elevation |
| Refraction | upwind/lapse: acoustic shadow zones, 10-30 dB extra loss beyond a few hundred m; downwind/inversion: enhanced levels | vertical profiles of wind and temperature; roughness length z0 and displacement height d per land cover |
| Turbulence | scatters sound into shadow zones (limits barrier/shadow loss to ~20-25 dB [K]); decorrelates signals across an array | turbulence strength (C_T², C_v²), outer scale; derived from met + surface roughness |
| Diffraction | 5-25 dB around a single edge, frequency dependent | explicit edges with wedge angle and adjacent-face impedance |
| Reflection | −0 to −3 dB per facade bounce (hard walls); comb filtering | surface normals, absorption α(f), impedance |
| Diffuse scattering | redistributes 10-80 % of reflected energy | scattering coefficient s(f) per surface |
| Transmission | thin barriers, fences, glass, foliage screens | transmission loss TL(f) per lamina surface |
| Vegetation | ISO 9613-2 foliage: 0.02-0.12 dB/m (63 Hz-8 kHz) beyond 20 m [K]; measured dense belts up to 15-20 dB [web] | stem density, trunk diameters, leaf area density, leaf size, canopy height, ground under canopy |
| Doppler | 20 m/s drone → ±5.8 % frequency shift | dynamic object trajectories (drones, vehicles) |
| Urban reverberation | street-canyon level along the street drops 20-30 dB/100 m; canyon adds up to ~8-14 dB vs free field for UAV-like sources [web] | facade geometry, opening/cross-street topology, absorption, scattering |

Notes:

* **Ground impedance.** Delany-Bazley (1970) and Miki (1990) one-parameter models give
  surface impedance from effective flow resistivity σ; Miki's variant stays physically
  valid (positive real part) at low f/σ [K, web]. Nord2000/Harmonoise ground classes
  (A-H) span σ ≈ 12.5 (snow, moss), 31.5 (forest floor), 80 (loose grass), 200 (pasture),
  500 (compacted field), 2000 (gravel road/parking), 20 000 (asphalt/concrete) up to
  200 000 kPa·s/m² (very hard) [K]. CNOSSOS-EU reduces this to a ground factor G ∈ [0,1]
  with eight categories ([web] Aarhus University CNOSSOS page). Attenborough warns that a
  single σ is unreliable for layered grounds such as forest floor and snow, which need a
  two-parameter or layered model ([web] IOA paper). **Requirement:** ground material is a
  layered attribute (σ, porosity/tortuosity or Miki parameters, layer thickness, backing),
  not a single scalar, and it must be paintable on terrain at ~1-5 m resolution.
* **Refraction.** Engineering models use an effective sound speed
  c_eff(z) = c(T(z)) + u(z)·cos(φ), with a log wind profile u(z) = (u*/κ)·ln((z−d)/z0).
  The aerodynamic roughness length z0 (≈0.01 m grass, 0.5-1 m suburbs, 1-2 m forest and
  city centres [K]) and displacement height d are **derivable from the environment model**
  (building heights and density, canopy heights), so the model should export them as
  per-cell aggregates.
* **Turbulence** degrades beamforming and TDOA at long baselines: coherence decreases with
  sensor separation and frequency, which pushes designs to several small-baseline
  sub-arrays ([web] Lincke et al. JASA 2023; Kozick/Bucknell chapter; Wilson ARL-TR-1286).
  The environment model does not contain turbulence, but it must supply surface roughness
  and heat-flux-relevant land cover so a micro-meteorology module can estimate it.

## 4. Engineering (standardised) outdoor models

These are fast, validated, and widely used for noise mapping. They model a 2-D vertical
cut between source and receiver, so their geometry inputs are simple, and they are useful
as **reference baselines** and for coarse site surveys:

* **ISO 9613-2** (1996; revised 2024 [K]): octave-band A_div + A_atm + A_gr + A_bar + A_misc
  (foliage, industrial sites, housing). Moderate downwind only. Inputs: source/receiver
  positions, ground factor G along the path, barrier edges (top and lateral), foliage path
  length.
* **CNOSSOS-EU** (EU Directive 2015/996, based on NMPB-2008): ground classes, favourable
  and homogeneous meteorological conditions, lateral diffraction, reflections from
  vertical facades. Mandatory for EU strategic noise maps ([web] sources disagree on the
  start date, 2018 vs 2022). Open-source GIS implementation: NoiseModelling (Université
  Gustave Eiffel) [K].
* **Nord2000** (Nordic): ray-based in a vertical plane with curved rays, Fresnel-zone
  weighting of ground segments, terrain profiles, diffraction over multiple wedges,
  1/3-octave bands, ~8 meteorological classes ([web] SP report 2014:33).
* **Harmonoise** (EU FP5): reference model is PE/BEM-calibrated; engineering model uses
  ~30 meteorological classes and seven impedance classes; more elaborate than NMPB
  ([web] DGMR Harmonoise paper).

What they ask from geometry: a **terrain profile + ground-type segments + barrier/building
tops as diffraction wedges + vertical reflecting facades**. That is a 2.5-D "noise mapping"
export (building footprints with heights, terrain, ground polygons) — CityGML LoD1/LoD2 is
the usual input. UberTopo must be able to export this cheaply because site surveys over
km² will use these models first.

## 5. Numerical methods

### 5.1 Geometric acoustics (GA)

* **Image-source method (ISM):** exact specular paths for planar polygons; cost grows as
  N^order; with visibility checks it is the accurate way to get early reflections.
  Needs planar faces with **consistent normals and exact polygons with holes** (windows
  are different materials). Facade surfaces must not be over-tessellated, because ISM
  cost scales with the number of planar reflectors: coplanar triangles must be merged
  into one reflector, so the B-rep face (not the triangle) is the useful unit.
* **Ray tracing / stochastic ray (particle) tracing:** energy-based, handles diffuse
  reflection via scattering coefficients; cheap per ray, needs many rays (10⁴-10⁶ per
  source). Needs a BVH over triangles with per-face material ID, robust to small gaps.
* **Beam / cone tracing:** beams from polygon apertures (Funkhouser et al. beam tracing);
  cone tracing in CATT-Acoustic. Needs polygonal planar faces and adjacency to split beams
  at edges; beams also enumerate diffraction edges (Tsingos et al. 2001).
* **Path tracing / bidirectional / radiosity (diffuse):** Kang's acoustic radiosity for
  streets with diffusely reflecting facades; hybrid image-source + radiosity validated on
  five real streets ([web] Kang 2002; Yu, Ma & Kang 2019). Needs patch subdivision of
  facades with per-patch scattering and form factors (visibility between patches).
* **Gaussian beam and curved-ray tracing:** used for drone/UAM noise in cities because it
  handles refraction and avoids caustics; validated against FEM at high frequency
  ([web] TU Delft thesis; Forum Acusticum 2023 papers). Needs smooth sound-speed fields.
* **Validity limit:** GA fails below a few tens of Hz in urban geometry and wherever
  features are comparable to λ ([web] FA2023 paper). In practice GA is acceptable for
  drones above ~200-300 Hz in streets, with diffraction added.

### 5.2 Edge diffraction

* **UTD (Kouyoumjian & Pathak 1974)** [K]: high-frequency asymptotic diffraction
  coefficient for an infinite wedge, applied by Tsingos, Funkhouser, Ngan & Carlbom
  (SIGGRAPH 2001) to real-time auralization with beam tracing, treating edges as secondary
  sources ([web]). Inputs per edge: the two face planes (wedge exterior angle), edge
  direction and endpoints, and face impedances. Frequency domain, cheap.
* **BTM (Biot-Tolstoy-Medwin)**: exact time-domain solution for a rigid infinite wedge,
  re-derived by Svensson et al. (1999) as a line integral over finite edges, so it works
  for finite edges and low frequencies where UTD fails ([web]). Svensson's EDtoolbox
  (MATLAB, GPL) computes specular + higher-order diffraction, but only for rigid or
  pressure-release surfaces ([web] NTNU page); Calamia's thesis covers fast
  edge-subdivision ([web]).
* **Requirements derived:** (1) an **edge list distinct from tessellation edges** — only
  true geometric creases are diffracting; edges between coplanar or nearly coplanar
  triangles of one B-rep face must be excluded or the cost explodes and artefacts appear.
  (2) Each edge needs **wedge (exterior) angle, length, the two adjacent faces and their
  materials**, and whether it is open (thin-plate edge, e.g. a wall top or roof overhang:
  exterior angle 2π) or a building corner. (3) Edges should be **merged across
  tessellation** into long straight segments; curved edges (domes, tanks, cylindrical
  towers) need a polyline at λ/8 tolerance of the highest band. (4) Edge
  visibility/adjacency queries ("which edges are visible from point P", "edges whose
  diffraction cone hits region R") must be fast. A B-rep with explicit edges and
  face-pair adjacency gives (1)-(3) for free; a triangle soup does not.

### 5.3 Wave-based methods

| Method | Notes | Geometry requirement |
|---|---|---|
| FDTD | simple, time-domain, GPU friendly; grid Δx ≤ λ_min/6-10 | voxelized solid/air classification; impedance boundary per boundary cell; watertight solids |
| PSTD / openPSTD | 2 points per λ; good for outdoor/atmospheric fields; rectangular subdomains | same, plus smooth media |
| ARD (Raghuvanshi, Narain, Lin 2009) | analytic solution in rectangular partitions; ≥100× faster than FDTD, 10× less memory; kHz range in large scenes ([web]); assumes homogeneous medium | voxelization + rectangular decomposition of air volume |
| TLM | equivalent to FDTD; used for outdoor ground/terrain | voxels |
| BEM | surface mesh only, but dense matrices; elements ≤ λ/6 | closed (watertight) surface mesh, consistent normals |
| DG / FEM (e.g. Treble's wave solver [K]) | unstructured meshes, curved boundaries | conforming tetrahedral volume mesh → needs watertight, manifold boundary and clean small features |
| Parabolic equation (CN-PE, GFPE) | one-way marching, 2-D vertical planes or N×2-D; best for refraction and turbulence; GFPE (Gilbert & Di 1993) handles range-dependent impedance ([web]) | terrain profile + impedance along path; poor for building backscatter, remedied by GFPE-BEM hybrids ([web] EPFL ATMOS) |

Cost reality check (FDTD at 6 cells per λ, 1 km × 1 km × 100 m air volume):
at 250 Hz Δx ≈ 0.23 m → ~8×10⁹ cells; at 1 kHz Δx ≈ 0.057 m → ~5×10¹¹ cells. Wave
methods are therefore practical only for **low frequencies (≲ 300-500 Hz) over km
domains, or for small regions (a sensor site, a courtyard) at higher frequency**.
Microsoft Project Acoustics (Triton) uses this regime: a wave bake at a few hundred Hz
on a voxel grid (~0.25 m voxels at 500 Hz per the docs [web]) directly on visual geometry,
with no simplified acoustic mesh ([web]). Its bake is static, so moving geometry is not
supported ([web]).

**Requirement:** robust **voxelization / inside-outside classification** of the
environment at a requested resolution, including thin (lamina) surfaces that must still
block (a 2 cm fence panel in a 5 cm grid). This needs either watertight solids or a
robust generalized-winding-number / "thickened lamina" rule, and per-voxel boundary
material.

### 5.4 Hybrid methods (what a real chain will use)

The pragmatic chain for drone localization studies is:

1. **Low band (≲ 250-500 Hz):** wave solver (FDTD/PSTD/ARD) on a coarse proxy, or PE
   for long-range refraction over terrain.
2. **Mid/high band:** GA with ISM for early specular paths, ray/path tracing with
   scattering for the diffuse tail, UTD/BTM for edges, vegetation as participating
   medium.
3. **Atmosphere:** curved rays or Gaussian beams in c_eff fields; turbulence via
   coherence-loss models or random-phase screens.
4. **Moving sources:** time-varying path sets evaluated in retarded time (propagation
   delay at 500 m is 1.46 s, so the emission position differs from the current one by
   up to ~30 m at 20 m/s), Doppler per path, interpolated delay lines.

Crossover between (1) and (2) means the environment model must deliver **two
consistent representations of the same scene**: a coarse watertight volume proxy and a
detailed polygonal surface model, both carrying the same material IDs.

## 6. Vegetation

Vegetation affects sound by (a) **scattering from trunks and branches** (mid-high
frequencies, depends on stem density and diameter), (b) **absorption/viscous losses in
foliage** (high frequencies, depends on leaf area density and leaf size), (c) a
**soft, porous forest floor** (strong ground effect, often the largest contributor), and
(d) **modified micro-meteorology** under canopy (weaker gradients). ISO 9613-2 only uses
path length through foliage; it underestimates attenuation at long distances and high
frequencies ([web] summaries). Measured dense roadside belts gave ~15 dB at 200-500 Hz
and 15-20 dB at 8-12.5 kHz ([web]). Forests also add a long diffuse reverberant tail
(multiple scattering by trunks), which confuses TDOA.

**Requirement:** vegetation needs **two representations**: (1) explicit instances
(trunk cylinders with diameter, crown envelope, species) near sensors and for EO and
flight collision; (2) a **participating-medium volume** (stem density n per m², mean
trunk diameter, leaf area density LAD(z) profile, leaf size, canopy height, gap fraction)
for statistical attenuation and scattering elsewhere. Both must be generated
consistently from one procedural forest description, with seasonal state (leaf-on/off)
and wind sway as parameters.

## 7. What the environment model must provide for acoustics

1. **Surfaces** as planar polygons with holes (windows, doors, vents as separate
   material regions), with outward normals and two-sidedness flags for laminas (walls,
   fences, awnings, billboards).
2. **Explicit diffracting edges** with wedge angle, adjacent faces and materials,
   independent of tessellation.
3. **Per-surface acoustic materials** in octave or 1/3-octave bands (63 Hz-8/16 kHz):
   absorption α(f), scattering s(f), transmission loss TL(f); or a physical model
   (impedance Z(f) via σ and layer depth for porous grounds; mass law for panels).
   Angle-dependent reflection for grounds (complex reflection coefficient from Z).
4. **Wavelength-driven LOD**: a query "give me the scene simplified to tolerance ε with
   features smaller than ε converted to scattering" for ε = λ/8 per band; and a
   **receiver-centric** detail policy (full detail within tens of metres of each sensor,
   coarser elsewhere).
5. **Watertight volume proxy** and robust voxelization for wave solvers.
6. **Terrain + ground-cover** with layered impedance, roughness length z0 and
   displacement height aggregates.
7. **Vegetation** as instances and as volumes with statistical parameters.
8. **Atmosphere fields** (T, RH, wind vector, turbulence parameters) as volumetric grids
   registered to the same frame, possibly time-varying; at minimum vertical profiles.
9. **Ambient noise sources** derived from the model: roads with traffic attributes
   (lanes, speed, surface type for CNOSSOS road emission), rail, HVAC units on roofs,
   industrial sites, water (fountains, weirs), wind-in-foliage. Ambient noise sets the
   detection threshold as much as propagation does.
10. **Dynamic objects** (vehicles, people, drones, foliage motion) with trajectories, so
    path sets can be updated per time step.
11. **Queries:** ray/segment intersection with material lookup, visibility between
    points, image-source validation (point-in-polygon on a face plane), edge visibility,
    voxelization, and closest-surface queries near sensors.

## References

* [web] Tsingos, Funkhouser, Ngan, Carlbom, "Modeling Acoustics in Virtual Environments
  Using the Uniform Theory of Diffraction", SIGGRAPH 2001.
  https://pixl.cs.princeton.edu/pubs/Tsingos_2001_MAI/index.php
* [K] Kouyoumjian & Pathak, "A uniform geometrical theory of diffraction for an edge in a
  perfectly conducting surface", Proc. IEEE 62(11), 1974. https://doi.org/10.1109/PROC.1974.9651
* [K] Svensson, Fred, Vanderkooy, "An analytic secondary source model of edge diffraction
  impulse responses", JASA 106(5), 1999. https://doi.org/10.1121/1.428071
* [web] Svensson, edge diffraction toolbox (EDtoolbox / EDB2). https://ulfps.folk.ntnu.no/software/index.html
* [web] Svensson & Calamia, edge-subdivision for time-domain diffraction, EURASIP JASP 2007.
  https://asp-eurasipjournals.springeropen.com/counter/pdf/10.1155/2007/63560
* [web] Calamia, PhD thesis, Princeton 2009. https://gfx.cs.princeton.edu/pubs/_2009_AIE/calamia-phd-thesis.pdf
* [web] Biot-Tolstoy-Medwin diffraction model (overview). https://en.wikipedia.org/wiki/Biot%E2%80%93Tolstoy%E2%80%93Medwin_diffraction_model
* [web] Raghuvanshi, Narain, Lin, "Efficient and Accurate Sound Propagation Using Adaptive
  Rectangular Decomposition", IEEE TVCG 2009. https://www.microsoft.com/en-us/research/?p=309119
* [web] Mehra et al., GPU time-domain wave solver. https://www.microsoft.com/en-us/research/publication/efficient-gpu-based-time-domain-solver-acoustic-wave-equation/
* [web] MPARD (distributed ARD). https://gamma.cs.unc.edu/MPARD/preprint.pdf
* [web] Microsoft Project Triton / Project Acoustics. https://www.microsoft.com/en-us/research/project/project-triton/ ;
  probe/voxel docs https://learn.microsoft.com/en-us/gaming/acoustics/unreal-baking-probes
* [web] CNOSSOS-EU overview (Aarhus University). https://envs.au.dk/en/research-areas/environmental-noise/cnossos-eu
* [web] Harmonoise, the new European harmonised prediction method (DGMR).
  https://dgmr.nl/wp-content/uploads/files/Harmonoise%20-%20The%20new%20European%20harmonised%20prediction%20method.pdf
* [web] SP Report 2014:33 (Nord2000 / CNOSSOS comparison, Trafikverket).
  https://bransch.trafikverket.se/contentassets/4b21db8abbe14998a78b6edfe399a3cc/sp_report_2014_33.pdf
* [K] ISO 9613-1:1993 (atmospheric absorption) https://www.iso.org/standard/17426.html ;
  ISO 9613-2 (general method) https://www.iso.org/standard/20649.html
* [K] Delany & Bazley, "Acoustical properties of fibrous absorbent materials", Applied
  Acoustics 3(2), 1970. https://doi.org/10.1016/0003-682X(70)90031-9
* [web] Miki, "Acoustical properties of porous materials - modifications of Delany-Bazley
  models", J. Acoust. Soc. Jpn (E) 11(1), 1990. https://www.jstage.jst.go.jp/article/ast1980/11/1/11_1_25/_article
* [web] Attenborough, "Ground effect in outdoor noise propagation" (IOA).
  https://www.ioa.org.uk/system/files/publications/K%20ATTENBOROUGH%20GROUND%20EFFECT%20IN%20OUTDOOR%20NOISE%20PROPAGATION.pdf
* [K] Embleton, "Tutorial on sound propagation outdoors", JASA 100(1), 1996. https://doi.org/10.1121/1.415879
* [K] Salomons, *Computational Atmospheric Acoustics*, Springer 2001. https://doi.org/10.1007/978-94-010-0660-6
* [K] Gilbert & Di, "A fast Green's function method for one-way sound propagation in the
  atmosphere", JASA 94(4), 1993. https://doi.org/10.1121/1.407454
* [web] Aballéa / EPFL, PE over irregular terrain with meteorological effects.
  https://infoscience.epfl.ch/record/120513
* [web] Van Renterghem et al., ridge wind turbine propagation (GFPE + conformal mapping).
  https://pubmed.ncbi.nlm.nih.gov/28265027/
* [web] Lincke, Schumacher, Pieren, turbulence coherence loss in virtual arrays, JASA 2023.
  https://elib.dlr.de/194540/
* [web] Wilson, ARL-TR-1286, turbulence effects on acoustic arrays. https://apps.dtic.mil/sti/pdfs/ADA327901.pdf
* [web] Kozick et al., signal processing and propagation for aeroacoustic sensor networks.
  https://www.eg.bucknell.edu/~kozick/acoustic/crc03/proof/4353-Chapter-13-REVISED.pdf
* [web] Kang, sound fields in urban streets with diffusely reflecting boundaries (IOA).
  https://www.ioa.org.uk/system/files/proceedings/j_kang_sound_fields_in_urban_streets_with_diffusely_reflecting_boundaries.pdf
* [web] Yu, Ma, Kang, hybrid radiosity/image-source street-canyon model, Applied Sciences 2019.
  https://doaj.org/article/cb50b8de5f844096a0e248ab4246a5dc
* [web] Forum Acusticum 2023: ray-based UAV propagation in street canyons.
  https://dael.euracoustics.org/confs/fa2023/data/articles/000017.pdf ;
  https://dael.euracoustics.org/confs/fa2023/data/articles/000104.pdf
* [web] TU Delft thesis, sound propagation in complex environments for UAM.
  https://repository.tudelft.nl/record/uuid:72d10b7a-6790-41fc-9b15-26f9cccdb77f
* [web] UAM/drone noise with Gaussian beam tracing (PolyU). https://research.polyu.edu.hk/en/publications/assessment-of-uam-and-drone-noise-impact-on-the-environment-based/
* [web] Intaratep et al., "Experimental study of quadcopter acoustics and performance at
  static thrust conditions", AIAA 2016-2873. https://www.bu.edu/ufmal/files/2016/07/aiaa-2016-2873.pdf
* [web] Vegetation attenuation: Martens, sound propagation in forests (IOA)
  https://www.ioa.org.uk/system/files/publications/MJM%20MARTENS%20SOUND%20PROPAGATION%20IN%20FORESTS.pdf ;
  Arup Strutt foliage attenuation https://strutt.arup.com/help/Environmental_Noise/EnviroFoliageAtten.htm ;
  LAUB report https://projekte.ffg.at/anhang/5fd0a43a4199f_20201118_LAUB_v1.2.pdf
* [K] ISO 17497-1, measurement of random-incidence scattering coefficient. https://www.iso.org/standard/31397.html
