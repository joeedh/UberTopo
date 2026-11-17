# Road networks, junctions, rail, and transport structures

Part of the [environment-generation research set](README.md). Curve geometry (clothoids, spiros,
alignment) is covered by another research thread; this file covers *network generation*,
*cross-sections*, *junction geometry* and *data sources*. City-block and parcel generation is in
[urban-layout.md](urban-layout.md).

Research status: November 2026, web search plus prior knowledge.

---

## 1. Why roads matter for drone defense

- Roads are **acoustic sources** (traffic noise masks drone signatures) and **hard reflective
  ground** (asphalt flow resistivity is effectively infinite compared with grass, so it gives a
  strong ground reflection and an interference "ground dip").
- **Street canyons** create multi-reflection paths and flutter echoes, so localisation by TDOA
  (time difference of arrival) suffers. The canyon geometry (width/height ratio) is the main
  parameter.
- Roads are **access and approach corridors** for vehicles that launch drones, and **open sky
  corridors** that drones follow at low altitude to avoid rooftops and trees.
- Bridges, overpasses and tunnels create **occluded volumes** and are critical-infrastructure
  targets in their own right.
- Parking lots are large open surfaces (launch sites, sensor sites).

So we need: a network topology (graph), alignment curves (another agent's topic), **cross-section
profiles** (lanes, shoulders, kerbs, sidewalks, verges, medians, barriers), **junction surfaces**,
grade-separated structures, and furniture (poles, signs, barriers — see
[infrastructure.md](infrastructure.md)).

---

## 2. Network generation methods

### 2.1 L-system / growth-based: Parish & Müller 2001
"Procedural Modeling of Cities" (SIGGRAPH 2001, https://doi.org/10.1145/383259.383292). An
*extended L-system* proposes road segments. **Global goals** (follow population density, keep a
pattern: grid, radial, organic) and **local constraints** (snap to nearby intersections, avoid
water, limit slope, trim at boundaries) accept or adjust each proposal. Highways seek population
peaks; streets fill in by pattern. It is very influential, but in practice the "L-system" is
a priority-queue growth algorithm with rule-based proposals. Most re-implementations drop the
formal grammar.

### 2.2 Tensor-field street modeling: Chen, Esch, Wonka, Müller, Zhang 2008
"Interactive procedural street modeling" (SIGGRAPH 2008, https://doi.org/10.1145/1360612.1360702).
The user designs a 2D **tensor field** from basis fields (grid, radial, boundary-aligned along
coastlines/rivers, heightfield-gradient-aligned). Major and minor **hyperstreamlines** are traced
to form the network. It is excellent for controllable, *globally coherent* patterns that follow
terrain and water, and the streamlines are naturally smooth curves (good for clothoid fitting).
Degenerate points of the field become irregular junctions, which look plausible. Recommended as
the **primary synthetic method for local/collector streets** in UberTopo.

### 2.3 Example-based
- Aliaga, Vanegas, Beneš 2008, "Interactive example-based urban layout synthesis" (SIGGRAPH Asia,
  https://doi.org/10.1145/1409060.1409113): synthesises new networks from fragments of real maps
  (with imagery).
- Nishida, Garcia-Dorado, Aliaga 2016, "Example-driven procedural urban roads" (CGF,
  https://doi.org/10.1111/cgf.12728): extracts patch statistics from real OSM networks and grows
  new ones that match a city's style. **This is the natural way to make "a town like X"**,
  and it pairs well with OSM.
- Yang, Wonka, Mitra et al. 2013, "Urban pattern: layout design by hierarchical domain
  splitting" (https://doi.org/10.1145/2508363.2508405): splits the domain recursively (streamline
  and template splits) into blocks. Blocks come out well shaped.

### 2.4 Terrain-aware and hierarchical
- **Galin, Peytavie, Maréchal, Guérin 2010, "Procedural generation of roads"** (CGF/Eurographics,
  https://doi.org/10.1111/j.1467-8659.2009.01612.x): **anisotropic weighted shortest path** over a
  discretised neighbourhood. The cost combines slope, curvature, water crossings (bridges) and
  tunnels. It generates highway/rural roads that respect terrain, and it emits bridges and tunnels
  where they are cheaper.
- **Galin et al. 2011, "Authoring hierarchical road networks"** (CGF/PG,
  https://doi.org/10.1111/j.1467-8659.2011.02055.x): highways connect cities, primary roads
  connect towns, secondary roads connect villages, each level built with the weighted-path method
  and merged.
- **Recommended hierarchy for UberTopo**:
  1. *Inter-urban* (motorway/trunk/primary/rail): settlement graph (e.g. a Delaunay/Gabriel graph
     of settlements, pruned by population) → least-cost paths over terrain (Galin 2010) → curve
     fitting with design-speed alignment (clothoids).
  2. *Arterial*: tensor-field major streamlines, or OSM.
  3. *Collector/local*: tensor-field minor streamlines, grid templates or example-based growth
     inside arterial cells.
  4. *Access/driveways/service*: generated per parcel ([urban-layout.md](urban-layout.md)).
  5. *Paths/tracks*: least-cost paths with low weight on slope; desire lines.

### 2.5 Simulation-coupled
- Weber, Müller, Wonka, Gross 2009, "Interactive geometric simulation of 4D cities" (CGF,
  https://doi.org/10.1111/j.1467-8659.2009.01400.x): roads, land use and buildings grow over
  time.
- Vanegas et al. 2009 couple road networks with UrbanSim-style behavioural simulation (see
  [urban-layout.md](urban-layout.md)).

### 2.6 Street network statistics (for validation)
OSMnx (Boeing, https://osmnx.readthedocs.io) computes intersection density, node degree
distribution, street orientation entropy, circuity and block size. Use these metrics to **validate
synthetic networks against real cities** of the target region. This is cheap and objective.

---

## 3. Cross-sections

A road is an **alignment curve** (plan + profile + superelevation; another agent covers this) plus
a **station-dependent cross-section**: an ordered list of lanes and elements with widths given as
functions of station (OpenDRIVE uses cubic polynomials in s). Elements:

- travel lanes (2.7–3.75 m), turn lanes, bus/bike lanes, parking lanes (2.0–2.6 m)
- shoulders (paved or gravel), kerbs (height 0.10–0.20 m; a step in the ground that matters for
  ground-reflection modelling and for wheeled robots), gutters
- sidewalks (1.5–5 m), verges/planting strips with street trees, medians (raised, planted,
  barrier), crash barriers (W-beam, concrete Jersey; acoustically these are low walls), noise
  barriers (3–6 m tall; acoustically very important, generating diffraction edges)
- cut/fill slopes and ditches linking the road edge to terrain (embankment slopes 1:2–1:4)

**Representation recommendation:** the road surface in the B-rep is a **sweep of the
cross-section along the alignment**, giving faces tagged per lane. Lane boundaries are embedded
curves (offset curves of the reference line) carried as edges or as attribute curves on faces.
Tessellation happens on demand to the required chord tolerance. This matches the requirement to
embed curves in the B-rep for road tessellation. The terrain must be **conformed** to the road
corridor: the road footprint cuts or stamps the terrain, plus cut/fill side slopes. That is a
Boolean-like imprint of the corridor boundary curves into the terrain surface, so the kernel needs
robust curve imprinting on terrain faces.

---

## 4. Junction (intersection) geometry

Junctions are the hardest part of road geometry, both procedurally and from OSM.

### 4.1 Geometric elements
- **Corner radii / kerb returns**: 3–8 m on urban local streets (NACTO argues for small radii),
  10–15 m+ for arterials with trucks. AASHTO design vehicles (P, SU, WB-40/62/67) set swept-path
  envelopes. Three-centred compound curves or taper + radius are common.
- **Stop lines, crosswalks, refuge islands, channelising islands** (painted or raised), splitter
  islands at roundabouts.
- **Turn lanes and tapers**: storage length + taper (e.g. 15:1 to 30:1 depending on design speed).
- **Roundabouts**: inscribed circle diameter (mini 13–25 m, single-lane 30–55 m, multi-lane
  45–80 m), central island, truck apron, entry/exit flare, deflection.
- **Interchanges** (grade-separated): diamond, cloverleaf, partial cloverleaf (parclo),
  single-point urban interchange (SPUI), stack, turbine, trumpet. Ramps are alignments with
  clothoid transitions, and the structures are bridges.
- **Vertical**: junction surface blending is needed where crossing roads have different grades and
  crossfalls. That is a surface-fitting problem: fill the junction region with a smooth surface
  matching the boundary curves and the cross-slopes of incoming roads (Coons/Gregory patches, or
  a thin-plate-spline heightfield over the junction polygon).

### 4.2 Algorithmic approach
1. For each incoming road, offset the reference line by the road half-widths to get boundary
   curves.
2. Intersect adjacent (in angular order) boundary curves. Trim each road back to the furthest
   intersection plus a clearance. This is the osm2streets approach: "trim back roads to avoid
   overlap; the common area becomes the intersection polygon."
3. Insert kerb-return fillets (arcs or three-centre curves) between adjacent boundary curves.
4. Generate **connecting lanes** (OpenDRIVE "connecting roads") from each incoming lane to legal
   outgoing lanes, with lane-level turn restrictions. Their paths are clothoid/cubic curves.
5. Build the junction surface face (polygon with curved edges) and the vertical blend.
6. Add crosswalks, stop lines, islands and signals (attributes for later furniture placement).

### 4.3 Tools and references
- **osm2streets** (A/B Street, https://github.com/a-b-street/osm2streets): turns OSM ways into
  lane-level road segments and intersection polygons. It infers lanes from tags (`lanes`,
  `turn:lanes`, `sidewalk`, `cycleway`, `width`, `parking:*`). It is the best open reference for
  OSM → junction geometry. Its maintainers say intersection polygons are guessed from road width,
  and snapping parallel cycleways/sidewalks is incomplete.
- Wilkie, Sewall, Lin 2012, "Transforming GIS data into functional road models for large-scale
  traffic simulation" (TVCG, https://doi.org/10.1109/TVCG.2011.116): fits arcs/clothoids to GIS
  polylines and builds junctions.
- Becker et al. 2020, "Generation of complex road networks using a simplified logical
  description for the validation of automated vehicles" (https://arxiv.org/abs/2006.03403):
  computes OpenDRIVE junction quantities from a logical description.
- Ikram et al. 2023 (https://arxiv.org/abs/2303.17900) and Muktadir et al. (SAE 2023): procedural
  intersections and roundabouts for AV testing.
- **ASAM OpenDRIVE** (https://www.asam.net/standards/detail/opendrive/): the standard
  for lane-level road description (reference lines from line/arc/spiral/poly3/paramPoly3, lane
  sections, elevation, superelevation, junctions, objects, signals). Even if we do not adopt it
  internally, **import/export of OpenDRIVE** connects us to CARLA, esmini, RoadRunner and AV
  toolchains. libOpenDRIVE (https://github.com/pageldev/libOpenDRIVE) parses and meshes it.
- Reconstruction from LiDAR to OpenDRIVE: "Divide and Conquer" (2024,
  https://arxiv.org/abs/2407.18703); Eisemann & Maucher (https://arxiv.org/abs/2405.07544).
- Design guides: AASHTO "A Policy on Geometric Design of Highways and Streets" (Green Book);
  NACTO Urban Street Design Guide (https://nacto.org/publication/urban-street-design-guide/);
  UK DMRB (https://www.standardsforhighways.co.uk/dmrb/). Encode their parameter tables as
  regional design profiles.

---

## 5. Bridges, overpasses, tunnels

- **Placement**: emitted by least-cost routing where water or valley crossing is cheaper by
  structure (Galin 2010), by grade separation at interchanges, or by OSM `bridge=*`, `tunnel=*`,
  `layer=*`.
- **Bridge typology**: beam/girder (most overpasses), box girder, truss, arch, cable-stayed,
  suspension. Parameterise by span, which sets the type, deck depth and pier spacing.
  Generate deck = sweep of cross-section along the alignment; piers on a station schedule;
  abutments; parapets/railings.
- **Bridges are non-manifold-relevant**: the deck sits above terrain and water, the volume under
  the deck is an occluded region (a drone can hide there), and the underside is an acoustic
  reflector that forms a "ceiling" over the road below.
- **Tunnels**: portal geometry + bored/cut-and-cover profile swept along the alignment; the
  terrain surface is unchanged except at portals. **Terrain with tunnels is not a heightfield**,
  so it needs volumetric or B-rep terrain locally (see [terrain.md](terrain.md)).
- **Cuttings/embankments, retaining walls**: retaining walls are acoustic barriers.

---

## 6. Sidewalks, paths, parking

- **Sidewalks**: part of the cross-section, or (as in OSM increasingly) separate `footway=sidewalk`
  ways. Kerb ramps at crosswalks.
- **Pedestrian networks**: plazas (`area:highway=pedestrian`), paths, stairs (`highway=steps`).
- **Parking lots**: generate stall layouts by tiling the lot polygon with module templates
  (bay = aisle + two stall rows; 90°/60°/45° stalls; aisle 6–7.3 m for 90°). Peng et al. 2014
  deformable templates handle irregular lots. Add landscape islands, light poles and entrances.
  Parking structures are buildings with ramps (helical or split-level).
- OSM: `amenity=parking` + `parking=surface|multi-storey|underground`, `parking_space` polygons
  (sparse).

---

## 7. Rail

- **Alignment**: like roads, but with stricter curvature and gradient (gradients usually < 2–3.5%,
  large radii, clothoid/cubic-parabola transitions, cant/superelevation).
- **Cross-section**: ballast shoulder, sleepers, rails (the rail is a profile swept on embedded
  curves), formation, drainage ditches, catenary masts (electrified lines) at 50–65 m spacing, and
  overhead line equipment. These are thin geometry that matters for drones (hazard) and for
  cameras (clutter). See [infrastructure.md](infrastructure.md).
- **Track topology**: switches/turnouts (geometric template), crossings, sidings, yards (fan of
  parallel tracks from ladders), stations (platforms 0.55–1.1 m high, canopies).
- **Data**: OSM `railway=*` (rail, tram, subway, light_rail; `electrified`, `gauge`, `usage`);
  OpenRailwayMap (https://www.openrailwaymap.org). The railML standard
  (https://www.railml.org) covers infrastructure exchange.
- Trams and light rail run in street cross-sections (embedded track lanes).

---

## 8. LOD variants

| LOD | Representation | Use |
|---|---|---|
| R0 | Graph: nodes + polyline/curve edges, class, width | Planning, routing, coarse LOS |
| R1 | Ribbon surfaces (constant width) draped on terrain; junctions as simple polygons | Large-area acoustics/visibility |
| R2 | Lane-level surfaces, kerbs, sidewalks, conformed terrain, bridges as solid decks | Default urban simulation |
| R3 | Full detail: markings, barriers, noise walls, furniture, drainage, bridge structure | Camera simulation, near-field |

Acoustically, R1/R2 are enough for propagation (kerbs and barriers matter, markings do not). For
cameras, markings and furniture matter at close range.

---

## 9. Recommendations

1. Internal model: **network graph + alignment curves + station-parameterised cross-sections +
   junction objects**, close to OpenDRIVE's conceptual model but with exact curves (clothoids
   etc.) stored in the kernel. Support OpenDRIVE import/export.
2. Synthetic networks: hierarchical (Galin 2011 least-cost for inter-urban, tensor fields for
   urban, example-based growth from OSM statistics for style).
3. OSM networks: osm2streets-style lane inference + our own junction solver + alignment fitting
   (smooth OSM polylines into clothoid splines) + terrain conformance.
4. Validate with OSMnx metrics against the target region.
5. The road corridor must imprint and modify terrain (cut/fill), so the terrain representation
   must accept curve-bounded imprints and local non-heightfield structures (bridges, tunnels,
   retaining walls).

---

## References

- Parish & Müller, "Procedural Modeling of Cities", SIGGRAPH 2001 — https://doi.org/10.1145/383259.383292
- Chen et al., "Interactive procedural street modeling", SIGGRAPH 2008 — https://doi.org/10.1145/1360612.1360702
- Aliaga, Vanegas, Beneš, "Interactive example-based urban layout synthesis", SIGGRAPH Asia 2008 — https://doi.org/10.1145/1409060.1409113
- Nishida, Garcia-Dorado, Aliaga, "Example-driven procedural urban roads", CGF 2016 — https://doi.org/10.1111/cgf.12728
- Yang et al., "Urban pattern: layout design by hierarchical domain splitting", SIGGRAPH Asia 2013 — https://doi.org/10.1145/2508363.2508405
- Galin et al., "Procedural generation of roads", CGF 2010 — https://doi.org/10.1111/j.1467-8659.2009.01612.x
- Galin et al., "Authoring hierarchical road networks", CGF 2011 — https://doi.org/10.1111/j.1467-8659.2011.02055.x
- Weber et al., "Interactive geometric simulation of 4D cities", CGF 2009 — https://doi.org/10.1111/j.1467-8659.2009.01400.x
- Wilkie, Sewall, Lin, "Transforming GIS data into functional road models", TVCG 2012 — https://doi.org/10.1109/TVCG.2011.116
- Peng, Yang, Wonka, "Computing layouts with deformable templates", SIGGRAPH 2014 — https://doi.org/10.1145/2601097.2601164
- Becker et al., 2020 — https://arxiv.org/abs/2006.03403 ; Ikram et al., 2023 — https://arxiv.org/abs/2303.17900
- "Divide and Conquer" OpenDRIVE from point clouds, 2024 — https://arxiv.org/abs/2407.18703 ; Eisemann & Maucher — https://arxiv.org/abs/2405.07544
- osm2streets — https://github.com/a-b-street/osm2streets ; A/B Street map docs — https://a-b-street.github.io/docs/tech/map/index.html
- ASAM OpenDRIVE — https://www.asam.net/standards/detail/opendrive/ ; libOpenDRIVE — https://github.com/pageldev/libOpenDRIVE
- OSMnx — https://osmnx.readthedocs.io
- NACTO Urban Street Design Guide — https://nacto.org/publication/urban-street-design-guide/
- UK DMRB — https://www.standardsforhighways.co.uk/dmrb/
- OpenRailwayMap — https://www.openrailwaymap.org ; railML — https://www.railml.org
