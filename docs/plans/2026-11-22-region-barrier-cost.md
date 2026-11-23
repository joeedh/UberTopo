# The cost of the `rebuild_regions` barrier (plan v2)

- **Date:** 2026-11-22
- **Status:** implemented 2026-11-22 (§7). v2, after a fresh-context review of v1 (§5);
  changes from v1 are in §6.
- **Why:** `bench_regions` (OI-P3-1 in the [P3b plan](2026-11-18-p3b-editing.md)
  §6) measured the barrier at 0.6–2.2 s on a 1.1·10⁵-face city, against about 1 ms for
  the `split_face` that dirtied the regions. Inside the barrier, at that size:
  - `build_regions`: about 175 ms;
  - the editor index's full rebuild (`idx_.invalidate(); idx_.flush(k_)`): about 185 ms;
  - the face-box grid's rebuild (`grid_.rebuild_if_used`): about 115 ms;
  - in `Local` mode, `validate(k_, ValidateLevel::Embedding)` over the whole
    complex: about 520 ms.

  Undoing the barrier (0.4–0.75 s) rebuilds the index and grid again.
- **Goal:** remove the work the barrier doesn't need, without changing what it
  guarantees. Incremental region maintenance (OI-P3-1) is not in scope; it is
  re-measured after this.

## 1. What `build_regions` changes

From `regions.h`: it writes `.f.region`, `.f.shell`, the face-side shell chains, the
shell and region domains (replaced), the resolved containers (`.v.container`,
`.e.container` of elements with a container hint), and clears `regions_dirty`.

It does not change positions, loops, edges, coedges, radial order, carriers, face
flags, IDs, hints, or any row count of the vertex, edge, coedge, loop or face domains.

## 2. Changes

**2.1 Validation (Local and OnCommit only).** Today the barrier runs
`validate(k_, Embedding)`: the full combinatorial layer and the full embedding layer.
Replace it with:
- the full combinatorial layer (`validate_combinatorial`), which is where the shell and
  region structure is checked (`check_regions`, invariant W). `validate_local.cc`
  says barriers validate this in full (§8 later narrowed it to the region part);
- the embedding layer restricted (`validate_embedding` with a `ValidateFilter`) to the
  vertices and edges whose container is a region after the rebuild. Their
  `ContainerGeometry` check is the only embedding check that reads regions. It is
  skipped while regions are dirty (`validate_geom.cc`, `regions_ok`), so edits made
  while regions were dirty were never re-checked against it. (Placement into a region
  is refused while regions are dirty, `embed_impl`.)
  - The elements are found by a column scan after `build_regions`, before anything
    else changes, so a failed check rolls back with the index and grid untouched.
  - The embedding pass runs only if the combinatorial layer passes, as in
    `validate()`.
  - The filter carries the editor's grid for region 0's ray only when the grid is
    already built (then flushed). Otherwise region 0 is checked by its own boundary,
    so a barrier never builds the grid.

Everything else in the embedding layer checks geometry the barrier doesn't change,
and each earlier transaction validated what it touched (Local, OnCommit), so the
full pass repeated work. `Full` mode keeps `validate(k_, Embedding)` unchanged, and
`None` still validates nothing.

**2.2 The index.** Today the barrier, its rollback and its undo call
`idx_.invalidate(); idx_.flush(k_)`, an O(N log N) rebuild of every map. Of the
index's contents (`editor_index.h`), only two depend on §1's writes:
- `votes_` and `hint_regions_`, from each hinted face side's region;
- `incl_`, for elements whose container changed.

IDs, `users_` (container hints) and `carriers_` (side hints) don't change. Add
`EditIndex::refresh_regions(k)`:
- if the index is invalid, rebuild it (as now);
- otherwise flush the pending dirty rows, then compare each live face's stored
  `region[2]` with `.f.region`, and each live vertex's and edge's stored container with
  the current one, and re-key only the rows that differ: the region part of a face
  (votes and hint regions, not its ID or carriers), the container part of an element
  (`incl_`, not its ID or hint);
- **and update the stored keys** (`fkeys_[f].region`, `vkeys_`/`ekeys_[i].c`) along
  with the maps. `same_as` doesn't compare the key arrays, so stale keys pass
  `index_consistent()` and only show when the row is next flushed, which removes keys
  that aren't there (`elem` erases through an unchecked `find`; `bump` wraps).

