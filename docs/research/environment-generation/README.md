# Environment generation research

Algorithms and data for generating each environment type UberTopo must model, at multiple levels
of detail, for developing and testing drone-defense systems: acoustic/camera sensor networks
locating drone swarms (including reflections), and vulnerability surveys of sites. See
[`docs/requirements.md`](../../requirements.md).

Sibling research threads (other folders under `docs/research/`) cover procedural-system
architecture, B-rep data structures, CAD kernels, curves (clothoids/alignments),
attributes/spatial queries, and simulation requirements. This folder covers **domain generation
techniques and real-world data sources**.

Research status (November 2026): based on web searches made in this session plus prior knowledge.
The shared web-search budget ran out partway through, and doi.org was not reachable from the
session. Citations marked *(verify)* were not re-checked online.

## Files

| File | Contents |
|---|---|
| [buildings.md](buildings.md) | Massing, roofs (straight/weighted skeletons, Kelly & Wonka procedural extrusions), facades (split grammars, CGA), openings, stairs, typologies by function/region, attributes |
| [interiors.md](interiors.md) | Floor-plan generation (Merrell 2010, treemaps, RPLAN/House-GAN/HouseDiffusion), Infinigen Indoors, ProcTHOR, cores/stairs, large-volume interiors |
| [road-networks.md](road-networks.md) | Parish & Müller, tensor fields, example-based, terrain-aware hierarchical roads; cross-sections; junction geometry; OpenDRIVE; osm2streets; bridges/tunnels; parking; rail |
| [urban-layout.md](urban-layout.md) | Land use and density fields, UrbanSim, blocks, parcel subdivision (Vanegas 2012), special site templates (military, airport, port…), morphometric validation |
| [terrain.md](terrain.md) | Representations, noise, erosion (thermal, hydraulic, stream power + uplift), hydrology-first, example/ML-based, amplification, cliffs/caves, LOD, ground materials |
| [water.md](water.md) | Rivers and networks, meanders, lakes/reservoirs, wetlands/mangroves, coasts, canals/dams, bathymetry, hydrography data |
| [vegetation-and-biomes.md](vegetation-and-biomes.md) | Plant models (L-systems, Weber–Penn, space colonization, self-organizing), placement and ecosystems, biomes, dunes, agriculture, vegetation LOD, acoustic modelling |
| [infrastructure.md](infrastructure.md) | Power lines (catenaries, towers), substations, solar/wind, telecom, pipelines, fences, poles and street furniture, industrial plants, airports |
| [real-world-data-sources.md](real-world-data-sources.md) | OSM, Overture, ML footprints/heights, CityGML/CityJSON, IFC, LiDAR + building reconstruction, DEMs, land cover, 3D Tiles, Google 3D Tiles, glTF, OpenUSD; conflation and fill-in pipeline |
| [ml-generation.md](ml-generation.md) | Skeptical review of ML generation (terrain diffusion, city layout, building reconstruction, floor plans, neural scenes) |
| [sensing-properties-and-lod.md](sensing-properties-and-lod.md) | Cross-cutting LOD ladder; acoustic, camera and vulnerability properties generators must emit; kernel implications |

## Key findings

1. **Acoustic LOD can be much coarser than visual LOD.** Drone signatures sit at about
   100 Hz–4 kHz (λ ≈ 3.4–0.09 m). Detail under about 0.1–0.3 m becomes a *scattering coefficient*,
   not geometry. Large-face position accuracy, clean diffraction edges (wedge angles), ground
   impedance and openings matter most. LOD should be selected **per query** (Fresnel zones for
   acoustics, pixel footprint for cameras), not per viewer.
