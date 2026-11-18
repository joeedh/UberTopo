# Plan P1 — litestl attribute core (`attr/`)

- **Status:** **implemented** on litestl branch `ubertopo/p1-attr`; see **§11**. The
  plan is v3: the v1 review (§10) said "needs rework", the v2 re-review (§10b) said
  "ready with changes", and those changes are the normative amendments in **§4b**.
  Where §4b conflicts with the W-item text, **§4b wins**. Intern pools are moved to
  **P1b**.
- **Date:** 2026-11-18
- **Design:** [core-data-model/03](../design/core-data-model/03-attributes.md), plus the
  snapshot and determinism parts of
  [04 §5, §8](../design/core-data-model/04-identity-versions-journal.md). Phase P1 in
  [06 §3](../design/core-data-model/06-modules-litestl-and-phasing.md).
- **Repos:**
  - Code goes in **litestl**, on branch `ubertopo/p1-attr`, branched from
    `ubertopo/p0-foundations`.
  - UberTopo gets the submodule bump, doc updates and a smoke test, on branch
    `ccr-34437002-4r9a3r`.
- **Decisions in force:**
  - DEC-002: no third-party code.
  - DEC-003 / DEC-008: litestl `attr/`.
  - DEC-007: open attribute system, concepts, no central type switch.
  - DEC-009: canonical encoding and content hashes.
  - The 40 KB rule. New decisions are recorded as DEC-010 (W9).

## 1. Goal

A generic, **open**, columnar attribute store in litestl, which UberTopo's topology
domains (P2) build on:

1. **Paged storage.** Pages are Absent, Constant or Dense. Dense pages are **shared
   copy-on-write** between snapshots with atomic refcounts, and the **tail page has
   partial capacity**, so small groups stay cheap.
2. **`ColumnBase` / `Column<T>` / `BoolColumn`.** Dispatch is per column per operation,
   with typed spans for hot loops. Each column has a version stamp and a type-erased
   page API.
3. **Generic mixing** for topology operations, with **simultaneous (gather-then-scatter)**
   semantics, validated batches, cross-column sources and a custom-policy hook.
4. **`AttrGroup`:** a named, insertion-ordered set of columns with **fixed builtin slots**
   and value-semantic snapshot copies.
5. **`ColumnTypeRegistry`:** explicit, locked registration keyed by stable type keys.
6. **A content hash and canonical serialisation** that are **independent of page size,
   representation and history**. Equality is bitwise throughout.
7. **A page-size decision** backed by benchmarks (closes OI-3).

## 2. Non-goals and deferrals

- **Intern pools and pool-referencing columns move to P1b.** Review findings 2, 3, 15
  and 16 showed that the real users have requirements a POD per-group pool can't meet:
  - P4 geometry entries are variable-size (curve segment lists, B-spline knots and
    control points, polylines, mesh surfaces).
  - One `GeomRef` column references several kinds of pool.
  - Pools live in a `GeometryStore` shared across domains (design 05 §2).
  - Refcount roots include references inside a pool and the journal.
  - City-scale pools (≥ 10⁵ entries) need a copy-on-write deduplication index.

  P1b is planned after P2 has fixed the domain and store ownership model. It covers a
  generic refcount funnel (`RefOps` retain and release with a context, invoked by every
  structural op), variable-size immutable entries, ownership above a single group,
  `audit(roots…)`, and a copy-on-write open-addressing index. Design 05 is updated to
  say so (W9).
- UberTopo domains and schemas, the topology-event propagator, FaceSide semantics
  (P2/P3). P1 provides the primitives they need: induced permutes, batch defaulting,
  simultaneous mix and the `Topo` flag.
- Variable-length list and struct-of-sub-column columns. The interface admits them, as
  the open-extension test shows; implementations are deferred.
- N-API/TS exposure, including `Bind<T>()` descriptors in the registry. Those are
  deferred to the N-API work, which also avoids fragile ODR-dependent detection
  (finding 22).
- Persistent content-hash caching (finding 4). P1 hashes on demand; W8 measures, and a
  cached or Merkle scheme can follow if needed.
- Migrating Sculptcore to `litestl::attr` (Q-H).

## 3. Baseline and constraints (verified 2026-11-18)

- **Builds.** litestl `ubertopo/p0-foundations` (`9f93dc9`) passes 23/23 in five
  configurations.
- **`math::Vec` is not trivially copyable.** It has a user-provided
  `Vec(const Vec&)` and `Vec(Vec&)` (`math/vector.h:98-111`) and no `operator==`.
  Because `memcpy` of it is undefined behaviour, `Vec` columns are impossible until W0
  fixes it.
- **gcc Debug + ASan fails on existing code**, verified by the reviewer:
  - `test_task`: LeakSanitizer reports the intentionally leaked per-thread `MemList`
    (`util/alloc.cc:78`).
  - `test_binding_system`: `new-delete-type-mismatch` at
    `binding/binding_constructor_builder.h:16`. `BindingBase` has virtual functions but
    **no virtual destructor**, which is real undefined behaviour.
  - TSan is clean on the existing threaded tests.
- **`util::shared_ptr`'s refcount is non-atomic**, so W1 adds `RcBuffer`.
- **There is no unique-ownership pointer**, so W1 adds `UniquePtr`.
- **`io/binfile.h` is a schema-driven struct serialiser**, so W2 adds a byte stream.
- **Spans are `util::span`**, an alias of `std::span` (`util/span.h`).
- **`util::Map` has no heterogeneous lookup**, so name lookups build a `util::string`.
  Lookups are off the hot path, so this is accepted.
- **`IndexRange` is `int`.** Columns are therefore limited to < 2³¹ elements, which is
  documented; tiles are far smaller.
- **Sanitizers:** gcc has TSan, ASan and UBSan; clang has none.
- **Sculptcore's `BuiltinAttr` caches raw pointers and its `Mesh` isn't copyable.** P1's
  groups are value-semantic, so P1 uses fixed builtin slots instead.

## 4. Work items

### W0 — litestl prerequisites (shared with Sculptcore; flagged)

1. **Make `math::Vec` trivially copyable.**
   - Replace both copy constructors with `constexpr Vec(const Vec&) = default;` and
     defaulted copy assignment.
   - Constrain any generic or variadic constructor so it never wins over copy for
     `Vec&` arguments. That is the probable reason for the non-const overload, so
     check it.
   - Add `static_assert(std::is_trivially_copyable_v<float3>)` and friends to
     `test_math_vec.cc`.
   - Add bitwise `operator==` only if needed (it isn't, because the attribute layer uses
     canonical-bytes equality).
2. **`virtual ~BindingBase() = default;`** This fixes undefined behaviour. The struct is
   already polymorphic, so its layout is unchanged.
