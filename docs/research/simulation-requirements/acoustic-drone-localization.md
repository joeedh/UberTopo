# Acoustic Drone Detection and Localization: Simulation Chain Requirements

Part of [simulation-requirements](README.md). Propagation physics is in
[outdoor-acoustics.md](outdoor-acoustics.md). This file covers the drone as a sound
source, the sensing algorithms, the effect of multipath and reflections, existing
datasets and systems, and what the end-to-end simulation chain needs from UberTopo.

Source tags: **[web]** verified via web search in this research; **[K]** background
knowledge, not re-verified online.

---

## 1. The drone as an acoustic source

* **Tonal content.** Each rotor emits a harmonic series at BPF = B·Ω/60 plus shaft-rate
  tones (from blade imbalance) and motor tones (electrical commutation, often several kHz).
  Hovering quadcopters show BPFs around 150-250 Hz; tones dominate up to ~6 kHz with a
  mid-frequency broadband hump ([web] Intaratep et al. AIAA 2016-2873; NASA studies).
* **Time variation.** Multirotors steer by changing individual rotor speeds, so each
  rotor's harmonic series wanders independently; the signature in flight is unsteady,
  unlike the clean lines in hover ([web] TU Delft; preprints). A swarm of N quadcopters is
  4N slightly detuned harmonic series — a hard source-separation and counting problem.
* **Level.** Small quadcopters are of order 70-85 dBA at 1 m (varies strongly with mass
  and loading) [K]. At 300 m spreading alone removes ~50 dB, putting the drone near urban
  ambient levels (45-65 dBA): detection is threshold-limited, so **ambient noise modelling
  is as important as propagation**.
* **Directivity.** Rotor noise is directional (strong below and near the rotor plane for
  thickness/loading noise); simulators of UAM noise include multi-rotor directivity
  ([web] PolyU EnvARC paper). The source model therefore needs drone attitude, which the
  swarm flight simulation provides.
* **Requirement on UberTopo:** none directly on geometry, but the scenario layer must
  carry drone trajectories with attitude and rotor states at audio-relevant rates
  (≥ 100 Hz kinematics, interpolated to audio rate), in the same coordinate frame as the
  environment.

## 2. Sensing approaches

| Approach | Principle | Sensitivity to environment model |
|---|---|---|
| Single-mic detection/classification | spectral features (harmonic lines, MFCC, CNN on spectrograms) | ambient noise realism; reverberation smears harmonics |
| Compact array DOA | beamforming (delay-and-sum, MVDR), SRP-PHAT, MUSIC, ESPRIT, GCC-PHAT pairs | reflections create ghost DOAs; diffraction shifts apparent bearing to edges; mount geometry near array causes comb filtering |
| Distributed TDOA | GCC-PHAT time differences between widely spaced nodes, multilateration | requires node positions to mm-cm; clock sync; multipath peaks in GCC |
| Bearing triangulation | DOAs from several arrays intersected | per-array errors from local reflectors |
| Learning-based localization | DNNs mapping multichannel features to DOA/position | trained on simulated data → domain gap if simulation lacks reflections and turbulence |
| Fingerprinting | match observed features to a simulated map of the site | entirely dependent on the site model accuracy |

Published systems and studies:

* Distributed MEMS microphones, Random Forest classification, TDOA triangulation; names
  multipath, environmental noise and range as main limits ([web] Ghouli, Acta Acustica 2026).
* Tetrahedral array + DNN validated on real flights; wind noise, building multipath and
  broadband rotor spectra break common assumptions ([web] MDPI Sensors 2026).
* Fraunhofer FKIE "Crow's Nest": volumetric randomly distributed arrays with coherent
  broadband beamforming and space-time filtering ([web] Forum Acusticum 2025).
* CNAM/ISL (Bavu, Hengy, Ramamonjy): compact MEMS arrays for drone detection and
  tracking in urban settings ([web]).
* Choi (Virginia Tech 2012): urban source localization where multipath, reflections and
  diffraction defeat conventional methods; compares phased-array beamforming with
  fingerprinting using many dispersed microphones ([web]). The fingerprinting approach
  requires a high-fidelity site model.
