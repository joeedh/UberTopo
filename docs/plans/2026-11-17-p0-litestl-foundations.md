# Plan P0 — litestl numeric and determinism foundations

- **Status:** draft, awaiting pressure test
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
  - The 40 KB source-file split rule.

## 1. Goal

Give litestl the numeric building blocks that every later phase depends on:

1. **Fixed-width wide integers** `Int128` / `Int256`, for exact lattice predicates.
2. **Geometric predicates:** adaptive-exact for `double`/`float`, and exact for
   `int64` lattice inputs.
3. **A stable 128-bit hash** (`hash128`) with canonical-encoding helpers, for
   persistent IDs and content hashes.
4. **A counter-based RNG** (Philox4x32-10) with keyed streams.
5. **Deterministic parallel reduction:** chunking and combine order independent of
   worker count.

All additions are **additive**. Existing litestl APIs, including `util/rand.h`, are left
unchanged, so Sculptcore is unaffected.

## 2. Non-goals

- UberTopo's `CoordTraits`, the `Lattice` type and derived predicates (P2).
- The attribute system (P1).
- Plane-based predicates for Booleans (P5).
- Performance tuning beyond "not pathological".
- Any N-API or binding exposure of the new types.

## 3. Baseline (verified 2026-11-17)

- litestl `765241e` configures and builds with **clang 18.1.3** + Ninja on Linux. All
  18 existing tests pass under `ctest`.
