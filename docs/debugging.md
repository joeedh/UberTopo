# Debugging Guide

Running log of debugging techniques, known pitfalls and their fixes. Add an entry
whenever a non-obvious problem is diagnosed.

Entry format:

```
## <short symptom>
- **Symptom:** what was observed
- **Cause:** root cause
- **Fix / technique:** how it was found and fixed
```

## Determinism digest differs between gcc and clang
- **Symptom:** `test_determinism_digest` (litestl) passed with clang but failed with gcc,
  while every other suite, including the pinned hash and RNG goldens, passed on both.
- **Cause:** the test drew several random values inside a single call expression, e.g.
  `int64_3(rng(), rng(), rng())`. C++ leaves function-argument evaluation order
  unspecified: clang evaluates left to right and gcc right to left, so the generated
  inputs differed. The same applies to the operands of `+` and similar operators.
- **Fix / technique:** sequence every draw into a named local before use. Any code
  whose *output* must be deterministic (procedural generators, test corpora, digests)
  must never pull more than one value from a stateful RNG within one expression.
  Stateless keyed streams (`litestl::rng::Stream`) avoid the problem by design. The
  digest test caught this on purpose: always run it under both compilers.

## Predicate oracle mismatches only on subnormal inputs
- **Symptom:** rare `incircle` failures in `test_predicates` near-filter cases; every
  failing case contained a subnormal coordinate (`0x0.000...p-1022`).
- **Cause:** the generator perturbed a coordinate that was exactly 0 by "k ulps",
  producing subnormals. These lie outside the documented exact domain (0 or |x| in
  [2⁻¹⁵⁰, 2²⁰⁰]), where products underflow.
- **Fix / technique:** generators never perturb zero coordinates. Debug builds of the
  predicates abort on out-of-domain inputs; run the predicate tests in Debug once when
  changing generators.

## Leak tracker reports `insphereexact scratch` blocks
- **Symptom:** litestl's leak tracker listed many 180 KB "insphereexact scratch" blocks
  at the end of `test_predicates`.
- **Cause:** the predicate port generator inserts `alloc::release` before the return,
  but its regex didn't match the actual return-statement form, so nothing was
  inserted.
- **Fix / technique:** the generator now asserts that it rewrote exactly one return.
  `test_end()` returns failure on leaks, so leak regressions fail the suite.

## ASan stack-buffer-overflow inside `fast_expansion_sum_zeroelim`
- **Symptom:** gcc Debug + ASan reported a stack-buffer-overflow read in
  `litestl::math::pred::detail::fast_expansion_sum_zeroelim`, reached from
  `orient2dadapt`. The release builds and all oracle tests were fine.
- **Cause:** Shewchuk's original code does `enow = e[++eindex];` even when `eindex`
  reaches `elen`. The value is never used, because the loop conditions check the index,
  but the read is one element past the end of the array, which is undefined behaviour.
- **Fix / technique:** the port generator rewrites these reads as bounded reads
  (`(++i < elen) ? e[i] : 0.0`). Behaviour is unchanged, as the predicate and digest
  suites confirm. Run the ASan configuration whenever vendored or ported numeric code
  changes.

## litestl leak report lists objects at `test_end()`
- **Symptom:** a new attribute test printed "Vector data", "RcBuffer" and
  "attr::Column" blocks and exited 1, although nothing actually leaked.
- **Cause:** `test_end()` runs the litestl leak tracker. Objects declared in `main()`
  are still alive at that point.
- **Fix / technique:** put test bodies in functions or blocks, so every object is
  destroyed before `test_end()`. Threaded tests also compare `alloc::getMemorySize()`
  with a baseline, because the per-thread tracker lists can't see worker-thread
  allocations.

## gcc ASan fails on pre-existing litestl tests
- **Symptom:** under gcc Debug + ASan, `test_task` failed with LeakSanitizer reports,
  and `test_binding_system` with a `new-delete-type-mismatch`.
- **Cause:** LeakSanitizer flagged intentional process-lifetime allocations (the
  per-thread tracker list, and binding descriptors that are deliberately never freed).
  `BindingBase` lacked a virtual destructor while being deleted through base pointers,
  which is real undefined behaviour.
