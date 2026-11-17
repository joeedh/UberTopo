# 07 — Embedding geometry in topology

Part of [B-rep topology research](README.md). This file covers how curves and surfaces
attach to edges and faces, pcurves vs. 3D curves, procedural and transcendental curves
approximated by B-splines, mesh-backed faces, tolerant modeling, and large coordinates.

The requirements stress embedding *plane/space curves and surface data* into the
B-rep (for example, to tessellate road networks). They also say curves will often be
**numerically integrated transcendental curves** (spiros, polynomial clothoids,
clothoids) approximated by polynomial B-splines.

## 1. The attachment points

```
Vertex  ── point (3D)                                     [+ tolerance]
Edge    ── curve C(t), t ∈ [t0, t1] (3D), optional         [+ tolerance]
Coedge  ── pcurve p(s) in the face's (u,v) domain          (one per face-edge incidence)
Face    ── surface S(u,v) + sense (normal flip)            [+ tolerance]
Region  ── medium / field reference (not geometry per se)
```

Why the **coedge** is the right owner of the pcurve: an edge shared by *k* faces needs
*k* parameter-space curves, one per adjacent surface. The coedge is exactly the
(edge, face) incidence. OCCT puts pcurves on the edge, in a list keyed by
(surface, location), and must search that list. That is a symptom of not having a
coedge entity. Parasolid's fins and ACIS's coedges own their pcurves directly.

## 2. 3D curves vs. pcurves: which is primary?

| | 3D curve primary | pcurve primary |
|---|---|---|
| Typical | Edges between two exact analytic faces (plane ∩ plane = line) | Edges *defined on* a surface: trimming curves of NURBS faces, roads draped on terrain |
| Good for | Edge-based queries (diffraction edges, length, curvature), interop with STEP 3D | Point-in-face, loop nesting, tessellation, imprinting, all done in 2D |
| Weakness | Projecting onto each surface for 2D work (approximate) | 3D shape depends on the surface. Two faces' pcurves disagree in 3D by up to the tolerance |

Classic kernels treat the 3D curve as primary when it exists, require pcurves for
curved faces (OCCT effectively always), and allow "tolerant" edges that have *only*
pcurves (Parasolid SP-curves). The disagreement between representations is then
**bounded by the edge tolerance**. This is the "same parameter" property in OCCT.

**Recommendation:** allow either to be primary per edge, with an explicit flag:

* `Edge.curve` is optional. `Coedge.pcurve` is optional for planar faces (it is
  derivable as an affine projection, so compute and cache it) and required otherwise.
* At least one exact or certified representation must exist, and the rest are derived
  with a recorded error bound ≤ the edge tolerance.
* Invariant: for every coedge, ‖S_f(p(s)) − C(t(s))‖ ≤ tol(E) at sampled parameters.
  `t(s)` is the reparameterization, identity when "same parameter".

## 3. Road networks: a case study for "the pcurve is the truth"

A road centerline is designed in **plan** (2D, horizontal alignment: lines, circular
arcs, clothoid transitions) plus a **profile** (vertical alignment: grades and
parabolic vertical curves) as functions of **station** (arc length along the plan
curve). The road surface is a sweep of a cross-section (lanes, crown or superelevation,
shoulders, curbs) along that alignment. The terrain is a heightfield or a
triangulated irregular network (TIN), whose natural parameter domain *is* the plan
(x, y).

So:

* The plan curve lives in the terrain face's parameter domain. **It is literally a
  pcurve**, and the 3D centerline is derived: S_terrain(p(s)) + profile adjustments.
* Imprinting the road edges onto terrain is a **2D arrangement in the terrain's
  parameter domain**. That is robust and fast. A 3D surface-surface intersection is
  neither.
* The road surface face's own parameterization is (station, offset). Its pcurves for
  edges shared with the terrain (the daylight lines where cut/fill slopes meet the
  ground) come from a 2D-2D correspondence.

Implications for topology:

* The **coedge-owned pcurve** with a pluggable curve type is essential.
* Edges need a **native parameter** (station/arc length) separate from the B-spline
  knot parameter. Store a monotone reparameterization map (a piecewise polynomial or
  a small table) on the edge. Station values are user-facing semantics: "sensor at
  station 1+250 on road R7".
