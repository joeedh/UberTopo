# Plan P0 — litestl numeric and determinism foundations

- **Status:** **implemented** (litestl branch `ubertopo/p0-foundations`). See §11 for
  implementation notes and deviations from this plan. v2 was the pressure-tested
  version (§10).
- **Date:** 2026-11-17
- **Design:** [core-data-model/06 §2–3](../design/core-data-model/06-modules-litestl-and-phasing.md)
  (phase P0). Rationale is in
  [02](../design/core-data-model/02-coordinates-and-predicates.md) and
  [04](../design/core-data-model/04-identity-versions-journal.md).
- **Repos:**
  - Code goes in **litestl**, branch `ubertopo/p0-foundations`.
  - UberTopo gets the submodule bump and a minimal CMake smoke target, on branch
    `ccr-34437002-4r9a3r`.
- **Decisions in force:**
  - DEC-002: no third-party code; re-implement published algorithms.
  - DEC-003: general-purpose additions go into litestl.
  - DEC-008: push to a litestl feature branch.
  - DEC-009: hash, tuple-encoding and RNG choices.
  - The 40 KB source-file split rule.

## 1. Goal

Give litestl the numeric building blocks that every later phase depends on:

1. **Fixed-width wide integers** `Int128` / `Int256`.
2. **Geometric predicates:** adaptive-exact for `double`/`float`, and exact for `int64`
   lattice inputs (templated on input bit width).
3. **A stable 128-bit hash** with a **type-tagged, domain-separated canonical tuple
   encoding**, for persistent IDs and content hashes.
4. **A counter-based RNG** (Philox4x32-10) with keyed streams and a pinned layout.
5. **Deterministic parallel reduction:** chunking and combine order independent of
   worker count.

All additions are **additive and default-off**. Existing litestl APIs, including
`util/rand.h`, behave identically, so Sculptcore is unaffected.

## 2. Non-goals

- UberTopo's `CoordTraits`, the `Lattice` type and derived predicates (P2).
- The attribute system (P1).
- Plane-based predicates for Booleans (P5).
- Performance tuning beyond "not pathological".
- N-API or binding exposure.
- **Deferred to P1/P2**, and recorded in design 06:
  - a deterministic parallel exclusive scan;
  - a sort that is deterministic across standard libraries (libstdc++, libc++ and the
    MSVC STL break ties in `std::sort` differently).

## 3. Baseline (verified 2026-11-17)

- litestl `765241e` builds with **clang 18.1.3** and **g++ 13.3** (Release) using Ninja
  on Linux. All 18 existing tests pass with both.
- The machine has 4 cores with FMA/AVX2. `LITESTL_WORKERS_COUNT = 12`.
- `__int128` is available on wasm32-emscripten, x86_64-windows-msvc (clang) and
  aarch64-windows-msvc (clang). It is not available on i386 or with cl.exe.
- The official SipHash `vectors.h` (which contains `vectors_sip128`) and the Random123
  `tests/kat_vectors` file are **fetchable** from raw.githubusercontent.com in this
  environment.
- Sculptcore's Windows toolchain is the **GNU-driver `clang++`**
  (`sculptcore/build_files/native-clang.cmake`, which already sets `-ffp-contract=off`).
  That toolchain itself can't be run here (§9 R1).
- litestl's `tests/` is always added (there is no option), and Sculptcore does
  `add_subdirectory(litestl)`, including in WASM builds. New suites therefore run inside
  Sculptcore builds too (see W6).

## 4. Work items

### W0 — Build options and test infrastructure (litestl)

- **New CMake options**, all additive:
  - `LITESTL_BUILD_TESTS` (default ON). UberTopo can then skip litestl's tests and the
    DTL `FetchContent`.
  - `LITESTL_NO_INT128` (default OFF). A **global** compile definition that forces the
    portable wide-integer path in every target, avoiding ODR mismatches.
  - `LITESTL_TEST_SCALE` (default 1). A multiplier on randomised case counts, also
    overridable by an environment variable at run time. ctest `LABELS long` is applied
    to the heavy suites.
- **Test helpers** (`tests/`):
  - `splitmix64.h`: a test-local RNG for generating inputs. It is independent of W4, so
    predicate tests don't depend on Philox correctness.
  - A **failure-counting harness** for bulk loops. It counts mismatches, prints the
    first 10 inputs as hex floats or hex limbs, then calls `test_assert` once. The
    existing `test_assert` breaks on the first failure.
  - `bigint_oracle.h`: an independent arbitrary-precision signed integer
    (sign-magnitude, `uint32_t` limbs, schoolbook add/sub/mul, compare). It is used by
    W1 and W2.
- **CI configurations** to run (§7): clang Release, clang Debug + UBSan, gcc Release,
  clang Release with `-march=x86-64-v3` (FMA available), and clang Release with
  `LITESTL_NO_INT128=ON`.