3. **LSan suppression** for the intentional per-thread `MemList` leak:
   `tests/lsan.supp`, wired through ctest `ENVIRONMENT` with `LSAN_OPTIONS`. litestl's
   own leak tracker remains the real leak check.
4. **Verify:** the whole existing suite passes under gcc Debug + ASan.
5. **Flag for Sculptcore** in the PR notes: `Vec` copy semantics and the `BindingBase`
   destructor are changes Sculptcore sees when it next bumps litestl. The owner should
   build Sculptcore once, since its full build (Vulkan, NW.js) isn't available here.

### W1 — `util/rc_buffer.h`, `util/memory.h` `UniquePtr`

```cpp
template <typename T> class RcBuffer {                // T trivially copyable
public:
  RcBuffer();                                         // empty (no buffer)
  static RcBuffer make(uint32_t capacity);            // uninitialised elements
  static RcBuffer copy_of(const T *src, uint32_t n, uint32_t capacity);
  RcBuffer(const RcBuffer &); RcBuffer &operator=(const RcBuffer &);   // retain: relaxed
  RcBuffer(RcBuffer &&) noexcept; RcBuffer &operator=(RcBuffer &&) noexcept; ~RcBuffer(); // release: acq_rel
  bool unique() const;   /* acquire */   uint32_t use_count() const;   explicit operator bool() const;
  T *data(); const T *data() const; uint32_t capacity() const;
};
template <typename T> class UniquePtr { /* move-only; alloc::New/Delete; Derived->Base conversion */ };
```

- **Allocation:**
  - The header is padded so that `data()` is aligned to `max(alignof(T), 16)`. Don't
    rely on `alloc::alloc`'s incidental alignment, which is 8 on WASM.
  - Allocation goes through `alloc::alloc`, so the leak tracker sees it.
- **`UniquePtr<Base>` from `UniquePtr<Derived>`:**
  - Allowed only when the conversion keeps the block pointer, which is checked:
    `alloc::release` needs the exact block. Documented: `ColumnBase` must be the
    *first* base of column classes.
  - Deletion uses the virtual destructor.
- **Thread contract:** distinct handles may be copied and destroyed concurrently. Only
  the thread owning a handle may `unique()`-check and then write through it.
- **Tests:**
  - refcounts and empty state;
  - alignment for `double`, `Int256` and over-aligned `T`;
  - leak tracker clean;
  - 8 threads copying and destroying handles 10⁶ times under TSan;
  - `UniquePtr` moves, base conversion and destruction.

### W2 — `io/bytes.h`: `ByteWriter` / `ByteReader`

- **`ByteWriter`** appends little-endian scalars, byte spans and u64-length-prefixed
  strings to a `util::Vector<uint8_t>` (limited to < 2 GB, documented).
- **`ByteReader`** is bounds-checked. Over-reads set a sticky error and return zeros.
  `remaining()` lets callers bound counts before allocating.
- **Tests:**
  - round trips;
  - sticky error;
  - a length prefix larger than `remaining()` is rejected without allocating.

### W3 — `attr/paged_store.h`: `PagedStore<T, PageShift>`

```cpp
template <typename T> concept ColumnValue = std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>;
template <ColumnValue T, int PageShift = kDefaultPageShift>
class PagedStore {
public:
  explicit PagedStore(const T &default_value = T{});
  uint32_t size() const;  void resize(uint32_t n);
  const T &default_value() const;
  const T &get(uint32_t i) const;                     // never allocates
  T &write(uint32_t i);                               // materialise + COW; see reference rules
  void set(uint32_t i, const T &v);                   // no-op write avoided when bitwise equal to a uniform page's value
  void fill(uint32_t first, uint32_t n, const T &v);  // whole pages become Constant
  void set_default_range(uint32_t first, uint32_t n); // batch reset (tombstones)
  util::span<const T> page_span(uint32_t p, util::Vector<T> &scratch) const;
  util::span<T> page_write(uint32_t p);
  void prepare_write(uint32_t first, uint32_t n);
  /* type-erased page API: diffs, journal capture, future typed-array views */
  uint32_t page_count() const; PageState page_state(uint32_t p) const;
  bool same_page_as(const PagedStore &o, uint32_t p) const;          // identical buffer or identical uniform value
  util::span<const uint8_t> page_bytes(uint32_t p) const;            // Dense only
  PagedStore share() const;                                          // O(pages)
};
```

- **Equality is bitwise** (canonical-bytes comparison) everywhere: the `set()`
  shortcut, uniform-page detection, `same_page_as` and serialiser normalisation. So
  −0.0 ≠ +0.0, and identical NaN payloads compare equal (finding 6).
- **Default value:**
  - It is fixed at construction.
  - **There is no setter.** A default that changes later would silently change
    logical contents (finding 9).
  - Absent pages read as the default.
- **Resize:**
  - **Grow:** every newly exposed element reads as the default. The tail is reset if
    needed, so a stale Constant or Dense tail can never reappear (finding 9).
  - **Shrink:** no writes.
  - **Live contents** are `[0, size)`.
- **Partial-capacity tail.** The last page's buffer is sized to the next power of two
  ≥ its live count, with a minimum of 16 elements and a maximum of `kPage`, and it grows
  on demand. A size-1 global group costs bytes, not `kPage × sizeof(T)` (finding 9).
- **Reference invalidation rules** (documented, finding 5):
  - A `T&` from `write()`, or a span from `page_write()`, is invalidated by **any**
    later operation on the store or its owner: `share()` or group copy, `resize`,
    `fill`, `set`, `permute`, `swap`, `mix` and `read`.
  - Holding one across `share()` would mutate the snapshot, so it is forbidden. A debug
    build checks it with a mutation epoch in each page header, compared on scope exit
    in `parallel_write`.
- **Parallel writes.** After `prepare_write(first, n)`, threads may write **disjoint
  elements** of that range concurrently. `Column::parallel_write` chunks are always
  whole pages, which `BoolColumn` requires (finding 14).
- **Tests:** see W7, the common model-based suite.

### W4 — `attr/column.h`: `ColumnBase`, `Column<T,Ops>`, `BoolColumn`, `ValueOps`, mixing

