# Road and Rail Alignment Geometry and Interchange Standards

Part of the [curves research set](README.md). What geometry road/rail designers
actually use, how the main interchange formats (LandXML, ASAM OpenDRIVE, IFC 4.3
`IfcAlignment`) encode it, and what a road-network model inside UberTopo must
store.

> Source note: web search worked but page fetches were blocked by the sandbox
> network policy (asam.net and buildingsmart.org could not be opened). Schema
> details below come from search-result excerpts of the official spec pages plus
> the author's knowledge; specific attribute names should be checked against the
> spec version we target before writing importers.

---

## 1. The 2.5D decomposition

Practically all road and rail standards describe a 3D alignment as **three
decoupled 1D/2D functions of one horizontal station** $s$ (horizontal arc length
along the plan curve):

1. **Horizontal alignment** — a plane curve $\mathbf h(s) = (x(s), y(s))$ in a
   projected map coordinate system, built from tangents, circular arcs and
   transition spirals, parameterised by its own arc length.
2. **Vertical alignment (profile)** — elevation $z(s)$ as a function of
   *horizontal* station, built from constant grades and vertical curves
   (parabolas, sometimes circular arcs).
3. **Cross-section functions** — superelevation (road) / cant (rail), lane
   widths, lane offsets, lateral shape, all as functions of $s$.

