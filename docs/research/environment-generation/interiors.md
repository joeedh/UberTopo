# Interiors and floor plans

Part of the [environment-generation research set](README.md). Exterior shells, openings and
stairs are covered in [buildings.md](buildings.md).

Research status: November 2026, web search plus prior knowledge.

---

## 1. Why interiors at all?

Interiors are **not** needed for most outdoor drone-defense scenarios. They matter in these cases:

- **Acoustic coupling through openings.** An open window, door or loading dock connects the
  exterior field to a reverberant interior volume, and the interior re-radiates. Indoor drone
  incursions (warehouses, stadium concourses, atria, hangars) need a full interior.
- **Camera views through glazing.** Behind glass, a camera sees interior clutter, not a black
  void. A cheap interior-mapping shader is often enough. True geometry is needed when a drone can
  be *inside* or *behind* the glass.
- **Vulnerability surveys of specific sites.** These need roof-access paths (stair cores, hatches),
  large open interior volumes (hangars, warehouses, atria) where drones can operate, and
  guard/sensor positions.
- **Ground-truth scenes for indoor counter-drone tests** (e.g. arenas, airport terminals).

**Recommendation:** model interiors at three tiers.
1. *None*: the interior is a single void cell with an aggregate absorption (an acoustic "room"
   with volume V and mean α), plus a shader-level interior for cameras.
2. *Coarse (core + floors)*: floor slabs, stair/elevator cores, and major subdivisions (open plan
   vs. cellular). Enough for roof-access analysis and for acoustic coupling.
3. *Full*: rooms, doors, interior windows, furniture. Only for designated sites, or for synthetic
   datasets.

The B-rep implication: a building with interiors is a **cellular complex**. Rooms are 3-cells that
share wall faces, and a wall has two sides belonging to different rooms. A 2-manifold
surface-per-solid representation duplicates the shared walls. A non-manifold / cellular
representation (radial-edge or partial-entity style, or a 3-manifold cell complex) stores each
wall once with two adjacent cells. This is direct evidence for the B-rep researchers' question of
whether a 3-manifold structure is needed: **interiors (and water bodies, terrain layers) favour an
explicit volumetric cell complex.**

---

## 2. Floor-plan generation methods

### 2.1 Rule- and optimisation-based