```cpp
enum class ColumnFlags : uint32_t { None = 0, Topo = 1, Temp = 2, Derived = 4 /* bits 16..31 user */ };
FlagOperators(ColumnFlags);
enum class MixPolicy : uint8_t { Skip, SetDefault, CopyFirst, Interpolate, WeightedMean, Sum, Custom };

struct MixBatch {                       // validated: offsets.size()==targets.size()+1,
  util::span<const uint32_t> targets;   // weights empty or == sources.size(),
  util::span<const uint32_t> offsets;   // targets unique and < size(), sources < source size;
  util::span<const uint32_t> sources;   // empty source list => SetDefault for that target
  util::span<const float> weights;
  bool validate(uint32_t target_size, uint32_t source_size, const char **err) const;
};
struct MixContext { const AttrGroup *group; util::stringref name; uint32_t slot; };

class ColumnBase {               // must be the first base of every column class (UniquePtr)
public:
  virtual ~ColumnBase() = default;
  virtual const ColumnType &type() const = 0;
  virtual uint32_t size() const = 0;  virtual void resize(uint32_t n) = 0;
  virtual void swap(uint32_t a, uint32_t b) = 0;
  /* old_to_new injective; NONE = dropped; unmapped new slots = default;
   * uniform gathered pages stay Absent/Constant */
  virtual void permute(util::span<const uint32_t> old_to_new, uint32_t new_size) = 0;
  virtual void set_default_range(uint32_t first, uint32_t n) = 0;
  virtual void set_default_list(util::span<const uint32_t> elems) = 0;   // tombstones, one call
  /* simultaneous semantics: gather all sources from the pre-call state, then scatter */
  virtual bool mix(const MixBatch &b, MixPolicy p, const MixContext &ctx) = 0;
  virtual bool mix_from(const ColumnBase &src, const MixBatch &b, MixPolicy p, const MixContext &ctx) = 0; // same type
  virtual util::UniquePtr<ColumnBase> share() const = 0;
  virtual void hash_into(hash::Hasher128 &h) const = 0;        // W6
  virtual void write(io::ByteWriter &w) const = 0;             // W6
  virtual bool read(io::ByteReader &r) = 0;                    // W6
  /* type-erased page API (forwarded to the store) */
  virtual uint32_t page_count() const = 0; virtual PageState page_state(uint32_t) const = 0;
  virtual bool same_page_as(const ColumnBase &, uint32_t p) const = 0;
  uint64_t version() const;          // bumped once per mutating call (incl. read, parallel_write)
  ColumnFlags flags = ColumnFlags::None;
  MixPolicy default_policy = MixPolicy::CopyFirst;
protected:
  virtual bool mix_custom(const MixBatch &, const MixContext &) { return false; }   // Custom policy hook
};
```

**Mix semantics (finding 7):**
- **Simultaneous.** All source values are gathered from the pre-call state, then all
  targets are written. This makes FaceSide side-swaps (`2f ← 2f+1`, `2f+1 ← 2f`)
  expressible, and makes the operation safe to parallelise later.
- **Duplicate targets are rejected** (`mix` returns false).
- **`mix_from`** takes sources from another column of the same type, for Booleans
  merging complexes, LOD transfer and journal restore.
- **`Custom`** calls `mix_custom`, with a `MixContext` that gives access to the group,
  so a custom policy can read sibling columns, as Sculptcore's CUSTOM merges do.
- **`Topo` columns are never mixed by group-level mix**: the operator writes links
  itself.
- **Errors** are reported as `bool` plus a message, never trapped, so error paths are
  testable (finding 17).

**`ValueOps<T>`** is selected by **concepts** (finding 8):

| Value family | Policies and rules | Default policy |
|---|---|---|
| **Float-like**: `float`, `double`, `Vec<float or double, N>`, `std::array<float or double, N>` | Interpolate is Σwᵢsᵢ. WeightedMean is Σwᵢsᵢ/Σwᵢ, with Σw = 0 falling back to CopyFirst. Sum is Σsᵢ. Accumulation is in `double`, left to right in source order, then converted to `T` once | Interpolate |
| **Integral**, scalar or vector | Sum accumulates in the column's own width with **defined wraparound**: unsigned arithmetic, then a cast back. Interpolate and WeightedMean compute in `double`, round **half away from zero**, then **saturate** to the type's range, so there is no undefined behaviour. Precision loss above 2⁵³ for 64-bit integers is documented | CopyFirst |
| **Other** (IDs, `Hash128`, references, enums) | CopyFirst, SetDefault, Skip and Custom only. Other policies make `mix` return false | CopyFirst |
| **Canonical encoding** | Arithmetic types and their arrays or `Vec`s are raw little-endian bytes (floats as raw bits). Other `T` must specialise `ValueOps<T>::encode/decode`, checked by `static_assert` | — |

- **FP contraction (finding 8d):**
  - `Column<T>::mix` is header-instantiated. The `attr` CMake target therefore exports
    `-ffp-contract=off -fno-fast-math` as **INTERFACE** compile options (or `/fp:precise`
    for MSVC-style drivers), so every consumer TU that instantiates the kernels gets
    them.
  - This is documented as a requirement for custom `ValueOps`.
  - The digest (W8) is built in the `-march=x86-64-v3` configuration to prove it.
- **`BoolColumn`** stores bits in `PagedStore<uint64_t>` words, with `get(i)` and
  `set(i, bool)`. There is no `bool&`. Parallel writes are page-aligned. It has its own
  type key, `"bool"`, and `Column<bool>` is rejected by `static_assert`.

**Tests:**
- the common model-based suite (W7);
- mix against an **independent reference implementation**, with random batches
  including aliasing, duplicates (expected failure) and empty source lists, for every
  policy and value family;
- integer ties, saturation and wraparound;
- determinism of the accumulation order;
- `Topo` columns skipped;
- the Custom hook reads a sibling column;
- `mix_from` across two groups.

### W5 — `attr/group.h`: `AttrGroup`

```cpp
class AttrGroup {
public:
  AttrGroup(); AttrGroup(const AttrGroup &); AttrGroup &operator=(const AttrGroup &);
  AttrGroup(AttrGroup &&) noexcept; AttrGroup &operator=(AttrGroup &&) noexcept;
  uint32_t size() const; void resize(uint32_t n);
  template <typename Col> Col *find(std::string_view name);              // null on missing or type mismatch
  template <typename Col> Col *ensure(std::string_view name, const typename Col::value_type &def = {}, ColumnFlags = {});
                                                                          // null if name exists with another type/default
  bool remove(std::string_view name);                                     // false for builtin slots
  template <typename Col> Col *add_builtin(std::string_view name, const typename Col::value_type &def = {}, ColumnFlags = {});
                                                                          // null after any non-builtin column exists
  template <typename Col> Col &builtin(uint32_t slot);                    // static_cast; type checked in debug
  uint32_t builtin_count() const;
  void swap(uint32_t a, uint32_t b);
  void permute(util::span<const uint32_t> old_to_new, uint32_t new_size, bool parallel = true); // per column, columns in parallel
  void set_default_list(util::span<const uint32_t>);
  bool mix(const MixBatch &, util::function_ref<MixPolicy(util::stringref name, uint32_t slot, const ColumnBase &)> policy_of);
  void hash_into(hash::Hasher128 &) const; hash::Hash128 content_hash() const;
  void write(io::ByteWriter &) const; bool read(io::ByteReader &);         // registry: ColumnTypeRegistry::global()
  /* insertion-ordered iteration */
};
```

