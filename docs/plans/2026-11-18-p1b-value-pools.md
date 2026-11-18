# Plan P1b — litestl value pools (`attr/` pools, pool store, ref columns)

- **Status:** **implemented** (see §11). Plan **v3** (v2 + the normative §10c amendments, after the v2 re-review in §10b said "ready with changes"). v2 was rewritten after the v1 review (§10) said "needs rework". The owner
  chose **mark-and-sweep collection over refcounts** (2026-11-18), reversing design
  05's refcount funnel.
- **Date:** 2026-11-18
- **Design:** [core-data-model/05](../design/core-data-model/05-geometry-pools.md) (status
  note and §2), [03](../design/core-data-model/03-attributes.md),
  [04 §5–§8](../design/core-data-model/04-identity-versions-journal.md). Phase P1b in
  [06 §3](../design/core-data-model/06-modules-litestl-and-phasing.md). The requirements
  come from the [P1 plan](2026-11-18-p1-attribute-core.md)'s §2 and its review findings
  2, 3, 15 and 16.
- **Repos:**
  - Code goes in **litestl**, on branch `ubertopo/p1b-pools`, branched from master
    `ccf0386`.
  - UberTopo gets the submodule bump, an owner-pattern smoke test and the doc updates,
    on branch `session/p1b-value-pools` (from master `787c00c`).
- **Decisions in force:** DEC-002, DEC-007 (open system, per-column virtual dispatch,
  no central switch), DEC-009, DEC-010, DEC-012, the 40 KB rule. New decisions are
  recorded as **DEC-015** (W8).

## 1. Goal

Generic value pools in litestl `attr/`, which P4's `GeometryStore` and any interned
attribute (material names, tags) build on:

1. **`InternPool<T>`:** immutable, content-deduplicated entries of any size. `T` may own
   heap memory (curve segment lists, knots, polylines, meshes).