So the 3D centreline is $\mathbf r(s) = (x(s), y(s), z(s))$ where **$s$ is not the
3D arc length**: $d\sigma = \sqrt{1 + z'(s)^2}\,ds$. Every library that confuses the
two will produce station drift of ~0.1–0.5% on graded roads. The kernel must
carry both, and "station" must always mean the horizontal one unless stated.

---

## 2. Horizontal alignment elements

### 2.1 Tangents and circular arcs

Lines and arcs are exact in every representation (NURBS included). Arc parameters
in practice: radius $R$, direction (left/right), length or deflection
$\Delta = L/R$, sometimes chord, PI (point of intersection of tangents),
tangent length $T = R\tan(\Delta/2)$, external $E = R(\sec(\Delta/2)-1)$.

Minimum radius from the point-mass formula (road):

$$
e + f = \frac{V^2}{g R}\quad\Longleftrightarrow\quad R_{\min} = \frac{V^2}{127\,(e_{\max}+f_{\max})}
$$

with $V$ in km/h, $R$ in m, $e$ superelevation rate, $f$ side-friction factor
(AASHTO Green Book). For rail the analogue is cant: equilibrium cant
$c_{eq} \approx 11.8\,V^2/R$ (mm, km/h, m, standard gauge), limited by cant and
cant-deficiency limits.

### 2.2 Transition curves (spirals / easements)

Purpose: ramp curvature (and simultaneously superelevation / cant) from 0 to
$1/R$. Let $t = l/L \in[0,1]$ along a transition of length $L$ into radius $R$,
$\kappa_1 = 1/R$. Shapes, as curvature laws $\kappa(t)$:

| Name | Curvature law $\kappa(t)/\kappa_1$ | Continuity at ends | Where used |
|---|---|---|---|
| Clothoid (Euler) | $t$ | G2 (curvature continuous, κ' jumps) | roads worldwide, most rail |
| Cubic parabola | defined in $x$: $y = x^3/(6RL)$ | approximates clothoid for small deflection | older rail/UK, LandXML `cubic`/`cubicParabola` |
| Cubic spiral | clothoid with series truncated in $\theta$ | approximation | legacy |
| Bloss | $3t^2 - 2t^3$ | G3 (κ' = 0 at both ends) | German rail (DB) |
| Biquadratic / Helmert / Schramm | $2t^2$ for $t\le\tfrac12$; $1 - 2(1-t)^2$ for $t>\tfrac12$ | G3 at ends, κ'' jumps mid | rail; IFC `HELMERTCURVE` (was BIQUADRATICPARABOLA) |
| Cosine (half-wave) | $\tfrac12(1-\cos\pi t)$ | G3 | rail (Japan) |
| Sinusoidal (Klein) | $t - \frac{1}{2\pi}\sin 2\pi t$ | G4 (κ', κ'' = 0 at ends) | high-speed rail |
| Viennese bend (Wiener Bogen®) | degree-7 polynomial, e.g. $35t^4-84t^5+70t^6-20t^7$, plus terms involving the height of the vehicle's centre of gravity above the track (couples cant rotation into plan curvature) | G4 | Austrian rail (patented) |

Integrate $\theta(t)$ and $\mathbf r(t)$ numerically — all of these are
**polynomial spirals** except the cosine/sine ones (still entire functions; same
integrator works). The cubic parabola is *defined in Cartesian form* (offset from
the initial tangent as a function of distance $x$ along it, not $s$), so its
"length" attribute is ambiguous between tools (some store $x$-length, others arc
length) — a classic interchange trap.

Classic clothoid series (start at $\kappa=0$, local frame on the tangent):

$$
x(l) = l - \frac{l^5}{40 A^4} + \frac{l^9}{3456 A^8} - \dots,\qquad
y(l) = \frac{l^3}{6A^2} - \frac{l^7}{336 A^6} + \frac{l^{11}}{42240A^{10}} - \dots
$$

with $A^2 = RL$. Shift of the circle ("p" or "throw") $p \approx L^2/(24R)$ — the
inward offset the circular arc must undergo to make room for the spirals.

### 2.3 Compound and reverse curves, and the PI-based design model

Designers do not place segments directly; they place **PI points** (vertices of
the tangent polygon) with, per PI, a radius $R$ and entry/exit spiral lengths
$L_1, L_2$ (possibly different — unsymmetric). The software derives TS, SC, CS, ST
stations. Variants: spiral–spiral (no arc), egg-shaped (spiral between two arcs of
different radii — clothoid with nonzero $\kappa_0$), reverse curves (S-curves with
a clothoid through $\kappa=0$), compound curves.

**For procedural generation this PI-based model is the right *editing*
representation**: a road generator emits PIs (from a path planner over terrain)
plus design speed; a solver turns them into segments. Constraints such as "spirals
must not overlap" (sum of tangent lengths ≤ PI distance) are checked there.

---

## 3. Vertical alignment

Elements: constant grades $g$ (rise/run, percent), connected at PVIs (points of
vertical intersection) by **parabolic vertical curves** (US/most countries) or
circular vertical curves (some European practice specifies radius $H$; a parabola
$z = x^2/(2H)$ is used as the computational approximation). Equal-tangent parabola
of horizontal length $L$ from PVC:

$$
z(x) = z_{PVC} + g_1 x + \frac{g_2-g_1}{2L}x^2,\qquad x\in[0,L],
$$

**$x$ is horizontal station**, not 3D length. Design control: $K = L/|A|$,
$A = g_2 - g_1$ in percent; crest $K$ from stopping sight distance and eye/object
heights, sag $K$ from headlight sight distance / comfort. Unsymmetrical vertical
curves (two parabolas meeting at a point under the PVI) exist (LandXML
`UnsymParaCurve`). OpenDRIVE generalises to a cubic in $ds$ per record.

Rail profiles additionally use circular vertical curves explicitly (vertical
radius in metres, e.g. ≥ 2000 m), sometimes clothoid-like vertical transitions in
high-speed design.

---

## 4. Superelevation and cant

Superelevation $e(s)$: cross-slope (rotation of the cross-section about a pivot
line). Components of a design:

* $e_{max}$ (4–12%, climate dependent), normal crown (e.g. −2% both sides).
* **Tangent runout**: adverse crown removed; **superelevation runoff**: from flat
  to full $e$, usually coinciding with the spiral (linear ramp in $s$, so cross
  slope tracks curvature).
* **Pivot**: centreline, inside edge, outside edge, or a specified lane edge.
  Changing the pivot changes edge elevations, i.e. the vertical position of lane
  edges is *not* simply profile + offset·slope unless the pivot is the reference
  line.
* Relative gradient limits (max difference between edge and centreline grade).

Rail **cant** $c(s)$ (mm) = height difference between rails; rotation about the low
rail or about the track centre. Cant transitions typically linear along the
clothoid (or matching Bloss etc.). IFC 4.3 has a dedicated `IfcAlignmentCant`
layer with its own segment types (constant, linear transition, Bloss, cosine,
sine, Helmert, Viennese).

Geometric meaning: the road surface at station $s$ and lateral offset $t$ is the
horizontal normal line rotated about the horizontal tangent by angle
$\phi(s) = \arctan e(s)$. Note that this is a **gravity-referenced frame**, not a
Frenet or rotation-minimising frame (see
[space-curves-and-frames.md](space-curves-and-frames.md)). The cross-section plane
is vertical (contains $\mathbf e_z$) and perpendicular to the *horizontal* tangent.

---

## 5. Lanes, offsets, widths

A road's lanes are defined relative to a reference line (centreline or a lane
edge). Models:

* **Width-based** (OpenDRIVE `<width>`): each lane's width is a cubic polynomial
  in $ds$ from the lane section start; lane boundaries are cumulative sums of
  widths from the centre lane.
* **Border-based** (OpenDRIVE `<border>`): the lane's outer border offset is given
  directly as a cubic.
* **Lane offset** (OpenDRIVE `<laneOffset>`): shifts the centre lane relative to the
  reference line, cubic in $ds$.
* **Template/assembly-based** (Civil 3D, OpenRoads): a cross-section template with
  parametric points/links, with targets (e.g. widen to follow an offset alignment)
  — essentially a procedural program evaluated per station.

All lane boundaries are therefore **variable-distance offset curves**
$\mathbf h(s) + w(s)\,\mathbf n(s)$ of the horizontal reference line. They are not
clothoids, arcs or polynomials in general; they must be evaluated through the
reference curve (see
[spline-approximation-and-offsets.md](spline-approximation-and-offsets.md) §4).

---

## 6. Interchange formats

### 6.1 LandXML 1.2

XML schema (2008) widely supported by civil tools (Civil 3D, OpenRoads, Trimble
Business Center, 12d). Relevant structures:

* `<Alignments>/<Alignment name length staStart>` containing:
  * `<CoordGeom>` — ordered list of `<Line>`, `<Curve>` (arc: rot, radius, length,
    start/center/end/PI points), `<Spiral>` (`length`, `radiusStart`, `radiusEnd`
    (INF allowed), `rot`, `spiType`, start/PI/end points);
    `spiType` in 1.2: `clothoid`, `cubic`, `biquadratic`, `bloss`, `cosine`,
    `cubicParabola`, `sinusoid`, `revBiquadratic`, `revBloss`, `revCosine`,
    `revSinusoid`, `sineHalfWave`, `japaneseCubic`, `radioid`, `weinerBogen`
    (spellings vary; Trimble documents only clothoid/cubic/biquadratic as
    reliably imported, so treat the rest as "best-effort").
  * `<StaEquation>` — station equations (stationing breaks/back-ahead), a feature
    any road model must support: stations are *labels* that can jump.
  * `<Profile>/<ProfAlign>` — `<PVI>`, `<ParaCurve>`, `<UnsymParaCurve>`,
    `<CircCurve>`; `<ProfSurf>` for existing ground profiles.
  * `<CrossSects>`, `<Superelevation>`, `<Cant>` (rail).
* Each element redundantly stores start/end coordinates **and** parameters, so
  imported data is frequently inconsistent at the 1e-3–1e-2 m level. Importers
  need a policy (trust parameters, trust points, or least-squares reconcile) and
  must report gaps.
* Japan's J-LandXML profile extends it for national use.

### 6.2 ASAM OpenDRIVE (1.6–1.8)

The de-facto format for driving simulation (CARLA, esmini, VTD, RoadRunner).
Structure per `<road>`:

* **Reference line** `<planView>` of `<geometry s x y hdg length>` records, each
  containing one of:
  * `<line/>`
  * `<arc curvature/>`
  * `<spiral curvStart curvEnd/>` (clothoid, curvature linear in $s$)
  * `<poly3 a b c d/>` — **deprecated** (local $v = a+bu+cu^2+du^3$)
  * `<paramPoly3 aU bU cU dU aV bV cV dV pRange/>` — parametric cubic in local
    $(u,v)$ frame of the record start; `pRange="arcLength"` ($p\in[0,L]$) or
    `"normalized"` ($p\in[0,1]$). **Only the endpoints of $p$ coincide with arc
    length**, so $p$ is not arc length in between — a library must reparameterise.
* Each geometry record carries its own start pose $(s,x,y,hdg)$. Continuity
  between records is *not* enforced by the format — small gaps/kinks are common.
* `<elevationProfile>`: `<elevation s a b c d>` cubic in $ds$.
* `<lateralProfile>`: `<superelevation s a b c d>` (roll angle in rad around the
  reference line, cubic in $ds$) and `<shape s t a b c d>` (lateral height profile,
  cubic in $dt$ at given stations, interpolated linearly along $s$).
* `<lanes>`: `<laneOffset>` (cubic), `<laneSection s>` with left/center/right lanes,
  each with `<width sOffset a b c d>` or `<border>`, `<height>` (inner/outer
  offsets for kerbs/sidewalks), road marks, materials, speed. Lanes with
  `level="true"` are excluded from superelevation (e.g. sidewalks kept flat).
* `<objects>`, `<signals>`: placed in $(s,t)$ coordinates with `zOffset`, `hdg`,
  `pitch`, `roll`; repeated objects along $s$ (`<repeat>`), outlines in local
  coordinates. Object placement in the road's **(s, t, h) curvilinear frame** is
  the main pattern UberTopo should copy for placing infrastructure (poles,
  barriers, signs, trees along roads).
* `<junction>`: connecting roads with lane links. Junction *surfaces* are not
  defined — only the connecting roads' lanes; the area between is left to the
  renderer. (A significant gap: real junction pavement must be synthesised.)
* Coordinate system: inertial Cartesian, georeferenced via `<geoReference>` (PROJ
  string) and `<offset>`.

Implementations: libOpenDRIVE (C++, Apache-2.0, OpenDRIVE 1.4-oriented, generates
road meshes at a given tolerance; used by GDAL's XODR driver), esmini RoadManager,
CARLA's parser, the `opendrive` Rust crate.

### 6.3 IFC 4.3 `IfcAlignment` (ISO 16739-1:2024)

IFC 4.3 separates **semantic/business layout** from **geometry**:

* Semantic: `IfcAlignment` nests `IfcAlignmentHorizontal`, `IfcAlignmentVertical`,
  `IfcAlignmentCant`, each nesting ordered `IfcAlignmentSegment`s whose
  `DesignParameters` are `IfcAlignmentHorizontalSegment` (start point, start
  direction, start/end radius of curvature, length, `PredefinedType`),
  `IfcAlignmentVerticalSegment` (start distance along, horizontal length, start
  height, start/end gradient, radius of curvature, type) and
  `IfcAlignmentCantSegment`.
* Horizontal `PredefinedType` (`IfcAlignmentHorizontalSegmentTypeEnum`): `LINE`,
  `CIRCULARARC`, `CLOTHOID`, `CUBIC` (cubic parabola), `HELMERTCURVE` (renamed from
  BIQUADRATICPARABOLA in RC4), `BLOSSCURVE`, `COSINECURVE`, `SINECURVE`,
  `VIENNESEBEND` (needs `GravityCenterLineHeight`).
* Geometry: `IfcCompositeCurve` (horizontal) of **`IfcCurveSegment`**s, each a
  trimmed, placed piece of a *parent curve*: `Placement`, `SegmentStart`,
  `SegmentLength` (as length measures/parameters), `ParentCurve` (`IfcLine`,
  `IfcCircle`, `IfcClothoid`, `IfcPolynomialCurve`, `IfcSecondOrderPolynomialSpiral`,
  `IfcThirdOrderPolynomialSpiral`, `IfcSeventhOrderPolynomialSpiral`,
  `IfcSineSpiral`, `IfcCosineSpiral`). Then `IfcGradientCurve` = horizontal base
  curve + vertical segments; `IfcSegmentedReferenceCurve` = gradient curve + cant.
* `IfcSpiral` subtypes express curvature via length-valued coefficients so units
  stay consistent, of the form

  $$
  \kappa(s) = \sum_{i} \frac{A_i}{|A_i|^{\,i+2}}\, s^{i}\quad(\text{e.g. } \kappa = \tfrac{A_1}{|A_1|^3}\,s \text{ for the clothoid}),
  $$

  each term optional (absent ⇒ zero). *(Verify exact conventions — sign and the
  constant term — against `IfcSecondOrderPolynomialSpiral` docs.)*
* **Linear referencing / placement:** `IfcLinearPlacement` with
  `IfcPointByDistanceExpression` (`DistanceAlong`, `OffsetLateral`,
  `OffsetVertical`, `OffsetLongitudinal`, `BasisCurve`), `IfcReferent` for
  stationing (incl. station equations via referents).
* **Sweeps along alignments:** `IfcSectionedSolidHorizontal`,
  `IfcSectionedSurface`, `IfcDirectrixCurveSweptAreaSolid` — cross-section
  profiles positioned at distances along a directrix, interpolated between.
  Exactly the "sweep of variable cross-sections along an alignment" UberTopo
  needs (see [surfaces-from-curves.md](surfaces-from-curves.md)).
* IFC also requires both the semantic segment and the geometric segment to agree;
  buildingSMART's alignment validation service checks continuity/consistency
  tolerances — evidence that real files disagree.

### 6.4 Others worth knowing

* **CityGML 3.0 / CityJSON Transportation module** — semantic road/lane areas,
  mostly as surfaces, LOD-based (LOD0 centreline networks to LOD3 detailed). Useful
  as an *output* target for LOD terminology.
* **OpenStreetMap** — polylines with tags (`lanes`, `width`, `layer`, `bridge`).
  The most likely *input* for procedurally generated real-world environments; has
  no curvature model. Converting OSM polylines into G2 alignments (fit clothoid
  splines to noisy polylines, Hermite-fit at detected curve apexes) is a needed
  procedural node.
* **railML 3** — rail infrastructure with horizontal/vertical/cant layers similar
  to IFC.
* **RoadXML**, **OpenCRG** (road surface elevation grids over $(s,t)$ — fine
  pavement detail for vehicle dynamics; irrelevant for drones, except maybe acoustic
  roughness).

---

## 7. What UberTopo's road-network model must store

Minimum faithful model (superset of OpenDRIVE + IFC needs):

1. **Alignment** (per road/rail/canal centreline or reference line):
   * Horizontal: ordered segments, each `{start pose (x,y,θ), length, curvature
     law (line | arc κ | clothoid κ0,κ1 | polyspiral coeffs | named transition
     type + params | paramPoly3 (u,v) coeffs + pRange)}`; global start station.
   * Station equations / referents (station labels ≠ arc length).
   * Design intent layer (optional but recommended for procedural editing): PI
     list with radius, spiral lengths, design speed; the segment list is derived.
   * Vertical: PVIs + vertical curve lengths/types, or piecewise cubic
     $z(s)$ records.
   * Cant/superelevation: piecewise functions of $s$ with pivot definition.
2. **Lane/cross-section model**: lane sections (station intervals), per lane width
   or border functions, lane offset, lane type/semantics, heights (kerbs), level
   flags, lateral shape; or a general cross-section *template* evaluated per
   station (procedural).
3. **Placed objects** in $(s, t, h)$ + local orientation, including repeats.
4. **Topology**: road graph with predecessor/successor/junction links, lane-level
   connectivity, junction areas (to be surfaced procedurally).
5. **Georeferencing**: CRS (projected; record EPSG/PROJ), local origin offset; all
   geometry evaluated in local metric coordinates (see robustness in
   [curve-library-design.md](curve-library-design.md)).
6. **Provenance & tolerances**: which representation is authoritative (parameters
   vs. stored points), import gaps, fit errors.

Derived (cached, regenerable) data: B-spline approximations of centrelines and lane
edges with error bounds; tessellated corridor surfaces; B-rep faces/edges with
curve attributes; terrain modifications (cut/fill).

**Design recommendation:** the *alignment* is a composite of exact
curvature-law segments (the authoritative data, exactly representing OpenDRIVE
`spiral`/`arc`/`line` and IFC spirals); `paramPoly3` is kept as its own exact
polynomial type; everything else (lane edges, kerbs, 3D corridor surfaces) is
evaluated *through* the alignment in $(s,t,h)$ coordinates and only approximated
by polynomials at the last moment, with stored error bounds.

---

## References

* ASAM OpenDRIVE 1.8.0 specification (geometry, lanes, elevation chapters):
  <https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/1.8.0/specification/09_geometries/09_06_param_poly3.html>,
  <https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/1.8.0/specification/09_geometries/09_07_poly3.html>,
  <https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/1.8.0/specification/11_lanes/11_04_lane_offset.html>,
  <https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/1.8.0/specification/10_roads/10_05_elevation.html>,
  <https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/1.8.0/specification/08_coordinate_systems/08_03_reference_line_coordinate_system.html>
* IFC 4.3 documentation (buildingSMART):
  <https://ifc43-docs.standards.buildingsmart.org/IFC/RELEASE/IFC4x3/HTML/lexical/IfcAlignmentHorizontalSegmentTypeEnum.htm>,
  <https://ifc43-docs.standards.buildingsmart.org/IFC/RELEASE/IFC4x3/HTML/lexical/IfcAlignmentHorizontalSegment.htm>,
  <https://ifc43-docs.standards.buildingsmart.org/IFC/RELEASE/IFC4x3/HTML/lexical/IfcSpiral.htm>,
  <https://ifc43-docs.standards.buildingsmart.org/IFC/RELEASE/IFC4x3/HTML/lexical/IfcSecondOrderPolynomialSpiral.htm>,
  <https://ifc43-docs.standards.buildingsmart.org/IFC/RELEASE/IFC4x3/HTML/lexical/IfcAlignmentCantSegment.htm>,
  <https://ifc43-docs.standards.buildingsmart.org/IFC/RELEASE/IFC4x3/HTML/annex_e/alignment-geometries-and-linear-positioning/segmented-reference-curve.html>
* OSArch wiki, IFC alignment: <https://wiki.osarch.org/index.php/IFC_-_Industry_Foundation_Classes/IFC_alignment>
* LandXML: <http://www.landxml.org/> ; Trimble LandXML import notes
  <https://help.fieldsystems.trimble.com/tbc/1009.htm>; J-LandXML
  <https://www.mlit.go.jp/tec/content/J-LandXML_schema_v18.pdf>
* iTwin.js integrated spiral types (good concise definitions of clothoid, bloss,
  biquadratic, cosine, sine "snap functions"):
  <https://www.itwinjs.org/reference/core-geometry/curve/integratedspiraltypename>
* Autodesk Civil 3D API spiral types:
  <https://help.autodesk.com/cloudhelp/2022/ENU/Civil3D-API/files/html/5ccd6fb4-05ee-c275-6247-0a5381b1656e.htm>
* libOpenDRIVE: <https://github.com/pageldev/libOpenDRIVE>; GDAL XODR driver
  <https://gdal.org/drivers/vector/xodr.html>
* AASHTO, *A Policy on Geometric Design of Highways and Streets* ("Green Book"),
  7th ed. 2018 (horizontal/vertical curve design, superelevation, K values).
* Railway transition curves (Bloss, Wiener Bogen, higher-degree polynomials):
  <https://www.mdpi.com/2076-3417/15/22/12066>,
  <https://www.problemykolejnictwa.pl/images/PDF/185_6E.pdf>;
  railML transition modelling: <https://railml.org/forum/pdf.php?msg=1234>
* Meek & Walton, *Clothoid spline transition spirals*, Math. Comp. 1992:
  <https://www.doi.org/10.1090/S0025-5718-1992-1134736-8>
* CityGML 3.0 Transportation module: <https://docs.ogc.org/is/20-010/20-010.html>