**Builtin slots:**
- The owner registers its builtin columns first, in a fixed order. Accessors such as
  `attrs.builtin<Column<uint32_t>>(kNext)` stay correct across copies and moves, because
  no pointer is cached.
- **`read()` verifies the builtin layout**: the owner constructs the group with its
  builtins, and the stream must match their count, names and type keys. It then fills
  them and appends the dynamic columns. On mismatch it returns false and leaves the
  group unchanged (finding 10d).

**Snapshots:**
- A copy shares every column, including Temp ones, since sharing is cheap.
- **Membership rule:** Temp and Derived columns are excluded from **both** the content
  hash and serialisation, so the two always agree (finding 10c). When read, they are
  left at defaults.

**Policy lookup:** `policy_of` receives the name, slot and column, so P3 can look the
schema up by name (finding 7d).

**Tests:**
- ensure and find with type and default mismatches;
- builtin slot validity after copy and move;
- builtin-after-dynamic returns null;
- remove;
- a snapshot is independent;
- parallel permute equals serial permute;
- the FaceSide pattern: a face permutation induces a `2π(f)+s` permutation on a
  group sized 2× faces, a P2 dry run;
- `read` with a mismatched builtin layout fails cleanly.

### W6 — Content hash and canonical serialisation (page-size independent)

- **Content hash:** `Hasher128(DomainTag::Content)` over `(type key, flags & ~user,
  size, default value bytes)`. Then the live values in element order are fed as **one
  continuous byte stream** of canonical encodings, using a single `raw_bytes` call per
  span for arithmetic types (finding 19).
  - The hash is **independent of PageShift, representation and history** by
    construction, because it depends only on the element sequence (finding 4d).
  - It is computed on demand, with no cache in P1 (finding 4). W8 measures the cost.
- **Serialisation:**
  - **Format:** a column header (type key, flags, size, default value) followed by a
    **run encoding** independent of page size. Each run is one of:
    - `Default(count)`;
    - `Constant(count, value)`;
    - `Dense(count, values…)`.

    The writer emits **maximal canonical runs**: adjacent runs of the same kind are
    merged, and uniform stretches longer than a threshold become Constant. So equal
    logical contents give equal bytes, whatever the page size or history (finding 10b).
  - **Group stream:** a format version (u32), the builtin count, the columns (name plus
    column), and a **trailing content hash**. `read` recomputes the hash and rejects a
    mismatch, which turns most corruption into a clean failure (finding 10e).
  - **Bounds:** every count is checked against `remaining()` before allocating.
- **Tests:**
  - round trips with non-zero defaults;
  - history independence (fill vs. per-element set, permute then inverse, shrink then
    grow);
  - **page-size independence**: the same operation sequence on stores with
    `PageShift` ∈ {2, 4, 12} gives identical hashes and identical bytes;
  - −0.0 vs. +0.0 differ; NaN payloads are preserved;
  - **fuzz** with truncations and byte flips. The assertion is *no crash, undefined
    behaviour or leak*; a failed read is expected but not required.

### W7 — Common model-based test suite (strengthened, finding 18)

`tests/test_attr_model.cc`, labelled `long`. It runs against `Column<T>` for
`f32`, `f64x3`, `i32`, `u64`, `f32[8]` and `h128`, and against `BoolColumn`.

- **Page sizes:** run with **`PageShift` 2 and 4** (many pages, tails and state
  transitions) as well as the default.
- **Operations:** random sequences of resize (grow and shrink), set, write, fill,
  set_default_range and list, swap, permute (random injective maps with drops), mix and
  mix_from (random valid batches), prepare_write plus page_write, `share()`, group copy,
  and serialise plus read.
  - Indices are biased toward page boundaries and the tail.
  - Generated values include ±0, NaN payloads and denormals.
- **Reference:** a `std::vector<T>` model with an independent mix implementation.
- **After every step**, compare:
  - observable contents;
  - `content_hash()` against an **independent reference encoder** run over the model;
  - serialised bytes against the reference run-encoder;
  - every **live snapshot** taken earlier against its frozen model copy.
- **Reference-lifetime tests:** a debug-epoch violation is detected when a reference is
  held across `share()`.
- **TSan stress:** one writer mutates the original while N threads continuously read,
  copy and destroy snapshots and compare them with frozen hashes. A second test runs
  parallel writers after `prepare_write`.
- **Volume:** ≥ 10⁵ operations per value type per page size, multiplied by
  `LITESTL_TEST_SCALE`.

### W8 — Registry, open extension, benchmarks, attr digest

- **`attr/registry.h` (finding 11):**
  - `ColumnTypeRegistry::global()`. **Every operation takes a mutex**; lookups only
    matter during deserialisation and registration.
  - `add<Col>(key)` is **idempotent** for the same column class and returns an error
    for a different class with the same key.
  - Keys come from `ColumnTypeKey<Col>`, specialised per **column class** (so
    `Column<T, CustomOps>` must declare its own key). `Column<bool>` is rejected.
  - Builtin types register on first use of `global()`. Other modules use explicit
    `register_*()` calls; there are no static registrar objects, which static libraries
    silently drop.
  - **Entries are allocated with plain `new`** and never freed, like binding
    descriptors. This keeps them out of the leak tracker without `PermanentGuard`,
    whose flag is process-global (`util/alloc.cc:27`).
  - **Builtin keys:**
    - scalars: `bool` (BoolColumn), `i8` `u8` `i16` `u16` `i32` `u32` `i64` `u64` `f32`
      `f64`;
    - vectors: `{f32,f64,i32,u32}x{2,3,4}` (needs W0);
    - `h128`;
    - `"<elem>[N]"` for `std::array`.
- **Open-extension test (guards DEC-007).** A test-only `TestPairColumn` is a custom
  `ColumnBase` with its own `Custom` mix, registered from the test file. It runs through
  group resize, permute, mix, snapshot, hash, write and read **with no litestl change**.
- **`tests/bench_attr.cc`** is built and is not a ctest test. For `PageShift` ∈
  {10, 12, 14} it measures:

  | Benchmark | What it measures |
  |---|---|
  | Sequential read | Span-based and `operator[]` |
  | **Random gather** | Through `operator[]`, as link traversal reads (finding 21) |
  | Sequential write | Including first-touch materialisation |
  | **Copy-on-write amplification** | Bytes copied per scattered single-element edit after `share()` |
  | Clustered edit | 1% of elements in clusters |
  | Content hash | Full column hash |
  | Memory per element | For large and for size-1 groups |

  **Decision rule:**
  - **Candidates:** page sizes whose sequential-read *and* random-gather times are
    within 10% of the best.
  - **Choice:** among the candidates, the one with the **least copy-on-write
    amplification**.
  - **Tie-break:** a larger page.

  The numbers and the choice are recorded in §11 and in design 03 §3.
