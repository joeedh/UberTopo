# Building generation: massing, roofs, facades, openings, typologies

Part of the [environment-generation research set](README.md). Interiors and floor plans are in
[interiors.md](interiors.md); data ingestion for footprints and LiDAR in
[real-world-data-sources.md](real-world-data-sources.md); acoustic and camera property tables in
[sensing-properties-and-lod.md](sensing-properties-and-lod.md).

Research status: written November 2026 from web searches plus prior knowledge. Paper
citations are well known and were checked where noted; claims marked *(verify)* were not
confirmed against the primary source.

---

## 1. What the drone-defense use case needs from buildings

Buildings matter to us in three ways:

1. **Acoustic reflectors and diffractors.** Building faces are the main specular reflectors in
   urban sound propagation. Building edges (roof eaves, corners, parapets) are the main
   diffraction sources. Drone rotor noise has its blade-passing fundamental at about 100–600 Hz,
   with harmonics into the low kHz range, so wavelengths run from about 0.1 m to 3 m. Facade relief
   finer than about 0.1 m acts as *scattering roughness*, not as geometry
   (see [sensing-properties-and-lod.md](sensing-properties-and-lod.md)). Openings such as open
   windows, courtyards, arcades and passages create coupled spaces and leakage paths.
2. **Visual occluders and backgrounds.** Cameras need correct silhouettes, glazing that reflects
   and transmits (window reflections of a drone are false targets), and a correct sky/building
   boundary. Detection is easiest against sky and hardest against facades.
3. **Vulnerability-survey objects.** Rooftops are drone landing, launch and loiter sites. We need
   roof access points (stair penthouses, hatches, fire escapes), parapets that give cover, HVAC
   and antenna clutter, and courtyards and light-wells hidden from street-level sensors. Sensor
   placement also needs mounting points (roof edges, poles, facades) with power and line of sight.

So a building generator must output **semantic B-rep** (faces tagged wall/roof/ground/window/door,
with material and opening state) at **several LODs that stay topologically consistent**, so that
one window is the same entity in LOD3 and in LOD4.

---

## 2. LOD ladder (CityGML-aligned)

Use the CityGML LOD concept with the refined LODs of Biljecki, Ledoux & Stoter (2016). In that
scheme each LOD *x* splits into *x.0–x.3*:

| LOD | Content | Typical use here |
|---|---|---|
| 0.x | Footprint / roof-edge polygon | 2D planning, coarse RF/visibility |
| 1.x | Prismatic block (flat top), optional parts | Long-range acoustics (> 500 m), large-area LOS |
| 2.0–2.3 | Roof shapes; 2.2 adds dormers and superstructures; 2.3 adds overhangs | Default urban acoustics, rooftop survey |
| 3.x | Openings (windows/doors) as real holes, balconies, recesses | Camera simulation, near-field acoustics, entry points |
| 4 / interior (CityGML 3.0 drops LOD4 and allows interiors at any LOD) | Rooms, stairs, interior openings | Indoor acoustic coupling, interior vulnerability |

The kernel needs *faces with holes* for LOD3 (window openings in wall faces). Windows also need
**non-manifold attachments**: a window pane face shares its boundary loop with the wall hole, and
an open window leaves the loop unfilled. This fits the project's non-manifold B-rep requirement.

References: Biljecki, Ledoux, Stoter, "An improved LOD specification for 3D building models",
CEUS 2016 — https://3d.bk.tudelft.nl/hledoux/pdfs/16_ceus_lod_specs.pdf ; CityGML 3.0 —
https://www.ogc.org/standards/citygml/

---

## 3. Massing

### 3.1 Footprint-driven massing (the common case)
Most real buildings come from a 2D footprint (OSM, Overture, Microsoft/Google, cadastre) plus a
height or level count. Massing steps:

