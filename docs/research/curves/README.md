# Curves and Surfaces for Procedural Environment Modelling — Research Index

Research for UberTopo (see `docs/requirements.md`) on the curve and surface
machinery needed to model roads, rail, rivers, cables, pipes and similar linear
features at many levels of detail. The requirements say that curves will mostly be
numerically integrated transcendental curves (spiros, polynomial clothoids,
clothoids) tessellated into polynomial basis splines, and that curves and surfaces
will be embedded in the B-rep as attribute data. This folder looks at what that
means in practice.

> **Research caveat:** web *search* worked while this was written, but direct page
> fetches were blocked by the sandbox network policy (arxiv.org, asam.net,
> buildingsmart.org and others could not be opened). Every cited work was
> confirmed to exist via search results. Formulas and schema details come from the
> literature as known to the author and should be checked against the originals
> before they are used as test oracles or importer specs. Places that need checking
> are flagged inline.

## Files

| File | Contents |
|---|---|
| [clothoids-and-spirals.md](clothoids-and-spirals.md) | Clothoid/Euler spiral; Fresnel integral evaluation (series, asymptotic, Heald, Boersma, Cody, Cephes); Bertolazzi–Frego G1/G2 Hermite fitting; clothoid splines; Levien's thesis, Spiro and Euler-spiral work in kurbo; polynomial spirals; Hobby splines; κ-curves; log-aesthetic curves; PH curves |
| [road-alignment-standards.md](road-alignment-standards.md) | Horizontal (tangents, arcs, clothoid/Bloss/cubic parabola/sine/cosine/Helmert/Viennese transitions), vertical (parabolic crest/sag, K values), superelevation/cant, lanes and offsets; LandXML, ASAM OpenDRIVE, IFC 4.3 `IfcAlignment`; what a road network model must store |
| [numerical-evaluation.md](numerical-evaluation.md) | Integrating $\int e^{i\theta(s)}ds$: Fresnel route and its cancellation, Bertolazzi–Frego generalised Fresnel moments, per-piece Gauss–Legendre/Taylor/Chebyshev, ODEs; arc length and inverse; closest point; intersection of transcendental curves; error control |
| [spline-approximation-and-offsets.md](spline-approximation-and-offsets.md) | Hermite-based B-spline approximation with error bounds; knot placement; degree choice; preserving G2/G3; area/moment cubic fitting; offsets and variable offsets (lane edges); cusp handling; flattening with nested LODs |
| [space-curves-and-frames.md](space-curves-and-frames.md) | 3D alignments (station vs 3D length), helices, catenaries (power lines), Frenet vs rotation-minimising frames (double reflection), gravity-referenced road/rail frames, sine-generated/Kinoshita river meanders, draping |
| [surfaces-from-curves.md](surfaces-from-curves.md) | Sweeps, $(s,t)$ road surfaces, ruled/loft/Coons/Gordon surfaces, junctions, cut/fill/daylight against terrain, pcurves and trimming, storing curve/surface data as B-rep attributes, LOD table |
| [curve-library-design.md](curve-library-design.md) | Proposed API and data model: `Curve` trait, concrete types, `KappaLaw`, tessellation, B-spline conversion, offsets, queries, frames/sweeps, procedural node integration, robustness, testing, build vs reuse |

Related research elsewhere in `docs/research/` (written separately; reconcile
when consolidating): `attributes-and-queries/embedded-geometry-attributes.md`,
`brep-topology/07-geometry-embedding.md`,
`environment-generation/road-networks.md`.

## Key findings

1. **Engineered linear features are designed by curvature, not position.** Roads,
   rail and canals are lines, arcs and transition spirals (clothoid or polynomial
   curvature laws), with height and cross-slope given as separate functions of
   horizontal station. River meanders are also curvature-defined
   (sine-generated / Kinoshita curves). So the right kernel primitive is a
   **Cesàro curve**: start pose, length and a pluggable curvature law $\kappa(s)$.
   Lines, arcs, clothoids, IFC polynomial spirals, Bloss, Viennese bends, log-aesthetic
   curves and meander curves all fit it, and one integrator serves them all.
2. **Evaluation is a solved problem if it is kept local.** Split each segment into
   pieces with bounded turning (about 0.5 rad or less) and integrate each piece with
   Gauss–Legendre or a Taylor/Chebyshev series. This avoids the cancellation that
   the global Fresnel formulas suffer when $\kappa'$ is small. It also gives
   per-piece polynomial caches with error bounds, and bounds on curvature and
   turning that drive every adaptive algorithm.
