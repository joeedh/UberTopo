# Space Curves and Moving Frames

Part of the [curves research set](README.md). 3D curves for environment
modelling (road/rail 3D alignments, helices, cables, pipes, rivers on terrain)
and the frames used to sweep cross-sections along them: Frenet, rotation-minimising
(RMF, Wang et al. double reflection), and the gravity-referenced frames that road
and rail standards actually use.

> Source note: page fetches were blocked; sources located by web search, formulas
> standard differential geometry or from the cited papers.

---

## 1. Plane vs space curves in UberTopo

Most environmental linear features are **2.5D**: a plane curve in map coordinates
plus height functions. Genuinely 3D curves appear for:

* 3D alignments of roads/rail/canals (horizontal + vertical, §2),
* ramps/spiral car-park ramps/helical stairs (helices, §3),
* overhead cables and power lines (catenaries, §4) — important obstacles for
  drones,
* pipes, ducts, tunnels, conveyors, slides (free-form space curves, RMF sweeps),
* rivers/streams *on terrain* (plan curve draped on a DEM, §6),
* flight paths of drones themselves (test scenarios; not geometry of the
  environment but share the curve library).

Recommendation: a single `Curve3` abstraction, with **composite
"horizontal × profile" curves as a first-class type** rather than converting
alignments to generic 3D NURBS, because station semantics (horizontal arc length)
and gravity-referenced frames must survive.

---

## 2. Road/rail 3D alignments

$$
\mathbf r(s) = \big(x(s),\,y(s),\,z(s)\big),\quad s = \text{horizontal station}.
$$

