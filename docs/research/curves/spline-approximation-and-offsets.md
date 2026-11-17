# Polynomial Approximation of Transcendental Curves, and Offsets

Part of the [curves research set](README.md). How to convert clothoids,
polynomial spirals and their offsets into Bézier / B-spline / NURBS form with
guaranteed error bounds, how to choose degree and knots, how to preserve G2/G3
continuity, and how to approximate offset (lane edge) curves.

> Source note: page fetches were blocked; sources were located by web search and
> formulas are standard approximation theory or from the cited works.

---

## 1. Why convert, and what "guaranteed" means

The authoritative representation of an alignment is the curvature law
([clothoids-and-spirals.md](clothoids-and-spirals.md)). We still need polynomial
forms because:

* generic B-rep algorithms (intersection, trimming, surface construction, export to
  STEP/IGES/USD/glTF tooling) speak NURBS/Bézier;
* GPUs and tessellators want polynomials or polylines;
* interchange (OpenDRIVE `paramPoly3`, IFC `IfcBSplineCurveWithKnots` fallbacks)
  needs them.

A conversion is a *derived cache* that must carry:

1. the source curve id and parameter interval,
2. a **certified Hausdorff (or parametric) error bound** $\varepsilon$,
3. a parameter map (approximation parameter $u$ ↔ source station $s$) with its own
   error bound — stations matter for roads (objects are placed by $s$),
4. the continuity class achieved at internal knots and at segment joins.

Parametric error $\max_u|\mathbf p(u) - \mathbf r(s(u))|$ bounds the Hausdorff
distance from above and is easy to certify; prefer it.

---

## 2. Approximation by Hermite interpolation

### 2.1 Error bound

Two-point Hermite interpolation matching $\mathbf r, \mathbf r', \dots,
\mathbf r^{(m-1)}$ at both ends of a piece of length $h$ gives a polynomial of
degree $2m-1$ with componentwise error

$$
\mathbf r(s) - \mathbf p(s) = \frac{\mathbf r^{(2m)}(\xi)}{(2m)!}(s-a)^m(s-b)^m
\;\Rightarrow\;
\|\mathbf r-\mathbf p\|_\infty \le \frac{h^{2m}}{2^{2m}(2m)!}\,M_{2m},\quad
M_{2m} = \max|\mathbf r^{(2m)}| \ (\text{per component; } \times\sqrt2 \text{ for the vector norm}).
$$

