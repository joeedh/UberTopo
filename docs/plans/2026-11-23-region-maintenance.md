# Plan: cheaper region rebuilds (OI-P3-1)

Status: **v2**, after a fresh-context review (§7). **Stage A implemented** (§9), and
B0 ([2026-11-24](2026-11-24-edit-index-regions.md)). Decision D1 is made: (c), sorted
indices (§8). Stage B has its own plan,
[2026-11-24](2026-11-24-incremental-regions.md), which replaces §4's outline.

## 1. Why and scope

- **The need.** After the [2026-11-22](2026-11-22-region-barrier-cost.md) fixes, a
  `rebuild_regions` barrier costs about what `build_regions` costs: 150–250 ms at
  1.1·10⁵ faces (`bench_regions`, clang-cl Release). Every face edit dirties the
  regions, so any interactive step that needs regions pays it. OI-P3-1 in the
  [P3b plan](2026-11-18-p3b-editing.md) asks for incremental region maintenance.
- **What the profile says** (§2): little of the cost is geometry. Most of it is
  bookkeeping: `std::map`s with 10⁴–10⁵ entries, vectors of vectors, and serial ID
  hashing.
- **So this plan has two stages:**
  - **Stage A (implement now):** rewrite `build_regions`' bookkeeping so it is linear
    and allocation-light, with bit-identical output. It is bounded and low risk, and
    the expected gain is close to 2×.
  - **Stage B (design only):** incremental maintenance. It needs decision D1 (§4.2)
    and gets its own reviewed plan once D1 is made and Stage A's numbers are in.
- **Not in scope:** changing what `build_regions` computes (shells, kinds, nesting,
  IDs, containers, reports), the validator, or the barrier's undo.

## 2. Measurements (2026-11-23)

- **How:** temporary per-phase timers in `build_regions` (`CLAUDENOTE`, behind
  `UT_REGION_TIMES`), over `bench_regions`' 29 builds at 1.1·10⁵ faces (31 753
  regions). Medians, in ms; the total median was 147 ms in this run.

  | phase | ms | what it does |
  |---|---|---|
  | triangulate | 11.7 | face triangles (parallel) |
  | dsu | 12.0 | union-find over the wedges of every live coedge |
  | side ids | 10.8 | `face_side_id` (a SipHash `derive`) per side, serial |
  | root_label | 18.3 | `find` per side; min side ID per DSU root in a `std::map` |
  | sort labels | 6.5 | sort the labels; `root_shell` `std::map` |
  | shells | 17.8 | `find` again per side; `std::map` lookups; vector of vectors |
  | kind/volume | 25.1 | per-shell volume, box, sorted vertex set (parallel) |
  | nesting | 0.6 | ray tests |
  | preg_sides | 4.3 | sides per provisional region, vector of vectors |
  | votes | 5.9 | a `std::map` of hint votes per region |
  | ids | 11.1 | `winners` `std::map`; ID hashes |
  | region ids | 6.7 | sort regions by ID; `DuplicateHint` `std::map` |
  | containers | 4.6 | `hint_region` / `carrier` `std::map` lookups |
  | writes | 9.4 | faces, shells, regions (`old_by_id` `std::map`) |

- **Reading it:** geometry (triangulate, kind/volume, nesting) is 37 ms. The other
  110 ms is serial bookkeeping, most of it in ordered maps and nested vectors, and
  none of it is inherently more than linear. The teardown of the per-face triangle
  vectors falls outside the timers.

## 3. Stage A: linear bookkeeping in `build_regions`

### 3.1 Changes (`topo/regions.cc`, plus one litestl helper)

1. **Side IDs** in a `parallel_for` over faces (each writes its own two slots).
2. **DSU roots once:** `root[s] = find(s)` for every live side, in one pass. The label
   and shell phases both read it.
3. **Shell labels** without maps:
   - `label[root]`, an array over the `2F` side slots: the minimum side ID per root;
   - `(label, root)` pairs for the roots only, sorted (S log S, S ≈ 3·10⁴; side IDs
     are unique, so the order is total);
   - `root_shell[root]` as an array.
