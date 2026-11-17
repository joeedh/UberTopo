# Simulators, Engines and Interchange Formats

Part of [simulation-requirements](README.md). A survey of the downstream tools that would
consume UberTopo environments, what each expects as input, and what that implies for
export formats. Tools are grouped by modality.

Source tags: **[web]** verified via web search in this research; **[K]** background
knowledge, not re-verified online. Product status changes quickly; check before
committing.

---

## 1. Acoustic engines

| Engine | Method | Outdoor suitability | Geometry/material input | Notes |
|---|---|---|---|---|
| **Steam Audio** (Valve) | GPU/CPU ray tracing, optional baking; reflections, pathing, occlusion/transmission | moderate (no refraction, no ground impedance model) | triangle meshes; 3-band materials: absorption, scattering, transmission for < 800 Hz, 800 Hz-8 kHz, > 8 kHz ([web] Unity material docs) | open source (Apache-2.0) [K]; 3 bands too coarse for drone harmonics analysis |
| **Microsoft Project Acoustics (Triton)** | precomputed wave simulation (ARD lineage) on voxelized visual geometry, parametric encoding | static scenes only (bake) | visual meshes + material absorption; voxel ~0.25 m at 500 Hz; adaptive probe layout ([web]) | wave effects (diffraction, portaling) without manual markup; bake on Azure; no runtime geometry change ([web]) |
| **NVIDIA VRWorks Audio** | OptiX ray tracing | — | meshes + materials | discontinued/legacy [K] |
| **Meta XR Audio SDK** | Shoebox or acoustic ray tracing | indoor-oriented | per-frequency absorption, transmission, scattering curves; energy conservation enforced; maps engine physics materials ([web]) | game/VR |
| **GSound / pygsound** (UNC/UMD) | ray tracing with path caching; specular + diffuse | indoor; extensible | triangle meshes + absorption/scattering | C++ source available; Python wrapper ([web]); GSound-SIR exposes per-ray data, Apache-2.0 ([web]) |
| **SoundSpaces** (Meta AI/Habitat) | bidirectional path tracing RIRs | indoor scans | Matterport/Replica meshes with semantic materials | used for embodied-AI datasets [K] |
| **pyroomacoustics** | ISM (shoebox/polyhedra) + ray tracing | rooms only | convex/polyhedral rooms, octave-band absorption/scattering | open source, convenient for array DSP prototyping [K] |
| **Odeon** | hybrid ISM + ray tracing | rooms/halls, some outdoor | polygon models, octave-band α and s | commercial [K] |
| **CATT-Acoustic (TUCT)** | randomized cone tracing | rooms | planar polygons, α/s per band | commercial [K] |
| **I-Simpa** (Univ. Gustave Eiffel) | SPPS particle tracing, TCR, diffusion model | **urban/outdoor capable**, includes atmospheric absorption, ground | closed meshes, material database | open source GUI/solvers [K] |
| **NoiseModelling** (Univ. Gustave Eiffel) | CNOSSOS-EU on GIS data | **outdoor km-scale noise maps** | building footprints + heights, ground polygons, DEM, road traffic | open source, PostGIS/H2GIS [K] |
| **Treble** | hybrid: wave-based (DG) low band + GA high band | rooms; outdoor limited | watertight meshes for wave part; materials with impedance | commercial cloud [K] |
| **Raven / Virtual Acoustics** (RWTH ITA) | hybrid ISM + ray tracing, real-time auralization | rooms, some urban research | polygon models | research [K] |
| **openPSTD** (TU Eindhoven) | PSTD wave solver | outdoor 2-D/3-D research | rectangular subdomains | open source [K] |
| **EDtoolbox** (Svensson) | ISM + BTM edge diffraction | exact low-order diffraction | rigid/pressure-release polyhedra only | MATLAB, GPL ([web]) |
| **SoundPLAN, CadnaA, Predictor** | ISO 9613-2, CNOSSOS, Nord2000, Harmonoise | outdoor noise mapping | 2.5-D GIS: footprints, heights, terrain, ground types | commercial standard tools [K] |

Note: "Eigen" (in the task brief) is a C++ linear algebra library, not an acoustic
engine; it would be a dependency of a solver rather than a propagation tool.

**Observations.**

* None of the real-time engines model **atmospheric refraction, turbulence or physically
  based ground impedance** — the factors that dominate outdoor drone detection at
  100-500 m. Noise-mapping tools model them but output levels (energy), not
  phase-coherent multichannel signals. A C-UAS acoustic simulator will likely be a
  **custom hybrid**: GPU GA (on top of OptiX/Embree/Vulkan RT) + BTM/UTD diffraction +
  low-band wave solver + atmospheric module, consuming UberTopo exports.