- g++ 13.3 is also available and will be used for the cross-compiler determinism checks.
- Windows/MSVC-ABI clang (Sculptcore's toolchain) is **not** available in this
  environment. See §9 R1.

## 4. Work items

### W1 — `math/wideint.h`: `Int128`, `Int256`

**API** (namespace `litestl::math`):

```cpp
struct Int128 { uint64_t lo; int64_t hi; ... };   // two's complement
struct Int256 { uint64_t w[4]; ... };              // two's complement, little-endian limbs

// construction / conversion
constexpr Int128 Int128::from(int64_t);  constexpr Int256 Int256::from(Int128);
double to_double(Int128|Int256);          // correctly rounded is NOT required; documented as approximate

// arithmetic (wrapping, like built-in unsigned; callers size their types)
+ - unary-  << >> (by constant < width)  == != < <= > >=  sign() -> int
// widening multiplies (the only multiplies predicates need)
constexpr Int128 mul_wide(int64_t, int64_t);
constexpr Int256 mul_wide(Int128, Int128);
// checked helpers for debug builds
bool add_overflows(a, b); bool sub_overflows(a, b);
```

**Implementation:**
- Portable limb code using `uint64_t`, with 64×64→128 products built from 32-bit halves.
- Fast paths where available:
  - `unsigned __int128` when `__SIZEOF_INT128__` is defined;
  - `_umul128` on MSVC-ABI.
- **No division** is provided. Predicates never need it, and it sidesteps the
  `__divti3` link issues on MSVC-ABI clang.
- Header-only and `constexpr` where possible.

**Tests** (`tests/test_wideint.cc`):
- An **independent test-only big integer** (`tests/bigint_oracle.h`: sign-magnitude,
  `uint32_t` limbs, schoolbook add/sub/mul, compare). This is deliberately a different
  algorithm and representation from W1.
- 10⁶ randomised operations per operator, with operands drawn from the full range and
  from near-boundary values (±2^63, ±2^127, 0, ±1). Results are compared with the
  oracle.
- Where `__int128` exists, `Int128` is also cross-checked against it.
- `constexpr` evaluation is exercised with `static_assert`s.

### W2 — `math/predicates.h` / `math/predicates.cc`

**API** (namespace `litestl::math::pred`). All functions return `int` in {−1, 0, +1}:

```cpp
int orient2d(const double2 &a, const double2 &b, const double2 &c);
int orient3d(const double3 &a, const double3 &b, const double3 &c, const double3 &d);
int incircle(const double2 &a, const double2 &b, const double2 &c, const double2 &d);
int insphere(const double3 &a, ..., const double3 &e);
// float overloads: promote to double (exact), then call the double versions
// exact integer versions; inputs must satisfy |x| <= 2^kLatticeInputBits (asserted in debug)
int orient2d(const int64_2 &...);  int orient3d(const int64_3 &...);
int incircle(const int64_2 &...);  int insphere(const int64_3 &...);
constexpr int kLatticeInputBits = 26;
```

**Sign conventions** follow Shewchuk's published definitions, documented in the header
and asserted by tests:
- `orient2d > 0` when a, b, c are counter-clockwise.
- `orient3d > 0` when d lies below the plane through a, b, c, where a, b, c appear
  counter-clockwise when viewed from above.
- `incircle > 0` when d is inside the circle through counter-clockwise a, b, c.
- `insphere > 0` when e is inside the sphere through positively oriented a, b, c, d.

**`double` implementation.** A **re-implementation** of Shewchuk's adaptive-precision
floating-point expansion algorithms (1997), per DEC-002:
- Building blocks: `fast_two_sum`, `two_sum`, `two_diff`, Dekker `split`,
  `two_product`, `grow_expansion` / `fast_expansion_sum_zeroelim`,
  `scale_expansion_zeroelim`.
- Each predicate has a static error-bound filter stage and an adaptive stage, falling
  back to a fully exact stage.
- **No FMA**: Dekker split everywhere, so results don't depend on FMA availability.
- Implementations live in **`predicates.cc`**, not inline, so that compile flags can be
  forced per file:
  - `-ffp-contract=off -fno-fast-math` (GCC/Clang), `/fp:precise` (MSVC);
  - a `#error` if `__FAST_MATH__` is defined;
  - a `static_assert` that `FLT_EVAL_METHOD == 0`, which rejects x87 extended
    precision.

**Input domain** (documented): finite doubles. Results are guaranteed while no
intermediate overflows or underflows, i.e. inputs with |x| in [2⁻²⁰⁰, 2²⁰⁰] or exactly
0. Outside that domain behaviour is unspecified and asserted in debug builds. This
matches the assumptions of the original algorithm.

**Integer implementation.**
- The determinants are computed exactly with `Int128` (orient2d, orient3d, incircle)
  and `Int256` (insphere), sized from `kLatticeInputBits`.
- Bit budgets are **re-derived in the header comment** for each predicate. The working
  estimates for 27-bit differences are ≈ 84 (orient3d), 111 (incircle) and 140
  (insphere) bits. A `static_assert` ties each budget to the chosen type.

**Tests** (`tests/test_predicates.cc`):
- **Exact oracle.** Every finite double is m·2^e. For each test case, scale all inputs
  by a common 2^k to integers, evaluate the determinant with `bigint_oracle.h`, and take
  the sign. This is independent of the expansion arithmetic.
- **Random cases:** 10⁶ uniform cases per predicate.
- **Degenerate generators:**
  - exactly collinear, coplanar, cocircular and cospherical points (constructed on
    integer grids, then scaled by powers of two);
  - near-degenerate points (perturbed by 1–3 ulps);
  - Shewchuk's classic failure case, `orient2d` on points near (0.5, 0.5) on a
    256×256 ulp grid, which naive evaluation gets wrong.
- **Integer versions:** random and degenerate cases at the full ±2^26 range, checked
  against the oracle.
- **Cross-check:** the double and integer predicates agree on lattice points converted
  exactly to double.
- **Conventions:** fixed hand-computed cases for every sign convention.

### W3 — `util/hash.h`: `hash128`, `Hasher128`, canonical encoding

**Resolves OI-4.** The algorithm is **SipHash-2-4 with 128-bit output** (Aumasson &
Bernstein, 2012), re-implemented.

**Why SipHash-2-4:**
- It is fully specified and has official **test vectors for 128-bit output**, which
  gives a strong golden test.
- Its streaming form is simple.
- It is keyed, so the project can fix a domain key.
- It is fast enough for the main load, short ID keys.

Page content hashing in P1 may need a faster function. If measurements show that, P1
adds one behind the same `Hasher128` interface. This plan doesn't pre-empt that.

```cpp
struct Hash128 { uint64_t lo, hi; bool operator==(...) const; uint64_t computeHash() const; };
class Hasher128 {                         // streaming
public:
  explicit Hasher128(Hash128 key = kDefaultKey);
  void bytes(const void *p, size_t n);
  // canonical encoders: fixed little-endian byte order regardless of host
  void u8(uint8_t); void u32(uint32_t); void u64(uint64_t); void i64(int64_t);
  void f64(double);    // bit pattern; -0.0 -> +0.0, all NaNs -> one canonical NaN
  void h128(Hash128);  void str(util::string_view);   // length-prefixed
  Hash128 finish() const;
};
Hash128 hash128(const void *p, size_t n, Hash128 key = kDefaultKey);
```

- `Hash128::computeHash()` makes it usable directly as a litestl `Map`/`Set` key, via
  the existing `HashCls` concept.

**Tests** (`tests/test_hash128.cc`):
- **Official vectors:** the published SipHash-2-4 128-bit-output test vectors (the
  64-entry table from the reference implementation for key `00..0f` and message
  `00..n-1`). See §9 R4 for how they are obtained.
- **Streaming equivalence:** any split of the input into `bytes()` calls gives the same
  hash.
- **Canonical encoders:**
  - same hash on little-endian and big-endian *simulated* byte orders, by testing the
    encoder output bytes directly;
  - `-0.0` and `+0.0` encode identically;
  - NaN payloads are normalised.
- **Golden values** for a few UberTopo-style ID derivations, e.g.
  `H(H(seed), "split", 1)`, pinned in the test. A change to them must be deliberate.

### W4 — `util/rng.h`: Philox4x32-10 and keyed streams

```cpp
namespace litestl::rng {
using Counter = std::array<uint32_t, 4>; using Key = std::array<uint32_t, 2>;
Counter philox4x32_10(Counter ctr, Key key);           // pure function, constexpr

class Stream {                                          // keyed, stateless per draw
public:
  Stream(Hash128 key);                                   // key = H(world_seed, op_site, stream_name) from caller
  uint32_t u32(uint64_t element, uint32_t draw = 0) const;   // counter = (element lo/hi, draw, lane)
  uint64_t u64(uint64_t element, uint32_t draw = 0) const;
  double   uniform(uint64_t element, uint32_t draw = 0) const;  // [0,1), 53 bits
  float    uniformf(uint64_t element, uint32_t draw = 0) const; // [0,1), 24 bits
  int      range(uint64_t element, int lo, int hi_excl, uint32_t draw = 0) const; // unbiased (rejection on draw lanes)
};
}
```

- **Counter layout** (documented): `ctr = {elem_lo32, elem_hi32, draw, 0}`. One block
  gives four 32-bit lanes.
- **`range` is unbiased:** it uses Lemire's multiply-shift with rejection, drawing from
  successive lanes or blocks *deterministically*.

**Tests** (`tests/test_rng.cc`):
- **Random123 known-answer vectors** for philox4x32-10 (zero, all-ones and π-digit
  cases), from Random123's `kat_vectors`. See §9 R4.
- **Statelessness:** `u32(e, d)` is identical regardless of call order. Shuffled orders
  are tested.
- **Distribution smoke tests:** chi-square on 2^20 draws over 256 bins, using fixed
  seeds so the result is deterministic. Mean and variance of `uniform`. `range`
  boundary coverage, and no value outside `[lo, hi)`.

### W5 — `util/task.h`: deterministic reduction

**The problem.** `parallel_for` cuts a range into `min(worker_count, tasks)` bands
(verified in `task.h`). Band boundaries therefore depend on the worker count, so any
floating-point reduction done per band is nondeterministic across machines.

**The fix.** Add:

```cpp
/* Chunks are [start + k*chunk, ...) for k = 0..n-1, independent of worker count.
 * cb(chunk_index, IndexRange) runs exactly once per chunk, on any thread. */
template <typename CB> void parallel_for_chunks(IndexRange r, int chunk, CB cb);

/* Deterministic: per-chunk results are stored by chunk index and combined
 * left-to-right on the calling thread. combine need not be associative in
 * floating point for the result to be reproducible. */
template <typename T, typename Map, typename Combine>
T parallel_reduce(IndexRange r, int chunk, T identity, Map map /* (IndexRange)->T */, Combine combine);
```

- **Scheduling.** Chunks are dispatched over `parallel_for` bands. Each band iterates
  the chunk indices it owns. The *chunk* boundaries depend only on `chunk`.
- **Storage.** Per-chunk results live in a `util::Vector<T>` sized to the chunk count.
- **Test hook.** Add `task::ScopedWorkerLimit(int n)`. It caps the band count used by
  `parallel_for` within the scope (thread-local or a pool-global cap), and lets tests
  run the same reduction at 1, 2 and N effective workers.

**Tests** (`tests/test_task.cc`, extended):
- A floating-point sum of 10⁷ values with wide dynamic range, at worker limits 1, 2, 3
  and max. The results must be **bit-identical**.
- `parallel_for_chunks` visits every index exactly once, including when the size is not
  divisible by the chunk size.
- Nested-use guard behaviour is unchanged.

### W6 — build integration and docs (litestl)

- **CMake:**
  - Add `predicates.cc` to the `math` library with the per-file flags from W2.
  - Add the new headers to the `SRC` lists.
  - Register the new tests with the `test()` macro.
- **Docs.** Add `documentation/numerics.md` covering wide integers, predicates (sign
  conventions and input domain), hash128 and the canonical encoding, RNG streams, and
  deterministic reduction. Link it from `Readme.MD`.
- **Size rule.** Keep each new file under 40 KB. `predicates.cc` is the likely
  candidate to exceed it. If it does, split it into
  `math/predicates/{expansion.h, orient.cc, incircle.cc, integer.cc}`.
- **WASM.** The new code must still compile under Emscripten, since litestl supports
  WASM for Sculptcore. Use the portable fallbacks if `__int128` is unavailable. This is
  checked by inspection plus a compile with `-DLITESTL_NO_INT128` forcing the portable
  path in native tests.

### W7 — UberTopo integration

- Bump `source/litestl` to the pushed `ubertopo/p0-foundations` head.
- Add a minimal top-level `CMakeLists.txt` that builds litestl and one smoke test:
  `source/ubertopo/tests/test_smoke_litestl.cc`, which calls each new API once.
- **Debugging guide.** Record anything non-obvious found during P0 in
  `docs/debugging.md`.

## 5. Order and dependencies

```
W1 wideint ──► W2 predicates (integer part)
W2 double part (independent of W1)
W3 hash128 ──► W4 rng (Stream key type)
W5 independent
W6 alongside each item; W7 last
```

Suggested commit sequence on the litestl branch: W1, W3, W5, W2-double, W2-integer, W4,
W6 docs. Each commit has passing tests.

## 6. Acceptance criteria

1. All existing litestl tests still pass, plus the new suites. This holds under
   **clang 18 Debug and Release** and under **g++ 13 Release**.
2. **Predicates:** zero disagreements with the big-integer oracle across all random and
   degenerate suites (≥ 10⁶ cases per predicate per build).
3. **Wide integers:** zero disagreements with the oracle; `constexpr` tests compile.
4. **hash128:** all official SipHash-2-4-128 vectors match. Golden ID hashes match on
   clang and gcc.
5. **RNG:** all Random123 known-answer vectors match.
6. **Determinism:** the reduction is bit-identical across worker limits 1, 2, 3 and max,
   and across clang and gcc builds. A test prints a digest of every new suite's outputs,
   and the digests must match between the clang and gcc runs.
7. **Hygiene:** no new file over 40 KB; no `CLAUDENOTE:` comments left in pushed code;
   docs updated.
8. **Integration:** the UberTopo smoke target builds against the bumped submodule.

## 7. Verification commands

```
# litestl
CC=clang CXX=clang++ cmake -S source/litestl -B build/lt-clang -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/lt-clang && (cd build/lt-clang && ctest --output-on-failure)
CC=gcc CXX=g++     cmake -S source/litestl -B build/lt-gcc   -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/lt-gcc   && (cd build/lt-gcc && ctest --output-on-failure)
# compare the determinism digests printed by test_determinism_digest in both builds
# UberTopo
cmake -S . -B build/ut -G Ninja && cmake --build build/ut && (cd build/ut && ctest --output-on-failure)
```

## 8. Estimated size

| Item | Code (LOC) | Tests (LOC) |
|---|---|---|
| Wide integers | ~500 | ~300 |
| Predicates | ~1200 | ~600 |
| Oracle | — | ~250 |
| hash128 | ~200 | ~200 |
| RNG | ~150 | ~150 |
| Deterministic reduce | ~120 | ~100 |
| Docs | — | — |

These are rough figures.

## 9. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | MSVC-ABI clang (Sculptcore's Windows toolchain) can't be tested here. `__int128` behaviour and link dependencies (`__multi3`, `__divti3`) differ there | No division API. Portable fallback forced on with `LITESTL_NO_INT128` and tested on Linux. Flag for a Windows check by the owner |
| R2 | Subtle bugs in the re-implemented adaptive predicates | Independent exact oracle with degenerate-heavy generators. Code review against the published paper's pseudo-code; each stage cites the paper section |
| R3 | Compiler flags leak: a header-inline predicate compiled with contraction on | All predicate arithmetic stays in `.cc` files with forced flags. `#error` on `__FAST_MATH__`. A test that would fail under FMA contraction (two_product residual check) |
| R4 | Network policy may block fetching official test vectors (SipHash reference repo, Random123 `kat_vectors`) | Try fetching. If blocked, ask the owner to provide the vectors or allow the host. **Do not** substitute vectors recalled from memory as "official"; at most mark them provisional. Self-generated values are only regression goldens, not correctness evidence |
| R5 | `ScopedWorkerLimit` touches the global pool; risk of affecting Sculptcore | Additive and default-off. When no limit is set, behaviour is identical. Covered by existing task tests |
| R6 | Performance regressions in Sculptcore | None expected; all changes are additive. Sculptcore keeps its submodule pin until it chooses to bump |

## 10. Pressure-test review

_To be filled in by a fresh-context review agent (CLAUDE.md rule). Findings and any
resulting plan changes are recorded below before implementation starts._
