# Plan: an EditIndex without region-keyed state (B0)

Status: **implemented** 2026-11-24 (§9). v2, after a fresh-context review (§8).

## 1. Why and scope

- **The need.** After [2026-11-23](2026-11-23-region-maintenance.md) Stage A, the
  `rebuild_regions` barrier after a merge, and its undo, cost more than
  `build_regions` itself. That plan blamed `EditIndex::refresh_regions` re-keying
  region-indexed rows (its work item B0), but had not timed it separately.
- **Measured** (2026-11-24, temporary timers, `bench_regions`, 1.1·10⁵ faces,
  clang-cl Release; ms):

  | phase | after a split | after a merge |
  |---|---|---|
  | barrier: snapshot | 0.1 | 0.1 |
  | barrier: `build_regions` | 92–117 | 100–128 |
  | barrier: validation (`Local`) | 26–35 | 22–50 |
  | barrier: commit, collect | 0.0 | 0.0 |
  | barrier: `refresh_regions` | 1.0–1.6 | 1.2–310 (median about 150) |
  | undo: restore the snapshot | 0.2–1.0 | 0.3–1.3 |
  | undo: `refresh_regions` | 1.0–2.2 | 1.0–305 (median about 180) |

  `refresh_regions` is the whole excess. Its O(N) comparison scan is 1–2 ms; the rest
  is map work for the faces whose region index shifted.
- **Goal:** remove that map work. Same answers to every query, same edit results.
- **Not in scope:** the maps that aren't keyed by region (`ids_`, `users_`,
  `carriers_`, and `incl_` for faces); `build_regions`; the validator; decision D1's
  other half (how Stage B would maintain sorted indices). This plan is what option (c)
  needs, and it helps under (a) and (b) as well.

## 2. What the region-keyed state is for

- **`incl_` with a region container is never queried.** `EditIndex::inclusions` has
  one caller, `Editor::inclusions(FaceRef)`.
- **`votes_` and `hint_regions_` have one reader:** `embed_impl` with a region target,
  through `winning_hint(r)` and `other_winner(r, h)`. It asks only for `r ≠ 0` and only
  while regions are clean (`RegionsDirty` otherwise).
- **While regions are clean, the votes are fixed by the last region build.** They are a
  function of `.fs.hint` and `.f.region` over live faces.
  - Every primitive that writes `.fs.hint`, or allocates or frees a face, calls
    `set_dirty()`. Nothing in `topo/edit` writes `.f.region` except by restoring rows
    or snapshots, and `compact`'s remap.
  - `regions_dirty` returns to false only by a barrier, or by a rollback or undo that
    also restores every row written since. So at any clean moment the hints and
    regions equal those right after the last barrier, or barrier undo.
- So the votes need no incremental maintenance. A cache validated by the version stamps
  of those two columns is enough. It is right even where the reasoning above has a
  hole, such as a write from outside the editor, and the reasoning only says that it is
  rarely rebuilt.

## 3. Changes (`topo/edit/`, and the vote in `topo/hint_votes.h`)

1. **`incl_` holds face containers only.** `ElemKey::c` stores the container if it is a
   face and none otherwise, so a region container never reaches the map.
2. **`votes_`, `hint_regions_`, `FaceKey::region` and `face_regions` go.** In their
   place, a cache of its own, `EditRegionHints` (`editor_region_hints.h`), held by the
   editor:
   - `winner_[r]`: the winning hint of region `r ≥ 1`, or `kNoId` (no hinted side, or
     the unbounded hint wins);
   - `wins_[h]`: a `util::Map` from hint to the number of regions `r ≥ 1` it wins;
   - `winner(k, r)` and `other_winner(k, r, h)` bring it up to date and then answer in
     O(1): `other_winner` is `wins_[h]` minus one if `winner_[r] == h`, > 0.
3. **The build** is O(F): `util::group_by_key` over the `2F` sides, keyed by region.
   Dead faces, unhinted sides, region 0 and any region not below `regions.capacity()`
   (NONE, or an invalid complex) don't vote. Each region's vote is counted by
   `HintVotes`, factored out of `build_regions`, so there is one copy of the tie rule.
   The unbounded hint takes part in the vote, and if it wins the region has no winner.
4. **The cache is valid** while the version stamps of `.fs.hint` and `.f.region`, and
   the region capacity, are those it was built from. Nothing invalidates it. Face
   liveness needs no stamp, since freeing a face resets its side hints.
5. **`refresh_regions`** becomes `refresh_containers`, which:
   - flushes, as now;
   - re-keys the elements whose *face* container changed. Only a hinted element's
     container changes in `build_regions`, and only to a region, so this matters for a
     hinted element that had a face container (not something the editor makes, but a
     loaded complex may hold one). The scan compares elements with a hint, and is
     skipped when `users_` is empty.
6. **`same_as`** compares what remains. The cache has its own check (§4).
7. **C13 (P3b) said barriers rebuild indices eagerly.** The votes become lazy: one O(F)
   pass on the first region embed after a barrier. A rolled-back or undone face edit
   also costs one, since restoring a face row writes all of its columns. Most barriers
   are never followed by a region embed. Recorded in the P3b plan next to C13.
8. **Not chosen: no cache at all.** Walking a region's sides until one carries the hint
   its ID is derived from (DEC-017) needs no state, but costs O(region) per refused
   embed and relies on the IDs of a loaded complex following DEC-017.

## 4. Testing

- **Hook:** `Editor::region_hints_consistent()`. If the cache claims to be current it is
  compared with a fresh one; otherwise it is brought up to date, so later steps have a
  warm cache to check. `test_edit_random` calls it after every step and after each undo
  and redo of the final passes. It draws nothing from the rng, so the pinned goldens
  don't move. It detects staleness only: both sides come from the same builder.
