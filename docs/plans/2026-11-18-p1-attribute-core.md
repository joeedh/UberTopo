# Plan P1 — litestl attribute core (`attr/`)

- **Status:** draft v1, awaiting pressure test.
- **Date:** 2026-11-18
- **Design:** [core-data-model/03](../design/core-data-model/03-attributes.md) (attribute
  system), plus the snapshot and determinism parts of
  [04 §5, §8](../design/core-data-model/04-identity-versions-journal.md) and the pool
  pattern from [05 §2](../design/core-data-model/05-geometry-pools.md). Phase P1 in
  [06 §3](../design/core-data-model/06-modules-litestl-and-phasing.md).
- **Repos:**
  - Code goes in **litestl**, on branch `ubertopo/p1-attr`, branched from
    `ubertopo/p0-foundations` because it depends on `hash128` and `task`.
  - UberTopo gets the submodule bump, doc updates and a smoke test, on branch
    `ccr-34437002-4r9a3r`.
- **Decisions in force:**
  - DEC-002: no third-party code.
  - DEC-003 / DEC-008: generic machinery goes in litestl `attr/`.
  - DEC-007: open attribute system and concepts; no central switch over types.
  - DEC-009: canonical encoding and content hashes.
  - The 40 KB source-file split rule.

## 1. Goal

A generic, reusable, **open** columnar attribute store for litestl. UberTopo's
topology domains (P2) and geometry pools (P4) are built on it, and Sculptcore can
adopt it later (Q-H). It provides:

1. **Paged storage with three page states** (Absent, Constant, Dense). Dense pages are
   **shared copy-on-write** between snapshots, with atomic refcounts.
2. **A polymorphic `ColumnBase`.** Dispatch is per column per operation; per-element
   loops stay on typed `Column<T>`.
3. **A `ColumnTypeRegistry`** keyed by stable type strings, for serialisation and binding
   descriptors without a type switch.
4. **`AttrGroup`:** a named, ordered set of columns with fixed builtin slots, and
   value-semantic snapshot copies.
5. **Generic mixing** for topology operations (interpolate, weighted mean, sum,
   copy-first) driven by a batch, with no topology concepts in litestl.
6. **`InternPool<T>` and `PoolColumn<T>`:** deduplicated, refcounted side tables (the
   `DeformPool` pattern), copy-on-write, with canonical renumbering.
7. **Representation-independent content hashes and canonical serialisation.**
8. **Page-size measurements** that close OI-3.

## 2. Non-goals

- UberTopo domains, schemas (unit/role/kind), the topology-event propagator,
  FaceSide semantics (P2/P3).
- Variable-length list columns and struct-of-sub-column columns. The interface must
  admit them (one test-only custom class proves it), but they're deferred.
- N-API or TS exposure. The column types only *carry* their binding descriptors.
- Computed (derived) attributes with dependency versions (P2/P3). P1 only provides the
  `Derived` flag and invalidation hooks.
- Migrating Sculptcore to `litestl::attr` (Q-H).

## 3. Baseline and constraints (verified 2026-11-18)

- **Builds:** litestl `ubertopo/p0-foundations` (`9f93dc9`) passes 23/23 in five
  configurations (P0 §11).
- **`util/memory.h` `shared_ptr` has a non-atomic `int users`.** It is unsuitable for
  pages shared across threads, so P1 adds its own atomically refcounted buffer
  (W1). The existing `shared_ptr` is left unchanged for Sculptcore.
- **litestl has no unique-ownership pointer.** P1 adds `util::UniquePtr` (alloc-based).
- **`io/binfile.h` is a schema-driven struct serialiser.** Columns need raw canonical
  byte streams, so P1 adds a small `io::ByteWriter`/`ByteReader` (W6).
  Integration with binfile is deferred.
- **Sculptcore's `BuiltinAttr` caches a raw column pointer and deletes moves**; its
  `Mesh` isn't copyable. P1 needs **value-semantic** groups, so it uses fixed builtin
  *slots* instead (W4).
- **Sanitizers:** gcc has working **TSan** and **ASan** here; clang has neither
  runtime installed. The UBSan configuration is gcc too (P0 §11).

