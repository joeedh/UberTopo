# Clothoids, Spirals and Curvature-Defined Curves

Part of the [curves research set](README.md). This file covers the *curve families*
that matter for procedural environment modeling: the clothoid (Euler/Cornu spiral)
and its evaluation via Fresnel integrals, Hermite fitting with clothoids, clothoid
splines, Raph Levien's spiral splines (Spiro and the later Euler-spiral work in
kurbo), polynomial spirals, Hobby splines, κ-curves, log-aesthetic curves and
Pythagorean-hodograph (PH) curves.

> Source note: web *search* was available while writing this, but direct page
> fetches were blocked by the sandbox network policy (arxiv.org, buildingsmart.org,
> asam.net, etc. could not be opened). Citations below were confirmed to exist via
> search results; formulas are from the cited literature as known to the author and
> should be checked against the originals before they become test oracles.

---

## 1. Why curvature-defined curves

Polynomial curves (Bézier, B-spline, NURBS) are defined by *position* as a function
of a parameter. Almost every engineered linear feature in the built environment —
roads, railways, canals, runways' taxiway fillets, kerbs, cycle paths — is designed
instead by specifying **curvature as a function of arc length** (the *Cesàro* or
*intrinsic* equation):

$$
\kappa = \kappa(s), \qquad
\theta(s) = \theta_0 + \int_0^s \kappa(u)\,du, \qquad
\mathbf r(s) = \mathbf r_0 + \int_0^s \begin{pmatrix}\cos\theta(u)\\ \sin\theta(u)\end{pmatrix} du .
$$

The reasons are physical. A vehicle moving at constant speed $v$ along the curve
experiences lateral acceleration $a = v^2\kappa$ and *lateral jerk*
$j = v^3\,d\kappa/ds$. Bounded jerk ⇒ curvature must be continuous (G2) and its
derivative bounded ⇒ straight → arc transitions need a "spiral" whose curvature
ramps up. Linear ramp ⇒ clothoid. Natural features also show this structure:
meandering rivers are well modelled by *sine-generated curves*, i.e.
$\theta(s) = \omega \sin(2\pi s/M)$ (Langbein & Leopold, 1966; see
[space-curves-and-frames.md](space-curves-and-frames.md) §6).

Consequence for UberTopo: the *design representation* of most linear features is
not a polynomial; it is a short list of curvature-function parameters per segment.
Polynomial bases (B-splines) are a *derived* representation used for tessellation,
interchange and generic geometric algorithms (see
[spline-approximation-and-offsets.md](spline-approximation-and-offsets.md)).

---

## 2. The clothoid (Euler spiral, Cornu spiral)

### 2.1 Definition

Curvature linear in arc length:

$$
\kappa(s) = \kappa_0 + \kappa' s, \qquad
\theta(s) = \theta_0 + \kappa_0 s + \tfrac12 \kappa' s^2 .
$$