| Hermite data | degree | bound | continuity at joins |
|---|---|---|---|
| $\mathbf r,\mathbf r'$ | cubic | $\frac{h^4}{384}M_4$ | C1 (G1 + exact unit tangent) |
| $\mathbf r,\mathbf r',\mathbf r''$ | quintic | $\frac{h^6}{46080}M_6$ | C2 ⇒ **G2 with exact curvature** |
| $\mathbf r..\mathbf r'''$ | septic | $\frac{h^8}{10321920}M_8$ | C3 ⇒ G3 |

For curvature-defined curves the derivatives are explicit (from the arc-length
derivative recursion in [numerical-evaluation.md](numerical-evaluation.md) §1), so
$M_{2m}$ is bounded by polynomials in $\max|\kappa^{(j)}|$; e.g. for a clothoid

$$
|\mathbf r^{(4)}| = \sqrt{(\kappa''-\kappa^3)^2 + 9\kappa^2\kappa'^2}\le K^3 + 3K\kappa'_{\max},
\qquad
|\mathbf r^{(6)}| \lesssim K^5 + c_1K^3\kappa' + c_2K\kappa'^2
$$

($K = \max|\kappa|$ on the piece). Because we parameterise the polynomial by the
**source arc length**, the approximation's parameter equals station up to the same
error — no separate parameter map needed. (The polynomial's own speed is not unit;
that is fine.)

### 2.2 Practical numbers

Cubic Bézier approximation of a circular arc of angle $\phi$ (standard
$k = \tfrac43\tan(\phi/4)$ tangent lengths, slightly better than Hermite) has
maximum radial error

$$
\varepsilon \approx R\,\frac{2}{27}\,\frac{\sin^6(\phi/4)}{\cos^2(\phi/4)} \approx \frac{R\,\phi^6}{55296},
$$

i.e. ~$2.7\times10^{-4}R$ for $\phi = 90°$, scaling as $\phi^6$. For $R=200$ m and
1 mm tolerance ⇒ $\phi \lesssim 46°$, i.e. one cubic per ~160 m of arc. Quintic Hermite pieces are
much longer at the same tolerance and give exact curvature at joins.

A highway clothoid ($A = 300$ m, $L=150$ m, $R_{end}=600$ m) at 0.1 mm tolerance
needs a handful of quintic pieces; a tight junction corner ($R=15$ m) needs more.
Numbers are cheap; **the real constraint is continuity and parameter fidelity, not
segment count**.

### 2.3 Knot placement

Equidistribute the error bound: choose piece lengths $h_i$ such that
$h_i^{2m}M_{2m}(\text{piece}_i)\approx$ const. Since $M_{2m}$ depends mainly on
curvature, piece density $\propto \max(|\kappa|^{(2m-1)/(2m)},\dots)$ — rule of
thumb: pieces shrink where curvature is high or changes fast. Algorithm:

```
pieces = [whole segment]
while any piece has bound > eps: bisect it (or split at h* = h * (eps/bound)^(1/2m))
```

Always place knots at:

* source segment boundaries (curvature law changes; κ' jumps between spiral and
  arc ⇒ the curve is only G2 there; a polynomial spanning the join would smear it),
* inflections (optional; helps convexity-based algorithms and bounding),
* station "events" that downstream consumers care about (lane section starts,
  superelevation breakpoints), so that each polynomial span maps to one road
  semantics interval.

### 2.4 Assembling a B-spline

Joined quintic Hermite pieces with matching $\mathbf r,\mathbf r',\mathbf r''$
form a C2 quintic B-spline with knots of multiplicity 3 (= degree − continuity) at
the joins. Knot removal (Tiller) can then reduce multiplicity where the curve is
smoother than required, within the tolerance budget. Alternatively:

* **Global least-squares B-spline fit** (fixed knots, linear LS on dense samples of
  the exact curve) gives fewer control points but no rigorous bound without a
  subsequent check (sample densely + use the bound of the sampled residual plus a
  derivative-based interpolation term).
* **Quasi-interpolation** (local schemes using point and derivative samples) —
  linear cost, local, decent bounds; good for very long alignments.

Degree choice:

| Degree | When |
|---|---|
| 1 (polyline) | GPU tessellation, acoustic/ray meshes; chord rule $h=\sqrt{8\varepsilon/K}$ |
| 2 rational | exact arcs/lines only; never for spirals (no inflection, poor curvature) |
| 3 | interchange (OpenDRIVE paramPoly3, SVG, most tools); G1/C1 only if Hermite; C2 via global interpolation but curvature not exact at knots |
| 5 | **default internal derived form**: exact G2 at joins, fast convergence |
| 7 | G3 needed (rail with Bloss/sine transitions, high-quality surface sweeps, PH septic approximations) |

### 2.5 Shape-aware alternatives

* **Moment/area-matching cubic fit** (Levien, kurbo `fit`): fixes endpoints and
  tangent directions, solves for tangent lengths so that the cubic has the same
  signed area and first moment as the source; error $O(n^{-6})$ — as good as quintic
  Hermite in segment count while staying cubic. G1 only (curvature not matched).
  Best choice when the consumer insists on cubics (OpenDRIVE paramPoly3, SVG,
  fonts) and G1 is acceptable.
* **G2 cubic Hermite with free tangent lengths** (de Boor–Höllig–Sabin 1987): a
  cubic can match position, tangent *and curvature* at both ends (geometric Hermite,
  6th order accurate) by solving a small nonlinear system for the two tangent
  lengths — solutions may not exist for some data. G2-continuous cubic splines with
  $O(h^6)$ error: attractive for paramPoly3 export.
* **PH degree-7 approximations** (Farouki–Pelosi–Sampoli): G2 + exact arc length +
  exact rational offsets.
* **Biarcs / arc splines**: G1 piecewise circular arcs — exact offsets (arcs offset
  to arcs), exact in NURBS. Used in CNC and some road tools for lane-edge export.
  Curvature discontinuous ⇒ unsuitable for surfaces needing smooth shading, fine
  for 2D footprints.
* **Euler-spiral intermediate** (kurbo/Vello): approximate everything by piecewise
  Euler spirals, offset and flatten those. For UberTopo the source already *is*
  Euler spirals, so this is the native path for 2D work.

---

## 3. Continuity preservation (G2/G3)

Rules:

1. **Never** cross a source segment boundary with one polynomial span; the exact
   curve's continuity class changes there.
2. Match derivatives up to order $k$ at every knot from the *exact* curve (Hermite),
   then the approximation is exactly $G^k$ wherever the source is $C^k$ in arc
   length — independent of tolerance.
3. Curvature *values* at knots are then exact; between knots the curvature error
   is $O(h^{2m-2})$, which is what matters for shading, for curvature-based sound
   reflection analysis and for vehicle-dynamics consumers.
4. Global C2 cubic interpolation (natural spline through samples) produces
   curvature *ripples* ("wiggles") between knots — avoid for display of
   road centrelines; the eye (and lighting) sees them.

---

## 4. Offsets (parallel curves) — lane edges, kerbs, shoulders

### 4.1 Geometry of offsets

Constant offset $d$ (left positive, $\kappa>0$ turning left):

$$
\mathbf r_d(s) = \mathbf r(s) + d\,\mathbf N(s),\qquad
\mathbf r_d'(s) = (1 - d\kappa(s))\,\mathbf T(s),
$$

$$
ds_d = (1-d\kappa)\,ds,\qquad \kappa_d = \frac{\kappa}{1-d\kappa}.
$$

* Lines → lines, arcs → concentric arcs (exact in NURBS).
* **Clothoid → not a clothoid**: $\kappa_d$ is not linear in $s_d$. Offsets of
  polynomial curves are not polynomial (degree-10 algebraic for cubics) except for
  PH curves (rational).
* **Cusp/fold** when $d\kappa = 1$ (offset distance reaches the radius of
  curvature); beyond it the offset reverses direction and self-intersects. For
  roads with $R \gg$ half-width this does not happen on the main carriageway; it
  does happen at junction corner radii, roundabout inner edges, hairpins, kerb
  returns with large offsets (e.g. sidewalk back edge), and for **corridor
  daylight lines** offset by tens of metres.

Variable offset (lane width functions $w(s)$, cubic in OpenDRIVE):

$$
\mathbf r_w(s) = \mathbf r(s) + w(s)\mathbf N(s),\quad
\mathbf r_w' = (1 - w\kappa)\mathbf T + w'\mathbf N,\quad
\mathbf r_w'' = -(2w'\kappa + w\kappa')\,\mathbf T + (\kappa - w\kappa^2 + w'')\,\mathbf N
$$

(using $\mathbf T' = \kappa\mathbf N$, $\mathbf N' = -\kappa\mathbf T$; all higher
derivatives follow the same way from $\kappa$'s and $w$'s derivatives).

### 4.2 Recommended approximation strategy: offset the exact curve

**Approximate the offset of the exact curve directly**, using the same Hermite
machinery: the offset curve's derivatives are available in closed form from the
source, so §2 applies verbatim with $M_{2m}$ bounded from $\kappa$, $w$ and their
derivatives. Parameterise by *source* station $s$, so a lane-edge spline's
parameter is the road station — consistent with OpenDRIVE/IFC linear referencing
and making lateral cross-section correspondence trivial ($u$ = same $s$ on every
lane edge ⇒ ruled/lofted lane surfaces between edges are well-parameterised).

**Do not** offset the B-spline approximation of the centreline: the tangent error
of the centreline approximation, $\delta\theta = O(h^{2m-1})$, becomes a position
error $|d|\delta\theta$ in the offset, and the offset of a polynomial is not
polynomial anyway — you would approximate twice.

### 4.3 Classical offset approximation methods (for curves without exact source)

When the source is itself a free-form NURBS (imported or user-drawn):

* **Tiller–Hanson** (1984): offset each control-polygon leg by $d$, intersect
  adjacent offset legs → new control polygon; refine where error is large. Simple;
  best for quadratics per the Elber–Lee–Kim comparison.
* **Least-squares fitting** of sampled offset points/tangents — Elber, Lee & Kim
  (*Comparing offset curve approximation methods*, IEEE CG&A 17(3), 1997) found
  least-squares methods perform very well generally, with caveats about the L2
  norm and sample dependence.
* **Hoschek** least squares with parameter correction; **Cobb**, **Coquillart**
  (quadratic offset with normals); **Lee–Kim–Elber** convolution-based methods.
* **Levien's area/moment fit** (kurbo `offset.rs`, 2022): cusp detection
  ($d\kappa=1$), subdivision at cusps, cubic fit by area/moment, error measured by
  normal-ray projection; $O(n^{-6})$ convergence. Currently the best practical
  2D method for cubic output.
* **Euler-spiral route** (Levien & Uguray 2024): convert to Euler spirals, offset
  analytically-ish, flatten with closed-form subdivision density $\sqrt{|1-s^2|}$
  (in normalised coordinates). Best for direct polyline/arc output.

### 4.4 Trimming offset self-intersections

Global self-intersections of offsets (e.g. the inner edge of a U-turn at large
offset) are not detectable locally. Procedure:

1. Find local cusps ($d\kappa = 1$) from the exact curvature — closed form for
   polynomial κ.
2. Approximate between cusps.
3. Compute self-intersections of the offset approximation (BVH + Newton) and remove
   loops whose winding/orientation is wrong — standard in 2D offsetting (Clipper-
   style polygon offsetting does this for polylines).

For roads, a cleaner alternative is to make the **road surface sweep** the
primary object and derive edges from the surface's valid region (see
[surfaces-from-curves.md](surfaces-from-curves.md)), but junction corners still
require 2D Boolean cleanup.

---

## 5. Converting OpenDRIVE paramPoly3 and other polynomials

`paramPoly3` is already a cubic in local $(u,v)$; convert exactly to a Bézier
(monomial→Bernstein). Its parameter $p$ is not arc length; build an arc-length
map $p(s)$ (Chebyshev fit, [numerical-evaluation.md](numerical-evaluation.md) §4)
so lane offsets (functions of $s$) can be applied. The deprecated `poly3`
($v = a + bu + cu^2 + du^3$) is a cubic graph, also exactly a Bézier.

---

## 6. Flattening (polylines) with guaranteed tolerance

For meshes (rendering, acoustic simulation, physics), a polyline with guaranteed
deviation $\varepsilon$:

* Per piece of bounded curvature $K$: $h\le\sqrt{8\varepsilon/K}$ (chord sagitta
  bound). Adaptive: distribute points with density $\sqrt{|\kappa(s)|/(8\varepsilon)}$
  (Levien's flattening idea) — total count $\int\sqrt{|\kappa|/(8\varepsilon)}\,ds$.
* Offsets: density $\sqrt{|\kappa_d|/(8\varepsilon)}\,ds_d =
  \sqrt{|\kappa(1-d\kappa)|/(8\varepsilon)}\,ds$.
* Arc output (polyarc) instead of lines: error $O(h^3|\kappa'|)$ — dramatically
  fewer pieces for clothoids; useful where the consumer supports arcs (2D maps,
  some CAD exchange).
* For **multiple LODs**, flatten hierarchically: choose a tolerance per LOD and
  keep vertex sets nested (LOD $k+1$ vertices ⊇ LOD $k$) by always splitting at
  existing parameters — avoids cracks between road edge and terrain stitching
  at transitions.

**Shared-edge consistency:** two faces sharing a B-rep edge must tessellate the edge
identically. So tessellate *edges first* (from the edge's curve attribute, by
station), then faces conforming to edge vertices. Standard in OCCT
(`BRepMesh`); mandatory here.

---

## 7. Recommendations

1. Internal derived form: **quintic Hermite B-spline parameterised by source
   station**, knots at all source segment boundaries, error-equidistributed
   subdivision, stored error bound. Septic when G3 is requested.
2. Export to cubic-only consumers: kurbo-style area/moment fit (G1) or
   G2-geometric-Hermite cubics where solvable.
3. Lane edges/kerbs: approximate **offsets of the exact centreline** with the same
   machinery; never offset an approximation.
4. Detect offset cusps analytically ($d\kappa=1$); run 2D Boolean cleanup at
   junctions.
5. Tessellate shared edges once, from the exact curve, with nested LOD vertex sets.
6. Consider PH septic approximations later for exact-length/exact-offset NURBS
   export.

---

## References

* de Boor, C., Höllig, K., Sabin, M. *High accuracy geometric Hermite
  interpolation.* CAGD 4(4), 1987, 269–278.
* Piegl, L., Tiller, W. *The NURBS Book*, 2nd ed., Springer 1997 (knot removal,
  global/LS interpolation, degree elevation).
* Tiller, W., Hanson, E.G. *Offsets of two-dimensional profiles.* IEEE CG&A 4(9),
  1984.
* Elber, G., Lee, I.-K., Kim, M.-S. *Comparing offset curve approximation methods.*
  IEEE CG&A 17(3):62–71, 1997. doi:10.1109/38.586019.
  <https://www.academia.edu/13832982/Comparing_offset_curve_approximation_methods>
* Lee, Kim, Elber. *New approximation methods for planar offset and convolution
  curves.* 1997. <https://researchr.org/publication/LeeKE97>
* Levien, R. *Parallel curves of cubic Béziers* (2022), *Fitting cubic Bézier
  curves* (2021), *Simplifying Bézier paths* (2023):
  <https://raphlinus.github.io/curves/2023/04/18/bezpath-simplify.html>; kurbo
  offset source <https://docs.rs/kurbo/latest/src/kurbo/offset.rs.html>
* Levien, R., Uguray, A. *GPU-friendly stroke expansion.* 2024.
  <https://arxiv.org/abs/2405.00127>
* Farouki, Pelosi, Sampoli. *Approximation of monotone clothoid segments by degree
  7 PH curves.* JCAM 382, 2021. <https://portal.mardi4nfdi.de/wiki/Publication:2196070>
* Chen, Y. et al. *Accurate and efficient approximation of clothoids using Bézier
  curves for path planning.* IEEE T-RO 33(5), 2017.
  <https://ieeexplore.ieee.org/document/7962257>
* Brezak, M., Petrović, I. *Real-time approximation of clothoids with bounded error
  for path planning applications.* IEEE T-RO 30(2), 2014.
  <https://www.bib.irb.hr/641911?rad=641911>
* Meek, D.S., Walton, D.J. *Approximation of discrete data by G1 arc splines* and
  related biarc/arc-spline work (CAD 1992 ff.).
* Clipper2 polygon offsetting library: <https://github.com/AngusJohnson/Clipper2>
* Open CASCADE `BRepMesh` (edge-first tessellation):
  <https://dev.opencascade.org/doc/overview/html/occt_user_guides__mesh.html>
