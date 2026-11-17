# Validation, Uncertainty Quantification and Scenario Variation

Part of [simulation-requirements](README.md). How to establish that the
environment → sensor-simulation → algorithm chain predicts reality, how to carry
uncertainty through it, and how procedural generation provides scenario variation
(domain randomization, coverage of the operating envelope, adversarial search).

Source tags: **[web]** verified via web search in this research; **[K]** background
knowledge, not re-verified online.

---

## 1. Why this matters for the environment model

Simulation results will be used to (a) train detectors, (b) choose sensor sites and
(c) argue that a defended site is adequately covered. All three need evidence that the
simulated quantities match reality **within known error bars**. Many of those error bars
come from the environment model: geometry simplification (LOD), material parameters and
missing objects. UberTopo should therefore make its own approximations **explicit,
controllable and recordable** (provenance), so validation studies can attribute
discrepancies.

## 2. Verification and validation ladder

Following the usual V&V structure (verification = solving the equations right;
validation = solving the right equations; DoD VV&A practice adds accreditation for a
specific use) [K]:

### 2.1 Code verification (analytic and benchmark cases)

* **Acoustics:** free field; point source over an impedance plane (Weyl-van der Pol
  spherical-wave reflection coefficient); rigid infinite wedge (BTM exact vs UTD
  asymptotic); shoebox image sources; the outdoor benchmark cases of Attenborough et al.
  (JASA 1995) for ground effect and refraction across FFP/PE/ray codes [K]; ISO 9613-2
  and CNOSSOS-EU reference test cases [K].
* **Room-acoustics round robins** show GA codes disagree substantially, with diffraction
  and low frequencies as weak points (Brinkmann et al., JASA 2019, BRAS database) [K].
* **RF:** canonical wedge/plate UTD cases; ITU-R reference measurements; Sionna's
  differentiability allows fitting material parameters to measurements [web Sionna paper].
* **EO/IR:** radiometric closure tests (white furnace, known blackbody), MODTRAN
  comparisons; DIRSIG-style truth outputs [web DIRSIG].
* **Geometry pipeline:** tests that LOD proxies satisfy their contracts (λ/8 deviation
  bound, conservative inner/outer containment, watertightness, preserved edge sets,
  stable IDs).

### 2.2 Component validation (measured parameters)

* Ground impedance in situ (two-microphone level-difference method, ANSI S1.18 [K]) for
  each ground type at the test site; fit σ (and layer depth) to calibrate the material
  library.
* Facade/material absorption and scattering (impedance tube, ISO 17497 scattering
  coefficients) for key materials [K].
* Vegetation: stem counts, diameters, LAI (hemispherical photography), leaf size —
  measured on the plots used in tests.
* Thermal: surface temperatures over a diurnal cycle with a calibrated LWIR camera and
  contact sensors.
* RF: material permittivity checks at the relevant bands; drive/walk tests.

### 2.3 System validation at a test site (digital twin)

1. **Survey a real test site**: terrestrial and airborne lidar + photogrammetry + manual
   semantic annotation; build the UberTopo model of it at several LODs.
2. **Instrument the atmosphere**: sonic anemometers and temperature sensors at several
   heights, a wind/temperature profiler (SODAR/lidar or tethered balloon), humidity —
   logged during every trial. Without this, acoustic validation beyond ~100 m is not
   interpretable because meteorological variation dominates (often ±5-10 dB at a few
   hundred metres) [K].
3. **Controlled sources**: a calibrated loudspeaker lifted by crane or carried by a
   heavy drone emits sweeps/MLS to measure impulse responses at the sensor nodes;
   then real drones fly scripted trajectories with RTK-GNSS/mocap ground truth.
4. **Compare at several levels**:
   * per-band level vs distance and per-path arrival structure (delays, directions of
     early reflections and diffraction) — validates propagation and geometry;
   * GCC-PHAT functions, beamforming maps, DOA error distributions — validates the
     signal chain;
   * detection-range curves and localization error statistics of the actual algorithms,
     sim vs real — validates fitness for purpose;
   * EO: rendered vs calibrated images, detection AP on real vs synthetic (the
     "train-on-sim, test-on-real" gap is the operational metric; DLR and Cranfield
     report how this gap behaves for drone detectors [web]).
5. **LOD sensitivity study**: rerun the validated scenario while coarsening geometry and
   materials step by step (and removing object classes, e.g. street furniture, parked
   cars, balconies). The LOD at which each metric departs from the full-detail result by
   more than the measurement uncertainty gives **empirical LOD requirements per band and
   per metric**. This is how UberTopo should set its default tolerances (the λ/8 rule
   in [outdoor-acoustics.md](outdoor-acoustics.md) is a starting hypothesis to test).

## 3. Uncertainty quantification

| Source | Typical effect | How the environment model helps |
|---|---|---|
| Meteorology (wind, temperature profile, turbulence) | dominant beyond ~100 m for acoustics; shadow zones | supply roughness/canopy aggregates; host field ensembles |
| Ground impedance | ±several dB around the ground dip | σ as distribution per ground class |
| Surface materials (α, s, ε, emissivity) | reflections ±1-3 dB; RF ±several dB; thermal contrast | attributes as distributions with provenance |
| Geometry error / LOD | timing errors (1 cm ≈ 29 µs acoustics), missing reflectors | record LOD and tolerance per export; generate at several LODs |
| Missing objects (cars, people, street furniture, scaffolding) | extra reflectors/occluders | procedural clutter with density parameters |
| Source model (drone noise, RCS, thermal) | ±3-6 dB source level | out of scope, but scenario metadata |
| Sensor calibration and position | bias in DOA/TDOA | mount geometry with tolerances |

