# 05 — Topological invariants, Euler operators, validity, robust editing

Part of [B-rep topology research](README.md). Assumes the cell-complex structure
recommended in [04-surface-vs-volumetric.md](04-surface-vs-volumetric.md).

## 1. Euler–Poincaré: from manifold solids to non-manifold complexes

### 1.1 Manifold solids (Mäntylä)

For a manifold solid B-rep:

```
V − E + F − (L − F) − 2(S − G) = 0
```

Here V, E, F count vertices, edges, and faces. L is the number of loops (so L − F is the
number of inner loops, or "rings"), S is the number of shells, and G the total genus.
This is a **necessary, not sufficient** condition: a zero value does not guarantee a
valid solid.

### 1.2 Non-manifold cell complexes: derive it, don't memorize it

The cleanest way to get the right formula for *any* complex is through the **Euler
characteristic with compact support** of each open cell, χ_c. It is additive over a
partition into cells:

```
χ(X) = Σ_cells χ_c(cell)            and            χ(X) = b0 − b1 + b2 − b3
```

Here b_i are the Betti numbers of the whole complex X, and b3 = 0 for anything
embedded in R³ that is not all of space. The χ_c values are:

| Open cell | χ_c |
|---|---|
| Vertex | +1 |
| Open edge (arc) | −1 |
| Closed edge with no vertex (a circle) | 0 |
| Open face that is a disk | +1 |
| Open face with r holes (planar domain with r+1 boundary loops) | 1 − r |
| Open face, closed surface of genus g with no boundary (sphere g = 0, torus g = 1) | 2 − 2g |
| Open region that is a ball | −1 |
| Open region = interior of a compact 3-manifold M | −χ(M) = −½ χ(∂M) |
| …a solid torus (one through-hole) | 0 |
| …a ball with one cavity | −2 |

The last rows follow from χ(M) = ½ χ(∂M) for compact 3-manifolds, and
χ_c(int M) = χ(M) − χ(∂M).

Summing gives the **generalized Euler–Poincaré relation** for our structure:

```
V − E + Σ_faces (1 − r_f)   [+ corrections for closed faces / closed edges]
  − Σ_regions (1 − h_R + c_R)   =   b0 − b1 + b2
```

* r_f is the number of inner loops (rings) of face f.
* h_R is the number of through-holes ("handles", genus) of region R, and c_R the number
  of cavities (inner shells) of R.
* b0 is the number of connected components, b1 the number of independent "tunnels"
  through the complex, and b2 the number of enclosed voids of the complex.
* The sum runs over bounded cells only. The unbounded region and symbolic world-box
  faces are excluded. Equivalently, X is the closure of all bounded cells.

This matches the form usually attributed to **Masuda (1993)**:

```
v − e + (f − r) − (V − Vh + Vc) = C − Ch + Cc
```

Here r is rings, V volumes, Vh volume holes, Vc volume cavities, C complexes
(components), Ch complex holes, and Cc complex cavities. I could not fetch the
original paper, so the symbols are quoted from secondary descriptions. The derivation
above lets us check the formula independently, so we do not depend on the exact
published notation.

**Practical use:** count everything in O(n) and compare the two sides. The left side
uses the per-cell topology (loop counts, and shell counts per region). The right side
uses an independent computation (union-find for b0, plus b1 and b2 from a homology
computation on the cell graph, or simply trust it from construction). A mismatch
pinpoints corrupt topology. This is a **cheap sanity check in debug builds** after
every operator. It cannot replace geometric validity (§3).

### 1.3 Local invariants (cheaper and more useful than the global one)

* **Loop closure:** following `next` from any coedge returns to it. `prev(next(c)) == c`.
* **Radial closure:** following `radial_next` returns to the start. All coedges in the
  cycle share the same edge.
* **Edge-coedge consistency:** for a coedge c with sense s, the head vertex of c is
  the tail vertex of next(c).
* **Region consistency at an edge:** walking radially, the region on the "outgoing"
  side of face i equals the region on the "incoming" side of face i+1. This is the
  single most important non-manifold invariant. It ties face-side region labels to the
  radial order.
* **Shell closure:** a bounding shell of a region is closed. Every coedge of a
  face-side in the shell has a radial neighbour, on the appropriate side, that is in
  the same shell. Open sheets are allowed only where both sides belong to the same
  region (a dangling sheet).
* **Vertex link consistency:** the faces and edges around a vertex, grouped into
  "zones" (connected components of the vertex link), assign consistent regions. This is
  checked geometrically on demand with a sphere-map construction, CGAL Nef-style, and
  only at vertices flagged non-manifold.

## 2. Euler operators

### 2.1 Classical manifold set (Mäntylä, after Baumgart and Braid)

| Operator | Effect | Inverse |
|---|---|---|
| `mvfs` | Make vertex, face, shell (new solid seed) | `kvfs` |
| `mev` | Make edge + vertex (split a vertex) | `kev` |
| `mef` | Make edge + face (split a face) | `kef` |
| `kemr` | Kill edge, make ring (create a hole in a face) | `mekr` |
| `kfmrh` | Kill face, make ring + hole (genus) | `mfkrh` |

