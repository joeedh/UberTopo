# P4b — Road surfaces, pcurves and the road on terrain (plan v2)

- **Date:** 2026-11-18
- **Phase:** the rest of P4 in
  [06 §phasing](../design/core-data-model/06-modules-litestl-and-phasing.md), after
  [P4a](2026-11-18-p4a-curves.md). P4's remaining acceptance item: "a road imprinted on
  terrain sheets".
- **Inputs:**
  - [05 Geometry pools](../design/core-data-model/05-geometry-pools.md) §1 (attachment
    points), §4 (initial kinds);
  - [07 §16](../design/core-data-model/07-topology-api.md#16-curves-and-the-geometry-store-p4a)
    and DEC-018 (what P4a built);
  - [surfaces-from-curves](../research/curves/surfaces-from-curves.md) §1.2 (road
    surfaces in (s, t)), §2 (terrain interaction), §3 (pcurves);
  - [road-alignment-standards](../research/curves/road-alignment-standards.md)
    (crossfall and superelevation);
  - DEC-014 (projected faces and the canonical CDT).
- **Status:** v2, after a fresh-context review of v1 (§11). The owner confirmed the
  decisions marked **(owner)** on 2026-11-19 (§11).

## 1. Split: P4b now, P4c later

P4a's plan listed seven items for "P4b". Together they are at least two phases, so
this plan takes the ones the acceptance case needs and moves the rest to P4c.

**P4b (this plan): one surface kind, pcurves, and a road on a terrain sheet.**
- `road3`: a ruled road surface in road coordinates (s, t) (§3).
- Two pcurve kinds in a surface's parameter space: `graph2` (t = f(s)) and
  `segment2` (§3.3).
- `.f.surface` and `.c.pcurve` in use: accept rules, validation, the editor, hashes
  and I/O (§4, §5). UTCX format 4 (§2).
- **The refined skeleton.** Curved edges contribute their certified flattening to the
  geometric tests that now use straight chords (§4.3). This closes the skeleton half of
  OI-P4a-1.
- **The acceptance case.** A road corridor placed into a terrain sheet, built, edited,
  undone, frozen and round-tripped in `double` and `Lattice` (§6).

**P4c (later):**
- Surfaces: an explicit `Plane` (its exact form is a `Coord`-dependent pool, 05 §5),
  `Heightfield`, `Sweep`, `BSplineSurface`.
- The in-plane check of a planar face's curved carriers (the other half of OI-P4a-1;
  OI-P4b-7).
- Queries: inversion (point → (s, t)), closest point, curve–curve and curve–surface
  intersection for every kind.
- Daylighting: cut and fill slopes found by root finding against terrain (research §2).
- Junction surfaces (CDT or Coons patches).
- Bertolazzi–Frego G1 fitting, non-polynomial κ laws (OI-P4a-3), septic (G3)
  approximation.
- General topology edits on surfaced faces (OI-P4b-2).
- The "pcurve primary" bit of `.e.geom_flags`, which nothing on the acceptance path
  reads. It stays reserved (must be 0).
- Automatic collect (OI-P4a-2) and `detmath` speed (OI-P4a-4).

**Why Plane and Heightfield move out.** Neither is on the acceptance path. Terrain is
already modelled as a sheet of projected faces (DEC-014), whose geometry is the
canonical CDT of the face. An exact `Plane` for `Lattice` needs integer coefficients
in a pool templated on `Coord`, which is a design question of its own.

## 2. Decisions (proposed DEC-019)

**The acceptance case (owner).** "A road imprinted on terrain sheets" is met by
*opening* the terrain sheet around the road and *stitching* the road into it:
- the terrain cells that a buffer around the corridor overlaps are removed (§6);
- the road's lane faces carry a `road3` surface and pcurves;
- one projected **fill face** joins them: its outer loop is the terrain hole's
  boundary, its hole loop is the road's outline. Its geometry is the CDT of its refined
  skeleton, a crude embankment.
- **Why not literal imprinting.** Imprinting the road's outer edges into terrain faces
  needs a vertex where each edge crosses a terrain edge. That vertex must lie both on
  the straight terrain edge (at terrain height) and on the road border (at road
  height), which only holds if the road follows the terrain exactly. Real corridors
  don't: they cut and fill. The fill face is the honest P4b version of that zone;
  daylight lines found by slope are P4c.

**The road surface:**
- **t is a horizontal offset (owner).** S(s, t) = r(s) + t·N(s) + Z(s, t)·e_z, with
  r and N the reference line's point and left normal.
  - A border t = b(s) then has the xy footprint `offset2(reference, b)` exactly, so
    every lane edge is a P4a kind with P4a's certified bounds.
  - OpenDRIVE instead rotates t about the reference line by the superelevation angle
    and measures widths along the tilted axis. At 6 % crossfall the two differ by a
    factor cos φ ≈ 0.998 in width. Importers convert.
- **The surface is ruled between borders.** Z(s, t) interpolates linearly in t between
  border heights Z_k(s). Crossfall, crown and superelevation are expressed through
  the Z_k, so the surface stores no slopes (§3.2).
- **Border carriers are P4a kinds.** Border k's edges carry
  `alignment3(offset2(reference, b_k), Z_k)`, or `alignment3(reference, Z_k)` when
  b_k ≡ 0. §3.2 pins the evaluation so that the border lies on the surface bit for bit.

