# 05 — Geometry Pools

Part of the [core data model design](README.md). Research basis:
[brep-topology/07](../../research/brep-topology/07-geometry-embedding.md),
[curves/README](../../research/curves/README.md),
[curves/curve-library-design](../../research/curves/curve-library-design.md),
[attributes-and-queries/embedded-geometry-attributes](../../research/attributes-and-queries/embedded-geometry-attributes.md).
The pool pattern is taken from Sculptcore's `DeformPool`.

## 1. Attachment points

| Element | Column | Carries | Default when NONE |
|---|---|---|---|
| Edge | `.e.curve` : `GeomRef` + `.e.range` : `f64×2` | 3D carrier curve and parameter interval | Straight segment between the endpoint positions |
| Coedge | `.c.pcurve` : `GeomRef` | Curve in the face's (u, v) parameter space | Derived by projection when needed |
| Face | `.f.surface` : `GeomRef` + `reversed` flag | Carrier surface | Plane through the loop (planar face) |
| Included edge on a face | `geom.pcurve` attribute | pcurve on the container face, since there is no coedge | — |
| Region | `medium.field` : `GeomRef` (optional) | Volumetric field such as vegetation density | Homogeneous medium from material |

**Either the 3D curve or the pcurve can be primary.** A per-edge flag records which one is
the definition.
- **Roads:** the alignment is primary. It is defined in a road surface's (s, t)
  parameter space, and the 3D edge curve is derived from it.
- **Building edges:** usually primary in 3D.

## 2. `GeomRef` and pools

```cpp
namespace ubertopo::geom {

struct GeomRef {               // 8 bytes, a column value type
  uint32_t kind  : 8;          // registered geometry kind (open set, like attribute column types)
  uint32_t pad   : 24;
  uint32_t index;              // slot in that kind's pool
};

template <typename G> class GeometryPool {   // one per kind; built on litestl::attr::InternPool
public:
  GeomRef add(G &&g);            // optional interning by canonical hash
  const G &get(GeomRef r) const;
  void retain(GeomRef r); void release(GeomRef r);
  // entries are immutable once added; "editing" = add new + reassign the column
};

}  // namespace ubertopo::geom
```

**What carries over from `DeformPool`:**
- The column stays a plain value type, so reorder, swap, serialise and copy-on-write keep
  working untouched.
- Pool entries are **immutable** and **refcounted**. Many edges can share one carrier,
  for example adjacent lanes sharing a single offset curve, so that they meet *by
  sharing* rather than by tolerance.
- A reference discipline is enforced by a funnel API. Column writes go through
  `GeomColumn::assign(i, ref)`, which retains the new reference and releases the old.
  An `audit_refcounts()` debug pass recomputes the counts from roots.

**What changes:**
- **Pools belong to a `GeometryStore`** that is shared by the snapshots of a complex.
  The store itself is copy-on-write: a pool is paged like attribute columns, so
  snapshots share it.
