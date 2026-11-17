# Simulation Requirements Research

What the downstream simulations (acoustic, EO/IR, RF, flight and survey analytics)
need from the UberTopo environment model, so that the modeling system's geometry,
attribute and level-of-detail (LOD) design can be derived from the use cases in
[`docs/requirements.md`](../../requirements.md): detecting and locating drone swarms with
sensor networks, and surveying sites for vulnerability to drones (defensive counter-UAS).

Other research folders cover the modeling system's internals; this folder covers only
the consumers.

## Files

| File | Contents |
|---|---|
| [outdoor-acoustics.md](outdoor-acoustics.md) | Drone-noise wavelengths vs geometric detail; propagation mechanisms (ground impedance, air absorption, refraction, turbulence, diffraction, vegetation, urban reverberation, Doppler); ISO 9613-2, CNOSSOS-EU, Nord2000, Harmonoise; GA/ISM/beam/path tracing, UTD and BTM diffraction, FDTD/PSTD/ARD/BEM/PE, hybrids |
| [acoustic-drone-localization.md](acoustic-drone-localization.md) | Drone source characteristics, arrays, TDOA/DOA/beamforming, multipath effects and echo-aware localization, swarm issues, datasets, published systems, end-to-end simulation chain |
| [eo-ir-camera-simulation.md](eo-ir-camera-simulation.md) | Pixel footprint-driven LOD, thin structures, PBR vs spectral/BRDF materials, thermal properties and solvers (DIRSIG, MuSES), atmosphere, synthetic data and sim-to-real, labels |
| [rf-propagation.md](rf-propagation.md) | Radar clutter/multipath/micro-Doppler, passive RF detection, Sionna RT and ITU-R P.2040 materials, GNSS multipath and shadow matching |
| [vulnerability-surveys.md](vulnerability-surveys.md) | 3-D visibility, launch sites, rooftop access, geofences, adversarial approach corridors, sensor placement, CFD-lite wind, swarm flight needs (ESDF, collision, GNSS) |
| [simulators-and-engines.md](simulators-and-engines.md) | Acoustic engines (Steam Audio, Project Acoustics, GSound, I-Simpa, NoiseModelling, Treble, ...), drone/robot simulators (AirSim lineage, Isaac Sim, Gazebo, ...), physics-based EO/IR/RF tools, interchange formats |
| [validation-and-variation.md](validation-and-variation.md) | V&V ladder, test-site digital twin, LOD sensitivity studies, uncertainty quantification, domain randomization and adversarial scenario search via procedural generation |
| [requirements-matrix.md](requirements-matrix.md) | **The consolidated matrix**: geometry, attributes, LOD/accuracy/scale, queries/performance, dynamics, ground truth, prioritised requirement list, open questions |

## Key findings

1. **LOD must be tolerance-based and selected per query, not a global visual level.**
   Acoustics needs detail relative to wavelength (λ/8 flatness: ~43 cm at 100 Hz, ~4 cm
   at 1 kHz, ~5 mm at 8 kHz); cameras need detail relative to pixel footprint (from mm
   for zoom cameras to metres for wide thermal cameras at range); flight needs
   conservative clearance. Many sensors query the same scene at different LODs at once.
2. **Acoustic and RF wavelengths overlap** (drone sound ~3.4 m-2 cm; drone links and
   radar ~70 cm-2 cm). One λ-relative simplification and edge-preservation system can
   serve both.
3. **Sub-tolerance detail must be converted, not dropped**: into scattering
   coefficients, roughness, porosity, normal maps or clutter statistics.
4. **Sensor-centric detail** dominates accuracy: surfaces within metres of microphones
   and antennas cause the strongest early reflections and DOA bias, so geometry there
   needs cm-level detail.
5. **Explicit diffracting edges** (wedge angle, adjacent materials, separate from
   tessellation edges) are required by acoustic and RF diffraction (UTD, BTM). A B-rep
   provides them; a triangle soup does not.
6. **Non-manifold mixtures of solids, laminas (fences, thin walls) and wires**, faces
   with holes, and **volumes with per-face-side media** (air domains, vegetation media,
   water, geofences) are needed. Whether 3-manifold cells are required is a kernel
   decision; the simulation requirement is enumerable closed regions with shared faces
   carrying per-side materials.
7. **Vegetation needs two consistent representations**: explicit instances (trunks,
   crowns) and participating-medium volumes (stem density, leaf area density, leaf
   size), driven by one procedural description with seasonal state.
8. **One multi-modal material library** with layered assemblies: acoustic band
   coefficients and impedance models, PBR, spectral/thermal properties, RF
   permittivity/conductivity, with uncertainty distributions and provenance.
9. **Real-time acoustic engines lack outdoor physics** (refraction, turbulence, ground
   impedance), and noise-mapping tools output only levels. A C-UAS acoustic simulator
   will be a custom hybrid; UberTopo must export a native acoustic scene format.
10. **The environment model is also a run-time input**: map-aware (echo-aware)
    acoustic localization and GNSS shadow matching use the site model in the deployed
    system, which needs compact "reflector map" exports.
11. **Procedural generation's main value** is structured scenario variation (domain
    randomization, coverage of the operating envelope, adversarial worst-case search)
    and perturbing surveyed digital twins. Generators must expose seeded, hierarchical,
    bounded parameters and regenerate locally and deterministically with stable IDs.
12. **Validation needs a surveyed test site** with meteorological instrumentation;
    meteorology dominates outdoor acoustic variance beyond ~100 m. LOD defaults should
    come from sensitivity studies on that site.

## Method and source caveats

* Web search worked during this research; direct page fetching was blocked by the
  session network policy (e.g. valvesoftware.github.io, learn.microsoft.com). Facts
  were therefore checked from search-result summaries. Each reference is tagged
  **[web]** (seen in search results) or **[K]** (from background knowledge, not
  re-verified). Numerical values tagged [K], e.g. ISO 9613 absorption tables, ground
  flow-resistivity classes and vegetation coefficients, should be checked against the
  standards before use in code.
* Product status (Project AirSim, VRWorks Audio, Project Acoustics, Isaac Sim/Cesium
  integration) changes quickly; verify before relying on any tool.