2. **A small set of geometric operators covers most generators**: the weighted straight skeleton
   (hip/gable roofs, *and* parcel subdivision), **sweep of a profile along an embedded curve**
   (roads, rail, rivers, canals, pipes, walls, fences, retaining walls, stadium bowls, quarry
   benches, terraces, Kelly–Wonka procedural extrusions), **split grammars on face-local frames**
   (facades), **planar subdivision/arrangement with attributes** (land use → blocks → parcels),
   **least-cost routing over terrain** (roads, power lines, pipelines), and **point processes**
   (vegetation, furniture, rooftop equipment). Making these robust (exact predicates, stable IDs)
   is worth more than any single domain algorithm.
3. **Real data is the skeleton; procedural is the flesh.** For site surveys, ingest
   OSM/Overture (GERS stable IDs), ML footprints and heights (Google 2.5D, GlobalBuildingAtlas),
   bare-earth DTMs (3DEP/national LiDAR > FABDEM > *never* raw Copernicus DSM as ground),
   canopy height and land cover. Fill in roofs, facades, openings, interiors and species
   procedurally, seeded by stable source IDs and tagged with provenance and confidence for
   sensitivity analysis.
4. **Hydrology must be consistent**: generate terrain and rivers together (Génevaux 2013;
   uplift + stream-power, Cordonnier 2016 / Schott 2023), and condition every DEM
   (priority-flood). Noise-only terrain gives wrong valley networks, and valley networks are
   the drone approach corridors.
5. **Vegetation needs three consistent representations**: per-plant records (placement), explicit
   meshes near consumers, and volumetric/statistical media (LAD, gap fraction, stem density, DBH)
   for LOS and acoustics. EcoLearn-style placement driven by real canopy-height data is the best
   fit.
6. **Thin 1D structures** (power lines, guy-wires, lattice towers, fences, trellis wires) are drone
   hazards and camera confusers. Keep them as exact curves/wire edges with attributes
   (catenaries are transcendental curves, matching the kernel requirement). Never mesh them at
   generation time.
7. **Environment generators should emit more than geometry**: ambient/confuser sound sources
   (traffic, transformers at 100/120 Hz, HVAC, turbines, water), sensor-mounting candidates, and
   vulnerability annotations (roof access, landing zones, cover, corridors, obstacles, occluded
   volumes).
8. **Kernel evidence**: faces with holes are needed everywhere; non-manifold sheets and wire edges
   (fences, wires, lattices); **shared-face cellular volumes** (rooms, water bodies, soil layers),
   which supports an explicit 3-cell layer; edge classification (feature vs. tessellation);
   exact embedded curves.
9. **ML**: use it now for *measurement* (footprints, heights, canopy, roof wireframes) and as
   *parameter/layout proposers* behind validators. LLM → procedural-program approaches (CityX,
   CityGenAgent, Holodeck) argue for a typed, declarative, sampleable parameter API in our
   procedural core. Do not let learned models emit final kernel geometry.

## Open questions for the project owner

- Which **frequency band and sensor types** are in scope (acoustic only up to a few kHz? EO
  cameras only, or also thermal IR, radar, RF)? This changes which material properties
  generators must emit.
- Target **geographic regions and site types** first (e.g. European cities, US
  suburbs, military bases, critical infrastructure, Global South informal settlements)? Typology
  libraries are region-specific.
- Required **scene extent and max fidelity** (e.g. 10 km × 10 km at LOD2 with a 1 km² LOD3 core?)
  and real-time vs. offline generation budgets.
- Do scenes need **interiors** (indoor drone incursions) or only openings coupled to statistical
  room volumes?
- **Data licensing constraints**: is ODbL share-alike acceptable for outputs? Is classified or
  export-controlled data (e.g. military site geometry) going to be ingested, which affects
  architecture (air-gapped pipelines)?
- Are **time-varying states** (season, tide, water level, traffic, crowds, construction cranes)
  part of the scenario model?
- Should we target **interoperability formats** as first-class outputs (CityJSON/CityGML,
  OpenDRIVE, 3D Tiles, OpenUSD, IFC 4.3)? This affects the internal semantic model.
