# 03 — Faces with holes (inner loops)

Part of [B-rep topology research](README.md).

The requirements ask for "a general non-manifold brep with holes in faces". Faces with
holes come up all the time in this domain:

* A wall with windows and doors. The openings are holes in the wall face, or separate
  "aperture" faces filling the holes.
* A terrain patch with building footprints, ponds, or road corridors cut out of it.
* A roof with a chimney and skylights.
* A lake surface with islands, and a forest canopy sheet with clearings.
* A road deck with drain grates or median cut-outs.
* In plan (2D): a parcel with a building footprint, or a road polygon with a
  traffic island.

Most of these holes are *semantic*, not just cosmetic. A window opening is where sound
and light pass, so it needs identity, attributes, and its own face filling it (glass,
open, or closed shutter).

## 1. The design options

### (A) Multiple boundary cycles per face (outer + inner loops)

```
 Face F
 ┌──────────────────────────┐   loop 0 (CCW seen from the front)
 │                          │
 │   ┌──────┐    ┌──────┐   │   loop 1 (CW): window
 │   │  W1  │    │  W2  │   │   loop 2 (CW): window
 │   └──────┘    └──────┘   │
 │          ┌───┐           │   loop 3 (CW): door, touches the outer loop? see §3
 └──────────┴───┴───────────┘
```

**Who uses it:** Mäntylä's GWB ("rings"), radial-edge, Parasolid, ACIS, OCCT (a face
holds several wires), Topologic, VGC, Q-Complex, and Blender BMesh in the abandoned
`USE_BMESH_HOLES` build.

**Benefits**

* *Canonical.* The face is one semantic entity, with one ID and one attribute set
  (material, wall ID). The hole is a cycle with its own identity, so "window 2" can be
  named, referenced, and filled with an aperture face that shares the hole's edges.
* *Geometrically honest.* No zero-width slits and no fake edges, so offsetting,
  normals, area, and Booleans see the true boundary.
* *Stable under editing.* Moving a window moves a loop; the rest of the face's
  topology is unchanged. With bridges, the bridge would have to be re-chosen.
* *Matches every exchange format we will import:* STEP/IFC (`IfcFaceOuterBound` and
  `IfcFaceBound`), CityGML/CityJSON (polygon interior rings), GIS (Simple Features
  polygon interior rings), OBJ/PLY (no holes, which is the import problem in reverse).

**Costs**

* An extra indirection: face → loops → coedges. In an index layout this is one small
  extra table (loop: `first_coedge`, `face`, `next_loop`), about 12 bytes per loop.
  Most faces have one loop, so the overhead is small. An optimization is to store the
  first loop inline in the face record.
* Algorithms must iterate over loops. Point-in-face tests, area, and tessellation must
  handle all loops. Tessellation needs a **constrained triangulation** (CDT, or
  ear-clipping with hole bridging done *internally and temporarily*) instead of a fan.
* The loops must be **nested consistently**. Every inner loop must lie inside the
  outer loop and inner loops must not overlap. This must be checked in the face's
  parameter space (see §4) and is part of validity checking
  ([05](05-invariants-operators-validity.md)).
* Euler bookkeeping gains a "ring" term (`F − L` in V − E + F − (L − F) − 2(S − G) = 0).

### (B) Bridge edges (keyhole / slit)

```
 ┌──────────────────────────┐
 │        ┌──────┐          │      Bridge edge b is traversed twice by the same
 │        │ hole │          │      face's single loop (once in each direction).
 │        └──┬───┘          │
 │           │ b            │
 └───────────┴──────────────┘
```

**Who uses it:** Blender n-gons in practice, many polygon libraries, and ear-clipping
triangulators (internally). OCCT **seam edges** on periodic surfaces are the same
construct: one edge used twice by one face.

**Benefits:** no new entity. Every face stays a single cycle, so half-edge, quad-edge,
and G-maps work unchanged.

**Costs**

* *Not canonical.* The choice of bridge is arbitrary. Two equivalent models differ
  topologically, which hurts diffing, caching, and persistent naming: the bridge edge
  needs a name that has no meaning.
* *Degenerate geometry.* The face boundary touches itself along a zero-width slit.
  Normal estimation, offsetting, buffering, point-in-polygon tests, CDT, and Booleans
  must all special-case "edge used twice by the same face". The radial cycle of the
  bridge edge holds the same face twice, which breaks the assumption "radial neighbours
  are distinct faces".
* *Fragile.* If the hole moves, the bridge may cross another hole or the outer
  boundary and has to be re-chosen. That is a topological change caused by a
  geometric edit.
