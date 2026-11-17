# Tessellation and multi-LOD output

Part of [CAD kernels, representations and robustness](README.md).

UberTopo's geometry is consumed by several simulators that want **different**
meshes of the same world. This file covers:

- what each consumer needs,
- how to tessellate without cracks,
- how to organise LOD so the levels are *consistent* with each other, which
  matters when results from different sensors are fused.

---

## 1. Consumers and their requirements

| Consumer | Geometric need | Detail scale | Watertight? | Edges? | Attributes |
|---|---|---|---|---|---|
| **Acoustic propagation** (geometric acoustics: image sources, ray/beam tracing; plus edge diffraction) | Coarse, closed, few large faces | Detail ≳ λ/2…λ/4 of the band of interest | **Yes**: leaks corrupt energy | **Yes**: explicit diffracting wedges with dihedral angles | absorption/scattering coefficients per octave band, impedance, transmission loss |
| **Acoustic (wave-based, low freq)**: FDTD/BEM/FEM | Volumetric grid or surface mesh at ≈ λ/6–λ/10 spacing | coarse | yes | implicit | impedance |
| **Camera (EO/IR) rendering** | Visual detail, normals, UVs | mm–cm near, m far | no (but no visible cracks) | sharp vs smooth edges for shading | albedo, roughness, emissivity, temperature |
| **Line-of-sight / vulnerability** | Fast ray queries, conservative occluders | 10 cm – 1 m | preferred | no | occluder class (opaque/partial/vegetation) |
| **Drone flight / collision** | Clearance distance, thin obstacles (wires) | cm–m | n/a | no | hazard class |
| **Radar/RF (future)** | Smooth curvature, specular facets, edges/wedges | ≈ λ/10 | yes | yes (UTD wedges) | permittivity, conductivity |

### 1.1 Acoustic scale

Speed of sound is c ≈ 343 m/s.

| f | λ | Comment |
|---|---|---|
| 50 Hz | 6.9 m | large drone blade-pass fundamentals, ground effect |
| 200 Hz | 1.7 m | typical small-quad blade-pass region *(order of magnitude)* |
| 1 kHz | 34 cm | |
| 4 kHz | 8.6 cm | motor/whine harmonics |
| 10 kHz | 3.4 cm | strongly attenuated over distance |