- **Oracle test** (new file, `test_edit_region_hints`): a row of rooms with random
  hints on every side, including the unbounded hint, unhinted sides, and rooms' hints
  facing region 0. Every region is embedded into (mode `None`, rolled back) and checked
  against a brute-force count, and against `build_regions` itself: an embed succeeds
  exactly when the region's ID is `region_id_from_hint(winner)`. Repeated through hint
  changes, walls removed, rebuilds, undo and redo. No earlier test covered
  `UnhintedRegion`.
- **The rules spelled out:** ties, ties with the unbounded hint on both sides of it, its
  majority, unhinted sides.
- **Cache lifetime:** a rolled-back transaction, a failed barrier, a merge with undo-all
  and redo-all, `compact`, and outside writes of each stamped column (one region index
  out of range).
- **A face container with a hint,** vertex and edge: after the outside write, a
  primitive flushes the index first, or the barrier would rebuild it and never reach the
  scan. Then barrier, undo, again, and edits on the re-keyed row.
- **Mutations:** §9.
- **Bench:** `bench_regions` before and after, with old and new binaries run
  alternately; plus, reported once, the first region embed after a barrier (the moved
  cost).

## 5. Expected result

- `refresh_regions` at 1.1·10⁵ faces: 1–310 ms → under 2 ms (near 0 with no hinted
  elements). Merge barrier in `None` mode ≈ `build_regions` + 2 ms; its undo ≈ 1–3 ms,
  as after a split.
- The first region embed after a barrier pays a pass over the faces: about 5 ms
  expected (measured: 11–16 ms, §9).
- The index's eager rebuild (attach, compact) also gets cheaper, since it drops two
  nested maps.

## 6. Risks

- **R1 — embed-heavy workloads with a barrier between every embed** pay the O(F) pass
  each time, about a tenth of the barrier. The same holds for a face edit undone or
  rolled back between embeds, without a barrier to hide it.

## 7. Work items

- **W1:** the change, the hook, the tests, mutations.
- **W2:** bench before/after; docs: the 2026-11-23 plan's B0 entry and §4.2 numbers,
  P3b C13 and OI-P3-1, design 03/07 rows if they name the region-keyed maps, the
  index, `docs/debugging.md` for anything non-obvious.

## 8. Review (fresh context, 2026-11-24, of v1)

Verdict: sound. The reviewer audited every writer and found no sequence through the
editor's API that makes a warm cache stale.

- **Confirmed:**
  - region inclusions are never queried, and the votes have one reader;
  - the §2 invariant, for every primitive, rollback, undo, redo and failed barrier;
  - the semantics against `winning_hint`, `other_winner` and `build_regions`;
  - the scan of hinted elements is complete in both directions, and the skip is right;
  - goldens can't move.
- **Findings, all adopted:**
  1. **Major:** `group_by_key` would write out of bounds if a complex's `.f.region` is out
     of range (loaded, or written from outside). Bounded, and tested (§3.3).
  2. **Major (test):** the face-container test was vacuous in the barrier direction,
     since an outside write makes the barrier rebuild the index (§4;
     `docs/debugging.md`).
  3. **Major (test):** the hook after the undo-everything pass checked nothing. It now
     runs after each undo and redo.
  4. **Design:** validate the cache by column version stamps instead of invalidation
     sites. This removed the plan's main risk (§3.4).
  5. **Minor:** some of v1's mutations were equivalent (§9).
  6. **Minor (test):** oracle gaps: region-0-facing hints, unbounded majorities, mode
     `None`, and `build_regions`' IDs as a second oracle.
  7. **Minor:** one region can hold most of the sides, so the per-region sorts were not
     small. The vote counter of `build_regions` is now shared.
  8. **Minor:** the hook detects staleness only; stated in §4.
  9. **Minor:** wording (nothing in `topo/edit` writes `.f.region`) and more docs to
     update.
  10. **Minor:** implementation details (`lookup_ptr`, flush before reading `users_`).

## 9. Implementation notes (2026-11-24)

- **As planned** (§3).
- **Results** (`bench_regions`, 1.1·10⁵ faces, old and new binaries run alternately
  three times on one busy machine; ms):

  | | before | after |
  |---|---|---|
  | barrier after a split, `None` | 122–146 | 135–147 |
  | barrier after a merge, `None` | 305–364 | 128–140 |
  | barrier after a merge, `Local` | 419–480 | 145–178 |
  | undo of the split barrier | 2.0–2.4 | 0.6–0.9 |
  | undo of the merge barrier | 221–252 | 0.4–0.8 |
  | first region embed after a barrier | — | 11–16 |

  - A merge barrier now costs what a split barrier does. Both are `build_regions` plus,
    in `Local`, the region checks.
  - The split barrier's own difference is within the noise: its refresh was 1–2 ms.
  - The first region embed after a barrier costs more than the 5 ms expected: 0.6 ms at
    1.1·10⁴ faces, 2 ms at 3.7·10⁴, then 11–16 ms at 1.1·10⁵. Reserving the winner map
    changed nothing. It grows faster than the face count, which suggests cache misses; it
    wasn't profiled further.
- **Mutations:** 27. 24 killed in Release.
  - The 25th, `refresh_containers` without its flush, is killed only by ASan (the stale
    rows are read past the end of a shrunk column; Release returns garbage it then
    discards). The test for it (3000 hinted vertices undone across a barrier) was added
    after the mutant first survived.
  - One mutant, the first map index of the vote counter, survived until `HintVotes` got
    a test of its own. It is among the 24.
  - Two are equivalent: counting dead faces (freeing a face resets its side hints), and
    keeping the vote counter in map mode after `clear()` (it counts the same).
