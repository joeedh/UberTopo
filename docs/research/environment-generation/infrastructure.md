# Physical infrastructure

Part of the [environment-generation research set](README.md). Roads/rail/bridges are in
[road-networks.md](road-networks.md); dams and canals in [water.md](water.md).

Research status: November 2026. Written mostly from prior knowledge plus earlier searches (the
web-search budget ran out). Engineering parameter ranges are typical values for plausibility,
not design values.

---

## 1. Why infrastructure is central for drone defense

Infrastructure is both the **protected asset** (substations, refineries, airports, dams, telecom)
and the **environment that constrains drones and sensors**:

- **Thin, high obstacles** (wires, guy-wires, masts, cranes) are collision hazards for drones,
  hard to detect for cameras, and usually missing from photogrammetry and DSMs.
- **Mounting points** for sensors: utility poles, lighting columns, towers and rooftops, often
  with power and backhaul.
- **Acoustic sources**: transformer hum (100/120 Hz + harmonics, close to drone blade-passing
  frequencies, a serious confuser), HVAC, cooling towers, wind turbines, traffic, industrial
  machinery, trains. Infrastructure generation must output **sound sources with spectra** as
  well as geometry.
- **Acoustic reflectors**: tanks, silos, large flat metal-clad sheds, noise barriers.
- **Camera confusers**: birds on wires, moving cranes, rotating wind turbines, reflective solar
  panels.
- **RF environment** (out of scope here, but the same objects carry RF attributes later).

---

## 2. Power transmission and distribution

### 2.1 Conductors: catenary geometry
- A conductor between supports under self-weight is a **catenary**: y(x) = a·cosh((x − x0)/a) + c,
  with a = H/w (horizontal tension / weight per unit length). For sag/span ratios below about 5%
  the parabola y ≈ x²/(2a) is accurate. The sag for span L is D ≈ wL²/(8H).
- **Inclined spans** (supports at different heights) use the same catenary with shifted x0. Solve
  for a from the span length, height difference and either sag or conductor length.
- **Sag depends on temperature, ice and wind** (thermal elongation, creep). Sag at max operating
  temperature is the clearance-critical state. A *ruling span* simplifies multi-span sections.
  For our purposes, sag is a scenario parameter (summer hot sag vs. winter).
- **Wind blowout**: the conductor swings out of plane under wind load. This matters for drone
  collision volumes.
- **Representation**: each conductor is an **exact space curve** (catenary, a transcendental
  curve, exactly the kind the kernel must embed and tessellate into B-splines on demand)
  plus a radius attribute (10–50 mm). Bundled conductors (2/3/4 sub-conductors with spacers) are
  an attribute, not separate geometry, until close range. Never mesh wires at generation time.
- **Wire types**: phase conductors (3 per circuit; 1–2 circuits per tower), shield/earth wires at
  the top (often OPGW fibre), distribution lines (lower, 3–4 wires on crossarms), service drops
  to buildings, telecom cables on poles, guy-wires (straight, inclined to ground anchors).

### 2.2 Supports
- **Lattice towers** (transmission, 110–765 kV): type by voltage and circuit count (Danube, Donau,
  delta, barrel, single-circuit horizontal). Height 25–70 m+, span 250–450 m. Generate from
  parameterised templates: body taper, cross-arms, insulator strings (suspension vs. tension
  towers at angle points). Members are thin angle sections, so they are **acoustically transparent
  but visually cluttered**. Represent as a lattice of line segments (a 1D cell complex: a
  non-manifold wireframe) with a section attribute; mesh only at close range.
- **Monopoles / H-frames / wood poles**: distribution and sub-transmission.
- **Placement**: along the line route at the nominal span, adjusted to terrain (towers on high
  ground; sag clearance to ground ≥ regulatory minimum, e.g. 6–9 m, checked against the terrain
  profile). Angle towers at direction changes, terminal towers at substations.
- **Line routing (synthetic)**: least-cost path over terrain/land use (avoid settlements, prefer
  straight lines) using the same weighted-path machinery as roads (Galin 2010; see
  [road-networks.md](road-networks.md)), then straight-segment simplification. Real routes look
  like *polylines with few angle points*, unlike roads.
- **Clearance corridor**: vegetation is cleared under transmission lines (right-of-way 30–70 m).
  Drones and their operators use these as open corridors.

### 2.3 Substations
- Fenced yard (grid layout of bays: busbars, breakers, disconnectors, CTs/VTs), transformers (the
  main noise source and a high-value target), control building, gantries (lattice), and line
  entries. Generate as a **site template** on a rectangular yard with bays on a grid
  (10–20 m bay width), a transformer row and a perimeter fence with security gate.