Room-acoustics research has long used a *set of models of the same room at
graded LOD*, with the number of models tied to the frequency resolution
([Pelzer, Maempel, Vorländer 2010](https://www.simpk.de/fileadmin/user_upload_sim/Dokumente/Forschung/Themen/Abteilung_III/Publikationen/Pelzer_Maempel_Vorlaender_2010_Room_Modeling_for_Acoustic_Simulation_and_Auralization_Tasks_-_Resolution_of_Structural_Details.pdf)).
Several guidelines follow from this literature:

- Model only detail on the order of the acoustic wavelength.
- Simplification should preserve **volume and absorption area**
  ([arXiv 2411.10375](https://arxiv.org/html/2411.10375v1)).
- Siltanen proposed **topology simplification followed by surface
  simplification**, because generic decimation is not tailored to geometric
  acoustics. This is the Aalto geometry-reduction work, Siltanen, Lokki et
  al. ([PDF](https://mediatech.aalto.fi/~ktlokki/Publs/59geom.pdf)). I took it
  from search excerpts and did not read the full text.
- Small details are better represented as **scattering coefficients** on a
  larger face than as geometry.

These are indoor studies. Outdoor urban propagation is dominated by
reflections from façades and ground, diffraction over roofs and around
corners, and atmospheric effects. That makes **explicit edges** (building
corners, roof ridges, parapets, barrier tops) essential. Geometric acoustics
add diffraction via edge models such as the Biot–Tolstoy–Medwin model or the
UTD. Both need each wedge's location, length and exterior angle *(prior
knowledge)*. The **polyhedral B-rep with explicit edges** is the natural
source for this. A triangle soup is not: diffracting edges would have to be
re-detected from dihedral angles, and coplanar triangulation edges filtered
out.

**Implication.** The acoustic LOD must be a *polyhedral B-rep-derived* model
that keeps true feature edges and merges coplanar facets into large polygons.
It should not be a decimated render mesh.

## 2. Crack-free tessellation

Cracks come from adjacent faces discretising their shared boundary
differently. Standard techniques:

1. **Edge-first tessellation** (the standard B-rep mesher approach, e.g. OCCT
   `BRepMesh`). Discretise every edge **once**, using the max of the
   requirements of all adjacent faces. Then tessellate each face with its
   boundary constrained to exactly those edge vertices (a 2D CDT in the face
   or parameter domain). Shared vertices are shared by index, which guarantees
   watertightness topologically.
   - In UberTopo's non-manifold B-rep, an edge may have more than two faces.
     The rule is the same: the edge owns its discretisation.
   - For edges that carry an analytic carrier (a clothoid alignment), the edge
     discretisation is the curve tessellation from the transcendental-curve
     track, driven by the LOD's chordal tolerance.
2. **T-junction elimination.** When an LOD transition puts a fine edge next to
   a coarse one, insert the fine vertices into the coarse side's
   triangulation. Never leave a T-vertex: T-junctions crack under float
   rasterisation and leak acoustic rays.
3. **Skirts / flanges** (terrain LOD practice) hide cracks visually by
   extending tile edges downward. This is acceptable for **render only**,
   never for acoustics or LOS, because it adds fake geometry.
4. **Locked boundaries in simplification.** Nanite builds a cluster DAG by
   grouping clusters, simplifying each group with its *outer boundary locked*,
   and re-splitting. Group boundaries change at each level, so no boundary
   stays locked forever. The runtime cut through the DAG is provably crack-free
   because sibling clusters make identical LOD decisions
   ([Karis et al., SIGGRAPH 2021 summary](https://cs418.cs.illinois.edu/website/text/nanite.html),
   [Beyond3D discussion](https://forum.beyond3d.com/goto/post?id=2367998)).
   This is the state of the art for visual LOD of huge static meshes.
5. **Deterministic tessellation.** The same face, edge and LOD must always give
   the same triangles. Otherwise caches and multi-machine simulation diverge.
   That rules out unordered parallel reductions in mesh generation.

## 3. LOD organisation

### 3.1 Semantic LOD (what exists) vs geometric LOD (how finely)

These are two orthogonal axes:

- **Semantic LOD** is CityGML-style. CityGML 3.0 defines LoD0–LoD3 for both
  exterior and interior: footprint or roof outline → block → roof shapes →
  façade detail with openings ([OGC CityGML 3.0 user guide](https://docs.ogc.org/guides/20-066.pdf),
  [GIM news](https://www.gim-international.com/content/news/citygml-3-0-conceptual-model-approved-as-official-ogc-standard)).
  In UberTopo this maps to **procedural rule depth**: which rules fire. For
  example, LoD2 = massing plus roofs, and LoD3 = plus openings, balconies and
  façade relief.
- **Geometric LOD** is the chordal or angular error of curved-surface
  tessellation, plus simplification error.

The procedural graph should take both as evaluation inputs:
`evaluate(region, semantic_lod, geometric_tolerance, consumer_profile)`.

### 3.2 Consistency between LODs

Fused simulations (acoustic plus camera plus LOS) must not contradict each
other. For example, a microphone must not be "inside" a building in the
acoustic model but outside it in the visual model. Possible guarantees:

| Guarantee | Meaning | Use |
|---|---|---|
| **Nested / conservative-outer** | Coarse ⊇ fine (as solids) | Collision, clearance, "is this point free?" |
| **Conservative-inner** | Coarse ⊆ fine | LOS "definitely blocked" tests |
| **Volume-preserving** | Equal volume / absorption area | Acoustics |
| **Best-fit** | Minimise Hausdorff distance | Rendering |

The kernel should record which guarantee each generated LOD satisfies, and
consumers should declare which one they need. Two ways to get conservative
outer hulls:

- Alpha wrapping with an offset gives an outer envelope at offset δ
  ([CGAL Alpha_wrap_3](https://doc.cgal.org/latest/Alpha_wrap_3/)).
- For planar buildings, plane-based simplification that pushes planes outward
  and drops small features.

### 3.3 Hierarchical spatial organisation

- **HLOD / tiles.** Group distant objects into merged proxies per tile, as in
  3D Tiles or game HLOD. Use the same tiling as the exact fixed-point frames
  (see [robustness §11](robustness-and-exact-arithmetic.md)), so geometry and
  LOD tiles coincide.
- **Terrain.** Use quadtree tiles of an adaptive TIN, or a regular grid with
  geomorphing (CDLOD-style). Breaklines (road edges, shorelines, building
  footprints) must be present at **every** LOD where their feature exists.
  Otherwise roads float above or sink into terrain. Make breakline
  simplification part of the same arrangement, so road and terrain LODs are
  generated together.
- **Vegetation.** Use instance LOD (mesh → billboard/impostor → volumetric
  density) rather than geometric simplification. For acoustics and LOS,
  forests are a **volume** with an attenuation or density field (VDB) plus a
  canopy envelope surface.

## 4. Simplification algorithms

- **Quadric error metrics** (Garland & Heckbert 1997) are the default for
  triangle meshes. Add attribute-aware variants (Hoppe 1999) to preserve UV
  and material seams *(prior knowledge)*.
- **Plane-based / structure-aware simplification** suits buildings: merge
  near-coplanar faces into planes, drop features below threshold, re-intersect
  the planes. This produces exact, large planar faces, which is ideal for
  acoustics. It is natural in the plane-based exact representation.
- **Procedural simplification.** The most effective simplifier for generated
  content is to **not generate** the detail. Re-evaluate the rules at a lower
  semantic LOD. This gives clean coarse geometry, which decimation cannot.
- **Volumetric simplification.** Voxelise or SDF at a coarse resolution, then
  extract with dual contouring. Robust to any input, including scans. It loses
  thin features and must be followed by plane fitting for acoustic-quality
  faces.
- **Alpha wrapping** for scanned or dirty input: a watertight 2-manifold with
  controllable detail (α) and offset
  ([Portaneri et al. 2022](https://history.siggraph.org/?p=128496)).

## 5. Adaptive tessellation per consumer: a profile system

Define **consumer profiles** that parameterise evaluation and tessellation:

```
profile acoustic_outdoor_200Hz_4kHz:
  semantic_lod: 2            # massing + roofs, no window recesses
  min_feature: 0.25 m        # ~ λ/4 at 343 Hz; smaller → scattering coeff
  curve_chord_tol: 0.10 m
  merge_coplanar: true       # big polygons, true feature edges only
  watertight: required
  edges: export_diffracting(min_length=0.5 m, max_exterior_angle=...)
  vegetation: volume(attenuation_field)
  guarantee: volume_preserving

profile camera_near (≤ 200 m):
  semantic_lod: 3
  curve_chord_tol: 2 mm
  bevels: visual
  vegetation: instanced_meshes
  guarantee: best_fit

profile los_conservative:
  semantic_lod: 2
  guarantee: conservative_inner   # never report blocked when it is not
  thin_obstacles: capsule_chains  # wires, masts
```

Profiles are data. Adding a radar profile later does not touch the kernel.

## 6. Output formats

- **Render.** glTF 2.0 with extensions, OpenUSD, and 3D Tiles for streaming.
  Manifold has published a glTF extension that preserves manifoldness through
  merge vectors ([Manifold wiki](https://github.com/elalish/manifold/wiki/Manifold-Library)).
- **Acoustic.** Polygonal formats with per-face material and explicit edge
  lists. Many acoustic tools ingest simple polygon formats (e.g. `.ac`, `.obj`
  with material groups). UberTopo should export polygons with holes
  triangulated *only if required*, plus a separate diffracting-edge list.
- **Analysis.** Native BVH / binary caches.

## 7. Recommendations

1. Tessellation is **edge-first**: shared edges own their discretisation, faces
   are CDT'd against it, and results are deterministic.
2. LOD is primarily **procedural re-evaluation** (semantic LOD), with geometric
   tolerance as a second axis. Decimation is a fallback for imported or scanned
   assets.
3. Every LOD declares a **consistency guarantee**. Consumers request
   guarantees, not just "LOD 2".
4. The **acoustic model is derived from the polyhedral B-rep** (merged
   coplanar faces, true feature edges, explicit diffracting-edge export), not
   from render meshes.
5. Visual LOD for huge scenes uses a **Nanite-style cluster DAG**, generated
   offline from the finest visual evaluation. This can be borrowed: the
   meshoptimizer library implements the building blocks *(prior knowledge:
   https://github.com/zeux/meshoptimizer)*.
6. Terrain, roads and footprints share **one 2D arrangement per tile and LOD**,
   so breaklines are consistent.
7. Vegetation and other statistical content are **volumes or instances**, never
   Booleaned or decimated.

## References

- Pelzer, Maempel, Vorländer 2010, Room modeling for acoustic simulation – resolution of structural details: https://www.simpk.de/fileadmin/user_upload_sim/Dokumente/Forschung/Themen/Abteilung_III/Publikationen/Pelzer_Maempel_Vorlaender_2010_Room_Modeling_for_Acoustic_Simulation_and_Auralization_Tasks_-_Resolution_of_Structural_Details.pdf
- Perceptual implications of simplifying GA models (2024): https://arxiv.org/html/2411.10375v1
- Pelzer 2011 quality assessment: https://www2.users.ak.tu-berlin.de/akgroup/ak_pub/seacen/2011/Pelzer_2011_P2_Quality_assessment_of_room_acoustic_simulation.pdf
- Lokki et al., geometry in acoustic modelling: https://mediatech.aalto.fi/~ktlokki/Publs/59geom.pdf
- Nanite overview (UIUC CS418): https://cs418.cs.illinois.edu/website/text/nanite.html ; Beyond3D: https://forum.beyond3d.com/goto/post?id=2367998
- CityGML 3.0 user guide: https://docs.ogc.org/guides/20-066.pdf ; GIM: https://www.gim-international.com/content/news/citygml-3-0-conceptual-model-approved-as-official-ogc-standard
- CGAL Alpha_wrap_3: https://doc.cgal.org/latest/Alpha_wrap_3/ ; paper: https://history.siggraph.org/?p=128496
- Manifold glTF extension (wiki): https://github.com/elalish/manifold/wiki/Manifold-Library
- Garland & Heckbert, Surface simplification using quadric error metrics, SIGGRAPH 1997 (prior knowledge): https://doi.org/10.1145/258734.258849
- meshoptimizer (prior knowledge): https://github.com/zeux/meshoptimizer