### W1 — `math/wideint.h`: `Int128`, `Int256` (namespace `litestl::math`)

```cpp
struct Int128 { uint64_t lo; int64_t hi; };   // two's complement; layout never uses __int128
struct Int256 { uint64_t w[4]; };              // two's complement, little-endian limbs
constexpr Int128 Int128::from(int64_t);  constexpr Int256 Int256::from(Int128);
double to_double(Int128|Int256);               // approximate; documented
// wrapping arithmetic (callers size their types; intermediates may overflow harmlessly)
+ - unary-  << >> (shift count < width, asserted)  <=> ==  sign() -> int
constexpr Int128 mul_wide(int64_t, int64_t);
constexpr Int256 mul_wide(Int128, Int128);
```

- **Two implementations, both always compiled:**
  - `detail::portable::` limb routines, with 64×64→128 products from 32-bit halves.
  - A fast path using `unsigned __int128` inside functions only, when
    `__SIZEOF_INT128__` is defined and `LITESTL_NO_INT128` is off.

  The `_umul128` path is **dropped**: clang on windows-msvc defines `__SIZEOF_INT128__`,
  and cl.exe is not a supported toolchain.
- **No division** and **no overflow-check API** on the arithmetic operators. Overflow
  checks would fire spuriously on intermediates under wrapping semantics; only final
  bounds matter.
- `sizeof` and `alignof` are independent of `LITESTL_NO_INT128`.

**Tests** (`tests/test_wideint.cc`):
- The oracle is `bigint_oracle.h`.
- **Both** the portable and fast-path routines are tested in the same binary, against
  the oracle and against each other.
- Operands are drawn from random values over the full range and from near-boundary
  values: 0, ±1, ±2^63, ±2^127, ±2^255, and limb-carry patterns. There are 10⁶ cases per
  operator, multiplied by `LITESTL_TEST_SCALE`.
- `static_assert`s cover `constexpr` evaluation.
- The UBSan configuration catches shift-by-64 bugs in the limb shifts.

### W2 — `math/predicates.{h,cc}` (namespace `litestl::math::pred`)

**API.** All functions return `int` in {−1, 0, +1}.

```cpp
int orient2d(const double2&, const double2&, const double2&);
int orient3d(const double3&, const double3&, const double3&, const double3&);
int incircle(const double2&, ...4);   int insphere(const double3&, ...5);
// float overloads: convert float2/float3 -> double2/double3 (exact), then call the above
template <int Bits = 26> int orient2d(const int64_2&, ...);   // also orient3d, incircle, insphere
```

**Integer vector types.** These are new in `math/vector.h`: `using int64_2 =
Vec<int64_t,2>`, `int64_3` and `int64_4`, with `force_type_is_simple`. The existing
`DEF_VECS` macro would generate names like `int642`, so these are written out
explicitly.

**Sign conventions** follow Shewchuk's published definitions:
- `orient2d > 0` when a, b, c are counter-clockwise.
- `orient3d > 0` when d lies below the plane through a, b, c, where a, b, c appear
  counter-clockwise when viewed from above.
- `incircle > 0` when d is inside the circle through counter-clockwise a, b, c.
- `insphere > 0` when e is inside the sphere through positively oriented a, b, c, d.

They are documented in the header and pinned by hand-computed tests.

**`double` implementation.** A re-implementation of Shewchuk's adaptive-precision
expansion arithmetic (1997):
- Building blocks: `fast_two_sum`, `two_sum`, `two_diff`, Dekker `split` and
  `two_product`, `fast_expansion_sum_zeroelim`, `scale_expansion_zeroelim`.
- Each predicate has stages A, B, C and D (static filter, adaptive stages, exact).
- There is **no FMA dependency** and each stage cites the paper section it implements.

**Floating-point discipline** (`predicates.cc`):
- **Per-file flags,** chosen by `CMAKE_CXX_COMPILER_FRONTEND_VARIANT`, not by `MSVC`:
  - GNU-driver clang/gcc: `-ffp-contract=off -fno-fast-math -fno-associative-math
    -fno-unsafe-math-optimizations`;
  - MSVC-driver (clang-cl): `/fp:precise`.
- **Pragmas as backup:** `#pragma clang fp contract(off)` and `#pragma STDC FP_CONTRACT
  OFF`. GCC ignores the latter, so the flag is what applies for gcc.
- **Compile-time checks:** `#error` if `__FAST_MATH__` is defined, and
  `static_assert(FLT_EVAL_METHOD == 0)`.
- **`detail::fp_selftest()`,** compiled *inside* `predicates.cc` and called by tests. It
  runs two probes:
  1. `a*b+c` with a = 1+2⁻³⁰ and c = −(1+2⁻²⁹), which must give exactly 0 (catches FMA
     contraction);
  2. a `two_sum(2⁵³, 1)` tail check (catches reassociation).

  It is meaningful in the `-march=x86-64-v3` CI configuration.