Methods: Monte Carlo / Latin hypercube sampling over attribute distributions and
meteorological classes (Harmonoise uses ~30 met classes [web]), surrogate models
(polynomial chaos, Gaussian processes) for expensive propagation runs, and variance-based
global sensitivity analysis (Sobol indices) to rank which environment parameters matter
[K]. Results should be reported as distributions (e.g. detection probability with
confidence bands) rather than single values.

**Requirements:**

* Attributes may be stored as **distributions or samplers** (mean, spread, bounds, or a
  named distribution) as well as point values, with a deterministic sampling API
  (seeded).
* **Provenance** on every attribute and geometry element: source (measured, survey,
  procedural, default library), version, LOD/tolerance, timestamp.
* **Deterministic regeneration**: same seed + same generator version → bit-identical
  scene (and stable IDs), so Monte Carlo runs and regression tests are reproducible.

## 4. Scenario variation via procedural generation

Procedural modelling is the main reason to build UberTopo instead of using scanned
meshes: it can generate many plausible variants of a site and of generic environments.

1. **Parameter randomization**: materials, colours/textures, ground types, moisture,
   season (leaf-on/off), time of day, weather, ambient-noise levels.
2. **Structural randomization**: building layouts, heights, roof types, facade
   articulation, vegetation species/density/placement, street furniture, parked cars,
   construction sites, cranes, wires.
3. **Structured (context-aware) randomization**: variations follow scene grammars and
   real statistics (zoning, road hierarchy, typical setbacks), avoiding random-object
   soup; structured domain randomization closed most of the sim-to-real gap for visual
   drone detection ([web] Cranfield AP50 97.0 % vs 97.8 %).
4. **Real-site perturbation**: start from a surveyed digital twin and apply plausible
   changes (new building, trees grown, temporary event structures, parked vehicles) to
   test robustness of a sensor layout. This needs the procedural system to accept
   **measured geometry as constraints** and to fit generator parameters to observed
   sites (inverse procedural modelling).
5. **Operating-envelope coverage**: define the dimensions of the operating envelope
   (environment type, density, weather, drone type, swarm size, approach direction) and
   sample them with coverage metrics (combinatorial / low-discrepancy sampling), as
   scenario-based testing does in automotive (OpenSCENARIO/OpenDRIVE) [K].
6. **Adversarial / falsification search**: optimise scenario parameters (Bayesian
   optimisation, evolutionary search) to find worst cases — the corridor or weather
   condition where the defence fails. Requires the generator to be **callable in a loop
   with parameter vectors** and to be fast at the survey LOD.

**Requirements on the procedural core:**

* Every random choice is a named, typed, bounded **parameter** with a distribution,
  exposed to an external sampler (no hidden randomness).
* Parameters are **hierarchical** (site → district → block → building → facade) so
  variation can be applied at the right level.
* Generation is **incremental and local**: changing one block's parameters regenerates
  only that block (and its dependent LOD tiles and derived fields).
* Generators produce **all representations consistently** (visual mesh, acoustic proxy
  with edges, thermal mesh, CFD proxy, ESDF, semantic labels) from the same parameters
  and seed.
* Export of variants as **USD variant sets** or parameter files, so downstream
  simulators can switch variants without re-export.

## 5. Derived requirements (validation and variation)

| # | Requirement |
|---|---|
| VV-1 | Attribute values as distributions with seeded sampling |
| VV-2 | Provenance metadata on geometry and attributes; LOD/tolerance recorded in every export |
| VV-3 | Deterministic, versioned regeneration with stable IDs |
| VV-4 | Ability to import surveyed sites (lidar/photogrammetry/CityGML) and annotate/upgrade them |
| VV-5 | Generate the same scene at a ladder of LODs for sensitivity studies |
| VV-6 | Exposed, hierarchical, bounded generator parameters for DR, coverage and adversarial search |
| VV-7 | Incremental local regeneration and dependency tracking of derived products |
| VV-8 | Canonical test scenes (half-space, wedge, street canyon, forest plot, shoebox) as built-in generators for verification |

## References

* [web] Wisniewski et al., structured domain randomization for drone detection. https://arxiv.org/html/2411.09077v1
* [web] Dieter et al., sim-to-real gap for drone detection (DLR). https://elib.dlr.de/195082/
* [web] Harmonoise meteorological classes (DGMR). https://dgmr.nl/harmonoise-noise-predictions-and-the-new-european-harmonised-prediction-model
* [web] Sionna RT (differentiable calibration of materials). https://arxiv.org/pdf/2303.11103
* [web] DIRSIG truth outputs. https://dirsig.cis.rit.edu/docs/reveal.js/overview.html
* [K] Attenborough et al., "Benchmark cases for outdoor sound propagation models", JASA 97(1):173-191, 1995 (DOI not re-verified).
* [K] Brinkmann et al., "A round robin on room acoustical simulation and auralization", JASA 145(4), 2019. https://doi.org/10.1121/1.5096178
* [K] ANSI/ASA S1.18, in-situ ground impedance measurement. https://webstore.ansi.org/
* [K] Saltelli et al., *Global Sensitivity Analysis: The Primer*, Wiley 2008. https://doi.org/10.1002/9780470725184
* [K] Tobin et al., domain randomization. https://arxiv.org/abs/1703.06907
* [K] Prakash et al., structured domain randomization. https://arxiv.org/abs/1810.10093
* [K] ASAM OpenSCENARIO. https://www.asam.net/standards/detail/openscenario/