* Faces need a **native parameterization** too: (station, offset) for road surfaces,
  and (x, y) for terrain.

## 4. Transcendental curves (clothoids, spiros) → B-splines

Clothoids (Euler spirals, with curvature linear in arc length), polynomial spirals
(curvature polynomial in arc length), and Levien's spiros have no closed-form
polynomial representation. Their positions are Fresnel-type integrals.

**Layered geometry per edge:**

```
Level 0  Definition    : procedural (e.g. clothoid: start pos, heading, κ0, κ', length)
                          — exact evaluator via numerical integration / series / Fresnel
Level 1  Certified approx: polynomial B-spline / NURBS with max deviation ε₁ (proved or sampled+bounded)
Level 2  Tessellation   : polyline at LOD-dependent chord tolerance ε₂ ≥ ε₁
```

* The topology references a **curve object** that holds all levels. Algorithms ask
  for what they need: exact evaluation (for stationing and design checks), a B-spline
  (for intersection and offset, using standard NURBS algorithms), or a polyline (for
  rendering, acoustics, and coarse arrangements).
* **Edge tolerance ≥ ε₁.** The approximation error must be part of the tolerance
  budget. This is how tolerant modeling (Jackson 1995) and procedural curves fit
  together.
* **Offsets of clothoids** (lane edges, curbs) are *not* clothoids. Define them
  procedurally ("offset d of curve C"). Their B-spline approximation is computed from
  the definition, not by offsetting the approximation, so the error does not compound.
* **G2 continuity across segments:** this is the reason to use clothoids at all.
  Contiguous alignment segments become separate edges joined at vertices, with
  continuity flags (G0/G1/G2) as edge-junction attributes. Alternatively, one edge per
  alignment carries a multi-segment curve. Prefer **one edge per topologically
  meaningful span** (between junctions or road features) with a multi-segment curve
  inside. Otherwise every design-segment boundary becomes a vertex that fragments
  faces.

The sibling research folder [`../curves/`](../curves/) (if present) covers the curve
mathematics. Topology needs only the interface: `eval`, derivatives, bounds, native
↔ approximation parameter maps, the certified error, and a tessellation at tolerance.

## 5. Surfaces: kinds the topology must accept

| Surface kind | Domain examples | Notes |
|---|---|---|
| Plane | Walls, floors, roofs (most building faces) | Fast paths everywhere. Pcurves are affine. Exact predicates practical |
| Quadrics, tori | Tanks, columns, domes, pipes | Periodic: no seams (see [03 §3.1](03-faces-with-holes.md)) |
| B-spline / NURBS | Free-form roofs, terrain fitted patches, approximated sweeps | Standard |
| Procedural sweep | Road surfaces (cross-section × alignment), embankments, tunnels | Exact evaluator plus certified B-spline approximation, as for curves |
| Heightfield / TIN | Terrain at all scales | Parameter domain = plan (x, y). Can be huge, so it must be **tiled and lazily evaluated** |
| Mesh-backed ("facet") | Photogrammetry, lidar, scanned buildings, far LODs, vegetation | A face whose geometry is a triangle mesh patch (Parasolid "convergent modeling" does this) |
| Implicit / SDF | Rocks, blobs, procedural vegetation volumes | Evaluate, project, and tessellate via marching methods. Often better as *included bodies* than as merged faces |

**Mesh-backed faces deserve emphasis.** Much of the real-world input for site surveys
will be meshes (photogrammetry and lidar). Converting them to NURBS is pointless.
Keeping them as separate meshes loses topology (no shared edges with designed
geometry, no regions). A **face whose surface is a triangle-mesh patch**, with
boundary edges whose curves are polylines lying on that mesh, lets scanned terrain
share edges with a designed road and lets a scanned building bound a region. The
"pcurve" on a mesh face is a polyline in barycentric or chart coordinates, or simply a
3D polyline constrained to lie on the mesh within tolerance.

## 6. Coordinates, scale, and precision

Environment models span tens of kilometres with millimetre detail. A 20 km extent at
1 mm resolution needs about 25 bits of mantissa. `f32` (24 bits) is insufficient
globally and marginal locally. `f64` (53 bits) is fine.

* Store geometry in **f64 tile-local frames**. Each tile has an origin in a global
  projected coordinate reference system (CRS), for example UTM or a local tangent
  plane. Compute in f64 tile-local coordinates. Export `f32` relative to a
  camera or tile origin for GPUs.