1. **Clean the footprint.** Snap near-collinear vertices, square off near-90° corners
   (orthogonalisation), remove slivers, and enforce CCW outer / CW holes. ML footprints are often
   noisy and need regularisation (Douglas–Peucker plus a dominant-direction snap).
2. **Split into parts.** Use OSM `building:part`; when parts are absent, infer them by
   decomposing the footprint into near-rectangles (maximal rectangle cover or a skeleton-based
   split). Each part gets its own height and roof.
3. **Extrude to LOD1** using `height`, `building:levels × storey height` (a regional prior of
   about 2.7–3.2 m residential, 3.5–4.5 m commercial ground floor, 6–12 m industrial), or a
   satellite-derived height (Google 2.5D, GlobalBuildingAtlas).

### 3.2 Generative massing (no footprint available)
For synthetic districts:

- **Shape grammars / CGA**: lot → setback → extrude → split into podium, tower and crown.
  Esri CityEngine's CGA is the de facto industrial tool (Müller et al. 2006); CGA++ (Schwarz &
  Müller 2015) adds shape-to-shape queries and events.
- **Zoning envelope**: FAR (floor area ratio), setbacks, height planes and sky-exposure planes
  (as in NYC zoning) define a max envelope. Massing is then a Boolean or offset of that envelope.
  This is the right way to generate *plausible* high-rise forms per city.
- **Typology templates** (Section 7): parameterised archetypes (courtyard block, slab, point
  tower, shed, compound) placed on parcels from [urban-layout.md](urban-layout.md).

---

## 4. Roofs

### 4.1 Straight skeleton (the workhorse)
The straight skeleton of a polygon (Aichholzer, Aurenhammer, Alberts & Gärtner 1995) comes from
shrinking all edges inward at unit speed. Lifted into 3D, the faces traced by the edges form a
**hip roof** with every slope equal. Most procedural roof generators use it:

- Hip roof = skeleton faces at a constant pitch.
- Gable roof = set the weight (speed) of selected short edges to 0, which makes them vertical
  gable walls.