* Material models in game engines use **3 bands** (Steam Audio) or free curves (Meta).
  The environment model should store **at least octave bands 63 Hz-8 kHz** (preferably
  1/3-octave, 50 Hz-16 kHz) and physical parameters (σ, layer depth) from which band
  values are derived, then down-sample on export.

## 2. Vehicle/drone and robotics simulators

| Simulator | Base | Status / notes | Scene input |
|---|---|---|---|
| **AirSim** (Microsoft) | Unreal | archived (2022) ([web]) | Unreal levels |
| **Project AirSim** | Unreal | Microsoft shut it down (end 2023); "never released" per maintainers ([web]); check current status of any third-party continuation | — |
| **Colosseum** | Unreal 5 | community successor of AirSim, MIT; compatibility issues reported ([web]) | Unreal levels |
| **Cosys-AirSim** | Unreal 5 | fork with GPU lidar, echo (sonar-like) sensors ([web]) | Unreal levels |
| **NVIDIA Isaac Sim / Omniverse** | OpenUSD, RTX | RTX lidar/radar/camera sensors; Pegasus Simulator for PX4 multirotors [K]; Cesium for Omniverse streams 3D Tiles but low-altitude tile fidelity is poor and FSD vs PhysX conflicts were reported ([web]) | USD stages |
| **Gazebo (Harmonic/Jetty; formerly Ignition)** | OGRE2 | PX4/ArduPilot SITL standard; CFD wind injection demonstrated ([web]) | SDF worlds + meshes (glTF/OBJ/DAE) |
| **CARLA** | Unreal | ground vehicles, OpenDRIVE roads, radar/lidar sensors [K] | OpenDRIVE + Unreal maps |
| **Flightmare** (UZH) | Unity + fast dynamics | many-agent quadrotor RL [K] | Unity scenes |
| **Unreal Engine + Cesium for Unreal** | Unreal | geospatial streaming of 3D Tiles [K] | 3D Tiles |
| **OneSAF** (US Army) | constructive sim | semi-automated forces; terrain databases | OneSAF terrain formats [K] |
| **JSIMS** | — | cancelled ~2002; historical [K] | — |
| **VBS4 / MÄK VR-Forces** | military virtual/constructive | commercial; terrain pipelines from GIS [K] | proprietary/CDB |

Physics-based signature tools:

| Tool | Modality | Input |
|---|---|---|
| **DIRSIG** (RIT) | spectral EO/IR, polarimetric, lidar, some RF | facetized geometry, material IDs with spectral BRDF/emissivity, texture-to-material maps ([web]) |
| **MuSES** (ThermoAnalytics) | thermal solver + EO/IR radiance | thermal mesh, layered materials, environment ([web]) |
| **CameoSim, OKTAL-SE SE-Workbench** | EO/IR/RF signatures | proprietary scene databases [K] |
| **Sionna RT** | radio propagation, differentiable | Mitsuba 3 XML + ITU materials ([web]) |
| **Wireless InSite, WinProp** | radio propagation | CAD/mesh + materials [K] |

**Observation (skepticism the requirements ask for):** much military terrain tooling
(OpenFlight, SEDRIS, OGC CDB, OneSAF terrain) dates from the 1990s-2000s and is built
around **visual polygon databases with tiles and switch-node LODs**, with material
codes for EO/IR added later. They lack wavelength-relative LOD, diffraction edges,
acoustic materials and procedural refinement. UberTopo should export to them only as a
compatibility target, not use them as its internal model.

## 3. Interchange formats

| Format | Use | Strength | Gap for UberTopo |
|---|---|---|---|
| **OpenUSD** | Omniverse/Isaac Sim, film/VFX | layered composition, variants (randomization!), instancing, custom schemas, semantics via `UsdSemantics`-style APIs [K] | acoustic/RF materials need custom schemas |
| **glTF 2.0** | real-time engines, Gazebo, Cesium | compact PBR, extensions, `EXT_mesh_features`/`EXT_structural_metadata` for per-feature attributes [K] | triangles only; no edges/B-rep |
| **OGC 3D Tiles** | geospatial streaming | hierarchical LOD with geometricError, geo-referencing, metadata [K] | LOD metric is visual only |
| **CityGML 3.0 / CityJSON** | semantic city models | semantic classes, LoD0-LoD3, building parts, openings | weak on vegetation structure, no acoustic/RF material schema (ADE extensions possible) |
| **ASAM OpenDRIVE / OpenSCENARIO** | road networks, traffic scenarios | road reference lines as lines, arcs, **clothoids (spirals)**, cubic polynomials, lanes [K] | aligns with project's transcendental-curve requirement; good import/export target |
| **Mitsuba 3 XML** | Sionna RT | named materials per shape | export target |
| **OGC CDB** | military sim databases | tiled, multi-LOD rasters + vectors + models | legacy, visual LOD |
| **GeoTIFF/COG, NetCDF/Zarr** | rasters & volumetric fields (DEM, wind, noise maps) | standard GIS/science tooling | export target for derived fields |
| **OpenVDB / NanoVDB** | sparse voxels (SDF, density, fields) | GPU-friendly sparse volumes | export target for ESDF, vegetation density, wind |