* Typical real-world acoustic detection range for small drones is ~100-500 m, dominated
  by wind, ambient noise and array size; windscreens extend range 31-131 % in strong wind
  ([web] MDPI Sensors 2025 outdoor mic range tests; Stevens; practitioner guides).

## 3. Multipath: the core problem the project names

Effects of the environment on localization:

1. **Ghost sources.** A specular reflection behaves like an image source mirrored in the
   reflector plane. Beamformers and TDOA estimators see it as a second drone. In a street
   canyon with two parallel facades, a series of images extends down the canyon.
2. **Coherent interference.** Direct + reflected tonal components interfere; with
   moving drones the comb filter sweeps, so harmonic amplitudes fade in and out (the
   ground reflection alone does this at every node).
3. **Diffracted-only reception.** When buildings block the direct path, the strongest
   arrival comes from roof edges or corners; the measured DOA points at the edge.
4. **Reverberant tail and diffuse field.** Raises the noise floor for GCC-PHAT and reduces
   array coherence.
5. **Refraction and turbulence.** Bend rays (elevation errors), create shadow zones (loss
   of detection upwind), decorrelate signals across long baselines.
6. **Doppler and retarded time.** Direct and reflected paths have different Doppler
   shifts and different emission times; a 20 m extra path is 58 ms delay.

Two research directions follow, and both drive UberTopo requirements:

* **Simulation for training and test:** generate realistic multichannel signals with all
  of the above so algorithms are trained/tested against realistic multipath. Needs the
  full propagation model of [outdoor-acoustics.md](outdoor-acoustics.md).
* **Map-aware (echo-aware) localization at run time:** treat reflectors as known and use
  echoes as extra virtual sensors. MIRAGE treats a microphone and its mirror image across
  a reflector as a virtual larger array to recover elevation from two mics ([web]); the
  acoustic rake receiver combines echoes in beamforming ([web]); EchoSLAM infers walls
  from echo times ([web]). The radio analogue is GNSS shadow matching, where a 3D city
  model predicts LOS/NLOS and improves urban positioning from ~25 m to < 2 m ([web]
  Groves, UCL; see [rf-propagation.md](rf-propagation.md)). **Implication:** UberTopo
  must export a light **"reflector map" / acoustic site model** (dominant planar
  reflectors with normals, extents and materials; dominant diffracting edges; ground
  impedance) that the *deployed* localization system loads, not just the simulator.

## 4. Swarm-specific needs

* Dozens to hundreds of simultaneous sources; per source the propagation path set changes
  continuously. Path computation should run from whichever side is smaller (sensor nodes
  are often fewer than swarm members, but distributed networks may have 10-100 nodes) and
  exploit reciprocity: trace from each node once per time step, then evaluate per-source
  paths by connecting to the source positions (bidirectional / listener-based tracing,
  as in GSound-style engines).
* Ground truth must include **per-drone, per-path** labels (direct/reflected/diffracted,
  order, delay, DOA at the array, amplitude per band) so algorithms can be scored on
  association and counting, not only position error.
* Swarm members occlude and scatter for each other negligibly in acoustics (small
  bodies), but they matter for EO and radar.

## 5. Sensor and site details that dominate errors

* **Mounting geometry.** Arrays sit on poles, rooftops, walls, vehicles. Surfaces within
  a few metres produce strong early reflections (1-30 ms) that dominate DOA bias. This
  local geometry needs **cm-level detail** up to 4-8 kHz: parapets, roof edges, the pole,
  the enclosure. This is the strongest argument for **sensor-centric LOD**.
* **Microphone heights above ground** set the ground-dip frequencies; terrain and ground
  type within ~10-50 m of each node must be accurate.
* **Node positions and orientations** must be known to < 1 cm / < 1° in the model for
  TDOA studies (1 cm ≈ 29 µs).
* **Wind noise at the microphone** (turbulent pressure on the capsule) is a sensor-model
  effect but depends on local wind speed at the mount, which depends on the building wind
  field (see [vulnerability-surveys.md](vulnerability-surveys.md) on CFD-lite).
