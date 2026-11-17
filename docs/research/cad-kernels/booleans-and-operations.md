# Modelling operations: Booleans, offsets, fillets, sweeps, intersection, trimming

Part of [CAD kernels, representations and robustness](README.md).

This file covers the operations a procedural environment modeller actually
needs. For each one it explains how established kernels do it, where they fail,
and what UberTopo should do. Each operation is weighed by how often UberTopo
will use it, which is not how often MCAD uses it.

| Operation | MCAD importance | UberTopo importance | Notes |
|---|---|---|---|
| 3D Booleans | very high | high (openings, building massing, terrain cuts) | Mostly planar operands |
| 2D Booleans / arrangements | medium | **very high** (road networks, parcels, footprints, land cover) | Biggest robustness win per effort |
| 2D offset | medium | **very high** (kerbs, sidewalks, setbacks, wall thickness, buffers) | |
| 3D offset / shell | high | medium (wall thickness, terrain clearance surfaces) | SDF route is robust |
| Fillet / blend | very high | low (visual only; mostly bevels/chamfers) | Avoid rolling-ball engine |
| Sweeps along curves | high | **very high** (roads, rails, kerbs, walls, pipes, power lines, rivers) | Transcendental curves |
| Surface–surface intersection | very high | low–medium (if curved surfaces stay procedural) | Avoid as core |
| Trimming | very high | medium (faces with holes, draped roads on terrain) | Planar trimming is easy |

---

## 1. 3D Booleans

### 1.1 Algorithm families

1. **B-rep/NURBS Booleans** (Parasolid, ACIS, OCCT `BOPAlgo`).
   - Steps: intersect every face pair (SSI), split edges and faces, classify
     the pieces, sew.
   - Tolerance-driven, and they fail on tangency and coincidence (see
     [kernel survey](kernel-survey.md)).
2. **Classical mesh Booleans with float arithmetic and ad hoc ε** (old Blender
   BMesh, many game tools). Fast and fragile.
3. **Topologically robust float Booleans** (Smith & Dodgson → Manifold).
   Always manifold, ε-valid. They need manifold input.