4. **Shell sides in CSR form** (counts, prefix offsets, one flat array) by a counting
   pass over sides in ascending order. Ascending side order within a shell must be
   kept: the `.fs.shell_next` chains, the test-point search, the floating volume
   summation order and the nesting ray's triangle order all depend on it.
5. **Per-shell vertex sets** as one flat array with per-shell ranges, sorted and
   deduplicated per range. This stays parallel. (Order before the sort is free.)
6. **Region sides:** `preg_sides` is dropped. The minimum side and the votes don't
   depend on order, so they are accumulated per provisional region in one pass over
   shells.
7. **Votes** without a per-region `std::map`:
   - a small array of distinct `(hint, count)` per region, scanned linearly, switching
     to a `util::Map` once it holds more than 8 hints. Region 0 can carry about 10⁵
     sides, almost all with the unbounded hint, so sorting the raw hints is out;
   - `unb` stays in the vote like any hint. The winner is the largest count, ties
     going to the smallest hint (`Hash128` order: hi, then lo), as the ascending
     `std::map` scan with `n > best` gave. A bounded region where `unb` ties and wins
     gets no hint, reports `HintConflict`, and its other hints become carriers;
   - `HintConflict`'s `votes.size() > 1` is the number of distinct hints, `unb`
     included.
8. **`winners`, `hint_region`, `carrier` and `DuplicateHint`** in one `util::Map` from
   hint to `{winning regions, first winner, carrier}`:
   - `DuplicateHint` keeps today's loop over final regions `r = 1..R`, reporting the
     second winner of a hint in final-index order (`seen[h]++ == 1`);
   - `carrier`'s "another region" test (`it->second != r`) is dead code, since a
     region's hints are distinct. It is kept as a comment, not as behaviour.
9. **`resolve` keeps its lookup order:** `unb` first, then the winning region (or
   `AmbiguousContainerHint` if two win it), then the unique minority carrier (or
   `AmbiguousContainerHint`), else `UnresolvedContainerHint`. The first error issue
   becomes the barrier's `region_code` (`editor_impl_face.h`), so the issue order
   (vertices, then edges, in index order) stays too.
10. **Region order:** the sort by ID stays (`std::sort` on unique IDs; a debug
    assertion that no two are equal).
11. **`old_by_id`** becomes a `util::Map` from ID to old index, filled in ascending
    old index so a duplicate ID keeps the last row, as `std::map`'s assignment does.
    It includes region 0. An old domain isn't guaranteed to be in ID order (a loaded
    complex isn't checked for it), so a merge is not used.
12. **The CSR grouping helper** (group indices by key into offsets plus a flat array)
    is general-purpose, so it goes into litestl (`util/`) on a feature branch, per
    CLAUDE.md. UberTopo's gitlink is bumped on the session branch, and UberTopo master
    waits for the litestl PR to merge (the PR is opened when the owner asks).

### 3.2 Expected result

- About 147 → 75–85 ms at 1.1·10⁵ faces. Geometry (37 ms), the DSU (12 ms) and the
  writes (about 8 ms) remain, plus 15–25 ms of linear passes.
- The `rebuild_regions` barrier falls by the same amount, since it is now
  essentially `build_regions`.

### 3.3 Equivalence

- **Bit-identical output** is the contract: the same columns, domains, generations,
  carried-over region columns, and `RegionReport` (issues, their order, counters).
- **During development:** an A/B harness (scratch, not committed) runs the old and
  new `build_regions` on copies. It covers:
  - every generator family (`gen/buildings.h`, `gen/road_terrain.h`, the fixtures);
  - every step of `test_edit_random`'s sequences;
  - float, double and Lattice;
  - 1, 2, 4 and 12 task bands, since the side IDs become parallel.

  It compares `hash_complex` and the full reports.
- **Why the goldens aren't enough:** they build fresh complexes and require
  `warnings == 0`. So they never exercise warnings, carry-over from an old region
  set, or generations. No existing test pins issue order either.
