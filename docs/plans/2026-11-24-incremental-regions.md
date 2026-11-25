# Plan: incremental region maintenance (Stage B of OI-P3-1)

Status: **v2**, after a fresh-context review (§10). Not implemented.

## 1. Why and scope

- **The need.** A `rebuild_regions` barrier at 1.1·10⁵ faces costs about 80–86 ms without
  validation (`None`) and 113 ms with it (`Local`), whatever the edit. The edit itself
  takes 0.7–3.4 ms. [2026-11-23](2026-11-23-region-maintenance.md) Stage A made the full
  build linear, and [2026-11-24 B0](2026-11-24-edit-index-regions.md) removed the
  editor's share. What is left is the full build and the region checks.
- **Decided (D1, option (c)):** shells and regions keep their canonical sorted order.
  Passes over all faces, shells and regions are allowed if they are cheap.
- **Goal:** at 1.1·10⁵ faces, a barrier after a local edit on the room-grid city takes
  10–15 ms in `None` and `Local`, with the same result as `build_regions`: the same
  columns, domains, generations, carried-over region columns and report issues. The
  work counters `ray_tests` and `ray_retries` will differ; nothing reads them.
- **Not in scope:**
  - what regions mean; IDs (DEC-017); the barrier's snapshot undo;
  - `Full` validation, which stays the oracle;
  - edits inside one shell the size of the model, beyond what the update gives
    (§4.6).

## 2. Measurements (2026-11-24, after B0)

Temporary timers, `bench_regions`, clang-cl Release, medians in ms. The timers were
removed afterwards.

| phase | 1.1·10⁴ faces | 3.7·10⁴ | 1.1·10⁵ |
|---|---|---|---|
| triangulate | 1.4 | 3.9 | 11.7 |
| DSU over wedges | 0.9 | 3.2 | 12.3 |
| side IDs | 0.4 | 1.0 | 2.9 |
| labels, sort | 0.6 | 2.0 | 7.3 |
| group sides by shell | 0.1 | 0.2 | 0.8 |
| kind, volume, box, vertex sets | 1.7 | 5.7 | 23 |
| nesting | 0.1 | 0.2 | 0.5 |
| votes and IDs | 0.6 | 2.2 | 8.2 |
| region order | 0.2 | 0.7 | 2.7 |
| containers | 0.0 | 0.1 | 0.3 |
| writes (faces, shells, regions) | 0.3 | 1.0 | 2.9 |
| **sum** | 6.4 | 20 | 73 |
| `validate_regions` (`Local` only) | 1.6 | 5.9 | 27 |

- **Vertex sets** are about 7.5 of the 23 ms. They are used only by nesting tests, and
  this model runs none.
- **One shell dominates.** The city's exterior is a single cavity shell of 32 760 sides,
  15 % of all sides; no other shell has more than 64. The road-on-terrain model is one
  sheet shell holding every side. So on terrain, *every* face edit changes a shell the
  size of the model. An update that costs O(sides of the touched shells) gains less
  there (§4.6).
- **Passes Stage B would keep,** measured on the same complex: comparing `.f.region` and
  `.f.shell` with their new values 0.46 ms; comparing the shell chains 0.4 ms; sharing
  every column 0.08 ms; testing every page for equality by identity 0.01 ms; one scan of
  all coedges 1.0 ms.

### 2.1 Baselines (W0, 2026-11-24)

`bench_regions` after W0, before W1; ms. One run on a shared machine; a second run slowed
down by 2.5× partway through, so comparisons are made with old and new binaries run
alternately.

| model | faces | shells | full build | edit | barrier `Local` | barrier `None` |
|---|---|---|---|---|---|---|
| city | 111 636 | 31 753 | 78 | interior wall split | 98 | 80 |
| | | | | exterior wall split | 96 | 86 |
| | | | | wall removed | 128 | 99 |
| | | | | vertex moved | 107 | 83 |
| sensors | 111 636 | 31 753 | 83 | interior wall split | 150 | 82 |
| | | | | wall removed | 143 | 82 |
| terrain | 102 400 | 1 | 142 | face split | 155 | 133 |
| | | | | vertex moved | 147 | 140 |
| hall | 21 600 | 4 002 | 458 | object split | 445 | 437 |
| | | | | hall wall split | 452 | 442 |