**Input domain** (corrected). Results are exact for finite inputs, each either 0 or
with |x| in [2⁻ᵐ, 2²⁰⁰]. The lower bound guarantees every exact product component stays
at or above 2⁻¹⁰²², which is safe even under FTZ/DAZ (e.g. `crtfastmath.o` linked in
by another TU):

| Predicate | Degree k | Bound m from k(m+52) ≤ 1022 |
|---|---|---|
| orient2d | 2 | 459 |
| orient3d | 3 | 288 |
| incircle | 4 | 203 |
| insphere | 5 | 152 |

The documented single bound is **[2⁻¹⁵⁰, 2²⁰⁰]**, asserted in debug builds. The
`float` overloads need no restriction: float granularity is at least 2⁻¹⁴⁹, so even
degree 5 stays above 2⁻⁷⁴⁵.

**Stack use.** A direct port of `insphereexact` needs about 180 KB of stack. The
**exact (D) stages use heap scratch** via `litestl::alloc` and free it on return. That
stage is rare, so the cost is irrelevant. Stages A–C stay on the stack, with a bound
documented in the header. A test runs every predicate on a thread with a **64 KB
stack**.

**Integer implementation.** Templated on `Bits`. The accumulator type is chosen by
`static_assert`ed budgets, computed with Hadamard's bound over 27-bit differences when
`Bits = 26`:

| Predicate | Bound | Type |
|---|---|---|
| orient2d | ≤ 2⁵⁵ | **int64** (hot path for CDT) |
| orient3d | ≈ 2^83.4, so 85 bits signed | Int128 |
| incircle | ≈ 2^111.4, so 113 bits signed | Int128 |
| insphere | ≈ 2^140.6, so 142 bits signed | Int256 |

- Minors are formed in int64 (≤ 2⁵⁵) and then `mul_wide` is applied.
- Wrapping intermediates are harmless; only the final bound matters (stated in the
  header).
- Inputs are range-checked in Debug. In Release a cheap `|x| ≤ 2^Bits` check is kept
  behind `LITESTL_PRED_CHECK_RANGE`, which is **ON** by default, because out-of-range
  inputs otherwise wrap silently and give a wrong sign.

**Tests** (`tests/test_predicates.cc`):
- **Independent exact oracle.** Scale the inputs to integers by a common 2^k. Evaluate
  the **Leibniz expansion of the homogeneous matrix on raw coordinates** (3×3, 4×4 and
  5×5 lifted forms) with `bigint_oracle.h`. This is a different formulation from the
  difference form used by the implementation.
- **Stage coverage.** Per-stage hit counters are compiled in under
  `LITESTL_PRED_STATS`, in the tests build. The requirement is **≥ 10⁴ hits for every
  stage of every predicate**.
- **Generators that reach the adaptive stages:**
  - Exactly co-circular and co-spherical integer configurations built from
    **rational parametrisations**: for circles, (1−t²)/(1+t²), 2t/(1+t²) scaled; for
    spheres, inverse stereographic projection.
  - Those configurations translated far from the origin by exactly representable
    offsets.
  - Points perturbed by ±1–3 ulps.
  - Balanced true signs (+1, −1 and 0 each make up at least 20% of the degenerate
    suite).
- **Kettner et al.'s "Classroom Examples" grid:** orient2d near (0.5, 0.5) on a 256×256
  ulp grid.
- **Extreme exponents:** edges of the documented domain, kept to about 10⁴ cases because
  of oracle cost.
- **Metamorphic checks:** swapping two points flips the sign; even permutations
  preserve it.
- **Integer versions** at the full ±2^26 range against the oracle. Double and integer
  predicates must agree on lattice points.
- **`fp_selftest()`** passes.
- **Volume:** 10⁶ uniform cases plus 10⁶ degenerate cases per predicate, multiplied by
  `LITESTL_TEST_SCALE`, with label `long`. Bulk loops use the failure-counting harness.

**Size.** The predicates are expected to exceed 40 KB, so split from the start:
`math/predicates.h` (API), `math/predicates/expansion.h` (building blocks, internal),
`orient.cc`, `incircle.cc`, `insphere.cc` and `integer.cc`.

### W3 — `util/hash128.h`: `Hash128`, `Hasher128`, canonical tuple encoding (namespace `litestl::hash`)

**Algorithm (DEC-009).** SipHash-2-4 with 128-bit output, as defined by the **reference
implementation**. The 128-bit variant is not described in the 2012 paper.
- **Why:** it is simple, keyed and has a streaming form, and the reference ships
  128-bit vectors.
- **Limitation:** a public fixed key gives no adversarial collision resistance, which is
  acceptable here.