- **New committed tests** (`test_topo_regions`):
  - a rebuild after edits that insert and remove regions mid-order, with a custom
    region column carried over and generations checked;
  - an old region domain out of ID order (built, then permuted), rebuilt;
  - a vote tie between two hints (the smaller wins), and one between `unb` and a hint;
  - `DuplicateHint` with three regions winning one hint (reported once, on the second
    in final order);
  - a pinned sequence of issues, with several `HintConflict`, `DuplicateHint` and
    container issues, and the first error the barrier reports.

  The ambiguous minority carrier is already covered (`test_edit_primitives_b`).
- **Mutations:**
  - the tie rule reversed;
  - `unb` filtered out of the votes;
  - `DuplicateHint` emitted in hint order;
  - side order within a shell reversed (CSR filled backwards);
  - the label minimum turned into a maximum;
  - region 0 left out of `old_by_id`;
  - first-wins on a duplicate old ID.

## 4. Stage B: incremental maintenance (design outline)

### 4.1 Outline

The barrier recomputes only what an edit can have changed.

- **Touched faces since the last clean build:** derived from touched coedges (a
  `radial_next` change writes a coedge) and from written vertices and edges (a moved
  vertex or a changed curve changes the volume, kind and box of every shell through
  its faces). The faces incident to them count as touched too. `EditIndex::mark`
  records only faces and sides today. The set is reset by a clean build and restored
  by undoing one.
- **A cache beside `EditIndex`,** per shell: its sides, kind, box and volume; per
  region: its winning hint. Old shell membership comes from the cache, not the
  columns, because freed face slots get reused.

The steps:
1. **Shells:** union-find over the sides of the old shells containing a touched side,
   plus new sides. A shell with no touched side keeps its sides, since its wedges
   are unchanged and lead only to its own sides.
2. **Kind, volume and box:** recomputed for shells with a touched face, in the
   oracle's side order (the floating filter depends on summation order).
3. **Nesting:** re-tested for:
   - changed cavity and sheet shells;
   - cavity and sheet shells whose parent outer shell changed;
   - cavity and sheet shells whose box lies inside a changed outer shell's box (an
     O(S) box scan, or a box index).
4. **Votes and IDs:** recomputed for affected regions. `winners` and `carrier` become
   counts in the cache. A region whose winning hint's winner count crosses 1 ↔ 2 is
   renamed even if untouched.
5. **Containers:** re-resolved for users of hints whose resolution changed (the
   editor's hint index maps hint → users), and for vertices and edges whose own
   container hint was written.
6. **Undo:** the barrier's snapshot undo already costs about 2 ms after a split. It
   stays.

### 4.2 Indices sorted by ID (decision D1)

- **Today** shells are indexed by minimum side ID and regions are ordered by ID
  (design 01 §6). A new region gets a hashed ID, so it lands at a random position,
  and every later region index shifts. That shift touches `.f.region`, `.f.shell`,
  `.s.region`, every region container, and the editor's region-keyed index.
- **What it costs:**
  - **The columns:** an O(F) pass of plain writes, about 1–3 ms at 1.1·10⁵ faces.
  - **The editor's index:** this is the real cost. `EditIndex` keys `incl_`,
    `votes_` and `hint_regions_` by region index, in nested `std::map` / `std::set`
    (`editor_index.h`). After a merge, `refresh_regions` re-keys almost every row.
    That is the likely cause of the 90–150 ms undo after a merge barrier, against
    about 2 ms after a split (inferred from the code, not separately timed).
- **D1** (decided 2026-11-24: **(c)**):
  - **(a) Sorted indices, as today.** Stage B pays the O(F) column shift and the
    index re-key on most edits that add or remove a region.
  - **(b) Stable slots.** Shells and regions become ordinary slot domains, with free
    lists and generations, while editing. Only a full build, `compact` and `freeze`
    produce the sorted canonical order. Stage B becomes O(change). It changes design
    01 §6, and an edited model hashes differently from a fresh build until compacted
    or frozen.
  - **(c) Sorted indices, with an index that doesn't care.** Keep the canonical
    order. Key `EditIndex`'s region maps by region ID, or by flat arrays remapped
    through the old-to-new index permutation in O(R). The shift then costs only the
    O(F) column pass, with a small constant.
- **Recommendation: (c).** It keeps the data model and its oracle equivalence, and
  removes the real cost. The O(F) pass is a few ms at 10⁵ faces and tens of ms at
  10⁶. (b) would remove that too, at the price of a data-model change. (c)'s
  `EditIndex` rework also helps today's full barrier and its undo, so it is worth
  doing first whatever Stage B becomes.

### 4.3 Testing (when planned in full)

- The oracle makes it testable: after every barrier in randomized edit sequences,
  compare the incremental result with a fresh `build_regions` of the same complex
  (hash and report). Under (b), canonicalise first.
- Error equivalence: the incremental path must fail exactly when the oracle fails,
  with the same issues. A failure can fall back to a full build to produce the
  oracle's report.

## 5. Risks

- **R1 — Stage A changes output by accident**, for example in tie order, report
  order or carry-over. Mitigated by the A/B harness, and by the new tie, duplicate,
  carry-over and issue-order tests.
- **R2 — Stage A gains less than expected.** Each step is measured with the phase
  timers, and steps that don't pay are dropped.
- **R3 — Stage B's equivalence is subtle** (shared winning hints renaming untouched
  regions, nesting changes far from the edit, geometry-only edits, floating volume
  order). That is why it gets its own reviewed plan.