- **`tests/test_attr_digest.cc`** is a **separate** pinned digest of column, group and
  serialisation outputs, pinned *after* the page-size decision. The P0 digest is not
  touched (finding 20).

### W9 — Docs and integration

- **litestl:**
  - `documentation/attributes.md` covers the model, page states, partial tails, the
    copy-on-write and reference-invalidation rules, `prepare_write`, mix semantics,
    `ValueOps` numerics and custom ops (including the FP-flag requirement), flags, the
    registry, hash and serialisation canonical forms, and the mapping to Sculptcore's
    attributes (R7).
  - `Readme.MD` and `CLAUDE.md` updated with the `attr/` module and `io/bytes.h`.
- **CMake:**
  - a new `attr` STATIC target, depending on `util`, `io` and `math`, with the INTERFACE
    FP options from W4;
  - **not** added to the WASM `LITESTL_LIB` list;
  - tests linked.
- **UberTopo:**
  - bump the submodule;
  - `test_smoke_attr.cc`: a group with builtin slots, a snapshot, mix, hash and round
    trip;
  - design 03 updated with the realised API: slots, simultaneous `MixBatch` with
    `mix_from` and `Custom`, explicit locked registration replacing "static
    registrar", no hash cache, the run-encoded format and the page size;
  - design 05 and 06: pools deferred to **P1b**;
  - **DEC-010**: the P1 API decisions, the W0 changes to shared litestl code, the
    deferral of pools to P1b, and the page size;
  - `docs/index.md`;
  - `docs/debugging.md` entries as issues arise.


## 4b. v3 amendments (normative; supersede the W-item text where they conflict)

These come from the v2 re-review (§10b). They are numbered by its findings.

- **A1. Stale-reference detection.** This replaces the "debug mutation epoch" for
  `share()`.
  - In debug builds, `share()` records a checksum of every Dense page that becomes
    shared, kept next to the buffer.
  - The checksum is re-verified when that buffer is later copied on write, or released
    while still shared. A mismatch means someone wrote through a stale reference into a
    shared page, and is reported as a test failure.
  - `parallel_write` keeps a scope epoch, now **atomic**, to catch `share()`, `resize`
    or other structural calls made during the scope.
  - **AC3 is reworded:** "the debug shared-page checksum detects a write through a
    reference held across `share()`, and the `parallel_write` scope epoch detects
    structural calls during the scope".
  - Both mechanisms are compiled into Debug and into the TSan configuration (which
    defines `LITESTL_ATTR_CHECKS`), not only when NDEBUG is undefined.
- **A2. Parallel-write contract.**
  - Concurrent writes are allowed **only through spans obtained serially**: from
    `page_write()` before the parallel region, or from the spans `parallel_write(fn)`
    passes to `fn`.
  - Calling `write()`, `set()`, `fill()` or any structural method inside a parallel
    region is forbidden.
  - The column `version()` is bumped **at the end** of `parallel_write` and when
    `page_write` is called, never by `prepare_write`.
- **A3. Shared tails.**
  - **Any** write into a shared buffer copies it first. That includes the default
    reset performed when growing past the old size inside the tail capacity.
  - `same_page_as` compares live length as well as content, and treats Absent and
    Constant(default) as equal.
- **A4. Custom mix context.** `MixContext` carries:
  - `pre`: a pre-call snapshot of the group (a cheap copy-on-write `share()`) that a
    Custom handler reads siblings from;
  - `src`: the source group for `mix_from`, or null;
  - `const void *op`: an operator-defined context (for example Sculptcore-style
    `merged_co`).

  A Custom column may **claim sibling slots** at registration. The group's generic mix
  loop skips claimed slots, and the claiming handler writes them, so a column and its
  guard columns are handled together.
- **A5. Validated batches.**
  - `AttrGroup::mix` validates **once** and produces a `ValidatedMixBatch` token. Column
    virtuals take only the token.
  - **Checks:**
    - `offsets[0] == 0`;
    - offsets are non-decreasing;
    - `offsets.back() == sources.size()`;
    - `weights` is empty or the same length as `sources`;
    - every target and source index is in range;
    - targets are unique, checked on a sorted copy, O(T log T).
  - Gather buffers are O(targets × sizeof(T)).
- **A6. `PageShift` becomes a runtime, per-store constant.**
  - It is fixed when the store is constructed. The default comes from the build
    constant, and tests pass 2 or 4.
  - `Column<T, Ops>` has **no** shift template parameter, so type identity, keys and
    `builtin<>` are unaffected.
  - Page arithmetic uses stored shifts and masks.
  - `builtin<>` and `find<>` compare `ColumnType` **pointers**.
- **A7. FP flags.**
  - The `attr` target uses **PUBLIC** options, the same flag set as `predicates`, so
    they also apply to `attr`'s own `.cc` files.
  - Mix kernel bodies also open with a block-scoped `#pragma clang fp contract(off)`.
  - `column.h` has an `#error` on `__FAST_MATH__`.
  - A **probe test** target compiled with `-ffp-contract=fast`, linking `attr`, checks
    the pinned mix digest. This proves the kernels are immune.
- **A8. Serialised builtin layout.**
  - The stream records the **full builtin layout**: name, key, flags and default for
    every builtin, with **data omitted** for Temp and Derived builtins.
  - `read()` parses into a **staging group**, verifies that the layout matches the
    owner's (including defaults and flags), and drops the target's existing dynamic
    columns. It **swaps the staging group in only on success**.
- **A9. Version stamps.**
  - Stamps come from a **process-global atomic counter**, so every mutation yields a
    value never seen before, and copies don't collide after divergence.
  - `AttrGroup::schema_version()` changes on add, remove and read.
  - `flags` becomes private, with `set_flags()` bumping the version.
- **A10. Read limits.**
  - `read(r, ReadLimits{max_elements, max_columns})` is checked **before** any resize.
  - Run counts must sum to the column `size`, and every column's size must equal the
    group size. Otherwise the read fails cleanly.
- **A11. Canonical run encoding,** defined precisely:
  - Maximal runs of bitwise-equal values are found over the **element stream**, not
    over pages.
  - Runs of length at least `kRunMin = 8` (a **format constant**) are emitted as
    `Default` if their value is the default, otherwise as `Constant`.
  - Everything else is merged into maximal `Dense` runs.
  - Dense bool runs are bit-packed from bit 0 of the run.
  - Size 0 means zero runs.
