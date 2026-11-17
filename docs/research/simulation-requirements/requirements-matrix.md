# Derived Requirements Matrix

Part of [simulation-requirements](README.md). This file consolidates what each
downstream simulation needs from the UberTopo environment model. Evidence and sources
are in the topic files:
[outdoor-acoustics](outdoor-acoustics.md),
[acoustic-drone-localization](acoustic-drone-localization.md),
[eo-ir-camera-simulation](eo-ir-camera-simulation.md),
[rf-propagation](rf-propagation.md),
[vulnerability-surveys](vulnerability-surveys.md),
[simulators-and-engines](simulators-and-engines.md),
[validation-and-variation](validation-and-variation.md).

Numbers are engineering estimates for sizing, to be refined by the LOD sensitivity
studies described in [validation-and-variation.md](validation-and-variation.md).

---

## 1. Simulation types considered

| ID | Simulation | Purpose in the project |
|---|---|---|
| S1 | Acoustic geometric propagation (ISM, ray/path tracing, UTD/BTM), ~250 Hz-16 kHz | multichannel signals for drone detection/localization |
| S2 | Acoustic wave solver (FDTD/PSTD/ARD/BEM), ≲ 500 Hz or small regions | low-band accuracy, diffraction, reference solutions |
| S3 | Acoustic engineering models (ISO 9613-2, CNOSSOS-EU, Nord2000, PE) | km-scale surveys, refraction, coverage maps |
| S4 | Visible camera rendering (game engine, PBR) | detector training data, coverage |
| S5 | Physics-based EO/IR + thermal solver (DIRSIG/MuSES class) | thermal detection, radiometric studies |
| S6 | Radar (clutter, multipath, masking, micro-Doppler) | C-UAS radar performance |
| S7 | RF propagation (C2/video link detection, inter-drone comms) | passive RF sensing, swarm comms |
| S8 | GNSS multipath/NLOS/jamming | drone navigation degradation |
| S9 | Onboard lidar/depth for drones | drone autonomy realism |
| S10 | Flight dynamics, collision, path planning (swarm) | threat behaviour, approach corridors |
| S11 | CFD-lite wind (LBM) | flyability, refraction, mic wind noise |
| S12 | Vulnerability survey analytics (visibility volumes, corridors, launch sites, sensor placement) | site assessment |

## 2. Geometry representation

| | Surfaces | Edges | Volumes | Curves / points | Watertight? | Notes |
|---|---|---|---|---|---|---|
| S1 | planar polygons with holes, normals, two-sided laminas, merged coplanar faces | **explicit diffracting edges** with wedge angle + adjacent faces | vegetation media | sensor mounts, noise sources | not required; small gaps tolerated | B-rep face (not triangle) = reflector unit |
| S2 | boundary with impedance | implicit | **air domain**; solid inside/outside | sources/receivers | **yes** (or robust inside/outside) | voxelization at λ/6-λ/10 |
| S3 | 2.5-D: footprints + heights, terrain TIN/raster, ground polygons | barrier/roof tops as wedges | — | road lines for emission | no | CityGML LoD1-2 level |
| S4 | triangle meshes, UVs, normals/tangents | — | participating media (fog) | wires (as tubes or curve primitives) | no | instancing essential (vegetation, clutter) |
| S5 | facets + material maps; thermal mesh with layered constructions | — | — | — | for view factors/shadows: mostly | thermal node mesh ≠ render mesh |
| S6 | facets ≤ λ/8 where flat; roughness statistics; dihedral/trihedral corners | **edges** (UTD) | vegetation media | — | no | |
| S7 | as S6 at 0.4-6 GHz | **edges** | vegetation media | antenna positions | no | Mitsuba XML export |
| S8 | building envelopes LoD1-2, roof edges ~0.5-1 m | roof edges | — | — | no | sky-mask queries |
| S9 | cm-detail near drone paths, NIR reflectance | — | foliage as stochastic surfaces | wires | no | |
| S10 | collision proxy (outer-conservative) | — | **ESDF / occupancy**; geofence volumes | **wires, poles**; flight paths | outer proxy yes | 0.1-0.5 m voxels near obstacles |
| S11 | simplified envelopes 0.5-2 m | — | **solid volumes**, porous vegetation volumes | — | **yes**, no leaks | |
| S12 | inner-conservative occluders; full 3-D (overhangs, bridges) | — | airspace voxels, geofences, parcels | mount points, roads/paths | inner/outer proxies | |

**Consequences for the kernel (input to the B-rep design discussion):**