**Requirement summary:** UberTopo needs an internal model richer than any of these, plus
exporters: USD (with variants for randomization and custom acoustic/RF schemas), glTF +
3D Tiles (with feature metadata), CityGML/CityJSON (semantics), Mitsuba XML (RF),
OpenDRIVE (roads), VDB (volumes and SDFs), COG/Zarr (rasters and fields), and a native
**acoustic scene format** (faces + edges + band materials + volumes) since none exists as
a standard.

## References

* [web] Steam Audio Unity material documentation. https://valvesoftware.github.io/steam-audio/doc/unity/material.html
* [K] Steam Audio source. https://github.com/ValveSoftware/steam-audio
* [web] Project Triton. https://www.microsoft.com/en-us/research/project/project-triton/
* [web] Project Acoustics probe placement. https://learn.microsoft.com/en-us/gaming/acoustics/unreal-baking-probes
* [web] Project Acoustics 2.0 announcement. https://techcommunity.microsoft.com/blog/themixedrealityblog/project-acoustics-2-0-is-now-available/2412893
* [web] Meta XR acoustic ray tracing materials. https://developers.meta.com/horizon/documentation/unity/meta-xr-acoustic-ray-tracing-unity-material/
* [web] GSound. https://gamma.cs.unc.edu/GSOUND ; pygsound https://pypi.org/project/pygsound/0.3 ; GSound-SIR https://arxiv.org/abs/2503.17866
* [K] SoundSpaces. https://github.com/facebookresearch/sound-spaces
* [K] pyroomacoustics. https://github.com/LCAV/pyroomacoustics
* [K] I-Simpa. https://github.com/Universite-Gustave-Eiffel/I-Simpa
* [K] NoiseModelling. https://github.com/Universite-Gustave-Eiffel/NoiseModelling
* [K] Treble. https://www.treble.tech/
* [K] Odeon. https://odeon.dk/ ; CATT-Acoustic https://www.catt.se/
* [K] openPSTD. http://www.openpstd.org/
* [web] Svensson EDtoolbox. https://ulfps.folk.ntnu.no/software/index.html
* [web] Colosseum. https://github.com/CodexLabsLLC/Colosseum
* [web] AirSim discussion on Project AirSim release. https://www.github.com/microsoft/AirSim/discussions/4791
* [web] Cosys-AirSim changelog. https://data.safetycli.com/changelogs/cosysairsim
* [web] Cesium for Omniverse. https://cesium.com/platform/cesium-for-omniverse/ ; quickstart https://cesium.com/learn/omniverse/omniverse-quickstart/
* [web] Isaac Sim + Cesium drone project notes. https://bsh-portfolio.pages.dev/projects/starbelt-drone-simulation/
* [K] Pegasus Simulator. https://github.com/PegasusSimulator/PegasusSimulator
* [K] Gazebo. https://gazebosim.org/ ; CARLA https://carla.org/ ; Flightmare https://github.com/uzh-rpg/flightmare
* [web] DIRSIG overview. https://dirsig.cis.rit.edu/docs/reveal.js/overview.html
* [web] MuSES. https://www.thermoanalytics.com/application/eo-ir-scene-simulation-for-ai-applications/
* [web] Sionna RT docs. https://nvlabs.github.io/sionna/rt/api/radio_materials.html
* [K] OpenUSD. https://openusd.org/ ; glTF https://www.khronos.org/gltf/ ; 3D Tiles https://www.ogc.org/standard/3dtiles/ ;
  CityGML https://www.ogc.org/standard/citygml/ ; OpenDRIVE https://www.asam.net/standards/detail/opendrive/ ;
  OGC CDB https://www.ogc.org/standard/cdb/ ; OpenVDB https://www.openvdb.org/
