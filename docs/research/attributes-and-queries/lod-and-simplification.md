# Level of Detail, HLOD and Semantics-Preserving Simplification

Part of [attributes-and-queries](README.md). This file covers how to produce and
select multiple levels of detail while keeping semantic identity, attributes and
simulation fidelity. It also assesses whether meshlet/Nanite-style virtualised
geometry suits simulation queries. Tiling and streaming are in
[large-world-coordinates-and-tiling.md](large-world-coordinates-and-tiling.md).

## 1. Two kinds of LOD

1. **Representational (semantic) LOD.** *What* is modelled changes. CityGML LOD0
   (footprint/roof-edge polygons) → LOD1 (block) → LOD2 (roof shapes, semantic
   surfaces) → LOD3 (openings, facade detail). Interiors exist at LOD0–3 in CityGML 3.0.
   A tree goes from point → crown ellipsoid → trunk+crown volumes → branches →
   leaves. Biljecki, Ledoux & Stoter refined CityGML 2.0's 5 levels into 16 (LOD x.y)
   because "LOD2" was too ambiguous for analysis
   ([Biljecki et al. 2016, *An improved LOD specification for 3D building models*](https://doi.org/10.1016/j.compenvurbsys.2016.04.005);
   [OGC CityGML 3.0](https://docs.ogc.org/is/20-010/20-010.html)).
2. **Geometric LOD.** The *same* things are represented with fewer elements and
   bounded geometric error (mesh simplification, coarser tessellation tolerance).

A procedural modeller can produce representational LOD **directly** by running the
generator at a lower level, which beats simplifying detailed output: semantics
stay exact, openings disappear cleanly, and IDs are consistent by construction.
Geometric simplification is still needed for imported data (photogrammetry, scans,
CAD) and for the last steps of HLOD aggregation.

**Rendering LOD differs from simulation LOD.** Rendering selects by projected
screen-space error from a camera. Simulation must select by physical relevance
(wavelength, sensor resolution, a survey tolerance) and do so deterministically. See
§6.

## 2. Identity and attribute consistency across LODs

Requirements:

* A query at any LOD reports **semantic IDs that are meaningful at every LOD**:
  building B17 → facade N → (at LOD3) window W3.
* An attribute authored at one LOD (a survey annotation on a facade, a sensor
  mounted on a roof) resolves at other LODs.
* Coarse elements carry **aggregated attributes** of the fine detail they replace.

Recommended model:

* **Semantic persistent IDs** are emitted by generators (see
  [attribute-propagation.md](attribute-propagation.md) §3.7) at the granularity of
  semantic features: building, storey, facade, roof plane, opening, road, lane, tree.
  They are LOD-independent.
* **Element IDs** (faces, edges) are LOD-specific. Each element carries
  `sem.feature` (the most specific semantic feature it belongs to *at this LOD*) and
  `proc.sources` (the finer-LOD elements or features it represents).
* **Feature hierarchy table** (feature → parent feature, minimum LOD at which it is
  explicitly represented). A query hitting a facade face at LOD2 can then answer "this
  facade contains windows W1..W12, which are not represented geometrically at this
  LOD".
* **Aggregation of dropped features into attributes.** When windows vanish at LOD2,
  the facade gets derived attributes such as `opening.area_fraction`, glazing area,
  and *area-weighted mixtures* of absorption, transmission and IR emissivity. This is
  essential for acoustics: a facade's reflection and transmission depend heavily on
  its glazing fraction. The mixture rule is the merge policy from
  [attribute-propagation.md](attribute-propagation.md) §2 (intensive →
  area-weighted).

3D Tiles achieves the identity part with **feature-ID attributes/textures**
(`EXT_mesh_features`) that survive HLOD merging, so a merged proxy mesh still answers
"which feature is this texel/vertex?"
([Cesium, fine-grained metadata](https://cesium.com/blog/2022/05/31/fine-grained-metadata-in-3d-tiles-next)).
UberTopo should do the same for every simplified or merged representation: per-vertex
or per-face `sem.feature`, and for very coarse proxies a feature-ID texture.

## 3. HLOD: hierarchical aggregation

HLOD replaces groups of objects with merged proxies at coarse levels (Unreal HLOD,
Simplygon, 3D Tiles `REPLACE` refinement). For a site-frame quadtree (see
[large-world-coordinates-and-tiling.md](large-world-coordinates-and-tiling.md)):

* Leaf tiles hold full-detail objects (procedurally generated at the finest LOD
  required).
* Each parent tile holds a proxy of its children's content: merged, simplified,
  re-attributed (feature IDs preserved, materials mixed), with bounded geometric
  error.
* **Tile-seam edges are locked** during simplification, so adjacent tiles at the same
  level stay watertight (meshoptimizer exposes border locking for this reason:
  [meshoptimizer](https://github.com/zeux/meshoptimizer)).

Where HLOD fails for simulation:

* **Interiors vanish.** Acoustic simulation inside a building needs the interior
  regardless of camera distance, so HLOD must be *per population*: an exterior-shell
  HLOD for outdoor propagation and optical sensors, and interiors loaded only when a
  source or receiver is inside or a path enters through an opening.
* **Vegetation merges badly.** Merging tree meshes yields meaningless geometry for
  acoustics. Use volumetric proxies (§5.3).
* **Thin structures disappear.** Power lines and fences drop out of simplified meshes
  but matter for drones (collision, optical detection, RF). Keep them as **curve
  primitives** (see
  [spatial-acceleration-structures.md](spatial-acceleration-structures.md) §9) that do
  not participate in mesh simplification.

## 4. Simplification algorithms

| Method | Idea | Attributes | Notes |
|---|---|---|---|
| **QEM** (Garland & Heckbert 1997) | edge collapse minimising sum of squared distances to supporting planes | positions only | baseline, fast ([paper](https://www.cs.cmu.edu/~garland/Papers/quadrics.pdf)) |
| QEM + attributes (Garland & Heckbert 1998) | quadrics in (x, y, z, attrs) space | colours, normals, UVs | higher-dimensional quadric |
| **Hoppe 1999** new quadric metric | attribute error measured via 3D correspondence; wedges for discontinuities | yes, with seams | more accurate and cheaper; O(m) solve per Hoppe & Marschner 2000 ([project](https://hhoppe.com/proj/newqem/)) |
| Lindstrom–Turk 1998 | memoryless, volume/boundary preserving | — | CGAL's default policy ([CGAL Surface_mesh_simplification](https://doc.cgal.org/latest/Surface_mesh_simplification/index.html)) |
| **meshoptimizer** `simplify` / `simplifyWithAttributes` | QEM with attribute weights, border lock, per-vertex lock/protect flags, sloppy mode | yes | production quality, fast, MIT ([repo](https://github.com/zeux/meshoptimizer)) |
| Vertex clustering (Rossignac–Borrel 1993; Lindstrom 2000 out-of-core) | snap to grid cells | crude | topology-agnostic, handles soups, low quality |
| **Variational Shape Approximation** (Cohen-Steiner, Alliez, Desbrun 2004) | Lloyd clustering of faces into planar proxies, then remesh | per-proxy | produces **large planar facets**, ideal for image-source acoustics ([paper](https://doi.org/10.1145/1015706.1015817)) |
| Structure-aware decimation (Salinas, Lafarge, Alliez 2015) | detect planes, preserve their adjacency structure while decimating | — | for urban/photogrammetry meshes ([paper](https://doi.org/10.1111/cgf.12531)) |
| PolyFit (Nan & Wonka 2017) | select planar faces from candidate set via ILP into a watertight polyhedron | — | building proxies from noisy data ([project](https://3d.bk.tudelft.nl/liangliang/publications/2017/polyfit/polyfit.html)) |
| Progressive meshes (Hoppe 1996) | invertible collapse sequence | yes | view-dependent refinement, mostly historical |
| Cluster DAG (Nanite, meshoptimizer `clusterlod`) | simplify groups of clusters with locked group borders; build a DAG | yes (with attribute-aware simplify) | see §7 |

## 5. Feature-preserving simplification for simulation proxies

### 5.1 What acoustic simulation needs from geometry

Geometric acoustics (image sources, ray tracing) is valid only when surfaces are
large relative to wavelength. λ = c/f, with c ≈ 343 m/s:

| Octave band | 63 Hz | 125 Hz | 250 Hz | 500 Hz | 1 kHz | 2 kHz | 4 kHz | 8 kHz |
|---|---|---|---|---|---|---|---|---|
| λ | 5.4 m | 2.7 m | 1.4 m | 0.69 m | 0.34 m | 0.17 m | 8.6 cm | 4.3 cm |

Consequences:

* Detail much smaller than λ does not reflect specularly. It **scatters**. The
  established practice in room-acoustics simulation is to model geometry coarsely and
  represent sub-wavelength detail through the **scattering coefficient** (ISO
  17497-1) of the coarse surface (Vorländer, *Auralization*, Springer 2008/2020;
  [Vorländer 2020](https://doi.org/10.1007/978-3-030-51202-6)). Pelzer and Vorländer
  proposed **frequency-dependent geometry**: different simplification levels per band
  (Pelzer & Vorländer, *Frequency- and time-dependent geometry for real-time
  auralizations*, ICA 2010; cited from memory).
* Simplification for acoustics therefore needs a **transfer rule**: removed geometric
  detail (roughness, small protrusions, facade relief) increases the scattering
  coefficient of the surviving face. An attribute is computed from the removed
  geometry, e.g. RMS height deviation relative to λ. This is the acoustic analogue of
  normal-mapping in rendering.
* Drone noise is broadband with strong blade-passing tonals, typically 100 Hz to
  several kHz, so several band-specific proxies are needed, not one.

Constraints the acoustic simplifier must honour:

1. **Watertight regions.** No leaks between rooms or into closed volumes, because leaks
   cause false transmission paths. Validate with region-consistency checks
   (cell-complex point location vs winding number; see
   [query-types-and-api.md](query-types-and-api.md) §1.4).
2. **Per-region volume conservation.** Reverberation time scales with V/A (Sabine).
3. **Per-material surface area conservation.** Equivalent absorption area A = Σ αᵢ Sᵢ
   must be preserved per band. When faces of different materials merge, the
   merged face's α is the area-weighted mixture, not "dominant material".
4. **Openings preserved as portals** (windows, doors, vents), since they dominate
   transmission into and out of buildings, or aggregated into `opening.area_fraction`
   when dropped (§2).
5. **Diffracting edges** above a length threshold (∝ λ) and with significant wedge
   angle are preserved as edges, and their identity is carried across LODs for the
   edge visibility graph (see [query-types-and-api.md](query-types-and-api.md) §1.7).
6. **Planarity.** Image-source methods enumerate reflections per *planar polygon*.
   A highly tessellated curved surface yields exponentially many tiny image sources.
   VSA-style planar proxies or explicit plane merging are preferred, and the
   B-rep's exact planar faces are already ideal when available.
7. **Tile seams locked, and tile-seam edges excluded** from diffraction.

### 5.2 Optical/IR sensor proxies

* **Silhouette preservation** matters more than interior detail for detection and
  occlusion. Silhouette-aware simplification preserves edges with high dihedral angle
  and boundary edges.
* Small occluders (poles, wires, sign gantries) must survive, because they occlude
  drones in camera views. Treat them as curve/instance primitives outside mesh
  simplification.
* Thermal/IR: material and emissivity boundaries matter, so lock them like material
  boundaries.

### 5.3 Vegetation

Mesh simplification of trees is wrong for every sensor modality at a distance:

* **Acoustic:** forest canopies act as scattering/attenuating media (excess
  attenuation from leaves/branches, ground effect from soft forest floor). Use a
  **volumetric proxy** (VDB density + per-band attenuation/scattering attributes)
  plus **trunk cylinders** (specular/diffractive scatterers for higher frequencies).
* **Optical:** use stochastic opacity (density-based transmittance) at distance,
  instanced meshes up close, and impostors for rendering only (impostors are useless
  for queries).
* The generator (`GeneratorRef`, see
  [embedded-geometry-attributes.md](embedded-geometry-attributes.md) §7) produces
  each representation on demand from (species, age, seed), so all LODs are consistent.

## 6. LOD selection policy for simulation

Rendering-style view-dependent selection is unsuitable for simulation because:

* it depends on a camera that does not exist in a simulation;
* streaming state changes which LOD is resident, so results become irreproducible;
* **reciprocity** breaks: a path A→B and B→A should see the same geometry, but
  per-ray LOD selection would differ by origin.

Recommended policy: before a simulation run, define a **simulation LOD field** ε(x)
over space. Examples: fine near sensors, protected assets and drone paths, coarse
elsewhere, scaled per frequency band (e.g. ε ≈ λ/8). Then **fix one LOD cut per
tile for the run**. All queries in the run use that cut. The cut is part of the
`SceneVersion` that queries reference (see
[query-types-and-api.md](query-types-and-api.md) §2.2–2.5), so reruns reproduce it
exactly.

A two-pass scheme is cheap to support and validates the choice: run at the chosen
cut, rerun selected scenarios at the next finer cut, and report the difference
(e.g. TDOA residual changes or RT60 changes) as a fidelity estimate.

## 7. Meshlets, Nanite and virtualised geometry

### 7.1 How it works

Nanite (UE5), described in Karis, Stubbe & Wihlidal's SIGGRAPH 2021 deep dive:

* Meshes are split into **clusters** of ~128 triangles.
* Clusters are grouped. Each group is simplified to half its triangle count with
  **group boundaries locked**, then re-clustered. Repeating this builds a **DAG**
  (not a tree), so the locked boundaries change between levels and seams do not
  accumulate.
* At runtime, a **cut** through the DAG is chosen per cluster from the projected
  error (screen-space), with monotonic error bounds guaranteeing a crack-free result.
* Pages of clusters are **streamed**. Small triangles are rasterised in software (compute).
* Ray tracing in UE5 traditionally uses separate **fallback meshes** (a coarse proxy),
  so traces can disagree with what is rendered. Native Nanite RT is experimental
  (`r.RayTracing.Nanite.Mode`).
* [Karis et al. 2021, *Nanite: A Deep Dive*](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf).

Open implementations and building blocks: meshoptimizer's clusterisation
(`meshopt_buildMeshlets`) and **cluster LOD** example (`clusterlod`); Bevy's virtual
geometry; NVIDIA's `nv_cluster_lod_builder` with RTX Mega Geometry samples. On the RT
side, **cluster acceleration structures** (NVIDIA CLAS / DXR Clustered Geometry
preview) and AMD **DGF** make ray tracing *the selected cut* practical by rebuilding
BLASes from per-cluster structures cheaply
([NVIDIA RTX Mega Geometry](https://developer.nvidia.com/blog/nvidia-rtx-mega-geometry-now-available-with-new-vulkan-samples);
[DirectX CLAS/PTLAS preview](https://devblogs.microsoft.com/directx/clas-ptlas-preview/);
[AMD DGF](https://gpuopen.com/download/DGF.pdf)).

### 7.2 Does it suit simulation queries?

**Advantages:**

* Continuous LOD with **certified, monotone error bounds**: an ε-cut is well defined.
* Crack-free within a mesh, with streaming at cluster-page granularity.
* With cluster AS, building an RT BLAS for any cut is fast, so simulation can ray trace
  exactly the geometry selected.
* The cluster granularity (~64–256 triangles) matches the edit granularity of
  procedural regeneration well.

**Problems:**

* **Screen-space selection** is meaningless for simulation. The fix is to select the
  cut by the **world-space** ε(x) field from §6. The DAG structure supports this
  directly, since it only needs an error threshold per cluster.
* **Lossy semantics.** Cluster simplification must be attribute-aware (material
  boundaries locked or penalised, feature IDs carried). Default Nanite-style pipelines
  optimise for visual error, not area-per-material conservation or diffracting-edge
  preservation.
* **Topology is discarded.** Clusters are triangle soups. B-rep adjacency, face-sides
  and regions are gone. Hits can be mapped back to B-rep elements only through
  per-triangle `src.face` / `sem.feature` attributes.
* **Quantisation.** Positions are typically quantised per cluster (Nanite, DGF), which
  is fine for rendering and generally fine within mm budgets, but must be accounted
  for in precision claims.
* **Planarity is not a goal.** Simplified clusters are not planar polygons, so they
  are bad inputs for image-source methods.

**Verdict:** a cluster DAG is a good **derived representation** for (a) visualisation
and (b) bulk stochastic ray queries (diffuse acoustic tails, camera sensor
simulation, LOS matrices) at a deterministic world-space ε-cut, provided that
clustering and simplification are attribute- and boundary-aware and every triangle
carries `src.face`/`sem.feature`. It is **not** a source of truth, and it is the wrong
representation for image-source acoustics (use planar proxies) and for kernel
operations (use the B-rep).

## 8. Validation of LODs

Automated, per tile and per LOD:

* **Geometric:** two-sided Hausdorff distance ≤ declared ε (closest-point queries
  both ways).
* **Semantic:** every feature either appears at the LOD or is listed as aggregated,
  and every element has a valid `sem.feature`.
* **Acoustic conservation:** per-region volume within x%, per-material area within
  x%, per-band equivalent absorption area within x%.
* **Topological:** watertight regions, consistent region assignment (cell-complex vs
  winding number).
* **Behavioural regression:** reference scenarios (impulse responses at fixed
  source/receiver pairs, viewshed coverage percentages) compared across LODs and
  across software versions.

## References

* Biljecki, Ledoux, Stoter, *An improved LOD specification for 3D building models*, CEUS 2016: https://doi.org/10.1016/j.compenvurbsys.2016.04.005
* OGC CityGML 3.0 Conceptual Model: https://docs.ogc.org/is/20-010/20-010.html
* Cesium, *Fine-grained metadata in 3D Tiles Next*: https://cesium.com/blog/2022/05/31/fine-grained-metadata-in-3d-tiles-next
* Garland & Heckbert, *Surface Simplification Using Quadric Error Metrics*, SIGGRAPH 1997: https://www.cs.cmu.edu/~garland/Papers/quadrics.pdf
* Garland & Heckbert, *Simplifying Surfaces with Color and Texture using Quadric Error Metrics*, IEEE Vis 1998: https://www.cs.cmu.edu/~garland/Papers/quadric2.pdf
* Hoppe, *New Quadric Metric for Simplifying Meshes with Appearance Attributes*, IEEE Vis 1999: https://hhoppe.com/proj/newqem/
* CGAL Surface Mesh Simplification: https://doc.cgal.org/latest/Surface_mesh_simplification/index.html
* meshoptimizer: https://github.com/zeux/meshoptimizer
* Cohen-Steiner, Alliez, Desbrun, *Variational Shape Approximation*, SIGGRAPH 2004: https://doi.org/10.1145/1015706.1015817
* Salinas, Lafarge, Alliez, *Structure-Aware Mesh Decimation*, CGF 2015: https://doi.org/10.1111/cgf.12531
* Nan & Wonka, *PolyFit*, ICCV 2017: https://3d.bk.tudelft.nl/liangliang/publications/2017/polyfit/polyfit.html
* Vorländer, *Auralization* (2nd ed.), Springer 2020: https://doi.org/10.1007/978-3-030-51202-6
* Karis, Stubbe, Wihlidal, *Nanite: A Deep Dive*, SIGGRAPH 2021 Advances: https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf
* NVIDIA RTX Mega Geometry Vulkan samples: https://developer.nvidia.com/blog/nvidia-rtx-mega-geometry-now-available-with-new-vulkan-samples
* Microsoft DirectX, Clustered Geometry & PTLAS preview: https://devblogs.microsoft.com/directx/clas-ptlas-preview/
* AMD Dense Geometry Format: https://gpuopen.com/download/DGF.pdf

Note: the Pelzer & Vorländer (ICA 2010) citation and the 2016/2015 DOIs above are
from memory and were not fetched in this research pass.