- **Fix / technique:** `tests/lsan.supp`, wired through ctest's `ENVIRONMENT`, and a
  virtual `~BindingBase()`. To reproduce outside ctest, set
  `LSAN_OPTIONS=suppressions=<litestl>/tests/lsan.supp`.

## `alloc::NewArray` constructed only its first element (fixed)
- **Symptom:** none observed in practice (no callers in litestl or Sculptcore). Found
  while reading `util/alloc.h` during P1.
- **Cause:** two bugs.
  - `new (elem) T(...)` never advanced the pointer, so slot 0 was constructed `size`
    times and slots 1 onwards were never constructed.
  - The by-value arguments were `std::forward`ed inside the loop, so movable arguments
    were moved from on the first iteration and later elements would have got
    moved-from values.
- **Fix / technique:** construct `elems + i` from lvalue copies of the arguments, with
  `size_t` indices (in `DeleteArray` too). The regression test `tests/test_alloc.cc`
  checks each slot's self-pointer and a heap-allocated string argument; it fails on the
  old code (23 asserts) and passes now. A placement-new loop must advance its target,
  and arguments must not be forwarded more than once.

## Attribute model suite diverges only in Release: NaN payloads
- **Symptom:** `test_attr_model` passed in Debug, under all sanitizers, and with gcc,
  but failed in clang Release. The only differing values were NaNs with different
  payloads (e.g. `0x7fc0000e` vs `0x7fc00001`).
- **Cause:** when arithmetic combines NaNs, IEEE 754 leaves the result's payload
  unspecified. On x86 the first operand's payload propagates, and an optimiser may
  legally swap the operands of commutative operations. The payload produced by
  Interpolate, WeightedMean or Sum therefore depends on the compiler and optimisation
  level.
- **Fix / technique:** arithmetic mix kernels canonicalise NaN results to the default
  quiet NaN (`0x7fc00000` / `0x7ff8000000000000`). Copy policies still preserve
  payloads. Any deterministic floating-point output must canonicalise NaNs produced by
  arithmetic. The model suite now prints the first differing index and the raw bytes
  on a content mismatch, which made this a one-run diagnosis.

## TSan heap-use-after-free in `AttrGroup::remove` (litestl `Vector::remove_at`)
- **Symptom:** gcc TSan reported a heap-use-after-free and double frees inside
  `util::Vector<AttrGroup::Slot>::remove_at`, reached from `AttrGroup::remove`. Other
  configurations passed silently.
- **Cause:** a pre-existing litestl bug. For non-trivially-copyable `T`, `remove_at`
  ran `data_[i].~T()` and then **move-assigned into the destroyed object**. For an
  owning type (a `UniquePtr` inside `Slot`), the assignment freed the dangling pointer a
  second time.
- **Fix / technique:**
  - Shift by move-assignment, which releases the removed value, and destroy only the
    vacated last slot.
  - The regression test `test_vector.cc` `test_remove_owning` uses an
    ownership-tracking element type; it fails on the old code (29 asserts) and passes
    now.
  - Lesson: run the sanitizer configurations on new container usage patterns. Owning
    element types exercise paths that trivially copyable ones skip.

## Line endings silently changed by edit scripts
- **Symptom:** a 72-line change to a litestl test showed as a 238-line diff.
- **Cause:** several litestl files use CRLF line endings. Python's text-mode
  read/write converted them to LF.
- **Fix / technique:** restore with a binary rewrite, and check
  `git diff --ignore-cr-at-eol --stat` against the original. When scripting edits to
  litestl files, open them in binary mode or preserve `newline=''`.

## Undefined `litestl::platform::cpu_core_count()` when linking UberTopo tests
- **Symptom:** a test that linked `ubertopo_topo` failed to link with an undefined
  reference to `cpu_core_count()` from `task.cc`, although `platform` was listed among
  the dependencies.
- **Cause:** litestl's `platform` is an **OBJECT** library. CMake adds its objects only
  to targets that link it directly. When a static library (`ubertopo_topo`) linked it,
  the objects ended up inside that archive, which appears *before* `libutil.a` on the
  link line, so the single-pass linker had already passed them when `util` needed
  them.
- **Fix / technique:** `ut_test()` links `platform` into every test executable. Library
  targets don't list it. To diagnose this kind of failure, read the
  `LINK_LIBRARIES` line in `build.ninja` for the failing target.