* *Bad for semantics.* A bridge edge is a fake edge: it appears in edge-based queries
  (for example "all wall-floor junction edges" or "all sharp edges for acoustic
  diffraction") and must be filtered out everywhere.
* *Acoustic diffraction.* Edge-diffraction acoustic models (for example
  Biot–Tolstoy–Medwin, or uniform theory of diffraction (UTD) for higher frequencies)
  enumerate *geometric* edges. A fake edge would produce spurious diffraction sources
  unless flagged.

**Verdict:** acceptable *only* as a transient inside a tessellator. Never in the
master model.

### (C) Decompose: every face is a disk (triangulate or split)

Split a face with holes into several hole-free faces, triangles in the extreme.

**Who uses it:** game and GPU meshes, half-edge processing libraries, CityGML LOD
data in practice ("multi-surface" of triangles), OBJ.

**Benefits:** the simplest topology. Everything is manifold-friendly and GPU-ready.

**Costs:** destroys face identity. "The wall" becomes 30 faces, so attributes must
be duplicated or kept in a side "face group" layer, which is a hole representation
in disguise. Splitting edges are arbitrary (the same non-canonicity as bridges), and
there are more entities (memory, Booleans, ray-tracing BVH size). It makes the
representation depend on the tessellation, which LOD must be free to change.

**Verdict:** this is the **derived render/simulation tessellation**, not the master
model.

### (D) Face groups over a disk-face mesh

Keep a disk-only mesh (C) plus a "polygon ID" attribute on every small face. A
"face" is then the group. CityJSON semantic surfaces and Blender's face sets work this
way.

**Verdict:** this is (A) implemented badly. You get (A)'s semantics without (A)'s
invariants. Use it only at import/export boundaries.

## 2. Comparison matrix

| Criterion | (A) loops | (B) bridges | (C) decompose | (D) groups |
|---|---|---|---|---|
| Canonical topology | ✓ | ✗ | ✗ | ✗ (within group) |
| Stable IDs under edits | ✓ | ✗ | ✗ | partial |
| Clean geometry (no degenerate edges) | ✓ | ✗ | ✓ | ✓ |
| Works with manifold half-edge code | ✗ (needs loops) | ✓ | ✓ | ✓ |
| Memory | +12 B/loop | +1 edge/hole | ++ | + |
| Tessellation | CDT needed | CDT on a degenerate polygon | trivial | trivial |
| Window/door = named hole | ✓ | ✗ | ✗ | ✗ |
| Exchange-format fit (IFC, CityGML, GIS) | native | lossy | lossy | lossy |

**Recommendation: (A).** Make it more general than "one outer and n inner": **a face
is bounded by an unordered set of boundary cycles.** Do not store an "outer" flag as
topology. §3 explains why.

## 3. Subtleties that a robust design must handle

### 3.1 "Outer" is not a topological property
On a planar face, the outer loop is the one whose signed area in the face frame is
positive. On a **periodic** surface it is undefined: a cylindrical tank wall has two
circular boundary loops and neither is outer. A spherical cap with a hole has two
loops, and "outer" depends on which side you call the cap. On a torus patch it can
be anything.

So treat every loop alike. Orientation is the only stored property: loops run with
the face's material on the left when viewed from the front side. "Outer" is a *derived,
cached* classification that is valid only for faces whose parameter domain is a plane
region. Parasolid does not require seam edges on periodic faces (see
[02](02-survey-systems.md)). We should follow that, and handle periodicity in the
pcurve and parameter-domain layer ([07](07-geometry-embedding.md)).

### 3.2 Faces with zero loops
A full sphere, a full torus, or a closed implicit blob are closed faces with no
boundary. Either allow loopless faces (Parasolid-style), or require at least one vertex
or edge on every face, as OCCT does with seams and degenerate pole edges. Recommendation:
**allow loopless faces.** Water droplets, tanks, and domes are rare, but a single
spherical acoustic test target is common in validation scenes.

### 3.3 Loops touching at a vertex (non-manifold loop contact)
A door opening that touches the bottom edge of a wall shares a vertex or an edge with
the outer boundary. Two options:

* Merge them into a single loop that passes through the shared vertex twice. This is
  a "pinched" loop: valid, and the vertex appears twice in the cycle.
* Or keep two loops that share a vertex. That makes the face's interior disconnected
  at that point, and Euler bookkeeping changes.

Pick one canonical rule. The SGC/Masuda view treats the face as an open set, so
loops touching at a vertex are fine, and loop *count* follows the number of boundary
components of the *closure*. **Recommendation:** canonicalize so that each loop is one
connected component of the face boundary. This is computable and unique. Loops may
then revisit vertices (pinches), and a pinch vertex is flagged non-manifold-in-face.

### 3.4 Isolated vertices and wire edges *inside* a face
A sensor mounted on a wall, or a road centerline painted on terrain without splitting
the terrain face. These are **inclusions**: lower-dimensional cells inside the open
face. OCCT uses `INTERNAL` orientation, Q-Complex calls them inclusion topology, and
in SGC they are just cells in the interior.

Inclusions are **not loops**: they do not bound the face's area. Store them in a
separate per-face inclusion list, and keep them out of the loop machinery. Imprinting
(splitting the face along the road) is a separate, explicit operator, used when the
road must carry its own surface attributes.

### 3.5 Holes that are filled
A window hole is usually *filled* by another face: glass, an opening, or an aperture.
The window face and the wall face share the loop's edges, so those edges become
non-manifold edges with 3–4 radial faces: wall front, wall back (for a thick wall,
the reveal faces), and glass. This is the strongest reason holes must be first-class
*together with* a non-manifold radial cycle. A wall face with a window loop, plus a
glass face sharing that loop's edges, is the canonical building-envelope
configuration.

For acoustics and visibility, the aperture face carries the attributes that matter:
transmission loss, open or closed state, glass type.

## 4. Where loop-nesting is decided: parameter space

Loop classification (orientation, nesting, inside/outside) should be done **in the
face's 2D parameter domain**, using the coedges' pcurves (parameter-space curves).
In 3D it is ill-conditioned. This is the main reason to keep pcurves on coedges even
for planar faces, where they are trivially an affine projection
([07](07-geometry-embedding.md)).

* Use exact or adaptive-precision 2D predicates (Shewchuk orient2d, and winding
  numbers over the polyline approximation with a certified error bound) for
  point-in-loop and loop-in-loop tests.
* Periodic domains need the universal-cover trick. Unwrap pcurves onto a strip and
  count the winding around the cylinder axis: a loop that winds once around is an
  "essential" loop (like the two rims of a tank), and one that does not is a
  contractible hole.

## 5. Tessellation of faces with holes

* Planar and near-planar faces: CDT in parameter space with all loop polylines as
  constraints. Libraries: Shewchuk's Triangle (non-commercial license), CGAL CDT_2,
  `earcut` (fast, not robust), and Clipper2 for preprocessing and Booleans.
* Curved faces: a parameter-space CDT, then refinement by a chordal-deviation
  criterion evaluated in 3D (standard practice, as in OCCT `BRepMesh`).
* LOD: holes below an LOD-dependent size threshold can be dropped *in the
  tessellation only*. This decision must be attribute-aware: a window that matters
  acoustically may still be dropped visually.

## 6. Recommendation summary

1. **A face is bounded by a set of oriented boundary cycles.** No stored outer flag,
   and zero cycles are allowed.
2. One loop per connected component of the boundary. Pinched loops are allowed and
   flagged.
3. **Inclusions** (vertices, edges, or wires inside the open face) are a separate list,
   not loops.
4. Holes are first-class identities. Filling a hole with an aperture face is a normal
   non-manifold radial configuration.
5. Bridge edges and seam edges are not allowed in the master model. Tessellators may
   create bridges internally.
6. Loop validity and nesting are computed in parameter space with robust predicates.

## References

* M. Mäntylä, *An Introduction to Solid Modeling*, 1988 (rings, `kemr` and `mekr`).
* BMesh `USE_BMESH_HOLES` / `BMLoopList`: https://projects.blender.org/archive/blender-archive/src/commit/5cc793912efe4432b51cc7ddda4005c56fcbc93b/source/blender/bmesh/bmesh_class.h
* B. Dalstein et al., Vector Graphics Complexes (faces bounded by multiple cycles): https://www.borisdalstein.com/research/vgc/vgc.pdf
* Q-Complex, inclusion topology: https://cg.cs.tsinghua.edu.cn/papers/lyj-CAD12.pdf
* OCCT TopoDS / orientation INTERNAL and EXTERNAL: https://dev.opencascade.org/doc/refman/html/classTopoDS__Shape.html
* J. R. Shewchuk, "Adaptive precision floating-point arithmetic and fast robust geometric predicates", *DCG* 18, 1997 — https://www.cs.cmu.edu/~quake/robust.html
* J. R. Shewchuk, Triangle — https://www.cs.cmu.edu/~quake/triangle.html
* CGAL 2D Constrained Delaunay Triangulation — https://doc.cgal.org/latest/Triangulation_2
* Clipper2 — https://github.com/AngusJohnson/Clipper2
* earcut — https://github.com/mapbox/earcut
* OGC Simple Features (polygon interior rings) — https://www.ogc.org/standard/sfa/
* CityGML 3.0 space and space-boundary concept — https://link.springer.com/10.1007/s41064-020-00095-z