3. **Bertolazzi & Frego's work is the reference for clothoids.** Their G1 Hermite
   fit reduces to one equation solved by Newton. Their G2 fit uses three arcs and
   Newton on two equations. They also solved point-to-clothoid projection and
   intersection of clothoid splines. Their C++ `Clothoids` library is the obvious
   test oracle.
4. **Levien's kurbo is the best prior art for the API and for fitting.** Its
   `ParamCurve*` traits are a good model. Its area/moment-matching cubic fit has
   $O(n^{-6})$ error convergence, and its Euler-spiral offset and flattening are
   state of the art for 2D work.
5. **Treat polynomial forms as derived caches with certified bounds.** The default
   should be quintic Hermite B-splines parameterised by source station, with knots
   at every segment join. This gives exact G2 at the knots and $O(h^6)$ error.
   Use septic splines where G3 is needed. Use cubic area/moment or G2-geometric-Hermite
   fits when exporting to cubic-only consumers such as OpenDRIVE paramPoly3.
6. **Offsets of clothoids are not clothoids,** and lane edges are variable-width
   offsets. Approximate the offset of the *exact* centreline using analytic
   derivatives. Never offset an approximation. Cusps occur where $w\kappa = 1$ and
   can be found analytically.
7. **Road and rail cross-sections use a gravity-referenced frame,** not an RMF or
   Frenet frame. The section lies in a vertical plane normal to the *horizontal*
   tangent and is rotated by superelevation or cant. Use RMF (double reflection) for
   pipes, tubes and branches. The frame should belong to the sweep, not the curve.
8. **Road surfaces should be exact $(s,t)$-parameterised surfaces.** Lane borders,
   markings and kerbs are then exact pcurves $t=b(s)$. The 3D edge curves are
   compositions of surface and pcurve, so they are consistent by construction, and
   trimming and tessellation become trivial strips in $s$.
9. **Store geometry in a content-addressed geometry store** that topology
   references through attributes: curve plus interval on edges, pcurves on face-uses,
   surfaces on faces. Adjacent lanes share one offset-curve object, which makes them
   watertight by sharing rather than by tolerance. Tessellations and B-splines are
   caches tagged with tolerance and LOD.
10. **No single interchange format is enough.** OpenDRIVE has clothoids and
    paramPoly3 but leaves junction surfaces undefined. IFC 4.3 has the richest
    spiral set plus linear placement and sectioned sweeps. LandXML is widespread
    but redundant and often inconsistent. OSM has no curvature at all, so a
    "fit G2 alignment to polyline" node is needed for real-world inputs.
11. **Things outside the pavement matter for drone-defence work:** embankments,
    cuttings and daylight lines (occlusion and acoustic reflection), and power-line
    catenaries (thin obstacles that should be queried as curves or tubes, not
    meshes).

## Top recommendations

* Build `CesaroCurve2` with `KappaLaw` and per-piece cached integration. Add
  `Composite`, `Offset2`, `Alignment3` (horizontal × profile × cant) and
  `RoadSurface(s,t)` as first-class exact types.
* Port or wrap the Bertolazzi–Frego algorithms. Adopt kurbo's trait design and
  fitting ideas.
* Make every approximation carry `{source id, range, error bound, parameter map}`.
* Tessellate edges first, from exact curves, with nested LOD vertex sets.
* Model design intent (PI points, radii, spiral lengths, widths as functions of
  station) in procedural nodes. Derive segments with a solver that reports
  diagnostics rather than repairing silently.

## Open questions for the project owner

1. Implementation language? This decides whether kurbo (Rust) or
   Clothoids + OCCT (C++) can be reused directly.
2. Which interchange formats matter, and in what priority? OpenDRIVE import, IFC 4.3,
   LandXML, OSM, CityGML/CityJSON export?
3. Is rail in scope, and does it need G3 transitions (Bloss, sine, Viennese bend)?
4. What accuracy and LOD targets do sensor simulations need, for example acoustic
   reflection (normals and curvature) versus visual (silhouette)? This sets
   tessellation tolerances per LOD.
5. Is exact NURBS export (STEP/IGES) needed, or are meshes plus exact internal
   geometry enough? This decides whether PH and septic machinery is worth building.
6. Scene extent and georeferencing: is one projected CRS per scene acceptable,
   or are multi-tile, large-area scenes needed?
7. Should real-world road data be imported (OSM, national road databases), or is
   everything synthetic? Real data needs robust curve fitting to noisy polylines.