- **A12. Hash contents.**
  - The hash and the stream cover **the same flag set** (all flags).
  - The default is hashed, because it governs grows.
  - Custom `ValueOps` must declare a fixed `kEncodedSize`; variable-width values belong
    in custom column classes, which must length-prefix them.
- **A13. Group canonical order.** The group hash and stream contain builtins in slot
  order, then dynamic columns **sorted by name bytes**. Iteration order stays insertion
  order. Group-level hash and bytes are therefore independent of `ensure` and `remove`
  history. The group hash covers the builtin count, the column count, and the name, key
  and content of every column.
- **A14. Registry storage.**
  - The registry uses **std containers with the std allocator**, plus `new`-allocated
    entries, so none of it is visible to litestl's leak tracker. A test registers types
    and then checks that `test_end` is clean.
  - `"<elem>[N]"` keys are **registered explicitly**. `ensure` never auto-registers, and
    `read()` needs prior registration, which the module init functions perform.
    `static_type()` still lazily registers for convenience, but read correctness never
    depends on it.
- **A15. Leak checks across threads.** Threaded tests also assert that
  `alloc::getMemorySize()`, which is global, returns to its baseline. The per-thread
  tracker lists can't see worker-thread leaks.
- **A16. `BoolColumn` paging.** A bool page is `kPage` *elements* (`kPage/64` words) and
  requires `PageShift ≥ 6`. The tail minimum is clamped to `kPage`. The model suite runs
  bools at shifts 6 and 8.
- **A17. Group permute.**
  - It is **serial by default**, and `permute_jobs()` exposes one job per column so P2's
    freeze can flatten all domains into one parallel loop without nesting.
  - Injectivity and range are validated once, returning `bool`.
- **A18. APIs P2 needs:**
  - `AttrGroup::mix_from(const AttrGroup &src, batch, policy_of)` matches columns by
    name and type pointer. Columns the target lacks are created with
    `ColumnBase::new_like(n)` (same type, default and flags).
  - Type-erased element access `elem_size()`, `get_bytes(i, out)` and
    `set_bytes(i, in)`, for the journal's `ColumnDelta`.
  - `ColumnBase::mix_from` compares `ColumnType` pointers and fails with a message.
- **A19. Weights are `double`.**
- **A20. Bounded model-suite cost.**
  - Element counts are capped at about 6 pages of the current shift, with at most
    **8 live snapshots**, rotating.
  - Full per-step checks run at shifts 2/4 (and 6/8 for bools). At the default shift,
    every 64th step is checked.
  - Sanitizer configurations run at 1/4 scale (environment variable).
- **A21. TSan stress.** The writer publishes snapshots to readers through a
  mutex-protected slot. Readers copy, read and destroy only snapshots they own.
- **A22. `io/bytes.h` is header-only**, an INTERFACE target, so `attr` doesn't link `io`,
  and through it `binding` and `lz4`.
- **A23. W0.1 wording.** There are **no generic constructors** to constrain; the
  re-review verified that only `operator T*()`/`Vec(const T*)` interplay, which loses to
  copy. Add `static_assert(sizeof(Vec<T,N>) == N * sizeof(T))`.
- **A24. Layout and API consistency.**
  - `column.h` is split up front into `value_ops.h`, `mix.h` and `bool_column.h`.
  - All name parameters are `std::string_view`.
  - The registry call is `add<Col>()`, taking its key from `ColumnTypeKey<Col>`.

## 5. Order and dependencies

```
W0 prerequisites ─► W1 RcBuffer/UniquePtr ─► W3 PagedStore ─► W4 Column (+minimal registry, hash_into/write/read)
W2 bytes ─────────────────────────────────────────────────────┘        │
                                                         W5 group ◄─────┘
                                     W6 hash/serialisation (completes W4/W5 bodies)
                                     W7 model suite ─► W8 registry/open-ext/bench/digest ─► W9 docs/integration
```

**litestl commit sequence:** W0, W1, W2, W3, then W4 and W6 for columns together (W4's
virtuals need the hash and serialisation bodies), then W5, W7, W8, W9. Every commit
builds and passes all tests.

## 6. Acceptance criteria

1. **All builds pass** in the full matrix, with the P0 digest unchanged and the attr
   digest identical in each:
   - clang Release;
   - gcc Release;
   - clang `-march=x86-64-v3`;
   - clang `LITESTL_NO_INT128`;
   - gcc Debug + UBSan;
   - **gcc Debug + ASan**, including the existing tests after W0;
   - **gcc RelWithDebInfo + TSan**.
2. **Model suite:** zero divergences at `PageShift` 2, 4 and the default. Hashes and
   bytes match the reference encoders after every step, and every live snapshot
   matches its frozen copy.
3. **Copy-on-write and threads:** TSan is clean on the writer-plus-snapshot-readers
   stress and on parallel writers. **The debug shared-page checksum detects a write
   through a reference held across `share()`, and the `parallel_write` scope epoch
   detects structural calls during the scope** (A1).
4. **Canonical forms:** hash and bytes are identical across page sizes and histories,
   round trips are exact, and fuzzing causes no crash, undefined behaviour or leak.
5. **The open-extension test passes with no litestl changes** (DEC-007).
6. **OI-3 is closed:** the benchmark table and the chosen page size are recorded.
7. **Hygiene:**
   - no new file over 40 KB;
   - no `CLAUDENOTE:` in pushed code;
   - docs updated;
   - the W0 changes are flagged for Sculptcore;
   - all existing tests pass, and the P0 goldens and digest are unchanged.

## 7. Verification commands

These are the P0 matrix (`run_configs.sh`), plus:

```
CC=gcc CXX=g++ cfg gcc-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
CC=gcc CXX=g++ cfg gcc-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=thread"
build/clang-rel/tests/bench_attr.cc_out
```

## 8. Estimated size

| Item | Code (LOC) | Tests (LOC) |
|---|---|---|
| W0 | ~40 | ~30 |
| W1 | ~250 | ~200 |
| W2 | ~150 | ~100 |
| W3 | ~550 | (in W7) |
| W4 | ~800 | ~400 |
| W5 | ~450 | ~300 |
| W6 | ~400 | ~250 |
| W7 | — | ~700 |
| W8 | ~250 | ~450 |
| W9 | — | — |

About 2.9k LOC of code. These are rough figures.