* **Ambient noise field.** Spatially varying: roads, HVAC, birds, foliage rustle,
  industry, aircraft, water. The environment model should carry **noise-source
  attributes** (road traffic class, HVAC sound power, etc.) so a coherent ambient field
  can be synthesised and propagated with the same engine.

## 6. Datasets and simulators for acoustic UAV work

* **DREGON** (Inria, IROS 2018): 8-channel cube array *on* a quadrotor, motion-capture
  ground truth, egonoise + source localization ([web]).
* **AIRA-UAS** corpus: in-flight audio for UAS processing evaluation ([web]).
* **DroneAudioset** (2025): 23.5 h, SNR −57 to −2.5 dB, for drone-based search and
  rescue ([web]).
* **Svanström et al. multi-sensor drone detection dataset** (visible, IR, audio) [web
  mention; K].
* Al-Emadi drone audio dataset (classification) [K: https://github.com/saraalemadi/DroneAudioDataset].
* Room-acoustics simulators commonly used to make training data: pyroomacoustics
  (shoebox ISM + ray tracing), GSound/pygsound, GSound-SIR, SoundSpaces (Habitat) —
  all indoor-oriented; none models outdoor refraction, ground impedance or turbulence
  ([web] GSound/pygsound; see [simulators-and-engines.md](simulators-and-engines.md)).
* **Gap:** there is no widely used **outdoor, urban, moving-source, multi-node** acoustic
  simulator for C-UAS. Most drone-noise propagation codes target community noise impact
  (levels), not phase-accurate multichannel signals. This is a gap UberTopo plus a
  propagation engine could fill.

## 7. The end-to-end acoustic simulation chain

```
scenario (drone trajectories, rotor states, met conditions, time of day)
   │
   ├─ source model: rotor harmonics + broadband, directivity, attitude
   ├─ environment model (UberTopo): surfaces+materials, edges, ground, vegetation,
   │      atmosphere fields, ambient sources, dynamic objects
   ├─ propagation: per (source, node, time-step) path set:
   │      direct, ISM reflections, UTD/BTM diffraction, diffuse tail,
   │      low-band wave solution, refraction, turbulence coherence
   ├─ rendering: time-varying FIR per path with fractional delay & Doppler
   ├─ sensor model: capsule response, windscreen, wind noise, self-noise,
   │      ADC, clock offset/jitter, node position error
   ├─ ambient noise synthesis (spatially coherent where needed)
   └─ outputs: multichannel audio + ground truth (positions, per-path labels)
```

Performance estimate: 100 drones × 50 nodes × path update at 20-50 Hz. With
receiver-side tracing at 10⁵ rays per node per update, 50 nodes × 50 Hz × 10⁵ =
2.5×10⁸ rays/s with several bounces each, i.e. ~10⁹ ray-segment intersections/s —
within reach of RTX-class GPU ray tracing, but only if the acoustic scene is kept to
~10⁵-10⁷ triangles in the active region. Offline (non-real-time) dataset generation can
trade this for more rays. Either way the environment model must **export compact,
band-appropriate proxies** rather than the full visual mesh.

## 8. Derived requirements (acoustic localization)

| # | Requirement | Rationale |
|---|---|---|
| AL-1 | Sensor-centric LOD: full detail (≤ 1-2 cm tolerance) within ~10-30 m of each sensor; λ/8 of the band of interest elsewhere | early reflections near the mount dominate DOA bias |
| AL-2 | Export a compact "reflector map" (merged planar reflectors + main edges + ground) for run-time map-aware localization | echo-aware methods, fingerprinting |
| AL-3 | Positions in double precision in a geodetic/ENU frame; mm-level consistency over km | TDOA (1 cm ≈ 29 µs) |
| AL-4 | Noise-source attributes on roads, buildings (HVAC), industry, water | ambient noise sets detection threshold |
| AL-5 | Time-varying scene: dynamic objects with trajectories; queries at 20-50 Hz | moving vehicles, foliage sway, swarm |
| AL-6 | Material attributes as distributions (mean ± spread) for Monte Carlo | uncertainty quantification |
| AL-7 | Per-path ground-truth export: path type, order, interacting faces/edges IDs | algorithm scoring, ghost-source labelling |
| AL-8 | Stable persistent IDs for faces/edges across LOD and regeneration | labels must refer to the same object across LODs |
| AL-9 | Local wind speed at sensor mounts (from CFD-lite or parametric) | wind noise model |
| AL-10 | Atmospheric fields (T, RH, wind, turbulence) co-registered | refraction, absorption, coherence |

## References

* [web] Intaratep et al., quadcopter acoustics at static thrust, AIAA 2016-2873.
  https://www.bu.edu/ufmal/files/2016/07/aiaa-2016-2873.pdf
* [web] NASA, rotor acoustics for UAS. https://ntrs.nasa.gov/api/citations/20160006429/downloads/20160006429.pdf
* [web] TU Delft drone noise paper. https://pure.tudelft.nl/ws/portalfiles/portal/86910795/6.2021_0713.pdf
* [web] Ghouli, passive MEMS array TDOA drone detection, Acta Acustica 2026.
  https://acta-acustica.edpsciences.org/articles/aacus/full_html/2026/01/aacus250134/aacus250134.html
* [web] Tetrahedral array + DNN, MDPI Sensors 26(6):1778, 2026. https://www.mdpi.com/1424-8220/26/6/1778
* [web] Fraunhofer FKIE volumetric arrays, Forum Acusticum 2025. https://dael.euracoustics.org/confs/fa2025/data/articles/000108.pdf
* [web] CNAM, acoustic localization and detection of UAVs. https://acoustique.cnam.fr/contacts/bavu/en/project/these_aro
* [web] Choi, urban source localization dissertation, Virginia Tech 2012.
  https://vtechworks.lib.vt.edu/bitstreams/0aad4071-4f07-4190-ab62-358d9e7219ef/download
* [web] Outdoor microphone range tests and UAV signatures, MDPI Sensors 2025.
  https://www.ncbi.nlm.nih.gov/pmc/articles/PMC12656299/
* [web] Stevens drone acoustic detection system. https://researchwith.stevens.edu/en/publications/improvements-to-the-stevens-drone-acoustic-detection-system/
* [web] Practitioner acoustic ML pipeline notes (range 100-500 m, non-peer-reviewed).
  https://www.jaredwatkins.com/research/drone-detection/open-source/acoustic-ml-pipeline/
* [web] NATO STO MP-SET-312 paper on acoustic C-UAS. https://www.sto.nato.int/publications/STO%20Meeting%20Proceedings/STO-MP-SET-312/MP-SET-312-18.pdf
* [web] Strauss et al., DREGON dataset, IROS 2018. https://hal-univ-tlse3.archives-ouvertes.fr/INRIA/hal-01854878
* [web] AIRA-UAS corpus. https://www.ncbi.nlm.nih.gov/pmc/articles/PMC6767060/
* [web] DroneAudioset. https://arxiv.org/html/2510.15383v1
* [web] IEEE SP Cup 2019 drone audio search-and-rescue. https://arxiv.org/pdf/1907.04655
* [web] Egonoise-resilient localization (2025). https://arxiv.org/pdf/2508.06310
* [web] MIRAGE: echo-augmented 2-mic localization. https://arxiv.org/pdf/1906.08968
* [web] Raking the Cocktail Party (acoustic rake receiver). https://arxiv.org/pdf/1407.5514
* [web] dEchorate calibrated echo database. https://arxiv.org/pdf/2104.13168v1.pdf
* [web] EchoSLAM. https://typeset.io/pdf/echoslam-simultaneous-localization-and-mapping-with-acoustic-2ae3tktg9l.pdf
* [web] PolyU, UAM/drone noise via virtual flights with directivity.
  https://research.polyu.edu.hk/en/publications/assessment-of-uam-and-drone-noise-impact-on-the-environment-based/
* [web] Passmore, passive drone-detection range equation (preprint). https://engrxiv.org/preprint/download/6810/11135/9456
* [web] GSound / pygsound. https://gamma.cs.unc.edu/GSOUND ; https://pypi.org/project/pygsound/0.3
* [web] GSound-SIR. https://arxiv.org/abs/2503.17866
* [web] Groves, shadow matching (UCL). https://discovery-pp.ucl.ac.uk/1394444/1/0681.pdf