- **Terrain** costs more per face than the city, as every face is projected and needs a
  triangulation.
- **Sensors:** `ContainerGeometry` on 10⁴ embedded vertices adds about 50 ms to a `Local`
  barrier. §5 leaves it as it is; it is the largest cost that this plan doesn't touch.
- **Hall:** 2000 cavity shells tested against a hall of 9600 faces. Nearly all of it is
  nesting.

## 3. Finding what changed: compare with a base copy

The 2026-11-23 outline tracked touched rows through `EditIndex::mark`, reset them on a
build and restored them on undo. This plan compares instead.

- **The base.** After each successful build, the builder keeps copy-on-write shares of
  the columns `build_regions` reads (§3.1). Sharing costs O(pages), 0.1 ms.
- **The diff.** At the next barrier, each input column is compared with its base page by
  page. Pages with the same buffer are equal (`ColumnBase::same_page_as`). The rows of
  the others are compared bytewise.
- **Why it is better than tracking:**
  - it is a function of the two states only. Rollback, undo, redo, failed barriers,
    undo across barriers, writes from outside the editor, and a whole complex replaced
    all come out right with no code of their own;
  - an edit that was undone counts as no change;
  - nothing is added to the write path or to undo.
- **Its costs:**
  - the base pins pages, so the first write to a page after a build copies it. With a
    journal this happens already, since the barrier's snapshot holds the same pages;
  - about 22 columns are held per editor, sharing their pages with the complex;
  - with `LITESTL_ATTR_CHECKS` on, sharing and releasing a column checksums its pages.
    So only columns that differ from their base are shared again.
- **Sizes:** rows that exist only in the base, or only now, differ. Lookups of a base row
  in the current complex are bounds-checked, since undo can shrink a domain. A column
  whose type or page size differs from its base means a full build.
- **Page identity can't lie:** the base holds a reference to every buffer it compares
  with, so a buffer can't be freed and its address reused.

### 3.1 Inputs

| group | columns |
|---|---|
| vertices | `.live`, `pos`, `.v.container_hint` |
| edges | `.live`, `.e.vs`, `.e.curve`, `.e.range`, `.e.container_hint` |
| coedges | `.live`, `.c.e`, `.c.v`, `.c.l`, `.c.next`, `.c.radial_next`, `.c.flags` |
| loops | `.l.c`, `.l.f`, `.l.next`, `.l.size` |
| faces | `.live`, `.f.l`, `.f.flags`, `.id` |
| face sides | `.fs.hint` |
| global | `.g.skeleton_tol` |
| geometry store | the curves that `.e.curve` names |

- **The container hints** are read in full on every build (a scan, 0.3 ms), so they don't
  need a base.
- **Outputs of the previous build** (shell and region domains, `.f.region`, `.f.shell`,
  `.fs.shell_next`, containers) are read from the complex itself, as today, for
  generations, carry-over and compare-before-write. They aren't part of the diff: after
  a barrier's undo they are older than the base, and that is fine (§4.5).
- **Curves.** A `PoolRef` has no generation, and a freed slot can be reused for another
  curve, so equal refs don't prove equal curves. The base keeps a copy of the store
  (O(pages)). If `geom.version()` is unchanged, refs mean the same. Otherwise each
  distinct ref of a live curved edge is compared by `entry_hash`, recursing through
  inner refs (bounded), memoised per ref. An edge whose ref's content changed counts as
  changed. `entry_hash` aborts on a dead slot, and a complex may hold a bad ref (tests
  do). So a ref that isn't well-formed and live in both stores, with the same pool
  class, counts as changed without being hashed.
- **`.g.skeleton_tol`** changed: full build.

### 3.2 From changed rows to touched faces

- **Link-touched** faces (the wedge links of their sides may have changed):
  - the face of every differing coedge row, through its loop, both in the base (if live
    there) and now (if live now);
  - the face of every differing loop row, in the base and now;
  - every face whose `.live` or `.f.l` differs.
- **Geometry-touched** faces (their triangles may have changed):
  - every link-touched face;
  - faces with a differing `.f.flags` (the projection axis);
  - faces with a coedge whose start vertex's `pos` differs, or whose edge's curve
    differs, or whose `.e.vs` or range differs while the edge is curved in the base or
    now. These are found with one scan of the coedges against bitmaps of such vertices
    and edges, run only if there are any (1 ms). The scan trusts no disk or radial
    links.