- **Attribution:** the vector data comes from the reference repo (CC0).

```cpp
struct Hash128 { uint64_t lo, hi;
  auto operator<=>(const Hash128&) const;          // hi then lo, unsigned
  bool operator==(const Hash128&) const = default;
  uint64_t computeHash() const;                     // litestl Map/Set key
  void to_le_bytes(uint8_t out[16]) const; static Hash128 from_le_bytes(const uint8_t*);
  util::string hex() const; };

enum class DomainTag : uint64_t;                    // registry of constants, documented
class Hasher128 {
public:
  Hasher128(DomainTag domain, Hash128 key = kDefaultKey);   // domain written first, always
  // Type-tagged encoders: each writes a 1-byte type tag, then little-endian payload.
  void u8(uint8_t); void u32(uint32_t); void i32(int32_t); void u64(uint64_t); void i64(int64_t);
  void boolean(bool); void f32_bits(float); void f64_bits(double);   // raw bit patterns
  void f64_canonical(double);   // -0 -> +0; any NaN -> 0x7ff8000000000000
  void h128(Hash128); void str(std::string_view);    // u64 LE length prefix, then bytes
  void raw_bytes(const void*, size_t);               // untagged; content hashing only
  Hash128 finish() const;
  // test seam: same encoding written to a byte sink
  template <typename Sink> static void encode_u32(Sink&, uint32_t); /* ...one per encoder */
};
Hash128 hash128(const void *p, size_t n, Hash128 key = kDefaultKey);   // plain SipHash
```

- **Structural collisions are impossible:** type tags guarantee that `u32(1), u32(2)`
  and `u64(2<<32|1)` encode differently. The mandatory `DomainTag` separates generator
  IDs from operator-derived IDs from content hashes. This answers the collision concern
  with ID derivation in design 04 §3.
- **Which `f64` encoder to use:**
  - `f64_canonical` is for **identity** derivation from geometry (quantised positions).
  - `f64_bits` and `raw_bytes` are for **content** hashes (P1 pages, P3 undo
    equality), where −0 and +0 must differ.
- **Endianness.** litestl assumes little-endian, so there is a `static_assert` on host
  byte order. Encoders are tested against golden byte sequences through the sink seam,
  not on a simulated big-endian host.

**Tests** (`tests/test_hash128.cc`):
- All 64 official `vectors_sip128` entries.
- Streaming equivalence under arbitrary splits.
- Golden byte sequences for every encoder.
- The −0/+0 and NaN rules for both f64 encoders.
- Ordering by `<=>`.
- Hex and LE-bytes round trips.
- **Pinned golden hashes** for representative ID tuples, e.g. `Hasher128(DomainTag::OpDerived)`
  then `.h128(parent).u32(OP_SPLIT).u8(SIDE_L)`. Changing them is a deliberate,
  reviewed act.

### W4 — `util/rng.h`: Philox4x32-10 and keyed streams (namespace `litestl::rng`)

```cpp
template <int Rounds = 10> constexpr Counter philox4x32(Counter ctr, Key key);  // Counter = u32[4], Key = u32[2]
class Stream {
public:
  explicit Stream(Hash128 stream_key);     // Philox key = stream_key.lo; stream_key.hi -> salt (below)
  uint32_t u32(uint64_t element, uint32_t draw = 0) const;
  uint64_t u64(uint64_t element, uint32_t draw = 0) const;
  double   uniform(uint64_t element, uint32_t draw = 0) const;   // [0,1), 53 bits
  float    uniformf(uint64_t element, uint32_t draw = 0) const;  // [0,1), 24 bits
  int64_t  range(uint64_t element, int64_t lo, int64_t hi_excl, uint32_t draw = 0) const;
  static uint64_t element_of(Hash128 persistent_id);  // fold: lo ^ mix64(hi), documented
};
```

**Pinned counter layout:** `ctr = {elem_lo32, elem_hi32, draw, block}`.
- `block = 0` for the primary draw.
- `range()` rejection uses `block = 1, 2, …` for the *same* `(element, draw)`, so it
  never reuses bits the caller would get from `draw + 1`.
- The four lanes of a block are consumed in order, as documented.
- `stream_key.hi` is XORed into `ctr[3]`'s high 16 bits as a salt (documented), so all
  128 bits of the stream key matter.

**Element folding.** A 128-bit persistent ID folds to a 64-bit element. With about 2³⁰
elements per stream there is a few-percent chance that some pair collides, which would
only correlate those two elements' draws. This is documented as acceptable.

**Tests** (`tests/test_rng.cc`):
- The Random123 `kat_vectors` lines for `philox4x32 10` (and `philox4x32 7`, since
  rounds is a template parameter). Attribution: Random123 is BSD-3.
- **Pinned goldens** for `Stream::u32`, `u64`, `uniform`, `uniformf` and `range`,
  including a high-rejection span of 2³¹+1. These replace the planned "statelessness"
  test, which is trivially true for a pure function.