## 9. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | Copy-on-write bugs that silently corrupt snapshots | Frozen-snapshot checks after every model step; the debug mutation epoch; ASan and TSan configurations; the reference-lifetime tests |
| R2 | Per-access page-state branch cost in `operator[]` and random gather | W8 measures it. Hot loops use spans. A Dense-only fast accessor is possible if needed |
| R3 | `static_type()` identity duplicated across shared-library boundaries | One addon image (DEC-005), documented. A debug check compares keys when pointers differ |
| R4 | Numeric determinism of mix (accumulation order, FP contraction in consumer TUs) | Fixed `double` left-to-right accumulation, INTERFACE FP options, and the attr digest built in the FMA configuration |
| R5 | The W0 changes affect Sculptcore | `Vec`: constrain generic constructors and test copy overload resolution. `BindingBase`: the struct is already polymorphic, so the layout is unchanged. Flag both for the owner to build Sculptcore |
| R6 | Explicit registration is forgotten | Clear "unknown key" errors. P2 adds a test that round-trips a group with every UberTopo column type |
| R7 | API divergence from Sculptcore attributes (Q-H) | Mapping documented in `attributes.md`: page states extend Sculptcore's `exists`; Topo/Temp/Derived and the mix policies map onto its flags and `AttrMerge` |
| R8 | On-demand hashing is too slow for the procedural cache | W8 measures it. Caching (keyed by buffer and live length, published atomically) is a follow-up if needed |

## 10. Pressure-test review (fresh-context agent, 2026-11-18)

**Verdict on v1:** "needs rework". There were 24 findings: 2 blockers, 13 major,
8 minor and 1 nit.
- **What was ready:** W1–W7 needed only the changes below. The W1 refcount memory
  orders were confirmed correct.
- **What needed redesign:** W8 (pools).
- **What the reviewer verified directly:**
  - by compiling, that `Vec` is not trivially copyable;
  - by building existing litestl under gcc ASan, the two pre-existing failures;
  - that TSan is clean on the existing threaded tests.

All findings were accepted. Their disposition:

| # | Sev. | Finding (summary) | Disposition in v2 |
|---|---|---|---|
| 1 | Blocker | `Vec` isn't trivially copyable, so there can be no vector columns | **W0.1** makes `Vec` trivially copyable; the concept is not relaxed |
| 2 | Blocker | `PoolColumn` can't reach its pool from the virtuals; snapshot divergence corrupts slots | **Pools moved to P1b** (§2), with requirements captured |
| 3 | Major | A POD per-group pool doesn't fit P4 (variable-size entries, multi-kind `GeomRef`, store above group, journal roots) | P1b scope: refcount funnel, variable-size entries, store-level ownership, `audit(roots)` |
| 4 | Major | The page-hash cache races, goes stale, depends on tail length and page size, and costs on write | No cache in P1; page-size-independent stream hash (W6); caching deferred (R8) |
| 5 | Major | Mutable-reference lifetime across `share()` undefined and untested | Invalidation rules, debug mutation epoch, targeted tests (W3, W7) |
| 6 | Major | Float `==` breaks ±0 and NaN canonical forms | Bitwise equality everywhere; generators emit ±0, NaN and denormals (W3, W7) |
| 7 | Major | Sequential aliasing breaks side swaps; Custom and swap_sides dropped; no cross-column mix; policy can't see the name; topology links mixed; no validation | Simultaneous gather-scatter, duplicates rejected, `mix_from`, `Custom` + `MixContext`, policy receives name and slot, `Topo` flag skipped, `MixBatch::validate` (W4, W5) |
| 8 | Major | i64 precision, UB on double→int conversion, unspecified ties, FMA contraction in consumer TUs | Integer Sum in its own width with wrap; saturating half-away-from-zero rounding; INTERFACE FP options; digest built in FMA configuration (W4, W8) |
| 9 | Major | Stale tail on grow, mutable default, naming clash, no way to set a default, full-page tail waste | Grow resets the tail; default fixed at construction and passed in `ensure`; `set_default_range`/`list` naming; partial-capacity tail (W3, W5) |
| 10 | Major | Format lacks the default, depends on page size, membership unclear, builtin layout unrecorded, fuzz expectation wrong | Default in header; page-size-independent run encoding; Temp/Derived excluded from both hash and bytes; builtin layout verified; trailing hash; fuzz = no crash, UB or leak (W5, W6) |
| 11 | Major | Registry: lazy-registration race, duplicate semantics, key collisions, leak-tracker interaction, registry parameter | Mutex on all operations; idempotent add for the same class; keys per column class; `Column<bool>` rejected; entries via `new`; uses `global()` only (W8) |
| 12 | Major | ASan matrix fails on existing code (LSan `MemList`, `BindingBase` delete mismatch) | **W0.2** virtual destructor, **W0.3** LSan suppression, W0.4 verify; flagged for Sculptcore |
| 13 | Major | No column version, no derived-invalidation hook, no type-erased page API | `ColumnBase::version()` (the hook consumers compare against) and the page API (W3, W4) |
| 14 | Major | `BoolColumn` parallel writes race on shared words; no write API | Page-aligned parallel chunks; `set(i, bool)` (W3, W4) |
| 15 | Major | Pool index rebuild cost underestimated | P1b requirement: copy-on-write open-addressing index, benchmarked at 10⁵ entries |
| 16 | Minor | Pool serialisation contradictions; sweep and audit signatures | P1b |
| 17 | Minor | Debug-assert error paths untestable | `bool` returns and messages; null from ensure and add_builtin (W4, W5) |
| 18 | Minor | Model tests too weak (default page size, no per-step hash/bytes checks, no reference-across-share, weak TSan) | W7 rewritten: page shifts 2/4, boundary bias, per-step reference encoders, frozen snapshots, writer+readers TSan, random mix vs reference |
| 19 | Minor | Per-element tagged encoding is slow | Single type tag in the header, then `raw_bytes` per span (W6) |
| 20 | Minor | Re-pinning the P0 digest couples formats | Separate `test_attr_digest`, pinned after W8's page decision |
| 21 | Minor | Page-size rule decides nothing; random gather and copy-on-write amplification ignored | New benchmarks and decision rule (W8) |
| 22 | Minor | `util::span`, no heterogeneous Map lookup, int `IndexRange`, alignment, `UniquePtr` base conversion, missing operators and ctors, CMake target, `Bind<T>` detection | All addressed in §3, W1, W4, W5 and W9. `Bind<T>` deferred to N-API |
| 23 | Minor | P2 interplay: tombstones in hash, uniform pages after permute, permute definition, parallel permute, NOCOPY | `set_default_list`; permute keeps uniform pages, is injective and defaults the rest; parallel group permute; snapshots carry Temp; no NOCOPY flag needed (W4, W5) |
| 24 | Nit | Design 03 still says "static registrar"; scope too large | Design 03 updated (W9); scope reduced by moving pools to P1b |

## 10b. Re-review of v2 (fresh-context agent, 2026-11-18)