## Builder rejects a hand-written hole as `HoleOrientation`
- **Symptom:** radial-order fixtures with a hole failed to build, reporting
  `HoleOrientation`.
- **Cause:** the fixture's hole loop was counter-clockwise, the same winding as the
  outer loop. The builder enforces opposite windings, because a hole wound like its
  outer loop makes the wall's half-plane at the hole edge point into the hole. That
  gives false coplanar ties and wrong regions (P2 plan review, finding 6).
- **Fix / technique:** wind holes clockwise when seen from the face's front, so the
  face interior is to the left of every coedge. To check a hand-written loop, compute
  its shoelace sum in the face's own (u, w) coordinates.


## Crash in `build_regions()` after freeing elements
- **Symptom:** a segfault in `build_regions()`, at the union-find `find`, on a complex from which a fence had been freed. Under ASan it showed up as a wild index into the face-side array.
- **Causes**, both in a test helper:
  1. It picked "sheets in region 0" to free, which also matched open-window walls. Those share edges with other walls, so freeing them left dangling radial links.
  2. It freed coedges while walking their loop. `Domain::free` resets every column but `.gen`, including `.c.next`, so the walk stopped after the first coedge. The remaining coedges stayed live on freed loops and faces, with `.l.f = NONE`.
- **Fix / technique:**
  - free only components that are closed under edge adjacency;
  - collect elements first, then free them;
  - `build_regions()` assumes a complex that passes the combinatorial validator. Run `validate()` first when a crash shows up there.

## LeakSanitizer reports `getMemList` leaks in UberTopo tests
- **Symptom:** under gcc ASan, `test_topo_regions`, `test_topo_freeze` and `test_topo_determinism` failed with LeakSanitizer "direct leak" reports, each with `getMemList` (`litestl/util/alloc.cc`) on the stack. The P2a tests passed in the same configuration.
- **Cause:** litestl keeps one leak-tracker list per thread and deliberately never frees it. P2b is the first UberTopo code to allocate inside pool worker threads (`parallel_for_chunks` in `build_regions`), so those lists now exist. litestl's own ctests pass `tests/lsan.supp`, but UberTopo's didn't.
- **Fix / technique:** `ut_test()` sets `LSAN_OPTIONS=suppressions=<litestl>/tests/lsan.supp`. The real leak check is still each test's `alloc::getMemorySize() == baseline` assertion. Suppress only by the intentional-allocation function name, never by test.

## "Redefinition of `UniquePtr`" when a header includes `util/memory.h` twice
- **Symptom:** clang reported redefinitions of `shared_ptr`, `weak_ptr`, `UniquePtr` and `make_unique` from `util/memory.h` as soon as a second header (`attr/pool.h`) included it.
- **Cause:** litestl's `util/memory.h` had no `#pragma once`. Earlier code happened to include it only once per translation unit.
- **Fix / technique:** add the guard (done in P1b). When a litestl header produces redefinition errors, check its first line before suspecting include paths.

## A `function_ref` bound to a lambda temporary
- **Symptom:** in `test_attr_pool`, a `collect` with `Roots::accept` set failed on a column that held no offending refs; the same call had passed earlier in the test.
- **Cause:** `PoolStore::AcceptFn acc = [&](...) {...};` binds a `util::function_ref` to a lambda temporary that dies at the end of the statement, so later calls read a dangling object.
- **Fix / technique:** name the lambda first (`auto f = [&](...) {...}; AcceptFn acc = f;`). The same applies to `AttrGroup::PolicyFn` and `ColumnFilter`, which are also `function_ref`s.

## Dynamic Temp columns vanish after an `AttrGroup` round trip
- **Symptom:** the pool model test failed after a write/read round trip, and later `remove("dyn")` failed, for a dynamic column created with `ColumnFlags::Temp`.
- **Cause:** `AttrGroup::write` skips dynamic Temp and Derived columns entirely, not just their data. Builtin Temp columns keep their slot and come back with default values.
- **Fix / technique:** after reading, treat a dynamic Temp or Derived column as absent, and `ensure` it again if needed.