Five operator pairs span the space of manifold-solid topologies: Mäntylä proved the
set complete for manifold solids. They guarantee Euler-consistency by construction.

### 2.2 Non-manifold extensions

Masuda (1993), Yamaguchi–Kimura (1995), Gursoz–Choi–Prinz (1990), and Lee & Lee's
group all defined non-manifold operator sets. The extra operators cover:

* Make/kill **wire edges** and **isolated vertices** (acorn) inside regions.
* Make/kill **region** (close a shell to create a volume, or open it).
* Make/kill **shell**, including cavity shells.
* **Glue / unglue** faces (merge two coincident faces into one shared face between
  regions, or split one shared face back into two).
* Make/kill **edge-face incidences beyond 2**: attach a face to an existing edge in a
  given radial position.

I found no published completeness proof for a non-manifold Euler operator set that
covers all mixed-dimensional complexes. Treat completeness as something our operator
set must be *tested* for (§4), not assumed.

### 2.3 BMesh's practical set

Blender's `bmesh_core` names are a good modern equivalent. They are explicit and
low-level, and they compose:

* `split_edge_make_vert` / `join_edge_kill_vert`
* `split_face_make_edge` / `join_face_kill_edge`
* `join_vert_kill_edge`, `split_vert` / splice-style operators

### 2.4 Critique: what Euler operators are good for today

* **As a user or procedural API: legacy.** Nobody builds a city through `mev` and
  `mef`. Procedural generators produce bulk geometry (extrusions, sweeps, footprints,
  arrangements). The right construction path is **bulk build + arrangement**: build
  each object's cells, compute intersections, merge, and assign regions. It is
  parallel-friendly and much faster than sequences of Euler steps.
* **As the internal journaling vocabulary: essential.** A small, closed set of
  invertible primitive mutations, each recording what it created, killed, and split,
  gives three things at once:
  1. **Undo/redo** by replaying inverses. This is the role ACIS "bulletin boards"
     played.
  2. **Element lineage** (A was split into A′ and A″) for **persistent naming**
     ([06 §7](06-memory-layout-performance.md)).
  3. **Incremental invariant checking**: validate only the elements touched.
* **As a validity contract:** each primitive keeps the local invariants in §1.3. So a
  high-level operator composed from primitives can only break *geometric* validity,
  not combinatorial validity. That halves the debugging space.

Recommendation: implement ~12 primitive, invertible, journaled mutations (the five
classical pairs, generalized to non-manifold, plus glue/unglue, region make/kill, and
inclusion add/remove). High-level operators (Boolean, imprint, sweep, offset, LOD
refine) are built on them or are bulk builders that emit the same journal records.

## 3. Validity checking

Validity has three layers, with very different costs.

| Layer | Checks | Cost | When |
|---|---|---|---|
| **Combinatorial** | §1.3 local invariants; index ranges; no dangling references; generations match | O(n), trivially parallel | Debug: after every operator. Release: after import and at commit |
| **Embedding consistency** | Vertex lies on its edges' curves within tolerance. Edge curve lies on its coedges' face surfaces (pcurve ↔ 3D curve "same parameter" within tolerance). Loops are oriented consistently with the face normal. Tolerance nesting tol(V) ≥ tol(E) ≥ tol(F) | O(n) evaluations, parallel | After geometric edits, import, and healing |
| **Global geometric** | Loops are not self-intersecting and nest correctly in parameter space. Faces intersect only along shared edges or vertices (no interpenetration). Radial order matches geometry. Shell nesting matches region assignment | O(n log n) with a bounding volume hierarchy (BVH) and exact predicates. Expensive | After Booleans and arrangements, on import, and as a test oracle |

The last layer is the one classic kernels most often skip, and the cause of most "bad
B-rep" failures downstream. For simulation-grade correctness (BEM, FDTD
classification), **face-face non-interpenetration** matters. Make it cheap to run on
a *region of interest*, not only globally.

A good **oracle** for the global layer: re-derive the arrangement of all faces from
geometry alone (the mesh-arrangements approach) at coarse tessellation, and compare its
cell adjacency and region assignment to the stored topology.

## 4. Robust editing operators

### 4.1 Principles

