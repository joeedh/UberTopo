# Surfaces Built from Curves, and Curve/Surface Data in the B-rep

Part of the [curves research set](README.md). How road corridors, embankments,
riverbeds, kerbs, tunnels and similar surfaces are generated from curves (sweeps,
lofts, ruled surfaces, Coons/Gordon patches), how they interact with terrain, and
how to store curve and surface geometry — including curves in a surface's
parameter space (pcurves) — as attribute data on a non-manifold B-rep.

> Source note: page fetches were blocked; references located by web search or from
> standard CAGD literature.

---

## 1. Surface constructions from curves

### 1.1 Sweeps (generalised cylinders)

Given a directrix $\mathbf r(s)$, a frame $(\mathbf t,\mathbf u,\mathbf v)(s)$ and a
(possibly station-dependent) profile $\mathbf p(s, q) = (p_u(s,q), p_v(s,q))$,

$$
\mathbf S(s,q) = \mathbf r(s) + p_u(s,q)\,\mathbf u(s) + p_v(s,q)\,\mathbf v(s).
$$

Variants: constant profile (pipes), scaled/morphing profiles (tapering channels),
profiles defined as *programs* evaluated per station (road templates). The frame
choice is discussed in [space-curves-and-frames.md](space-curves-and-frames.md):
gravity-referenced for roads/rail, RMF for pipes.