- **Canonical renumbering** before freeze and serialise. Slots are reassigned in order
  of first reference by canonical element order. This removes Sculptcore's
  shard-dependent nondeterminism ([04 §8](04-identity-versions-journal.md#8-determinism-contract)).
- **Open set of kinds.** Kinds register like column types (DEC-007), so new curve or
  surface families don't need core edits. Evaluation dispatch is per kind (virtual on a
  `CurveKind`/`SurfaceKind` interface, or a typed visitor per batch). It is never a
  switch over a closed list.

**Precision.** Pools are **not** templated on `Coord`. Curves and surfaces are evaluated
in `double` in the tile frame ([02 §7](02-coordinates-and-predicates.md#7-what-is-not-templated-on-coord)).
Each entry records a **certified error bound** relative to its definition. The embedding
check in [01 §8](01-cell-complex.md#8-validation) compares vertex positions against the
carrier within that bound.

## 3. Layered geometry

```
Definition (exact / procedural)      e.g. CurvatureLawCurve, Catenary, RoadSurface(s,t)
   │  certified approximation (error ε₁, parameter map)
   ▼
Approximation cache                   B-spline (UberTopo-owned, DEC-003), per-segment polynomials
   │  tessellation (chord tolerance ε₂ ≥ ε₁, LOD-dependent)
   ▼
Tessellation                          produced by consumers, not stored in pools
```

**Approximation caches.**
- Caches are pool entries of their own and point back to their source:
  `BSplineCurve{source: GeomRef, range, max_error, param_map}`.
- They are **derived**. They can be dropped and regenerated deterministically.

**Tessellation.** Tessellation is a consumer product with its own design later. Shared
edges are discretised once, so adjacent faces get crack-free tessellations.

## 4. Initial kinds

**Curves** (3D carriers or 2D pcurves):

| Kind | Definition | Notes |
|---|---|---|
| `Line` | Implicit (NONE) | The common case; costs nothing |
| `CircularArc` | Centre, normal, radius, angles | Exact |
| `CurvatureLawCurve` | Start pose plus a sequence of segments, each with length and a pluggable κ(s) law (constant, linear = clothoid, polynomial, Bloss, sine, …) | The core road, rail and river primitive ([curves research](../../research/curves/README.md)). Evaluated by piecewise Gauss–Legendre or series integration, with certified per-piece polynomial caches |
| `OffsetCurve` | Base curve plus a width function w(s) | Lane edges. Approximated from the *exact* base's analytic derivatives, never from an approximation of the base. Cusps where w·κ = 1 are found analytically |
| `Catenary` | Endpoints, sag parameter, plane | Power lines and wires |
| `BSplineCurve` | Degree, knots, control points (optionally rational), plus an optional `source` | Either an approximation cache or a primary definition (imported) |
| `Polyline` | Points | Imported data, scans |
| `SpaceAlignment` | Horizontal CurvatureLawCurve plus a vertical profile (parabolic and other curves by station) | 3D road and rail alignments |

**Surfaces:**

| Kind | Definition | Notes |
|---|---|---|
| `Plane` | Implicit (NONE), or explicit for plane-shared faces (§5) | The common case |
| `RoadSurface` | SpaceAlignment plus cross-section (lane widths and crossfall as functions of station) | Exact (s, t) parameterisation. Lane borders are exact pcurves `t = b(s)` |
| `Sweep` | Profile curve, path curve and frame rule (gravity-referenced for roads and rail, rotation-minimising for pipes) | Walls on curves, kerbs, embankments |
| `Heightfield` | A window into a terrain grid/TIN with its own frame | Terrain faces |
| `BSplineSurface` | Tensor product, optionally rational | Imported, or cache |
| `MeshSurface` | Triangle set (positions plus optional normals) attached to a face | Photogrammetry and LiDAR-backed faces ([brep-topology/07](../../research/brep-topology/07-geometry-embedding.md)) |
| `Revolution`, `Cylinder`, `Sphere` | Analytic | Tanks, silos, domes |

## 5. Plane sharing (relevant to OI-1)

**Faces that share a plane.** When a generator produces coplanar faces *by intent*, for
example all panels of one facade, it can give them one explicit `Plane` entry. That makes
coplanarity a recorded fact rather than a numeric coincidence. It is also the input form
for plane-based exact Booleans (option b in
[02 §5](02-coordinates-and-predicates.md#5-the-plane-based-option-d2)).

**Exact planes.** For `Complex<Lattice>`, a `Plane` entry has **integer** coefficients:
`ax + by + cz + d = 0`, with the bit widths of the cad-kernels estimate. This is the one
pool kind that depends on `Coord`. It lives in a small `PlanePool<Coord>` rather than the
`double` pools.

## 6. Evaluation API (sketch)

```cpp
template <typename K> concept CurveKind = requires(const K &k, double t) {
  { k.eval(t) } -> std::same_as<math::Vec<double, 3>>;
  { k.deriv(t, int{}) } -> std::same_as<math::Vec<double, 3>>;
  { k.domain() } -> std::same_as<math::Vec<double, 2>>;
  { k.length(t, t) } -> std::same_as<double>;          // exact for arc-length-parameterised kinds
  { k.bound(t, t) };                                    // conservative AABB / turning bound
  { k.to_bspline(double{} /*max_error*/) };            // certified approximation
};
```

- **Batch evaluation** (`eval_many(span<double>, span<Vec3>)`) is the primary entry point.
  It is per kind, so the virtual call happens once per batch.
- **Projection and closest point**, plus intersection with planes, are required for
  imprinting and road junctions. They are implemented per kind, with a generic B-spline
  fallback.
- **Library location.** The curve library lives in `ubertopo::curves`. B-splines live in
  UberTopo (DEC-003).

## 7. Testing

- **Clothoid and Fresnel accuracy** against an offline oracle: Bertolazzi–Frego
  `Clothoids`, used only as a test tool per DEC-002, plus high-precision reference
  values.
- **Certified bounds hold:** sample-based checks that the true deviation is at most
  `max_error` on random alignments, including near-zero curvature and very long spirals.
- **Offsets:** lane-edge offsets agree with independent dense sampling; cusp locations
  are found.
- **Pools:** refcount audit after random edit sequences; canonical renumbering gives
  identical serialisation for different insertion orders.