**Pcurves:**
- **Same parameter.** A coedge's pcurve is parameterised by its edge's parameter, as
  OCCT's `SameParameter`/`SameRange`:
  - a curved edge: its carrier parameter over `.e.range`. Its pcurve is a `graph2`;
  - a straight edge: λ ∈ [0, 1] from `vs[0]` to `vs[1]` (straight edges have no
    parameter today; this defines one). Its pcurve is a `segment2`.

  So `split_edge_at` and `join_edge` keep pcurve refs unchanged on curved edges, and
  one `graph2` entry serves every edge of a border. No pcurve range column is needed.
- **Required on surfaced faces.** Every coedge of a face with a surface has a pcurve,
  and no coedge of a face without one does.

**Validation is certified, not exact.** P2's embedding checks stay exact on stored
and skeleton points. The new checks compare points within tolerance, like P4a's
endpoint rule:
- a vertex of a surfaced face lies on S(γ(t)) at each incident coedge's end;
- an edge lies on its face's surface along its whole range: structurally for road
  borders, by a second-order certified bound otherwise (§4.2);
- a face lies within one link of its road surface;
- the face's `Reversed` flag matches the surface normal.

**Surfaced faces may be planar or projected.** The surface is the face's geometry.
The skeleton classification (planar, or projected along an axis) only drives the
topological tests. A lane on a tangent with constant grade and crossfall has four
coplanar corners in exact arithmetic, and after rounding it may classify either way;
both are valid.

**The refined skeleton (§4.3):**
- A curved edge contributes its deterministic flattening at `.g.skeleton_tol` to the
  tests that read face loop points: projection and T(f), radial order, contacts,
  wire–face tests, containers, regions, face boxes and the editor's grid.
- **Planarity is still decided on stored vertices only.** A planar face's curved
  carrier is not yet checked against its plane (OI-P4b-7).
- Flattening points are rounded to `C` and numbered after the stored vertices in one
  table per validation pass. They are derived and never stored or hashed.

**Format and hashes:**
- **UTCX format 4.** New kinds are appended to `kinds()` (existing pool ids are
  unchanged), and `.g.skeleton_tol` joins the global schema.
  - Both changes break format-3 reading: the complex's stored hash covers the
    store's pool count, and the global group's builtin count changes.
  - **Format 3 is read through a migration**, as `io_legacy.cc` does for formats 1–2:
    - a legacy global schema without `skeleton_tol`, moved by `migrate_from`;
    - the stored hash checked by a format-3 hash that covers only the first n pools.
      litestl's `PoolStore::hash_into` gains a pool-count limit for this;
    - a format-3 fixture captured in W0, before any change.
- **Goldens.** Content hashes change through the pool count and the new global. The
  random test also changes behaviourally (§5.3). Both re-pins happen in W1, where the
  kinds are registered, with the P4a proof: structure hashes are unchanged by the
  registration alone.

## 3. The curves library

### 3.1 `Fn1D::mul`

- **What:** the product of two piecewise polynomials with merged breakpoints, degree
  ≤ `kMaxDegree` (refused above). Coefficients are shifted and multiplied with
  rounding, like `add`; the result is a new exact definition near the true product.
- **Why:** the road builder (§3.4) computes border heights Z_k = z + Σ c_j·w_j where a
  varying crossfall slope overlaps a varying width (the widening inside the
  superelevation transition).

### 3.2 `road3`: the ruled road surface (`curves/road.h`)

**Definition:**
- `reference`: a live `cesaro2` (the reference line r, N, κ);
- `borders`: m + 1 ≥ 2 `Fn1D` b_0 < b_1 < … < b_m on a common domain [s0, s1];
- `heights`: m + 1 `Fn1D` Z_0 … Z_m on the same domain.

**Evaluation** on link k (b_k ≤ t ≤ b_{k+1}), with w = (t − b_k)/(b_{k+1} − b_k):

S(s, t) = r(s) + t·N(s) + ((1 − w)·Z_k(s) + w·Z_{k+1}(s))·e_z

- **Side selection.** In t, a `Side` picks the link on a border. In s, a `Side` picks
  the piece at a break, as for curves.
- **Partials** up to second order (S_s, S_t, S_ss, S_st, S_tt). They come from r's and
  N's derivatives (via κ) and the Fn1D derivatives.
- **The normal** S_s × S_t points up for a well-formed link. `Reversed` on the face
  records the opposite.

**Bitwise agreement with border carriers.** These three rules make
S(s, b_k(s)) equal the border carrier's point bit for bit:
1. road3's xy part evaluates `r + t·N` with the same expression and operation order as
   `Offset2View` (offset.cc), with t = b_k(s) evaluated by the same `Fn1D::eval`;
2. z uses the convex form (1 − w)·Z_k + w·Z_{k+1}, which is exact at w = 0 and at
   w = 1 (the form Z_k + w·ΔZ is not exact at w = 1);
3. both evaluations pass the same `Side` at Fn1D breaks.

A test compares the two bit for bit at every station and break of the acceptance road.

**`make` refuses:**
- borders not provably ordered (b_{k+1} − b_k > 0 by `Fn1D::range`);
- domains that differ or don't lie inside the reference's;
- folds: 1 − t·κ(s) > 0 must hold for t = b_0 and t = b_m, by P4a's `offset_valid`
  enclosure;
- non-finite data.