- **ID-touched** faces (`.id` differs): their side IDs change, so labels and minimum sides
  do.
- **Hint-touched** sides (`.fs.hint` differs): only their shells' hint counts change.

### 3.3 Full builds

A full build runs if:

- there is no base, or the skeleton tolerance changed;
- more than a quarter of the live faces are geometry-touched. `compact` and loading a
  complex fall under this rule (every page differs). The fraction is tuned by
  measurement (§7);
- the update meets something that a correct diff of a consistent complex can't produce:
  a wedge between an open and a closed side (§4.3), a link out of range, a loop walk
  that doesn't close, two equal labels or two equal region IDs. A full build handles
  these as today. They are counted, and tests assert the counts;
- the update finds an error (§4.4).

## 4. The incremental build

### 4.1 One engine

`build_regions` becomes a class, `RegionBuilder<C>`, with the same stages as today over
its own arrays. `build_regions(k, report)` is a full build by a fresh builder. The
editor keeps a builder and calls `update(k, report)`. So there is one copy of the kind,
nesting, vote, ID, resolve and write code. The full build stays independent of any
cached state, and is the oracle.

The builder keeps:

- **per face:** whether it triangulated, its box, and its volume subtotal (§4.2), about
  90 bytes; its two side IDs;
- **per side:** its shell slot. A slot is the builder's own stable number for a shell,
  unlike its index, which is its rank by label;
- **per shell slot:** label, kind, volume, box, parent outer slot (or none), and its
  distinct hints with their counts (usually none or one). A dead slot's number isn't
  reused within the same update;
- **per region** (an outer shell's slot): the ID, with what it was derived from;
- **sorted lists:** (label, slot) and (region ID, outer slot).

Triangles and vertex sets aren't kept. They are rebuilt for touched faces and for the
shells a nesting test needs, at most once per shell and build.

### 4.2 Per-face subtotals

- The full build computes each face's volume subtotal (`approx`, `abs_sum`, `perm_sum`,
  `terms`, or the exact sum for Lattice) and box in the parallel triangulation loop. A
  shell's volume is the sum of its sides' subtotals, in side order, `approx` negated for
  FRONT sides.
- **This changes the floating rounding** of `approx`. The error bound holds for any order
  of summation with at most `terms` additions on a path, and a two-level sum has no more.
  `kind` can change only for a volume within its bound (an error today or after), and
  `smaller_than` only for volumes within rounding of each other. A sign can't flip; a
  shell can only become or stop being degenerate. Lattice is exact and
  doesn't change.
- **Vertex sets** are built only for shells that reach step (b) of nesting, once each.
- Both also make the full build faster (§2: the big shell is summed serially today).

### 4.3 Shells

1. **Affected shells:** the base shells of the link-touched faces' base sides.
2. **Open sides:** the live sides of affected shells, and the sides of link-touched live
   faces.
3. **Union-find over the open sides.** Links come from the coedges of the open sides'
   faces (walked through their loops). Each component becomes a new slot. By the claim
   below, a wedge at an open side leads to an open side. If one doesn't, the diff is
   wrong or the complex is inconsistent, and a full build runs (§3.3). This is a check
   on §3.2 that also runs in Release.
4. **Claim: every other shell is unchanged,** if `radial_next` is a permutation in both
   states (it is in a combinatorially valid complex). Suppose an untouched face's side gains or
   loses a link across the wedge from coedge `p` to coedge `c`, with `c` its own.
   - It loses one if `radial_next(p)` was `c` and no longer is: `p`'s row differs, so
     `p`'s face is link-touched, and `p`'s side was in the same shell.
   - If it gains one from a new predecessor, it also lost the old one, as each coedge
     has exactly one predecessor.
   - Wedges from its own coedges change only if their rows do, which makes it touched.
   - A wedge whose coedges are the same can still join other sides if a coedge's face
     or sense changed. Those rows differ, so that face is touched, and its old side was
     in the shell.

   So a side outside the affected shells keeps all of its links, and they lead only to
   its own shell.
5. **Geometry-changed shells:** new slots, and kept shells with a geometry-touched or
   ID-touched side. A kept shell with geometry changes gets a new slot too, with the
   same sides, so later steps see only "new" and "kept" slots.