## 4. Work items

### W1 — `util/rc_buffer.h` and `util/memory.h` additions

```cpp
namespace litestl::util {
/* Intrusive, atomically refcounted, alloc-tracked array of trivially copyable T. */
template <typename T> class RcBuffer {
public:
  static RcBuffer make(uint32_t n);                 // uninitialised; caller fills
  static RcBuffer copy_of(const T *src, uint32_t n);
  RcBuffer(const RcBuffer &);                       // retain (atomic, relaxed)
  RcBuffer &operator=(const RcBuffer &);
  RcBuffer(RcBuffer &&) noexcept; ~RcBuffer();     // release (acq_rel), free at 0
  bool unique() const;                              // refcount == 1 (acquire)
  uint32_t use_count() const;                       // diagnostics/tests
  T *data(); const T *data() const; uint32_t size() const;
};
template <typename T> class UniquePtr { /* alloc::New/Delete based, move-only */ };
}
```

- **Thread-safety contract (documented):**
  - Distinct `RcBuffer` handles to the same buffer may be copied and destroyed
    concurrently.
  - `unique()` followed by an in-place write is safe only for the thread that owns the
    *handle*. This is the standard copy-on-write argument: no other handle exists to
    race with.
- **Tests** (`test_rc_buffer.cc`):
  - refcount and free;
  - the leak tracker sees no leaks;
  - 8 threads copying and destroying handles of one buffer 10⁶ times, run under **TSan**;
  - `UniquePtr` moves.

### W2 — `attr/paged_store.h`: `PagedStore<T, PageShift>`

```cpp
namespace litestl::attr {
template <typename T> concept ColumnValue = std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>;

template <ColumnValue T, int PageShift = kDefaultPageShift /* 12 => 4096 */>
class PagedStore {
public:
  static constexpr uint32_t kPage = 1u << PageShift;
  uint32_t size() const;  void resize(uint32_t n);             // new elements = default
  const T &get(uint32_t i) const;                               // never allocates
  T &write(uint32_t i);                                         // materialise + COW
  void set(uint32_t i, const T &v);                             // avoids materialising if v == page constant (when T is equality-comparable)
  void fill(uint32_t first, uint32_t n, const T &v);            // may produce Constant pages
  util::Span<const T> page_view(uint32_t p, util::Vector<T> &scratch) const; // Dense: direct; Absent/Constant: filled scratch
  util::Span<T> page_write(uint32_t p);                         // unique dense page
  void prepare_write(uint32_t first, uint32_t n);               // make pages unique up front (for parallel writers)
  PageState state(uint32_t p) const;  uint32_t page_count() const;
  PagedStore share() const;                                      // O(pages) refcount bumps
  bool shares_page_with(const PagedStore &, uint32_t p) const;   // tests
  void set_default(const T &d);  const T &default_value() const;
};
}
```

- **Page states.**
  - **Absent:** the value is the column default.
  - **Constant:** one stored value.
  - **Dense:** an `RcBuffer<T>` of `kPage` elements.
  - The last page is always allocated at full length. Elements at or beyond `size()`
    hold the default and are **excluded** from hashing and serialisation.
- **Copy-on-write.** `write`, `page_write` and `prepare_write` turn Absent or Constant
  pages into Dense, and clone a shared Dense page first, so the page is unique before
  it is modified. Reads never allocate.