- Chi-square smoke test (2^20 draws, 256 bins, fixed key); `range` bounds and coverage.

### W5 — `util/task.h`: deterministic reduction

**Verified problem.** `parallel_for` uses `min(worker_count, ceil(size/grain))` bands
(`task.h:346-347`). Band boundaries therefore depend on the core count.

```cpp
/* Chunk k = [start + k*chunk, min(end, start+(k+1)*chunk)). `chunk` must be a constant,
 * never derived from hardware. cb(chunk_index, IndexRange) runs exactly once per chunk. */
template <typename CB> void parallel_for_chunks(IndexRange r, int chunk, CB cb);
template <typename T, typename Map, typename Combine>
T parallel_reduce(IndexRange r, int chunk, T identity, Map map, Combine combine);
// per-chunk results stored by chunk index, left-folded on the caller

class ScopedBandCount {   // test/diagnostic hook: thread-local override of the band count
public: explicit ScopedBandCount(int n);  // clamped to [1, LITESTL_WORKERS_COUNT]; may exceed core count
};
```

- **Implementation.**
  - Chunks are claimed from an atomic counter by the participating bands, for load
    balance; results go into a `util::Vector<T>` indexed by chunk.
  - The band count comes from `ScopedBandCount` if one is set on the calling thread,
    otherwise from the existing rule.
  - The clamp is required by the fixed `BandTask tasks[LITESTL_WORKERS_COUNT]` array at
    `task.h:402`.
  - There is **no pool-global state**, so concurrent callers such as Sculptcore are
    unaffected.
- **Nesting.** When called from a pool worker (a thread-local flag set in the worker
  main loop), `parallel_reduce` and `parallel_for_chunks` run **serially**. That avoids
  starving the pool through waits that don't help with the work. The result is
  bit-identical, because chunking doesn't depend on the band count. Plain
  `parallel_for`'s behaviour is unchanged.

**Tests** (`tests/test_task.cc`, extended):
- A floating-point sum of 10⁷ values with wide dynamic range, at `ScopedBandCount` of 1,
  2, 3, 4, 7 and 12. Each result is compared **bitwise against a serial reference**
  (chunk sums, then a left fold).
- The hook is **asserted to take effect** by counting distinct bands that executed.
- **Negative control:** a naive per-band sum over the same data gives *different* bits
  for different band counts, which proves the data is sensitive.
- `parallel_for_chunks` visits each index exactly once when the size isn't divisible by
  the chunk.
- Nested call from inside a pool task: completes, and gives the same result.

### W6 — Build integration, determinism digest and docs (litestl)

- **CMake:**
  - Options from W0.
  - Per-file FP flags for `math/predicates/*.cc`.
  - New headers added to the `SRC` lists.
  - New tests registered, with `LABELS long` on the heavy ones.
  - Test targets compiled with `-ffp-contract=off`, so generated inputs don't change
    under gcc's default `-ffp-contract=fast` on FMA targets.
- **`tests/test_determinism_digest.cc`.** Runs a fixed, small workload through every new
  API: wide-integer ops, every predicate on a fixed corpus, hash encoders and IDs, RNG
  streams, and `parallel_reduce` at several band counts. It hashes all outputs with
  `Hasher128` and **compares against a pinned constant in the test**. Every
  configuration then checks itself, which covers cross-compiler agreement and drift
  over time with no manual comparison.
- **`documentation/numerics.md`.** Covers wide integers, predicates (conventions, input
  domain, stack use, flags), `hash128` (tuple encoding, domain tags, which f64 encoder
  to use), RNG layout, and deterministic reduction. Linked from `Readme.MD`. The
  litestl `CLAUDE.md` layout section is updated.
- **WASM:**
  - wasm32 uses the `__int128` path.
  - The portable path serves i386, cl.exe and `LITESTL_NO_INT128`.
  - The exact stages use heap scratch, which suits Emscripten's small pthread stacks.
- **No new file over 40 KB.**

### W7 — UberTopo integration and docs

- Bump `source/litestl` to the pushed `ubertopo/p0-foundations` head.
- **Re-bump** to the merged commit if that branch is later merged into litestl `master`,
  so the pinned commit stays reachable after any squash-merge or branch deletion.
- Add a top-level `CMakeLists.txt`:
  - `LITESTL_BUILD_TESTS=OFF`;
  - `-ffp-contract=off` project-wide (design 04 §8.6);
  - smoke test `source/ubertopo/tests/test_smoke_litestl.cc`, which calls each new API
    once.
