# City layout, land use, blocks and parcels

Part of the [environment-generation research set](README.md). Roads are in
[road-networks.md](road-networks.md); building typologies in [buildings.md](buildings.md).

Research status: November 2026, web search plus prior knowledge.

---

## 1. The layering

Urban generation is a pipeline of nested 2D partitions, each refining the last:

```
region  ->  land-use zones  ->  (arterial road cells)  ->  blocks  ->  parcels/lots  ->  building footprint + yard/open space
           (density field)       (road network)            (local streets)  (subdivision)   (setbacks, typology)
```

Every stage is a **planar subdivision** (a planar graph with faces with holes). The kernel needs
robust 2D arrangement/overlay operations (CGAL Arrangement_2 style) with attributes on faces,
edges and vertices, and **persistent IDs** so that local edits (moving a road) only regenerate the
affected cells.

---

## 2. Land use and density

### 2.1 Driving fields
- **Population/employment density fields** steer road growth (Parish & Müller 2001), parcel size
  and building height. Real sources: WorldPop (https://www.worldpop.org, 100 m),
  GHSL (Global Human Settlement Layer, https://human-settlement.emergency.copernicus.eu/, built-up
  surface, built-up volume, population, settlement typology "degree of urbanisation"),
  LandScan (ORNL, https://landscan.ornl.gov). GHSL's built-up volume and height layers are a good
  prior for building height where no footprint height exists.
- **Synthetic density**: monocentric (exponential decay from the CBD, Clark 1951) or polycentric
  (sum of kernels at sub-centres), modulated by terrain slope, water and transport accessibility.
- **Land-use classes**: residential (low/medium/high), commercial, mixed use, industrial,
  institutional, military, agricultural, park/recreation, transport (rail yards, airports),
  utilities. Real sources: OSM `landuse=*`, Copernicus Urban Atlas
  (https://land.copernicus.eu/en/products/urban-atlas, EU cities, 27 classes), US national and
  local zoning datasets (e.g. National Zoning Atlas, https://www.zoningatlas.org).

### 2.2 Assigning land use synthetically
- **Rule-based**: von Thünen / Burgess concentric rings, Hoyt sectors, multiple nuclei. These are
  rough but quick. Industrial goes along rail/water/highways, commercial along arterials and
  junctions, residential fills the rest, parks on steep or flood-prone land.
- **Cellular automata** (White & Engelen 1993; SLEUTH, Clarke et al. 1997): grid-based land-use
  transitions with neighbourhood rules. Good for "grow this town 30 years".
- **UrbanSim** (Waddell 2002, "UrbanSim: Modeling urban development for land use, transportation,
  and environmental planning", JAPA, https://doi.org/10.1080/01944360208976274;
  https://github.com/UDST/urbansim): agent-based/discrete-choice microsimulation of households,
  jobs and real-estate developers on parcels. Coupled to 3D geometry by Vanegas et al. 2009,
  "Interactive design of urban spaces using geometrical and behavioral modeling" (SIGGRAPH Asia,
  https://doi.org/10.1145/1618452.1618457): behavioural simulation sets land value and use, and
  geometry follows. UrbanSim is overkill for scenario generation, but it is the reference if
  "plausible economic structure" ever matters.
- **Agent-based procedural cities**: Lechner, Watson, Wilensky 2003, "Procedural city modeling"
  (agents for developers and road extenders); Weber et al. 2009 (4D cities, see roads).

For drone-defense scenarios, **rule-based assignment conditioned on real regional statistics** is
enough. Fidelity of the *spatial pattern* (where tall buildings, open areas, industrial zones are)
matters more than economic plausibility.

---

## 3. Blocks

Blocks are the faces of the road graph once local streets are added (see the tensor-field and
domain-splitting methods in [road-networks.md](road-networks.md)). Practical concerns:
- Compute block polygons from **road edge curves offset by half the road corridor width** (not
  centrelines), giving the *buildable block* boundary with curved edges.
- Handle degenerate blocks: slivers, very small blocks (become plazas or green space), very large
  blocks (insert private roads/superblock internal paths).
- Block depth sets the possible typologies: deep blocks give perimeter blocks with courtyards;
  shallow blocks give row houses; huge blocks give industrial/campus.

---

## 4. Parcel subdivision

### 4.1 Vanegas et al. 2012
"Procedural generation of parcels in urban modeling" (Vanegas, Kelly, Weber, Halatsch, Aliaga,
Müller; CGF/Eurographics 2012, https://doi.org/10.1111/j.1467-8659.2012.03034.x). Two methods:

1. **Oriented bounding box (OBB) subdivision**: recursively split the block by a line
   perpendicular to the long axis of its minimum-area bounding box, near the middle, until parcels
   reach a target area/width. Reject splits that leave a parcel without street access, using a
   frontage constraint. This gives grid-like, rectangular parcels suited to planned
   suburbs and Manhattan-style blocks.
2. **Straight-skeleton subdivision**: compute the block's straight skeleton. Each skeleton face is
   the region "closest" to one block edge, so every parcel fronts a street. Faces become strips
   that are cut perpendicular to the frontage at regular widths, with corner regions reassigned
   (diagonal-edge handling). This gives realistic European-style perimeter parcels with uniform
   depth.

Both methods take user parameters (target area, width, irregularity), and they reproduce real
parcel statistics. Note the reuse: **the same straight-skeleton code serves roofs and parcels.**

### 4.2 Other approaches
- **Template tilings**: Peng, Yang & Wonka 2014's deformable templates
  (https://doi.org/10.1145/2601097.2601164) also tile blocks into lots.
- **Learned parcel subdivision** exists (e.g. GAN/graph models trained on cadastre). Treat it as
  experimental.
- **Real cadastre**: many countries publish parcels (NL BRK, DK Matrikel, US county GIS, Regrid
  commercial nationwide US parcels, https://regrid.com). Where available, use real parcels;
  procedural subdivision fills the gaps.

### 4.3 From parcel to building
Per parcel: zoning/typology → setbacks (front/side/rear), build-to lines, max coverage, FAR, height
→ envelope → footprint (rectangular/L/U/courtyard templates fitted to the envelope, or the full
envelope for perimeter blocks) → building generator ([buildings.md](buildings.md)). Residual open
space → yard/garden/parking/hardscape → vegetation placement
([vegetation-and-biomes.md](vegetation-and-biomes.md)) and furniture.

---

## 5. Special land-use layouts (relevant to drone threat sites)

| Site | Layout logic |
|---|---|
| **Military base** | Fenced perimeter, gates with chicanes and standoff (UFC 4-010-01), functional clusters (HQ, barracks, motor pool, hangars, ammo storage with earth-covered magazines at explosive safety distances), airfield. Sensor networks usually ring the perimeter and the high-value assets. |
| **Airport** | Runway(s) aligned with prevailing wind, taxiways, aprons, terminal(s), control tower, hangars, fuel farm. OSM `aeroway=*` is very complete for major airports. FAA/ICAO obstacle surfaces (approach, transitional) are relevant to drone restrictions. |
| **Prison** | Double fence/wall perimeter, cell blocks around yards, clear zone. Contraband-drone use case. |
| **Stadium / event venue** | Bowl + concourse + parking + transit stops; temporary crowd areas. |
| **Power plant / substation / refinery** | See [infrastructure.md](infrastructure.md); a regular, equipment-driven grid. |
| **Port** | Quay line, container stacks (block grids of 2.6×2.9×12.2 m boxes stacked 4–6 high), cranes, warehouses. |
| **Farmstead / rural** | Farmhouse + barns + silos clustered near the road, with fields beyond. |
| **Informal settlement** | Organic accretion along paths; minimal open space; see [buildings.md](buildings.md). |

**Recommendation:** implement these as **site templates**: parameterised layout programs that
consume a site polygon and an access point and emit sub-zones, internal roads, fences and building
programs. Military and critical-infrastructure templates are high priority for the drone-defense
domain.

---

## 6. Validation and statistics

- **Morphometrics**: momepy (https://docs.momepy.org) computes building/plot/block/street
  morphometric indicators (coverage, compactness, street profile width/height ratio, etc.). Use it
  to compare synthetic districts with real ones. This matters because **street-canyon H/W ratio
  and building coverage directly drive acoustic reverberation and LOS statistics**.
- OSMnx for network metrics (see [road-networks.md](road-networks.md)).
- Space Syntax integration/choice measures for pedestrian movement (optional).

---

## 7. LOD and simulation implications

- At **regional scale** (tens of km), urban areas can be represented as *statistical urban canopy*
  cells: building coverage, mean/variance of height, H/W ratio, mean facade absorption. This is
  enough for coarse acoustic propagation (e.g. Nord2000/Harmonoise-like engineering models
  treat built areas statistically) and for probabilistic LOS. ISO 9613-2 has a simple
  "propagation through built-up areas" term.
- At **district scale**, LOD1/2 buildings plus road ribbons.
- At **site scale**, LOD3+ around the sensor network and the protected asset, with procedural
  detail generated lazily.

The layout pipeline must therefore be able to **stop at any stage** and emit statistical
summaries instead of geometry. For example, a land-use cell carries "expected building
coverage 0.35, mean height 12 m" until it is refined.

---

## 8. ML-based layout generation (brief; see [ml-generation.md](ml-generation.md))
Diffusion and GNN models now generate plausible block, road and building-footprint layouts
(CityGen, ControlCity, CityDreamer, 2024–2025 work). They are useful as "style samplers". They do
not guarantee topological validity, street access per parcel or regulatory constraints, so wrap
them in a procedural validator.

---

## References

- Parish & Müller, "Procedural Modeling of Cities", SIGGRAPH 2001 — https://doi.org/10.1145/383259.383292
- Vanegas et al., "Procedural generation of parcels in urban modeling", CGF 2012 — https://doi.org/10.1111/j.1467-8659.2012.03034.x
- Vanegas et al., "Interactive design of urban spaces using geometrical and behavioral modeling", SIGGRAPH Asia 2009 — https://doi.org/10.1145/1618452.1618457
- Vanegas et al., "Modelling the appearance and behaviour of urban spaces", CGF 2010 — https://doi.org/10.1111/j.1467-8659.2009.01535.x
- Waddell, "UrbanSim", JAPA 2002 — https://doi.org/10.1080/01944360208976274 ; code — https://github.com/UDST/urbansim
- Smelik et al., "A survey on procedural modelling for virtual worlds", CGF 2014 — https://doi.org/10.1111/cgf.12276
- Weber et al., "Interactive geometric simulation of 4D cities", CGF 2009 — https://doi.org/10.1111/j.1467-8659.2009.01400.x
- Yang et al., "Urban pattern", SIGGRAPH Asia 2013 — https://doi.org/10.1145/2508363.2508405
- WorldPop — https://www.worldpop.org ; GHSL — https://human-settlement.emergency.copernicus.eu/ ; LandScan — https://landscan.ornl.gov
- Copernicus Urban Atlas — https://land.copernicus.eu/en/products/urban-atlas
- National Zoning Atlas — https://www.zoningatlas.org
- Regrid parcels — https://regrid.com
- momepy — https://docs.momepy.org ; OSMnx — https://osmnx.readthedocs.io
- UFC 4-010-01 — https://www.wbdg.org/ffc/dod/unified-facilities-criteria-ufc/ufc-4-010-01