- **Parallel writes.** After `prepare_write(range)`, any number of threads may write
  *disjoint elements* through `write()` or `page_write()` without synchronisation. This
  is the contract for `task::parallel_for` attribute kernels. Without
  `prepare_write`, concurrent writes are a data race (documented, and checked by a
  debug-only owner assertion if it's cheap).
- **`PageShift` is a template parameter** with a project default. This lets W9 measure
  1K / 4K / 16K pages in one binary. Production code uses the default.
- **Tests** (`test_paged_store.cc`, model-based):
  - **Randomised operation sequences** (resize, set, write, fill, share, permutation
    via the column in W3, prepare_write) are applied both to the store and to a
    reference `std::vector<T>` model. Observable contents are compared after every
    step. Snapshots taken mid-sequence are compared later against frozen model
    copies, which tests that **snapshots are never mutated**.
  - Sharing counts behave as expected: an untouched page stays shared, a written page
    is unshared.
  - Reads don't allocate, checked by `alloc` counters.
  - Parallel writes after `prepare_write`, run under **TSan**.

### W3 — `attr/column.h`: `ColumnBase`, `Column<T>`, `BoolColumn`, `ValueOps`

```cpp
namespace litestl::attr {
enum class ColumnFlags : uint32_t { None = 0, Temp = 1, Derived = 2, /* bits 16..31: user */ };
enum class MixPolicy : uint8_t { Interpolate, WeightedMean, Sum, CopyFirst, SetDefault, Skip };

/* One entry per target element: target <- mix(sources[offsets[k]..offsets[k+1]], weights[...]) */
struct MixBatch {
  util::Span<const uint32_t> targets, offsets, sources;
  util::Span<const float> weights;
};

class ColumnBase {
public:
  virtual ~ColumnBase() = default;
  virtual const ColumnType &type() const = 0;
  virtual uint32_t size() const = 0;
  virtual void resize(uint32_t n) = 0;
  virtual void swap(uint32_t a, uint32_t b) = 0;
  virtual void permute(util::Span<const uint32_t> old_to_new, uint32_t new_size) = 0; // dropped: old_to_new[i] == NONE
  virtual void set_default(uint32_t i) = 0;
  virtual void copy_elem(const ColumnBase &src, uint32_t from, uint32_t to) = 0;     // same type, checked
  virtual void mix(const MixBatch &batch, MixPolicy policy) = 0;
  virtual util::UniquePtr<ColumnBase> share() const = 0;                              // COW snapshot
  virtual hash::Hash128 content_hash() const = 0;                                     // logical, see W5
  virtual void write(io::ByteWriter &w) const = 0;                                    // canonical, see W6
  virtual bool read(io::ByteReader &r) = 0;
  ColumnFlags flags = ColumnFlags::None;
  MixPolicy default_policy;
};

template <ColumnValue T, typename Ops = ValueOps<T>> class Column final : public ColumnBase {
public:
  PagedStore<T> store;
  const T &operator[](uint32_t i) const;  T &write(uint32_t i);
  template <typename F> void for_each_page(F &&f) const;   // f(Span<const T>, base)
  template <typename F> void parallel_write(uint32_t chunk, F &&f); // prepare_write + task::parallel_for_chunks
  static const ColumnType &static_type();
  /* overrides */
};
class BoolColumn final : public ColumnBase { /* bit-packed PagedStore<uint64_t>, same COW */ };
}
```

**`ValueOps<T>`** is selected by **concepts**, not by a switch:
- **Lerpable** (floating point, `Vec<float|double, N>`, `std::array<float|double, N>`):
  - `Interpolate` is Σwᵢsᵢ;
  - `WeightedMean` is Σwᵢsᵢ / Σwᵢ, falling back to `CopyFirst` when Σw = 0;
  - `Sum` is Σsᵢ;
  - accumulation is in `double` for floats, left to right in source order, so results
    are deterministic.
- **Integral and integral vectors:** `Sum` is defined; `Interpolate` and `WeightedMean`
  **round to nearest** (documented). The default policy is `CopyFirst`.
- **Anything else** (IDs, references, enums, `Hash128`): only `CopyFirst`, `SetDefault`
  and `Skip`. Requesting another policy is a **debug assertion and a no-op** in release.
  Types can opt in with an explicit `ValueOps<T>` specialisation.
- **Canonical encoding** (for hashing and serialisation, W5/W6):
  - Arithmetic types, and arrays or `Vec`s of them, encode as raw little-endian bytes.
    Floats use **raw bits** (DEC-009 content rule).
  - Any other `T` must provide `ValueOps<T>::encode`/`decode`. This is enforced by a
    `static_assert`, so padding bytes can never leak into hashes.

**Mixing:** sources and targets may alias. All source values for a target are read
before it is written, and targets are processed in batch order.

**Tests** (`test_column.cc`):
- Model-based permute, swap, copy_elem and resize.
- Each mix policy per value family, against hand-computed expectations.
- **Aliasing:** a target that is also a source.
- Deterministic accumulation order.
- Unsupported policies on non-lerpable types.
- `BoolColumn` runs through the same model-based suite.
- `share()` independence: a snapshot is unchanged after mutations to the original.

### W4 — `attr/group.h`: `AttrGroup`, builtin slots

```cpp
class AttrGroup {
public:
  AttrGroup(); AttrGroup(const AttrGroup &);            // snapshot: share() every column; same order
  AttrGroup &operator=(const AttrGroup &); AttrGroup(AttrGroup &&) noexcept;
  uint32_t size() const;  void resize(uint32_t n);      // all columns
  template <typename Col> Col *find(std::string_view name);            // checked downcast (type pointer compare)
  ColumnBase *find_any(std::string_view name);
  template <typename Col> Col &ensure(std::string_view name, ColumnFlags = {});
  bool remove(std::string_view name);                   // not allowed for builtin slots
  /* Builtin slots: the owner registers a fixed list of columns first, in a
   * fixed order; it then reaches them by constant index with no lookup. */
  template <typename Col> Col &add_builtin(std::string_view name, ColumnFlags = {});  // only before any non-builtin
  template <typename Col> Col &builtin(uint32_t slot);   // static_cast; type checked in debug
  /* structural ops on every column, in order */
  void swap(uint32_t a, uint32_t b); void permute(util::Span<const uint32_t>, uint32_t new_size);
  void mix(const MixBatch &, util::function_ref<MixPolicy(const ColumnBase &)> policy_of);
  hash::Hash128 content_hash() const;                   // ordered, includes names + type keys
  void write(io::ByteWriter &) const; bool read(io::ByteReader &, const ColumnTypeRegistry &);
  /* iteration in insertion order (deterministic) */
};
```

**Why builtin slots instead of cached pointers.** A group is copied by value into
snapshots, so cached column pointers inside the owner would dangle. With slots, the
first K columns are the owner's builtins. A domain type (P2) writes accessors like
`Column<uint32_t> &next() { return attrs.builtin<Column<uint32_t>>(kNext); }`, so a
copied owner is automatically correct. Hot loops take the reference once.

**Ordering and names:**
- Insertion order is the iteration, hashing and serialisation order. A hash map is
  used only for lookup, so hash-map order never leaks.
- Names are `util::string`. Lookup takes `std::string_view`.

**Tests** (`test_attr_group.cc`):
- ensure, find and type mismatch (`find` returns null);
- builtin slots stay valid after group copy and move;
- a snapshot is independent after mutations;
- remove;
- group-wide permute, resize and mix;
- the content hash is order-sensitive and name-sensitive;
- a builtin added after a dynamic column fails.

### W5 — Content hashing

- **`content_hash()`** is a `Hasher128(DomainTag::Content)` over the column's type key,
  `size()`, and then the canonical encoding of each element in `[0, size)`.
  - It is **independent of representation**: an Absent, Constant or Dense page with the
    same logical values hashes the same. The hash depends only on contents, never on
    history.
  - **Per-page hashes are cached** and invalidated on write to that page. A Constant
    page's hash is computed once per (value, length). Shared pages share their cached
    hash, because the cache lives next to the `RcBuffer`, which is immutable while
    shared.
- **Derived and Temp columns** are excluded from `AttrGroup::content_hash()`, because
  they are recomputable.
- **Tests:**
  - equal contents produced through different histories (fill vs. per-element set, a
    dense page reverted to defaults, permute-then-inverse) hash equal;
  - any single-element change changes the hash;
  - −0.0 and +0.0 hash differently (content rule);
  - **golden hashes** pinned for a few fixed columns. They join the determinism contract
    and are added to `test_determinism_digest`.

### W6 — `io/bytes.h` (`ByteWriter` / `ByteReader`) and canonical serialisation

- **`ByteWriter`** appends little-endian scalars, byte spans and u64-length-prefixed
  strings to a `util::Vector<uint8_t>`. **`ByteReader`** is bounds-checked; reads past the
  end set an error flag and return defaults, never undefined behaviour.
- **Column format:**
  - header: type key, flags, size, page shift;
  - per page: a state byte plus payload.
  - The writer **normalises representation**:
    - a Dense page whose live elements are all equal is written as Constant;
    - a Constant page equal to the default is written as Absent.

  Serialised bytes are therefore **canonical**: equal logical contents give equal
  bytes, whatever the history.
- **Group format:** a format version (u32), then the column count, then per column its
  name followed by the column. `read` resolves type keys through the registry. An
  unknown key or a malformed stream returns `false` with an error message, and the
  group is left unchanged.
- **Tests:**
  - round trip for every builtin value type and `BoolColumn`;
  - canonical bytes under different histories;
  - a corrupted or truncated stream fails cleanly; this is fuzzed with truncations and
    random byte flips;
  - an unknown type key fails.

### W7 — `attr/registry.h`: `ColumnTypeRegistry`, `ColumnType`, `ColumnTypeKey<T>`

```cpp
struct ColumnType {
  util::string key;                                      // e.g. "f32", "f64x3", "h128", "f32[8]"
  util::UniquePtr<ColumnBase> (*create)();
  const binding::BindingBase *value_binding;             // Bind<T>() where available, else null
  uint32_t value_size, value_align;
};
class ColumnTypeRegistry {
public:
  static ColumnTypeRegistry &global();                   // builtins registered on first use
  template <typename Col> const ColumnType &add(std::string_view key);  // duplicate key = error
  const ColumnType *find(std::string_view key) const;
};
template <typename T> struct ColumnTypeKey;              // primary undefined; specialised / concept-built
```

- **Explicit registration only.** There are no static registrar objects. Their
  translation units are silently dropped from static libraries when nothing else
  references them, which is the classic static-registration pitfall.
  - Builtin column types are registered inside `global()` on first use.
  - Each module registers its own types through an explicit `register_*_column_types()`
    call, made by its init routine.
- **Builtin keys:**
  - scalars: `bool`, `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64`, `f32`, `f64`;
  - vectors: `{f32,f64,i32,u32}x{2,3,4}`;
  - `h128`;
  - fixed arrays `"<elem>[N]"`, built at compile time by a `ColumnTypeKey`
    specialisation for `std::array<E, N>`.
- **Identity.** `Column<T>::static_type()` returns the registry entry, registering
  lazily if needed. Type identity is a pointer comparison against that entry, with no
  RTTI. Risk: a shared-library boundary could duplicate the entry (see R3).
- **Thread safety.** Registration happens at module init. Lookups after that are
  lock-free and read-only. Concurrent registration is guarded by a mutex.
- **Tests:**
  - lookup and duplicate detection;
  - all builtin keys present;
  - **the open-extension test (guards DEC-007)**: a test-only `TestPairColumn`, a custom
    `ColumnBase` subclass storing pairs and registered from the test file, runs through
    group resize, permute, mix (custom policy handling), share, hash, serialise and
    deserialise, **with no changes to litestl code**.

### W8 — `attr/intern_pool.h`: `InternPool<T>`, `PoolColumn<T>`

- **`InternPool<T>`** is a value-semantic, copy-on-write, deduplicated, refcounted side
  table.
  - **Storage:** values live in a `PagedStore<T>` and refcounts in a
    `PagedStore<uint32_t>`, plus a freelist. **Slot 0 is the immortal default value.**
  - **`intern(const T &) -> slot`** retains the slot. Deduplication uses a hash index
    (canonical encoding → `Hash128`) plus an equality check.
  - **The index is per version and is rebuilt lazily** on the first intern after a
    snapshot copy. That costs O(pool), documented as R5.
  - **`retain`, `release` and `get`.** Release at zero returns the slot to the freelist
    (LIFO, deterministic).
  - **`sweep()`** compacts.
  - **`canonical_renumber(cols…)`** assigns slots in order of first reference while
    scanning the given columns in order, rewrites those columns, and returns the
    permutation. Serialised pools are then independent of history.
- **`PoolColumn<T>`** is a column of `uint32_t` slots bound to a pool owned by the same
  `AttrGroup`, held by a slot reference rather than a pointer so it survives copies.
  - **All writes go through `assign(i, value_or_slot)`**, which retains the new slot
    and releases the old. Structural operations (resize, permute, copy_elem, mix with
    `CopyFirst`, set_default) keep refcounts exact.
  - **`content_hash` and serialisation use the dereferenced values**, not slot numbers.
    That way equal contents give equal hashes whatever the slot assignment.
- **Snapshots.** A snapshot of the group copies the pool together with its columns, so
  refcounts stay consistent *per version*. Pages are copy-on-write, so the copy is cheap.
- **`audit_refcounts()`** recomputes the counts from every `PoolColumn` that references
  the pool and reports mismatches. It is the debugging tool for a write that bypassed
  the funnel (as in Sculptcore's `DeformPool`).
- **Thread safety.** Interning is **single-writer**: no sharded pool, because shard
  assignment made Sculptcore's slot numbering depend on scheduling. A parallel producer
  builds per-chunk local values and interns them afterwards in chunk order.
- **Tests:**
  - model-based random assign, permute and resize sequences, with `audit_refcounts()`
    clean after every step;
  - deduplication;
  - slot 0 is immortal;
  - snapshot independence for pool and column, including refcounts;
  - `canonical_renumber` gives identical serialisation for two different insertion
    histories with the same logical content;
  - sweep compacts correctly;
  - the leak tracker is clean.

### W9 — Benchmarks and the page-size decision (closes OI-3)

`tests/bench_attr.cc` is built but **not** a ctest test. It prints results; run it
manually or in a `bench` configuration. It measures each of `PageShift` ∈ {10, 12, 14}:

| Benchmark | What it measures |
|---|---|
| Sequential read | ns/element via `for_each_page`, and via `operator[]` |
| Sequential write | Includes first-touch materialisation |
| Snapshot | Cost of `share()`, then scattered single-element writes (copy-on-write amplification) |
| Clustered edit | 1% of elements in Morton-like clusters |
| Content hash | Full column hash |
| Memory | Bytes per element, Dense |

**Decision rule:** pick the smallest `PageShift` whose sequential read is within 10% of
the best and whose memory overhead is ≤ 1%. Record the numbers and the choice in this
plan (§11) and in design 03 §3. Set the default to it.

### W10 — Determinism digest, docs, integration

- **Digest.** `test_determinism_digest` gains a column, group and pool workload, with
  the content hashes and the serialised bytes folded into the digest. **The digest
  constant changes once**, in this phase, as a deliberate and documented act (DEC-009:
  changing pinned goldens requires a DEC entry, DEC-010 below).
- **litestl docs:**
  - `documentation/attributes.md` covers the model, page states, copy-on-write and
    thread contract, `prepare_write`, mix semantics, `ValueOps` customisation, the
    registry and explicit registration, `InternPool`/`PoolColumn`, and the hashing and
    serialisation canonical forms.
  - `Readme.MD` link.
  - `CLAUDE.md` layout update (new `attr/` module and `io/bytes.h`).
- **UberTopo:**
  - bump the submodule;
  - smoke test (`test_smoke_attr.cc`): a group with builtin slots, snapshot, mix, hash
    and round trip;
  - design 03 updated with the realised API, including slot builtins, `MixBatch`
    replacing the design's `PropagationEvent` at the litestl level, value-semantic pools
    and the page size;
  - DEC-010 (P1 API decisions and the digest change);
  - `docs/index.md`;
  - `docs/debugging.md` as issues arise.

## 5. Order and dependencies

```
W1 rc_buffer/UniquePtr ─► W2 paged_store ─► W3 column ─► W4 group ─► W7 registry*
                                              │            │
W6 bytes (independent) ──────────────────────►┴─► W5 hashing / W6 serialisation
W8 intern pool (after W4, W5, W6) ─► W9 bench ─► W10 digest/docs/integration
* W7 is needed by W3's static_type(); a minimal registry lands with W3 and is completed in W7.
```

**litestl commit sequence:** W1, W6 (bytes), W2, W3 + minimal W7, W4, W5, W7 complete +
open-extension test, W8, W9, W10. Every commit builds and passes all tests.

## 6. Acceptance criteria

1. **All builds pass.** All existing and new tests pass in the P0 configuration matrix,
   with the same digest in each: clang Release, gcc Release, clang `-march=x86-64-v3`,
   clang `LITESTL_NO_INT128`, gcc Debug + UBSan. Two new configurations are added:
   **gcc Debug + ASan**, and **gcc RelWithDebInfo + TSan** for the threaded tests.
2. **Model-based suites:** zero divergences from the reference models. Default sizes:
   ≥ 10⁵ random operations per suite, multiplied by `LITESTL_TEST_SCALE`.
3. **Copy-on-write:** no snapshot is ever observed to change. Page-sharing counts match
   expectations. TSan is clean on concurrent snapshot readers, copiers and parallel
   writers after `prepare_write`.
4. **Hashing and serialisation:** representation- and history-independence tests pass,
   round trips are exact, corrupt streams fail cleanly, and the goldens are pinned.
5. **The open-extension test passes with no litestl code changes** (DEC-007).
6. **Pools:** `audit_refcounts()` is clean after every randomised step, canonical
   renumbering gives identical bytes across histories, and the leak tracker is clean.
7. **OI-3 is closed:** benchmark numbers and the chosen page size are recorded.
8. **Hygiene:**
   - no new file over 40 KB;
   - no `CLAUDENOTE:` comments in pushed code;
   - litestl and UberTopo docs updated;
   - existing litestl behaviour unchanged: `shared_ptr`, `rand.h` and the other
     existing tests are untouched and passing.

## 7. Verification commands

These are the P0 `run_configs.sh` matrix, plus:

```
CC=gcc CXX=g++ cfg lt-gcc-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
CC=gcc CXX=g++ cfg lt-gcc-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=thread"
build/<cfg>/tests/bench_attr.cc_out      # W9, Release only
```

## 8. Estimated size

| Item | Code (LOC) | Tests (LOC) |
|---|---|---|
| W1 rc_buffer / UniquePtr | ~200 | ~150 |
| W2 paged store | ~450 | ~400 |
| W3 column / ValueOps / BoolColumn | ~700 | ~500 |
| W4 group | ~400 | ~300 |
| W5 / W6 hashing, bytes, serialisation | ~450 | ~400 |
| W7 registry | ~200 | ~250, including the open-extension column |
| W8 pool | ~500 | ~400 |
| W9 bench | — | ~250 |
| Docs | — | — |

These are rough figures.

## 9. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | Copy-on-write bugs, where a snapshot changes through a shared page, are silent and corrupt the procedural cache | Model-based tests that keep frozen snapshot copies and recheck them; `shares_page_with` assertions; ASan for use-after-free; TSan for refcount races |
| R2 | The per-access page-state branch makes `operator[]` slow in hot loops | Hot loops use `for_each_page` / `page_write` spans. W9 measures both paths. Consider a Dense-only fast accessor if the gap is large |
| R3 | `static_type()` identity duplicated across shared-library boundaries | UberTopo ships as one addon (DEC-005). Document that column types must be registered in, and used from, one image. A debug check compares type keys when pointers differ |
| R4 | Integer interpolation rounding and float accumulation order are subtle determinism hazards | Fixed left-to-right `double` accumulation; documented rounding; covered by the digest |
| R5 | The pool's hash index is rebuilt lazily per version, which is O(pool) after every snapshot that interns | Acceptable for P1 (pools are small: curves, materials). Measure in W9. A persistent copy-on-write index is a later optimisation |
| R6 | Explicit registration is forgotten, so deserialisation fails with "unknown key" | A clear error message naming the key. UberTopo's P2 module init registers everything, and a test deserialises a group containing every UberTopo column type |
| R7 | Divergence from Sculptcore's attribute API makes later adoption (Q-H) harder | Keep the concepts similar: page states extend Sculptcore's `exists` flag, flags and merge policies map onto its flags and `AttrMerge`. Document the mapping in `attributes.md` |

## 10. Pressure-test review

_To be filled in by a fresh-context review agent (CLAUDE.md rule) before
implementation starts._