- **R4 — The litestl dependency.** UberTopo master can't take Stage A until the
  litestl helper's PR merges. If that's unwelcome, the helper can stay local to
  `regions.cc` until a second user appears; the owner decides.

## 6. Work items

- **A0:** the litestl CSR helper, with tests, on a litestl feature branch.
- **A1:** the changes of §3.1, one commit, with the A/B harness run recorded in §8.
- **A2:** the new tests of §3.3, mutation-checked.
- **A3:** before/after `bench_regions`; phase timers removed; docs: 01 §7's OI-P3-1
  line, the P3b plan's OI-P3-1 entry, 07's barrier row if its numbers change, the
  index, `docs/debugging.md` for anything non-obvious.
- **B0:** the `EditIndex` rework of option (c), measured on the merge barrier and its
  undo. Done in [2026-11-24](2026-11-24-edit-index-regions.md): the region-keyed state
  turned out not to be needed, so it was removed rather than remapped.
- **B:** a separate plan for incremental maintenance.

## 7. Review (fresh context, 2026-11-23, of v1)

- **Confirmed:**
  - the vote tie rule (ascending map scan with `n > best`, `Hash128` ordered hi then
    lo);
  - generations (both compare by index);
  - the order constraints on shell sides, and that vertex-set order doesn't matter;
  - the profile's reading: `face_side_id` is one SipHash `derive` per side;
  - Stage B step 1, for sides.
- **Findings, all adopted:**
  1. **Major:** sorting each region's raw hints would be slow for region 0, which
     carries about 10⁵ unbounded-hint sides. Now a linear distinct-hint array with a
     map fallback (§3.1.7).
  2. **Major:** Stage B ignored geometry-only edits, which change volumes, kinds,
     boxes and nesting without changing shell membership. "Touched" now includes
     faces incident to written vertices and edges, derived from coedges as well as
     faces (§4.1).
  3. **Major:** Stage B missed vertices and edges whose own container hint changed
     (§4.1 step 5).
  4. **Major:** D1 was framed unfairly. The cost of a shift is `EditIndex`'s nested
     maps, not the column writes. Option (c) added and recommended. Step 6 (journal
     undo) dropped: the snapshot undo is already cheap.
  5. **Minor:**
     - `unb` must stay in the vote; `HintConflict` counts it (§3.1.7).
     - `DuplicateHint` keeps final-index order (§3.1.8).
     - `resolve`'s lookup order is stated (§3.1.9).
     - `old_by_id` doesn't assume ID order and keeps last-wins (§3.1.11).
     - Compute DSU roots once (§3.1.2).
     - The CSR helper belongs in litestl (§3.1.12, R4).
     - An ID-collision assertion (§3.1.10).
     - The touched set is reset and restored with barriers (§4.1).
     - The undo cost was cited as 90–170 ms; the source says 90–150 ms.
     - The ambiguous-carrier test already exists.
