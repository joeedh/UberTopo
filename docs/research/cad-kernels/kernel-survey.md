# Survey and critique of geometry kernels

Part of [CAD kernels, representations and robustness](README.md).

This file covers geometry kernels as whole systems: what each is built on, which
parts are 1980s/90s legacy, and where each one fails. It ends with lessons for
UberTopo. B-rep topology data structures are covered in a separate research
track. This file only covers them where they affect a kernel's robustness or
scale.

Research method: web searches done in November 2026, plus prior knowledge.
Where a claim comes from prior knowledge and was not checked against a current
source, it is marked *(unverified)*.

---

## 1. The industrial NURBS B-rep kernels

All of the industrial kernels follow the same 1980s design:

- **Exact geometry.** Analytic surfaces (plane, cylinder, cone, sphere, torus)
  plus NURBS, procedural sweeps and offsets.
- **Boundary topology.** Body / region / shell / face / loop / edge / vertex.
- **Trimmed parametric surfaces.** Each face is a carrier surface clipped by
  p-curves (2D curves in the surface's parameter space). Each edge also carries
  a 3D curve.
- **Floating-point arithmetic with tolerances.** Two entities are "the same" if
  they are within ε. Imported or sloppy geometry is handled with tolerant edges
  and vertices, sewing and healing.
- **Hard algorithms.** Surface–surface intersection (SSI) by marching, offset
  surfaces, and the rolling-ball blend (fillet) engine.

### 1.1 Parasolid (Siemens)

- **Lineage.** ROMULUS (Ian Braid et al., Cambridge, late 1970s), then Shape
  Data, then Parasolid (1988). It is now owned by Siemens Digital Industries
  Software and resold through Tech Soft 3D
  ([Tech Soft 3D](https://www.techsoft3d.com/products/parasolid)).
- **Users.** NX, Solid Edge, SolidWorks, Onshape, Shapr3D and Plasticity
  ([Engineering.com](https://mobile.engineering.com/amp/17309.html),
  [Plasticity wiki](https://wiki.privacytools.io/wiki/Plasticity_(software))).
- **Numerical model.** Parasolid is unitless. It has a linear precision of
  **1e-8** and a **size box of 1000 units**, i.e. coordinates within ±500 of the
  origin ([Altair Feko docs](https://2021.help.altair.com/2021/feko/topics/feko/user_guide/cadfeko/model_extents_feko_c.htm),
  [Bentley MicroStation help](https://docs.bentley.com/LiveContent/web/MicroStation%20Help-v28/en/AdvancedUnitSettings.html)).
  Host applications that need a larger working area trade precision for extent
  ([Bentley wiki](https://communities.bentley.com/products/building/building_analysis___design/w/building_analysis_and_design__wiki/10507/bim-solids-working-area-resolution)).
  So Parasolid is built for a 1 km cube at 10 nm resolution. A procedurally
  generated 20 km × 20 km site does not fit in one Parasolid body without
  rescaling.
- **Tolerant modelling.** In 1995 Jackson described how Parasolid moved from one
  global tolerance to *local* tolerances on edges and vertices. A tolerant edge
  is represented by its p-curves on the adjacent faces, without one exact 3D
  curve ([Jackson 1995](https://ftp1.cs.wisc.edu/pub/users/prem/jackson-SM-95.pdf)).
  This is the core 90s pattern: the kernel stops trying to make geometry
  consistent and records how inconsistent it is.
- **Strengths.** It is the most battle-tested blending, shelling and offsetting
  engine in existence, after 35+ years of customer bug reports.
- **Weaknesses for UberTopo.**
  - Closed source and expensive per-seat or royalty licensing
    ([change.org petition on licensing barriers](https://www.change.org/p/open-parasolid-kernel-s-source-code-and-apis-under-a-free-license)).
  - Single-threaded at its core *(unverified for recent versions)*.
  - Size-box limited.
  - Designed for parts with tens to thousands of faces, not cities with 10⁷
    faces.
  - Its attribute system is generic but not designed for dense per-element data
    such as acoustic absorption spectra.

### 1.2 ACIS / Autodesk ShapeManager (Spatial / Dassault Systèmes)

- **Lineage.** Three-Space Ltd (Braid, Grayer, Lang) built ACIS in 1989.
  Spatial owns it, and Spatial has been part of Dassault since 2000.
  Autodesk forked it in 2001 as ShapeManager, which is used by Inventor and
  Fusion *(fork date from prior knowledge)*.
- **Numerical model.** It uses global resolutions, `resabs` ≈ 1e-6 and
  `resnor` ≈ 1e-10 *(unverified defaults)*, plus tolerant entities
  (`TEDGE`/`TVERTEX`).
- **Healing.** Its healing and "tolerize" workflow is the textbook example: let
  the software heal what it can, then attach tolerances where gaps remain
  ([KeyCreator / ACIS tolerant modelling docs](https://help.kubotekkosmos.com/keycreator/2024SP1/A10196)).
  Tolerant bodies keep their gaps when exported to STEP or STL, so the
  inconsistency spreads downstream.
- **Assessment.** Same architecture as Parasolid, generally considered somewhat
  less robust, and commercially licensed. There is no reason to adopt it here.

### 1.3 CGM (Dassault) and Granite (PTC)

- **CGM** is the CATIA V5/3DEXPERIENCE kernel. Spatial markets it as a
  licensable component
  ([Engineering.com: Spatial, ACIS, CGM](https://www.engineering.com/?p=129684)).
  It is rich in features, but kernels cannot be swapped: "you can't unbolt
  Parasolid or ACIS, and bolt in CGM"
  ([TransMagic](https://transmagic.com/which-geometric-modeling-kernel/)).
- **Granite** is PTC's in-house kernel for Pro/ENGINEER and Creo. It is
  effectively not available to third parties today
  ([Wikipedia: geometric modeling kernel](https://www.Wikipedia.org/wiki/CAD_kernel)).
- Both are the same 1990s tolerance-based NURBS B-rep design. Neither is a
  realistic option.

### 1.4 C3D (C3D Labs / ASCON)

- A commercial modeller, constraint solver and converter suite. It is used by
  nanoCAD, KOMPAS-3D and some Western CAM and EDA vendors
  ([TransMagic](https://transmagic.com/which-kernel)).
- **Procurement risk.** C3D Labs is a Russian company (ASCON Group). For a
  **drone-defense** program, supply-chain, sanctions and export-control rules
  probably rule out a Russian-origin kernel regardless of its technical merit.
  The project owner should confirm this with their compliance function.

### 1.5 SMLib (Solid Modeling Solutions, now NVIDIA "USD BRep")

- SMLib is a long-standing commercial NURBS / trimmed-surface / solid library.
  In **October 2026** NVIDIA released it as open source inside **USD BRep**
  under Apache-2.0. USD BRep is a C++/Python NURBS B-rep library with OpenUSD
  import and export ([NVIDIA SMLib manual](https://docs.nvidia.com/smlib/manual/smlib/geometry/index.html),
  [news coverage](https://alphasignal.ai/news/nvidia-opens-usd-brep-s-cad-kernel-to-developers-under-apache-2-0),
  [pbxscience](https://pbxscience.com/?p=24995)). A Rust port, *solidcore*,
  already exists ([solidcore wiki](https://github.com/bherbruck/solidcore/wiki)).
- **Assessment.** This is now the most permissively licensed full NURBS solid
  kernel. It is new as an open-source project, and its robustness relative to
  OCCT has not been independently shown. It is still a tolerance-based 90s
  design. **Evaluate it as an adapter** for NURBS import, export and evaluation,
  not as the core. The release is about one month old at the time of writing,
  and I could not open the develop3d article (network-blocked), so details
  should be re-checked.

### 1.6 Open CASCADE Technology (OCCT)

- **Lineage.** Matra Datavision's CAS.CADE (1990s), open-sourced in 1999.
  Since 6.7.0 (2013) it has been licensed under **LGPL-2.1 with the Open
  CASCADE Exception**, which allows header-inlined template code
  ([SPDX OCCT-exception-1.0](https://spdx.org/licenses/OCCT-exception-1.0.html),
  [OCCT license page](https://dev.opencascade.org/doc/overview/html/occt_public_license.html)).
  Wikipedia lists an 8.0.1 release in July 2026, which I have not verified
  ([Wikipedia](https://en.wikipedia.org/wiki/Open_Cascade_Technology)).
- **Users.** FreeCAD, CadQuery, build123d, KiCad (STEP), Salome, and Gmsh
  (optional).
- **Robustness model.** Every vertex, edge and face carries a tolerance. Many
  operations *increase* these tolerances to make the result valid. Tolerances
  grow monotonically across a modelling history, which practitioners call
  "tolerance creep".
  - The Boolean component exposes a **fuzzy value** (an extra tolerance for
    near-coincident inputs) and a **gluing option** (`GlueShift`, `GlueFull`).
    The *caller* must guarantee coincidence for gluing, and "setting an
    inappropriate option is likely to lead to an incorrect result"
    ([OCCT forum: gluing options](https://dev.opencascade.org/content/gluing-options-boolean-component),
    [OCCTSwift cookbook](https://gsdali.github.io/OCCTSwift/guides/cookbook/booleans.html)).
  - So robustness is pushed onto the user as tuning knobs.
- **Known failure modes.**
  - Booleans on tangent or coincident faces.
  - Fillets that fail on complex edge chains.
  - Booleans that hang or return null on self-intersecting or inverted operands
    ([OCCTSwift cookbook](https://gsdali.github.io/OCCTSwift/guides/cookbook/booleans.html)).
  - Mostly single-threaded algorithms (the Boolean has a parallel mode).
  - A dated API: custom handles, RTTI and collections.
- **Assessment.** It is the only mature, *free*, full-featured NURBS kernel
  besides the brand-new SMLib release. It is useful at the edges: STEP/IGES
  import, analytic surface evaluation, and occasional exact-geometry jobs. It
  should not be the core. LGPL is manageable through dynamic linking, but it
  constrains static and embedded deployment.

### 1.7 Rhino / openNURBS (McNeel)

- **openNURBS** is a free toolkit for reading and writing `.3dm` files and
  evaluating NURBS. It does **not** include Rhino's Booleans or intersection
  code.
- **Rhino** uses a per-document *absolute tolerance* (for example 0.001 units).
  Its Booleans are tolerance-driven and famously fail on "naked edges". Rhino 7
  added SubD as a first-class object *(prior knowledge)*.
- **Takeaway.** Rhino shows that practitioners will accept a *non-solid*,
  tolerance-based, "good enough" surface model if the UX is good. Downstream
  consumers that need watertightness (acoustic simulation) then suffer.

### 1.8 Cloud products built on kernels: Onshape, Plasticity, Zoo

- **Onshape** and **Plasticity** both license Parasolid
  ([Onshape forum](https://forum.onshape.com/discussion/27002/plasticity-2025-1)).
  Their innovation is in the app layer: cloud/collaboration for Onshape,
  artist-friendly UX for Plasticity. The kernel is unchanged.
- **Zoo (formerly KittyCAD)** says it wrote its own GPU-based, cloud-hosted
  B-rep engine. Its stated motivation is that most CAD still runs on Parasolid
  "from the late '80s". The application (Design Studio, the KCL language) is
  open source. **The engine is proprietary**
  ([Zoo Design Studio v1](https://zoo.dev/blog/zoo-design-studio-v1),
  [geometry engine](https://zoo.dev/geometry-engine),
  [Feb 2026 update](https://docs.zoo.dev/blog/whats-new-feb-2026)).
  This is a validation of the "start fresh" thesis, but there is nothing to
  adopt.

---

## 2. Open-source / research B-rep kernels

### 2.1 Fornjot (Rust)

- An early-stage B-rep kernel in Rust by Hanno Braun. It is now **shut down**:
  "Its goals were never reached"
  ([README](https://cdn.jsdelivr.net/gh/hannobraun/fornjot@main/README.md),
  [GitHub](https://github.com/hannobraun/fornjot)).
- **Lesson.** A small team building a general NURBS-capable B-rep kernel from
  scratch is a multi-year effort that commonly stalls before the hard parts:
  curved-surface Booleans, fillets and robust SSI. UberTopo must keep its
  geometric scope narrow (see [recommendations](recommendations.md)).

### 2.2 Truck (Rust, ricosjp) and monstertruck

- Truck is an Apache-2.0 Rust kernel: NURBS geometry, B-rep topology, modelling,
  WebGPU rendering, and `truck-shapeops` (Booleans and shape healing)
  ([GitHub](https://github.com/ricosjp/truck)).
- The *monstertruck* fork adds STEP I/O, meshing and filleting
  ([docs.rs](https://docs.rs/monstertruck)).
- **Assessment.** It is clean, modern Rust, but small. Its Booleans are
  tolerance-based. It is useful as a reference for NURBS evaluation in Rust. It
  is not a robust core for km-scale scenes.

### 2.3 SolveSpace

- A GPLv3 parametric modeller by Jonathan Westhues. It has its own small NURBS
  kernel with approximate trimming, and a mesh-based fallback for Booleans
  *(prior knowledge)*.
- It shows that a small team can get 90% of the way with a compact codebase. The
  last 10% (tangent cases, robust SSI) is where its Booleans visibly fail.

### 2.4 Code-CAD frontends: OpenSCAD, CadQuery, build123d

- **OpenSCAD** used CGAL Nef polyhedra (exact but very slow) for years. During
  2025 its development snapshots switched to the **Manifold** backend by default
  for large speed-ups. CGAL is still required for some operations, such as
  Minkowski sums
  ([OpenSCAD mailing list](https://lists.openscad.org/empathy/thread/TMJEJCZINIJNYJX2YF7IDNBAPQY66KIF),
  [issue #4825](https://github.com/openscad/openscad/issues/4825)).
  Users report occasional Manifold failures that CGAL handled, and STL exports
  that slicers flag for repair
  ([mailing-list reports](https://lists.openscad.org/empathy/thread/WC6UVRBYPBFHCXY5Z4BFBUOXWMTDSYAN)).
- **CadQuery and build123d** are Python APIs over OCCT (via OCP). They inherit
  OCCT's strengths and its failure modes.
- **Lesson.** A *programmatic, procedural* modelling front end, which is what
  UberTopo's procedural core is, does not need a NURBS kernel to be useful.
  OpenSCAD-style CSG on robust meshes covers a large share of building and
  infrastructure geometry.

---

## 3. Mesh-native kernels and libraries

### 3.1 CGAL

- A C++ computational geometry library. It is dual-licensed: GPL/LGPL (kernel
  and support libraries are mostly LGPL; most algorithms are GPL) or a
  commercial licence from GeometryFactory
  ([CGAL license](https://www.cgal.org/license.html)).
- **Relevant packages.**
  - Kernels: `Epick` (exact predicates, inexact constructions) and `Epeck`
    (exact predicates and constructions, lazy-exact).
  - **Nef polyhedra.** Exact, regularised Booleans that support non-manifold
    results, but orders of magnitude slow and memory-hungry.
  - **Polygon Mesh Processing corefinement.** Fast Booleans with exact
    predicates, but requires closed, non-self-intersecting inputs.
  - **2D Arrangements and 2D Snap Rounding.**
  - Constrained Delaunay triangulation.
  - **3D Alpha Wrapping.** Produces a watertight, intersection-free 2-manifold
    wrap of *any* triangle soup or point set, with controllable cavity size
    (α) and tightness (offset)
    ([CGAL Alpha_wrap_3](https://doc.cgal.org/latest/Alpha_wrap_3/),
    [Portaneri et al. 2022](https://history.siggraph.org/?p=128496)).
- **Assessment.** It is the reference for exact geometric computation (EGC).
  The GPL licensing of the most useful packages is the main obstacle for
  defense use. Alpha wrapping is *directly* relevant to building coarse,
  watertight acoustic proxies (see [tessellation-lod](tessellation-lod.md)). A
  CGAL-free reimplementation exists
  ([alecjacobson/alpha-wrap](https://github.com/alecjacobson/alpha-wrap)).

### 3.2 libigl

- An MPL-2.0 C++ geometry processing library. It hosts the *mesh arrangements*
  Booleans (Zhou et al. 2016) through CGAL exact kernels (that part inherits
  CGAL's licence), plus generalised winding numbers
  ([Mesh Arrangements project page](https://www.cs.columbia.edu/cg/mesh-arrangements)).
- It is excellent for research. It is not a production kernel.

### 3.3 Manifold (Emmett Lalish)

- Apache-2.0 C++, with JS/WASM, Python and C bindings. It is used by OpenSCAD,
  Blender 4.5+ and many web tools
  ([GitHub](https://github.com/elalish/manifold),
  [wiki](https://github.com/elalish/manifold/wiki/Manifold-Library),
  [intro blog](https://elalish.blogspot.com/2022/02/introducing-my-manifold-library.html)).
- **Foundations.**
  - Its Boolean derives from Julian Smith's topologically robust approach
    ([Smith & Dodgson 2007](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-766.pdf)).
    Each geometric decision is made once, and later decisions are derived from
    it, so the output is *topologically* manifold even when arithmetic is
    inexact.
  - Ties are broken by **symbolic perturbation**. "The Boolean does not account
    for any tolerance."
  - It uses an ε-validity notion: an output is ε-valid if some perturbation of
    its vertices within ε makes it geometrically valid.
  - It uses a parallel BVH broad phase (Thrust, CPU or GPU).
- **Attributes.** It carries per-vertex property channels interpolated
  through Booleans, plus `originalID`/`faceID` provenance. This is valuable for
  carrying material and acoustic attributes and for back-mapping results to
  procedural sources.
- **Limitations (from its own wiki).**
  - Manifold **input is required**. Blender's manual says the Manifold solver
    "will not work at all on non-manifold meshes"
    ([Blender manual](https://docs.blender.org/manual/en/5.0/modeling/modifiers/generate/booleans.html)).
  - There is no self-overlap removal.
  - Degenerate triangles can remain.
  - Marginal inputs can give geometrically wrong (but still manifold) output.
  - Once coordinates carry rounding error, symbolic perturbation "no longer
    applies".
  - Triangulating large polygons is O(n²).
- **Extras.** It provides `LevelSet` (SDF to mesh), smooth/refine, and the
  CrossSection 2D Booleans.
- **Assessment.** It is the best *drop-in* Boolean for closed triangle solids
  today: fast, permissive licence, actively maintained. It is not exact, and it
  is not designed for non-manifold or open surfaces. Terrain, roads and
  vegetation are open surfaces, which is most of UberTopo's world.

### 3.4 Blender's Booleans

- Blender has three solvers. **Float** is the old BMesh solver: fast, and
  fragile with coplanar faces. **Exact**, by Howard Trickey (2020), follows the
  mesh-arrangements approach with exact rational arithmetic and float filters
  *(implementation details from prior knowledge)*. It handles self-intersecting
  inputs and has a "hole tolerant" mode, but it is slow. **Manifold** was added
  in 4.5 LTS (2025)
  ([Blender 4.5 manual](https://docs.blender.org/manual/en/4.5/modeling/modifiers/generate/booleans.html),
  [CG Channel](https://www.cgchannel.com/2025/07/blender-4-5-lts-is-out-check-out-its-5-key-features),
  [Blender issue #120182](https://projects.blender.org/blender/blender/issues/120182)).
- **Lesson.** A mature production tool chose to keep **multiple Boolean
  back-ends**, each with a different contract (fast/manifold-only, exact/slow,
  float/fragile). UberTopo should do the same, behind one interface.

### 3.5 Research mesh-Boolean codes

These are covered in depth in [robustness](robustness-and-exact-arithmetic.md)
and [Booleans](booleans-and-operations.md).

- **Cherchi et al. 2022.** Interactive and robust mesh Booleans using indirect
  predicates. MIT licence. The repo requires manifold, watertight,
  self-intersection-free inputs, and the maintainers say they can no longer
  promise to address issues
  ([repo](https://github.com/gcherchi/InteractiveAndRobustMeshBooleans)).
- **EMBER (Trettner, Nehring-Wirxel, Kobbelt 2022).** Plane-based geometry with
  integer homogeneous coordinates. Exact and interactive on million-triangle
  meshes ([paper](https://www.graphics.rwth-aachen.de/publication/03339)).
- **Geogram mesh CSG (Lévy 2024/25).** Exact predicates and exact
  constructions. BSD-licensed
  ([arXiv 2405.12949](https://arxiv.org/abs/2405.12949v2),
  [geogram wiki](https://github.com/BrunoLevy/geogram/wiki/Exact)).

---

## 4. Implicit / SDF / field systems

| System | Foundation | Notes |
|---|---|---|
| **nTop** | Field-driven implicit modelling (SDF-based) | Claims B-reps become impractical at 10⁴ faces; implicits robust for offsets/lattices. Concedes implicit→B-rep export is hard ([nTop blog](https://ntopology.com/resources/blog/understanding-the-basics-of-b-reps-and-implicits/), [docs](https://docs.ntop.com/Product-Documentation/ntop/_concept/modeling)). Vendor claims, no independent benchmarks. |
| **libfive** | Matt Keeter, MPL-2.0 C++; expression DAG + interval arithmetic octree + dual contouring | Mature implicit kernel. |
| **Fidget** | Keeter, MPL-2.0 Rust; JIT-compiled expression evaluation, interval and (2025) Lipschitz/gradient pruning | "Experimental infrastructure" ([project](https://mattkeeter.com/projects/fidget), [gradients blog](https://www.mattkeeter.com/blog/2025-05-14-gradients/)). |
| **Curv** | Doug Moen; SDF functional language on GPU | Art/design oriented *(prior knowledge)*. |
| **Womp, Adobe Substance Modeler, Dreams, Claybook** | GPU SDF sculpting/scene engines | Show SDFs scale for interactive editing; output is meshes/voxels *(prior knowledge)*. |
| **Lipschitz Pruning** (Barbier et al., EG 2025) | Spatially-local pruned CSG trees for SDFs with thousands of primitives | Real-time sphere tracing; relevant to procedural scenes ([paper](https://diglib.eg.org/handle/10.1111/cgf70057)). |

**Strengths.** Booleans, blends, offsets and shells **cannot fail
topologically**. The result is always a valid volume. They suit procedural
generation and GPU evaluation.

**Weaknesses.**
- No explicit edges or faces, so there is no B-rep topology to attach attributes
  to, and no exact sharp features without special handling.
- Meshing is approximate and resolution-bound.
- Thin features vanish below the sampling resolution.
- Very large scenes need spatial culling of the expression tree.
- Precise dimensional control, such as a road exactly 7.3 m wide, is awkward
  unless the field is exact.

---

## 5. Volumetric engines

- **OpenVDB / NanoVDB** (Museth; ASWF; Apache-2.0 per current docs). A sparse,
  shallow, high-fan-out tree for narrow-band level sets and volumes. NanoVDB is
  a pointer-free, read-only, GPU-friendly linearisation
  ([OpenVDB about](https://openvdb.org/about),
  [NanoVDB paper](https://research.nvidia.com/labs/prl/nanovdb/nanovdb2021.pdf),
  [NVIDIA blog](https://developer.nvidia.com/blog/accelerating-openvdb-on-gpus-with-nanovdb)).
- This is the industry standard for film effects (smoke, water, level-set
  CSG). It is a strong candidate for UberTopo's **volumetric fields**:
  vegetation density, terrain voxel edits (caves, overhangs), and atmospheric
  or acoustic media. It also provides SDF-based coarse proxies.

---

## 6. Cross-cutting critique: what is 90s legacy

1. **Global or per-entity tolerances instead of consistency.** Parasolid, ACIS,
   OCCT and Rhino all decide "equal" with an ε. Equality under ε is not
   transitive, so a≈b and b≈c do not imply a≈c. Decisions made at different
   times can therefore contradict each other. This produces cracks, slivers,
   non-manifold glitches and Boolean failures. Tolerances then *grow* to paper
   over the contradictions.
2. **Sewing and healing as normal operations.** These exist because data
   exchange via IGES/STEP between different-tolerance kernels loses
   consistency. A procedurally *generated* world does not need to import
   inconsistent geometry, apart from survey data, which needs a separate path
   (see [representations](representations.md)). UberTopo can avoid healing
   altogether if it is consistent by construction.
3. **Exact NURBS as the only first-class geometry.** It is ideal for mechanical
   parts. It is mostly overkill for environments, where 95%+ of surfaces are
   planar (buildings, roads in cross-section), heightfields (terrain), or
   statistical (vegetation, rubble).
4. **Single-threaded, pointer-heavy, mutable object graphs.** These designs
   predate multicore CPUs and GPUs. Modern systems (Manifold, EMBER, NanoVDB)
   use flat arrays, index-based topology and data-parallel algorithms.
5. **Fixed size box / unitless precision.** Parasolid's 1 km box at 1e-8 is the
   clearest example of a design assumption that is wrong for km-scale
   environments.
6. **History-based parametric modelling tightly coupled to the kernel**
   (persistent naming problems). UberTopo's procedural graph should own history.
   The kernel should be a pure evaluator of (inputs) → (geometry + provenance).

## 7. Summary table

| Kernel | Repr. | Robustness | Licence | Scale fit | Verdict |
|---|---|---|---|---|---|
| Parasolid | NURBS B-rep | tolerant, local tol. | commercial | 1 km box | Not suitable (cost, closed, scale) |
| ACIS / ASM | NURBS B-rep | tolerant | commercial | — | Not suitable |
| CGM / Granite | NURBS B-rep | tolerant | commercial/closed | — | Not available |
| C3D | NURBS B-rep | tolerant | commercial (RU) | — | Procurement risk |
| SMLib / USD BRep | NURBS B-rep | tolerant | Apache-2.0 (2026) | — | Evaluate as NURBS adapter |
| OCCT | NURBS B-rep | tolerant + fuzzy | LGPL-2.1+exc | — | Edge adapter (STEP, analytic eval) |
| Truck / Fornjot | NURBS B-rep (Rust) | tolerant | Apache/MIT-ish | — | Reference only / dead |
| CGAL | exact mesh/Nef/arrangements | EGC | GPL/LGPL/comm. | good (local) | Algorithms reference; alpha-wrap |
| Manifold | triangle mesh | topological + symbolic perturbation | Apache-2.0 | good | Adopt for closed solids |
| EMBER / plane-based | polygon mesh | exact integer | research | per-tile | Adopt the *technique* |
| Cherchi / Attene | triangle mesh | indirect predicates | MIT | good | Reference/benchmark |
| Geogram | triangle mesh | exact + expansions | BSD-3 | good | Predicates/CDT source |
| libfive / Fidget | implicit | interval arithmetic | MPL-2.0 | culling needed | Borrow ideas for SDF layer |
| OpenVDB/NanoVDB | sparse voxels | n/a | Apache-2.0 | excellent | Adopt for volumes |

## References

- Tech Soft 3D, Parasolid: https://www.techsoft3d.com/products/parasolid
- Engineering.com, Parasolid/D-Cubed: https://mobile.engineering.com/amp/17309.html
- Altair Feko, Parasolid model extents: https://2021.help.altair.com/2021/feko/topics/feko/user_guide/cadfeko/model_extents_feko_c.htm
- Bentley MicroStation advanced unit settings: https://docs.bentley.com/LiveContent/web/MicroStation%20Help-v28/en/AdvancedUnitSettings.html
- Bentley wiki, solids working area: https://communities.bentley.com/products/building/building_analysis___design/w/building_analysis_and_design__wiki/10507/bim-solids-working-area-resolution
- Jackson, Boundary representation modelling with local tolerances (1995): https://ftp1.cs.wisc.edu/pub/users/prem/jackson-SM-95.pdf
- KeyCreator tolerant modelling: https://help.kubotekkosmos.com/keycreator/2024SP1/A10196
- Engineering.com, Spatial/ACIS/CGM: https://www.engineering.com/?p=129684
- TransMagic, which kernel: https://transmagic.com/which-geometric-modeling-kernel/ and https://transmagic.com/which-kernel
- NVIDIA SMLib manual: https://docs.nvidia.com/smlib/manual/smlib/geometry/index.html
- USD BRep open-source news: https://alphasignal.ai/news/nvidia-opens-usd-brep-s-cad-kernel-to-developers-under-apache-2-0 ; https://pbxscience.com/?p=24995
- OCCT licence: https://dev.opencascade.org/doc/overview/html/occt_public_license.html ; https://spdx.org/licenses/OCCT-exception-1.0.html
- OCCT gluing options: https://dev.opencascade.org/content/gluing-options-boolean-component
- Zoo Design Studio v1: https://zoo.dev/blog/zoo-design-studio-v1 ; https://zoo.dev/geometry-engine
- Fornjot: https://github.com/hannobraun/fornjot
- Truck: https://github.com/ricosjp/truck ; monstertruck: https://docs.rs/monstertruck
- OpenSCAD Manifold default: https://lists.openscad.org/empathy/thread/TMJEJCZINIJNYJX2YF7IDNBAPQY66KIF
- CGAL licence: https://www.cgal.org/license.html ; Alpha wrap: https://doc.cgal.org/latest/Alpha_wrap_3/
- Manifold: https://github.com/elalish/manifold ; wiki: https://github.com/elalish/manifold/wiki/Manifold-Library
- Smith & Dodgson / Smith thesis: https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-766.pdf
- Blender Boolean manual: https://docs.blender.org/manual/en/4.5/modeling/modifiers/generate/booleans.html
- Mesh arrangements: https://www.cs.columbia.edu/cg/mesh-arrangements
- Cherchi et al. code: https://github.com/gcherchi/InteractiveAndRobustMeshBooleans
- EMBER: https://www.graphics.rwth-aachen.de/publication/03339
- Lévy mesh CSG: https://arxiv.org/abs/2405.12949v2
- nTop: https://ntopology.com/resources/blog/understanding-the-basics-of-b-reps-and-implicits/
- Fidget: https://mattkeeter.com/projects/fidget
- Lipschitz pruning: https://diglib.eg.org/handle/10.1111/cgf70057
- OpenVDB: https://openvdb.org/about ; NanoVDB: https://research.nvidia.com/labs/prl/nanovdb/nanovdb2021.pdf