- **Squarified treemaps** (Marson & Musse 2010, "Automatic real-time generation of floor plans
  based on squarified treemaps algorithm", IJCGT, https://doi.org/10.1155/2010/624817): split the
  footprint recursively by room-area targets, then add corridors and doors. Fast and controllable,
  but gives only rectangular rooms in rectangular footprints.
- **Constrained growth** (Lopes, Tutenel, Smelik, de Kraker, Bidarra 2010, "A constrained growth
  method for procedural floor plan generation", GAME-ON): rooms grow from seeds on a grid
  until they fill the footprint, subject to adjacency.
- **Merrell, Schkufza, Koltun 2010, "Computer-generated residential building layouts"** (SIGGRAPH
  Asia, https://doi.org/10.1145/1882261.1866203). This is the key reference. A **Bayesian network**
  trained on real architectural programs generates the *program*: room list, sizes and the
  adjacency graph. **Stochastic optimisation** (Metropolis–Hastings) then lays out the rooms to
  satisfy area, aspect ratio, adjacency and accessibility, across multiple storeys with stairs. The
  output includes 3D models with windows and doors. The two-stage split, *program graph first,
  geometry second*, is still the right architecture.
- **Generalisations**: Bao, Yan, Mitra, Wonka 2013, "Generating and exploring good building
  layouts" (https://doi.org/10.1145/2461912.2461977), explores the space of valid layouts. Wu et
  al. 2018 use mixed-integer quadratic programming for building interiors
  (https://doi.org/10.1111/cgf.13380). Peng, Yang, Wonka 2014, "Computing layouts with deformable
  templates" (https://doi.org/10.1145/2601097.2601164), tile a domain with templates, which also
  suits parking lots and office floors.
- **Commercial/office**: core-and-shell rules (central or side core; lease depth 9–14 m from core
  to glass; column grid 7.5–9 m). Template-based generation is simpler and more plausible than
  learned methods here.

### 2.2 Graph-constrained learned methods

- **RPLAN** (Wu et al. 2019, "Data-driven interior plan generation for residential buildings",
  https://doi.org/10.1145/3355089.3356556) is an 80k-plan dataset of Chinese apartments plus an
  iterative room-placement network. RPLAN is the dominant training set for the papers below, so
  their outputs are biased toward Chinese apartment typologies.
- **Graph2Plan** (Hu et al. 2020, https://arxiv.org/abs/2004.13204): from a boundary plus a layout
  graph to a plan.
- **House-GAN / House-GAN++** (Nauata et al. 2020/2021, https://arxiv.org/abs/2003.06988,
  https://arxiv.org/abs/2103.02574): relational GANs conditioned on a bubble diagram (room-type
  graph).
- **HouseDiffusion** (Shabani, Hosseini, Furukawa, CVPR 2023, https://arxiv.org/abs/2211.13287):
  a diffusion model over room/door *polygon vertex coordinates*, with discrete and continuous
  denoising. It produces vector output, not raster, which is nicer for a B-rep pipeline.
- **Review**: "Computer-Aided Layout Generation for Building Design: A Review" (Computational
  Visual Media, 2025, https://arxiv.org/abs/2504.09694). It finds that deep models still
  produce wrong arrangements, artifacts and limited input formats.

**Skeptical assessment:** learned floor-plan generators are trained on narrow datasets (RPLAN,
LIFULL, MSD), output 2D room polygons without wall thickness or structural logic, and break down on
footprints unlike their training data. That covers most footprints we will see (irregular,
commercial, industrial, non-Asian housing). For UberTopo, **rule/optimisation methods with
typology-specific templates are more robust**. Learned models can be an optional "program proposal"
source for residential units.

### 2.3 Furniture and object layout
- Merrell et al. 2011, "Interactive furniture layout using interior design guidelines"
  (https://doi.org/10.1145/2010324.1964982), and Yu et al. 2011, "Make it home"
  (https://doi.org/10.1145/2010324.1964981): cost-function optimisation of furniture.
- **ProcTHOR** (Deitke et al. 2022, https://arxiv.org/abs/2206.06994): procedural houses for
  embodied AI, made of room specs → floor plan → objects, with semantic richness.
- **Infinigen Indoors** (Raistrick et al., CVPR 2024, https://arxiv.org/abs/2406.11824):
  Blender-based procedural indoor generator with fully procedural assets (furniture, appliances,
  architectural elements) and a **constraint DSL plus solver** for arrangement. It exports to
  real-time simulators. BSD licensed. This is the best open reference for a constraint-language
  design.
- **Holodeck** (Yang et al., CVPR 2024, https://arxiv.org/abs/2312.09067): LLM proposes spatial
  relations, a solver places objects. The "LLM proposes, solver disposes" pattern is the sensible
  use of LLMs.

For acoustics, furniture mainly changes the **aggregate absorption and diffusion** of a room. A
statistical room (volume, surface area, mean α per band, a furniture "fitting density" for
diffuse scattering) usually suffices. Explicit furniture geometry matters for camera views and
for indoor drone navigation.

---

## 3. Stairs, cores, circulation

- **Placement**: cores must stack vertically. Choose core locations at the building level, before
  per-floor layout; Merrell 2010 handles multi-storey by constraining stairs. Egress rules give a
  maximum travel distance and a minimum number of exits.
- **Stair geometry**: parametric generation from floor-to-floor height. The riser count
  n = ceil(H / r_max) and tread depth set the run length. Add landings. The B-rep result is a
  stair solid plus a **shaft void** cut through the floor slabs, i.e. slab faces with holes.
- **Elevators, shafts, atria and light-wells** are vertical voids. They act as acoustic and visual
  pathways between floors and as roof hatches.
- **Roof access**: the stair core extends to a roof penthouse, or a ladder leads to a hatch.
  Record it as an annotation (`roof_access: stair|hatch|ladder|external|none`) for vulnerability
  analysis.

---

## 4. Large-volume interiors (most relevant indoor drone settings)

| Space | Generator approach | Notes |
|---|---|---|
| Warehouse / distribution centre | Rack grid (aisle 3–4 m, rack height 6–12 m), dock doors along one or two long sides | Long reverberation; metal racks are strong scatterers |
| Hangar | Single span, large door, mezzanines | Very large door opening is acoustically huge |
| Stadium / arena | Bowl generated from seating-riser profile swept around a pitch outline (another sweep-with-profile use) | Open roof or retractable roof |
| Airport terminal | Concourse spines with gates; atria | Multi-level, glazing-heavy |
| Atrium / mall | Galleries around voids | Cross-floor visibility |
| Industrial hall | Equipment instancing, cranes, pipe racks | See [infrastructure.md](infrastructure.md) |

These are better built from **parametric templates** than from general floor-plan generators.

---

## 5. Data sources for interiors

- **IFC/BIM** for real sites (see [real-world-data-sources.md](real-world-data-sources.md)): rich
  semantics (IfcSpace, IfcDoor, IfcWindow, IfcStair), but usually only for new-build or
  government sites, and often not shareable.
- **OSM Simple Indoor Tagging** (https://wiki.openstreetmap.org/wiki/Simple_Indoor_Tagging):
  sparse, mostly airports, malls and stations.
- Floor-plan datasets: RPLAN, CubiCasa5K (https://github.com/CubiCasa/CubiCasa5k), Structured3D
  (https://structured3d-dataset.org), MSD Swiss Dwellings (*verify current URL*). Good for priors
  on room sizes and adjacencies, and for validation.

---

## 6. Recommendations

1. Two-stage generation: **program graph** (rooms, areas, adjacency, typology rules or Bayesian
   prior) → **geometric layout** (rule/optimisation solver in the footprint, aligned to
   structural grid and facade bays so that exterior windows land in rooms sensibly).
2. Facade and interior must **share opening entities**. The facade grammar proposes window bays.
   The interior layout either respects them (room boundaries fall on mullions/piers) or the
   facade is regenerated from the plan. Pick one direction per typology: residential is
   plan-driven, curtain-wall office is facade-driven.
3. Represent rooms as 3-cells in a cellular B-rep; walls are shared faces with thickness either
   explicit (two parallel faces with a solid between) or as an attribute on a single mid-surface
   face. Mid-surface + thickness is lighter and fine for acoustics at LOD < 3.
4. For acoustics, always compute and store per-room **volume, surface area, mean absorption per
   octave band**, and the **openings to the exterior**. These feed statistical
   (Sabine/Eyring) coupling even when geometry is coarse.
5. Use Infinigen Indoors' constraint DSL as the main design reference for object arrangement.

---

## References

- Merrell, Schkufza, Koltun, "Computer-generated residential building layouts", SIGGRAPH Asia 2010 — https://doi.org/10.1145/1882261.1866203
- Marson & Musse, "Automatic real-time generation of floor plans based on squarified treemaps algorithm", 2010 — https://doi.org/10.1155/2010/624817
- Bao et al., "Generating and exploring good building layouts", SIGGRAPH 2013 — https://doi.org/10.1145/2461912.2461977
- Peng, Yang, Wonka, "Computing layouts with deformable templates", SIGGRAPH 2014 — https://doi.org/10.1145/2601097.2601164
- Wu et al., "Data-driven interior plan generation for residential buildings" (RPLAN), SIGGRAPH Asia 2019 — https://doi.org/10.1145/3355089.3356556
- Hu et al., "Graph2Plan", SIGGRAPH 2020 — https://arxiv.org/abs/2004.13204
- Nauata et al., House-GAN (2020) — https://arxiv.org/abs/2003.06988 ; House-GAN++ (2021) — https://arxiv.org/abs/2103.02574
- Shabani et al., "HouseDiffusion", CVPR 2023 — https://arxiv.org/abs/2211.13287
- "Computer-Aided Layout Generation for Building Design: A Review", CVM 2025 — https://arxiv.org/abs/2504.09694
- Merrell et al., "Interactive furniture layout using interior design guidelines", SIGGRAPH 2011 — https://doi.org/10.1145/2010324.1964982
- Yu et al., "Make it home", SIGGRAPH 2011 — https://doi.org/10.1145/2010324.1964981
- Deitke et al., "ProcTHOR", NeurIPS 2022 — https://arxiv.org/abs/2206.06994
- Raistrick et al., "Infinigen Indoors", CVPR 2024 — https://arxiv.org/abs/2406.11824
- Yang et al., "Holodeck", CVPR 2024 — https://arxiv.org/abs/2312.09067
- OSM Simple Indoor Tagging — https://wiki.openstreetmap.org/wiki/Simple_Indoor_Tagging
- CubiCasa5K — https://github.com/CubiCasa/CubiCasa5k ; Structured3D — https://structured3d-dataset.org