## Building the configuration matrix on the Windows workstation
- **Symptom:** the matrix needs Linux gcc and clang with ASan, UBSan and TSan, but the Windows host has only MSVC and clang-cl.
- **Fix / technique:**
  - Use WSL Debian, with `clang gcc g++ cmake ninja-build git` installed from apt.
  - Point the sources at `/mnt/c/dev/UberTopo` and put the build directories under the WSL home (`~/b/<proj>-<cfg>`); that is much faster than building on `/mnt/c`.
  - Working copies on Windows are CRLF (`core.autocrlf=true`), so scripts that patch files must preserve each file's line endings.

## TSan: "FATAL: ThreadSanitizer: unexpected memory mapping" under WSL
- **Symptom:** in the gcc TSan configuration every test failed within 0.01 s with `FATAL: ThreadSanitizer: unexpected memory mapping 0x...`. The other configurations passed.
- **Cause:** the WSL2 kernel (6.18) randomises mmap addresses with more bits than gcc 14's TSan runtime expects. Fixing that through `vm.mmap_rnd_bits` needs root.
- **Fix / technique:** run the TSan tests with ASLR disabled for the process, which needs no root: `setarch $(uname -m) -R ctest ...`, or the same prefix on a single test binary.

## Editor journal empties itself after a failed primitive
- **Symptom:** after any failed primitive, the next `undo()` returned `JournalStale`, or the journal was empty.
- **Cause:** the journal detects writes made outside the editor by comparing the complex's version stamp with the stamp after the last journaled step. A rollback restores bytes but issues *new* column version stamps, so the restored complex looked externally modified.
- **Fix / technique:** re-sync the journal's expected version after a full rollback, and at `begin` when the journal is already consistent (`editor_impl_txn.h`).

## Use-after-free on a dynamic column after undoing `rebuild_regions`
- **Symptom:** ASan heap-use-after-free in `Column<...>::operator[]` right after `Editor::undo()` (test_edit_primitives, the pool-roots test).
- **Cause:** undoing a barrier restores a whole-complex snapshot by move-assignment, which replaces every group's column objects. Pointers to dynamic columns taken earlier now dangle.
- **Fix / technique:** fetch column pointers again (`group.find<...>(name)`) after undoing a barrier. Builtin columns are reached through slots and are unaffected. The same holds for `io::read`.

## `validate_global` hangs and allocates without bound
- **Symptom:** a test sat at 100% CPU with memory growing past 800 MB inside `validate_global` (in `test_edit_random`, and later in `test_edit_primitives`).
- **Cause:** faces whose triangulation step returned early without reporting kept uninitialised `FaceMesh` bounding boxes. The uniform contact grid then iterated over astronomically many cells. The early return came from a refactor's text substitution that also matched a deeper-indented `continue`.
- **Fix / technique:** every live face that isn't fully built gets a bounding box from its vertices. When converting a loop body to a lambda, replace `continue` only at the loop's own nesting level, and re-run the full suite before trusting the refactor.

## Validator reports `NonPlanarFace` for a face that is planar
- **Symptom:** `split_face` failed validation with `NonPlanarFace` on a planar face.
- **Cause:** the chord ran between two vertices on one straight side (a midpoint inserted by `split_edge` lies between them), so one child's corners were all collinear. `face_is_planar` reports degenerate faces as non-planar.
- **Fix / technique:** `split_face` now checks both children for degeneracy first and returns `DegenerateLoop`.

## Bash heredocs with apostrophes fail in this environment
- **Symptom:** `python - <<'EOF' ... EOF` in the Bash tool failed with `unexpected EOF while looking for matching '` whenever the script contained an apostrophe (for example in a comment like "doesn't"), even though the heredoc delimiter was quoted.
- **Fix / technique:** write multi-line scripts to a file in the scratchpad with the Write tool and run them with `python <file>`. Keep heredocs for scripts without apostrophes.

## Sanitizer configurations fail only when mutation checks run at the same time
- **Symptom:** in a matrix run, gcc-ubsan segfaulted and gcc-tsan failed in `test_edit_primitives`. The release and ASan configurations had passed, and rerunning the two configurations alone passed.
- **Cause:** the mutation script edits the shared source files in place while it builds in `ut-clang-rel`. The sanitizer configurations, compiling in parallel, built the mutated sources.
- **Fix / technique:** never run mutation checks while any matrix configuration is still building. Run them before or after the matrix, or in a separate worktree.