The *clothoid parameter* (road engineering) $A$ satisfies $A^2 = R\,L$ for a spiral
starting at $\kappa=0$ and reaching radius $R$ after length $L$, i.e.
$\kappa' = 1/A^2$. Lines ($\kappa_0=\kappa'=0$) and circular arcs ($\kappa'=0$) are
degenerate members of the family, which is why a road alignment can be stored as a
uniform list of "clothoid segments" $(\mathbf r_0,\theta_0,\kappa_0,\kappa',L)$ —
exactly what Bertolazzi & Frego's `ClothoidCurve` and OpenDRIVE's `<spiral>`
(curvStart, curvEnd, length) do.

### 2.2 Normalised form and Fresnel integrals

The standard ("unit") clothoid $\kappa(s) = \pi s$ has

$$
x(t) = C(t) = \int_0^t \cos\!\left(\tfrac{\pi}{2}u^2\right)du, \qquad
y(t) = S(t) = \int_0^t \sin\!\left(\tfrac{\pi}{2}u^2\right)du .
$$

Both tend to $\tfrac12$ as $t\to\infty$ (the spiral winds into the point
$(\tfrac12,\tfrac12)$). A general clothoid segment with $\kappa'\neq 0$ is an affine
(similarity) image of a piece of the unit clothoid: shift $s$ so the curvature is
zero at the origin ($s^\* = -\kappa_0/\kappa'$), scale by
$a = \sqrt{\pi/|\kappa'|}$, rotate and possibly reflect. So general evaluation is
"two Fresnel evaluations + a similarity transform". **This is numerically poor when
$\kappa'$ is tiny** (the scale $a$ blows up and you subtract two huge, nearly equal
Fresnel values): the near-circle and near-line regimes need a different formula
(series in $\kappa'$), see [numerical-evaluation.md](numerical-evaluation.md) §2.

### 2.3 Evaluating Fresnel integrals

| Method | Domain | Accuracy | Notes |
|---|---|---|---|
| Power series | small $t$ (say $t<2$) | arbitrary | cancellation grows like $e^{\pi t^2/2}$-ish for large $t$ |
| Asymptotic expansion via auxiliary $f,g$ | large $t$ | improves with $t$ | divergent series, truncate at smallest term |
| A&S 7.3.32–33 / Heald (1985) rational $f,g$ | all $t\ge0$ | $2\times10^{-3}$ … $4\times10^{-8}$ | cheap, one formula on $[0,\infty)$; good for initial guesses only |
| Boersma (1960) | split at $x=4$ | ~$10^{-9}$ | Lanczos τ-method; basis of later Mielenz work |
| Cody (1968) Chebyshev rationals | split at $|x|=1.6$ | double | basis of netlib/JPL code (Snyder 1992) |
| Cephes `fresnl` (Moshier) | split at $x^2 = 2.5625$ | ~2e-15 relative peak | used by SciPy; good default |

**Power series** (convergent for all $t$, use for $|t|\lesssim 2$):

$$
C(t) = \sum_{n\ge0} \frac{(-1)^n (\pi/2)^{2n}}{(2n)!\,(4n+1)} t^{4n+1}, \qquad
S(t) = \sum_{n\ge0} \frac{(-1)^n (\pi/2)^{2n+1}}{(2n+1)!\,(4n+3)} t^{4n+3}.
$$

**Auxiliary functions** for large $t$:

$$
C(t) = \tfrac12 + f(t)\sin\!\left(\tfrac{\pi}{2}t^2\right) - g(t)\cos\!\left(\tfrac{\pi}{2}t^2\right),
\quad
S(t) = \tfrac12 - f(t)\cos\!\left(\tfrac{\pi}{2}t^2\right) - g(t)\sin\!\left(\tfrac{\pi}{2}t^2\right),
$$

$$
f(t) \sim \frac{1}{\pi t}\left(1 - \frac{1\cdot3}{(\pi t^2)^2} + \frac{1\cdot3\cdot5\cdot7}{(\pi t^2)^4}-\dots\right), \quad
g(t) \sim \frac{1}{\pi^2 t^3}\left(1 - \frac{1\cdot3\cdot5}{(\pi t^2)^2} + \dots\right).
$$

The cheap rational approximations (Abramowitz & Stegun 7.3.32–33, error ≤ 2e-3):

$$
f(t) \approx \frac{1 + 0.926t}{2 + 1.792t + 3.104t^2}, \qquad
g(t) \approx \frac{1}{2 + 4.142t + 3.492t^2 + 6.670t^3}.
$$

Heald (1985) generalises these into a family of rational approximations over the
undivided domain $[0,\infty)$ with max errors from $1.7\times10^{-3}$ to
$4\times10^{-8}$. Useful as a fast first guess for Newton iterations, not as a
kernel-grade evaluator.

**Implementation note.** Cephes `fresnl` uses rational approximations in $t^4$
for $t^2<2.5625$ and rational $f,g$ in $1/(\pi t^2)^2$ beyond, and is accurate to
about 2e-15 relative over $[0,10]$. The argument $\tfrac{\pi}{2}t^2$ in the sine/cos
must be reduced carefully for large $t$ (Cephes computes $\tfrac{\pi}{2}t^2$
directly; for very long spirals evaluate $t^2$ mod 4 in extended precision or use
the per-segment local formulation of §2 of numerical-evaluation instead, which
never sees large arguments).

**Generalised Fresnel integrals (Bertolazzi & Frego).** For fitting, the useful
primitives are the *moments*

$$
X_k(a,b,c) = \int_0^1 \tau^k \cos\!\left(\tfrac a2\tau^2 + b\tau + c\right)d\tau,\qquad
Y_k(a,b,c) = \int_0^1 \tau^k \sin\!\left(\tfrac a2\tau^2 + b\tau + c\right)d\tau .
$$

A clothoid segment of length $L$ with $\theta(s)=\theta_0+\kappa_0 s+\tfrac12\kappa' s^2$
ends at $\mathbf r_0 + L\,(X_0, Y_0)(\kappa' L^2, \kappa_0 L, \theta_0)$.
Bertolazzi & Frego compute $X_k,Y_k$ with two regimes: a "large $|a|$" formula via
standard Fresnel integrals plus a recurrence in $k$, and a "small $|a|$" series in $a$
whose zero-$a$ terms need reduced Lommel functions. The threshold is chosen so that
neither branch loses more than a couple of digits. Their C++ `Clothoids` library
(`ebertolazzi/Clothoids`, BSD-style licence) is the best open reference
implementation and should be used as a test oracle.

### 2.4 Clothoid properties relevant to a kernel

* **Arc length is the parameter.** Arc-length queries, station queries and
  "point at distance" are trivial — a big advantage over polynomials.
* **Tangent angle is an explicit quadratic** in $s$: inflection points, total
  turning, monotonic-curvature tests and tangent-direction queries are closed form.
* **Bounding:** a sub-segment whose total turning $|\Delta\theta|<\pi$ lies inside the
  triangle formed by its endpoints and the intersection of its end tangents
  ("tangent triangle"). Bertolazzi, Bevilacqua & Frego (2020) build intersection
  hierarchies from these triangles.
* **Not closed under offset.** The parallel curve at distance $d$ has
  $\kappa_d = \kappa/(1-d\kappa)$ as a function of the *base* arc length; it is
  not a clothoid. Lane edges are therefore *not* clothoids (see
  [spline-approximation-and-offsets.md](spline-approximation-and-offsets.md)).
* **Not closed under affine maps** other than similarities (scaling the map
  anisotropically breaks the curvature law) — irrelevant for CAD but relevant for
  map projections: a clothoid designed in a national grid is a clothoid only in that
  projected plane, not on the ellipsoid.

---

## 3. Hermite fitting with clothoids

### 3.1 G1 Hermite: one clothoid through two poses

Problem: given $(x_0,y_0,\theta_0)$ and $(x_1,y_1,\theta_1)$ find
$(\kappa_0,\kappa',L)$. Three unknowns, three equations, and the solution is unique
once you restrict total turning to $<2\pi$-ish branches.

Bertolazzi & Frego (2015, *Math. Methods Appl. Sci.* 38(5)) reduce it to **one
scalar nonlinear equation**. With chord $\Delta = (x_1-x_0, y_1-y_0)$,
$r = |\Delta|$, $\varphi = \operatorname{atan2}(\Delta_y,\Delta_x)$,
$\phi_0 = \theta_0-\varphi$, $\phi_1=\theta_1-\varphi$, $\delta = \phi_1-\phi_0$,
write the tangent angle (relative to the chord) as
$\vartheta(\tau) = \phi_0 + (\delta - A)\tau + A\tau^2$ on $\tau\in[0,1]$. The end
point lies on the chord iff

$$
g(A) = Y_0(2A,\; \delta - A,\; \phi_0) = 0,
$$

and then

$$
L = \frac{r}{X_0(2A,\,\delta-A,\,\phi_0)},\qquad
\kappa_0 = \frac{\delta - A}{L},\qquad
\kappa' = \frac{2A}{L^2}.
$$

A small-angle expansion ($\sin\vartheta\approx\vartheta$) gives the initial guess
$A_0 = 3(\phi_0+\phi_1)$; the paper refines this with a fitted polynomial
correction and Newton then converges in ≤ ~4 iterations to machine precision across
the whole angle domain. This is the routine every road/rail tool needs ("connect
these two poses with a spiral").

Older approaches: Meek & Walton's work on Cornu spirals and *clothoid spline
transition spirals* (Math. Comp. 59, 1992) solved related problems (join two
points with given unit tangents and curvatures) and gave existence/uniqueness
conditions; Levien's thesis surveys the history.

### 3.2 G2 Hermite: matching curvature too

Matching $(\mathbf r,\theta,\kappa)$ at both ends is 8 constraints (including
"curvature continuity" at both ends). One clothoid has only 3 free shape
parameters, two clothoids are not always enough either. Bertolazzi & Frego, *On the
G2 Hermite interpolation problem with clothoids* (J. Comput. Appl. Math. 341,
2018) prove that one or two segments do not always admit a solution and give a
**three-segment** scheme (8 equations, 10 unknowns), reduced to 2 equations solved
by Newton; tested over a dense hypercube of angle/curvature configurations, it
always converged in ≤ 5 iterations. The `Clothoids` library exposes this as
`G2solve3arc`, and `pyclothoids` wraps it.

### 3.3 Clothoid splines

Two common constructions:

1. **Interpolating G1 clothoid spline** — independent G1 Hermite per span after
   choosing nodal tangent angles (e.g. by a heuristic or by minimising curvature
   jumps).
2. **Interpolating G2 clothoid spline** — unknown nodal angles chosen so that
   curvature matches at each node: a nonlinear system with tridiagonal-like
   structure, solved by Newton. Bertolazzi, Bevilacqua & Frego ("Interpolating
   clothoid splines with curvature continuity", Math. Methods Appl. Sci. 2018) and
   Levien's Spiro (below) both do this. With Euler-spiral segments the resulting
   curve is G2; curvature is piecewise linear and continuous — exactly the
   road/rail continuity class.

For procedural generation of roads, **the G1 Hermite fit plus explicit design
parameters** (radius, spiral length) is usually more useful than a fully
interpolating spline, because engineers design with PI points and radii, not
through-points (see [road-alignment-standards.md](road-alignment-standards.md)).

---

## 4. Raph Levien: Spiro, polynomial spirals and Euler spirals in kurbo

### 4.1 The thesis and the history paper

* *The Euler spiral: a mathematical history* (UC Berkeley tech report
  EECS-2008-111): Bernoulli posed the elastica problem; Euler characterised the
  curve (1744); Fresnel (diffraction, 1818) and Cornu (1874) rediscovered it;
  Talbot (1890) introduced it as the *railway transition spiral*. The curve was
  reinvented several times in unrelated fields.
* *From Spiral to Spline: Optimal Techniques in Interactive Curve Design* (PhD
  thesis, Berkeley 2009, EECS-2009-162). Central thesis: an ideal interpolating
  spline is made of segments cut from **one fixed generating curve**, so
  designing a spline reduces to choosing the curve; the Euler spiral is the leading
  candidate (log-aesthetic curves maybe slightly better in some cases). It defines
  extensionality, roundness, locality, and analyses existing splines (Bézier,
  Hobby, elastica, MEC, clothoid splines) against those criteria.
* Levien & Séquin, "Interpolating splines: which is the fairest of them all?"
  (CAD&A 2009) is the short version.

### 4.2 Spiro

Spiro is a curve library and editing model (used in FontForge, Inkscape) built
from **polynomial spirals**: each segment's curvature is a polynomial in arc length

$$
\kappa(s) = k_0 + k_1 s + \tfrac12 k_2 s^2 + \tfrac16 k_3 s^3, \quad s\in[-\tfrac12,\tfrac12]
$$

(normalised to unit chord, centred). G2 nodes use Euler spirals ($k_2=k_3=0$),
"G4" nodes use the cubic-curvature spiral. The global solve: unknowns are the $k$
per segment; constraints are curvature (and higher derivative) continuity at nodes
and endpoint chord matching; Newton with a banded Jacobian. Key engineering ideas
worth stealing:

* **Integration by Taylor series in the curvature parameters.** For a segment
  with modest total bend (|bend| ≲ 1 rad), the endpoint/chord integral
  $\int_{-1/2}^{1/2} e^{i\theta(s)}ds$ is expanded as a polynomial in $(k_0..k_3)$,
  with hand-derived (code-generated) terms up to ~12th order; larger bends are
  recursively subdivided. Cheap, branch-free and fully differentiable — good for
  Newton Jacobians.
* **Normalise to the chord**, solve in the canonical frame, then place with a
  similarity transform. Never integrate across long distances in world coordinates.

### 4.3 Euler spirals as a computational intermediate (kurbo)

Levien's later blog/paper work (kurbo, Vello) uses Euler spiral segments as an
*intermediate representation* between cubic Béziers and output:

* "Cleaner parallel curves with Euler spirals" (blog, 2021) and Levien & Uguray,
  *GPU-friendly Stroke Expansion* (HPG 2024, arXiv:2405.00127): convert cubic
  Béziers to piecewise Euler spirals with a *closed-form error estimate* (no need
  to build the approximation to measure it), then compute parallel curves
  (offsets) and flatten them to lines/arcs. The Euler spiral's parallel curve has a
  simple definition and clean cusp behaviour; with inflection and cusp positions
  normalised, the flattening subdivision density is simply $\sqrt{|1-s^2|}$, so the
  number of output segments is an integrable closed form. Euler spirals represent
  inflections natively, so no inflection splitting is needed (unlike quadratic
  Béziers).
* "Parallel curves of cubic Béziers" (blog, 2022) and "Fitting cubic Bézier
  curves" / kurbo's `fit` and `offset` modules: approximate any smooth curve by
  cubic Béziers by matching **signed area and first moment** of the source
  segment, solving a quartic for the tangent lengths, with an error metric
  (Tiller–Hanson-style normal projection as a Fréchet-distance proxy). Achieved
  error scales as $O(n^{-6})$ in the number of segments (vs $O(n^{-4})$ for naive
  Hermite cubics), i.e. near-optimal. kurbo's `ParamCurveFit` trait (introduced in
  "bezpath simplify", 2023) is a clean API pattern: anything that can report
  position+derivative samples and moment integrals over a range can be fitted.
* "How long is that Bézier?" (blog, 2018): arc length of Béziers — closed form for
  quadratics, Gauss–Legendre quadrature with error estimation for cubics, Gravesen's
  subdivision bound for guaranteed results.
* "Hyperbezier" (Linebender blog): a family of curves aimed at interactive design
  combining Bézier-like control with spiral-like fairness.

**Takeaway for UberTopo:** kurbo is 2D, f64, MIT/Apache licensed and very
well-engineered; its `ParamCurve*` trait family is a good model for our curve
trait (see [curve-library-design.md](curve-library-design.md)), and its
curve-fitting core can be adopted for *any* curve type that exposes position,
derivative and moment integrals.

---

## 5. Polynomial spirals / generalised clothoids

Generalise the clothoid to $\kappa(s) = \sum_{i=0}^{n} c_i s^i$, so
$\theta(s)$ is a polynomial of degree $n+1$ and

$$
\mathbf r(s) = \mathbf r_0 + \int_0^s e^{i\theta(u)}du
$$

(complex notation). Uses:

* **Vehicle path planning** (Kelly & Nagy 2003, "Reactive nonholonomic trajectory
  generation via parametric optimal control"; Nagy & Kelly cubic spirals):
  $\kappa$ cubic in $s$ lets you match position, heading and curvature at both ends
  with 4 coefficients + length (5 unknowns for 5 constraints — the G2 Hermite
  problem has a *single-segment* solution with a cubic spiral, unlike the clothoid).
* **IFC 4.3**: `IfcSecondOrderPolynomialSpiral`, `IfcThirdOrderPolynomialSpiral`,
  `IfcSeventhOrderPolynomialSpiral` are exactly polynomial spirals; Bloss and
  Viennese-bend transitions are polynomial-curvature spirals with fixed
  coefficient patterns (see [road-alignment-standards.md](road-alignment-standards.md)).
* **Spiro's** cubic-curvature segments (G4).

Evaluation: the integrand is entire and smooth, so per-segment Gauss–Legendre or
Taylor series converge geometrically; there is no closed-form Fresnel analogue for
$n\ge 2$, which is fine — nobody needs one (see
[numerical-evaluation.md](numerical-evaluation.md)).

**Recommendation:** implement *one* `PolySpiral` type (curvature polynomial of
degree ≤ 7 in local arc length, plus start pose and length) with clothoid, arc and
line as fast-path specialisations. This covers OpenDRIVE spirals, IFC
clothoid/polynomial spirals, Bloss, Viennese bend, Spiro segments and cubic
spirals for path planning in one code path.

---

## 6. Hobby's splines (MetaPost)

Hobby, *Smooth, easy to compute interpolating splines* (Discrete Comput. Geom.
1, 1986): a cubic Bézier spline through points with approximate G2 ("mock
curvature") continuity, solved by a **linear, diagonally dominant tridiagonal
system** for the tangent angles, so it is $O(n)$ and robust. Each span is a cubic
whose control points are placed from the angles $\theta$ (departure, relative to
the chord) and $\varphi$ (arrival) via Hobby's "velocity" function

$$
\rho(\theta,\varphi) = \frac{2 + \sqrt2\,(\sin\theta - \tfrac1{16}\sin\varphi)(\sin\varphi-\tfrac1{16}\sin\theta)(\cos\theta-\cos\varphi)}
{1 + \tfrac12(\sqrt5-1)\cos\theta + \tfrac12(3-\sqrt5)\cos\varphi},
$$

with control points $P_0 + \tfrac{\rho}{3\tau}\,|P_1-P_0|\,R(\theta)\hat c$ etc.
($\tau$ = tension, $\hat c$ = unit chord). It is the de-facto "nice curve through
points" in MetaPost/TikZ. Relevance: a cheap, *polynomial-native* option for
procedural curves where exact engineering geometry is not needed (footpaths,
field boundaries, fences, shorelines at coarse LOD). It approximates a clothoid
spline's fairness at a fraction of the cost and emits Béziers directly.

---

## 7. κ-curves (Yan, Schiller, Wilensky, Carr, Schaefer 2017)

*κ-curves: interpolation at local maximum curvature* (ACM TOG 36(4), SIGGRAPH
2017). Piecewise quadratic Béziers that interpolate the control points **at the
points of local maximum curvature**, almost-everywhere curvature continuous (G2
except at inflection-related points), solved by a fast local/global iteration.
Shipped as Adobe Illustrator's "Curvature" tool. ϵκ-curves (Miura et al.) add
control over the magnitude of the curvature maximum.

Relevance: when a *user or a procedural rule* specifies "the bend apex is here"
(e.g. a river bend apex, a hairpin turn), κ-curves give exactly that semantics.
Output is quadratic Béziers — trivially NURBS. Not appropriate for engineering
alignments (curvature extrema at points, not constant-radius arcs).

---

## 8. Log-aesthetic curves (LAC)

Harada observed that aesthetic curves in car styling have **linear logarithmic
curvature graphs**: with radius $\rho = 1/\kappa$,

$$
\log\!\left(\rho\,\frac{ds}{d\rho}\right) = \alpha\,\log\rho + c .
$$

Miura (*A general equation of aesthetic curves and its self-affinity*,
CAD&A 3, 2006) integrated this to the Cesàro form; Yoshida & Saito
(*Interactive aesthetic curve segments*, Visual Computer 22, 2006) gave a G1
Hermite-style interactive segment (endpoints + tangents + $\alpha$). Normalised:

$$
\kappa(s) = \begin{cases}
(\Lambda\alpha s + 1)^{-1/\alpha}, & \alpha \ne 0\\
e^{-\Lambda s}, & \alpha = 0
\end{cases}
$$

Special cases: $\alpha=-1$ clothoid; $\alpha=0$ Nielsen's spiral;
$\alpha=1$ logarithmic spiral; $\alpha=2$ circle involute; $\alpha\to\infty$ circle.
Séquin named them "log-aesthetic"; Levien's thesis notes they may beat the Euler
spiral as a spline generating curve in some cases. Extended LACs and G2 Hermite
versions exist (Gobithaasan, Miura, Yoshida et al.).

Relevance: a single-parameter family that spans clothoids and log spirals —
attractive for *natural* features (shorelines, dune crests, coastal spits, shell-
like landforms) where the curvature varies monotonically but not linearly. Same
`curvature(s)` → integrate machinery as polynomial spirals; just a different
$\kappa(s)$ closure. Cheap to add once the generic Cesàro-curve integrator exists.

---

## 9. Pythagorean-hodograph (PH) curves (Farouki)

A polynomial curve $\mathbf r(t)$ is PH if $|\mathbf r'(t)|$ is a polynomial:

$$
x'(t)^2 + y'(t)^2 = \sigma(t)^2 .
$$

In the plane this holds iff $x' = u^2 - v^2$, $y' = 2uv$, $\sigma = u^2+v^2$
(times a common factor) — compactly $\mathbf r'(t) = \mathbf w(t)^2$ in complex
form with $\mathbf w = u + iv$. Spatial PH curves use quaternions
($\mathbf r' = \mathcal A\,\mathbf i\,\mathcal A^\*$). Consequences:

* **Exact polynomial arc length** $s(t) = \int\sigma\,dt$ → exact stationing,
  exact arc-length reparametrisation (by polynomial root finding).
* **Rational offsets:** unit normal $\mathbf n = (y',-x')/\sigma$ is rational, so
  $\mathbf r + d\,\mathbf n$ is an *exact* rational (NURBS-representable) curve.
  Lane edges of a PH centreline are exact NURBS.
* **Rational rotation-minimising frames** for some spatial PH families (RRMF
  curves) — exact sweep frames for pipes/tunnels.
* PH quintics solve the G1 Hermite problem (4 solutions; pick by absolute rotation
  number / bending energy); PH quintic/septic G2 constructions exist.
* Farouki, Pelosi & Sampoli (J. Comput. Appl. Math. 382, 2021) approximate
  **monotone clothoid segments by degree-7 PH curves**, matching endpoints,
  tangents, curvatures *and* arc length exactly (5 algebraic equations, PH quintic
  for the starting guess).

Reference: Farouki, *Pythagorean-Hodograph Curves: Algebra and Geometry
Inseparable* (Springer, 2008).

**Relevance:** PH curves are the best *polynomial* stand-in for a road
centreline when downstream consumers require NURBS: exact length (stations stay
consistent between design and tessellation) and exact rational offsets (lane edges
need no further approximation). Cost: degree 7 per clothoid piece, nonlinear
construction. Recommended as a later optimisation for export/interchange, not as the
core representation.

---

## 10. Comparison and recommendations

| Family | Native param | Arc length | Offset closed? | G-continuity typical | Construction | Use in UberTopo |
|---|---|---|---|---|---|---|
| Line/arc | $s$ | exact | yes (line/arc) | G1 | trivial | everywhere |
| Clothoid | $s$ | exact | no | G2 | G1 Hermite: 1D Newton | roads, rail, kerbs |
| Polynomial spiral | $s$ | exact | no | G2–G4 | Newton | IFC spirals, path planning, Spiro |
| Log-aesthetic | $s$ | exact | no | G2 | Newton | natural features |
| Sine-generated | $s$ | exact | no | C∞ | direct | rivers/meanders |
| Hobby | $t$ | numeric | no | ≈G2 | linear tridiagonal | coarse procedural paths |
| κ-curve | $t$ | numeric | no | G2 a.e. | local/global iter. | apex-driven shapes |
| PH | $t$ | exact polynomial | rational exact | G1–G2 | nonlinear | exact NURBS export |
| B-spline/NURBS | $t$ | numeric | no (approx) | user | linear | universal derived rep |

Recommendations:

1. Make the **Cesàro (curvature-of-arc-length) curve** a first-class kernel
   primitive with pluggable $\kappa(s)$: constant, linear (clothoid), polynomial,
   LAC, sine-generated, piecewise. One integrator, many closures.
2. Use **Bertolazzi–Frego** G1/G2 Hermite clothoid fitting; validate against
   their library.
3. Treat B-splines as **derived caches** carrying the error bound they were built
   with, never as the source of truth for curvature-defined features.
4. Borrow **kurbo's** trait design and moment-based cubic fitting.

---

## References

* Bertolazzi, E., Frego, M. *G1 fitting with clothoids.* Math. Methods Appl. Sci.
  38(5):881–897, 2015. Preprints: <https://arxiv.org/abs/1209.0910>,
  <https://arxiv.org/abs/1305.6644>
* Bertolazzi, E., Frego, M. *On the G2 Hermite interpolation problem with
  clothoids.* J. Comput. Appl. Math. 341:99–116, 2018.
  doi:10.1016/j.cam.2018.03.029 — <https://iris.unitn.it/handle/11572/210452>
* Bertolazzi, Bevilacqua, Frego. *Efficient intersection between splines of
  clothoids.* Math. Comput. Simul. 176:57–72, 2020.
  <https://ideas.repec.org/a/eee/matcom/v176y2020icp57-72.html>
* Clothoids library: <https://github.com/ebertolazzi/Clothoids>,
  docs <https://ebertolazzi.github.io/Clothoids/README.html>;
  G1fitting: <https://github.com/ebertolazzi/G1fitting>;
  pyclothoids: <https://pyclothoids.readthedocs.io/en/latest/basic.html>
* Meek, D.S., Walton, D.J. *Clothoid spline transition spirals.* Math. Comp. 59
  (1992) 117–133. <https://www.doi.org/10.1090/S0025-5718-1992-1134736-8>
* Heald, M.A. *Rational approximations for the Fresnel integrals.* Math. Comp. 44
  (1985) 459–461. <https://www.ams.org/journals/mcom/1985-44-170/S0025-5718-1985-0777277-6>
* Boersma, J. *Computation of Fresnel integrals.* Math. Comp. 14 (1960) 380.
  <https://www.ams.org/journals/mcom/1960-14-072/S0025-5718-1960-0121973-3/>
* Mielenz, K.D. *Computation of Fresnel integrals II.* J. Res. NIST 105(4), 2000.
  <https://nvlpubs.nist.gov/nistpubs/jres/105/4/j54mie2.pdf>
* Cephes `fresnl.c` (Moshier), SciPy copy:
  <https://sources.debian.org/src/python-scipy/1.1.0-7/scipy/special/cephes/fresnl.c>
* Netlib/JPL Fresnel (Cody/Snyder): <https://sunsite.icm.edu.pl/packages/netlib/math/docpdf/ch02-17.pdf>
* DLMF §7 (Error functions, Fresnel integrals): <https://dlmf.nist.gov/7>
* Levien, R. *The Euler spiral: a mathematical history.* UCB/EECS-2008-111.
  <https://www2.eecs.berkeley.edu/Pubs/TechRpts/2008/EECS-2008-111.html>
* Levien, R. *From Spiral to Spline.* PhD thesis, UCB/EECS-2009-162.
  <https://levien.com/phd/thesis.pdf>, <https://levien.com/phd/phd.html>
* Levien, Séquin. *Interpolating splines: which is the fairest of them all?*
  <https://levien.com/phd/LevienSequinCAD09_014.pdf>
* Spiro: <https://github.com/raphlinus/spiro>
* Levien, R. *How long is that Bézier?* (2018)
  <https://raphlinus.github.io/curves/2018/12/28/bezier-arclength.html>
* Levien, R. *Cleaner parallel curves with Euler spirals* (2021);
  *Parallel curves of cubic Béziers* (2022);
  *Simplifying Bézier paths* (2023):
  <https://raphlinus.github.io/curves/2023/04/18/bezpath-simplify.html>
* Levien, R., Uguray, A. *GPU-friendly Stroke Expansion.* HPG 2024.
  <https://arxiv.org/abs/2405.00127>
* kurbo: <https://github.com/linebender/kurbo>, offset module
  <https://docs.rs/kurbo/latest/src/kurbo/offset.rs.html>
* Linebender, *The mathematical beauty of hyperbezier curves*:
  <https://linebender.org/blog/hyperbezier/>
* Hobby, J.D. *Smooth, easy to compute interpolating splines.* Discrete Comput.
  Geom. 1 (1986) 123–140. <https://link.springer.com/content/pdf/10.1007/BF02187690.pdf>
* Yan, Schiller, Wilensky, Carr, Schaefer. *κ-curves: interpolation at local
  maximum curvature.* ACM TOG 36(4), 2017.
  <https://people.engr.tamu.edu/schaefer/research/kcurves.pdf>
* Miura, K.T. *A general equation of aesthetic curves and its self-affinity.*
  CAD&A 3, 2006. Yoshida, N., Saito, T. *Interactive aesthetic curve segments.*
  Visual Computer 22 (2006) 896–905. Overview:
  <https://sites.google.com/view/yoshida-lab-citnu/english/research-e/log-aesthetic-curves>
* Farouki, R.T. *Pythagorean-Hodograph Curves: Algebra and Geometry Inseparable.*
  Springer 2008. <https://www.springer.com/book/9783540733973>;
  intro notes: <https://faculty.engineering.ucdavis.edu/farouki/wp-content/uploads/sites/51/2021/07/Introduction-to-PH-curves.pdf>
* Farouki, Pelosi, Sampoli. *Approximation of monotone clothoid segments by degree
  7 Pythagorean-hodograph curves.* J. Comput. Appl. Math. 382, 2021.
  <https://portal.mardi4nfdi.de/wiki/Publication:2196070>
* Kelly, A., Nagy, B. *Reactive nonholonomic trajectory generation via parametric
  optimal control.* IJRR 22(7–8), 2003.
* Langbein, W.B., Leopold, L.B. *River meanders — theory of minimum variance.*
  USGS PP 422-H, 1966. <https://pubs.usgs.gov/pp/0422h/report.pdf>