**Certification** (the P4a contract, with the per-term white-box test from the start):
- `eval_error`: the reference's bounds for r and N, the Fn1D evaluation errors of b_k,
  b_{k+1}, Z_k and Z_{k+1}, the products t·N, the division for w and the
  interpolation;
- `deriv_bound(box, (i, j))`: a bound on |∂^{i+j}S/∂s^i∂t^j| over a parameter box.
  Link k's formula is extended over the whole box, so a box on a border (a pcurve that
  lies on b_k) is covered. The bound uses `Fn1D::range` enclosures and the reference's
  `deriv_bound`;
- the special parameters in s: the union of the reference's breaks and every border's
  and height's breaks.

**The oracle.** Quad-precision evaluation of the same formula (`tools/curve_oracle/`),
on fixtures: a straight crowned road, a superelevated curve with transitions, a
widening lane, and a border that crosses the reference line.

### 3.3 Pcurve kinds (`curves/pcurve.h`)

| Kind | Definition | γ(λ) | Parameter | Used on |
|---|---|---|---|---|
| `graph2` | `Fn1D f` | (λ, f(λ)) | station s, over f's domain | curved edges |
| `segment2` | points p0, p1 | p0 + λ(p1 − p0) | λ ∈ [0, 1] | straight edges |

- Both are `Curve2` kinds with certified bounds, so P4a's generic flattening and boxes
  apply to them.
- They are only accepted in `.c.pcurve` (§4.1), never as edge carriers.

### 3.4 The road builder (`curves/road_build.h`)

A helper for generators. It is not a kind.
- **Input:** a reference `cesaro2` ref, a centre profile z(s), and a cross-section:
  link widths w_j(s) outward from a pivot border at t = 0, and crossfall slopes
  c_j(s), all as `Fn1D`.
- **Output:** the borders b_k (sums of widths), the heights Z_k = z + Σ c_j·w_j (via
  `add` and `mul`), the interned `road3`, and for each border its `offset2`, its
  `alignment3` carrier and its `graph2` pcurve.
- **Superelevation.** A transition changes the outer slopes linearly over a station
  range (`Fn1D::linear`), for example from −2 % crown to +6 %.

## 4. The complex

### 4.1 Accept rules and columns

- **`.f.surface`** takes `road3`. The face flag `Reversed` gives its orientation.
- **`.c.pcurve`** takes `graph2` on a curved edge's coedge and `segment2` on a straight
  edge's, and only where the coedge's face has a surface.
- **Inner refs, explicitly:** `road3` names a `cesaro2`; `graph2` and `segment2` name
  nothing. No other kind may name a `road3`, `graph2` or `segment2`. (Today's `Inner`
  default accepts anything, geom_store.cc:45; W1 adds explicit branches.)
- **`.e.geom_flags`** must still be 0.
- **`.g.skeleton_tol`**: a Topo global, `double`, default 2⁻⁶ frame units (§4.3,
  OI-P4b-5). It is a geometry column for the coordinate-free structure hash.
- **Roots.** `GeomRoots` already visits every `Geom<>` column. `JournalRoots` gains
  the two new op payloads (§5.1).
- **Relaxed P4a checks.** `validate_curves.cc` reports `GeometryUnsupported` for any
  pcurve or surface today (lines 30–37, 60–63). W3 narrows it to non-zero
  `.e.geom_flags`.

### 4.2 Validation (`validate_surfaces.cc`, after `validate_curves`)

New codes:
- `SurfaceKind`: a ref that isn't a live accepted surface;
- `PcurveKind`: a pcurve of the wrong kind for its edge, a pcurve on a coedge of an
  unsurfaced face, or a missing pcurve on a surfaced face;
- `PcurveRange`: the pcurve's domain doesn't cover the edge's parameter range;
- `PcurveVertex`: |S(γ(t_end)) − p| exceeds the tolerance at either end of a coedge;
- `PcurveDeviation`: the edge's 3D geometry leaves S(γ(t)) by more than the tolerance,
  or the bound is inconclusive within the subdivision cap (the detail string says
  which);
- `SurfaceLink`: no single link contains all of the face's pcurves;
- `SurfaceOrientation`: `Reversed` disagrees with the sign of (S_s × S_t)·n_front,
  where n_front is the skeleton's front normal at an interior point of the face.

