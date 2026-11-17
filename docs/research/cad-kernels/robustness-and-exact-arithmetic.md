# Robustness: tolerances, exact arithmetic and large coordinates

Part of [CAD kernels, representations and robustness](README.md).

Geometric algorithms fail when they make **inconsistent combinatorial
decisions** from inexact numbers. A classic example: the algorithm decides
point p is left of line L, and later decides p's neighbour q is right of L,
even though the computation that produced q assumed the opposite. The output
then has cracks, flipped faces or non-manifold junk, or the algorithm crashes.

This file surveys the strategies used to avoid that, from the 1980s to 2025,
and evaluates them for UberTopo.

---

## 1. Taxonomy of strategies

| Strategy | Idea | Examples | Failure mode |
|---|---|---|---|
| **ε-tolerances** | Treat values within ε as equal | Parasolid, ACIS, OCCT, Rhino, most game engines | Non-transitive equality; tolerance creep; user-tuned "fuzzy" values |
| **Topology-first / consistent decisions** | Ask each geometric question once; derive all topology from that minimal set so output is always topologically valid | Sugihara & Iri; Smith & Dodgson 2007; Manifold | Geometry may be slightly wrong (ε-valid); marginal inputs give wrong-but-valid results |
| **Exact predicates, float constructions** | Signs of determinants computed exactly (adaptive); constructed points rounded | Shewchuk predicates; Triangle; TetGen; CGAL `Epick` | Fine for Delaunay (no constructions); insufficient for Booleans (rounded intersection points break later predicates) |
| **Exact Geometric Computation (EGC)** | Exact predicates **and** exact constructions (rationals, lazy exact, algebraic numbers) | CGAL `Epeck`, Nef polyhedra, LEDA, CORE, Blender Exact | Slow; memory blowup; bit-length growth over iterated operations |
| **Indirect predicates / implicit points** | Constructed points stored as their defining inputs (e.g. line-plane intersection); predicates evaluated on the defining data with float filters + exact fallback | Attene 2020; Cherchi et al. 2020/2022 | Output still has to be rounded for export, which can create new intersections |
| **Plane-based geometry with integers** | Store planes (exact integer coefficients); vertices are implicit intersections of 3 planes; cutting never derives new planes from rounded points | Campen & Kobbelt 2010; Nehring-Wirxel et al. 2021; EMBER 2022 | Input must be quantised; fixed-width big integers (~256-bit); export still rounds |
| **Symbolic perturbation** | Break exact ties (degeneracies) consistently as if inputs were infinitesimally perturbed (SoS) | Edelsbrunner & Mücke 1990; Geogram PCK; Manifold | Only valid if inputs are exact; changes meaning of coplanar contact (union vs. touch) unless direction chosen per operation |
| **Snap rounding** | Round an exact arrangement to a grid while preserving topology approximately | Hobby 1999; CGAL 2D Snap Rounding; Devillers–Lazard–Lenhart (3D) | 2D is well solved; 3D is hard and may move geometry by more than one grid cell |
| **Interval / affine arithmetic** | Compute bounds; decide only when the interval excludes 0; otherwise escalate | CGAL `Interval_nt` filters; libfive / Fidget for implicits | Overestimation (dependency problem); needs an exact fallback for decisions |

## 2. ε-tolerances: why the 90s approach fails

- **Non-transitivity.** |a−b|<ε and |b−c|<ε do not imply |a−c|<ε. Clustering
  vertices by ε is order-dependent. Two Booleans performed in different orders
  give different topology.
- **Tolerance creep.** To keep results valid, kernels enlarge entity tolerances
  (OCCT does this explicitly). Each later operation sees fatter entities, so
  more coincidences are found, so tolerances get enlarged again. After hundreds
  of procedural operations, which is typical for a generated building, results
  become unpredictable.
