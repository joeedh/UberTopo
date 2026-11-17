# Curve Library Design for UberTopo (proposal)

Part of the [curves research set](README.md). A proposed API and data model for
UberTopo's curve (and curve-derived surface) library, synthesising the other files
in this folder. Language-neutral; sketches use Rust-like trait syntax because the
model maps cleanly onto traits/interfaces and because kurbo (Rust) is the closest
prior art — this is **not** a language recommendation.

---

## 1. Design goals

1. **Exact, intent-preserving source geometry.** Curvature-defined curves
   (clothoids, polynomial spirals, LAC, sine-generated), alignments
   (horizontal × profile × cant), offsets and on-surface curves are stored by
   their defining parameters, never only as approximations.
2. **One generic interface** usable by B-rep algorithms (intersection,
   projection, tessellation), procedural nodes, and exporters.
3. **Approximations are tolerance-tagged caches**, regenerable, with certified
   error bounds and a parameter map back to the source.
4. **Robustness by construction**: parametric coincidence detection, shared curve
   objects for shared edges, explicit ambiguity results, deterministic evaluation.
5. **Cheap bulk evaluation** (millions of points for terrain-scale tessellation;
   thousands of curves for power-line corridors, forests' branches, fences).

Non-goals (for v1): exact rational arithmetic, symbolic algebra, general implicit
curves.

---

## 2. Core abstractions

### 2.1 Parameterisation

Every curve has a native parameter $u$ over a closed interval. Curves additionally
declare whether $u$ is (a) arc length, (b) a *station* (horizontal arc length for
alignments), or (c) a general parameter. Arc-length/station maps are exposed as
first-class functions because roads place everything by station.

```rust
enum ParamKind { ArcLength, Station /* horizontal arc length */, General }

trait Curve<const D: usize> {          // D = 2 or 3
    fn domain(&self) -> Interval;
    fn param_kind(&self) -> ParamKind;

    // --- evaluation ---
    fn eval(&self, u: f64) -> Point<D>;
    fn eval_derivs(&self, u: f64, n: usize, out: &mut [Vec<D>]); // r, r', ..., r^(n)
    fn tangent(&self, u: f64) -> UnitVec<D>;
    fn curvature(&self, u: f64) -> f64;            // signed for D = 2
    fn torsion(&self, u: f64) -> Option<f64>;      // D = 3

    // --- metric ---
    fn arc_length(&self, range: Interval, tol: f64) -> Bounded<f64>;
    fn param_at_arc_length(&self, from: f64, len: f64, tol: f64) -> Bounded<f64>;

    // --- certified bounds (drive all adaptive algorithms) ---
    fn bounds(&self, range: Interval) -> Aabb<D>;          // guaranteed containment
    fn turning_bound(&self, range: Interval) -> f64;       // ∫|κ| ds upper bound
    fn curvature_bound(&self, range: Interval) -> f64;     // max |κ| upper bound
    fn special_params(&self, range: Interval) -> SmallVec<SpecialParam>;
        // inflections, curvature extrema, segment joins, cusps, continuity breaks

    // --- structure ---
    fn split(&self, u: f64) -> (CurveRef, CurveRef);  // exact (re-parameterised child)
    fn subcurve(&self, range: Interval) -> CurveRef;  // exact view
    fn reversed(&self) -> CurveRef;
    fn transformed(&self, xf: &Similarity<D>) -> CurveRef; // exact for similarities
    fn continuity_at(&self, u: f64) -> Continuity;    // G0/G1/G2/G3/C∞ at u

    // --- identity ---
    fn geometry_hash(&self) -> Hash128;  // content address
    fn same_carrier(&self, other: &dyn Curve<D>, tol: f64) -> Option<ParamMap>;
        // exact parametric coincidence test (e.g. two clothoids on one spiral)
}
```

`Bounded<T>` = value + certified error bound. Algorithms (projection,
intersection, tessellation) are **free functions over the trait**, specialised
when both operands have a fast path (line/arc closed forms, clothoid tangent
triangles, Bézier clipping for polynomial pairs).

### 2.2 Concrete types (v1)

| Type | Defining data | Param | Notes |
|---|---|---|---|
| `Line2/3` | start, direction, length | arc length | exact everywhere |
| `Arc2` | centre/start pose, κ, length | arc length | exact NURBS export |
| `CesaroCurve2` | start pose $(\mathbf r_0,\theta_0)$, length, `KappaLaw` | arc length | clothoid/polyspiral/LAC/sine/Kinoshita/catenary-in-plane |
| `ParamPoly3` | local frame, $u(p),v(p)$ cubics, pRange | general | OpenDRIVE; exact Bézier |
| `Bezier<D>` / `BSpline<D>` / `Nurbs<D>` | control points, weights, knots | general | universal derived/imported |
| `PhCurve<D>` | preimage polynomial | general (exact $s(t)$) | export/exact offset option |
| `Composite<D>` | ordered segments + station offset + station equations | station | alignments, polylines of curves |
| `Offset2` | base curve id, `Fn1D` $w(s)$ | base station | lane borders, kerbs |
| `Alignment3` | horizontal `Composite2`, profile `Fn1D` $z(s)$, cant/superelev. `Fn1D` | station | road/rail centreline |
| `OnSurface` | surface id, 2D pcurve | pcurve param | lane borders on road surfaces |
| `Helix3`, `Catenary3` | closed-form params | arc length | ramps, cables |
| `OdeCurve<D>` | state-dependent κ (closure id + params), dense-output cache | arc length | path simulation; rare |

`KappaLaw` (all with closed-form $\theta(s)$, $\kappa^{(k)}(s)$ and exact bounds):

```rust
enum KappaLaw {
    Constant(f64),                       // arc (Arc2 fast path)
    Linear { k0: f64, k1: f64 },         // clothoid
    Poly(SmallVec<[f64; 8]>),            // polynomial spiral, IFC 2nd/3rd/7th order
    Transition { kind: TransitionKind, k_start: f64, k_end: f64 }, // Bloss, cosine,
                                         // sine (Klein), Helmert, Viennese (+params)
    LogAesthetic { alpha: f64, lambda: f64, k0: f64 },
    SineGenerated { omega: f64, wavelength: f64, phase: f64 },
    Kinoshita { theta0: f64, k: f64, js: f64, jf: f64 },
    Piecewise(Vec<(f64, KappaLaw)>),     // rare; prefer Composite
}
```

`Fn1D` = piecewise polynomial in station (OpenDRIVE cubic records, LandXML
parabolas, linear ramps) with explicit breakpoints; also exact.

### 2.3 Evaluator implementation (CesaroCurve)

* At construction: split $[0,L]$ into **pieces of bounded bend** (|Δθ| ≤ 0.5 rad,
  plus at all special params). For each piece store start pose and a
  per-piece polynomial cache (Taylor or Chebyshev of $e^{i\theta}$ integrated) with
  tail-coefficient error bound — see
  [numerical-evaluation.md](numerical-evaluation.md).
* `eval(s)`: locate piece (binary search), Horner on cached polynomial. Derivatives
  from $\theta(s)$ directly (no differentiation of the cache).
* Clothoid fast path: Bertolazzi–Frego $X_k,Y_k$ (two regimes) for one-off
  queries; test oracle consistency between both paths.
* Caches built lazily, thread-safe (once-cell), keyed by geometry hash.

---

## 3. Derived operations

### 3.1 Tessellation

```rust
fn tessellate(c: &dyn Curve<D>, opts: &TessOpts) -> Polyline<D> // with params per vertex
struct TessOpts {
    chord_tol: f64,          // max deviation (sagitta)
    angle_tol: f64,          // max turning per segment (for shading/normals)
    max_len: f64,            // e.g. for terrain-draping resolution
    must_include: Vec<f64>,  // station events (lane sections, superelev. breaks)
    nest_with: Option<LodParams>, // vertex set ⊇ coarser LOD's set
}
```

Density $\propto\sqrt{|\kappa|/(8\varepsilon)}$, splits at special params;
deterministic (same input ⇒ same parameters). Edge tessellation is done once
per B-rep edge and shared by incident faces.

### 3.2 Polynomial conversion

```rust
fn to_bspline(c: &dyn Curve<D>, req: &FitRequest) -> Fitted<BSpline<D>>
struct FitRequest { tol: f64, degree: u8 /*3,5,7*/, continuity: Continuity,
                    param: FitParam /* SourceStation | Native */,
                    breaks: BreakPolicy /* at segment joins, inflections, events */ }
struct Fitted<T> { curve: T, error_bound: f64, param_map: ParamMapApprox,
                   source: CurveId, source_range: Interval }
```

Default: quintic Hermite at error-equidistributed knots, parameterised by source
station, knots at all continuity breaks
([spline-approximation-and-offsets.md](spline-approximation-and-offsets.md)).
Export modes: `Cubic::AreaMoment` (kurbo-style, G1), `Cubic::G2Hermite`,
`ArcSpline` (biarcs), `PhSeptic`.

### 3.3 Offsets

`offset(base, w: Fn1D) -> Offset2` is **exact and lazy**; it exposes
`special_params` including cusps ($w\kappa = 1$) and reports `OffsetValidity`
ranges. Approximating an `Offset2` uses the generic `to_bspline` on the exact
offset (never offsets an approximation). Global self-intersection cleanup is a
separate 2D Boolean step.

### 3.4 Queries

```rust
fn project(c, p, hint: Option<f64>) -> ProjectionResult  // all local minima + global
fn to_curvilinear(alignment, p) -> CurvilinearResult      // (s, t, h) or Ambiguous/OutOfTube
fn intersect(a, b, tol) -> Vec<Intersection>              // points, tangencies, overlaps
fn intersect_surface(c, s, tol) -> Vec<CurveSurfaceHit>
fn distance_to_tube(c, radius, ray) -> Option<Hit>        // wires/cables for sensor sims
```

Results distinguish `Transversal`, `Tangential`, `Overlap(range_a, range_b)`
(via `same_carrier`) and `Uncertain` (near-tangent within tolerance — caller
decides).

### 3.5 Frames and sweeps

```rust
enum FrameMode { Gravity { superelev: Fn1D, pivot: Pivot }, Rmf { initial: Vec3 },
                 UpVector(Vec3), Frenet, Custom(FrameFnId) }
fn frames(c: &dyn Curve<3>, mode: &FrameMode, params: &[f64]) -> Vec<Frame>
struct Sweep { directrix: CurveId, frame: FrameMode, profile: ProfileSpec }
```

`RoadSurface` = `Sweep` with `Gravity` frame and a template profile, exposing
$(s,t)$ parameters and exact pcurves $t=b(s)$
([surfaces-from-curves.md](surfaces-from-curves.md)).

---

## 4. Procedural integration

### 4.1 Nodes create *intent*, the library resolves geometry

Typical node chain for a road:

```
OSM way / planner path (polyline)
  → FitAlignment {design_speed, max_dev}      // PI extraction + radius/spiral solve
  → Alignment (Composite of Line/Arc/Clothoid; PI design layer retained)
  → Profile from terrain {max_grade, K_crest, K_sag}  // vertical design on DEM
  → CrossSectionTemplate {lanes, shoulders, kerbs, superelevation policy}
  → Corridor {terrain} → RoadSurface + daylight lines + terrain modification
  → B-rep faces/edges with curve/surface attributes, LOD tessellations
```

Other generators: `MeanderRiver{θ-law params, noise}`, `PowerLine{tower
positions, sag}`, `Fence/Wall{along curve, draped}`, `TreeRow{along offset curve,
spacing by station}`, `Kerb{offset, profile}`.

### 4.2 Editing semantics

* Nodes edit **design parameters** (PI positions, radii, spiral lengths, widths
  as $w(s)$), never control points of derived splines.
* A solver turns design parameters into segments; invalid designs (overlapping
  spirals, radius below minimum for design speed, offset cusp inside a lane)
  produce **diagnostics** attached to the node, not silent repair.
* Content-addressed geometry ⇒ unchanged upstream parameters reuse cached
  geometry, approximations and tessellations.
* **Stable identities**: downstream references (an object placed at station 1234.5
  on lane −2) are expressed in *(alignment id, station, t, h)* — robust to
  re-tessellation and to edits that do not move that station. Station equations
  keep labels stable when upstream lengths change.

### 4.3 Determinism

Same inputs ⇒ bit-identical geometry on a given platform: fixed-order reductions,
no data-dependent parallel reduction order, fixed quadrature orders chosen from
a-priori bounds (not from adaptive estimates that might flip with rounding), no
`fma` contraction differences across builds (pin compiler flags). Needed for cache
correctness and reproducible test scenarios.

---

## 5. Robustness concerns and mitigations

| Concern | Mitigation |
|---|---|
| Large coordinates (UTM ~1e6 m; geocentric ~6e6 m) | Per-scene/tile local origin; curves stored in local metric frames with a CRS transform; f64 everywhere in the kernel; rebase to f32 only on GPU per tile. Double at 1e6 m resolves ~1e-10 m — fine for the kernel, catastrophic in f32 (6 cm). |
| Map projection distortion | Design geometry is in a projected CRS (as in LandXML/OpenDRIVE/IFC); keep it there. Don't reproject curves by reprojecting control points of approximations — reproject the start pose and accept the scale factor (or treat as 2D similarity per tile). |
| Inconsistent imported data (gaps/kinks between segments) | Import policy per format: trust parameters vs points; measure and store gaps as diagnostics; optional G1/G2 repair via Hermite refit of short transitions. |
| Catastrophic cancellation in Fresnel route | Two-regime evaluation; generic per-piece integration never sees large arguments. |
| Near-tangent intersections | Certified (interval/Krawczyk) leaf tests; `Uncertain` results; tolerance-aware topology merge. |
| Overlapping curves | `same_carrier` exact coincidence via parameters (Cesàro curves: compare κ-laws after shift); polynomial curves: degree/knot-normalised comparison + sampling. |
| Offset cusps / sweep folds | Analytic $w\kappa=1$ detection; validity ranges; diagnostics. |
| Station vs 3D length confusion | Distinct types (`Station` vs `ArcLength` param kinds); API requires explicit choice. |
| Accumulated drift along long chains | Store each segment's start pose after solve; evaluate locally. |
| Tolerance stack-up through caches | Every cache carries its error bound; topology ops combine bounds; global tolerance policy (e.g. modelling tol 1e-6 m, display tol per LOD). |
| Shared edges tessellated differently | Edge-first tessellation, shared parameter sets, nested LOD vertex sets. |
| Degenerate inputs (zero-length, κ'=0 clothoid, R=∞ spiral ends) | Canonicalisation at construction (clothoid with κ'=0 → arc; arc with κ=0 → line), with tests for each limit. |

---

## 6. Testing strategy

* **Oracles**: mpmath (arbitrary precision Fresnel/quadrature), the
  `ebertolazzi/Clothoids` C++ library (G1/G2 fits, distances, intersections), kurbo
  (Bézier fits/offsets, arc length), libOpenDRIVE/esmini (OpenDRIVE evaluation).
* **Property tests**: split/re-evaluate invariance; reverse/transform
  commutation; arc length additivity; fitted B-spline error ≤ reported bound at
  10⁴ random samples; offset of offset ≈ offset by sum; projection residual
  orthogonality; tessellation vertices within tolerance and nested across LODs.
* **Standards fixtures**: sample OpenDRIVE files (ASAM examples, CARLA towns),
  IFC 4.3 alignment test files (buildingSMART validation suite), LandXML
  alignments from Civil 3D/OpenRoads exports.
* **Degenerate sweeps**: tight junction corners, hairpins, zero-length segments,
  station equations, spirals with R=∞ at both ends.

---

## 7. Build vs reuse

| Component | Reuse option | Recommendation |
|---|---|---|
| Clothoid fitting/distance/intersection | ebertolazzi/Clothoids (C++, BSD-2) | Port/wrap for G1/G2 fitting; use as oracle |
| 2D Bézier ops, fitting, offsets | kurbo (Rust, MIT/Apache) | Adopt if Rust; else port the fitting/offset algorithms |
| NURBS core | OCCT (C++, LGPL), opennurbs (MIT), nurbs-rs crates | Write minimal own NURBS (evaluation, knot insertion/removal, degree elevation) — small, well-documented (The NURBS Book) |
| OpenDRIVE parsing/evaluation | libOpenDRIVE (Apache-2.0) | Use for import + as oracle; map onto our types |
| IFC parsing | IfcOpenShell (LGPL) | Import pipeline; alignment geometry via our evaluator |
| Polygon offset/Boolean (2D cleanup) | Clipper2 (Boost licence) | Use for tessellated 2D cleanup |

---

## 8. Open design questions

1. Implementation language (affects reuse: kurbo/Rust vs Clothoids+OCCT/C++).
2. Is G3 (Bloss/sine/Viennese rail transitions) needed in v1, or is G2 enough?
3. Target interchange formats in priority order (OpenDRIVE import seems essential
   for road networks; IFC 4.3 and LandXML secondary; OSM as primary *real-world*
   input).
4. Required geometric accuracy for drone-defence use (acoustic reflection may care
   about surface normal accuracy more than position; tolerances per LOD need
   numbers).
5. Do we need exact NURBS export at all (CAD interoperability), or are meshes +
   exact internal geometry sufficient? (Decides whether PH/septic machinery is
   worth building.)
6. Geodetic scope: single projected CRS per scene, or multi-tile/curved-Earth
   scenes (tens of km for drone detection ranges are fine in a projected CRS;
   hundreds of km are not)?

---

## References

* kurbo (traits `ParamCurve`, `ParamCurveArclen`, `ParamCurveFit`, offset/fit):
  <https://github.com/linebender/kurbo>, <https://docs.rs/kurbo/latest/kurbo/>
* Levien, R. *Simplifying Bézier paths* (ParamCurveFit):
  <https://raphlinus.github.io/curves/2023/04/18/bezpath-simplify.html>
* Bertolazzi & Frego Clothoids library: <https://github.com/ebertolazzi/Clothoids>
* libOpenDRIVE: <https://github.com/pageldev/libOpenDRIVE>
* esmini (OpenDRIVE/OpenSCENARIO player with RoadManager): <https://github.com/esmini/esmini>
* IfcOpenShell: <https://ifcopenshell.org/>
* Open CASCADE Technology: <https://dev.opencascade.org/>
* openNURBS: <https://github.com/mcneel/opennurbs>
* Clipper2: <https://github.com/AngusJohnson/Clipper2>
* Piegl & Tiller, *The NURBS Book*, Springer 1997.
* mpmath: <https://mpmath.org/>
* See also the other files in this folder for algorithm references.
