# Recommendations: representation, robustness, borrow vs build

Part of [CAD kernels, representations and robustness](README.md).

This file pulls together the findings of the
[kernel survey](kernel-survey.md), [representations](representations.md),
[robustness](robustness-and-exact-arithmetic.md),
[operations](booleans-and-operations.md) and
[tessellation / LOD](tessellation-lod.md) into a concrete proposal. It is
*research input* to a design. It is not itself a plan. Per CLAUDE.md, a plan
derived from it must go in `docs/plans/` and be pressure-tested.

---

## 1. Core thesis

> **Do not build (or adopt) a general-purpose NURBS B-rep CAD kernel.**
> Build a **procedural evaluator** whose integration representation is an
> **exact, plane-based, polyhedral, non-manifold B-rep with holes and
> attributes**, organised in **tile-local integer frames**, with analytic
> carriers (curves/surfaces) attached as attributes and **sparse volumes** and
> **heightfields** as peer representations for natural content.

Reasoning:

1. **The industrial kernels solve a different problem.**
   - Their scale is a 1 km box at 1e-8 (Parasolid).
   - They are built for 10²–10⁴-face parts, not 10⁷-face environments.
   - Their robustness comes from tolerances, with healing as a fallback.
   - Their hardest-won features (NURBS SSI, rolling-ball fillets) are the ones
     UberTopo needs least.
   - The open-source attempts to rebuild that design either stalled (Fornjot)
     or inherited the same tolerance problems (Truck, SolveSpace, OCCT).
2. **The geometry of environments is mostly planar, 2.5D, swept or
   statistical.** Polyhedral exactness is achievable for planar content. As of
   2021–2025 exact polyhedral Booleans are also *fast*: EMBER is
   interactive on million-triangle meshes, Cherchi et al. are interactive at
   200K triangles, and Nehring-Wirxel reports 2.5 M plane cuts per second per
   core.
3. **Procedural generation makes "consistent by construction" possible.**
   - No healing is needed for generated content.
   - LOD becomes re-evaluation rather than decimation.
   - Generators can emit **planes** directly, which is exactly the input form
     of the plane-based exact methods.
4. **Simulation consumers need validity more than accuracy.**
   - Watertight acoustic models, consistent LOS, and deterministic
     reproducible worlds.
   - Exact integer arithmetic gives determinism and validity at once.

## 2. Representation stack (proposed)

| Layer | Representation | Purpose | Authority |
|---|---|---|---|
| L0 | Procedural graph (rules + params + seeds + GIS inputs) | Source of truth | **Authoritative** |
| L1a | Analytic carriers: transcendental curves (clothoid/Spiro), catenaries, surfaces of revolution, optional NURBS via adapter | Exact shape intent for curved things; re-tessellation | Derived from L0; attached to L2 edges/faces |
| L1b | 2D exact arrangements per tile (roads, parcels, footprints, land cover, shorelines) | Plan-view topology | Derived |
| L2 | **Plane-based polyhedral non-manifold B-rep** (faces with holes, regions/3-cells, integer homogeneous coordinates per tile) | Integration layer; Booleans; attributes; acoustic edges | Derived; exact |
| L3a | Heightfield / adaptive TIN with breaklines (exact CDT) | Terrain | Derived; stitched to L2 via shared breaklines |
| L3b | Sparse volumes (OpenVDB/NanoVDB): vegetation density, media, SDF edits, clearance volumes | Natural & statistical content | Derived |
| L4 | Consumer tessellations (render cluster DAG, acoustic polygon model + diffracting edges, LOS BVH, collision capsules) | Outputs | Caches |

The B-rep data structure itself (half-edge or radial-edge, 2- or 3-manifold) is
owned by the topology research track. The constraints this track places on it:

- It must store **planes as first-class entities**, shared by reference so that
  coplanarity is intent rather than coincidence.
- **Vertices may be implicit** (plane triples), with cached rounded doubles.
- Faces need **inner loops** (holes).
- It needs **regions/3-cells** for arrangement output.
- It needs **provenance ids** on every face and edge.
- It needs **attribute slots for analytic carriers**.

## 3. Robustness strategy (proposed)

1. **No ε in topological decisions.** Tolerances exist only as explicit
   modelling or LOD operations.
2. **2D:** fixed-point integer coordinates (int64), exact predicates, and
   exact intersection, with 2D snap rounding where export to a grid is needed.
   Clipper2 handles offsets and clipping. An exact-predicate CDT handles
   terrain.
3. **3D:** plane-based geometry with integer homogeneous coordinates.
   - About 26-bit input lattice per tile, with ≤256-bit fixed-width predicate
     arithmetic.
   - Arrangement-based N-ary Booleans with winding-number or label
     classification (EMBER / Nehring-Wirxel family).
4. **Coordinates.**
   - Geodetic anchor, then a site ENU/projected frame (double), then tile
     integer lattices (e.g. 0.1 mm over about 6 km, or 1 mm over about 60 km).
   - Cross-tile seams lie on shared integer planes.
   - Camera-relative float only at the GPU boundary.
   - Correct for Earth curvature in long-range LOS and acoustic queries.