**Verdict:** "ready with changes". The architecture holds. The reviewer **verified W0
by compiling it**: with defaulted copy, `Vec` is trivially copyable on clang and gcc,
and every copy form resolves correctly, because there are no generic constructors.
`Vec` is already `force_type_is_simple`, so W0 legalises existing behaviour rather than
changing it.

The claimed fixes for v1 findings 1, 2/3/15/16, 4, 6, 8, 12, 17, 19, 20, 21 and 24
hold. Findings 5, 7, 9/10, 11, 13 and 14 were only partly fixed; they are completed by
the amendments below.

| # | Sev. | Finding | Amendment |
|---|---|---|---|
| 1 | Major | The debug epoch can't detect a write through a reference held across `share()`; it races in const `share()`, and NDEBUG compiles it out of TSan | A1 |
| 2 | Major | The parallel-write contract conflicts with `version()`, the epoch and bool words | A2 |
| 3 | Major | The partial tail can be written while shared; `same_page_as` ignores live length | A3 |
| 4 | Major | The Custom hook reads live, order-dependent sibling state; no claims, no source group, no operator context | A4 |
| 5 | Major | `MixBatch::validate` incomplete (offset checks) and O(size × columns) | A5 |
| 6 | Major | `PageShift` isn't part of column identity, so a test-shift `builtin<>` cast is UB | A6 |
| 7 | Major | INTERFACE FP options miss `attr`'s own TUs; the probe proves nothing | A7 |
| 8 | Major | Temp/Derived exclusion breaks the builtin layout check; read semantics unspecified | A8 |
| 9 | Major | Per-column version counters collide after copy; no schema epoch; public flags | A9 |
| 10 | Major | Unbounded allocation in `read()` from Default/Constant runs | A10 |
| 11 | Minor | Run encoding not canonical as written | A11 |
| 12 | Minor | Hash and bytes disagree on user flags; variable-width encode is ambiguous | A12 |
| 13 | Minor | Group hash composition and column order unspecified | A13 |
| 14 | Minor | Registry `Map`/`string` storage is leak-tracked; `[N]` keys auto-registered | A14 |
| 15 | Minor | Worker-thread leaks invisible to the per-thread tracker | A15 |
| 16 | Minor | `BoolColumn` paging undefined; tail minimum exceeds `kPage` at shift 2 | A16 |
| 17 | Minor | Parallel permute nests `parallel_for` in P2 freeze | A17 |
| 18 | Minor | Missing group `mix_from`, `new_like`, type-erased element bytes | A18 |
| 19 | Minor | Float weights limit f64 precision | A19 |
| 20 | Minor | Model-suite cost unbounded | A20 |
| 21 | Minor | TSan stress test violates the threading contract as written | A21 |
| 22 | Nit | `attr` → `io` drags in bindings and lz4 | A22 |
| 23 | Nit | W0 wording; size assertion | A23 |
| 24 | Nit | Split `column.h` early; string type and registry API inconsistencies | A24 |

## 11. Implementation notes and deviations (2026-11-18)

Implemented on litestl `ubertopo/p1-attr` (from `8473fba` to `f0b8f2c`). The UberTopo
side has the submodule bump, `test_smoke_attr`, design 03/05/06 status notes, DEC-010
and the debugging-guide entries.

| Item | Deviation or note |
|---|---|
| W0 | As planned. **ASan also found an out-of-bounds read** in `fast_expansion_sum_zeroelim`, inherited from Shewchuk's code. It is fixed in the port generator, and outputs are unchanged. LSan suppressions cover binding descriptors as well as the tracker list. `alloc::NewArray` constructs only its first element; this is recorded in the debugging guide for the owner and not changed. |
| W1/W2 | As planned. `io/bytes.h` is header-only and uses a `const_cast` because `util::Vector` has no `const data()`. |
| W3 | `PagedStore`'s page shift is a runtime setting (A6). Version stamps are minted **lazily**: mutations set a dirty bit and `version()` takes a fresh global stamp. Per-element writes therefore cost nothing extra and stamps stay unique. The A1 checksum is verified on copy-on-write of a shared page, on releasing a shared page via a uniform fill, and in the destructor. |
| W4 | **`BoolColumn` stores one byte per element** (`Column<uint8_t, BoolOps>`, key `"bool"`). That makes parallel span writes race-free and paging identical to other columns, so the A16 bit-paging rules don't apply. Packing is a later memory optimisation. **NaN results of arithmetic mixes are canonicalised**, found by a Release-only divergence (see `docs/debugging.md`). Column virtuals take `ValidatedMixBatch` (A5). |
| W5 | **TSan found a pre-existing litestl bug** in `util::Vector::remove_at`: it move-assigned into a destroyed element, double-freeing owning types such as group slots. Fixed, with a regression test. As planned otherwise, with A4/A8/A13/A17/A18. `permute` is serial; callers flatten with `validate_permutation` plus per-column permutes. |
| W6 | As planned with A10–A12. **`kRunMin = 8`.** The header is flags, size and default; then the run count; then the runs. |
| W7 | `test_attr_model`: 7 value types at shifts 2/4/12 (bool 2/6/8), 20k steps each. **Planted bugs** (no copy-on-write, no tail reset, sequential mix) were each caught, with 74–172 failures. Every 16th step also checks page-shift independence of the bytes. |
| W8 | The registry is in `registry.cc`, with builtins registered on first use. The open-extension test is `TestPairColumn` in `test_attr_group.cc`, a full `ColumnBase` over `std::vector` with a claimed-slot Custom mix. The **A1 death test** forks a child that writes through a stale reference and must abort. The A21 threaded stress test runs under TSan. **Page-size benchmark** (clang Release, median of 5 runs, see the table below): shifts 12 and 14 are both within 10% of the best on read speed, and 12 has the least copy-on-write amplification, so **the default stays at 4096** (closes OI-3). `test_attr_digest` is pinned to `eb4c52c53316afaf23dbdc3cd35d0630`. The `_fma` probe (built with `-ffp-contract=fast`) and builds with clang and gcc `-march=x86-64-v3` all reproduce it. |
| W9 | `documentation/attributes.md`, `Readme.MD`, litestl `CLAUDE.md`. `LITESTL_TEST_SCALE_DIV` was added to the harness for sanitizer runs (A20), and the predicate stage-coverage minimum scales with it. |

**Page-size benchmark** (W8):

| Page shift | Sequential read (ns/elem) | Random gather (ns/elem) | Bytes copied per scattered edit |
|---|---|---|---|
| 10 | 0.88 | 5.4 | 3.6 KB |
| 12 | 0.67 | 4.3 | 10.4 KB |
| 14 | 0.69 | 4.3 | 16.5 KB |

**Configuration matrix:** see the run recorded below. It is filled in when the
configuration run completes.