1. **Separate combinatorial decisions from numeric evaluation.** Every branch
   that changes topology ("is this point inside this loop?", "which side of this face
   is that face around this edge?", "do these edges cross?") must use **exact or
   certified predicates** (Shewchuk adaptive predicates, or indirect predicates as in
   Attene's and Cherchi et al.'s exact mesh Booleans). Numeric evaluation (positions,
   curve fitting) can be approximate.
2. **Operators are transactions.** Begin → primitives (journaled) → validate the
   touched region → commit or roll back. This needs the journal from §2.4. Combined with
   persistent storage ([06](06-memory-layout-performance.md)), rollback is just
   "discard the new version".
3. **Arrangement-first Booleans ("merge then select").** Following SGC and Masuda and
   modern mesh arrangements (Zhou et al. 2016, EMBER 2022), compute the full
   arrangement of the operands *once*. Label cells with winding or membership vectors,
   then *select* to obtain union, difference, or "everything that is soil". Benefits:
   * Re-selection for a different operation is free. This suits procedural parameter
     tweaks.
   * All intersection curves are kept, which is useful for imprinting road edges on
     terrain.
   * Non-manifold and mixed-dimensional inputs are handled uniformly, with sheets as
     zero-thickness separators.
   * Region assignment comes naturally out of winding-number labelling.
4. **Tolerances are explicit and local** (Jackson 1995). Every merge, snap, or
   coincidence decision consults the tolerances of the participating entities and
   records any tolerance growth. Avoid a global epsilon.
5. **Degeneracy policy is declared per operator**: what happens with coplanar faces,
   coincident edges, or touching vertices. Use symbolic perturbation only where it
   cannot create topology the user did not intend. For architectural data, coplanarity
   is the *normal* case, not a degeneracy. So prefer exact coplanarity detection and
   merging over perturbation.

### 4.2 Operator catalogue (for the domain)

| Operator | Domain use | Notes |
|---|---|---|
| Imprint (curve or wire onto a face) | Road edges on terrain, lot lines, wall openings | Splits faces and creates new loops. Pcurves are needed |
| Glue (merge coincident faces) | Adjacent rooms or buildings from separate generators | Tolerance-based. Creates shared faces and fixes regions |
| Inflate face → region | Thin wall → thick wall (LOD) | Two faces plus a region. Lineage: face → (front, back) |
| Collapse region → face | Thick → thin (LOD down) | Inverse. Keeps the semantic ID |
| Split region by sheet | Simulation domain partition, virtual sheets | Adds a virtual face and updates shells |
| Fill hole (aperture) | Windows and doors | Adds a face on an existing loop. Radial insert |
| Sweep / extrude / loft | Walls from footprints, roads from centerline + cross-section | Bulk builders that emit journal records |
| Arrangement + select | Booleans, terrain cut-and-fill, building into terrain | See §4.1.3 |
| Heal / sew | Imported GIS, CityGML, IFC data | Tolerance-driven. Produces flagged "suspect" cells |

## 5. Testing strategy (suggested)

* **Property-based tests:** random sequences of primitive operators. After each one,
  check the combinatorial invariants and the Euler relation. Apply inverses and check
  for identity.
* **Differential tests:** compare region assignment against CGAL Nef_3 on small linear
  models (Nef is slow but exact).
* **Round-trip tests:** freeze ↔ edit layouts ([06](06-memory-layout-performance.md)).
  Export a manifold per shell and re-import.
* **Fuzzed import:** malformed CityGML, OSM, or IFC through the healing path, which must
  never crash and must flag what it cannot fix.

## References

* M. Mäntylä, *An Introduction to Solid Modeling*, 1988; Euler formula summary: https://pages.mtu.edu/~shene/COURSES/cs3621/NOTES/model/euler.html
* C. H. Séquin, "Generalized Euler-Poincaré theorem": https://people.eecs.berkeley.edu/~sequin/PAPERS/EulerRel.pdf
* H. Masuda, CAD 25(2), 1993 — https://portal.mardi4nfdi.de/wiki/Publication:1209190
* Y. Yamaguchi, F. Kimura, IEEE CG&A 15(1), 1995, doi:10.1109/38.364963
* E. L. Gursoz, Y. Choi, F. B. Prinz, Boolean set operations on non-manifold B-rep objects, CAD 23(1), 1991.
* Gomes et al., non-manifold operators extending Euler-Masuda operators, SMA 2004 — https://diglib7.eg.org/bitstream/handle/10.2312/sm20041408/315-320_gomes.pdf
* Euler characteristic / Betti numbers: https://en.wikipedia.org/wiki/Euler_characteristic ; https://en.wikipedia.org/wiki/Betti_number
* J. R. Shewchuk, robust predicates — https://www.cs.cmu.edu/~quake/robust.html
* Q. Zhou et al., Mesh arrangements for solid geometry, SIGGRAPH 2016 — https://www.cs.columbia.edu/cg/mesh-arrangements
* EMBER: exact mesh Booleans via efficient and robust local arrangements, SIGGRAPH 2022 — https://www.graphics.rwth-aachen.de/media/papers/339/ember_exact_mesh_booleans_via_efficient_and_robust_local_arrangements.pdf
* M. Attene, "Indirect predicates for geometric constructions", CAD 2020 — https://doi.org/10.1016/j.cad.2020.102856
* G. Cherchi, M. Livesu, R. Scateni, M. Attene, "Fast and robust mesh arrangements using floating-point arithmetic", SIGGRAPH Asia 2020 — https://doi.org/10.1145/3414685.3417818
* D. J. Jackson, local tolerances, SMA 1995 — https://ftp1.cs.wisc.edu/pub/users/prem/jackson-SM-95.pdf
* CGAL Nef_3 (differential-testing oracle): https://doc.cgal.org/latest/Nef_3
