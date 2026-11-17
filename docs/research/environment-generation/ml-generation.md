# ML-based environment generation (state as of late 2026): a skeptical review

Part of the [environment-generation research set](README.md).

Research status: November 2026. Items found by web search in this session are cited with URLs.
The search budget ran out partway, so coverage of 2026 work is incomplete. Treat this as a map of
directions, not a leaderboard.

---

## 1. Framing

UberTopo needs **metrically valid, topologically consistent, semantically labelled,
deterministic, multi-LOD geometry** whose acoustic and visual properties can be traced to its
inputs. Most generative ML in 3D outputs one of:
- rasters (heightmaps, semantic maps, images),
- implicit fields / NeRFs / 3D Gaussian splats (no surfaces, no topology),
- triangle soups or meshes (often not watertight, no semantics, arbitrary scale),
- graphs or vector polygons (floor plans, road graphs). These are the most compatible with us.

The productive pattern, repeated across the literature, is **"ML proposes, procedural/constraint
system disposes"**: learned models supply priors, parameters or layouts, and a deterministic
generator plus validator produces the final geometry. Recommendation: **design the procedural
core so that any parameter or layout stage can be sampled from a learned model, but never let a
learned model emit final kernel geometry directly.**

---

## 2. Terrain

| Work | What it does | Assessment |
|---|---|---|
| Guérin et al. 2017 (cGAN sketch → DEM, https://doi.org/10.1145/3130800.3130804) | Sketch of ridges/rivers → heightmap | Early; resolution and drainage limited |
| Lochner et al. 2023 (CGF, https://doi.org/10.1111/cgf.14941) | Diffusion-based interactive terrain authoring | Good interactivity |
| MESA (CVPRW 2025, https://arxiv.org/abs/2504.07210) | Text → terrain via latent diffusion trained on global Copernicus DEM (Major TOM Core-DEM dataset released) | Data-centric; ~30 m source data; text control is a gimmick for us |
| Terrain Diffusion (Goslin, Dec 2025, https://arxiv.org/abs/2512.08309) | Hierarchical diffusion stack + "InfiniteDiffusion" for unbounded, seamless, real-time terrain; positioned as a "successor to Perlin noise" | Interesting for infinite synthetic worlds; seamless tiling is the key contribution |

**Skeptic's notes:**
- Training data is mostly Copernicus/SRTM DSMs at 30–90 m. The models learn **DSM artefacts**
  (canopy, buildings, radar noise) and cannot invent valid sub-30 m structure.
- **No hydrological guarantee.** Generated terrains can contain sinks and inconsistent drainage.
  Always post-process with priority-flood conditioning, and check flow-accumulation statistics
  against real terrain.
- **Determinism and LOD consistency**: diffusion sampling is seedable, but hierarchical
  consistency (coarse = low-pass of fine) is not guaranteed unless built in, as Terrain Diffusion
  attempts.
- For real sites, ML terrain is irrelevant at the macro scale (we have DEMs). The useful niche is
  **learned amplification** (super-resolution from 30 m to 1–5 m). Here the risk is hallucinated
  ridges that change LOS. Use it only where the uncertainty is reported.

---

## 3. City layout and 3D cities

| Work | Type | Notes |
|---|---|---|
| InfiniCity (Lin et al., ICCV 2023) *(verify)* | BEV map generation → 3D | Infinite-scale semantic maps |
| CityDreamer (Xie et al., CVPR 2024, https://arxiv.org/abs/2309.00610) | Compositional neural fields: buildings vs. background | Renders, no CAD geometry |
| CityGen (https://arxiv.org/abs/2312.01508) | Outpainting layouts (roads, buildings, vegetation) to unbounded extent | Layout rasters |
| ControlCity (https://arxiv.org/abs/2409.17049) | Multimodal diffusion (ControlNet) generates building footprints conditioned on roads + land use from OSM | Per-city models; footprint rasters → vectorise |
| Multimodal diffusion for urban design (https://arxiv.org/abs/2505.24260) | Staged: roads/land use → building layout → detail | Human-in-the-loop |
| Scene-level 3D city generation (https://arxiv.org/abs/2509.23804) | Graph-based layout + cVAE for heights | Topology-aware |
| UrbanWorld (https://arxiv.org/abs/2407.11965) | OSM layout + urban multimodal LLM + Blender | LLM for scene design |
| CityX (https://arxiv.org/abs/2407.17572) | Multimodal input (OSM, semantic maps, satellite) → **executable procedural programs** for unbounded 3D cities | Closest to our architecture |
| CityGenAgent (Feb 2026, https://arxiv.org/abs/2602.05362) | LLM writes Block and Building programs, refined via natural-language edits | Same "LLM → procedural program" pattern |

**Skeptic's notes:**
- Most outputs are **renders or rasters**. Only the program-synthesis line (CityX,
  CityGenAgent) produces editable structure, and there the 3D quality comes from the
  procedural backend.
- Evaluation is usually FID/KID on renders, which says nothing about metric plausibility (street
  widths, parcel access, H/W ratios). For us the evaluation should be **morphometrics**
  (momepy/OSMnx statistics vs. the target region; see [urban-layout.md](urban-layout.md)).
- Training regions are biased (China, US, Europe). Informal settlements, military sites and
  industrial zones are underrepresented. These are exactly the high-value cases for drone
  defense.
- **Useful now:** footprint/height *extraction* from imagery at global scale (Google Open
  Buildings, Microsoft, GlobalBuildingAtlas). This is ML used for *measurement*, not generation,
  and it is mature enough to use with confidence attributes.
- **LLMs as program authors** (CityX, CityGenAgent, Holodeck for interiors) are a plausible
  front-end for scenario authoring ("a fenced substation next to a two-lane rural road in a pine
  forest"). This only works if our procedural core has a clean, documented, typed parameter
  API, which argues for a declarative rule/graph representation that an LLM can write and a
  validator can check.

---

## 4. Buildings

- **Reconstruction from LiDAR / imagery** (see [real-world-data-sources.md](real-world-data-sources.md)):
  Building3D benchmark (ICCV 2023), Point2Roof, Points2Model (2025), PolyGNN (polyhedron-based GNN,
  ISPRS J. 2024, *verify*), LOD2-Former (ISPRS Annals 2026). Learned methods are getting good at
  roof wireframes, but still miss edges and can produce invalid topology. **Use them as proposers
  feeding a PolyFit/KSR-style solver that guarantees watertight output.**
- **Facade/texture generation**: FrankenGAN (2018) and successors; image diffusion now produces
  plausible facade textures. For simulation, the only facade property that matters is the
  **opening layout** (window grid, WWR, door positions) plus material. Facade parsing models
  (segmenting windows in street-level imagery, e.g. from Mapillary or Street View, subject to
  licence) can extract real opening layouts to drive split grammars.
- **Text/image → 3D asset generators** (e.g. TRELLIS, https://arxiv.org/abs/2412.01506; Hunyuan3D
  2.x; and others): produce textured meshes of single objects (furniture, street furniture,
  vehicles). Possibly useful to populate *asset libraries* for camera simulation after manual
  curation (scale normalisation, retopology, material assignment). Not suitable for
  buildings-as-CAD.

## 5. Floor plans and interiors
Covered in [interiors.md](interiors.md). Graph-conditioned diffusion (HouseDiffusion) is the best
vector-output approach; narrow training data (RPLAN). Infinigen Indoors' constraint solver and
Holodeck's LLM + solver are the pragmatic patterns.

## 6. Vegetation
- Learned tree reconstruction from images/LiDAR and neural/Gaussian-splat vegetation exist, but
  procedural plant models (Weber–Penn, self-organising, SpeedTree) remain better for
  controllability, LOD and botanical plausibility.
- **EcoLearn (Kapp et al. 2020)** is the useful ML direction: learn *placement statistics and canopy
  height from real data*, not geometry.
- Global ML canopy-height products (Meta/WRI 1 m, ETH 10 m) are measurement data we should ingest.

## 7. Neural scene representations (NeRF, 3D Gaussian splatting)
Excellent for photorealistic **visual** replicas of surveyed sites (drone imagery → splats in
hours). They have no surfaces, so they cannot serve acoustic simulation or CAD queries directly.
Mesh extraction from them (SuGaR, 2DGS, etc.) yields noisy, unsegmented meshes. **Possible niche:**
a visual layer for camera simulation registered to our semantic B-rep, with the B-rep used for
physics and the splats for appearance. Watch the alignment and the lighting (baked).

---

## 8. Recommendations

1. Architect the procedural core with **explicit, typed, sampleable parameters at every stage**
   (land-use field, road graph, parcel set, typology parameters, plant distribution). ML models
   then plug in as samplers or proposers, and LLMs can author scenarios as programs.
2. Use ML for **measurement** (footprints, heights, canopy, field boundaries, roof structure)
   now; use it for **generation** only behind validators.
3. Evaluate any generator, learned or procedural, with **domain metrics**: drainage statistics
   for terrain, morphometrics for cities, gap-fraction/stem-density distributions for forests,
   and the downstream acoustic/LOS metrics that matter.
4. Do not depend on any single ML model in the core. Licences, weights and APIs churn yearly.

---

## References

- Guérin et al., cGAN terrain authoring, 2017 — https://doi.org/10.1145/3130800.3130804
- Lochner et al., diffusion terrain authoring, CGF 2023 — https://doi.org/10.1111/cgf.14941
- MESA, CVPRW 2025 — https://arxiv.org/abs/2504.07210 ; https://openaccess.thecvf.com/content/CVPR2025W/MORSE/html/Borne--Pons_MESA_Text-Driven_Terrain_Generation_Using_Latent_Diffusion_and_Global_Copernicus_CVPRW_2025_paper.html
- Terrain Diffusion, 2025 — https://arxiv.org/abs/2512.08309
- CityDreamer, CVPR 2024 — https://arxiv.org/abs/2309.00610
- CityGen — https://arxiv.org/abs/2312.01508
- ControlCity — https://arxiv.org/abs/2409.17049
- Multimodal diffusion urban design — https://arxiv.org/abs/2505.24260
- Scene-level 3D city generation — https://arxiv.org/abs/2509.23804
- UrbanWorld — https://arxiv.org/abs/2407.11965
- CityX — https://arxiv.org/abs/2407.17572
- CityGenAgent — https://arxiv.org/abs/2602.05362
- Building3D, ICCV 2023 — https://arxiv.org/abs/2307.11914
- LOD2-Former, ISPRS Annals 2026 — https://isprs-annals.copernicus.org/articles/XI-2-2026/187/2026/
- FrankenGAN — https://arxiv.org/abs/1806.07179
- TRELLIS — https://arxiv.org/abs/2412.01506
- Holodeck — https://arxiv.org/abs/2312.09067
- HouseDiffusion — https://arxiv.org/abs/2211.13287
- EcoLearn — https://doi.org/10.1145/3414685.3417848
- GlobalBuildingAtlas — https://arxiv.org/abs/2506.04106