5. **Symbolic perturbation only for accidental degeneracies.** Intentional
   contact (shared walls) is encoded as shared plane ids.
6. **Determinism is a tested property.** The same seed and inputs give
   bit-identical L2 on every platform. Add CI tests that hash L2 output.
7. **Fuzz and differential testing.** Run random procedural buildings through
   several Boolean back-ends (own exact, Manifold, CGAL Nef as an offline
   oracle) and compare volumes and topology.

## 4. Borrow vs build

| Component | Recommendation | Candidate | Licence | Rationale |
|---|---|---|---|---|
| Exact predicates (orient2d/3d, incircle) | **Borrow** | Shewchuk (public domain) / Geogram PCK (BSD-3) / Rust `robust` | permissive | Tiny, proven |
| 2D clipping & offsetting | **Borrow** | Clipper2 | Boost | Integer, fast, robust |
| 2D CDT | **Borrow or build** | Triangle (non-free for commercial *(prior knowledge)*), Geogram CDT (BSD), CDT libs, `spade` (Rust) | varies | Must accept exact constraints; check licences |
| 2D arrangement / snap rounding | **Build** (on exact predicates) | CGAL as reference oracle only (GPL) | — | Central to roads; needs our attributes |
| 3D plane-based exact Booleans | **Build** (following EMBER / Nehring-Wirxel papers) | Papers + `zalo/ember-csg` as reference | — | Core differentiator; no permissive production implementation exists; needs our non-manifold B-rep and provenance |
| Fast float Booleans for closed visual assets | **Borrow** | Manifold | Apache-2.0 | Fast, maintained, attribute provenance |
| Robust Booleans on dirty imported meshes | **Borrow (optional)** | Cherchi et al. (MIT) / Geogram mesh CSG (BSD) | permissive | For ingesting soups |
| Watertight wrapping of scans/soups | **Borrow** | CGAL Alpha_wrap_3 (GPL) or `alecjacobson/alpha-wrap` reimplementation | GPL / check | Acoustic proxies from scans; licence decides which |
| Sparse volumes | **Borrow** | OpenVDB / NanoVDB | Apache-2.0 | Industry standard |
| Implicit/SDF evaluation | **Build small**, borrow ideas | libfive / Fidget (MPL-2.0) | MPL | Need integration with procedural graph; MPL file-level copyleft is workable |
| NURBS evaluation / STEP-IFC import | **Adapter** | OCCT (LGPL-2.1+exc) or SMLib/USD BRep (Apache-2.0, Oct 2026) | — | Keep out of core; tessellate on import |
| Mesh simplification / clustering / visual LOD | **Borrow** | meshoptimizer (MIT) | MIT | Nanite-style DAG building blocks |
| Subdivision | **Borrow if needed** | OpenSubdiv | Apache-2.0 | Organic assets only |
| Transcendental curves | **Build** | (other research track) | — | Domain-specific |

**Licences to avoid in the core:**

- GPL (most of CGAL, Blender code, SolveSpace), if the project may be
  distributed in proprietary or classified form. Note that GPL obligations
  apply on *distribution*. Internal government use may be acceptable, but
  vendor or partner sharing changes this.
- Commercial kernels (Parasolid, ACIS, CGM, C3D) on cost and openness grounds.
- C3D on supply-chain grounds.
- LGPL OCCT is acceptable as a dynamically linked *adapter*.

## 5. Language

| Consideration | C++ (C++20/23) | Rust |
|---|---|---|
| Ecosystem to borrow | **Strong**: Manifold, OpenVDB, Geogram, Clipper2, meshoptimizer, OCCT, CGAL (oracle) are all native | Thinner: Fidget, Truck, `robust`, `spade`, Clipper2 ports; OpenVDB only via FFI |
| Safety for a long-lived kernel | Weak by default; sanitizers & discipline | **Strong**: memory safety, no data races; good for parallel evaluation of procedural graphs |
| Wide fixed-width integers (256-bit) | Hand-rolled or `boost::multiprecision` fixed | `crypto-bigint`/`ruint`-style fixed-width crates; const generics help |
| GPU / SIMD | CUDA, SYCL, mature | wgpu/rust-gpu maturing; CUDA via FFI |
| Determinism | Achievable; watch compiler FP flags | Achievable; FP flags less surprising |
| Modern kernel precedents | Manifold, EMBER, Zoo engine *(language unverified)* | Fornjot (stopped), Truck, Fidget |
| Python bindings for procedural authoring | pybind11 / nanobind | PyO3 |

**Assessment.** Both are viable, because the core exact algorithms are
self-contained integer code. Decide on the size of the *borrowed* set:

- If OpenVDB, Manifold and OCCT are first-class dependencies, **C++** avoids
  FFI friction.
- If the core is mostly built in-house and only OpenVDB/OCCT sit at the edges
  via FFI, **Rust** gives better long-term safety for parallel procedural
  evaluation.

Fornjot's shutdown was about scope (a general MCAD kernel), not language. This
is the owner's call (see open questions).

## 6. Suggested phasing (input to a future plan)