2. **`PoolStore`:** a *value type* owning several pools.
   - Copying it is a snapshot, O(pages). Snapshots diverge freely; the immutable entry
     nodes are shared between them.
   - One ref column can reference several pools (design 05's multi-kind `GeomRef`).
3. **Ref columns are plain `Column<PoolRef>`.** There is no funnel and no binding.
   Columns that hold refs expose them through two `ColumnBase` virtuals, so custom
   column classes can hold refs too (DEC-007).
4. **Liveness by mark-and-sweep.** `collect(roots)` frees every slot unreachable from
   the roots: ref columns of the given groups, extra roots such as P3's journal, and
   refs inside reachable entries. The owner calls it at points it chooses
   deterministically.
5. **`audit(roots)`**, **canonical renumbering** (`canonicalize`) and **canonical
   serialisation** of the hashed closure.
6. **Cross-store `import`** (deep re-intern) for merging and composition (P3, P4).
7. **A copy-on-write deduplication index** that scales to ≥ 10⁵ entries, with a
   benchmark.

## 2. Non-goals and deferrals

- **UberTopo integration** is **P4**: a `PoolStore` member of `Complex`,
  `GeometryStore`, geometry kinds, `GeomRef` columns, and choosing the collect points
  in the primitives. P1b proves the owner pattern with a test owner (W7), and design 07
  records the contract.
- **The journal** is P3. P1b gives it the `RefRoot` interface: a journal is one more
  root, so captured ref bytes stay valid as long as the journal holds them.
- **Non-interned adds.** Every add deduplicates (§3.4).
- **Parallel pool mutation.** A store is mutated by one thread at a time. Parallel
  builders collect their values and intern them serially in canonical order, or
  canonicalise afterwards.
- **Pool-id remapping on read.** Pool ids are the owner's registration order (§3.5).
  Remapping by key can come with a real plugin system.
- **Packed 4-byte refs**, and N-API/TS exposure.

## 3. Design

### 3.1 Baseline (verified 2026-11-18, litestl `ccf0386`; confirmed by the v1 reviewer)

- **`ColumnBase` virtuals carry no context.** v2 doesn't need any: no column operation
  touches a pool.
- **`AttrGroup`'s copy calls `share()`.**
- **`AttrGroup::read`** stages into a fresh group, checks the trailing `content_hash`
  (`group.cc:461`), and move-assigns.
- **`AttrGroup::canonical_order()`** (builtins in slot order, then dynamic columns by
  name) is **private**, and W4 exposes it.
- **`PagedStore<T>`** needs trivially copyable, padding-free `T`. Its pages are
  `util::RcBuffer<T>`, which has no per-element hooks.
- **UberTopo's `read`** builds a fresh complex and move-assigns it on success
  (`topo/io.cc:101,137`). Pool reads follow the same pattern.

### 3.2 Liveness and memory

- **A slot is live iff it holds a node.** `intern` makes a slot live, and only
  `collect` / `canonicalize` free slots.
- **Between collects, unreachable entries ("garbage") stay live.** `intern` may
  resurrect one: it is equal and immutable, so this is harmless. The pool counts the
  slots taken since the last collect (`taken_since_collect()`), so an owner can apply a
  threshold policy (e.g. collect when it exceeds `max(1024, live / 2)`).
- **Nodes are shared between snapshots by an atomic `holders` count,** the number of
  slot-table page buffers that point at the node. It works like `RcBuffer`'s count.
  - copy-on-write of a slot page increments the `holders` of each node in it;
  - the last release of a page decrements them;
  - freeing a slot in a unique page decrements its node's count.

  A node is freed at 0.
- **Lifetime of `const T &`** from `get`: it is valid until the next `collect`,
  `canonicalize`, assignment or destruction of *that* store. Other snapshots can't
  affect it.
- **Rooting rule.** A `PoolRef` held anywhere other than a root (a local variable
  between `intern` and the assignment into a column, say) must not be held across a
  `collect`. Owners only collect at operation boundaries.
- **Forgotten roots.** If an owner forgets a root, `collect` frees an entry that is
  still referenced. A later `audit` or `collect` reports the dangling ref, unless the
  slot has been reused for another value in the meantime. For tests,
  `set_reuse_freed(false)` (a runtime option, off by default) quarantines freed slots,
  so every dangling ref stays detectable. It is not tied to `LITESTL_ATTR_CHECKS`,
  because slot numbering must not differ between build configurations.

### 3.3 `InternPool<T>` storage (per snapshot)

- **Slot table:** a paged copy-on-write array of `uintptr_t` slot words.
  - A live slot holds a `Node *` (low bit 0).
  - A free slot holds `(next_free << 1) | 1`.
  - A quarantined slot holds `~uintptr_t(0)`.
  - Pages are `detail::SlotPage`: intrusive, atomically refcounted, alloc-tracked, with
    the node hooks of §3.2.
  - **Page size: 2¹⁰ slots** (8 KB on 64-bit), benchmarked in W6.
- **Free list:** LIFO, with the head in the pool. `collect` pushes freed slots in
  descending index order, so the lowest index is reused first. This is deterministic.
- **Index:** content hash → slot.
  - Open addressing with linear probing over a `PagedStore<IndexRec>` with page shift
    8 (4 KB pages), copy-on-write between snapshots.
  - `IndexRec { uint64_t h_lo; uint32_t h_hi32; uint32_t slot_plus1; }` is 16 bytes and
    padding-free. 0 means empty; `0xffffffff` is a tombstone.
  - The capacity is a power of two, ≥ 16. It doubles when live + tombstones exceed
    3/4, and is rebuilt at the same capacity when tombstones exceed 1/4.
  - A hash hit is confirmed with `PoolTraits<T>::equal`.
- **Node:** `Node { std::atomic<uint32_t> holders; hash::Hash128 hash; T value; }`,
  through `alloc::New`, immutable once published.
- **Entry hash:** `Hasher128` with a domain tag `"litestl.attr.pool.v1"` followed by
  `PoolTraits<T>::hash`.

### 3.4 Entry values: `PoolTraits<T>`

Every pool value type declares whether it holds refs:
`PoolTraits<T>::has_refs` must be defined, **so a struct holding a `PoolRef` can't be
silently treated as raw bytes** (v1 finding 7).

**Built-in traits:**
- arithmetic types, `math::Vec<E, N>` and `std::array<E, N>` of arithmetic `E`: raw
  little-endian bytes, `has_refs = false`, bitwise `equal`;
- `util::string`: `has_refs = false`.

**Every other type** goes through member functions, and gets a compile error if one is
missing:
- `void hash_into(hash::Hasher128 &) const`;
- `bool equal(const T &) const`, which **must be bitwise for floats** (it is not
  `operator==`, because −0.0 and NaN would disagree with the hash);
- `void write(io::ByteWriter &) const`;
- `static bool read(io::ByteReader &, T &, const PoolReadLimits &)`;
- `static constexpr bool has_refs`;
- if `has_refs`, also `void for_each_ref(F &&) const` (calls `f(PoolRef)`) and
  `void remap_refs(F &&)` (calls `f(PoolRef) → PoolRef`). A `static_assert` checks this.

**Every add interns.** `intern(v)` returns the existing live slot when an equal entry
exists, and otherwise takes a slot. Within a pool, ref equality is value equality.
Inner refs of a new entry must be valid in the same store, or `intern` aborts.

### 3.5 `PoolRef`, `RefRoot` and `PoolStore`

```cpp
namespace litestl::attr {
struct PoolRef { uint32_t pool; uint32_t slot; };   // 8 bytes; pool == 0 means none
inline constexpr PoolRef kNoRef{0, 0};              // {0, s != 0} is malformed everywhere

struct RefVisitor { virtual void visit(PoolRef r, uint64_t count) = 0; };  // count: Constant pages
struct RefRemap   { virtual PoolRef map(PoolRef r) const = 0; };

class RefRoot {     // external holders of refs (P3 journal, tests)
public:
  virtual void visit_refs(RefVisitor &v) const = 0;
  virtual void remap_refs(const RefRemap &m) = 0;
};

class PoolStore {   // value type: copy = O(pages) snapshot; movable
public:
  template <typename P> uint32_t add_pool(std::string_view key);   // ids 1, 2, ... in call order
  template <typename P> P &pool(uint32_t id);                      // checked class tag
  template <typename P> const P &pool(uint32_t id) const;
  PoolBase &pool_any(uint32_t id);
  uint32_t pool_count() const;

  template <typename P, typename V> PoolRef intern(uint32_t pool_id, V &&v);
  template <typename P> PoolRef find(uint32_t pool_id, const typename P::value_type &v) const;
  template <typename T> const T &get(PoolRef r) const;             // checked
  bool live(PoolRef r) const;

  struct Roots {    // in the owner's canonical order
    util::span<const AttrGroup *const> groups;
    util::span<const RefRoot *const> extra;
  };
  struct MutRoots {
    util::span<AttrGroup *const> groups;
    util::span<RefRoot *const> extra;
  };

  bool collect(const Roots &roots, const char **err = nullptr);   // no change on failure
  uint64_t taken_since_collect() const;
  bool audit(const Roots &roots, bool require_reachable,
             util::Vector<util::string> *problems = nullptr) const;
  bool canonicalize(const MutRoots &roots, const char **err = nullptr);
  bool canonical() const;      // no store mutation since the last canonicalize

  PoolRef import(const PoolStore &src, PoolRef r, ImportMemo &memo);   // deep re-intern by key

  void hash_into(hash::Hasher128 &h) const;   // requires canonical()
  bool write(io::ByteWriter &w) const;        // requires canonical(); false otherwise
  bool read(io::ByteReader &r, const PoolReadLimits &lim = {}, const char **err = nullptr);
                                              // into a store with pools registered and all empty

  void set_reuse_freed(bool);                 // test quarantine (§3.2)
};
}
```

- **Fixed layout.** Pools are registered in a fixed order when the owner is
  constructed, like builtin columns. Ids are 1-based registration indices; P4's
  `GeomRef.kind` is the pool id. New kinds are appended. `read` requires each stream
  pool *i* to have the key of local pool *i*. The local store may have extra pools at
  the end, so older streams stay readable.
- **Value semantics.** `PoolStore` copies, moves and assigns like an `AttrGroup`.
  Nothing points into it, so an owner such as P4's `Complex` keeps its defaulted copy
  and move.
- **`import`** copies an entry from another store, deep-copying its inner refs, with
  pools matched by key. The memo makes shared sub-entries import once. Merging owners
  and the cross-owner `mix_from` in P3 must go through it: **a raw ref copied between
  stores is meaningless**, and the column layer can't detect it. This is documented as
  an owner rule.

### 3.6 Ref columns and the `ColumnBase` additions

- **Values.** `Column<PoolRef>` is an ordinary column. `ValueKey<PoolRef>` = `"ref"`,
  the `ValueOps` "other" family (CopyFirst / SetDefault / Skip), default `kNoRef`.
- **Three new `ColumnBase` virtuals**, with no-op defaults:
  - `bool holds_refs() const`;
  - `void visit_refs(RefVisitor &) const`, page-aware: one call per Constant page, with
    `count`;
  - `void remap_refs(const RefRemap &)`, which keeps uniform pages uniform.
- **`Column<T>`** implements these when `T` is `PoolRef`, via `if constexpr`. Custom
  column classes that store refs override them, as P1's `TestPairColumn` overrides
  `mix_custom`. This is the open-extension path.
- **A `Column<UserStruct>` with an embedded `PoolRef`** would be invisible to
  `collect`. This is documented. The model test shows the failure mode, and the
  quarantine mode exposes it.
- **Defaults.** A ref column's default must be `kNoRef`. `audit`, `collect` and
  `canonicalize` reject any other default.
- **No funnel, no binding.** The whole `ColumnBase` structural API is unchanged:
  resize, swap, permute (including the parallel per-column permute at freeze),
  `set_default_*`, mix, `mix_from`, share, `new_like`, `get_bytes` / `set_bytes` and
  `read_data`.

### 3.7 `collect`, `audit` and `canonicalize`

**Mark** (shared by all three):
- Visit every column with `holds_refs()` in the root groups, Temp and Derived included,
  then the extra roots.
- Then take the closure through the inner refs of marked entries, using a worklist and
  one mark bitmap per pool. Cycles (possible only in a crafted stream) are harmless.
- **Validation during the mark:** every visited ref must be `kNoRef` or a live slot of
  an existing pool. `{0, s ≠ 0}` is malformed. Otherwise the call fails with the first
  offending ref and changes nothing.

**`collect`** marks, then sweeps each pool in ascending slot order. Each unmarked live
slot is:
- removed from the index;
- freed (after making its page unique), and its node's `holders` is decremented;
- pushed onto the free list (or quarantined).

**`audit(roots, require_reachable)`** marks without mutating, then checks:
- every root and inner ref is valid;
- if `require_reachable`, every live slot was marked;
- the free list holds exactly the free slots, with no loop;
- the index maps exactly the live slots, with hashes equal to the nodes';
- no two live slots of one pool are equal;
- every node reachable from a page has `holders ≥ 1`;
- every ref column default is `kNoRef`.

**`canonicalize(roots)`** renumbers in order of first reference, in **three phases,
each followed by its own breadth-first inner-ref closure** (v1 finding 6):
1. **Hashed columns:** root groups in the given order, each group's columns in
   `canonical_column_order()`, elements ascending.
2. **Temp and Derived columns,** in the same order.
3. **Extra roots.**

Then it proceeds as follows:
- **Order.** Each pool numbers its slots from 0. The slots numbered in phase 1 form
  the **hashed closure**, and each pool records its size `k`. Unreachable slots are
  dropped, so `canonicalize` implies `collect`.
- **Rewrite.** Columns are rewritten through `remap_refs`, extra roots through
  `RefRoot::remap_refs`, and inner refs through `PoolTraits::remap_refs`. A node whose
  inner refs change is replaced by a new node, because nodes are shared with other
  snapshots.
- **Rebuild.** Each pool is rebuilt compact (no free slots), with fresh slot pages,
  and its index is rebuilt.
- **Record.** It records the store's mutation stamp, so that `canonical()` holds until
  the next `intern`, `collect` or `read`.

### 3.8 Serialisation and hashing

- **Canonical content is the hashed closure only** (v1 finding 5).
  - **`write`** emits the format version and pool count, then per pool the key, `k`,
    and the `k` entries (`PoolTraits::write`), then a trailing `content_hash`. It
    returns `false` unless `canonical()`.
  - **`hash_into`** hashes the same content in the same order (key, `k`, then entry
    hashes) and aborts unless `canonical()`.
  - Entries reachable only from Temp, Derived or extra roots are never written or
    hashed, so the bytes don't depend on caches, undo history or sibling snapshots.
- **Owner write** = copy the owner (O(pages)), `canonicalize` the copy with its groups
  as roots and no extras, then write the store followed by the groups. The copy's Temp
  and Derived columns may reference slots ≥ `k`. That is fine: their data isn't
  written.
- **Owner content identity** is the canonical copy's store hash plus the group hashes.
  A group hash on its own hashes slot numbers and is **not** a content identity (v1
  finding 16).
- **`read`**, into a fresh store with pools registered and all empty:
  - checks the version, the key prefix, and limits
    (`PoolReadLimits { max_slots, max_entry_bytes }`, each length checked against
    `remaining()`);
  - checks that entries read cleanly, with no duplicate values in a pool, and that inner
    refs point at in-range slots of existing pools;
  - checks the trailing hash.

  **Owner read:**
  1. build a fresh owner;
  2. `store.read`, then each group's `read`;
  3. `audit(roots, require_reachable = true)`;
  4. move-assign on success.
- **Fuzz harness.** The trailing hash would reject almost every random corruption, so
  the fuzz harness re-signs the hash after mutating (W5).

### 3.9 Threading

- Distinct snapshots may be used on different threads at once. Nodes and slot pages are
  shared through atomic counts, and nodes are immutable after publication.
- **Per store:** one writer, or concurrent readers only. `collect`, `canonicalize` and
  `intern` are writes.

## 4. Work items

### W1 — `ColumnBase` ref virtuals; `ValueKey<PoolRef>`; `AttrGroup::canonical_column_order()` public

- **Acceptance:** all existing attr tests pass, and the `test_attr_digest` golden
  `eb4c52c53316afaf23dbdc3cd35d0630` is unchanged.

### W2 — `attr/pool.h`: `PoolRef`, visitors, `PoolTraits`, `SlotPage`, `InternIndex`, `PoolBase`, `InternPool<T>`

- If the file passes 40 KB, split it into `pool_slots.h`, `pool_index.h` and `pool.h`.

### W3 — `attr/pool_store.h/.cc`

- `PoolStore`, `RefRoot`, mark, `collect`, `audit`, `canonicalize`, `import`, I/O and
  quarantine.

### W4 — `Column<PoolRef>` ref virtuals; `"ref"` registered among the builtin column types

### W5 — Tests (litestl)

**`test_attr_pool.cc` (unit):**
- intern dedup and resurrection of garbage;
- variable-size entries: `Polyline { util::Vector<double3> pts; PoolRef source; }` with
  `has_refs`;
- `util::string` interning;
- a 10⁴-long inner-ref chain collected iteratively;
- `collect` failure on dangling refs and on `{0, s ≠ 0}`;
- the quarantine mode catching a forgotten root, and a `Column<UserStruct>` holding an
  embedded ref;
- `import` with shared sub-entries;
- `read` rejection: key mismatch, duplicates, dangling inner refs, truncation, oversize
  lengths;
- **structural fuzz:** parse a valid stream, mutate the fields (counts, keys, lengths,
  ref values, entry bytes), **re-sign the trailing hash**, and require a clean
  `false`, with no crash, UB or leak (v1 finding 12b).

**`test_attr_pool_model.cc` (long, model-based):**
- **Reference model:** per snapshot, a `std::map<slot, value>` per pool, plus plain
  `std::vector<PoolRef>` columns. The model computes reachability itself.
- **Random operations** over 3–4 test owners (a store, two groups and an extra root):
  - intern (hits, misses, resurrections);
  - every structural column operation, through the group and directly;
  - adding and removing dynamic ref columns, Temp columns included;
  - snapshot, destroy and assign owners;
  - `collect` (at random points), `canonicalize`, `import` between owners;
  - write/read round trips into fresh owners.
- **After every step,** compare the values seen through refs with the model.
- **Every 16th step,** `audit` must pass.
- Slot page shifts 2 and 10 (a test-only template parameter) and column page shifts 2
  and 12. Honours `LITESTL_TEST_SCALE_DIV`.

**Canonical form** (v1 finding 12a): owners with the same *hashed* final content give
**identical bytes and store hashes** after `canonicalize`, even though they differ in:
- insertion order and release order;
- the garbage present and when collects ran;
- Temp and Derived contents;
- extra roots;
- sibling snapshots.

A pinned golden goes into a new `test_attr_pool_digest`.

**TSan:** a base owner; N threads each copy it and diverge (intern, collect,
canonicalize, destroy). The base must be unchanged at the end, and
`alloc::getMemorySize()` must return to its baseline (v1 finding 17).

**Mutation checks** (plant, confirm the failure, revert, and record the counts in §11):
1. mark skips inner refs;
2. no copy-on-write before freeing a slot in a shared page;
3. no `holders` increment on page copy;
4. index entry not removed on sweep;
5. canonicalize visits Temp before the hashed closure;
6. `Column<PoolRef>::remap_refs` ignores Constant pages.

### W6 — Benchmark: `bench_attr_pool`

- **At 10³, 10⁴, 10⁵ and 10⁶ entries:**
  - intern miss, intern hit, and `find` hit and miss (ns/op);
  - copy-owner time;
  - "copy, then intern and assign 1% new entries": time and bytes copied (the
    procedural-cache workload);
  - `collect` and `canonicalize` time;
  - `write` throughput.
- **Slot page sizes 2⁸, 2¹⁰ and 2¹²:** pick by the P1 rule (within 10% of the best on
  intern hit, then the least copy-on-write amplification).
- **Acceptance:**
  - intern hit at 10⁵ is ≤ 3× the time at 10³;
  - copy time at 10⁶ is ≤ 15× the time at 10⁵;
  - `collect` at 10⁶ refs is under 50 ms (clang Release).

### W7 — UberTopo: submodule bump and owner-pattern smoke test

`source/ubertopo/tests/test_smoke_pools.cc` defines a test owner: a `TypedGroup` whose
schema has `AttrDef<".e.curve", attr::Column<attr::PoolRef>>`, and a `PoolStore`
holding two pools (polylines with inner refs, and names). It:
- uses the **defaulted copy and move**;
- exercises diverging snapshots, `collect`, `canonicalize`, owner write and read
  (fresh owner, audit, move-assign), and `import`.

### W8 — Docs and integration

- **litestl:** `documentation/attributes.md` gets a pools section: the owner pattern,
  roots and the rooting rule, collect points, canonical write, import, threading. Add a
  one-liner to litestl `CLAUDE.md`.
- **UberTopo docs:**
  - design 05: replace the status note and §2 with the realised model (mark-and-sweep,
    not a funnel), and note that `GeomRef` becomes a `PoolRef` whose pool is the kind
    id;
  - design 03: a pools pointer;
  - design 04: §6 (the journal is a `RefRoot`) and §8.4 (canonicalise before
    serialising);
  - design 06: the P1b row;
  - design 07: the owner contract for P4;
  - `decisions.md` DEC-015;
  - `docs/index.md`;
  - `docs/debugging.md`, for anything non-obvious found.

## 5. Order and dependencies

W1 → W2 → W3 → W4 → W5 (alongside W2–W4) → W6 → W7 → W8 → configuration matrix.

## 6. Acceptance criteria

- **Configuration matrix:** all litestl and UberTopo tests pass in every configuration
  (run in WSL Debian; see §7):
  - **UberTopo:** clang Release, gcc Release, clang `-march=x86-64-v3`, gcc Debug+ASan,
    gcc Debug+UBSan, gcc TSan with `-DLITESTL_ATTR_CHECKS=1`;
  - **litestl standalone:** the same, plus clang `-DLITESTL_NO_INT128=ON`, with
    `LITESTL_TEST_NO_BREAK=1`;
  - sanitizer runs use `LITESTL_TEST_SCALE_DIV=4`.
- **Goldens:** the P0, P1 and P2 goldens are unchanged, and the new pool digest matches
  everywhere.
- Every W5 mutation check is caught.
- The W6 bounds are met.

## 7. Verification commands

- **Environment:** the matrix runs in WSL Debian (clang, gcc, cmake and ninja from
  apt) against `/mnt/c/dev/UberTopo`, with build directories under the WSL home.
- **litestl:** `cmake -S source/litestl -B ~/b/l-<cfg> -G Ninja <flags>`, then
  `cmake --build`, then `LITESTL_TEST_NO_BREAK=1 ctest --output-on-failure`.
- **UberTopo:** the same with `-S .`.
- **Scripts** are recreated in the session scratchpad and not committed.

## 8. Estimated size

| File | Size |
|---|---|
| `pool.h` (+ splits) | ~22 KB |
| `pool_store.h/.cc` | ~22 KB |
| column, base and group changes | ~3 KB |
| tests | ~45 KB |
| benchmark | ~8 KB |
| UberTopo smoke test | ~6 KB |

## 9. Risks

- **R1 — Forgotten roots.** These free live entries, and are detected only if the slot
  hasn't been reused. Mitigations:
  - the quarantine mode in tests;
  - the model test, which roots everything through the owner;
  - the owner contract in design 07;
  - P3's journal must register as a root.
- **R2 — Refs copied raw between stores.** They resolve silently to the wrong entry. The
  mitigation is the documented rule plus `import`. P3's cross-owner `mix_from` must
  use `import`.
- **R3 — `SlotPage` lifetime bugs** across snapshots. Covered by ASan and TSan,
  mutation check 3, and the `getMemorySize` baseline.
- **R4 — Collect cost.** `collect` is O(ref elements + reachable entries); uniform pages
  cost O(1). Owners must not collect per element. The W6 bound covers this.
- **R5 — Determinism of slot reuse.** It depends on where collects happen. Collect
  points are owner code, so they are deterministic, but the owner must not make them
  depend on timing or thread count.

## 10. Pressure-test review of v1 (fresh-context agent, 2026-11-18)

**Verdict on v1:** "needs rework".
- The reviewer confirmed the §3.1 baseline claims against `ccf0386`, and that the P1
  digest golden matches `test_attr_digest.cc:79`.
- **Its main recommendation was mark-and-sweep in place of refcounts.** The owner
  accepted it on 2026-11-18.

| # | Sev. | Finding (summary) | Disposition in v2 |
|---|---|---|---|
| 1 | Major | Refcounts plus binding cause most of the defects; mark-and-sweep removes them | **Adopted**: §3.2, §3.6, §3.7 |
| 2 | Blocker | Owner read contradicts "live iff refs > 0"; reading into an existing owner corrupts counts; a half-replaced owner | No counts. Live = node present. Owner reads into a fresh owner, audits, then move-assigns (§3.8) |
| 3 | Blocker | Group copy- and move-assign drop counts and unbind | Gone: no counts, no binding |
| 4 | Blocker | Snapshot counts include uncopied roots (journal, `OwnedRef`) | Gone: roots are passed to each `collect`; `OwnedRef` is dropped |
| 5 | Major | Temp, Derived and extra-only entries leak into bytes and hash, and the read audit rejects valid streams | Bytes and hash cover only the hashed closure `[0, k)` after `canonicalize`; owner write canonicalises a copy (§3.8) |
| 6 | Major | Canonicalize visit order lets Temp renumber hashed content | Three phases, each with its own closure (§3.7); mutation check 5 |
| 7 | Major | Structs holding `PoolRef` are silently uncounted by raw-bytes traits | `has_refs` mandatory; raw bytes only for arithmetic, Vec and array types; `equal` must be bitwise, not `operator==` (§3.4) |
| 8 | Major | Unbound columns abort on construction, staging and snapshot paths | Gone: no binding |
| 9 | Major | Parallel permute races on the store | Gone: column operations never touch the store |
| 10 | Major | No cross-store transfer; the `mix_from` host check comes after column creation | `PoolStore::import` added (§3.5). `mix_from` is unchanged and the raw-copy hazard is documented (R2) |
| 11 | Major | Journal capture of raw refs can be freed and reused | The journal is a `RefRoot`; captured refs stay reachable (§2) |
| 12 | Major | Tests don't prove the canonical and fuzz claims; group-level mutation checks missing | Canonical test varies Temp, extras, garbage, collect timing and siblings; structural fuzz re-signs the hash; mutation checks rewritten (W5) |
| 13 | Minor | Crafted cycles leave cascades unsafe | No cascades exist. Mark uses a visited bitmap, so cycles are harmless (§3.7) |
| 14 | Minor | Pool ids depend on registration order | Kept as a fixed owner layout, with appended kinds allowed on read; remapping by key deferred (§2, §3.5) |
| 15 | Minor | A crafted `"ref"` default must fail cleanly; `{0, s ≠ 0}` | `Column<PoolRef>` reads defaults like any column. `audit`, `collect` and `canonicalize` reject non-`kNoRef` defaults and `{0, s ≠ 0}` (§3.6, §3.7) |
| 16 | Minor | A group hash is not a content identity | Owner content identity defined (§3.8) |
| 17 | Minor | Threaded leak checks are blind | `getMemorySize` baseline in the TSan test (W5) |
| 18 | Minor | Benchmark acceptance vague | Numeric bounds plus the copy-then-1% workload (W6) |
| 19 | Nit | Underspecified holders transitions, index page shift, word size, domain tag, `get` lifetime | Specified in §3.2 and §3.3 |

## 10b. Re-review of v2 (fresh-context agent, 2026-11-18)

**Verdict on v2:** "ready with changes". The mark-and-sweep design holds.
- The reviewer confirmed against `ccf0386`:
  - `canonical_order()` is private;
  - `PoolRef` falls into `ValueOps`' "other" family and satisfies `ColumnValue`;
  - no-op virtuals don't disturb `TestPairColumn`, `BoolColumn` or the compile-fail
    snippets.
- v1 dispositions 5, 7, 10, 11, 12, 13, 15 and 18 were only partly done. They are
  completed by the amendments in §10c.

| # | Sev. | Finding (summary) | Amendment |
|---|---|---|---|
| 1 | Major | `canonical()` tracks only the store: groups changed after `canonicalize` (including by an intern *hit* of a slot ≥ k) make `write` emit dangling refs; `pool<P>()` bypasses the stamp | B1 |
| 2 | Major | The index's copy-on-write amplification: random-page inserts copy most of a 16–32 MB index per diverged snapshot | B2 |
| 3 | Major | A journal capturing type-erased bytes can't find refs in them | B3 |
| 4 | Major | Cross-owner `mix_from` has no correct path | B4 |
| 5 | Major | Crafted cycles break `import`; recursive `import` overflows | B5 |
| 6 | Major | P4 caches need weak roots, and memos need a store version | B6 |
| 7 | Major | Wrong-kind refs pass read and audit, then abort in `get` | B7 |
| 8 | Minor | `Column<std::array<PoolRef, N>>` compiles and is invisible to mark | B8 |
| 9 | Minor | Re-signing the hash in the fuzz harness is circular | B9 |
| 10 | Minor | `read` accepts non-canonical numbering | B10 |
| 11 | Minor | `canonicalize` compacts away quarantined slots | B11 |
| 12 | Minor | No mechanism to check a custom column's default | B12 |
| 13 | Minor | W1 needs types W2 defines; naming inconsistent | B13 |
| 14 | Minor | Benchmark and mutation-check gaps | B14 |
| 15 | Nit | Thresholds, memory order, lifetime, `import` failure, entry format version, key-prefix ids | B15 |

## 10c. v3 amendments (normative; they supersede §3–§4 where they conflict)

**B1 — Canonical state covers the roots.**
- `canonicalize` records the store's stamp **and the `version()` of every root group**.
- `canonical(roots)`, `hash_into(h, roots)` and `write(w, roots)` take the `Roots` and
  require that nothing has changed since: the store stamp and every group version.
  `write` returns `false` otherwise, and `hash_into` aborts.
- `InternPool::intern` is reachable only through `PoolStore` (`friend`). Typed pool
  access is `const`, plus a non-mutating `find`.
- **Regression test:** after `canonicalize`, intern-hit a Temp-only slot, assign it into
  a hashed column, then `write` must fail.

**B2 — A two-level copy-on-write index.** Per pool, there is:
- a **base** open-addressing table, held in an atomically refcounted buffer;
- a small **delta** table (inserts and removal tombstones), also refcounted.

How it works:
- **Insert or remove:** if the base is unique, it goes straight into the base, and
  nothing is copied. If the base is shared, it goes into the delta, which is copied
  first if *it* is shared (O(delta)).
- **Lookup:** delta first, then base.
- **Materialise** (copy the base, merge the delta, clear the delta) when the delta
  exceeds `max(1024, base_live / 8)`, and at `canonicalize`.
- **Records:** `IndexRec { uint32_t h32; uint32_t slot_plus1; }` (8 bytes). Hits are
  confirmed against the node's full `Hash128`, then by `equal`.
- **W6 bound:** in "copy, then intern 1%", the bytes the index copies are
  ≤ 2 × 8 B × (inserts + delta size at the copy), unless a materialisation is
  triggered, which is then reported separately.

**B3 — Element-level ref hooks for captured bytes.** `ColumnBase` gains
`visit_elem_refs(const uint8_t *elem, RefVisitor &) const` and
`remap_elem_refs(uint8_t *elem, const RefRemap &) const`, with no-op defaults. A
journal that captures bytes with `get_bytes` keeps a reference to the column type and
visits or remaps its captures through these hooks. **Test:** a journal-like `RefRoot`
holding captured bytes of a custom two-ref column survives `collect` and
`canonicalize`.

**B4 — Row-scoped import.**
- Add `ColumnBase::remap_refs_rows(const RefRemap &, util::span<const uint32_t> rows)`.
- Add `PoolStore::import_rows(ColumnBase &dst, util::span<const uint32_t> rows, const PoolStore &src, ImportMemo &)`,
  which imports the refs in those rows from `src` and rewrites them.
- The cross-owner `mix_from` recipe is `mix_from` then `import_rows` on the targets. It
  is a model-test operation.

**B5 — Cycles.**
- `read` and `audit` reject inner-ref cycles, using an iterative three-colour DFS.
- `import` is iterative (a post-order worklist).
- **Tests:** importing the 10⁴-long chain, and a crafted cyclic stream.

**B6 — Weak roots and store version.**
- `RefRoot` gains `virtual void prune(const MarkQuery &)`, default no-op, called after
  mark and before sweep, so a source→cache map can drop entries for unmarked keys.
  `MarkQuery::marked(PoolRef)`.
- `PoolStore::version()` changes, to a never-before-seen value, on an intern miss,
  `collect`, `canonicalize` and `read`, so memos keyed by `PoolRef` can detect reuse.
- **07 contract:** approximation caches live in Derived columns, in entries that
  reference their source (an inner ref), or in a pruned weak map.

**B7 — Kind checks.**
- `try_get<T>(PoolRef) → const T *` returns null for a dead slot or a wrong pool type.
- `Roots` gets an optional validator:
  `util::function_ref<bool(const RefSite &, PoolRef)> accept`, where `RefSite` says
  `{Column, group index, column index}`, `{Inner, pool id}` or `{Extra, index}`.
  `audit` and `collect` reject refs it refuses. The owner uses it to require, say,
  curve pools on `.e.curve`.

**B8 — Arrays of refs.** `Column<std::array<PoolRef, N>>` (and `Vec`) is a
`static_assert`: use a custom column. A `PoolRef` embedded in a user struct can't be
detected, which is documented (§3.6).

**B9 — Fuzzing past the hash.** Add `bool verify_hash = true` to `PoolReadLimits` and to
`attr::ReadLimits` (checked in `AttrGroup::read`). The fuzz harness sets it to `false`
and needs no re-signing.

**B10 — Strict owner read.**
- `canonicalize` gains `bool *changed` (whether any slot or column ref moved).
- The owner read recipe audits, then canonicalises the fresh owner and **rejects the
  stream if anything changed**. Read → write is then byte-identical.

**B11 — Quarantine limits.** `canonicalize` compacts quarantined slots away, so the
forgotten-root test does not call `canonicalize`, and the 07 contract says so. The
model test asserts the hazard explicitly: after a forgotten root and a `canonicalize`,
the stale ref resolves to a different value.

**B12 — Defaults through the visitor.** `visit_refs` also visits the column default,
with `count = 0`. Mark rejects a non-`kNoRef` default, so custom columns are covered
without a new virtual.

**B13 — Ordering.**
- W1 creates `attr/pool_ref.h`: `PoolRef`, `kNoRef`, `RefVisitor`, `RefRemap`,
  `MarkQuery`, `RefSite` and `ValueKey<PoolRef>`. `column_base.h` and `registry.cc`
  include only that.
- W1 makes `AttrGroup::canonical_order()` public under that name.

**B14 — Benchmark and mutation-check additions.**
- **W6:** `collect` at 10⁶ refs is measured with 10% garbage, and so is snapshot
  destruction after divergence.
- **W5 mutation checks:**
  7. `canonicalize` skips remapping Temp columns or extra roots;
  8. `read` skips the inner-ref range check (fuzz must catch it);
  9. no `holders` decrement on freeing a slot in a unique page (the `getMemorySize`
     baseline must catch it);
  10. the B1 regression.

**B15 — Nits.**
- When both index thresholds trip, rebuild rather than double.
- `holders` decrements use `acq_rel`.
- The `get` lifetime also ends at `read` and when the store is moved from.
- `import` fails cleanly (`kNoRef` plus `*err`) when the destination lacks the key.
- Each pool writes `PoolTraits<T>::version` (u32), and `read` requires it to match.
- Pool ids stay registration-ordered with appended kinds (deferred, §2). The reviewer's
  key-remap suggestion is noted for when plugins exist.

## 11. Implementation notes and deviations (2026-11-18)

The code is on litestl `ubertopo/p1b-pools` (`c0047b1`, `608afca`, from `ccf0386`).
The UberTopo side is on `session/p1b-value-pools`: the submodule bump,
`test_smoke_pools`, the docs, and DEC-015.

| Item | Deviation or note |
|---|---|
| W1 | As planned (B13). Also two litestl fixes: **`util/memory.h` had no `#pragma once`**, which broke the first translation unit that included it twice; and `hash::DomainTag::PoolEntry`. The P1 digest is unchanged. |
| W2 | Split as planned into `pool_slots.h` (slot pages and the index) and `pool.h`. **Index refinement of B2:** while the base is shared, removals are *lazy*. A stale record is harmless, because every lookup verifies the candidate against the current slot word, the node's full hash and `equal`. So index layout never affects results. Stale records count toward the materialisation threshold. One consequence: "index entry not removed on sweep" is not a bug, so mutation check 4 became "lookup trusts records" (below). |
| W3 | As planned, with B1, B4–B7, B10 and B15. `canonical(roots)` compares the store stamp and every root group's `version()`. `read` hashes only the stream's pools, so a store with appended kinds reads older streams. |
| W4 | `Column<PoolRef>` implements the ref hooks through `if constexpr`. `Column<std::array<PoolRef, N>>` is a `static_assert` (B8). `"ref"` is registered among the builtins. |
| W5 | Files: `test_attr_pool` (unit, with structural fuzz past both hashes), `test_attr_pool_model` (long, 4 × 8000 steps, slot page shifts 2 and 10 × column shifts 2 and 12), `test_attr_pool_threads` (TSan stress, with a `getMemorySize` baseline), `test_attr_pool_digest` (pinned `{0x73283dc9b10b45d0, 0x7a0914cd7544d6fa}`, byte-identical at slot shifts 2 and 10), and `tests/pool_test_types.h` (`Polyline`, `RefPairColumn`, `JournalRoot`, test owner). The structural fuzz accepts about 7% of mutated streams as valid, and all of them pass `audit`. |
| W6 | Results are in the table below. **The slot page default stays at 2¹⁰.** The P1 rule (within 10% of the best intern hit, then the least copy-on-write) would pick 2⁸, but 2⁸ makes the snapshot copy about 3–7× slower (16.7 µs vs 2.3 µs at 10⁵) to save about 6 KB per 1% edit, and intern-hit timings between the sizes are within run-to-run noise. **Deviation: copy at 10⁶ is about 17–20× the copy at 10⁵, against the 15× bound.** Copying is linear in pages (977 slot pages plus 245 column pages at 10⁶), but at that size each page-header increment is a cache or TLB miss. An O(1) copy would need a shared page directory, which is deferred until a real workload needs it. |
| W7 | `test_smoke_pools`: `TypedGroup<Schema<AttrDef<".e.curve", Column<PoolRef>>, ...>>` plus a `PoolStore` by value, defaulted copy and move, kind checks through `Roots::accept`, strict owner read, and `import`. Design 07 §13 records the owner contract for P4. |
| W8 | As planned. **Also `.gitattributes` in litestl** (`tests/lsan.supp eol=lf`): a Windows checkout gives a CRLF copy that LeakSanitizer can't parse when the tests run under WSL ("failed to parse suppressions", seen only in tests whose suppressions are consulted). |
| Process | The v1 plan was rejected by its review, and the owner chose mark-and-sweep. v2 was "ready with changes", and all v2 findings were adopted as B1–B15. |

**Benchmark** (`bench_attr_pool`, clang 19 Release, WSL, `uint64` entries, slot pages
2¹⁰):

| Entries | Intern miss (ns) | Intern hit (ns) | Find miss (ns) | Copy owner (µs) | Copy, then 1% edits: slot pages / index (KB) | Collect, 10% garbage (ms) | Canonicalize (ms) |
|---|---|---|---|---|---|---|---|
| 10³ | 110 | 31 | 34 | 0.2 | 8.0 / 0.5 | 0.01 | 0.04 |
| 10⁴ | 104 | 33 | 37 | 0.3 | 8.0 / 2.5 | 0.14 | 0.78 |
| 10⁵ | 144 | 37 | 41 | 2.3 | 16.0 / 42.5 | 1.40 | 4.8 |
| 10⁶ | 200 | 120 | 84 | 47 | 88 / 170 | 20.5 | 73 |

**W6 acceptance:**
- intern hit at 10⁵ is 1.2× the time at 10³ (bound 3×): met;
- `collect` at 10⁶ takes 20.5 ms (bound 50 ms): met;
- index bytes copied are within the B2 bound at every size: met;
- copy at 10⁶ is 17–20× the copy at 10⁵ (bound 15×): **missed**, see W6 above.

**Mutation checks** (a scratchpad script; clang Release and gcc ASan; each reverted):

| # | Planted bug | Caught by |
|---|---|---|
| 1 | mark skips inner refs | unit (3 asserts plus abort in `get`), model, digest |
| 2 | no copy-on-write before freeing a slot in a shared page | model (30 failures, segfault; ASan), threads |
| 3 | no `holders` increment on page copy | unit and model (asserts; ASan use-after-free), threads |
| 4 | index lookup trusts records (replaces "index not cleared", see W2) | model (24 failures) |
| 5 | canonicalize visits Temp before the hashed closure | unit (canonical-form asserts), model (59), digest |
| 6 | `Column<PoolRef>::remap_refs` ignores Constant pages | model (19 failures, segfault; ASan) |
| 7 | canonicalize skips remapping extra roots | unit (journal test; it initially passed because the captured slot didn't move, so the test now interns the captured entry first) |
| 8 | `read` skips the inner-ref range check | unit (crafted dangling stream: crash/abort) |
| 9 | no `holders` decrement on freeing a slot in a unique page | every test's memory baseline |
| 10 | `canonical()` ignores group versions (B1) | unit (the B1 regression) |

**Configuration matrix (final, 2026-11-18):** WSL Debian 13, clang 19 and gcc 14.
- **litestl** (`608afca`): 44/44 tests pass in all 7 configurations: clang Release,
  gcc Release, clang `-march=x86-64-v3`, clang `LITESTL_NO_INT128`, gcc Debug+ASan,
  gcc Debug+UBSan, and gcc RelWithDebInfo+TSan with `LITESTL_ATTR_CHECKS=1`.
- **UberTopo:** 18/18 tests pass in all 6 configurations.
- The sanitizer runs use `LITESTL_TEST_SCALE_DIV=4`.
- The P0, P1, P2 and pool digests reproduce everywhere.
- TSan runs under `setarch -R`; see `docs/debugging.md`.