6. **Label, kind, volume and box** of new slots, from the per-face data.
7. **Shell order:** dead slots are dropped from the sorted label list and the new ones
   merged in, O(S).
8. **Side lists** by shell index: one counting pass over all sides (0.8 ms), as today.

### 4.4 Nesting, regions, IDs

- **Nesting.** For each cavity or sheet shell A:
  - new, or its parent slot died: tested against every outer shell, as in a full build;
  - kept: tested against new outer shells only, each compared with its current parent.
    A smaller volume wins, and an equal one wins if its label is smaller (the full
    build scans outer shells in label order and keeps the first of equals).
  - A kept pair (A, B) has the same boxes, sides and geometry, so the same answer. A's
    tests against kept shells were error-free, or the last build would have failed.
- **Regions and IDs are recomputed for all regions,** with the full build's code, from
  per-shell summaries instead of sides. This costs an estimated 2–3 ms at 3·10⁴ shells,
  against about 1 ms for doing it incrementally, and removes the hardest bookkeeping:
  - a region's minimum side is the smallest label of its shells;
  - its votes are the sum of its shells' hint counts. Shells with a hint-touched side
    recount theirs;
  - winners, carriers, conflicts and duplicates follow as today, in today's order;
  - an ID is hashed again only if its hint, whether another region shares the hint, or
    its minimum side differs from what the kept ID was derived from.
- **Region order:** the kept order with removed and renamed regions dropped and the new
  ones merged in, O(R).
- **Containers:** resolved for every hinted vertex and edge, as today.
- **Errors.** If any step reports an error, the incremental result is dropped and a full
  build runs, so the reported issues and their order are the full build's by
  construction. If the full build succeeds, its result stands; that is a bug, so it is
  counted and tests assert the count is zero. A failed build leaves the builder's state
  as it was, with its base: the next diff starts from there.

### 4.5 Writes

Same writes as today, in one place, with two changes:

- `.f.region`, `.f.shell`, `.fs.shell_next` and containers are **compared first** and
  written only if different: under 1 ms when nothing differs. Column versions and pages
  then change only where regions do. After a merge about half of the faces are
  renumbered, and the writes cost what they do today (2.9 ms).
- The compare runs against the complex's current values, whatever they are. This is why
  stale outputs (after a barrier's undo, or an outside write) need no tracking.

Shell and region domains are rebuilt, with `replace_generations` and carry-over, as today
(1 ms).

### 4.6 One shell the size of the model

On a 10⁵-face terrain sheet a link edit re-runs the union-find, the label and the hint
counts over every side, an estimated 20 ms, instead of 80 ms for a full build. A sheet's
volume is never used, so it isn't summed.

Doing better means finding locally whether the shell stayed connected, and by a similar
search which part split off. The review found errors in v1's version of that (§10,
finding 4), and the gain isn't known until the terrain bench (W0) and the update (W3)
exist. It gets its own plan then, if the numbers justify it.

## 5. Validation of the barrier (`Local`, `OnCommit`)

`validate_regions` takes 27 ms, most of it serial walks along chains and coedges. v1
proposed checking only what the update changed. The review showed that this misses
failures (§10, findings 1–3), so **coverage stays what it is today** and the check gets
faster:

- **A flat, parallel pre-check** that passes only if `validate_regions` would:
  - one unbounded region, at index 0; region IDs strictly ascending from index 1, none
    equal to region 0's; the region chains, O(S + R);
  - per live side: its shell and region are live and the shell's region is the side's;
    `.fs.shell_next` is NONE or a later side of the same shell;
  - per shell: exactly one side without a successor, no side with two predecessors, and
    `.s.first` is its smallest side. With ascending links that makes each chain one
    path over exactly the shell's sides;
  - per coedge: invariant W;
  - per vertex and edge: containers live, and none on a face vertex.
- **If the pre-check fails,** `validate_regions` runs and decides. So issues, order and
  codes are today's, and a valid chain that isn't ascending (never written by
  `build_regions`) is still accepted.
- **`ContainerGeometry`** stays as it is: every element with a region container. W0's
  sensor model measures it. If it dominates, it gets a plan of its own.
- It doesn't depend on the diff or on what earlier barriers validated. That makes
  invariant W a Release check of the update itself.
- Expected 3–5 ms (not measured).