- Data: OSM `power=substation|transformer|line|minor_line|tower|pole|cable`, voltage tags. Open
  Infrastructure Map (https://openinframap.org) renders the OSM power/telecom/pipeline layers
  and is the best quick view of coverage.

### 2.4 Generation plants
- **Solar farms**: rows of panels (fixed tilt facing the equator, or single-axis trackers on N–S
  rows rotating E–W); row pitch is set by ground coverage ratio (0.3–0.5); inverters and a
  substation. Panels are **specular glint sources** for cameras and hard acoustic reflectors over
  large areas. Data: OSM `power=plant` + `plant:source=solar`, `generator:source=solar`; global ML
  solar inventory (Kruitwagen et al. 2021, Nature, https://doi.org/10.1038/s41586-021-03957-7).
- **Wind turbines**: tower 80–160 m, rotor 100–200 m+ diameter. Rotating blades are camera and
  radar clutter and an acoustic source (blade swish, low frequency). Data: USWTDB
  (https://eerscmap.usgs.gov/uswtdb/), OSM `generator:source=wind`.
- **Thermal/nuclear plants**: cooling towers (hyperboloid of revolution: an analytic surface),
  stacks, turbine halls, switchyards, coal yards.

---

## 3. Telecommunications

- **Towers/masts**: self-supporting lattice (3- or 4-legged), monopoles, **guyed masts** (up to
  600 m; guy-wires are major drone hazards, and the guy anchors spread over a large radius),
  rooftop antennas, camouflaged "monopines". Antenna panels at sector azimuths, microwave dishes.
- **Data**: FCC Antenna Structure Registration (https://www.fcc.gov/wireless/systems-utilities/antenna-structure-registration)
  for US structures above about 61 m or near airports; **FAA Digital Obstacle File** (DOF,
  https://www.faa.gov/air_traffic/flight_info/aeronav/digital_products/dof/) lists vertical
  obstacles (towers, stacks, buildings, wind turbines) with heights and lighting. This is an
  important layer for drone-airspace work. ICAO eTOD (electronic terrain and obstacle data)
  equivalents exist elsewhere. OSM `man_made=mast|tower` + `tower:type=communication`.
  OpenCelliD (https://opencellid.org) gives cell IDs with approximate positions, not structures.
- **Fibre/cable**: buried (not geometric except for manholes/cabinets) or aerial on poles.

---

## 4. Pipelines

- Buried (only markers, valves and compressor/pump stations are visible) or above-ground (Alaska,
  industrial sites, desert; supports every 10–20 m, expansion loops, pipe racks).
- Pipe = swept circular profile along an alignment curve (polyline with bend radii), the same
  sweep machinery again.
- Stations: compressor (acoustic source), pigging stations, tank farms.
- Data: OSM `man_made=pipeline` (incomplete; often sensitive), national regulators (e.g. US NPMS,
  restricted).

---

## 5. Fences, walls, barriers

These matter a lot for site security and acoustics:
- **Types**: chain-link (acoustically and visually mostly transparent; a camera sees through it
  but it causes moiré), palisade, weld-mesh, solid timber, masonry walls, concrete T-walls/blast
  walls (military), HESCO barriers, Jersey barriers, hedges, **noise barriers** along highways
  (3–6 m, absorptive or reflective panels, crucial diffracting edges), crash barriers, bollards.
- **Representation**: a polyline/curve along the ground + height profile + type. A wall is a
  thin solid; a fence is a **non-manifold sheet** (zero-thickness face) with a porosity/
  transmission attribute, which the non-manifold B-rep supports directly. Posts are instanced
  at spacing.
- **Generation**: parcel boundaries (from [urban-layout.md](urban-layout.md)) with per-typology
  probabilities (front fences, rear walls), site perimeters (double fence + clear zone + patrol
  road for high-security sites), field boundaries (wire/post fences, hedgerows, dry-stone walls).
- **Data**: OSM `barrier=fence|wall|hedge|retaining_wall|gate|bollard`, `fence_type=*`, `height=*`.

---

## 6. Utility poles, lighting and street furniture

- **Utility poles**: 9–15 m, spacing 30–60 m along roads, crossarms, transformers (pole-mounted
  "pots"), streetlights. Generate along road edges per regional practice: overhead
  distribution in N. America, much of Asia and rural Europe; mostly underground in European
  cities.
- **Street lights**: spacing about 3–4 × mounting height; mounting height 6–12 m. Lighting matters
  for night camera simulation (light sources).
- **Street furniture**: traffic signals, signs, gantries over highways (good sensor mounts),
  CCTV poles, bus shelters, benches, bins, kiosks, bollards, planters, bike racks, hydrants.
  Generate via **placement rules on the road cross-section and junction objects** (signals at
  stop lines, signs at approach distances, lights on a spacing schedule).
- **Data**: OSM `highway=street_lamp|traffic_signals|stop`, `man_made=street_cabinet`,
  `amenity=bench`, `man_made=surveillance` (existing CCTV, relevant to sensor-network design!).
  Mapillary detections (https://www.mapillary.com) of signs and poles.

---

## 7. Industrial plants

- **Refineries / chemical plants**: process units on a grid, pipe racks (the network backbone),
  distillation columns (tall cylinders), flare stacks, **tank farms** (fixed/floating-roof
  cylindrical tanks in bunded compounds: a strong curved reflector), spheres (LPG), cooling towers,
  control rooms. Generate by site template: tank farm cells + process unit grid + pipe-rack
  spine graph.
- **Factories**: sheds (sawtooth roofs, portal frames), yards, chimneys, silos, conveyors.
- **Ports**: container stacks (grid of 20/40 ft boxes, 4–6 high), ship-to-shore gantry cranes
  (very tall, moving), straddle carriers, quays, warehouses.
- **Mines/quarries**: benched pits (terrain), conveyors, headframes, spoil heaps.
- **Data**: OSM `man_made=storage_tank|silo|chimney|crane|works`, `landuse=industrial`.

---

## 8. Airports and heliports

- Runways (length 1.5–4 km, width 30–60 m, markings, lighting), taxiways, aprons, terminals, piers
  with jet bridges, control tower, hangars, fuel farms, ILS/VOR antennas, perimeter fence,
  approach lighting.
- **Obstacle limitation surfaces** (ICAO Annex 14: approach, transitional, inner horizontal,
  conical) are geometric volumes we should generate. They define restricted airspace and are
  natural constructs for drone-threat analysis.
- **Data**: OSM `aeroway=*` (very complete), OurAirports (https://ourairports.com/data/), FAA NASR.
  Heliports: hospital roofs (`aeroway=helipad`), a rooftop landing zone tie-in with
  [buildings.md](buildings.md).

---

## 9. Bridges and dams (cross-references)

Bridges: [road-networks.md](road-networks.md) §5. Dams: [water.md](water.md) §7. Both are critical
infrastructure; their undersides and spillways are occluded volumes.

---

## 10. LOD and representation summary

| Object | Far | Mid | Near |
|---|---|---|---|
| Power line | Polyline + clearance corridor | Catenary curves per conductor (attribute radius) | Tessellated tubes, insulators, spacers |
| Lattice tower | Vertical line + footprint + height | Envelope solid (low-density medium) | Member wireframe → swept sections |
| Fence | Line + height + transmission attr. | Non-manifold sheet | Posts + mesh/rails |
| Solar farm | Polygon + GCR + tilt | Row solids | Individual panels + trackers |
| Tank farm | Cylinders as prisms | Cylinders + roofs + bunds | Stairs, piping, vents |
| Street furniture | Not represented (statistical clutter density) | Instanced proxies | Detailed assets |

The pattern recurs: **1D elements (wires, members, fences) should stay curves/segments with
attributes as long as possible.** The kernel needs first-class wire edges (edges without faces)
in the non-manifold structure, and queries such as "minimum distance from a drone trajectory
to any wire" against embedded curves, without tessellating them.

---

## 11. Recommendations

1. Build infrastructure as **site templates** (substation, solar farm, tank farm, airport,
   military compound) and **linear-feature generators** (power lines, pipelines, fences, rail
   electrification) driven by routing over terrain and land use.
2. Wires are exact catenary curves in the kernel. Sag is a parameter (temperature/ice).
3. Ingest OSM power/telecom/man_made/barrier layers plus FAA DOF/FCC ASR (US) for real
   obstacles; OSM coverage of power lines is good, of fences and poles patchy.
4. Every infrastructure generator also emits **acoustic sources** (with spectra and directivity)
   and **sensor-mounting candidates** (pole tops, tower platforms, roof edges with power).
5. Produce **obstacle volumes** (ICAO surfaces, wire collision envelopes) as derived geometry for
   vulnerability and path-planning queries.

---

## References

- Open Infrastructure Map — https://openinframap.org
- OSM Power tagging — https://wiki.openstreetmap.org/wiki/Power_networks (*verify page name*)
- FCC Antenna Structure Registration — https://www.fcc.gov/wireless/systems-utilities/antenna-structure-registration
- FAA Digital Obstacle File — https://www.faa.gov/air_traffic/flight_info/aeronav/digital_products/dof/
- OpenCelliD — https://opencellid.org
- Kruitwagen et al., "A global inventory of photovoltaic solar energy generating units", Nature 2021 — https://doi.org/10.1038/s41586-021-03957-7
- US Wind Turbine Database — https://eerscmap.usgs.gov/uswtdb/
- OurAirports data — https://ourairports.com/data/
- ICAO Annex 14 (Aerodromes), obstacle limitation surfaces — https://www.icao.int (standard is paywalled)
- Mapillary — https://www.mapillary.com
- Galin et al., "Procedural generation of roads" (least-cost routing reused for lines), CGF 2010 — https://doi.org/10.1111/j.1467-8659.2009.01612.x
- Kiessling, Nefzger, Nolasco, Kaintzyk, *Overhead Power Lines: Planning, Design, Construction*, Springer 2003 (catenary and sag-tension reference) — https://doi.org/10.1007/978-3-642-97879-1