- Mansard and gambrel = two-stage extrusion: offset to height h1 at pitch p1, then continue at p2.
- Flat roof with parapet = inward offset (the skeleton's offset curve) plus an extruded ring.
- Shed / skillion = a single plane. No skeleton needed.

**Weighted straight skeletons** give each edge its own speed, so each roof plane can have its own
pitch. Edge weight = cot(pitch angle). Watch out: weighted skeletons have degenerate and
non-unique cases (Biedl, Held, Huber, Kaaser, Palfrader 2015, "Weighted straight skeletons in the
plane"). Negative weights and multiple simultaneous events need careful handling.

**Implementations**
- CGAL `Straight_skeleton_2` supports weighted skeletons and `extrude_skeleton()` for polygons
  with holes from CGAL 6.0. Weights may be 0 (vertical edge) and negative (exterior extrusion). The
  output is a closed 2-manifold triangle mesh. This is the most robust open implementation; it uses
  exact predicates. https://doc.cgal.org/6.0/Straight_skeleton_2/index.html ,
  https://www.cgal.org/2023/05/09/improved_straight_skeleton
- Tom Kelly's `campskeleton` (Java) implements the procedural-extrusions model, including
  edge-direction changes during the sweep: https://github.com/twak/campskeleton
- Held & Huber's STALGO / "Surfer" is fast and robust, using wavefront propagation with motorcycle
  graphs.
- Felkel–Obdržálek (1998) is widely copied and has known failures on reflex-vertex events. Avoid
  naïve ports.

**Skeptic's note:** generic straight-skeleton code (e.g. many Blender add-ons and JS ports) fails
on real footprints with near-parallel edges, collinear vertices and holes. Use exact or filtered
predicates, pre-clean the footprint, and treat the skeleton as a *planar graph with numerical
tolerance*, not as floats.

### 4.2 Procedural extrusions (Kelly & Wonka 2011)
"Interactive architectural modeling with procedural extrusions" (ACM TOG 30(2)) generalises the
weighted skeleton. Each footprint edge carries a **profile** (a polyline in the edge's vertical
plane), and the sweep follows the profile while handling topological events (edge collisions,
splits). **Anchors** attach features such as dormers, chimneys and windows to the profile at a
given height. One mechanism generates roofs, cornices, setbacks, overhangs (negative-weight
segments) and whole building shells from a 2D plan. This is a strong model for UberTopo:

- It produces a coherent B-rep (faces, edges, vertices with known provenance: "this face came from
  footprint edge 7, profile segment 2"). That gives stable semantic tagging and stable IDs across
  regenerations.
- Profiles are 1D curves, which matches the kernel's embedded-curve requirement.
- Kelly's later "SitePlan" and "chordatlas" reuse the same machinery:
  https://github.com/twak/chordatlas

Reference: https://doi.org/10.1145/1944846.1944854 ; project page
http://twak.org/ (*verify URL*)

### 4.3 Roof superstructures and rooftop clutter (LOD2.2+)
Dormers, chimneys, skylights, stair/elevator penthouses, water tanks (NYC), HVAC units, solar
panels, satellite dishes, antennas and lightning rods. For **vulnerability surveys** these are
first-class:
- **Rooftop access** (penthouse door, roof hatch, external ladder, fire escape) decides whether a
  drone operator or a counter-drone team can reach the roof.
- **Cover**: parapets (height 0.3–1.2 m), equipment screens, tanks.
- **Landing surfaces**: flat, unobstructed areas of at least about 2×2 m.

Generate these with rule-based placement on roof faces: an inset polygon, Poisson-disk placement,
and type priors by building class. Commercial roofs carry HVAC on a grid near the core. Residential
pitched roofs get chimneys near ridge lines.

---

## 5. Facades

### 5.1 Split grammars
- **Wonka, Wimmer, Sillion, Ribarsky 2003, "Instant Architecture"**
  (https://doi.org/10.1145/882262.882324) introduced split grammars with a control grammar for
  attribute propagation.
- **Müller, Wonka, Haegler, Ulmer, Van Gool 2006, "Procedural Modeling of Buildings"**
  (https://doi.org/10.1145/1141911.1141931) introduced CGA Shape: scope-based rules
  (`split(y){ floor_h : Floor }*`, `repeat`, `comp(f)`) and occlusion queries. This became
  CityEngine.
- **CGA++** (Schwarz & Müller 2015, https://doi.org/10.1145/2766956) added shapes as first-class
  values, global context and events. These matter for coordinating features across facades, e.g.
  aligning floors around corners.
- **Esri PyPRT** exposes the CGA runtime to Python: https://github.com/Esri/pyprt (proprietary
  runtime, open bindings).

Typical facade decomposition: facade → floors (ground, typical, top/attic) → tiles (bays) →
window/wall/door. Each terminal is either geometry (an inset hole with a frame, sill and lintel)
or a parametric asset.

### 5.2 Inverse and example-based facades
- Facade parsing from images (Teboul et al. 2011, Martinović et al. 2012) derives split grammars
  from photos.
- **BigSUR** (Kelly et al. 2017, https://doi.org/10.1145/3130800.3130823) fits structured
  building models (procedural extrusions plus facade grammar) to noisy photogrammetry, street-view
  imagery and GIS footprints. This is the most relevant "real data + procedural" template for us.
- **FrankenGAN** (Kelly et al. 2018, https://arxiv.org/abs/1806.07179) uses GANs to produce facade
  and roof *layouts and textures* for coarse masses. Useful as a style source, but the geometry is
  still procedural.
- Nishida et al. 2016, "Interactive sketching of urban procedural models"
  (https://doi.org/10.1145/2897824.2925951): a CNN selects grammar snippets and parameters from
  sketches. This is the "ML picks parameters, grammar builds geometry" pattern, which is the
  pragmatic way to use ML.

### 5.3 Openings (windows, doors) — acoustics and camera critical
Model openings as **real holes in wall faces** (inner loops) with these attributes:
- `opening_type` (window, door, vent, garage door, loading dock, louvre), `frame_depth` (reveal,
  usually 0.1–0.3 m; it creates the acoustic cavity and visual shadow), `sill_height`,
  `glazing` (single/double/triple, laminated; the pane's transmission loss is about 25–40 dB),
  `state` (closed / open fraction / broken / missing), `operable` (bool), and `blinds/curtains`.
- **Window-to-wall ratio (WWR)** by typology and orientation is the main statistical prior:
  residential 15–30%, office curtain wall 40–90%, warehouses <5% plus clerestory. Building-energy
  datasets (e.g. US DOE commercial reference buildings, EU TABULA typologies) provide WWR
  distributions.
- Camera: glazing needs a Fresnel-reflective material plus transmission and an interior "backdrop"
  (an interior-mapping shader, or real interiors at LOD4). Curtain walls act as mirrors and produce
  drone *ghost images*.
- Acoustic: closed glazing behaves like a rigid reflector at most drone frequencies, with
  mass-spring resonance in the 100–300 Hz range for double glazing. Open windows couple to interior
  volumes. An open window is a hole in the reflector **and** a resonator.

### 5.4 Balconies, arcades, colonnades, overhangs
These are LOD3 elements, and they matter: balconies give cover and create flutter echoes;
arcades and colonnades create semi-enclosed passages; roof overhangs (LOD2.3) shadow facades from
aerial views. Procedural extrusions with negative-weight segments handle overhangs. Balconies are
anchored components on floor splits.

---

## 6. Stairs, ramps, fire escapes, elevators (vertical circulation)

Vertical circulation decides roof and floor access, so it matters for vulnerability analysis.
- Parameterise stairs by building code: riser ≤ about 178–196 mm, tread ≥ about 250–280 mm
  (International Building Code; national variants), the 2R + T ≈ 600–640 mm comfort rule
  (Blondel), and landings every ≤ 3.7 m rise. Types: straight, L, U (dog-leg), scissor, spiral.
- Place stair cores by egress rules: max travel distance (often 30–76 m depending on occupancy and
  sprinklers), two remote exits above certain occupancies, and the core extending to the roof as
  a penthouse in mid- and high-rise buildings.
- External fire escapes (historic NYC, older US cities) are rooftop access paths and visual
  clutter.
- See [interiors.md](interiors.md) for placing cores in the plan.

---

## 7. Typologies (by function and region)

Each typology is a parameterised archetype: massing rule + roof rule + facade grammar + interior
template + material palette + attribute priors (WWR, storey height, parapet).

| Typology | Massing / roof | Facade / openings | Drone-defense relevance |
|---|---|---|---|
| Detached house (N. America, timber frame) | Gable/hip, attached garage, 1–2 storeys | 15–25% WWR, siding | Yard launch sites, low cover |
| Terraced / row house (UK, NL, US east) | Party walls, gable fronts, back extensions | Bay windows, chimneys | Continuous street canyons |
| Perimeter block (Europe, Barcelona, Berlin) | Courtyard block, mansard, 4–7 storeys | Regular bays, balconies | Courtyards hidden from street sensors |
| Mid/high-rise residential slab or tower (Asia, ex-Soviet) | Prismatic, flat roof, penthouse | Repetitive windows, balconies, AC units | Strong reflectors; many roof sites |
| Commercial high-rise | Podium + tower + crown, setbacks per zoning | Curtain wall (mirror glazing) | Visual ghosting; acoustically smooth |
| Warehouse / logistics | Large-span portal frame, low-pitch metal roof, loading docks | Very low WWR, roller doors, skylights | Very large flat roofs; metal is a strong reflector |
| Industrial plant | Sheds + tanks + stacks + pipe racks + conveyors | Open structures | See [infrastructure.md](infrastructure.md) |
| Religious | Nave/dome/minaret/spire/pagoda; courtyards | Large openings, stained glass | Tall landmarks used for sensor mounts |
| Military / government compound | Perimeter wall/fence, setback standoff zone, gatehouse, multiple low buildings, hardened shelters | Few or blast-resistant openings | Standoff distance per UFC 4-010-01; perimeter is the sensor ring |
| Informal settlement | Very dense, irregular small units (10–40 m² footprint), corrugated metal flat/mono-pitch roofs, narrow alleys (1–3 m), organic incremental vertical growth | Irregular openings | Hard for LOS; metal roofs are reflective; data mostly from ML footprints |
| Historic / vernacular | Region-specific (East Asian bracket roofs, Mediterranean flat roofs, timber frame) | Thick masonry, small openings | Thick walls; towers and walls of forts |

Notes:
- **Military/government compounds**: the US DoD *Minimum Antiterrorism Standards for Buildings*
  (UFC 4-010-01) set standoff distances, controlled perimeters and parking exclusion zones. These
  are good generative constraints for plausible compound layouts.
  https://www.wbdg.org/ffc/dod/unified-facilities-criteria-ufc/ufc-4-010-01
- **Informal settlements**: the procedural literature here is thin. A workable approach is an
  agent-based incremental growth model on irregular parcels (accretion along paths, vertical
  additions over time), calibrated against Google Open Buildings / Microsoft footprints, which
  cover much of the Global South. The Google Open Buildings 2.5D Temporal dataset has heights for
  2016–2023 at about 4 m effective resolution with about 1.5 m height MAE.
  https://sites.research.google/gr/open-buildings/temporal
- **Historic East Asian**: Teoh 2009, "Generalized descriptions for the procedural modeling of
  ancient East Asian buildings" (Computational Aesthetics) is a good example of encoding
  vernacular rules.
- **Regional typology data**: EU TABULA/EPISCOPE residential typologies
  (https://episcope.eu/building-typology/) give age bands × types × construction. EUBUCCO
  (https://eubucco.com) gives about 200M EU buildings with height, age and type where available.
  GlobalBuildingAtlas (TUM, ESSD 2025) has 2.75B footprints and LoD1 heights at 3 m resolution,
  with continent-level RMSE of 1.5–8.9 m (https://arxiv.org/abs/2506.04106).

**Recommendation:** treat a typology as a *data asset*, not code. Store a typology file
(JSON/TOML) with parameter distributions keyed by region and era, and a small grammar library it
references. Region priors come from the land-use and building-attribute data in
[real-world-data-sources.md](real-world-data-sources.md).

---

## 8. Materials and attributes to attach at generation time

Per face (or per face-region via attribute maps):
- `semantic`: GroundSurface / WallSurface / RoofSurface / ClosureSurface / OuterCeiling /
  OuterFloor / Window / Door (CityGML 3.0 thematic surfaces).
- `material_id` pointing to a material library with **acoustic** data (octave-band absorption α
  and scattering s, ISO 354/17497 style), **visual** data (PBR albedo, roughness, metalness,
  transmission, IOR) and **RF** data (optional permittivity, for radar/RF work later).
- `roughness_scale`: amplitude and correlation length of unmodelled relief (brick courses,
  mullions, corrugation). Acoustics derives a scattering coefficient from it.
- `construction`: masonry, concrete, timber, steel/metal clad, glass curtain wall. Drives
  transmission loss and the material priors.
- Provenance (`source`: OSM / ML / procedural / LiDAR) and confidence. This is essential when
  procedural fill-in sits next to measured data.

---

## 9. Pipeline recommendation for UberTopo

1. **Input**: footprint polygon with holes (real or generated) + attributes (height, levels, roof
   shape, typology, era).
2. **Massing**: part decomposition → LOD1 extrusions (kernel: extrude face with holes).
3. **Roof**: weighted straight skeleton / procedural-extrusion engine → LOD2.0–2.3 faces with
   provenance tags. Use exact predicates (CGAL Epick/Epeck or the project's own robust kernel).
4. **Facade**: split grammar on each wall face's local 2D frame → opening loops cut into wall
   faces (LOD3); components (balconies, frames) as attached solids or non-manifold sheets.
5. **Interior** (optional, LOD4): see [interiors.md](interiors.md); openings shared between facade
   and interior walls by ID.
6. **Rooftop clutter and access**: rule-based placement; emit vulnerability-survey annotations
   (access points, cover polygons, landing zones).
7. **Output**: semantic non-manifold B-rep; attributes per face; stable IDs derived from
   (input ID, rule path, split index) so that regeneration and LOD switching keep identity.

Key design implication: **grammar evaluation must be deterministic, seeded per entity, and
lazy/LOD-aware**. A building far from any sensor should never expand past LOD1/2. Expansion
happens on demand along the rule tree (see the procedural-architecture research by the other
agents).

---

## References

- Aichholzer, Aurenhammer, Alberts, Gärtner, "A novel type of skeleton for polygons", J.UCS 1995 — https://www.jucs.org/jucs_1_12/a_novel_type_of.html
- Eppstein & Erickson, "Raising roofs, crashing cycles, and playing pool", DCG 1999 — https://doi.org/10.1007/PL00009464
- Biedl et al., "Weighted straight skeletons in the plane", CGTA 2015 — https://doi.org/10.1016/j.comgeo.2014.08.006
- Kelly & Wonka, "Interactive architectural modeling with procedural extrusions", ACM TOG 30(2) 2011 — https://doi.org/10.1145/1944846.1944854
- campskeleton — https://github.com/twak/campskeleton ; chordatlas — https://github.com/twak/chordatlas
- CGAL 2D Straight Skeleton (6.0, weighted + extrusion) — https://doc.cgal.org/6.0/Straight_skeleton_2/index.html ; announcement — https://www.cgal.org/2023/05/09/improved_straight_skeleton
- Wonka et al., "Instant Architecture", SIGGRAPH 2003 — https://doi.org/10.1145/882262.882324
- Müller et al., "Procedural Modeling of Buildings", SIGGRAPH 2006 — https://doi.org/10.1145/1141911.1141931
- Schwarz & Müller, "Advanced procedural modeling of architecture" (CGA++), SIGGRAPH 2015 — https://doi.org/10.1145/2766956
- Esri PyPRT — https://github.com/Esri/pyprt
- Kelly et al., "BigSUR: Large-scale Structured Urban Reconstruction", SIGGRAPH Asia 2017 — https://doi.org/10.1145/3130800.3130823
- Kelly et al., "FrankenGAN", SIGGRAPH Asia 2018 — https://arxiv.org/abs/1806.07179
- Nishida et al., "Interactive sketching of urban procedural models", SIGGRAPH 2016 — https://doi.org/10.1145/2897824.2925951
- Biljecki, Ledoux, Stoter, "An improved LOD specification for 3D building models", CEUS 2016 — https://3d.bk.tudelft.nl/hledoux/pdfs/16_ceus_lod_specs.pdf
- OGC CityGML 3.0 — https://www.ogc.org/standards/citygml/
- UFC 4-010-01 DoD Minimum Antiterrorism Standards for Buildings — https://www.wbdg.org/ffc/dod/unified-facilities-criteria-ufc/ufc-4-010-01
- Google Open Buildings 2.5D Temporal — https://sites.research.google/gr/open-buildings/temporal
- GlobalBuildingAtlas (Zhu et al., ESSD 2025) — https://arxiv.org/abs/2506.04106 ; https://essd.copernicus.org/articles/17/6647/2025/
- EUBUCCO — https://eubucco.com ; TABULA/EPISCOPE — https://episcope.eu/building-typology/
- OSM Simple 3D Buildings — https://wiki.openstreetmap.org/wiki/Simple_3D_Buildings