* **Faces with holes** are needed everywhere (windows, doors, vents, courtyards).
* **Lamina (zero-thickness, two-sided) faces** are needed for fences, thin walls,
  awnings, billboards, roof overhangs; at higher LODs the same object becomes a thick
  solid. So the model must support **non-manifold mixtures of solids, sheets and wires**.
* **Volumes as first-class entities**: air domains for wave solvers, vegetation media,
  water bodies, building interiors, geofences, parcels extruded to airspace. Each
  boundary face must know **what medium lies on each side** (air/solid/vegetation/
  water/interior). Whether this is implemented as a 3-manifold cell complex or a
  2-manifold B-rep with region labels is a kernel decision; the simulation requirement
  is: *closed regions can be enumerated, adjacent regions share boundary faces, and each
  face side carries its own material*.
* **Edges classified** as feature (diffracting crease), smooth (tangent-continuous), or
  tessellation-only, with wedge angle; long straight edges kept merged across
  tessellation.
* **Curves**: wires/cables (catenaries with radius), road centrelines (clothoids/
  spirals), flight paths; **points**: sensor mounts, noise sources, antennas, lights.
* **Volumetric fields** (wind, temperature, LAD, P_detect, ESDF) attached to geo-tiles
  on a shared grid, stored sparsely (VDB-like).

## 3. Attributes

| | Semantic | Acoustic | Optical / thermal | RF | Other |
|---|---|---|---|---|---|
| S1 | class + instance ID per face/edge | α(f), s(f), TL(f) in octave or 1/3-octave bands 50 Hz-16 kHz; ground: σ, layer depth (Delany-Bazley/Miki); angle-dependent reflection | — | — | noise-source emission (traffic, HVAC) |
| S2 | — | complex impedance Z(f) or σ-based model per boundary | — | — | medium per region |
| S3 | building height, ground class (G or σ class A-H) | barrier absorption | — | — | roughness length z0, displacement d; road traffic attributes |
| S4 | class, instance, material IDs | — | PBR (base colour, metal, roughness, normal, transmission, subsurface) | — | randomization parameters |
| S5 | class, instance | — | spectral reflectance / BRDF ID, emissivity, solar absorptivity; layered k, cp, ρ, thickness; internal heat sources | — | geo-location, date/time |
| S6 | class | — | — | ε′, σ (ITU-R P.2040 class), roughness, scattering pattern | foliage sway, water state |
| S7 | class | — | — | as S6 + layered walls, glazing type | antenna mounts |
| S8 | class | — | — | reflectivity class (glass/metal/concrete) | ECEF exactness |
| S9 | class | — | NIR (905/1550 nm) reflectance | — | — |
| S10 | class; landing/perch suitability | — | — | — | collision class (hard / soft canopy / wire), geofence limits with vertical datum |
| S11 | — | — | — | — | porosity/drag (LAD·Cd) for vegetation; z0 outside domain |
| S12 | parcels, land use, ownership/access, roof access, mount candidates | (via S1/S3) | (via S4/S5) | (via S6/S7) | concealment/cover |

**Single material record** (one library feeding all modalities):

```yaml
material: brick_wall_solid_240mm
semantic_class: building/facade/wall
provenance: {source: library/default, version: 3, confidence: medium}
acoustic:
  bands_hz: [63, 125, 250, 500, 1000, 2000, 4000, 8000]
  absorption: [0.02, 0.02, 0.03, 0.04, 0.05, 0.07, 0.07, 0.08]   # example values
  scattering: {from_geometry: true, residual: [0.05, 0.05, 0.06, 0.08, 0.1, 0.15, 0.2, 0.3]}
  transmission_loss_db: [40, 44, 48, 53, 58, 62, 65, 65]
  impedance_model: null            # used for porous grounds instead
optical:
  pbr: {base_color_tex: ..., roughness: 0.85, metallic: 0.0}
  spectral_brdf_id: brick_red_01
thermal:
  emissivity_lwir: 0.93
  solar_absorptivity: 0.65
  layers: [{k: 0.7, cp: 840, rho: 1800, thickness: 0.24}]
rf:
  itu_class: brick                 # ITU-R P.2040 frequency model
  roughness_rms_m: 0.005
uncertainty:
  absorption: {dist: lognormal, rel_sd: 0.3}
```

Layered constructions are shared across thermal, RF penetration and acoustic
transmission; the material system must therefore support **assemblies of layers**, not
only single homogeneous materials.

## 4. LOD, accuracy and scale