## 6. Testing

- **`EditorOptions::verify_regions`:** every barrier also runs a full build on a copy
  taken before it, and compares the content hash, generations and floors of every
  domain, and the report's issues. A mismatch aborts with a description. Its default
  comes from a CMake option that every test preset sets, Release ones included: the
  Windows gate's two presets both define NDEBUG, so a debug-only default would never
  run there. Benches turn it off. Every editor test then checks the update.
- **A new test, `test_region_update`:**
  - random edits with a barrier after every 1–8 of them, undo and redo across barriers,
    failed barriers, outside writes between barriers, `compact`, in float, double and
    Lattice, at 1 and 4 bands;
  - nesting: a room inside a room inside a room; the middle one removed and restored; a
    fence in the air enclosed by a new room far larger than it; a sheet moved from one
    region to another by `set_position`; two containing shells of equal volume (they
    must overlap, so in `None` mode);
  - hints: winners crossing 1 ↔ 2 ↔ 3 and 1 → 3 in one barrier, and back; a minority
    carrier becoming the winner; an untouched region renamed; container users of each,
    with a container that moves to another region without its row changing;
  - validation: a barrier refused as `Invalid`, then run again without an edit, is
    refused again; a `None` barrier followed by a `Local` one that must fail; the moved
    endpoint of a wire with a region container;
  - curves: an arc replaced, the old one collected, its slot reused by another arc and
    set on an edge, with the ref's bits equal to the base's;
  - curves, badly formed: a dead ref and a ref of another pool in the base, and now;
  - one shell split in two and merged back; a face split and joined inside a sheet;
  - a face freed and its slot reused by an unrelated face; a vertex moved and moved back;
    domains shrunk below the base's size by undo;
  - inconsistent links by outside write: two coedges with the same `radial_next`.
- **Inputs are complete (§3.1):** a test classifies every column of every group as an
  input, an output the build also reads, or not read, and fails on a column it doesn't
  know.
  - Not read: random bytes in every row must not change what a full build writes.
  - Inputs: one row changed by a write from outside, the update must equal a full build
    and, where the full build's result changes, must not be a full build itself. The
    skeleton tolerance is the exception, since it forces one.
- **How much work an update does** is counted (triangulated faces, open sides, nesting
  tests, full builds and their reasons) and asserted for a few edits, so that an update
  that silently falls back to a full build fails a test.
- **The flat pre-check:** for each rule, a complex that breaks it makes the barrier
  report what `validate_regions` reports.
- **Goldens don't move** (Lattice by construction; floating if no decision is within
  rounding). If one does, the reason is found before anything is re-pinned.
- **Mutations:** each touched rule of §3.2 dropped; base or current face of a differing
  row dropped; old parent kept after it changed; new outer shells not tested against
  kept shells; tie by label reversed; an ID kept though its hint became shared; hint
  counts not recounted; the store's version ignored; inner refs not compared; base not
  refreshed after a build; base refreshed after a failed one; merge of sorted lists off
  by one; compare-first skipping a needed write; the half-open wedge ignored; each rule
  of the pre-check (§5) dropped.

## 7. Work items

- **W0: bench.** `bench_regions` reports an interior wall split, an exterior wall split, a
  merge and a `set_position` separately, and gains:
  - a terrain sheet of about 10⁵ projected faces (one shell);
  - a city with hinted rooms and 10⁴ embedded sensors;
  - a hall with many objects inside (nesting): one object edited, then the hall.

  Baselines are recorded here.
- **W1: the engine.** `RegionBuilder`, per-face subtotals, lazy vertex sets,
  compare-first writes; only full builds. Checked against today's `build_regions` by a
  temporary A/B hook, as Stage A was. Files: `topo/regions.cc` splits into
  `topo/regions/` (builder, shells, nesting, naming, write), each under 40 KB.
- **W2: the diff** (§3), with its own unit test against brute-force comparison.
- **W3: the update** (§4.1–§4.5), `verify_regions`, `test_region_update`.
- **W4: validation** (§5). Independent of W2 and W3, and useful with today's full build.
- **W5:** mutations, bench, thresholds, docs: 01 §7, 07, P3b OI-P3-1, the 2026-11-23
  plan, the index, `docs/debugging.md`.