- **Testing gaps, all adopted (§3.3):** carry-over and generations, an out-of-order
  old domain, the `unb` tie, a pinned issue sequence, the A/B harness across
  coordinate types and band counts, and three more mutations.

## 8. Owner decisions (2026-11-23)

- **Stage A:** implement now.
- **The CSR helper:** in litestl, per the rule (R4 accepted: UberTopo master waits for
  the litestl PR).
- **D1:** decided after Stage A's measurements.
  - **2026-11-24: (c).** Shells and regions keep their canonical sorted order. Stage B
    pays the O(F) column shift when a region is added or removed.
  - The editor half of (c) is done: B0 removed the region-keyed state, so nothing in
    `EditIndex` needs re-keying.

## 9. Implementation notes (Stage A, 2026-11-23)

- **A0:** litestl `util::group_by_key` / `KeyGroups` (`util/sort.h`), with a
  randomized test against a vector-of-vectors reference and reuse checks. 5 mutants,
  all killed. It is on litestl branch `ubertopo/group-by-key`. UberTopo's gitlink
  points there until the litestl PR merges.
- **A1, as planned,** with two additions:
  - per-face coedge counts size the flat per-shell vertex buffer up front;
  - `old_by_id` is built only when the region domain has custom columns, because
    nothing else reads it.
- **The A/B harness** was a temporary hook in `build_regions` (never committed).
  - With `UT_REGIONS_AB` set, it ran the pre-Stage-A code on a copy, and the new
    code at 1, 2, 4 and 12 bands. It aborted on any difference in the report (issues
    in order, counters) or in anything `build_regions` writes: face regions and
    shells, shell chains, containers, the shell and region domains with their
    generations and floors.
  - `node tools/check.mjs` passed with it on, on clang-cl Release and MSVC ASan:
    every test, every coordinate type, every region build.
  - A tie-rule change planted to check the hook was caught.
  - It compared columns, not `write()` bytes: `write()` refuses the test complexes
    whose geometry store is deliberately invalid.
- **A2 tests** (`test_topo_regions`):
  - `test_votes_and_order`:
    - a two-hint tie, and `unb` ties on both sides of `unb` (including the carrier
      a container resolves through);
    - more than 8 hints in one region, in one shell with fixed face order so the
      winning hint is counted through the map;
    - three winners of one hint, and two hints with two winners each (`DuplicateHint`
      in final order);
    - a minority hint carried by two regions;
    - the issue sequence of a failed build.
  - `test_carry_over`: a renamed region, with carry-over (region 0 included) and
    generations; an old domain out of ID order; a duplicated old ID (the last row
    wins).
- **Mutations:** 13, 12 killed. The survivor, vertex sets left undeduplicated, only
  changes which candidate test vertex nesting tries. Any valid test point gives the
  same result, so it is effectively equivalent.
- **Results** (`bench_regions`, 1.1·10⁵ faces, old and new binaries run alternately
  on one machine; this machine was slower than in §2):

  | | before | after |
  |---|---|---|
  | `build_regions` | 223–257 ms | 83–87 ms |
  | barrier after a split, `None` | 244–247 ms | 105–106 ms |
  | barrier after a split, `Local` | 269–278 ms | 133–147 ms |
  | barrier after a merge, `None` | 401–430 ms | 245–341 ms |
  | undo of the merge barrier | 165–207 ms | 188–276 ms |

  - `build_regions` gained 2.6–3×, more than §3.2 expected.
  - The merge barrier and its undo are now dominated by `EditIndex::refresh_regions`
    re-keying region-indexed rows, which Stage A doesn't touch. That is B0's
    target (option (c) of D1).
  - At 1.1·10⁴ and 3.7·10⁴ faces `build_regions` went from 14 to 8 ms and from 49
    to 27 ms.