- **UberTopo docs** (*done during planning*; verify they're still accurate after
  implementation):
  - DEC-009 (hash, tuple encoding, RNG layout; resolves OI-4).
  - Design README open-issues table (OI-4 resolved).
  - Design 02 §3.1 bit budgets, updated to the numbers in W2.
  - Namespaces in design 02/04/06 aligned: `litestl::math` (`Int128`, `Int256`, `pred`),
    `litestl::hash` (`Hash128`, `Hasher128`), `litestl::rng`.
  - `docs/index.md`.
  - `docs/debugging.md` entries for anything non-obvious found.

## 5. Order and dependencies

```
W0 (options, oracle, splitmix64, harness)
 ├─► W1 wideint ──► W2 integer predicates
 ├─► W2 double predicates
 ├─► W3 hash128 ──► W4 rng
 └─► W5 deterministic reduce
W6 digest after W1–W5; W7 last
```

**litestl commit sequence:** W0, W1, W3, W4, W5, W2-double, W2-integer, W6. Every commit
builds and passes tests. W2 doesn't depend on W4, because tests use the local
`splitmix64`.

## 6. Acceptance criteria

1. **All builds pass.** Every CI configuration in W0 passes all existing and new tests:
   clang Release, clang Debug + UBSan, gcc Release, clang `-march=x86-64-v3` and clang
   `LITESTL_NO_INT128`.
2. **Predicates:** zero oracle disagreements at `LITESTL_TEST_SCALE=1` (≥ 2·10⁶ cases per
   predicate), **≥ 10⁴ hits on every stage**, metamorphic checks pass, `fp_selftest`
   passes in all configurations, and the 64 KB-stack test passes.
3. **Wide integers:** both the portable and fast paths agree with the oracle and with
   each other. `constexpr` tests compile. UBSan is clean.
4. **hash128:** all 64 official 128-bit vectors match, plus the encoder golden bytes
   and the pinned ID goldens.
5. **RNG:** the Random123 vectors (10 and 7 rounds) and the pinned `Stream` goldens
   match.
6. **Determinism:**
   - `parallel_reduce` is bit-equal to the serial reference at band counts 1, 2, 3, 4,
     7 and 12.
   - The negative control differs.
   - The hook demonstrably takes effect.
   - The pinned digest matches in **every** configuration.
7. **Hygiene:**
   - No new file over 40 KB.
   - No `CLAUDENOTE:` comments in pushed code.
   - litestl docs updated.
   - Sculptcore-visible behaviour unchanged, verified by the existing 18 tests passing
     unmodified.
8. **Integration:** the UberTopo smoke target builds and passes against the bumped
   submodule, with litestl tests disabled. The UberTopo docs from W7 are updated.

## 7. Verification commands

```
B=build   # under the scratchpad or a gitignored build/ dir
cfg() { cmake -S source/litestl -B $B/$1 -G Ninja "${@:2}" && cmake --build $B/$1 && (cd $B/$1 && ctest --output-on-failure); }
CC=clang CXX=clang++ cfg lt-clang-rel   -DCMAKE_BUILD_TYPE=Release
CC=clang CXX=clang++ cfg lt-clang-ubsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-sanitize-recover"
CC=gcc   CXX=g++     cfg lt-gcc-rel     -DCMAKE_BUILD_TYPE=Release
CC=clang CXX=clang++ cfg lt-clang-v3    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-march=x86-64-v3"
CC=clang CXX=clang++ cfg lt-clang-noi128 -DCMAKE_BUILD_TYPE=Release -DLITESTL_NO_INT128=ON
# UberTopo
cmake -S . -B $B/ut -G Ninja && cmake --build $B/ut && (cd $B/ut && ctest --output-on-failure)
```

## 8. Estimated size

| Item | Code (LOC) | Tests (LOC) |
|---|---|---|
| W0 infrastructure | ~100 (CMake) | ~350 (oracle, harness, splitmix) |
| W1 wideint | ~600 | ~300 |
| W2 predicates | ~1800, split across files | ~900 |
| W3 hash128 | ~350 | ~300 |
| W4 rng | ~200 | ~150 |
| W5 reduce | ~150 | ~200 |
| W6 digest | — | ~150 |
| Docs | — | — |

These are rough figures.

## 9. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | Sculptcore's Windows clang toolchain is untestable here | The int128 path there matches Linux clang (`__SIZEOF_INT128__` is defined). The portable path is tested via `LITESTL_NO_INT128`. Flag selection uses the frontend variant. **Owner to run the suite on Windows once** |
| R2 | Subtle bugs in the re-implemented adaptive predicates | Independent oracle (different formulation), required per-stage coverage, adversarial co-circular and co-spherical generators, metamorphic checks, and stage-by-stage citation of the paper |
| R3 | FP flags leak or contraction goes undetected | Per-file flags chosen by frontend variant, pragmas, `#error`/`static_assert`, an in-TU `fp_selftest` with real FMA probes, and an FMA-enabled CI configuration |
| R4 | ~~Official vectors unavailable~~ **Retired:** verified fetchable. If fetching later fails, stop and ask the owner. Never substitute recalled values | — |
| R5 | `ScopedBandCount` affects other callers | Thread-local and default-off, with no global state. Existing tests pass unchanged |
| R6 | New suites slow down Sculptcore builds and CI | `LITESTL_TEST_SCALE`, `LABELS long`, small extreme-exponent subsets, and `LITESTL_BUILD_TESTS` for consumers that opt out |
| R7 | Pinned goldens (IDs, RNG, digest) make later intentional changes painful | Intended: they are the determinism contract. Changing them requires a DEC entry and a test update in the same commit |