$$
\mathbf r'(s) = (\cos\theta,\ \sin\theta,\ z'(s)),\qquad
|\mathbf r'| = \sqrt{1+z'^2},\qquad \sigma(s) = \int_0^s\sqrt{1+z'^2}\,du .
$$

3D curvature and torsion follow from the general formulas

$$
\kappa_{3D} = \frac{|\mathbf r'\times\mathbf r''|}{|\mathbf r'|^3},\qquad
\tau = \frac{(\mathbf r'\times\mathbf r'')\cdot\mathbf r'''}{|\mathbf r'\times\mathbf r''|^2},
$$

with $\mathbf r'' = (-\kappa_h\sin\theta,\ \kappa_h\cos\theta,\ z'')$. Neither is
used in road design (design is done on the 2D components separately), but they
matter for: rendering/shading, physics consumers, and checking that sweeps don't
self-intersect.

Vertical profiles are piecewise polynomial in $s$ (parabola/cubic) and only G1
at PVCs/PVTs (grade continuous, vertical curvature jumps). Horizontal geometry is
G2 (with spirals). So the 3D alignment is typically **G1 overall**; any frame
derived from $\mathbf r''$ (Frenet) will jump. Another reason not to use Frenet.

---

## 3. Helices and helical ramps

Circular helix of radius $a$, pitch $2\pi b$:

$$
\mathbf r(t) = (a\cos t,\ a\sin t,\ bt),\qquad
\kappa = \frac{a}{a^2+b^2},\quad \tau = \frac{b}{a^2+b^2}.
$$

In road terms a helical ramp is just a circular horizontal arc with a constant
grade profile — the composite alignment type represents it exactly, as do spiral
ramps with clothoid entry (clothoid plan + constant grade). Helix in NURBS: not
exactly representable (rational polynomials can't give $t$ and $\cos t$ in the same
parameter); approximate like any other transcendental curve.

---

## 4. Catenaries (cables, power lines)

A uniform cable under gravity hangs as a catenary in a vertical plane:

$$
z(x) = z_0 + a\left(\cosh\frac{x-x_0}{a} - 1\right),\qquad a = \frac{H}{w}
$$

($H$ horizontal tension, $w$ weight per length). Arc length
$s = a\sinh\frac{x-x_0}{a}$ (closed form), so the catenary is also a Cesàro curve:
$\kappa(s) = \frac{a}{a^2+s^2}$. Given two support points and sag (or cable
length), solve for $a$ by 1D Newton. For short spans with small sag the parabola
$z \approx w x^2/(2H)$ is the engineering approximation. Spans with insulators,
multiple conductors, and wind sway are procedural parameters.

For drone-defence environments, **thin wires are among the most important
obstacles** and are invisible to many sensors; the curve library should make
catenaries cheap to generate in bulk (a power line corridor = thousands of spans)
and expose them as curves (not meshes) for ray/sensor queries (distance to curve,
ray–tube intersection with radius = conductor radius).

---

## 5. Moving frames

A sweep needs, at each station, an orthonormal frame
$(\mathbf t,\mathbf u,\mathbf v)$ to place the cross-section.

### 5.1 Frenet frame

$\mathbf T = \mathbf r'/|\mathbf r'|$, $\mathbf N = \mathbf T'/|\mathbf T'|$,
$\mathbf B = \mathbf T\times\mathbf N$. Undefined on straights and at inflections,
flips at inflections, twists with torsion. **Unsuitable** for sweeping in general.

### 5.2 Rotation-minimising frame (RMF, Bishop frame)

Normal vectors evolve with no rotation about the tangent:

$$
\mathbf u' = -(\mathbf u\cdot\mathbf t')\,\mathbf t\quad(\text{arc-length } s),\qquad
\mathbf v = \mathbf t\times\mathbf u .
$$

Equivalently the angular velocity of the frame has no tangential component. RMFs
are what you want for pipes, cables-as-tubes, tunnels (when not gravity-referenced),
generalised cylinders, slides, tubes in procedural vegetation (branches/roots!).

**Double reflection method** (Wang, Jüttler, Zheng, Liu, *Computation of rotation
minimizing frames*, ACM TOG 27(1), 2008). Given samples $\mathbf x_i$ with unit
tangents $\mathbf t_i$ and a frame vector $\mathbf r_i$:

$$
\begin{aligned}
\mathbf v_1 &= \mathbf x_{i+1}-\mathbf x_i,\quad c_1 = \mathbf v_1\cdot\mathbf v_1,\\
\mathbf r_i^L &= \mathbf r_i - \tfrac{2}{c_1}(\mathbf v_1\cdot\mathbf r_i)\,\mathbf v_1,\qquad
\mathbf t_i^L = \mathbf t_i - \tfrac{2}{c_1}(\mathbf v_1\cdot\mathbf t_i)\,\mathbf v_1,\\
\mathbf v_2 &= \mathbf t_{i+1} - \mathbf t_i^L,\quad c_2 = \mathbf v_2\cdot\mathbf v_2,\\
\mathbf r_{i+1} &= \mathbf r_i^L - \tfrac{2}{c_2}(\mathbf v_2\cdot\mathbf r_i^L)\,\mathbf v_2,\qquad
\mathbf s_{i+1} = \mathbf t_{i+1}\times\mathbf r_{i+1}.
\end{aligned}
$$

(If $c_2\approx0$, $\mathbf r_{i+1} = \mathbf r_i^L$.) Fourth-order global accuracy,
same per-step cost as the second-order projection (Klok) and rotation
(Bloomenthal) methods, simpler and faster than RK4 on the RMF ODE at equal
accuracy. **Recommended default.**

**Closed curves** (ring roads as tubes, closed pipes, loops): the RMF does not
close up — the holonomy angle $\alpha$ between start and end frame must be
distributed: rotate the frame at station $s$ by $-\alpha\,\sigma(s)/\sigma_{tot}$.

**Exact RMFs**: for PH space curves, certain families (RRMF — rational RMF curves,
Farouki, Han, Jüttler et al.) have rational RMFs, so sweeps along them are exactly
rational surfaces. Niche, but relevant if exact NURBS sweeps are ever needed.

### 5.3 Gravity-referenced (road/rail) frames — what alignments actually use

Road and rail cross-sections are **not** placed with an RMF or Frenet frame. The
standards (OpenDRIVE, IFC 4.3, LandXML corridor practice) use:

* $\mathbf t_h = (\cos\theta,\sin\theta,0)$ — horizontal tangent of the plan curve,
* $\mathbf n_h = (-\sin\theta,\cos\theta,0)$ — horizontal left normal,
* $\mathbf e_z$ — world up.

The cross-section plane is the **vertical plane** spanned by $\mathbf n_h$ and
$\mathbf e_z$ (perpendicular to the horizontal tangent, *not* to the 3D tangent),
then rotated by superelevation/cant angle $\phi(s)$ about $\mathbf t_h$ (OpenDRIVE:
around the reference line):

$$
\mathbf u(s) = \cos\phi\,\mathbf n_h + \sin\phi\,\mathbf e_z,\qquad
\mathbf v(s) = -\sin\phi\,\mathbf n_h + \cos\phi\,\mathbf e_z .
$$

A cross-section point $(t, h)$ maps to

$$
\mathbf P(s,t,h) = \mathbf r(s) + t\,\mathbf u(s) + h\,\mathbf v(s).
$$

Consequences:

* Lateral offsets are measured horizontally (rotated by superelevation), so a lane
  of width $w$ has *plan* width $w\cos\phi$ (OpenDRIVE notes superelevation does
  not change lane width but changes projected width).
* Vertical objects (poles, signs, buildings) stay vertical — important; with an RMF
  on a 6% grade every pole would lean 3.4°.
* The frame is defined pointwise from $\theta(s)$, $z'(s)$ is not involved, and it
  never needs integration — no holonomy, no drift.
* Bridges and tunnels follow the same frame (tunnel cross-sections are usually
  vertical sections, sometimes perpendicular-to-grade for TBM tunnels — a
  per-structure option).

Rail cant rotates about the **low rail** (or track centreline, by convention) by
$\phi = \arcsin(c/G_{eff})$, $G_{eff}$ = rail head centre distance (~1500 mm for
standard gauge).

**Recommendation:** the frame is a property of the *sweep*, not of the curve:
`FrameMode = { Gravity(superelevation fn, pivot), RMF(initial normal), Frenet,
Fixed(up vector), Custom(fn) }`.

### 5.4 Minimal-torsion / up-vector frames for free curves

For procedural pipes running along buildings, guardrails following terrain, or
fences, a hybrid "keep $\mathbf v$ as close to world-up as possible" frame
(project $\mathbf e_z$ onto the normal plane; fall back to RMF when the tangent is
near vertical) is often what artists expect. This is the `Fixed(up)` mode; it is
singular when $\mathbf t\parallel\mathbf e_z$ and must transition to RMF there.

---

## 6. Rivers, streams and curves on terrain

### 6.1 Plan geometry: sine-generated and Kinoshita curves

Langbein & Leopold (USGS PP 422-H, 1966) argued meanders minimise the variance of
direction change; the resulting **sine-generated curve** has direction angle

$$
\theta(s) = \omega\,\sin\!\left(\frac{2\pi s}{M}\right)
$$

($M$ meander arc length, $\omega$ max angle to the mean down-valley axis; ratio of
meander length to mean bend radius ≈ 4.7). Kinoshita (1961) added skewness and
flatness:

$$
\theta(s) = \theta_0\sin(ks) + \theta_0^3\big(J_s\cos 3ks - J_f\sin 3ks\big).
$$

Both are **Cesàro curves with closed-form $\theta(s)$** — they drop straight into the
same integrator as clothoids. Procedural river generation: centreline from
sine-generated/Kinoshita curves (parameters from stream order/valley slope),
perturbed with noise in $\theta$ (not in position — keeps smoothness and arc
length), then draped and carved into terrain. Width, depth and bank profile as
functions of $s$ (asymmetric at bends: deep outer bank, point bar inner bank,
driven by curvature).

### 6.2 Draping

A 2D curve on a heightfield $H(x,y)$: $z(s) = H(x(s),y(s))$. The draped curve
inherits the heightfield's continuity (C0 for TIN, C1/C2 for bicubic DEM
interpolation). For roads, the opposite happens — the road *profile* is designed
and the terrain is modified to match (cut/fill; see
[surfaces-from-curves.md](surfaces-from-curves.md)). For rivers, the water surface
profile is monotone non-increasing downstream; draping must enforce that (take a
running minimum, then carve the bed into the terrain).

### 6.3 Geodesics and shortest paths

Footpaths, animal trails, and some procedural road routing use least-cost paths
over terrain (graph search on a grid with slope costs), then **fit** a G2 curve
(clothoid spline via G1 Hermite at chosen nodes) to the resulting polyline. This
"plan then fit" pattern is the main procedural entry point for curves.

---

## 7. Sweeping-specific robustness

* **Local self-intersection** of a sweep occurs where the cross-section extends
  beyond the radius of curvature in the direction of the curvature normal: for a
  road at offset $t$, when $t\kappa_h \ge 1$ (the offset cusp of
  [spline-approximation-and-offsets.md](spline-approximation-and-offsets.md)).
  For RMF tube sweeps of radius $\rho$: $\rho\kappa_{3D}\ge1$.
* **Global self-intersection** (a road crossing itself in plan — interchanges,
  spiral ramps) is legitimate: the plan curves cross but $z$ separates them. The
  B-rep must not try to merge them; intersection tests must be 3D.
* Frame sampling for tessellation: sample frames at the same stations as the
  curve tessellation, plus at all superelevation breakpoints.

---

## References

* Wang, W., Jüttler, B., Zheng, D., Liu, Y. *Computation of rotation minimizing
  frames.* ACM TOG 27(1), Article 2, 2008.
  <https://i.cs.hku.hk/GraphicsGroup/publications/pdf/Computation%20of%20rotation%20minimizing%20frames.pdf>;
  Microsoft Research page <https://www.microsoft.com/en-us/research/?p=344486>
* Bishop, R.L. *There is more than one way to frame a curve.* Amer. Math. Monthly
  82(3), 1975.
* Klok, F. *Two moving coordinate frames for sweeping along a 3D trajectory.* CAGD
  3(3), 1986.
* Bloomenthal, J. *Calculation of reference frames along a space curve.* Graphics
  Gems I, 1990.
* Farouki, R.T. *Pythagorean-Hodograph Curves* (Springer 2008), chapters on
  rational rotation-minimising frames; Farouki & Sakkalis, *Rational
  rotation-minimizing frames on polynomial space curves of arbitrary degree*,
  J. Symb. Comput. 45, 2010.
* ASAM OpenDRIVE 1.8 — reference line coordinate system, superelevation:
  <https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/1.8.0/specification/08_coordinate_systems/08_03_reference_line_coordinate_system.html>,
  <https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/1.8.0/specification/10_roads/10_05_elevation.html>
* IFC 4.3 segmented reference curve (cant applied to gradient curve):
  <https://ifc43-docs.standards.buildingsmart.org/IFC/RELEASE/IFC4x3/HTML/annex_e/alignment-geometries-and-linear-positioning/segmented-reference-curve.html>
* Langbein, W.B., Leopold, L.B. *River meanders — theory of minimum variance.* USGS
  Professional Paper 422-H, 1966. <https://pubs.usgs.gov/pp/0422h/report.pdf>
* Kinoshita, R. *Investigation of channel deformation in Ishikari River.* Report of
  Bureau of Resources, Japan, 1961 (as cited in meander modelling literature, e.g.
  Abad & García, *Flow and bed dynamics in a Kinoshita meandering channel*, Water
  Resour. Res. 2009).
* Irvine, H.M. *Cable Structures.* MIT Press 1981 (catenary/elastic cable theory).