Local validity: the Jacobian $\partial_s\mathbf S\times\partial_q\mathbf S$ vanishes
when profile points reach the centre of curvature (in the frame's plane):
$1 - p_u\kappa_u - p_v\kappa_v = 0$, where $\kappa_u,\kappa_v$ are the curvature
components along $\mathbf u, \mathbf v$. Sweeps must be checked and split there.

**IFC 4.3** standardises exactly this: `IfcSectionedSolidHorizontal` /
`IfcSectionedSurface` / `IfcSectionedSolid` place a list of cross-section profiles
at distances along a directrix (alignment) and interpolate between them;
`IfcDirectrixCurveSweptAreaSolid` sweeps a constant profile. Profiles must be
topologically compatible (same number of points/edges) for interpolation.

### 1.2 Road corridor surfaces in $(s,t)$

Using OpenDRIVE conventions (reference line, superelevation $\phi(s)$, lane
border functions $t = b_k(s)$, lateral shape $h(s,t)$):

$$
\mathbf S(s,t) = \mathbf r_h(s) + z(s)\,\mathbf e_z + t\,\mathbf u(s) + h(s,t)\,\mathbf v(s),
$$

with $\mathbf u,\mathbf v$ the superelevated gravity frame. Key property: **the
road surface is naturally parameterised by road coordinates $(s,t)$**. Lane
borders, road markings, kerb lines and object footprints are *graphs*
$t = b(s)$ in this parameter space — exact, trivially evaluable pcurves, no
approximation and no projection needed. This is the strongest argument for
storing road surfaces as $(s,t)$-parameterised procedural surfaces in the kernel
rather than as NURBS patches.

Between two lane borders with linear cross-slope (no lateral shape), the lane
surface is a **ruled surface** between two 3D boundary curves, with rulings along
the cross-section direction $\mathbf u(s)$:

$$
\mathbf S(s,\lambda) = (1-\lambda)\,\mathbf B_k(s) + \lambda\,\mathbf B_{k+1}(s),\quad
\mathbf B_k(s) = \mathbf S(s, b_k(s)).
$$

Because both borders are parameterised by the *same* station $s$, rulings are
exactly the cross-section lines — the right correspondence (parameterising borders
by their own arc lengths gives skewed rulings — a classic bug).

Typical cross-section elements (template "links"), each a ruled strip: lanes,
shoulders, kerb face (near-vertical strip, $h$ jump), gutter, sidewalk (often
`level` — not superelevated), verge, ditch (foreslope/backslope), cut/fill side
slopes to daylight, medians, barriers (swept profiles), retaining walls.

### 1.3 Lofts (skinning)

Surface through a sequence of section curves $\mathbf C_k(q)$ at stations $s_k$:
make sections compatible (common degree via degree elevation, common knot vector
via knot insertion), then interpolate control points across sections with a
B-spline in $s$. Uses: river channels from surveyed cross-sections (HEC-RAS style
data), terrain ditches, irregular embankments, hull-like shapes. Pitfalls:
section compatibility (point correspondence) and twisting when sections are
not consistently oriented. For road corridors the $(s,t)$ sweep is better than a
loft because correspondence is defined by the template.

### 1.4 Ruled and developable surfaces

$\mathbf S(u,\lambda) = (1-\lambda)\mathbf C_0(u) + \lambda\mathbf C_1(u)$.
Simple and exact in NURBS if $\mathbf C_0,\mathbf C_1$ are compatible NURBS.
Developable (zero Gaussian curvature) ruled surfaces matter for sheet materials
(retaining walls, noise barriers, tunnel liners) — a later concern.

### 1.5 Coons patches

Given four boundary curves $\mathbf c_0(u),\mathbf c_1(u),\mathbf d_0(v),\mathbf d_1(v)$
meeting at corners, the bilinearly blended Coons patch

$$
\mathbf S(u,v) = \underbrace{(1-v)\mathbf c_0(u) + v\,\mathbf c_1(u)}_{\text{ruled in } v}
+ \underbrace{(1-u)\mathbf d_0(v) + u\,\mathbf d_1(v)}_{\text{ruled in } u}
- \underbrace{\mathbf B(u,v)}_{\text{bilinear of corners}}
$$

interpolates all four boundaries. Bicubically blended (Hermite) versions also
match cross-boundary derivatives (C1 joins). Uses: filling **junction areas** of
roads (OpenDRIVE leaves them undefined), filling gaps between corridor and
terrain, procedural roofs, ramps between levels.

### 1.6 Gordon surfaces

Gordon (1969) generalises Coons to a network of curves $\{\mathbf c_i(u)\}$ (at
$v_i$) and $\{\mathbf d_j(v)\}$ (at $u_j$):

$$
\mathbf S = \mathcal L_u + \mathcal L_v - \mathcal T_{uv},
$$

sum of the loft through the $u$-curves and the loft through the $v$-curves minus
the tensor-product interpolant of the intersection points. Requires a compatible
network (curves intersect at consistent parameters). Uses: terrain-blending
surfaces around complex interchanges, embankments under bridges, river confluence
beds. In practice, *n-sided* holes (5-way junctions, roundabout approaches) need
n-sided schemes (Gregory patches, generalised Coons/transfinite n-sided patches —
e.g. Várady et al. "Transfinite surface interpolation over curve networks"), or
simply a constrained triangulation with smoothed normals.

### 1.7 Pragmatic recommendation for junctions

Junction surfaces in road modelling are better generated as **constrained
Delaunay triangulations** of the junction polygon (bounded by lane-edge curves,
tessellated to tolerance), with heights interpolated from the incoming roads'
surfaces (e.g. harmonic / thin-plate interpolation of boundary heights) and
optional smoothing — rather than NURBS patches. Acoustic and visual consumers need
reasonable normals and closed watertight geometry, not CAD-grade continuity.
Keep the boundary curves exact (attributes on the bounding edges), so LOD changes
re-tessellate consistently.

---

## 2. Terrain interaction: draping, cut/fill, daylighting

1. **Corridor drives terrain.** The road surface (designed $z(s)$) is
   authoritative; at each station, side slopes (cut slope, e.g. 1:2; fill slope,
   e.g. 1:3, chosen by sign of design minus existing ground) are extended outward
   from the hinge point until they meet terrain — the **daylight line**. The
   daylight line is a 3D curve found by per-station 1D root finding
   (intersection of a ray in the cross-section plane with the terrain section),
   then fitted/tessellated. Terrain inside the daylight lines is replaced by the
   corridor surface.
2. **Terrain conforming**: re-triangulate terrain with the daylight polyline as a
   constraint (CDT), remove triangles inside the corridor footprint, stitch.
   Shared vertices on the daylight line are taken from the corridor tessellation
   (edge-first tessellation, see
   [spline-approximation-and-offsets.md](spline-approximation-and-offsets.md) §6).
3. **Draping** (the opposite): footpaths, field boundaries, fences, rivers as
   drainage lines — curves follow terrain; $z(s) = H(x(s),y(s))$ possibly with
   smoothing.
4. **Bridges/tunnels**: where the corridor leaves terrain by more than a threshold
   (fill height > ~10 m or cut depth > ~15–20 m), switch to bridge/tunnel
   structures; portals are curve–surface intersections of the alignment with
   terrain.
5. **Rivers**: water surface is a sweep of a horizontal line along the river
   centreline at water-surface elevation $z_w(s)$ (monotone); bed is a swept/lofted
   channel profile carved into terrain; banks are daylight-like intersections.

For drone-defence use (line of sight, acoustic reflection), **embankments and
cuttings are significant**: they occlude sensors and create reflecting surfaces.
They must be modelled, not just the pavement.

---

## 3. Curves on surfaces: pcurves and trimming

### 3.1 What CAD kernels do

In STEP / Parasolid / ACIS / OCCT, a face is a region of an underlying surface
$\mathbf S(u,v)$ bounded by loops of edges; each edge has a 3D curve
$\mathbf C(t)$, and each *use* of the edge by a face (coedge / fin / OCCT
"edge on face") has a **pcurve** $\gamma(t) = (u(t),v(t))$ in that face's
parameter space, with $\mathbf S(\gamma(t)) \approx \mathbf C(t)$. Consistency
requirements (OCCT's `SameParameter`/`SameRange`): same parameter range and
$|\mathbf S(\gamma(t)) - \mathbf C(t)| \le$ edge tolerance for all $t$. The
pcurves are what tessellators and trimming algorithms use; 3D curves are what
intersection and display use. Inconsistency between them is the #1 source of
"bad B-rep" bugs — hence *tolerant modelling* (per-edge/vertex tolerances).

### 3.2 What changes for UberTopo

* For road/corridor faces parameterised by $(s,t)$, pcurves of lane borders are
  exact graphs $t=b(s)$ and the 3D curve is $\mathbf S(s,b(s))$ — **the 3D curve
  can be defined *as* the composition**, eliminating the consistency problem by
  construction. Store the pcurve as authoritative and derive the 3D curve lazily.
* For faces of general surfaces (terrain TINs, building walls), store 3D curves
  and compute pcurves only when a parametric surface exists.
* For the non-manifold B-rep (BMesh-like with holes, per requirements), an edge can
  have more than two incident faces (radial edge structure); each face-use gets
  its own optional pcurve.

### 3.3 Trimming

Trimmed surface = surface + outer loop + hole loops in parameter space. For
$(s,t)$ road surfaces the trimming loops are piecewise graphs over $s$ (borders)
plus cross-section lines $s = s_0$ (section ends) — the trimmed domain is
$s$-monotone, enabling trivial point-in-face tests ($s$ in range and
$b_{low}(s) \le t \le b_{high}(s)$) and trivial tessellation (strips in $s$, with
vertices at shared station sets). Holes (drains, manholes, traffic islands) are
general loops in $(s,t)$.

---

## 4. Storing curve and surface data as B-rep attributes

The requirements say curves/surfaces are embedded in the B-rep as attribute data.
Proposed model (cross-reference the kernel research in `docs/research/cad-kernels/`
and `docs/research/attributes-and-queries/` when consolidating):

### 4.1 Geometry store, separate from topology

* `CurveStore` / `SurfaceStore`: immutable, content-addressed (hash of
  parameters) geometry objects: `Line`, `Arc`, `CesaroCurve{κ-law}`,
  `Composite{segments}`, `Alignment3{horizontal, profile}`, `Offset{base, w(s)}`,
  `OnSurface{surface, pcurve}`, `BSpline/NURBS`, `ParamPoly3`, `Catenary`,
  surfaces `RoadSurface{alignment, superelevation, template}`,
  `Sweep{directrix, frame mode, profile(s)}`, `Ruled`, `Coons`, `BSplineSurface`,
  `Heightfield/TIN`.
* Geometry objects reference each other (DAG), never topology. Content addressing
  gives dedup and cache keys for procedural re-evaluation.

### 4.2 Topology attributes referencing geometry

| Element | Attribute | Content |
|---|---|---|
| Vertex | `point` | exact position (+ tolerance) |
| Edge | `curve` | `{curve_id, interval [a,b], sense, tolerance}` |
| Edge | `curve_approx` (cache) | `{bspline_id, error_bound, param_map}` |
| Face-use of edge (loop-edge / coedge) | `pcurve` | `{curve2d_id in face's surface param space, interval, sense}` |
| Face | `surface` | `{surface_id, sense, tolerance}` |
| Face | `param_domain` | trimming loops in $(u,v)$ (derived from pcurves) |
| Face | `semantics` | e.g. `lane{road_id, lane_id, type}` — referencing the road model |
| Any | `lod_tessellation` (cache) | per-LOD meshes with per-vertex $(s,t)$, normals |

Rules:

1. **Exact geometry is the source of truth**; approximations (B-splines,
   tessellations) are caches keyed by (geometry id, tolerance, LOD) and can be
   dropped and regenerated.
2. Each cache records its **error bound**, and topological operations take the
   max of input tolerances (tolerant modelling).
3. Edges carry **parameter intervals on shared curves** rather than copies — lane
   edges of adjacent lanes are the *same* curve object (`Offset{centreline, b_k(s)}`)
   referenced with opposite senses by the two lane faces. Watertightness follows
   from sharing, not from tolerance matching.
4. Per-vertex attributes in tessellations carry $(s,t)$ (for texturing, lane
   lookup, sensor queries in road coordinates), plus source face id.

### 4.3 Levels of detail

| LOD | Road representation | Curves used |
|---|---|---|
| 0 | network graph; centreline polyline | alignment flattened at metres tolerance |
| 1 | carriageway ribbon (single face per road section), flat | centreline + two outer edges |
| 2 | lanes with superelevation; shoulders; terrain cut/fill | all lane borders as offsets, daylight lines |
| 3 | kerbs, sidewalks, markings as faces/decals, barriers, drains | + kerb profiles, marking pcurves |
| 4 | pavement detail, manholes, joints | + local trims/holes |

All LODs derive from the same exact model — a key reason for procedural, exact
source geometry.

---

## 5. Recommendations

1. Make `RoadSurface(s,t)` (and generally `Sweep(directrix, frame, profile)`) a
   first-class exact surface type with natural $(s,t)$ parameters.
2. Store pcurves on road faces as functions $t=b(s)$ and derive 3D curves from
   them — consistency by construction.
3. Use ruled strips between consecutive cross-section breaklines; Coons/CDT for
   junctions; loft for surveyed river sections.
4. Model cut/fill/daylight; terrain is conformed to corridors via CDT with
   daylight constraints.
5. Keep geometry in a content-addressed store referenced by topology attributes;
   B-splines and meshes are tolerance-tagged caches.

---

## References

* Gordon, W.J. *Spline-blended surface interpolation through curve networks.*
  J. Math. Mech. 18(10), 1969.
* Coons, S.A. *Surfaces for computer-aided design of space forms.* MIT MAC-TR-41,
  1967.
* Farin, G. *Curves and Surfaces for CAGD*, 5th ed., Morgan Kaufmann 2002 (Coons,
  Gordon, lofting).
* Piegl, L., Tiller, W. *The NURBS Book* (skinning, swept surfaces, ruled surfaces).
* Várady, T., Rockwood, A., Salvi, P. *Transfinite surface interpolation over
  irregular n-sided domains.* CAD 43(11), 2011.
* IFC 4.3 sectioned solids/surfaces along alignments (`IfcSectionedSolidHorizontal`,
  `IfcSectionedSurface`):
  <https://ifc43-docs.standards.buildingsmart.org/IFC/RELEASE/IFC4x3/HTML/annex_e/alignment-geometries-and-linear-positioning/segmented-reference-curve.html>
* ASAM OpenDRIVE 1.8 lane geometry and road surface:
  <https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/1.8.0/specification/11_lanes/11_06_lane_geometry.html>
* libOpenDRIVE mesh generation from $(s,t)$: <https://github.com/pageldev/libOpenDRIVE>
* Open CASCADE Technology — shape healing / `SameParameter`, tolerances, BRepMesh:
  <https://dev.opencascade.org/doc/overview/html/occt_user_guides__shape_healing.html>
* Weiler, K. *Topological structures for geometric modeling* (radial edge
  structure for non-manifold B-reps), PhD thesis RPI, 1986.
* Shewchuk, J.R. *Triangle* / CDT references:
  <https://www.cs.cmu.edu/~quake/triangle.html>
* US Army Corps of Engineers HEC-RAS (cross-section based river geometry):
  <https://www.hec.usace.army.mil/software/hec-ras/>