## 10. Pressure-test review (fresh-context agent, 2026-11-17)

**Verdict:** "ready with changes". There were 21 findings, 9 of them major; none needed
a redesign. The reviewer also ran experiments:
- confirmed the gcc baseline and that the official vectors are fetchable;
- found that the planned FMA canary (a Dekker `two_product` residual check) can't detect
  contraction;
- confirmed which platforms have `__int128`.

All findings were accepted. Their disposition:

| # | Sev. | Finding (summary) | Change made |
|---|---|---|---|
| 1 | Major | The predicate input domain [2⁻²⁰⁰, 2²⁰⁰] is wrong for insphere (granularity underflow, FTZ/DAZ) | Per-predicate derivation; single bound [2⁻¹⁵⁰, 2²⁰⁰]; edge-of-domain tests (W2) |
| 2 | Major | The FMA canary can't detect contraction; no FMA build; `/fp:precise` keyed on the wrong variable | `fp_selftest` in the TU with real probes, `-march=x86-64-v3` configuration, pragmas, frontend-variant flags (W0, W2) |
| 3 | Major | Random tests rarely reach stages B–D; oracle not independent; no stage coverage | Per-stage counters (≥ 10⁴), rational-parametrisation generators, translation, balanced signs, metamorphic checks, Leibniz homogeneous oracle (W2) |
| 4 | Major | The worker-limit test passes even if the hook does nothing; "max" is only 4 cores; a global cap races | Thread-local `ScopedBandCount` clamped to 12; serial reference; hook-effect assertion; negative control; band counts 1–12 (W5) |
| 5 | Major | `LITESTL_NO_INT128` per test is an ODR problem and can't reach `predicates.cc`; the `__int128` cross-check is circular | Global option; both paths compiled and tested; `_umul128` dropped; layout independent of the macro; UBSan configuration (W0, W1) |
| 6 | Major | Canonical tuple encoding and domain separation undefined | Type-tagged encoders, mandatory `DomainTag`, u64 length prefix (W3, DEC-009) |
| 7 | Major | `f64` normalisation is wrong for content hashes | `f64_bits` vs `f64_canonical` with documented consumers; canonical NaN pinned; more encoders (W3) |
| 8 | Major | `range()` rejection collides with `draw+1`; layout inconsistent; KATs don't cover `Stream` | Layout `{elem_lo, elem_hi, draw, block}`; rejection on `block`; pinned `Stream` goldens (W4) |
| 9 | Major | Determinism digest had no work item and was checked manually | `test_determinism_digest` with a pinned constant; tests built with `-ffp-contract=off` (W6) |
| 10 | Minor | `int64_2`/`int64_3` and `util::string_view` don't exist | Explicit `Vec<int64_t,N>` aliases; `std::string_view` (W2, W3) |
| 11 | Minor | Bit budgets imprecise; orient2d fits int64; not templated on bits | Hadamard budgets; `template <int Bits>`; Release range check (W2) |
| 12 | Minor | Exact stages need about 180 KB of stack (WASM/macOS thread risk) | Heap scratch for stage D; 64 KB-stack test (W2) |
| 13 | Minor | Oracle cost and test runtime inside Sculptcore builds | `LITESTL_TEST_SCALE`, `LABELS long`, small extreme subset (W0) |
| 14 | Minor | Predicate tests need an RNG before W4; `test_assert` breaks on the first failure | Test-local splitmix64; failure-counting harness; reordered commits (W0, §5) |
| 15 | Minor | Phantom "nested guard"; nested starvation; scan/sort determinism needed later | Serial when nested; chunk-alignment and constant-chunk rules documented; scan and sort deferred to P1/P2 (W5, §2) |
| 16 | Minor | `Hash128` needs `<=>`, bytes and hex; simulated big-endian test pointless; namespace drift | Added; `static_assert` plus golden-bytes sink; namespaces unified (W3, W7) |
| 17 | Minor | Stream key and element truncated to 64 bits | Key-hi salt; documented element fold; 7-round KATs (W4) |
| 18 | Minor | UberTopo would pull litestl tests and DTL; FP flags; submodule commit reachability | `LITESTL_BUILD_TESTS`; project `-ffp-contract=off`; re-bump after merge (W0, W7) |
| 19 | Minor | UberTopo docs (DEC entry, OI-4, design 02 budgets, index) not covered | Added to W7 |
| 20 | Nit | SipHash-128 isn't in the paper; rationale; attribution | Fixed citation, rationale and attribution (W3, W4) |
| 21 | Nit | Global vs thread-local undecided; float promotion helper; LOC estimate low | Thread-local; conversion helper; estimates raised (W2, W5, §8) |