| | Detail rule | Near-sensor rule | Positional accuracy | Spatial extent |
|---|---|---|---|---|
| S1 | flatness tolerance ≈ λ/8 of highest band used per region; features λ/6-2λ → scattering coefficient; > 2-4λ explicit | ≤ 1-2 cm within 10-30 m of each mic node | node positions < 1 cm; reflectors ~λ/8 | ~1 km² per array cluster |
| S2 | voxel λ/6-λ/10 (≈ 0.1-0.2 m at 250-500 Hz) | — | voxel | 10⁴-10⁶ m² at low band |
| S3 | 1-5 m buildings and ground polygons; DEM 1-5 m | — | ~1 m | 1-100 km² |
| S4 | ≤ 0.5 pixel footprint per sensor (e.g. 1.4 cm at 100 m, 27 cm at 2 km for a 30° 4K camera) | narrow-FOV zoom regions refined on demand | — | horizon to 5-10 km |
| S5 | thermal nodes 0.1-1 m; render as S4 | — | — | 1-10 km² |
| S6 | λ/8 at radar band for large flat surfaces & corners; roughness stats otherwise | — | ~λ for clutter maps | 5-10 km radius |
| S7 | λ/8 at link band near receivers; coarse elsewhere | receiver mount cm-level | ~0.1-0.5 m | 1-5 km |
| S8 | building edges ~0.5-1 m | — | ~0.5 m, exact geodetic | urban districts |
| S9 | 1-5 cm near flight paths | — | cm | flight corridor |
| S10 | 0.1-0.5 m voxels near obstacles; **outer-conservative** | — | conservative | 1-20 km |
| S11 | 0.5-2 m; watertight | — | ~1 m | 1-4 km² resolved |
| S12 | multi-resolution; inner/outer conservative proxies | — | ~1 m | 1-100 km² |

**LOD policy implied:**

1. LOD is a **tolerance in metres + a list of preserved feature classes** (edges,
   openings, thin structures), not an integer level. Integer "LoD0-4" (CityGML) can be
   a label on top.
2. LOD is **selected per query**: by wavelength for S1/S2/S6/S7, by pixel footprint for
   S4/S5/S9, by clearance/conservativeness for S10/S12. Several sensors query the same
   scene at different LODs concurrently.
3. **Sensor-centric refinement** (around mics, antennas, PTZ frusta) matters more than
   viewer-centric refinement.
4. Sub-tolerance detail is **converted, not dropped**: into scattering coefficients,
   roughness, normal maps, porosity, or statistical clutter densities.
5. Proxies come in flavours with **guarantees**: inner-conservative (occluder),
   outer-conservative (collision), watertight (CFD/wave), edge-preserving (diffraction).
6. **Stable IDs** across LODs: a coarse face records which fine faces/objects it
   represents, so labels and ground truth stay consistent.

## 5. Queries and performance

| | Main queries | Throughput target (order of magnitude) |
|---|---|---|
| S1 | ray/segment intersection with face+material ID; point visibility; image-source validation (point-in-face-polygon); visible-edge enumeration; nearest surfaces to node | 10⁸-10⁹ ray segments/s (GPU), scene 10⁵-10⁷ triangles active |
| S2 | voxelization with per-cell boundary material; inside/outside | 10⁹-10¹⁰ cells per bake (offline) |
| S3 | vertical-profile extraction (terrain, ground segments, obstacles) along source-receiver lines | 10⁶-10⁸ profiles per map |
| S4/S5 | rasterization or ray tracing; streaming tiles; instancing | real-time to offline; 10⁹+ rays/s on RTX |
| S6/S7 | SBR ray tracing + edge enumeration | 10⁷-10⁹ rays per scene snapshot |
| S8 | sky-mask (satellite visibility) at points; single-bounce reflection points | 10³-10⁶ points |
| S10 | SDF value+gradient at points; segment collision; geofence containment | 10⁶-10⁸ queries/s (O(1) voxel lookup) |
| S11 | voxelization at 0.5-2 m, porous-cell flags | 10⁸-10⁹ cells per case |
| S12 | batch visibility point→volume; least-cost paths over voxel cost fields | 10⁹-10¹² rays per survey (hierarchical, GPU) |

Implications: the authoritative model need not itself be a ray tracer, but it must
**export GPU-ready acceleration inputs fast** (flattened triangle buffers with
face/material/edge IDs, instanced vegetation, sparse voxel volumes), and support
**incremental updates** for dynamic objects (BVH refit/rebuild of only changed
instances).

## 6. Dynamics and time