This is an O(N) scan of plain columns with map work only for changed rows. The
barrier calls it after its validation passes, and an undone barrier (`undo` restoring
`t.snapshot`) calls it when the transaction's op is `RebuildRegions`. A rolled-back
`rebuild_regions` needs neither the refresh nor the grid: §2.1's order leaves both
untouched until the barrier has passed, and only a failed `rebuild_regions` ever rolls
back (`compact` doesn't validate). Rollback therefore restores the snapshot only. The restored pre-barrier state differs from the post-barrier
state only in §1's columns, so the same refresh is correct in both directions.
`compact` (which renumbers rows) keeps the full rebuild.

**2.3 The grid.** The face-box grid (`editor_grid.h`, `face_grid.h`) stores face boxes,
large faces' triangles and wire segments: no region, shell or container data. The
barrier writes none of what it reads, so `rebuild_regions` (and its rollback and
undo) skip `grid_.rebuild_if_used`. `compact` keeps it.

The editor must know which barrier it is in: the rollback path sees only
`barrier_snapshot_`, so the barrier records its kind beside it. The undo path reads
the transaction's op kind (`OpKind::RebuildRegions`).

## 3. Tests

- After `rebuild_regions`, a refused barrier (rolled back), its undo and its redo:
  `index_consistent()` and `grid_consistent()` (the editor's test hooks, which compare
  against fresh rebuilds) hold, on a model where the rebuild renumbers regions and
  resolves a container hint.
- A barrier that must fail through §2.1's restricted layer: a vertex hinted to a
  room but placed outside it is accepted while regions are dirty, and the barrier
  reports `ContainerGeometry` and rolls back.
- The existing suite, and `bench_regions` before and after.
- **Edits after the refresh:** after the barrier and after its undo, edit rows that
  the refresh re-keyed (kill or re-embed a region-contained element, split a face whose
  regions changed), then check `index_consistent()`. Stale keys show only here.
- Tests run in Local or OnCommit mode after a validation, so the grid is built
  (`grid_consistent()` is vacuous otherwise).
- A renumbering case: an edit that adds a region and shifts many region indices.
- **Mutations:**
  - the refresh skipping faces;
  - the refresh skipping elements;
  - the refresh updating the maps but not the stored keys;
  - the restricted layer listing no elements;
  - the undo of a `compact` skipping its rebuild.
- **Bench:** `bench_regions` before and after, with the snapshot copy and
  `collect_internal` timed too, since they may dominate afterwards.

## 4. Risks

- **R1 — §1 is wrong somewhere.** If `build_regions` writes something else the index
  or grid reads, the refresh leaves them stale. The test hooks catch this on the
  tested models only; the mutation run checks that they would.
- **R2 — The validation contract narrows.** A barrier no longer re-checks geometry
  that earlier transactions validated. A write outside the editor already
  invalidates the index and grid (`check_index_stamp`) but was never validated by
  Local mode; the full pass in the barrier caught such writes by accident. This is
  documented in 07, not relied on.
- **R3 — The grid's cell size** is no longer re-tuned at region rebuilds, only by
  `compact` and outside writes. Region rebuilds don't move geometry, so the tuning
  they did was incidental.

## 5. Review (fresh context, 2026-11-22, of v1)

The reviewer read the code and confirmed all four claims:
- **§1:** `build_regions`' writes are as listed (`regions.cc:655-727`). It never
  changes an unhinted element's container or any row count. `ContainerRef` holds no
  generation, so region IDs and floors never reach the index.
- **§2.1:** `ContainerGeometry` is the only region-gated embedding check.
  `check_regions_fresh` is in the Global layer, which the barrier never ran. The
  combinatorial layer keeps `check_regions` and the region-liveness check of
  containers.
- **§2.2:** the maps that change are exactly `votes_`, `hint_regions_` and `incl_`.
  Pending dirty rows, freed and reused rows, an invalid index, undo and redo all work.
- **§2.3:** the grid holds geometry only. Nothing holds pointers into `k_`, which
  `restore_snapshot` move-assigns.

Findings, by severity:
1. **High:** the refresh must update the stored keys as well as the maps (§2.2).
   `same_as` can't see stale keys.
2. **Medium:** the order of validation and refresh wasn't fixed. A column scan before
   the refresh makes rollback free. Compact never rolls back, so the "compact
   rollback" mutation tested an unreachable path.
3. **Medium:** using the grid for region 0 could build it (about 115 ms). Skipping the
   rebuild also stops re-tuning its cell size (R3).
4. **Low:** docs state the old behaviour: 07 §14's barrier row, 07 §15 ("barriers
   rebuild both eagerly"), and the headers of `editor_grid.h` and `editor_index.h`.
5. **Low:** keep `validate()`'s gate: the embedding pass only after the combinatorial
   layer passes.
6. **Low:** regions are ordered by ID, so one new room can shift many region indices
   and re-key many rows. This is still cheaper than a rebuild; bench it.
7. **Low:** the cost breakdown left out the snapshot copy and `collect_internal`.
8. **Info:** `grid_consistent()` is vacuous until the grid is built.

The comment at `validate_grid.cc:313` ("only barriers ask") is stale: nothing sets
`flt.regions`.

## 6. Changes from v1

- §2.1: the wording on placement; a column scan before the refresh; the
  combinatorial gate; the grid only when built.
- §2.2: the stored keys; rollback needs no index or grid work; the refresh runs after
  validation passes.
- §3: edit-after-refresh tests, a built grid, a renumbering case, the new mutations
  (the unreachable compact-rollback mutation is replaced by the compact-undo one), and
  the snapshot and collect costs in the bench.
- R3, and the doc updates in finding 4, which land with the change.

## 7. Implementation notes

- **As planned:**
  - `EditIndex::refresh_regions` (the stored keys with the maps);
  - `Editor::validate_region_barrier` (a column scan, the combinatorial gate, the
    grid only when built);
  - `barrier_kind_`, so a failed `rebuild_regions` rolls back by the snapshot alone;
  - an undone `rebuild_regions` re-keys and leaves the grid.
- **Tests** (`test_edit_primitives_b`, `test_region_barrier`, Local and OnCommit,
  `double` and `Lattice`). A probe embedded in room B is re-keyed when the shared wall
  goes. After the barrier, its undo and its redo, the index and grid match fresh
  rebuilds, including after edits to the re-keyed rows. A sensor moved into the
  other room while regions are dirty fails the barrier with `ContainerGeometry`, and
  the rollback leaves the index and grid consistent.
- **Mutations:** 7, 6 killed:
  - the refresh skipping faces; the refresh skipping elements;
  - the maps updated without the element keys, or without the face keys (killed only
    by the edit-after-refresh checks, as the review predicted);
  - the barrier's validation listing no elements;
  - the undo of a `compact` skipping its rebuild.

  **One equivalent survivor:** the barrier skipping the combinatorial layer. The
  editor refuses to embed into an unhinted region (`UnhintedRegion`), so every
  region container has a hint that `build_regions` resolves. That layer can then
  fail after a rebuild only through a bug in `build_regions` itself. §8 then narrowed
  it to its region part.
- **Results** (`bench_regions`, 1.1·10⁵ faces, clang-cl Release; two runs):

  | | before | after |
  |---|---|---|
  | barrier after a split, `Local` | 1.3–2.2 s | 380–470 ms |
  | barrier after a split, `None` | 0.6–1.0 s | 160–190 ms |
  | its undo | 0.4–0.75 s | 1.4–2 ms |
  | barrier after a merge (half the regions renumbered), `Local` | — | 490–690 ms |
  | barrier after a merge, `None` | — | 250–370 ms |
  | its undo | — | 90–150 ms |

  Inside the barrier, at 10⁵ faces after a split:
  - the snapshot: 0.1 ms;
  - `build_regions`: 150–250 ms;
  - the combinatorial layer: 220–260 ms;
  - the index refresh: about 1 ms;
  - collect: 0 ms.

  After a merge, the refresh re-keys about half the rows, so the barrier and its undo
  cost more.
- **What is left:**
  - `build_regions` itself (OI-P3-1's incremental maintenance would remove most of it);
  - in `Local` and `OnCommit`, the full combinatorial layer, now the larger half. Its
    shell and region checks (`check_regions`, invariant W, the region IDs) are what a
    barrier needs. They depend on per-coedge validity arrays that the earlier
    combinatorial checks fill in, so running them alone needs its own entry point.
    That is the next cheap step, worth about 240 ms at 10⁵ faces.

## 8. Amendment: the region part of the combinatorial layer only

- **Why:** after §2, the full combinatorial layer is the larger half of a `Local`
  barrier: 220–260 ms of 380–470 ms at 1.1·10⁵ faces. §7's mutation run showed that,
  for every state the editor can reach, it can only catch a bug in `build_regions`.
- **What a barrier needs from it** (`validate.cc`, `Checker`): only three checks read
  what `build_regions` writes:
  - `check_regions`: one unbounded region at index 0, shells partitioning the live
    face sides, shell and region chains, and invariant W on every wedge;
  - unique region IDs (`check_unique_ids` over the region domain);
  - region liveness of `.v.container` and `.e.container` (`check_container`, region
    case).

  Every other combinatorial check reads topology that `build_regions` doesn't write
  and that earlier commits validated: §2.1's argument, applied to this layer.
- **Change:** add `validate_regions(k, v, rep)` running those three, and (review,
  §8.1) `ContainerOnFaceVertex`. `check_regions`
  reads `coedge_ok`, which the full layer fills in while reporting link errors. Here
  it is computed quietly: the coedge is live, and its loop, radial next and radial
  prev are live and in range. That keeps the region checks memory-safe on any
  input, without re-checking links. The barrier (`Local`, `OnCommit`) calls it in
  place of `validate_combinatorial`. `Full` mode is unchanged.
- **Tests** (`test_topo_validate`):
  - `validate_regions` is clean on the valid bases.
  - It reports each region corruption the full layer reports: `WedgeInvariant`,
    `ShellPartition`, `ShellRegion`, `UnboundedRegion`, `DuplicateId` on regions,
    and `ContainerInvalid` for a container naming a dead region.
  - It also runs on every topology corruption case, with its result ignored, so
    ASan checks it doesn't read out of range.
- **Mutations:** each of the three checks removed; the quiet `coedge_ok` taking all
  coedges as followable (ASan, through the topology corruptions).
- **Risk:** a topology corruption that only a full combinatorial pass would catch is
  no longer caught at a barrier. The barrier never wrote it, and Local and OnCommit
  validated it when it was made (R2).

### 8.1 Review (fresh context, 2026-11-22, of §8)

- **Confirmed:**
  - the quiet `coedge_ok` bounds every read `check_regions` makes;
  - the view is needed (`verts` is in `Complex<C>`, not `ComplexBase`);
  - per transaction, earlier commits validated everything else.
- **Findings, all adopted:**
  1. **Medium:** `ContainerOnFaceVertex` reads `.v.container` too. `build_regions`
     resolves every hinted live vertex, so one with a `None` container lying on a
     face would start failing it. The builder, `make_face`, weld and imprint keep
     hinted vertices off faces, so this is unreachable, but the check is O(E) and is
     added.
  2. **Medium: the risk section missed a gap.** The `Editor` constructor validates
     nothing, so in `Local` and `OnCommit` the first barrier was, by accident, the
     only full combinatorial check of a built or loaded complex. It also caught
     anything `touched_filter` missed. Both are now the caller's job (R4).
  3. **Low: docs this makes false:** `validate_local.cc`'s header, the barrier
     comments, §2.1, 07's barrier row, §7's survivor note.
  4. **Low:** with dirty regions, `validate_regions` skips `check_regions` and keeps
     the ID and container checks, like the full layer.
  5. **Low:** out-of-range corruptions must be far out of range (NONE, or beyond 2×
     capacity) for ASan to see a read.
  6. **Low:** `coedge_ok` and the W loop aren't free, so the saving is under 240 ms.
- **Tests added on its advice:**
  - parity on every corruption case (the full layer's region issues must all be in
    `validate_regions`' report);
  - a container of kind 3;
  - a region index past the domain;
  - a container naming a dead region;
  - a region container on a face vertex;
  - far-out-of-range links on coedges, loops, shells, face sides and regions.

### 8.2 Implementation notes

- **As planned,** with §8.1's additions. `validate_regions` is a second entry point on
  the combinatorial `Checker` (`run_regions`).
- **Mutations:** 7, 6 killed by `test_topo_validate`:
  - each of the five checks removed;
  - the quiet `coedge_ok` following every coedge (it reads out of range on the wild
    links even in Release).

  **The equivalent survivor moves:** the barrier calling no region checks at all
  survives, for §7's reason (after `build_regions`, only a bug in it could fail
  them). The direct tests are what pin `validate_regions` down.
- **Results** (`bench_regions`, 1.1·10⁵ faces; this run's machine was slower, with
  `build_regions` at 223 ms):
  - after a split, `Local` now costs 21–25 ms more than `None`, against 220–260 ms
    before;
  - after a merge, it costs about 75 ms more.

  The barrier is now essentially `build_regions` itself, which only incremental region
  maintenance (OI-P3-1) can remove.
- **R4 — the initial complex.** An editor never validates the complex it is given.
  Callers that build or load one validate it (`validate(k, Global)`) before editing
  in `Local` or `OnCommit`. Documented in 07.