## 11. Implementation notes and deviations (2026-11-17)

Implemented on litestl `ubertopo/p0-foundations`, one commit per work item. The UberTopo
side has the submodule bump, a top-level `CMakeLists.txt` and the smoke test.

| Item | Deviation or note |
|---|---|
| W0 | **Harness improvements** beyond the plan:<br>• `test_break` flushes stdout before trapping, so diagnostics already printed aren't lost.<br>• `LITESTL_TEST_NO_BREAK=1` reports every failure instead of trapping on the first.<br>• `LITESTL_PRINT_GOLDENS=1` prints pinned goldens.<br>• All test targets build with `-ffp-contract=off`.<br>• litestl `CLAUDE.md` now documents the harness conventions. |
| W1 | `Int128`/`Int256` are `WideInt<2>`/`WideInt<4>`, one template rather than separate structs. The API is unchanged. A planted carry bug was caught by the oracle; it is only observable in `Int256`, because with two words there is no carry-in chain. |
| W2 | **Predicates are their own libraries**, `predicates` and `predicates_stats`, rather than part of `math`. That way the strict FP flags apply to exactly those translation units, and the stats variant can be linked by tests only.<br>**The double predicates are a mechanical port** of Shewchuk's public-domain `predicates.c`, made by a generator script committed in `math/predicates/generator/`. That is lower-risk than a free re-implementation. The original host (cs.cmu.edu) returned 403, so a GitHub mirror was used.<br>**`-fno-reciprocal-math`** was added to the flag set.<br>**Integer range violations abort** in Release too: `LITESTL_PRED_CHECK_RANGE=1` by default. |
| W2 tests | **Case counts** at `LITESTL_TEST_SCALE=1` are about 400k per predicate, not 2·10⁶. Each case is evaluated three times (direct, swap, rotation), all checked against the oracle or the metamorphic relation. Release runtime is about 45 s; a larger count was not worth the oracle cost.<br>**Stage coverage** far exceeds the ≥10⁴ target, with stage D at 130k–240k hits per predicate. Two generator families made the difference:<br>• the **span family**: power-of-two multiples of base vectors. These give exact zeros with inexact differences for *all four* predicates, because collinear points lift to a plane and coplanar points to a hyperplane.<br>• the **near-filter family**: exact lattice configurations with 1–4096 ulp perturbations, which reaches stage B.<br>**A leak was found and fixed**: the heap scratch wasn't released, because the generator's regex missed the return-statement form. The generator now asserts it found exactly one return.<br>**Domain bound confirmed**: the only oracle disagreements seen were for subnormal inputs created by a generator bug, which lie outside the documented domain. The generator never perturbs zero coordinates now. |
| W3 | As planned. 64/64 official vectors pass. |
| W4 | **Stream key derivation changed:** the Philox key is `key.lo ^ mixHash(key.hi)`, instead of salting `ctr[3]` with `key.hi`. All 128 key bits still matter, and `ctr[3]` stays a full block index. DEC-009 and design 04 are updated to match. `util/hash.h` `mixHash` became `constexpr` (additive). |
| W5 | As planned. `ScopedBandCount` is thread-local. Chunked APIs run serially on pool workers. Atomic chunk claiming was *not* added, because static band assignment of chunk indices was simpler and results are stored by index anyway. |
| W6 | `test_determinism_digest` is pinned to `faa8caf7a78cea9f195969b9ce97109a`. The digest is also the check that the five configurations agree. |
| W6 matrix | **Results (2026-11-18):**<br>• clang Release, gcc Release, clang `-march=x86-64-v3` and clang `LITESTL_NO_INT128` all pass 23/23 with the same pinned digest.<br>• **UBSan ran under gcc Debug**, because clang's UBSan runtime (`libclang_rt.ubsan_standalone`) isn't installed in this environment. It found one issue, a pre-existing signed overflow in `test_task`'s busy-work smoke loop, which is now fixed.<br>• The predicate suite takes about 10 minutes in Debug + UBSan, against about 45 s in Release.<br>• The first gcc run exposed an argument-evaluation-order bug in the digest test itself (see `docs/debugging.md`). |
| W7 | UberTopo `CMakeLists.txt`, `source/ubertopo/tests/` (own minimal harness `ut_test.h`, because litestl's harness depends on DTL) and `test_smoke_litestl`. `.gitignore` ignores `build/`. |