* **Do not put transforms in topology** (no OCCT-style per-sub-shape `Location`).
  Transforms live at tile level (frames) and object level (instances of included
  bodies, [04 §5.3](04-surface-vs-volumetric.md)).
* Geodesy (Earth curvature over 10–20 km is about 8–30 m of drop) matters for
  long-range line of sight and acoustics. Handle it in the CRS and tile frame, not in
  topology.

## 7. Tolerant modeling

* **Per-entity tolerances** (vertex, edge, face) with nesting tol(V) ≥ tol(E) ≥
  tol(F) for incident entities (OCCT's rule; Jackson's Parasolid design).
* Tolerances grow only through explicit operations (sewing imported data, merging
  coincident items, approximation), and **each growth is journaled**. That lets you
  find where precision was lost.
* Tolerances should be **scale-aware**: GIS imports (OSM footprints, ±0.5–2 m) and
  CAD-quality designed geometry (±0.01 mm) coexist. A designed road meeting a GIS
  terrain needs a tolerance at the junction that reflects the *worse* input, without
  polluting the designed side. Local tolerances support exactly that.
* **Region classification under tolerance:** a point within tol of a face is "on"
  the boundary. Simulation exports must choose deterministically (for example, the
  higher-priority medium wins). This is a policy, declared per export.

## 8. Geometry ownership and sharing

* Geometry objects (curves, surfaces) live in their **own arenas**, separate from
  topology, and are referenced by index. Many edges can share one curve (a road edge
  split by junctions shares the alignment curve with different [t0, t1]). Many faces
  can share one surface (all coplanar faces of a facade share a plane record).
* Sharing makes "coplanar" and "same curve" **exact identity checks** instead of
  tolerance tests. That is valuable for merging and for arrangements on architectural
  data.
* Geometry is immutable once referenced by a frozen version. Edits create new geometry
  records. This fits the chunked persistence model ([06 §5](06-memory-layout-performance.md)).

## 9. Summary of recommendations

1. Pcurves live on **coedges**. 3D curves live on edges. Either may be primary per
   edge, and the derived one carries a certified error ≤ tolerance.
2. Curves and surfaces are **layered**: definition (possibly procedural or
   transcendental) → certified B-spline approximation → tessellation.
3. **Native parameterizations** (station/offset, plan x/y) are stored alongside
   approximation parameters.
4. Faces may be **mesh-backed**, so scanned and designed geometry can share topology.
5. **No transforms inside topology.** Use f64 tile frames plus object-level instancing.
6. Use **local, journaled tolerances** with nesting rules.
7. Geometry lives in shared, immutable arenas, so coplanarity and co-curvature are
   identity checks.

## References

* D. J. Jackson, local tolerances (Parasolid), SMA 1995 — https://ftp1.cs.wisc.edu/pub/users/prem/jackson-SM-95.pdf
* OCCT BRep format and tolerances ("same parameter", pcurves): https://dev.opencascade.org/doc/overview/html/specification__brep_format.html ; shape healing: https://dev.opencascade.org/doc/overview/html/occt_user_guides__shape_healing.html
* L. Piegl, W. Tiller, *The NURBS Book*, 2nd ed., Springer 1997.
* R. Levien, *From Spiral to Spline: Optimal Techniques in Interactive Curve Design*, PhD thesis, UC Berkeley, 2009 — https://levien.com/phd/phd.html
* D. J. Walton, D. S. Meek, "A controlled clothoid spline", *Computers & Graphics* 29, 2005 — https://doi.org/10.1016/j.cag.2005.03.008
* E. Bertolazzi, M. Frego, "G1 fitting with clothoids", *Math. Methods in the Applied Sciences*, 2015 — https://arxiv.org/abs/1305.6644 ; Clothoids library: https://github.com/ebertolazzi/Clothoids
* OpenDRIVE (road description with clothoid/spiral geometry, station-based): https://www.asam.net/standards/detail/opendrive/
* LandXML alignments (stationing, spirals): http://www.landxml.org/
* Parasolid convergent modeling (facet + classic B-rep), Siemens: https://www.plm.automation.siemens.com/global/en/products/plm-components/parasolid.html
