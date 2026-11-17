# Numerical Evaluation of Curvature-Defined (Transcendental) Curves

Part of the [curves research set](README.md). How to evaluate, measure, project
onto and intersect curves defined by $\kappa(s)$ (clothoids, polynomial spirals,
log-aesthetic and sine-generated curves) and how to control error. Also covers
arc-length reparameterisation of polynomial curves (paramPoly3, Béziers).

> Source note: page fetches were blocked in the sandbox; references were located
> by web search. Formulas are standard numerical analysis or from the cited papers.

---

## 1. Problem statement

Given a segment with start pose $(\mathbf r_0,\theta_0)$, length $L$ and curvature
law $\kappa(s)$, compute for $s\in[0,L]$:

$$
\theta(s) = \theta_0 + \int_0^s\kappa,\qquad
\mathbf r(s) = \mathbf r_0 + \int_0^s \mathbf T(u)\,du,\quad \mathbf T = (\cos\theta,\sin\theta).
$$

Derivatives are free once $\theta$ is known:

$$
\mathbf r' = \mathbf T,\quad \mathbf r'' = \kappa\mathbf N,\quad
\mathbf r''' = \kappa'\mathbf N - \kappa^2\mathbf T,\quad
\mathbf r^{(4)} = (\kappa'' - \kappa^3)\mathbf N - 3\kappa\kappa'\mathbf T, \dots
$$

with $\mathbf N = (-\sin\theta,\cos\theta)$. So **the only hard part is one
integral of a smooth oscillatory function**, $\int e^{i\theta(u)}du$, with
$\theta$ known in closed form (polynomial for polynomial spirals; for LAC and
sine-generated curves, $\theta$ is still closed form).

Requirements for a kernel:

* Absolute position accuracy ~1e-9 relative to segment length (so that
  kilometre-long roads are accurate to sub-micrometre in local coordinates, far
  below any modelling tolerance; leaves headroom for chained segments).
* Deterministic, branch-stable results (bitwise reproducibility across runs on the
  same platform for procedural caching).
* Cheap repeated evaluation (tessellation, projection) — target well under 1 µs
  per point.
* Differentiable w.r.t. curve parameters (for fitting/solving).

---

## 2. Clothoid-specific evaluation

### 2.1 Via Fresnel integrals (large curvature rate)

For $\kappa' \ne 0$: complete the square,
$\theta(s) = \theta_0 + \kappa_0 s + \tfrac12\kappa' s^2
= \theta^\* + \tfrac12\kappa'(s - s^\*)^2$ with $s^\* = -\kappa_0/\kappa'$. With
$a = \sqrt{\pi/|\kappa'|}$, the segment is a similarity image of the unit clothoid
over $t\in[(0-s^\*)/a,\,(L-s^\*)/a]$:

$$
\int_0^s e^{i\theta(u)}du = a\,e^{i\theta^\*}\Big[\big(C(t_1)-C(t_0)\big) \pm i\big(S(t_1)-S(t_0)\big)\Big].
$$

Loss of accuracy: when $|\kappa'|$ is small, $a$ is huge, $t_0,t_1$ are large and
close; the difference $C(t_1)-C(t_0)$ cancels catastrophically. Relative error
grows like $\epsilon_{mach}\cdot a/L$.

### 2.2 Small curvature rate: series in $\kappa'$ (Bertolazzi–Frego)

Use the generalised Fresnel moments $X_k(a,b,c), Y_k(a,b,c)$
(see [clothoids-and-spirals.md](clothoids-and-spirals.md) §2.3) with
$a = \kappa' L^2$, $b = \kappa_0 L$, $c = \theta_0$:

$$
\mathbf r(L) - \mathbf r_0 = L\,\big(X_0(a,b,c),\; Y_0(a,b,c)\big).
$$

For $|a| < \varepsilon$ expand $\cos(\tfrac a2\tau^2 + \dots)$ in powers of $a$; the
coefficients are moments of $\cos(b\tau + c)$, $\sin(b\tau+c)$ against $\tau^{2n+k}$,
computable stably by recurrence (with Lommel-function care for small $b$). For
$|a|\ge\varepsilon$ use the Fresnel route. A threshold around $\varepsilon\approx 0.01$
keeps both branches accurate to ~1e-15 relative. This is what `ebertolazzi/Clothoids`
does; copy the structure, test against it.

### 2.3 Local Taylor/Gauss per piece (generic, recommended)

Rather than special-casing clothoids, split the segment into pieces of bounded
turning and integrate locally — no large arguments ever appear, and the same code
works for any $\kappa(s)$.

---

## 3. Generic integration of $\int e^{i\theta(s)}ds$

### 3.1 Gauss–Legendre quadrature per piece

On a piece $[s_a, s_b]$ of length $h$, $n$-point Gauss–Legendre:

$$
\int_{s_a}^{s_b} e^{i\theta(s)}ds \approx \frac h2\sum_{j=1}^n w_j\, e^{i\theta\left(\frac{s_a+s_b}{2} + \frac h2 x_j\right)} .
$$

It is exact for polynomials of degree $2n-1$. For an integrand analytic inside the
Bernstein ellipse $E_\rho$ the error decays like $O(\rho^{-2n})$. For
$e^{i\theta}$ with $\theta$ polynomial, the governing quantity is the **variation
of $\theta$ over the piece** (the "bend"). Rule of thumb from experience with
Spiro/kurbo-type code:

* bend ≤ 0.5 rad: $n=8$ gives ~1e-14 relative,
* bend ≤ 1 rad: $n=10$–$12$,
* larger: subdivide.

**Adaptive error control:** compare $n$ and $2n$ (or use Gauss–Kronrod 7/15 or
10/21) and bisect where the estimate exceeds the tolerance. Because the integrand
is cheap and smooth, a *fixed* subdivision based on a priori bend bounds is better
for determinism and SIMD: choose pieces so that
$\max|\theta'| h \le \beta$ and $\max|\theta''| h^2 \le \beta$, then fixed $n$.

### 3.2 Taylor series per piece

Expand around the piece midpoint $m$: $\theta(m + u) = \theta_m + \kappa_m u +
\tfrac12\kappa'_m u^2 + \dots$, then

$$
e^{i\theta(m+u)} = e^{i\theta_m}\,e^{i(\kappa_m u + \frac12\kappa'_m u^2 + \dots)}
$$

and expand the second exponential as a power series in $u$ (the coefficients come
from a simple recurrence: if $E(u) = e^{iP(u)}$ then $E' = iP'E$, giving
$e_{k+1} = \frac{i}{k+1}\sum_j (j+1)p_{j+1}e_{k-j}$). Integrate term by term. With
the piece bend bounded by ~1 rad, ~12–16 terms reach double precision.

Advantages over quadrature:

* Gives a **polynomial in $u$** for position on the whole piece — evaluation of
  many points (tessellation) costs one Horner per point. This *is* a polynomial
  basis approximation with a known remainder bound: it is the natural bridge to
  B-splines (see [spline-approximation-and-offsets.md](spline-approximation-and-offsets.md)).
* Analytic derivatives w.r.t. curve parameters for Newton Jacobians (Levien's Spiro
  does exactly this in the $k_0..k_3$ parameters).

Remainder bound: if $|\theta^{(j)}|$ are bounded on the piece, $|E^{(N)}(u)|$ is
bounded by a Bell-polynomial expression; in practice use the next omitted term
times a safety factor of 2–4, verified by tests against high-precision (mpmath)
results.

### 3.3 Chebyshev representation (alternative)

Sample $e^{i\theta}$ at Chebyshev points on each piece, build Chebyshev series,
integrate exactly (Clenshaw–Curtis). Same accuracy class as Gauss; the result is a
Chebyshev polynomial of position directly, convertible to Bernstein/Bézier form
exactly (basis change). The "chebfun" style is an excellent fit for a kernel that
wants **one polynomial cache per piece** with an a-posteriori error (size of tail
coefficients).

### 3.4 ODE integration (when κ is not a pure function of s)

If curvature depends on state — e.g. a vehicle path with $\kappa$ limited by speed,
a river meander simulation, a path that follows terrain — the curve is the solution
of $\dot{\mathbf r} = \mathbf T(\theta)$, $\dot\theta = \kappa(s,\mathbf r,\theta)$.
Use an embedded Runge–Kutta (Dormand–Prince 5(4) or Verner 8(7)) with dense
output; store the dense-output polynomials as the piecewise representation. For
pure $\kappa(s)$ always prefer §3.1–3.3 (no error accumulation in $\theta$, which is
integrated analytically).

### 3.5 Accumulation over many segments

Long alignments chain hundreds of segments. Two policies:

1. **Stored start poses** (OpenDRIVE/IFC style): each segment carries its start
   pose; no accumulation, but gaps appear if data is inconsistent. Detect and
   report gaps > tolerance; optionally repair by redistributing.
2. **Chained** (design-intent style): start pose of segment $k+1$ = end pose of
   segment $k$. Accumulates rounding error ~$\sqrt{N}\epsilon$ (fine) but also
   propagates any heading error linearly into position at distance: heading error
   $\delta\theta$ at station 0 moves station 10 km by $10^4\delta\theta$ m. Compute
   end poses with compensated summation, and compute headings analytically from
   $\theta_0 + \int\kappa$ (exact for polynomial $\kappa$).

Recommendation: keep design intent chained, but **materialise and store** each
segment's start pose after solving (as the standards do), and evaluate each
segment locally from its own start pose. Evaluation then never depends on segment
count.

---

## 4. Arc length and reparameterisation

### 4.1 Curvature-defined curves

Arc length *is* the parameter; nothing to do. The 3D gradient curve needs
$\sigma(s) = \int_0^s\sqrt{1+z'(u)^2}du$ (Gauss–Legendre; $z$ is piecewise
polynomial, integrand smooth per piece).

### 4.2 Polynomial curves (paramPoly3, Bézier, B-spline)

$s(t) = \int_0^t|\mathbf r'(\tau)|d\tau$. $|\mathbf r'|$ is the square root of a
polynomial — smooth unless $\mathbf r'$ vanishes (cusp). Methods:

* Gauss–Legendre on spans, adaptive bisection (Levien, "How long is that
  Bézier?": ~16-point GL with error estimate; Gravesen's bound
  $\tfrac12(\text{chord} + \text{control polygon length})$ with subdivision gives
  guaranteed bounds).
* For an inverse map $t(s)$: Newton on $F(t) = s(t) - s^\* $,
  $F'(t) = |\mathbf r'(t)|$, safeguarded by bisection (bracket from a monotone
  table). Or build a **Chebyshev/polynomial approximation of $t(s)$** per span to
  tolerance once, then evaluate cheaply — preferred for OpenDRIVE paramPoly3 where
  lane offsets are functions of *arc length* $s$ but geometry is in $p$.
* PH curves: $s(t)$ is an exact polynomial; invert by polynomial root finding.

---

## 5. Closest point / projection

Given point $\mathbf p$, find $s$ minimising $|\mathbf r(s) - \mathbf p|^2$.
Stationary condition and its derivative for an arc-length parameterised plane curve:

$$
f(s) = (\mathbf r(s)-\mathbf p)\cdot\mathbf T(s) = 0,\qquad
f'(s) = 1 + \kappa(s)\,(\mathbf r(s)-\mathbf p)\cdot\mathbf N(s).
$$

$f'$ vanishes when $\mathbf p$ is at the centre of curvature — the projection is
non-unique (evolute). Robust algorithm:

1. **Localise**: bounding hierarchy over pieces of bounded turning (§7); keep
   candidate pieces whose min-distance lower bound ≤ current best upper bound.
2. On each candidate piece, the number of stationary points is small: Frego &
   Bertolazzi (*Point-clothoid distance and projection computation*, SIAM J. Sci.
   Comput. 41(5), 2019) split a clothoid into ≤ 4 sub-segments and show, with a
   follow-up note proving at most three stationary points of squared distance for
   a non-inflecting clothoid segment of turning ≤ 2π, that a guarded Newton/
   bisection per sub-segment suffices.
3. Newton on $f$ with bracketing (fall back to bisection when $f'\le 0$ or the step
   leaves the bracket). Converges quadratically; ~3–5 iterations.
4. Compare endpoint distances; return the global minimum and *all* local minima if
   requested (needed for "which lane is this point in" when roads double back).

**Curvilinear coordinates $(s,t)$:** the inverse map from world $(x,y)$ to road
coordinates is exactly this projection, with $t = (\mathbf p - \mathbf r(s))\cdot\mathbf N(s)$.
It is well-defined only within the curvature tube $|t| < 1/|\kappa|$ — real road
data violates this at tight junction corners. The API must return
"ambiguous/out-of-tube" explicitly.

---

## 6. Intersections between transcendental curves

No algebraic method applies (no implicit form). Standard approach is
**subdivision + bounding + Newton**:

1. **Bounding volumes** per piece with guaranteed containment:
   * *Tangent triangle* (Bertolazzi–Bevilacqua–Frego 2020): for a piece with total
     turning $|\Delta\theta| < \pi$ and monotone $\theta$ (no inflection inside —
     split at inflections, $\kappa=0$, which are closed form for polynomial
     $\kappa$), the curve lies in the triangle formed by the two endpoints and the
     intersection of their tangent lines.
   * *Fat arcs / bounding circles*: for small turning, the piece lies within
     distance $\le \tfrac18 h^2 \max|\kappa|$-ish of its chord (sagitta bound);
     with known $\max|\kappa|$ this gives a rigorous "fat line" (chord ± bound).
   * AABBs of the above for BVH acceleration.
2. **Hierarchy**: build a BVH (or the tree of tangent triangles) per curve; traverse
   pairs; discard non-overlapping pairs.
3. **Leaf solve**: 2D Newton on $\mathbf F(s_1,s_2) = \mathbf r_1(s_1) - \mathbf r_2(s_2) = 0$,
   Jacobian $[\mathbf T_1, -\mathbf T_2]$, determinant $\sin(\theta_2-\theta_1)$ —
   singular for tangential intersections.
4. **Tangential and overlapping cases**: if $|\det|$ is small, switch to minimising
   $|\mathbf F|^2$ and classify as touching (distance below tolerance at a
   single point) or overlapping (two clothoid segments with identical
   $(\kappa_0,\kappa')$ and a shared point — detectable *exactly* from parameters:
   two curvature-defined curves coincide on an interval iff their curvature
   functions match after shifting $s$ and one point+tangent coincide). This
   parametric coincidence test is a big robustness win over polynomial
   approximations, where overlap detection is fuzzy.
5. **Certification (optional)**: interval arithmetic or Krawczyk test on the leaf
   box to certify a unique root — worth it for B-rep construction where a missed or
   duplicated intersection breaks topology.

Alternative used by many kernels: intersect the **B-spline approximations** with
Bézier clipping / subdivision, then refine the result on the exact curves with
Newton. Fine as long as the approximation tolerance is folded into the
bounding-box inflation (otherwise near-tangent intersections can be missed).

Curve–surface (e.g. alignment vs. terrain, for bridge/tunnel portals; flight path
vs. road corridor) uses the same pattern with 3-variable Newton.

---

## 7. Bounding and turning bounds

For polynomial $\kappa(s)$ on a piece, $\max|\kappa|$ and the total turning are
computable exactly (roots of $\kappa'$ and $\kappa$). For LAC and sine-generated
curves, also closed form. This is the key asset of curvature-defined curves: **all
the quantities needed for rigorous adaptive subdivision are cheap and exact.**

Sagitta/chord deviation for a piece of length $h$ with $|\kappa|\le K$:

$$
\max_s \operatorname{dist}(\mathbf r(s), \text{chord}) \le \frac{K h^2}{8} \quad(\text{for } Kh \ll 1;\ \text{exact for arcs: } R(1-\cos\tfrac{h}{2R})).
$$

Used for (a) bounding volumes, (b) polyline tessellation to tolerance: choose
$h \le \sqrt{8\varepsilon/K}$. Total segments for a clothoid from $\kappa=0$ to
$\kappa_1$ over $L$: $N \approx \int_0^L\sqrt{\kappa(s)/(8\varepsilon)}\,ds =
\tfrac23 L\sqrt{\kappa_1/(8\varepsilon)}$.

---

## 8. Error control strategy (summary)

| Operation | Method | Error control |
|---|---|---|
| pose at $s$ | local Taylor/Gauss on bounded-bend piece from stored piece start | a priori bend bound → fixed order; tested vs mpmath |
| many poses (tessellation) | per-piece polynomial cache (Taylor/Chebyshev) | tail coefficient bound |
| $X_k, Y_k$ for fitting | Bertolazzi–Frego two-regime | threshold on $|a|$ |
| arc length of polynomials | adaptive GL | GL($n$) vs GL($2n$) |
| $t(s)$ inverse | safeguarded Newton or Chebyshev fit | residual in $s$ |
| projection | BVH + guarded Newton | residual + bracket width |
| intersection | BVH/tangent triangles + Newton (+ Krawczyk) | box width, certified uniqueness |

Test oracle: an **mpmath**-based reference (arbitrary precision Fresnel and
quadrature) plus `ebertolazzi/Clothoids` for cross-checking; property tests
(splitting at $s$ and re-evaluating must agree to 1e-12; reversing a curve must
negate curvature; similarity transforms commute with evaluation).

---

## References

* Bertolazzi, E., Frego, M. *Fast and accurate clothoid fitting* / *G1 fitting with
  clothoids* (generalised Fresnel integrals, two-regime evaluation).
  <https://arxiv.org/abs/1209.0910>, <https://arxiv.org/abs/1305.6644>
* Frego, M., Bertolazzi, E. *Point-clothoid distance and projection computation.*
  SIAM J. Sci. Comput. 41(5):A3326–A3353, 2019. doi:10.1137/18M1200439.
  <https://iris.unitn.it/handle/11572/243762>
* *A note on the point-clothoid distance algorithm* (at most three stationary
  points): <https://arxiv.org/pdf/2609.10179>
* Bertolazzi, Bevilacqua, Frego. *Efficient intersection between splines of
  clothoids.* Math. Comput. Simul. 176:57–72, 2020.
  <https://ideas.repec.org/a/eee/matcom/v176y2020icp57-72.html>
* Clothoids library: <https://github.com/ebertolazzi/Clothoids>
* Levien, R. *How long is that Bézier?*
  <https://raphlinus.github.io/curves/2018/12/28/bezier-arclength.html>
* Levien, R. *From Spiral to Spline* (Spiro integration by Taylor series),
  <https://levien.com/phd/thesis.pdf>; Spiro source <https://github.com/raphlinus/spiro>
* Gravesen, J. *Adaptive subdivision and the length and energy of Bézier curves.*
  Comput. Geom. 8 (1997) 13–31.
* Trefethen, L.N. *Approximation Theory and Approximation Practice* (Chebyshev
  methods, Bernstein-ellipse convergence). <https://people.maths.ox.ac.uk/trefethen/ATAP/>
* Trefethen, L.N. *Is Gauss quadrature better than Clenshaw–Curtis?* SIAM Review
  50(1), 2008.
* Hairer, Nørsett, Wanner. *Solving Ordinary Differential Equations I* (embedded RK,
  dense output).
* DLMF chapter 7 (Fresnel integrals): <https://dlmf.nist.gov/7>
* mpmath (arbitrary precision reference): <https://mpmath.org/>
* Sederberg, T.W., Nishita, T. *Curve intersection using Bézier clipping.* CAD 22(9),
  1990.
* Krawczyk / interval Newton: Moore, Kearfott, Cloud, *Introduction to Interval
  Analysis*, SIAM 2009.