**Tolerance** (as P4a's endpoint rule): the carrier's `eval_error` + the surface's
`eval_error` + |∇S|·(pcurve `eval_error`) + √3·rounding_C(p) + `curve_tol` + the
rounding of the distance computation.

**`PcurveDeviation`, in two passes:**
1. **Structural.** The edge passes with no sampling when all of these hold:
   - its carrier is `alignment3(offset2(R, f), z)`, or `alignment3(R, z)` when the
     border is ≡ 0 (P4a collapses a zero offset);
   - R is the surface's reference;
   - f is bitwise one of the surface's borders b_k and equal to the `graph2`'s f;
   - z is bitwise Z_k.

   D is then pure rounding (§3.2 makes it zero).
2. **General: a second-order bound.** D(t) = C(t) − S(γ(t)), with C the carrier or
   the straight segment.
   - On a subrange [a, b] with no special parameter inside:
     max |D| ≤ max(|D(a)|, |D(b)|) + (b − a)²/8 · M2, with
     M2 ≥ |C″| + |(S∘γ)″|. That is the Peano bound P4a's approximation uses
     (approx.h). (S∘γ)″ = S_ss·s′² + 2·S_st·s′t′ + S_tt·t′² + S_s·s″ + S_t·t″,
     bounded from `deriv_bound` over the subrange's box.
   - A first-order (Lipschitz) bound can't work here: it charges L ≈ 2 per unit length
     even though D ≡ 0 in exact arithmetic, so a 50 m edge would need ~10¹⁰ subranges
     at `curve_tol`.
   - Bisect until the bound holds, up to 2¹² subranges per edge. A straight cross edge
     at constant s within one link has (S∘γ)″ = 0, so it passes on its first range.

**`SurfaceLink`.** The face's link is the unique k for which every pcurve's t lies in
[b_k(s) − ε, b_{k+1}(s) + ε], with ε the tolerance divided by |S_t|:
- a `graph2` whose f is bitwise b_k or b_{k+1} is in link k structurally;
- a `segment2`'s two end parameters are tested pointwise (it is straight in (s, t),
  and the link's bounds are linear in t at fixed s);
- any other pcurve uses `Fn1D::range` enclosures of b over its s-range.

Zero candidates, or more than one, is `SurfaceLink`.

### 4.3 The refined skeleton (`topo/face_skeleton.h`)

**Consumers** (the full inventory, with line numbers, is §12):
- `radial.h`: `FacePoints::gather`, `face_frame`, `coedge_half_plane`,
  `radial_order` (which sorts about the chord v0→v1) and
  `projected_third_vertices` (which returns `VertRef`, so it cannot name a
  flattening point today);
- `face_geom.h`: `classify_face` and the projected classification;
- `validate_geom.cc`: projection, T(f) for radial order and containers, contacts,
  wires against faces;
- `validate_detail.h`: `face_fan`, the segment–triangle test with `SharedV`/`SharedE`,
  `embedded_wire_ok`, the loop checks;
- `validate_grid.cc`, `validate_local.cc`;
- `regions.cc`: signed volumes and ray winding;
- `face_grid.cc` and the editor's grid: face boxes and T(f) entries;
- the editor: `make_face`, `split_face` and `join_faces` (editor_impl_face.h, including
  the `face_area` propagation weights), `resort_radial` and `fix_projection`
  (editor_impl_cycles.h), `split_impl` (its planarity check and resort),
  `split_edge_at` (editor_impl_curves.h:111–117), `imprint` and `unimprint`.

**`FaceSkeleton`, specified:**
- **Flattening.** Each curved edge is flattened once, in edge order over
  (range[0], range[1]), by `flatten(carrier, t0, t1, skeleton_tol)`. A coedge of
  opposite sense uses the same points reversed. Adaptive flattening is not symmetric,
  so flattening in coedge order would give two faces different points.
- **Rounding.** Points are rounded to `C`. A point equal to its predecessor (or to the
  edge's end vertex) is dropped.
- **Numbering.** Stored vertices keep their indices. Flattening points are numbered
  from the vertex count in one table per validation pass (or per editor operation),
  keyed by (edge, k).
- **Sharing.** Two faces sharing a curved edge share its points *and its segments*.
  `SharedV` and `SharedE` accept skeleton points and segments, so contact tests treat
  them as shared.
- **Radial order** about a curved edge is computed about its first skeleton segment
  from `vs[0]`. Third vertices may be skeleton points: `projected_third_vertices`
  returns skeleton indices.
- **Degenerate skeletons.** A loop whose rounded skeleton touches itself, or a curved
  edge whose skeleton crosses another edge of the same face, is reported as a new
  code, `SkeletonDegenerate`.
- **Deterministic:** a function of the carrier entry, the range and the tolerance.
- **A complex with no curved edges** yields exactly today's point lists.

**What the skeleton is.** Exact predicates on skeleton points decide projection,
T(f), radial order, contacts and regions. The true carrier lies within
skeleton_tol + rounding of the skeleton, so faces closer than that are judged on the
skeleton (OI-P4b-1).

## 5. Editing

### 5.1 New primitives

- `intern_curve` is generalised to `intern_geom(kind, def, &ref)`, with the same
  checks for surface kinds. `intern_curve` stays as an alias.
- `set_face_surface(f, ref, reversed)`. NONE clears the surface, the face's pcurves and
  `Reversed`. The rules are checked at commit by validation.
- `set_coedge_pcurve(c, ref)`.
- Both are journaled (`OpKind::SetFaceSurface`, `OpKind::SetCoedgePcurve`), rooted by
  `JournalRoots`, and redone through `apply_op`.

### 5.2 Existing primitives on surfaced faces

| Primitive | Rule |
|---|---|
| `split_edge_at` on a curved edge | Pcurve refs are copied explicitly to the new coedges in `split_impl`: `.c.pcurve` is Topo, so the attribute mixer never touches it. The new vertex goes at round_C(c(t)) |
| `join_edge` on a curved edge | P4a's rules plus equal pcurve refs on matching coedges |
| `make_face`, `kill_face` | Allowed. `make_face` classifies on the refined skeleton |
| `flip_face` | Already toggles `Reversed` with the sides (editor_impl_face.h:511) |
| `set_position` | Allowed. Validation catches a vertex off the surface |
| `split_edge` and `join_edge` on straight edges, `split_face`, `join_faces`, `imprint`, `unimprint`, `add_hole`, `remove_hole`, `weld` | Refused with the new code `SurfacedFace` when a touched face has a surface (OI-P4b-2). A kept `segment2` would be wrong after the λ reparameterisation |

**Barriers.** `compact` and `rebuild_regions` keep surfaces and pcurves (both are
plain columns). The collect that runs after a barrier keeps every entry these columns
reach.

### 5.3 Goldens

- **`test_edit_random` changes behaviourally.** Its arc step puts xy arcs with linear z
  on arbitrary edges (test_edit_random.cc:396–406). On tilted faces the refined
  skeleton changes the outcome of later steps. The step is constrained to wires and to
  edges whose faces are all horizontal (where an xy arc stays in the plane), and the
  goldens are re-pinned in W2 with that reason.
- **New goldens:** the acceptance case's content and structure hashes, in `double` and
  `Lattice`.

## 6. The acceptance case (`tests/gen/road_terrain.h`, `test_road_terrain.cc`)

**Coordinates.** Everything the generator decides is decided in coordinate-free
values, so `double` and `Lattice` build the same topology:
- `Lattice` uses a quantum of 2⁻¹⁰ m (about 1 mm);
- terrain heights are multiples of 2⁻⁴ m, so terrain points are exact in both;
- `skeleton_tol` is 2⁻⁶ m in both: 2⁻⁶ in `double`, 16 quanta in `Lattice`;
- the coordinate-free structure hash leaves out `skeleton_tol` (a geometry column) and
  the `ProjAxis` bits of `.f.flags` (which can differ after rounding, §2).

**The terrain:**
- A 2 km × 2 km sheet of 40 × 40 grid cells. Heights come from a fixed integer hash of
  the grid point, up to ±15 m.
- Cells are quads, projected along z. The complex allows projectable faces.

**The road:**
- **Reference line:** 1.2 km of line, clothoid, 300 m arc, clothoid, line. It starts
  and ends at least 200 m inside the tile.
- **Profile:** a crest and a sag vertical curve, running up to 4 m above and below the
  terrain.
- **Cross-section:** two 3.5 m lanes and two 2 m shoulders (five borders). There is a
  −2 % crown on the straights, rotating to +6 % superelevation through the curve over
  the clothoids. A 30 m widening ramps the outer lane to 4.5 m inside the first
  transition, so a varying width meets a varying slope (§3.1).

**Stations.** Every 50 m, plus the breaks of the reference and of every border and
height function. A break closer than 1 m to a 50 m station replaces it, so no two
stations are closer than 1 m.

**The kill set**, decided in `double` from the definition alone:
1. **Buffer polygon.** Flatten `offset2(ref, b_0 − 10)` and `offset2(ref, b_m + 10)`
   at 0.1 m. Close them with straight end caps extended 10 m along the end tangents.
2. **Cells.** A cell is killed if its square meets the buffer polygon, by `orient2d`
   on the doubles.
3. **Close the set to a disc.** Add any kept cell enclosed by killed cells. Add a cell
   wherever two killed cells touch only at a corner (a pinch vertex, which
   `make_face` refuses). Repeat until stable.
4. **Assert:** the set is 4-connected, simply connected and doesn't touch the tile
   border.

**One editor transaction (`place_road`):**
1. Intern the reference, the `road3` and the border carriers and pcurves
   (`road_build.h`).
2. Kill the cells, then the edges and vertices only they used, interior first.
3. Make the road vertices round_C(S(s_i, b_k(s_i))), the curved border edges, the
   straight cross edges, and one face per link and station interval. Set surfaces and
   pcurves.
4. Make the fill face: outer loop = the hole's boundary, inner loop = the road's
   outline.
5. Commit. This validates the touched rows.

**Checks:**
- `validate(Global)` is clean, regions rebuild unchanged, and the terrain stays one
  sheet shell.
- **Watertight by sharing:** every road border edge has radial degree 2 (two road
  faces, or a road face and the fill face).
- **Bitwise borders:** S(s, b_k(s)) equals each border carrier's point at every station
  and break (§3.2).
- **Surface evaluation:** dense sampling (10⁴ points per face) of road3 against the
  quad oracle surface agrees within the certified bound. This tests evaluation, not
  the B-rep.
- **Undo** restores the terrain bit for bit (content hash); **redo** reproduces the
  road (`verify_redo`).
- **Edits:** split a border edge at t with `split_edge_at`; check that the pcurves are
  copied and that validation passes; join it back; undo both and compare content
  hashes. (A split and a join don't return the same hash, since the split derives new
  IDs.) `split_face` on a road face is refused with `SurfacedFace`.
- **Freeze and I/O:** freeze, write, read and compare hashes. The P4a format-3 fixture
  still reads.
- **double vs Lattice:** equal coordinate-free structure hashes; pinned content hashes
  for each.
- **Skeleton:** the fill face's T(f) includes the border flattening points. A
  deliberately crossing variant (the outline pushed into a kept terrain cell) reports
  `FaceIntersection`.

## 7. Tests (library and complex)

- **`test_curves_road`:** oracle for S and partials; certified bounds; a per-term
  white-box hook (as `test_piece_terms`); refusals (unordered borders, fold, domain
  mismatch); finiteness checked before any comparison.
- **`test_curves_pcurve`:** `graph2` and `segment2` against the oracle; `Fn1D::mul`
  against exact rational products.
- **`test_face_skeleton`:**
  - a complex with no curved edges gives today's lists;
  - two faces share an edge's points and segments, with the same indices;
  - opposite-sense coedges give reversed identical points;
  - deterministic under insertion order;
  - radial order about a curved edge of degree 3;
  - `SkeletonDegenerate` on a self-touching rounded loop.
- **`test_validate_surfaces`:** one fixture per new code, each made by corrupting a
  valid road face; the structural pass rejects a `graph2` whose f is not a border.
- **`test_edit_surfaces`:** the primitives of §5.1 and the rules of §5.2, with undo,
  redo and barriers.
- **`test_road_terrain`:** §6.
- **`test_topo_legacy`:** the format-3 fixture reads, and its hash checks.

## 8. Work breakdown

| W | Content |
|---|---|
| W0 | Finish the skeleton consumer inventory (§12). Pin current goldens. Capture a P4a format-3 fixture |
| W1 | Register `graph2`, `segment2` and `road3` together (stub evaluators refuse); explicit accept branches; `.g.skeleton_tol`; UTCX format 4 with the format-3 migration and litestl's pool-limited hash; goldens re-pinned (registration only) |
| W2 | `FaceSkeleton` and its consumers; random arc step constrained; behavioural re-pin |
| W3 | `Fn1D::mul`; `graph2`, `segment2`; `road3` evaluation and certification; oracle; `road_build.h` |
| W4 | `validate_surfaces` (structural and second-order passes); relax `GeometryUnsupported` |
| W5 | Editor primitives, journal, explicit pcurve copy, refusals |
| W6 | `road_terrain` generator and the acceptance tests |
| W7 | Benchmark, §9 mutation run, full matrix, docs (DEC-019, 07 §17, 05, 06, index) |

**Why the skeleton comes before the surface.** It is the riskiest change, it touches
code every earlier phase depends on, and it can be tested with P4a's curved edges
alone. Its golden churn then lands before any surface code exists.

**Benchmark (W7, not gated):** surface evaluations per second; `place_road` time;
`validate(Global)` time on the road case against the same terrain without the road;
skeleton point count.

## 9. Mutation checks

Each must be killed by at least one test:
1. `road3` interpolation weight uses b_{k+1} − b_{k−1};
2. `road3` drops t·N (heights only);
3. `road3` uses Z_k + w·ΔZ (loses bitwise agreement at w = 1);
4. `road3::make` skips the fold check;
5. `road3::make` skips the border-order check;
6. `road3` `eval_error` drops the Fn1D evaluation errors;
7. `road3` `deriv_bound` ignores the reference's κ term;
8. `graph2` evaluates (f(λ), λ);
9. `Fn1D::mul` drops cross terms of different pieces at a merged break;
10. the skeleton uses chords for curved edges (P4a behaviour);
11. the skeleton flattens in coedge order;
12. the skeleton numbers flattening points per face (not shared);
13. radial order about a curved edge uses the chord;
14. the second-order bound drops the (b − a)²/8·M2 term;
15. the structural pass accepts an f that is not one of the surface's borders;
16. `PcurveVertex` checks only one end;
17. `PcurveRange` check removed;
18. `SurfaceLink` accepts a pcurve in the neighbouring link;
19. `SurfaceOrientation` check removed;
20. accept rules let a `road3` or `graph2` into `.e.curve`;
21. `split_edge_at` doesn't copy pcurves;
22. `join_edge` skips the pcurve-equality check;
23. `JournalRoots` misses `SetFaceSurface` or `SetCoedgePcurve` (one mutant each);
24. the refusal of `split_face` on surfaced faces is removed;
25. the format-3 read hashes all pools (the fixture then fails to read);
26. the fill face is made without its hole loop (the road faces then overlap it).

## 10. Open issues

- **OI-P4b-1 — skeleton contacts.** Contacts and regions are decided on the refined
  skeleton, which is within skeleton_tol of the true geometry. Two faces closer than
  that are judged on the skeleton.
- **OI-P4b-2 — few edits on surfaced faces.** Face splits, joins, imprints, holes,
  welds and straight-edge splits are refused there. P4c gives them pcurve rules.
- **OI-P4b-3 — horizontal t.** Lane widths are horizontal, not along the tilted
  surface as in OpenDRIVE.
- **OI-P4b-4 — the fill face is not an embankment design.** Its shape is the CDT of
  its boundary. Daylight lines by slope are P4c.
- **OI-P4b-5 — skeleton_tol units.** It is in frame units, which are quanta for
  `Lattice`. The default of 2⁻⁶ suits `double` in metres. For `Lattice` it is far
  below the rounding and makes very dense skeletons, so generators must set it.
- **OI-P4b-6 — no inversion queries.** Nothing maps a point to (s, t) yet. The
  acceptance case only builds forward.
- **OI-P4b-7 — planar faces' curved carriers aren't checked against the plane.** Only
  the stored vertices decide planarity, as in P4a.
- **OI-P4b-8 — `split_edge_at`'s re-sort of the second piece is untested** (W2
  mutation check). It needs three planar faces on an S-shaped carrier split
  exactly on its chord. Due with the W5 editor tests.

## 11. Review (fresh context, 2026-11-18, of v1)

A reviewer with no access to the planning discussion checked v1 against the code. Its
findings and the changes they caused:

**Must fix (all taken):**
1. **Format 3 would not read.** The complex's stored hash covers the pool count
   (io.cc:202, pool_store.cc:798), and a new global changes the builtin count
   (group.cc:466). → UTCX format 4 with a format-3 migration and a pool-limited hash
   (§2).
2. **The sampling bound was first-order** and could never pass at `curve_tol`.
   → a structural pass for road borders, plus the second-order Peano bound (§4.2).
3. **"Surfaced faces are projected" contradicted the code.** `make_face` refuses
   `ProjAxis` bits (editor_impl_face.h:85), and planarity is derived. → surfaced faces
   may be planar or projected (§2).
4. **The skeleton inventory missed the hardest consumers** (radial order, third
   vertices, `SharedV`/`SharedE`, editor resorts and weights), and the spec omitted
   edge-order flattening, shared segments, per-pass numbering and degenerate cases.
   → §4.3 rewritten; inventory finished in W0.
5. **`test_edit_random` would change behaviourally**, not just through the pool count.
   → arc step constrained, behavioural re-pin (§5.3).
6. **`SurfaceLink` was ill-defined** on borders. → unique-link rule with ε (§4.2).
7. **double vs Lattice structure hashes would differ** through `skeleton_tol`,
   rounded kill tests and `ProjAxis`. → coordinate-free decisions, dyadic terrain,
   masked bits (§6).
8. **The kill rule wasn't well defined.** → an explicit buffer polygon, closure to a
   disc, asserts (§6).

**Should fix (all taken):** explicit pcurve copy in `split_impl` and refusal of
straight-edge joins (§5.2); relaxing `GeometryUnsupported` and explicit accept
branches (§4.1); a `SurfaceOrientation` code (§4.2); pinned bitwise evaluation rules
and complete `eval_error` (§3.2); registering kinds in one step with the format change
(W1); comparing after undo, not after split and join (§6); stating the Lattice
skeleton tolerance (§6); minimum station spacing (§6); seven more mutants (§9).

**Nits taken:** the pcurve-primary bit is deferred; the surface sampling test is
described as an evaluation test; the planar in-plane check is deferred (OI-P4b-7).

**Not taken:** replacing `Fn1D::mul` with a scale operation. The acceptance road's
widening overlaps the superelevation transition, so both factors vary.

**Owner decisions (2026-11-19):**
- the acceptance case is hole and fill, not literal imprinting or draping;
- t is a horizontal offset;
- the P4c split of §1 is accepted (queries stay in P4c).

## 12. Skeleton consumer inventory (W0)

Found by searching `source/ubertopo/topo` for `PolyView`, `FacePoints`, `cdt_proj`,
`face_frame`, `coedge_half_plane`, `radial_order`, `projected_third_vertices`,
`face_fan`, `classify_face`, `face_points_planar`, `face_area`, `face_box` and
`embedded_wire_ok` (2026-11-19, at dbd5c25).

**The hub.** Most consumers read loop points through `FacePoints<C>::gather`
(radial.h:34), which appends each coedge's start vertex. W2 splits it in two:
- `gather` stays as it is: stored vertices only. **Planarity** uses it.
- `gather_skeleton(k, f, table)` expands curved edges (§4.3). It also fills a parallel
  `ids` array (a stored vertex's index, or vertex count + the table index of a
  flattening point) and the coedge each point belongs to.

**A. Planarity: stays on stored vertices (no change but a test):**
- `editor_impl_curves.h:113` (`split_edge_at` keeps incident planar faces planar);
- `editor_impl_vertex_edge.h:115` (`split_impl`);
- `editor_impl_cycles.h:228` (`fix_projection`);
- `editor_impl_face.h:364` (`join_faces`), `editor_impl_unimprint.h:100`;
- `editor_impl_imprint.h:206–207, 448` (degeneracy of the split loops).

**B. Projection, T(f) and frames: switch to `gather_skeleton`:**
- `face_geom.h:62` `classify_face`: planarity on the vertices, the CDT on the skeleton.
  Its callers build their own point lists: `builder.cc:357` (the builder makes no
  curved edges, so it passes vertices only), `editor_impl_face.h:163` (`make_face`),
  `editor_impl_imprint.h:191, 203` (the two sides of a split).
- `radial.h:67` `face_frame(k, f)` and `radial.h:104` `face_triangles`;
- `regions.cc:169` (T(f) for volumes and rays);
- `validate_detail.h:61` `face_fan`, `:213` loop checks and T(f), `:293`
  `embedded_wire_ok`;
- `validate_geom.cc:57` (projection and planarity checks: planarity on vertices, the
  CDT on the skeleton) and `:164` (container fans);
- `face_grid.cc:129` (T(f) entries of large faces);
- `editor_impl_face.h:34` `face_area` (propagation weights; the skeleton makes a
  curved face's area right to within skeleton_tol);
- `editor_impl_imprint.h:153, 432, 782` (loop points of the face being imprinted).

**C. Index users: need skeleton ids, not `VertRef`:**
- `radial.h:120` `projected_third_vertices` returns (coedge, `VertRef`) pairs. It
  becomes (coedge, skeleton id), and for a curved edge the triangle is the one on its
  first skeleton segment.
- `radial.h:79` `coedge_half_plane` walks the loop's stored vertices for an off-line
  point. For a curved coedge it uses the first skeleton segment and walks skeleton
  points.
- `radial.h:153` `radial_order` sorts about the chord v0→v1. For a curved edge: about
  its first skeleton segment from `vs[0]`. Callers: `validate_geom.cc:99–125`,
  `builder.cc:624, 689`, `editor_impl_cycles.h:38–59` (`resort_radial`).
- `validate_geom.cc:238` and `validate_detail.h:130–133`: T(f) triangles in vertex
  indices and the `SharedV`/`SharedE` contact sets. They switch to skeleton ids, with
  shared segments of a curved edge added to `SharedE`.

**D. Boxes:**
- `face_grid.cc:16` `face_box` reads vertex positions. A curved edge adds its
  skeleton points' box, grown by skeleton_tol + rounding, so the box still covers
  the true carrier.

**E. Wires** (`validate_geom.cc:490`, `validate_grid.cc:250`): a curved wire tests
each skeleton segment against the face's T(f). Its endpoints are unchanged.

**The table.** One `SkeletonTable` per validation pass or editor operation: a map from
(edge, k) to an id, filled on first use, so every face that names an edge sees the
same ids.

## 13. Implementation notes (changes made while building)

**W1:**
- **`graph2` and `segment2` were implemented in full**, not as stubs: they are small,
  and their tests (`test_curves_pcurve`) come with them. `road3` has its definition,
  pool protocol and structural checks; its evaluator, order and fold checks are W3
  (the kind's `make_surface` returns null until then).
- **Surface interface.** `curves/surface.h`: `SurfaceEval` and `SurfaceKindOps`, a
  `KindOps` whose `as_surface()` returns itself and whose `dim()` is 0. No RTTI.
- **The topology hash leaves out `.g.skeleton_tol`.** It is in frame units, so a
  `double` and a `Lattice` build of one model would otherwise differ there (as they
  would on positions). The DEC-014 topology golden still changed, because the
  group's filtered hash covers its builtin count; it was re-pinned with its
  structure golden unchanged, as in P4a W5.
- **Goldens re-pinned:** `test_edit_random` content (structure goldens unchanged) and
  the DEC-014 topology golden.
- **litestl:** `PoolStore::hash_into(h, map, pool_limit)` returns false while a pool
  past the limit has a reachable entry. Tested in `test_attr_pool`.
- **Mutation check:** 16 W1 mutants, all killed (the pool-limit guard only after
  `test_attr_pool` gained its test).


**W2:**
- **`Skeleton<C>`** (`topo/face_skeleton.h`) is the §12 table. `FacePoints::gather(k,
  f, sk)` expands curved edges and fills `ids` (skeleton ids) and `owner` (the
  coedge of each point); `gather(k, f)` stays vertices-only. `warm()` flattens
  every curved edge up front, so `build_regions`' parallel loop only reads the
  table.
- **Planarity rule.** `face_planar_rule` (face_geom.h): the stored vertices decide,
  unless they are degenerate (a loop of arcs can have two vertices), and then the
  skeleton decides. The editor (`classify_face`, `fix_projection`, `split_edge_at`,
  imprint's children) and the validator share it.
- **`coedge_half_plane` walks skeleton points** after the axis (as §12 C says).
  Without curved edges this is the old walk. For a planar face whose arc leaves
  the face's plane (OI-P4b-7), the first off-line point can be off the plane; such
  faces are rejected once OI-P4b-7 is closed.
- **`set_edge_curve` on a face edge** touches the edge's coedges, so their faces
  join the commit filter. It also re-sorts the radial order and marks the regions
  dirty. `split_edge_at` re-sorts both pieces. Before W2, the faces around a newly
  curved edge were never re-validated.
- **`SkeletonDegenerate`** is reported in two cases. The first is a loop meeting
  that involves a segment with a flattening point at either end. The second is a
  curved edge that doesn't flatten at `.g.skeleton_tol`: before W2 such an edge
  was silently read as straight.
- **Face grid.** Records of faces and wires with curved edges hold skeleton ids
  from the table of the build that made them. `Rec::curved` marks them. Their
  triangles are read by index from the current pass's T(f), since the canonical
  CDT gives the same order. Face boxes are grown by 2·skeleton_tol (+1 quantum for
  `Lattice`).
- **Contact sets** take skeleton ids: `skel_point_on_face` and
  `skel_segment_on_face` (validate_detail.h). A flattening point is on f when its
  edge bounds f. A segment is on f when it is consecutive on a curved edge that
  bounds f, or is a straight edge of f.
- **Goldens.** `test_edit_random` was re-pinned behaviourally (§5.3). Across its
  runs, 45 arcs land on face edges and 72 on wires. The determinism goldens are
  unchanged. `test_edit_curves` now expects dirty regions after
  `set_edge_curve` on a face edge.
- **Fixture lesson** (debugging.md): fins on a curved edge with apexes at one
  height intersect exactly when the radial order would swap.
- **Imprint** reads the children of a split and an island cycle on their
  skeletons. A hole in a curved edge's bulge goes to the child owning the edge, and
  an inclusion in a cycle's bulge goes to the island.
- **Mutation check:** 17 W2 mutants, 15 killed (all by `test_face_skeleton`, some
  also by `test_edit_curves` and `test_edit_random`). Two survive:
  - *`set_edge_curve` without the explicit coedge touch* is equivalent. The
    resort that follows writes every coedge's radial links, which touches them
    anyway. The touch stays as a guard against setters that skip no-op writes.
  - *`split_edge_at` re-sorting only the first piece* is open (OI-P4b-8). Planar
    faces on a split curved edge must all contain the new vertex. With three or
    more faces, that is reachable only when the split point lies exactly on the
    chord, which needs an S-shaped carrier and `Lattice` rounding. The test
    belongs with the W5 editor tests.
  - The mutation runner now pushes file times forward on every write, after one
    batch reported a stale build as a survivor.