4. **Exact arrangement-based Booleans.**
   - Compute the arrangement of all input faces, i.e. the cell complex induced
     by all surfaces.
   - Label each cell, typically with a vector of generalised winding numbers
     (one per operand).
   - Select cells with the Boolean expression and extract the boundary.
   - Variants by arithmetic:
     - Rational EGC: Zhou et al. 2016; Blender Exact; Lévy 2024.
     - Indirect predicates: Cherchi et al. 2022.
     - Plane-based integers: Campen & Kobbelt 2010; Nehring-Wirxel 2021;
       EMBER 2022.
   - Winding numbers make them tolerant of *dirty input* (open, self-intersecting,
     overlapping), as long as the input is "piecewise-constant winding number"
     ([Zhou et al.](https://www.cs.columbia.edu/cg/mesh-arrangements)).
5. **BSP-tree CSG** (classic Naylor/Thibault; Nehring-Wirxel's octree-embedded
   BSPs for iterated CSG). Plain BSPs blow up as O(n²) or worse. Embedding them
   in an octree fixes that
   ([arXiv 2103.02486](https://arxiv.org/abs/2103.02486v1)).
6. **Volumetric / SDF Booleans** (min/max on fields, OpenVDB level-set CSG).
   Never fail. Accuracy is limited by resolution.

### 1.2 Why arrangement-based Booleans suit UberTopo

- **N-ary, not binary.** A building is often "massing ∪ wings ∪ roof −
  openings − courtyard". An arrangement computes all of it in one pass and
  evaluates any expression over the labelled cells. Iterated binary Booleans
  re-intersect the same faces many times.
- **The output is a cell complex.** That is exactly a non-manifold B-rep with
  regions. Shared walls between adjacent buildings, interior rooms, and
  multi-material layers all fall out naturally, and the cell labels *are*
  attributes (which operand, which material).
- **Provenance.** Every output face comes from an input face, which carries the
  procedural rule id, material and acoustic properties. Manifold's
  `originalID`/`faceID` shows the value of this
  ([Manifold wiki](https://github.com/elalish/manifold/wiki/Manifold-Library)).
- **Locality.** EMBER subdivides space adaptively and only does work where
  geometry interacts
  ([EMBER](https://www.graphics.rwth-aachen.de/publication/03339)). In a
  procedural city most building pairs do not interact at all.

### 1.3 Scaling to a city

Do **not** compute a global Boolean of the whole world. Instead:

- Booleans are **scoped to procedural objects**: a building, a junction, a
  bridge. They are cached by content hash of (rule, params, LOD).
- Interactions between objects (a building sitting on terrain, a road cutting
  an embankment) are handled by **contracts**, not general Booleans. For
  example, the building footprint is imprinted into the terrain TIN as a
  constrained loop, and the building's base is extended down to the
  terrain-min under the footprint.
- Exact Booleans are tile-local. Cross-tile objects are split on shared integer
  planes (see [robustness §11](robustness-and-exact-arithmetic.md)).

## 2. 2D Booleans, arrangements and offsets

A large share of environment geometry is 2D at heart: footprints, parcels, land
cover polygons, road edges, water bodies, lane markings.

- **Clipper2** (Angus Johnson, Boost Software License). Integer-coordinate
  (int64) polygon clipping (Vatti-style) and offsetting. It is robust in
  practice, fast, and widely used, including earlier versions of Manifold's
  CrossSection *(details from prior knowledge)*
  ([GitHub](https://github.com/AngusJohnson/Clipper2)). The integer coordinates
  match the tile fixed-point frames.
- **Manifold's `CrossSection`** now uses its own "Boolean2" manifold-native 2D
  arrangement pipeline *(reported by an indexed docs mirror; unverified)*.
- **CGAL 2D Arrangements and Snap Rounding** are the exact reference but GPL.
- **Road networks.** A road network is a planar **arrangement of curves**
  (clothoid centrelines with widths). Junctions are faces of the arrangement of
  carriageway boundaries. The robust pipeline:
  1. Tessellate each alignment to a polyline with a chordal error suited to the
     LOD (the transcendental-curve track).
  2. Offset to carriageway edges with exact or integer offsetting.
  3. Build the 2D arrangement exactly.
  4. Classify faces as carriageway, junction, island or verge from labels.
  5. Generate junction geometry procedurally per face.

  Keeping the **analytic** alignment as an edge attribute lets higher LODs
  re-tessellate without recomputing topology, *provided* the arrangement's
  combinatorics do not change between LODs. That assumption must be validated
  per LOD. A typical failure: a sharp kerb return whose tessellation crosses a
  neighbouring edge at a coarse LOD.

## 3. Offsetting

- **2D offsets** (polygon buffers). Use Clipper2 or exact straight skeletons.
  The straight skeleton (Aichholzer et al.) also gives **hip roofs** and
  sloped-surface generation from footprints, a classic procedural building
  technique. CGAL has `Straight_skeleton_2` (GPL). Exact or robust straight
  skeletons are notoriously tricky with near-parallel edges, and need exact
  predicates *(prior knowledge)*.
- **3D offsets of B-reps** (shelling, thickening). NURBS kernels struggle here.
  Offset surfaces self-intersect where the offset exceeds the radius of
  curvature.
  - **For planar polyhedra**, a mitred offset is plane translation:
    d → d + δ‖n‖. In plane-based integer geometry this is *exact* if the
    offset is quantised to the plane's normalisation. This is a strong reason
    to keep plane representations. Wall thickening is an offset of the plane
    set followed by re-arrangement.
  - **Robust general offsets** go through SDFs: build a narrow-band level set
    (OpenVDB `LevelSetFilter` offset), then re-mesh. This is robust but loses
    sharp features unless dual contouring is used. It is appropriate for
    clearance volumes ("everything within 3 m of power lines"), terrain-following
    flight envelopes, and acoustic proxies.

## 4. Fillets, chamfers, bevels

- **Rolling-ball fillets** are the hardest part of NURBS kernels: vertex blends,
  setbacks, and fillets running over edges. UberTopo has almost no
  *engineering* need for them.
- **Visual bevels** (rounded edges on building corners, kerbs, parapets) matter
  for camera realism. Options:
  1. A mesh bevel operator like Blender's Bevel, applied at render-LOD
     generation only, with no effect on the exact layer.
  2. Shading-only bevels (rounded-corner normal tricks, displacement).
  3. Explicit profile sweeps for kerbs and parapets. These are designed
     profiles, not fillets.
  4. SDF smooth-min blends for organic transitions (terrain to rock, eroded
     edges).
- **Chamfers** on planar solids are just extra planes. They are exact in
  plane-based geometry.
- **Recommendation.** No rolling-ball fillet engine. Chamfers are exact. Bevels
  are a visual-LOD post-process.

## 5. Sweeps and extrusions along curves (roads, walls, rails, pipes, rivers)

This is UberTopo's most important "CAD" operation.

### 5.1 Ingredients

- **Path.** A transcendental curve (clothoid, polynomial spiral, Spiro), a
  catenary for power lines, or a spline, plus a **vertical profile** (grade,
  vertical curves). Road design standards separate horizontal alignment
  (line / clothoid / arc), vertical alignment (grades plus parabolic vertical
  curves) and cross-section (lanes, crossfall, superelevation, kerbs,
  shoulders, ditches).
- **Frame.**
  - Roads and rails: an *up-locked* frame. The tangent is horizontal-projected,
    the up direction is world-up rotated by superelevation. Frenet frames flip
    at inflection points, where clothoid curvature crosses zero, so they are
    wrong here.
  - Pipes and tubes: **rotation-minimising frames**, e.g. Wang et al. 2008
    double reflection *(prior knowledge)*.
- **Profile.** A 2D polygon (cross-section), possibly varying along the path:
  lane additions, tapers, superelevation transitions.

### 5.2 Robust sweep generation

1. **Sample the path adaptively** by arc length, with spacing driven by the
   curvature and the target chordal or angular error for the LOD. Clothoids
   have linear curvature, so the sample count can be computed in closed form
   per segment.
2. **Stitch consecutive profile instances** into quad strips, which become
   faces of the polyhedral B-rep. Each strip face keeps an attribute
   back-reference: (alignment id, station s0..s1, profile edge id). This lets
   higher LODs re-sample, attributes such as lane id and surface material
   follow, and the strip be re-generated analytically.
3. **Detect inner-side self-intersection.** This happens when the profile
   offset exceeds the local radius of curvature, i.e. offset × κ > 1, which can
   occur on tight hairpins and junction kerb returns. Resolve it by clipping
   in 2D: compute the swept region as a 2D offset/union in plan view (exact
   integer 2D), then lift to 3D using the vertical profile.
4. **Lift to 3D and drape.** The road surface is a ruled surface between
   stations. Terrain is then *conformed to the road*, not the road to the
   terrain: the carriageway edges become CDT breaklines in the terrain TIN, and
   cut/fill slopes are generated as their own swept profile out to the
   daylight line where they meet the existing ground.
5. **Junctions** come from the 2D arrangement (§2). Their surfaces are
   triangulated from the face boundary, with heights interpolated from the
   adjoining road profiles using a smooth surface fit.

### 5.3 Walls, fences, rails

- Walls are 2D polyline offsets extruded vertically: exact planes. Openings are
  Boolean subtractions (or face holes, see §7).
- Fences and rails are thin, so they are **instanced** along the path. They are
  not Booleaned. For acoustics they are mostly transparent, or modelled as
  barriers with transmission loss. For LOS they are partial occluders.

### 5.4 Power lines, cables

- A catenary curve between pylons, with a radius attribute.
- Never Booleaned and never voxelised: too thin.
- Represented as **curve primitives** in the query layer: capsule chains in
  the BVH for drone-collision and LOS queries, and swept tubes at the visual
  LOD. This is a drone-specific concern, since cables are a major hazard and
  hard to see on camera.

## 6. Surface–surface intersection (SSI)

- The general NURBS–NURBS problem is solved by subdivision (to find start
  points and loops) plus marching (to trace curves). It needs tolerances, can
  miss small loops, and degrades at tangencies. Its output is an approximating
  curve that is on neither surface, which is the root of tolerant edges.
- **Avoid as a core dependency.** In UberTopo almost every curved surface is
  procedural (alignment sweep, revolution, terrain heightfield) and can be
  tessellated **before** intersection, at the requested LOD. Exact intersection
  then happens between planar facets.
- **Special cases worth analytic handling:**
  - plane ∩ heightfield, for water level against terrain, giving the
    shoreline; computed by marching squares on the TIN with exact predicates;
  - plane ∩ quadric (tank, dome, cylinder);
  - sweep ∩ plane (bridge deck vs. abutment).
- If true NURBS SSI is ever needed, for example for imported BIM with curved
  façades, delegate it to an adapter kernel (OCCT, or SMLib/USD BRep) and
  tessellate the result into the polyhedral layer.

## 7. Trimming and faces with holes

- In NURBS kernels, trimming means p-curve loops in parameter space. The 3D
  edge and the two p-curves disagree within tolerance.
- In the polyhedral B-rep, a **face is a plane plus an outer loop plus inner
  loops (holes)**. Trimming is exact 2D polygon work in the face's plane: a
  projection to 2D using the dominant axis, exact with integer planes.
- **Holes as first-class** (the requirement's "BMesh with holes"). Window and
  door openings can be *face holes* without a 3D Boolean, which is much cheaper
  and naturally LOD-aware: openings appear only above a given LOD. The opening's
  reveal (the wall thickness inside the opening) is generated as separate
  faces linking the inner loops on both wall faces. Use full 3D Booleans only
  where geometry genuinely interpenetrates.
- **Draped trimming.** Road areas on terrain, land-cover polygons and water
  bodies are 2D polygons imprinted on the terrain TIN as constrained edges. The
  terrain faces get labels. This is trimming in the 2.5D sense and stays exact
  with a CDT.

## 8. Minkowski sums and swept volumes

- **Drone vulnerability use cases:**
  - Clearance envelopes: a drone of radius r flying near obstacles is
    obstacles ⊕ ball(r).
  - Sensor coverage volumes.
  - Approach corridors.
- **Exact polyhedral Minkowski sums** are expensive. Campen & Kobbelt (SGP
  2010) used plane-based geometry for them. OpenSCAD still needs CGAL for
  `minkowski()`
  ([OpenSCAD list](https://lists.openscad.org/empathy/thread/TMJEJCZINIJNYJX2YF7IDNBAPQY66KIF)).
- **Recommendation.** Use SDF/level-set dilation for clearance volumes, which
  is accurate enough and robust. For analysis queries ("distance from path to
  obstacles"), compute with BVH distance queries and do not build Minkowski
  geometry at all.

## 9. Operation contracts for the kernel API

Every operation should declare:

- **Input validity** requirements: closed, PWN, manifold, or anything.
- **Output guarantees**: exact, ε-valid, or approximate within h.
- **Determinism**: bit-identical or not.
- **Provenance mapping**: output element → input element(s).
- **LOD semantics**: whether the result is consistent across LODs, and how
  (nested, conservative-outer, conservative-inner, best-fit).

Blender's three solvers (Float, Exact, Manifold) are an informal version of
this ([Blender manual](https://docs.blender.org/manual/en/4.5/modeling/modifiers/generate/booleans.html)).
UberTopo should make it formal.

## References

- Zhou et al., Mesh arrangements for solid geometry: https://www.cs.columbia.edu/cg/mesh-arrangements
- Nehring-Wirxel et al., octree-embedded BSPs: https://arxiv.org/abs/2103.02486v1
- EMBER: https://www.graphics.rwth-aachen.de/publication/03339
- Cherchi et al. 2022: https://arxiv.org/abs/2205.14151v1
- Lévy 2024/25: https://arxiv.org/abs/2405.12949v2
- Manifold wiki: https://github.com/elalish/manifold/wiki/Manifold-Library
- Smith & Dodgson / Smith thesis: https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-766.pdf
- Clipper2: https://github.com/AngusJohnson/Clipper2
- CGAL straight skeleton (prior knowledge): https://doc.cgal.org/latest/Straight_skeleton_2/
- OCCT Boolean gluing/fuzzy: https://dev.opencascade.org/content/gluing-options-boolean-component
- Blender Boolean solvers: https://docs.blender.org/manual/en/4.5/modeling/modifiers/generate/booleans.html
- OpenSCAD Minkowski/CGAL: https://lists.openscad.org/empathy/thread/TMJEJCZINIJNYJX2YF7IDNBAPQY66KIF
- Campen & Kobbelt 2010 (plane-based): https://www.graphics.rwth-aachen.de/media/papers/campen_2010_eg_021.pdf
- Wang et al., Computation of rotation minimizing frames, ACM TOG 2008 (prior knowledge): https://doi.org/10.1145/1330511.1330513
- OpenVDB: https://openvdb.org/about