Order: W0, W1, W4, W2, W3, W5. W1 and W4 help every barrier whatever follows. Each of
W1, W3 and W4 is a commit of its own with a green gate.

## 8. Expected result (1.1·10⁵ faces)

All but the first column are estimates.

| | now | after W1 and W4 | after W3 |
|---|---|---|---|
| full build | 80 | 60–65 | same |
| barrier, interior wall split, `None` | 81–86 | 65–70 | 8–10 |
| barrier, merge, `None` | 81–86 | 65–70 | 10–13 |
| barrier, exterior wall split, `None` | 81–86 | 65–70 | 12–16 |
| barrier, `Local`, on top of `None` | 27–32 | 3–5 | same |
| barrier, terrain sheet, `None` | not measured | | about 20–25 |

- An exterior wall split re-runs the union-find and sums of a shell of 32 760 sides.
- **Nesting costs are on top.** An edit to an object inside a large outer shell
  triangulates that shell and builds its vertex set once per barrier. An edit to the
  outer shell itself retests every shell in it, as a full build does.

What remains is linear: the side lists, the compare, the coedge scan (moved vertices
only), the merges, the domain rebuilds and the base share. At 10⁶ faces this is
expected to be 80–100 ms.

## 9. Risks

- **R1: an input missing from §3.1** gives a stale result. The classification test and
  `verify_regions` guard against it.
- **R2: the nesting and naming rules are global** (far effects, shared hints). That is
  what `test_region_update` and the mutations target.
- **R3: inconsistent links.** A full build scans all coedges; an update walks faces'
  loops. They agree if every live coedge is reachable from its face's loops, which the
  combinatorial layer checks. In `None` mode the caller guarantees it, as for every
  primitive. The walk must still be bounded and in range on a corrupt complex.
- **R4: memory.** About 90 bytes per face plus the pinned pages, per editor: about 10 MB
  at 1.1·10⁵ faces.
- **R5: the first barrier** after attaching an editor is a full build.

## 10. Review (fresh context, 2026-11-24, of v1)

Verdict: adopt with changes. The diff and the shell, nesting and write parts were found
sound; no counterexample to §4.3's claim on a combinatorially valid complex.

- **Confirmed:**
  - the input list (except each curve kind's evaluator body and `cdt_proj`, not read);
    outputs aren't inputs; page identity;
  - kept pairs in nesting; the order of report issues;
  - the error bound of §4.2;
  - nothing relies on `build_regions` writing unchanged values.
- **Findings, all adopted:**
  1. **Blocker:** v1's filtered validation was relative to a base that no validation had
     passed. A refused barrier, run again, would have passed. Filtering dropped (§5).
  2. **Major:** its `ContainerGeometry` set missed moved endpoints of embedded wires, and
     containers that resolve to another region without their row changing. Moot for
     the same reason.
  3. **Major:** invariant W over all coedges is Release's only check that doesn't depend
     on the diff. Kept, and the flat pre-check proposed (§5). This settled v1's decision
     D2: nothing is given up.
  4. **Major:** v1's local reconnection was misstated (boundary sides, several shells
     merging, a kept slot with changed sides, base predecessors). Deferred to its own
     plan (§4.6).
  5. **Major:** a debug-only `verify_regions` never runs on the Windows gate. Confirmed
     (both presets define NDEBUG); now a CMake option (§6).
  6. **Major:** a half-open wedge was silently ignored. Now a full build, counted (§3.3).
  7. **Major:** `entry_hash` aborts on a dead ref. Guarded (§3.1).
  8. **Major:** vertex sets and triangles must be built once per shell, not per pair, and
     nesting costs were missing from §8 and the bench (§4.1, §7, §8).
  9. **Minor:** size changes specified (§3).
  10. **Minor:** edges curved in the base or now (§3.2).
  11. **Minor:** "exactly one winner" rather than "1 ↔ 2", slot reuse, dead cavities, the
      minimum side is a minimum of labels. Mostly moot with the global naming pass.
  12. **Minor:** test gaps: a third column class, detection of input changes, the
      equal-volume case, checksum costs of re-sharing, work counters (§1, §3, §6).
  13. **Minor:** §8 mixed three kinds of edit. Split.
- **Simplification adopted:** naming is recomputed for all regions from per-shell
  summaries, instead of v1's per-hint lists and affected regions (§4.4).