| Element | Needed by | Representation |
|---|---|---|
| Drones (swarm) | all | trajectories with attitude, rotor states; 100-1000 Hz physics |
| Vehicles, trains | S1 (noise + reflectors), S4, S6 (clutter) | instanced rigid bodies on road/rail network (OpenSCENARIO-like) |
| People/crowds | S4/S5 (false alarms, thermal), S1 (noise) | instanced agents |
| Birds | S4, S6 (false alarms) | agents with habitat attributes from environment |
| Foliage motion | S4 (flicker), S6 (Doppler clutter), S1 (rustle noise) | per-species sway parameters, wind-driven |
| Water surface | S4, S5, S6 | wave spectrum parameters per water body |
| Atmosphere | S1, S3, S5, S11 | time-varying fields / met classes |
| Sun, sky, thermal history | S5 | date/time, location; 24-72 h thermal spin-up |
| Season | all | leaf-on/off, snow cover (ground impedance changes drastically), soil moisture |
| Doors/windows open, construction | S1, S4, S10 | state attributes on openings; temporary structures |

## 7. Ground truth and outputs to support

* Persistent **instance IDs** and **semantic classes** for every face/object across LODs.
* Acoustic: per-path records (type, order, delay, direction at receiver, per-band
  amplitude, interacting face/edge IDs) and image-source positions.
* EO/IR: per-pixel class/instance/material, depth, normals, motion vectors; 2-D/3-D boxes,
  occlusion and truncation.
* RF/GNSS: path lists, LOS/NLOS flags per satellite, channel impulse responses.
* Survey: 3-D P_detect volumes, warning-time maps, corridors, placements, with
  uncertainty.
* Provenance and LOD/tolerance metadata on every export.

## 8. Consolidated requirement list (prioritised)

**Must (blocking for the primary use cases):**

1. R1 Non-manifold model mixing solids, laminas and wires; faces with holes.
2. R2 Explicit edge classification (diffracting crease vs smooth vs tessellation) with
   wedge angle and adjacent-face materials.
3. R3 Regions/volumes with per-face-side medium/material.
4. R4 Single multi-modal material library (acoustic bands, PBR, spectral/thermal, RF),
   with layered assemblies.
5. R5 Tolerance-driven, per-query LOD with conversion of sub-tolerance detail to
   statistical attributes; sensor-centric refinement.
6. R6 Conservative inner/outer and watertight proxies with guarantees.
7. R7 Stable persistent IDs + semantic classes across LOD and regeneration.
8. R8 Geodetic georeferencing; double-precision positions; local ENU frames; vertical
   datums.
9. R9 Vegetation dual representation: instances (trunks, crowns) + participating-medium
   volumes (stem density, diameters, LAD profile, leaf size), seasonal state.
10. R10 Terrain + ground cover with layered impedance and roughness aggregates.
11. R11 Fast exports: GPU triangle buffers with IDs, Mitsuba XML, USD/glTF/3D Tiles,
    VDB volumes, CNOSSOS-style 2.5-D GIS layers, native acoustic scene format.

**Should:**

12. R12 Attributes as distributions; seeded deterministic regeneration; provenance.
13. R13 Curve primitives for wires, road centrelines (clothoids), flight paths.
14. R14 Volumetric field storage on shared geo-tiles (wind, T, LAD, P_detect, ESDF).
15. R15 Noise-source and clutter attributes (roads/traffic, HVAC, birds' habitats).
16. R16 Dynamic objects with incremental acceleration-structure updates.
17. R17 Parcels, access, roof semantics, mount candidates, geofences.
18. R18 Exposed hierarchical generator parameters for randomization/adversarial search;
    USD variant export.

**Could:**

19. R19 Thermal-node mesh generation linked to render mesh.
20. R20 Inverse procedural fitting to surveyed sites.
21. R21 Built-in canonical verification scenes.

## 9. Open questions for the project owner

1. Which frequency range and fidelity is required for acoustics: phase-accurate
   multichannel signals (for localization algorithms) or band levels (for coverage
   maps)? Both imply different engines.
2. Which sensor modalities are in scope for the first iterations (acoustic first?
   EO/IR? radar? RF?), and which external simulators must be supported (Isaac Sim,
   Unreal/Colosseum, Gazebo, Sionna, DIRSIG)?
3. Target site sizes and number of sensors/drones per scenario; real-time vs offline.
4. Is a real test site available for the validation loop (survey, met instrumentation,
   controlled sources)?
5. Should the deployed C-UAS system consume UberTopo exports at run time (map-aware
   localization, shadow matching)? That adds format stability and footprint
   requirements.
6. Indoor spaces: do interiors matter (drones inside atria, stadium concourses,
   warehouses) or only exteriors?
7. Classification and export-control constraints on data, materials libraries and site
   models (affects provenance and storage design).