- **User-visible knobs.** OCCT's fuzzy value and gluing modes make the *caller*
  responsible for knowing about coincidences, and a wrong choice gives wrong
  output ([OCCT forum](https://dev.opencascade.org/content/gluing-options-boolean-component)).
  A procedural system issuing millions of operations cannot tune per-operation
  knobs.
- **Scale coupling.** An absolute ε is meaningless across scales. Parasolid
  fixes 1e-8 inside a 1000-unit box
  ([Feko](https://2021.help.altair.com/2021/feko/topics/feko/user_guide/cadfeko/model_extents_feko_c.htm),
  [Bentley](https://docs.bentley.com/LiveContent/web/MicroStation%20Help-v28/en/AdvancedUnitSettings.html)).
  ACIS-based tools see the same "larger working area → worse accuracy" trade
  ([Bentley wiki](https://communities.bentley.com/products/building/building_analysis___design/w/building_analysis_and_design__wiki/10507/bim-solids-working-area-resolution)).

**Verdict.** UberTopo should not use ε-based *topological decisions*. Tolerances
remain legitimate for **modelling intent**, which is a different concept. Two
examples: "snap this road edge to the kerb if within 5 cm" (a procedural rule),
and "simplify features smaller than λ/4 for acoustics" (an LOD rule). Those are
explicit, deliberate operations, not hidden equality tests.

## 3. Exact predicates (Shewchuk)

Shewchuk (1997) provides adaptive-precision `orient2d`, `orient3d`, `incircle`
and `insphere`.

- A fast floating-point evaluation is tried first. It is extended with
  expansion arithmetic only when its error bound cannot certify the sign. Cost
  is near-float for non-degenerate inputs.
- It assumes IEEE-754 round-to-nearest. The code is public domain, and ports
  exist in Rust (`geometry-predicates`, `robust`) and many C++ libraries
  ([paper](https://people.eecs.berkeley.edu/~jrs/papers/robust-predicates.abstract),
  [PDF](https://www.wias-berlin.de/people/si/course/files/Shewchuk96-predicates.pdf),
  [Rust crate](https://rust-digger.code-maven.com/crates/geometry-predicates)).
- Geogram's **PCK (Predicate Construction Kit)** generalises this. It generates
  filtered predicates from formulas and adds symbolic perturbation
  ([Lévy 2015](https://core.ac.uk/download/49461268.pdf),
  [geogram wiki](https://github.com/BrunoLevy/geogram/wiki/Exact)).

**Limits.** Exact predicates on *rounded* constructed points are exact answers
to the wrong question. Booleans construct intersection points and then ask more
predicates about them. So predicates alone suffice for Delaunay, CDT, convex
hulls and point location, but **not** for Booleans or arrangements. This is
exactly Lévy's argument for exact constructions in mesh CSG: rounding an exact
intersection point can create new intersections
([Lévy 2024/25](https://arxiv.org/html/2405.12949v2)).

**For UberTopo.** Use them for every 2D operation where no new points are
constructed, or where constructed points stay symbolic: terrain CDT with
breaklines, point-in-polygon, footprint containment tests, and the
orientation/visibility tests in LOS queries.

## 4. Exact Geometric Computation (EGC) and CGAL kernels

- Yap's EGC paradigm: make *every* decision exactly. Numbers can be
  approximate as long as their signs are certified.
- CGAL's `Exact_predicates_exact_constructions_kernel` uses **lazy exact**
  numbers: an interval approximation plus a DAG recording how the number was
  built, re-evaluated exactly with GMP/MPFR rationals only when the interval is
  inconclusive.
- **Nef polyhedra** build regularised Booleans on this. They are fully exact
  and support non-manifold results, but are notoriously slow and memory-heavy.
  This is why OpenSCAD moved to Manifold
  ([OpenSCAD list](https://lists.openscad.org/empathy/thread/TMJEJCZINIJNYJX2YF7IDNBAPQY66KIF)).
- **Bit growth.** In iterated CSG, a vertex created by intersecting edges that
  were themselves created by intersections has a rational representation whose
  bit length grows with *construction depth*. For procedural buildings with
  hundreds of chained Booleans, this is unbounded in principle.
- **Mesh arrangements (Zhou, Grinspun, Zorin, Jacobson 2016)** compute the full
  arrangement of all input triangles with exact constructions, then label cells
  by **generalised winding number** vectors. Any Boolean, even of
  self-intersecting or non-closed "PWN" inputs, is then a cell selection
  ([project page](https://www.cs.columbia.edu/cg/mesh-arrangements)). It is
  very robust and very general. EMBER reports it as slow: 141 s on an example
  where an inexact method takes about 1 s
  ([EMBER paper](https://www.graphics.rwth-aachen.de/media/papers/339/ember_exact_mesh_booleans_via_efficient_and_robust_local_arrangements.pdf)).
  Blender's "Exact" solver follows this design.

## 5. Indirect predicates (Attene) and interactive robust Booleans (Cherchi et al.)

- **Attene 2020.** A constructed point is never rounded. It is represented
  *implicitly* by the explicit inputs that define it:
  - **LPI**: line–plane intersection, from 5 explicit points.
  - **TPI**: three-plane intersection, from 3 triangles.

  Predicates on implicit points expand into polynomial expressions of the
  original float inputs. A floating-point filter with a static or semi-static
  error bound decides most cases, with exact expansion arithmetic as fallback.
  Construction depth is therefore **always one**, so bit growth is bounded
  ([arXiv 2105.09772](https://arxiv.org/abs/2105.09772v2)). The catch is
  combinatorial: each mix of explicit and implicit arguments is a separate
  predicate, for example 16 `orient3d` variants and 32 `inSphere` variants
  ([Diazzi et al., CDT paper](https://arxiv.org/pdf/2309.09805)).
- **Cherchi, Pellacini, Attene, Livesu (TOG 2022), "Interactive and Robust Mesh
  Booleans".** Their pipeline:
  1. Exact mesh arrangement using indirect predicates.
  2. Inside/outside labelling via a *cascade* of ray-casting tests, from fast
     inexact to exact, using the exact test only when needed.

  They report the first Boolean with robustness guarantees at interactive
  rates for meshes up to about 200K triangles, and up to 100× faster than
  topological flooding or patch-graph methods
  ([arXiv 2205.14151](https://arxiv.org/abs/2205.14151v1),
  [SIGGRAPH history](https://history.siggraph.org/?p=215726)). The code is MIT
  licensed, but its README requires manifold, watertight,
  self-intersection-free input, and it is maintained on a best-effort basis
  ([repo](https://github.com/gcherchi/InteractiveAndRobustMeshBooleans)).

## 6. Plane-based geometry with integer coordinates

This is the most promising family for UberTopo.

### 6.1 Idea

Represent each polygon by its **supporting plane** plus the **edge planes**
that bound it. Do not store it by its vertices.

- A plane is (a, b, c, d) with integer coefficients.
- A vertex is the intersection of three planes. It is stored implicitly and
  evaluated as homogeneous coordinates (x, y, z, w) using integer determinants.
- Cutting a polygon by a plane only *adds a reference to an existing plane*.
  New planes are never derived from constructed points.
- Therefore **construction depth stays one forever**. Bit widths are fixed and
  computable in advance, and iterated CSG never grows numbers.

**Lineage.**

- **Campen & Kobbelt 2010** ("Exact and Robust (Self-)Intersections for
  Polygonal Meshes", Eurographics) introduced the adaptive-octree + local BSP +
  plane-based approach. Their analysis showed that, with minimal input
  quantisation, fixed-precision arithmetic suffices
  ([PDF](https://www.graphics.rwth-aachen.de/media/papers/campen_2010_eg_021.pdf),
  [EG diglib](https://diglib.eg.org/xmlui/handle/10.2312/CGF.v29i2pp397-406)).
- **Nehring-Wirxel, Trettner, Kobbelt 2021**, "Fast Exact Booleans for Iterated
  CSG using Octree-Embedded BSPs" (CAD 135). It uses plane-based geometry and
  integer arithmetic for "unconditionally robust operations". It reaches up to
  2.5 M mesh-plane cuts per second on one core, and was demonstrated on sweep
  volumes and milling simulation, i.e. long chains of iterated Booleans
  ([arXiv 2103.02486](https://arxiv.org/abs/2103.02486v1)).
- **Trettner, Nehring-Wirxel, Kobbelt 2022, EMBER** ("Exact Mesh Booleans via
  Efficient & Robust Local Arrangements", SIGGRAPH/TOG 41(4)). Its design:
  - plane-based input with **homogeneous integer coordinates**,
  - recursive subdivision of the bounding box with early-out, so no global
    acceleration structure is pre-built,
  - local arrangements plus generalised winding numbers for classification.

  The authors report Booleans on meshes with millions of triangles at
  interactive rates, beating state-of-the-art methods, *including inexact
  ones*, by orders of magnitude on Thingi10K
  ([publication page](https://www.graphics.rwth-aachen.de/publication/03339),
  [PDF](https://www.graphics.rwth-aachen.de/media/papers/339/ember_exact_mesh_booleans_via_efficient_and_robust_local_arrangements.pdf)).
  A community port, `zalo/ember-csg`, uses fixed-width integers up to 256 bits.
  Its final winding-number classification uses double-precision multi-ray
  casting, so it is not fully exact
  ([ember-csg](https://github.com/zalo/ember-csg)).

### 6.2 Bit-width budget (worked estimate)

The following derivation is mine and approximate. It is consistent with the
≤256-bit fixed-width arithmetic reported for EMBER implementations.

Let input vertex coordinates be signed integers of **b bits**.

| Quantity | Formula | Bits ≈ |
|---|---|---|
| Edge vector | p1 − p0 | b + 1 |
| Plane normal (a,b,c) | (p1−p0)×(p2−p0) | 2b + 3 |
| Plane offset d | −n·p0 | 3b + 5 |
| Vertex w (3 planes) | det[n1 n2 n3] | 6b + 12 |
| Vertex x,y,z | det with one column of d's | 7b + 14 |
| Classify vertex vs plane | n·(x,y,z) + d·w | ≈ 9b + 20 |

With **b = 26**, the worst predicate needs about 254 bits, which fits in
**256-bit fixed-width integers**. These are implementable as 4×64-bit limbs
with no heap allocation, are SIMD-friendly, and are much faster than GMP.

What 26 bits means at different grid resolutions:

| Grid resolution | 26-bit extent (2²⁶ steps) | Comment |
|---|---|---|
| 1 mm | 67.1 km | covers a large test site in one frame |
| 0.1 mm | 6.7 km | comfortable per-tile for buildings |
| 1 cm | 671 km | coarse regional work |

**Implication.** Combine plane-based exact Booleans with **local frames
(tiles)**, for example 4–8 km tiles on a 0.1 mm grid. All exact work then
happens in small integers, and the size limit becomes irrelevant. Most
Booleans in UberTopo are naturally local: a building, a bridge abutment, a road
junction.

### 6.3 Issues to plan for

- **Input quantisation.** Snapping input vertices to the grid can make a
  near-planar quad non-planar. It must be split into triangles, or represented
  by a plane fitted *then* quantised, with vertices re-derived as plane
  intersections. Procedural generators can emit planes directly: a wall is
  naturally "plane + 4 bounding planes". This is a strong fit, because
  procedural building grammars think in planes.
- **Curved geometry** enters as already-faceted planes at a chosen LOD.
- **Export.** Output vertices are rational (x/w, y/w, z/w). Rounding them to
  double or float for consumers can create tiny self-intersections. This is
  harmless for rendering and acoustic ray tracing if the topology is consistent
  and faces are not re-intersected. If re-ingestion is needed, keep the exact
  representation as the authority and treat rounded meshes as derived caches,
  never as inputs to further Booleans.

## 7. Topology-first robustness (Smith & Dodgson; Manifold)

Smith & Dodgson (CAD 39(2), 2007) and Smith's 2009 thesis give a Boolean on
polyhedra using ordinary floating point. It is *guaranteed* to produce a
topologically valid result from a topologically valid input, whatever the
numerical error. It works by building all topological decisions from one
consistent set of primitive tests
([UCAM-CL-TR-766](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-766.pdf)).

Manifold builds on this and adds:

- symbolic perturbation for floating-point-equal ties, with the perturbation
  direction chosen per operation (outward for union, inward for difference and
  intersection) so coplanar faces resolve sensibly,
- an ε-validity contract,
- edge-collapse cleanup of degenerate triangles,
- full data parallelism

([wiki](https://github.com/elalish/manifold/wiki/Manifold-Library)).

**Pros.**
- Fast.
- Float coordinates throughout (no quantisation).
- Permissive licence.
- Always-manifold output, which is valuable for acoustic and physics consumers.

**Cons.**
- Geometric correctness is not guaranteed for marginal input.
- Inputs must already be manifold.
- Once outputs are re-fed with rounding error, tie-breaking loses its
  guarantees. Iterated CSG accumulates ε.

## 8. Symbolic perturbation and snap rounding

- **Simulation of Simplicity (SoS)** perturbs input coordinates by symbolic
  infinitesimals ε^(2^i) (Edelsbrunner & Mücke 1990). Every degenerate
  predicate then gets a consistent non-zero answer. This removes special-case
  code for coplanar and collinear configurations.
  - **Caveat.** For Booleans, perturbation *chooses* whether two coplanar faces
    "touch" or "overlap". Union and difference need opposite choices (Manifold
    handles this).
  - In UberTopo, two walls sharing a face on purpose are *intent*. That intent
    should be encoded explicitly, for example as a shared plane id in the
    plane-based representation. It should not be left to perturbation.
- **Snap rounding** converts an exact arrangement with rational vertices into
  one on a grid while preserving topology approximately.
  - In 2D it is mature: Hobby 1999; iterated snap rounding (Halperin & Packer);
    CGAL `Snap_rounding_2` *(prior knowledge; CGAL docs: https://doc.cgal.org/latest/Snap_rounding_2/)*.
  - In 3D it is much harder. Fortune and Devillers–Lazard–Lenhart give
    algorithms with large worst-case displacement or complexity *(prior
    knowledge)*.
  - **Recommendation.** Use 2D snap rounding for road-network and parcel
    arrangements exported to fixed grids. Avoid relying on 3D snap rounding by
    keeping the exact plane-based representation authoritative.

## 9. Interval arithmetic

- **As a filter.** Intervals bound the value of a predicate cheaply. If 0 is
  not in the interval, the sign is certain. CGAL uses this everywhere
  (`Interval_nt`).
- **For implicits.** Evaluate f over a box. If the interval excludes 0, the
  whole box is inside or outside. This drives octree meshing and rendering in
  libfive and Fidget. Interval overestimation grows with expression depth.
  Keeter's 2025 work uses Lipschitz/gradient bounds as a less conservative
  alternative ([blog](https://www.mattkeeter.com/blog/2025-05-14-gradients/)).
- **For curve integration.** Validated numerics (interval ODE solvers) could
  bound clothoid/Spiro integration error. This is relevant to the
  transcendental-curve track.

## 10. Rational and fixed-point coordinates

| Choice | Pros | Cons |
|---|---|---|
| IEEE double | Universal; 53-bit mantissa; hardware fast | Not closed under constructions; needs predicates/filters |
| IEEE float | GPU-native, half the memory | 24-bit mantissa: ~1 mm ulp at 10 km, 0.5 m at Earth-centred coordinates |
| Fixed-point int32/int64 | Exact add/sub; uniform resolution; trivially reproducible across platforms | Range/resolution trade; multiplication grows bits |
| Fixed-width wide int (128/256) | Exact plane-based predicates without heap | Implementation effort; ~2–10× slower than double for predicates (est.) |
| GMP rationals / lazy exact | Fully general | Heap allocation; 10–100× slower; unbounded growth |

**Recommendation.** Use integer (fixed-point) **storage** for exact-layer
positions and planes inside tiles. Use double for evaluation, procedural
parameters and analytic carriers. Use float only in GPU-facing,
camera-relative buffers.

**Determinism benefit.** Integer exact Booleans give **bit-identical results
across platforms and compilers**. Reproducible test scenarios are a hard
requirement for testing defense systems: the same seed must give the same world.

## 11. Large coordinates: geospatial scale

Unit in last place (ulp) for each format, by magnitude of the coordinate:

| Coordinate magnitude | float ulp | double ulp |
|---|---|---|
| 1 km | 61 µm | 1.1e-13 m |
| 10 km | 0.98 mm | 1.8e-12 m |
| 100 km | 7.8 mm | 1.5e-11 m |
| 6,400 km (ECEF) | 0.5 m | 9.3e-10 m |

(ulp = 2^(⌊log₂|x|⌋ − 23) for float, −52 for double.)

**Consequences.**

- **Float world coordinates are unusable** beyond a few km. A microphone array
  with 5 cm spacing at an ECEF position quantised to 0.5 m is meaningless.
  Time-difference-of-arrival localisation at 343 m/s needs sub-cm *relative*
  precision.
- **Double ECEF** has nm precision and is adequate as a *storage and exchange*
  frame. Doing geometry in ECEF is still poor practice, though: planes are not
  axis-aligned, terrain is curved, and determinants mix 1e7 magnitudes with
  1e-3 features, so the filters fail more often.

**Industry practice.**

- **Unreal Engine 5 Large World Coordinates.** Double-precision transforms on
  the CPU, and a camera-relative float on the GPU via a tile offset
  ([Epic LWC docs](https://dev.epicgames.com/documentation/unreal-engine/large-world-coordinates-in-unreal-engine-5)).
  Subsystems that were not converted, such as distance fields initialised with
  planetary coordinates, still break
  ([Cesium forum](https://community.cesium.com/t/found-precision-loss-while-converting-matrix-to-gpu-format-in-large-cesium-scene/44115)).
- **Cesium / 3D Tiles.** Tiles are stored relative to a centre (RTC) or with
  per-tile transforms. Origin rebasing is used, and dropped, in different
  versions ([Cesium forum](https://community.cesium.com/t/relocate-georeferencing-center-runtime/20729)).
- **Origin rebasing** (floating origin) is a rendering and physics fix only. It
  does not help geometry kernels.

**Recommended frame strategy for UberTopo.**

1. **Global reference.** A geodetic datum (WGS84/ITRF) plus a project
   CRS. Store each site's anchor in double ECEF or geodetic coordinates.
2. **Site frame.** Local East-North-Up (ENU), or a conformal projection
   (UTM / transverse Mercator) for 2D procedural layout. Roads and parcels are
   naturally 2D-projected. Earth curvature over 20 km is about 31 m of drop,
   which is relevant for long-range LOS and acoustics. The site frame must
   therefore either be true 3D ENU, or apply a curvature correction for
   long-range queries.
3. **Tile frames.** Fixed-point integer lattices, for example 0.1 mm over 4–8
   km tiles, for exact Booleans and arrangements. A tile is identified by an
   integer key. A position is (tile key, int offset). Cross-tile objects
   (roads, rivers, power lines) are split along tile boundaries using
   *integer* planes common to both tiles, so the seams are exact.
4. **Consumer frames.** Camera-relative float for GPU. Sensor-relative double
   for acoustic and RF simulation.

## 12. Recommended robustness strategy

1. **2D layer** (road networks, footprints, parcels, terrain breaklines):
   - Integer/fixed-point coordinates.
   - Exact predicates and exact (rational or plane-based) intersections.
   - 2D arrangement with snap rounding for export.
   - Exact-predicate CDT for terrain.
   - Clipper2-style integer polygon offsetting and clipping (see
     [Booleans](booleans-and-operations.md)).
2. **3D polyhedral Booleans:**
   - Plane-based, integer homogeneous coordinates (EMBER / Nehring-Wirxel
     family).
   - Tile-local.
   - Arrangement-based, producing a cell complex, with winding-number or label
     classification.
   - Exact, deterministic, and no tolerances.
3. **Fast path:** Manifold for closed triangle solids where float speed matters
   and ε-validity is acceptable, such as visual-only detail assets.
4. **Implicits:** interval or Lipschitz-bounded evaluation. Robustness here is
   about *meshing* (dual contouring with exact sign tests on the grid).
5. **No ε-based equality** anywhere in topology. Modelling tolerances are
   explicit procedural operations.
6. **Determinism** as a test requirement. The same procedural input produces
   bit-identical exact-layer output on all platforms.

## References

- Shewchuk, Adaptive precision floating-point arithmetic and fast robust geometric predicates (1997): https://people.eecs.berkeley.edu/~jrs/papers/robust-predicates.abstract ; PDF: https://www.wias-berlin.de/people/si/course/files/Shewchuk96-predicates.pdf
- Lévy, Robustness and efficiency of geometric programs: the Predicate Construction Kit (2015): https://core.ac.uk/download/49461268.pdf
- Geogram exact arithmetic wiki: https://github.com/BrunoLevy/geogram/wiki/Exact
- Lévy, Exact predicates, exact constructions and combinatorics for mesh CSG (2024/25): https://arxiv.org/abs/2405.12949v2
- Zhou, Grinspun, Zorin, Jacobson, Mesh arrangements for solid geometry (2016): https://www.cs.columbia.edu/cg/mesh-arrangements
- Attene, Indirect predicates for geometric constructions (CAD 2020): https://arxiv.org/abs/2105.09772v2
- Diazzi et al., Constrained Delaunay tetrahedrization (uses implicit points): https://arxiv.org/pdf/2309.09805
- Cherchi, Pellacini, Attene, Livesu, Interactive and robust mesh Booleans (TOG 2022): https://arxiv.org/abs/2205.14151v1 ; code: https://github.com/gcherchi/InteractiveAndRobustMeshBooleans
- Campen & Kobbelt, Exact and robust (self-)intersections for polygonal meshes (EG 2010): https://www.graphics.rwth-aachen.de/media/papers/campen_2010_eg_021.pdf
- Nehring-Wirxel, Trettner, Kobbelt, Fast exact Booleans for iterated CSG using octree-embedded BSPs (CAD 2021): https://arxiv.org/abs/2103.02486v1
- Trettner, Nehring-Wirxel, Kobbelt, EMBER (TOG 2022): https://www.graphics.rwth-aachen.de/publication/03339 ; community port: https://github.com/zalo/ember-csg
- Smith & Dodgson (2007) / Smith thesis: https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-766.pdf
- Manifold wiki: https://github.com/elalish/manifold/wiki/Manifold-Library
- Edelsbrunner & Mücke, Simulation of Simplicity, ACM TOG 9(1) 1990 (prior knowledge): https://doi.org/10.1145/77635.77639
- CGAL 2D Snap Rounding (prior knowledge): https://doc.cgal.org/latest/Snap_rounding_2/
- OCCT gluing/fuzzy: https://dev.opencascade.org/content/gluing-options-boolean-component
- Parasolid size box: https://2021.help.altair.com/2021/feko/topics/feko/user_guide/cadfeko/model_extents_feko_c.htm
- Unreal LWC: https://dev.epicgames.com/documentation/unreal-engine/large-world-coordinates-in-unreal-engine-5
- Cesium precision loss thread: https://community.cesium.com/t/found-precision-loss-while-converting-matrix-to-gpu-format-in-large-cesium-scene/44115
- Keeter, Gradients are the new intervals: https://www.mattkeeter.com/blog/2025-05-14-gradients/