1. **Foundations.**
   - Coordinate frames and tiling.
   - Fixed-point types and 256-bit predicate arithmetic, with exhaustive
     tests.
   - Exact predicates.
   - Determinism test harness.
2. **2D layer.** Exact 2D arrangement with attributes, Clipper2 offsets, CDT
   with breaklines, snap rounding. Demo: road network plus parcels plus
   terrain TIN for a 5 km tile.
3. **L2 B-rep plus plane-based Booleans.**
   - Start with the Nehring-Wirxel octree-BSP or EMBER-style local
     arrangements on convex polygons.
   - Output a cell complex with provenance.
   - Demo: procedural buildings with openings at LoD1–3.
4. **Sweeps.** Clothoid alignments → road strips → terrain conformance
   (cut/fill to daylight lines) → junctions from the arrangement.
5. **Volumes.** OpenVDB integration: vegetation density, clearance volumes,
   SDF terrain edits meshed into L2/L3.
6. **Consumer profiles.** Acoustic polygon model plus diffracting edges; LOS
   BVH with thin-obstacle capsules; render cluster DAG.
7. **Adapters.** STEP/IFC via OCCT or SMLib; LiDAR/photogrammetry via alpha
   wrap and semantic fitting.

## 7. Risks

- **Plane-based exact Booleans have no permissive production library.**
  UberTopo would own an implementation of research-grade algorithms.
  Mitigations: follow the papers closely; differential-test against Manifold
  and CGAL; keep Manifold as a fallback back-end behind the same interface.
- **Quantisation artifacts.** Snapping procedural inputs to a 0.1 mm lattice
  can make intended-planar quads non-planar if they are emitted as vertices.
  Generators must emit planes, or triangulate.
- **Curved content at high visual LOD.** Domes, tanks and curved façades are
  faceted in L2. Use carrier attributes for smooth normals and displacement at
  render time. Boolean results on curved parts are only as good as the facet
  LOD at which they are computed, so recompute per LOD from L0.
- **Export rounding.** Rounded double or float outputs can contain tiny
  self-intersections. Consumers must never feed them back as Boolean inputs.
- **Acoustic edge semantics.** Diffraction modelling needs well-defined
  wedges. Merging coplanar faces and dropping small features must preserve
  significant edges. This needs validation with the acoustic simulator team.
- **Scope creep toward MCAD.** Fillets, general NURBS and drafting should be
  out of scope unless a concrete requirement appears.

## 8. Open questions for the project owner

1. **Distribution and licensing.** Will UberTopo be distributed outside the
   organisation (partners, vendors, export)? This decides whether GPL
   components (CGAL alpha wrap, Nef as oracle) are acceptable even as tools,
   and whether LGPL OCCT is fine.
2. **Supply-chain / ITAR / export-control constraints.** These would rule out
   some vendors (C3D) and may affect cloud use.
3. **Language preference** (C++ vs Rust), and expected team size and skills.
4. **Required accuracy and extent.** Largest site size? Smallest feature that
   matters (mm for camera close-ups, or cm)? This sets the tile lattice
   resolution and size.
5. **Acoustic simulator.** Which acoustic method(s) will consume the geometry:
   ray/image-source with BTM/UTD edge diffraction, wave-based FDTD, or
   parabolic-equation outdoor propagation? This decides edge export and the
   acoustic LOD profile.
6. **Imported CAD/BIM.** How much exact CAD/BIM (IFC with NURBS, STEP) must be
   ingested? This decides whether a NURBS adapter (OCCT vs SMLib) is phase 1
   or later.
7. **Real-site capture.** Will real sites be captured (LiDAR,
   photogrammetry, splats) and mixed with procedural content? This decides the
   priority of the scan ingest path.
8. **Determinism.** Is cross-platform bit-identical generation a hard
   requirement, e.g. for test certification? The recommendation assumes yes.
9. **RF/radar.** Is radar or RF simulation in scope later? It adds curvature
   and edge requirements similar to acoustics, but at different wavelengths.

## References

See the per-topic files. Key sources:

- EMBER: https://www.graphics.rwth-aachen.de/publication/03339
- Nehring-Wirxel et al. 2021: https://arxiv.org/abs/2103.02486v1
- Campen & Kobbelt 2010: https://www.graphics.rwth-aachen.de/media/papers/campen_2010_eg_021.pdf
- Cherchi et al. 2022: https://arxiv.org/abs/2205.14151v1
- Lévy 2024/25: https://arxiv.org/abs/2405.12949v2
- Manifold: https://github.com/elalish/manifold
- Clipper2: https://github.com/AngusJohnson/Clipper2
- OpenVDB: https://openvdb.org/about
- CGAL licence: https://www.cgal.org/license.html
- OCCT licence: https://dev.opencascade.org/doc/overview/html/occt_public_license.html
- SMLib / USD BRep: https://docs.nvidia.com/smlib/manual/smlib/geometry/index.html
- Parasolid size box: https://2021.help.altair.com/2021/feko/topics/feko/user_guide/cadfeko/model_extents_feko_c.htm
- Fornjot shutdown: https://github.com/hannobraun/fornjot
- meshoptimizer: https://github.com/zeux/meshoptimizer
