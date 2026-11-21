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
- **Retired 2026-11-21:** the matrix now runs in GitHub Actions, and the local
  check builds natively on Windows (`docs/building.md`). This entry and the WSL
  entries below are kept as history.
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
- **Related: backslash escapes don't survive either.** A `\\n` written inside a heredoc'd Python string reached the C++ file as a real line break, splitting a string literal (`missing terminating '"' character`). Generated C++ that contains escape sequences must come from a script file written with the Write tool, or be fixed afterwards with the Edit tool.

## Sanitizer configurations fail only when mutation checks run at the same time
- **Symptom:** in a matrix run, gcc-ubsan segfaulted and gcc-tsan failed in `test_edit_primitives`. The release and ASan configurations had passed, and rerunning the two configurations alone passed.
- **Cause:** the mutation script edits the shared source files in place while it builds in `ut-clang-rel`. The sanitizer configurations, compiling in parallel, built the mutated sources.
- **Fix / technique:** never run mutation checks while any matrix configuration is still building. Run them before or after the matrix, or in a separate worktree.

## `WireCrossesFace` for a wire that is meant to lie in a face
- **Symptom:** after P3b W8, tests that built a wire inside a face with `make_edge` followed by `embed_edge` started failing at the `make_edge` step with `Invalid`, and the report named `WireCrossesFace`.
- **Cause:** between the two calls the wire is a free wire lying in the face's plane. The validator treats a free wire as crossing any face it touches away from a shared endpoint, so the first primitive leaves the complex invalid.
- **Fix / technique:** create the wire already embedded, with the `in` argument: `make_edge(a, b, id, &e, EmbedTarget::face(f))`. Use the same pattern whenever an intermediate state would be invalid. Do it inside one primitive, or in a transaction validated on commit, never as two primitives each validated on its own.

## A mutation "survives" because the test binary was never rebuilt
- **Symptom:** a planted bug in the grid's rollback path left `test_edit_random` passing, so the new grid check looked useless.
- **Cause:** an earlier edit had broken the test file. A heredoc turned a `\n` escape into a real line break, so the file no longer compiled. The command sent ninja's output to `/dev/null` and then ran the test anyway, which executed the previous binary: no grid check and no mutant.
- **Fix / technique:** make every build-then-run command stop when the build fails. For example, `(ninja t > /tmp/b.log 2>&1 || { echo BUILD FAILED; exit 1; }) && ./t`. Treat a mutation that survives as a reason to check the build first, before concluding anything about the test.

## An evaluator returns garbage made of tiny doubles (`alloc::New` copies its arguments)
- **Symptom:** a curve evaluator built with `litestl::alloc::New<T>(tag, *this)` reported `eval_error()` values such as 5.4e-323, which are small integers read as doubles. Positions were wrong too.
- **Cause:** `alloc::New(const char *tag, Args... args)` takes its constructor arguments **by value**. The evaluator stores a `const Entry &` member, which bound to the by-value copy inside `New`, and that copy is destroyed when `New` returns.
- **Fix / technique:** pass a pointer (`New<T>(tag, this)` with a `const Entry *` constructor). Never pass an object that the constructed object will keep a reference to through `alloc::New` or `util::make_unique`.

## A curve's pieces bisect forever ("too many pieces")
- **Symptom:** `Cesaro2::make` refused simple curves (a 60 m cubic spiral, a 10 km chain) with "too many pieces", after bisecting to the depth limit.
- **Cause:** pieces were accepted only when their whole certified error was at most `kPieceTol·h`. Some terms are proportional to h, chiefly the heading uncertainty at the piece midpoint (δθ·h), so bisection cannot make them smaller relative to h. Once δθ is about 1e-14 rad, which a heading of 2.5 rad reaches through rounding alone, no piece can pass.
- **Fix / technique:** split an error budget into the terms bisection reduces faster than h (Taylor tail, coefficient uncertainty, recurrence rounding) and the terms proportional to h. Accept a piece on the first group, and keep both groups in the certified bound.

## A "structure hash" that excludes columns still changes when columns are added
- **Symptom:** structure goldens pinned before P4a W5, meant to prove that only geometry
  columns changed, failed after the switch. They used
  `AttrGroup::hash_into(h, keep)` with the geometry columns filtered out.
- **Cause:** the filtered group hash still covers the group's size and its builtin
  column count (`h.u32(builtins_)`). Adding builtins (`.e.range`, `.e.geom_flags`,
  `.g.curve_tol`) changes that count, filtered or not.
- **Fix / technique:** a hash meant to survive schema changes must hash the kept
  columns itself (name plus `ColumnBase::hash_into`, in `canonical_order()`). Pin it
  with the old code, and check that the switch leaves it unchanged before trusting it
  as a proof.

## `function_ref` from a const lambda does not compile
- **Symptom:** `reinterpret_cast from 'const (lambda) *' to 'void *' casts away
  qualifiers` in `litestl/util/function.h`, when a `const auto keep = [](...) {...};`
  was passed as an `AttrGroup::ColumnFilter`.
- **Cause:** litestl's `function_ref` stores a `void *` to the callable and does not
  accept const callables.
- **Fix / technique:** declare the lambda without `const` (`auto keep = ...`), or pass
  it as a temporary.

## A carrier fails `CurveEndpoint` at the end the test didn't build
- **Symptom:** a clothoid wire built as `make_vertex(a)`, `make_vertex(b)` at c(L),
  `make_edge(a, b)`, `set_edge_curve(e, ref, 0, L)` fails `CurveEndpoint` for some seeds,
  although b lies exactly on c(L).
- **Cause:** `make_edge` stores the ends in its own order, so `edge_verts(e)[0]` is not
  always `a`. The range (t0, t1) refers to `vs[0]` and `vs[1]`.
- **Fix / technique:** read `k.edge_verts(e)` and pass (L, 0) when `vs[0] != a`. To
  diagnose, print the carrier at both range ends next to both stored vertices: the end
  that matches exactly is not the failing one.

## Windows PowerShell's `Set-Content -Encoding utf8` writes a BOM
- **Symptom:** a header rewritten with
  `(Get-Content f -Raw) -replace ... | Set-Content -Encoding utf8 f` starts with
  `EF BB BF`.
- **Cause:** in PowerShell 5.1, `utf8` means UTF-8 with a BOM.
- **Fix / technique:** edit source files with the editor tools or a Python patch script,
  not PowerShell. Check with `head -c 3 f | od -c`.

## Code with `\n` written through a shell heredoc arrives broken
- **Symptom:** a patch applied through `python - <<'EOF' ... EOF` from Git Bash turns
  a C++ string literal's `\n` into a real line break (`missing terminating '"'
  character`), or a generated Python file gets an unterminated string. Inline
  `wsl -- bash -lc '...'` from Git Bash also lost `$variables`.
- **Cause:** the text passes through more than one layer of quoting and escape
  processing (Git Bash, the heredoc, the Python literal, WSL's command line), and one
  of them interprets the escape.
- **Fix / technique:**
  - Write patch scripts and shell scripts to files with the editor tools, then run
    them by path (`python script.py <root>`, `wsl -d Debian -- bash /mnt/c/.../x.sh`).
  - Never pass code containing backslash escapes through a heredoc or an inline
    command.

## Root clusters just inside a segment end make sliver pieces
- **Symptom:** the approximation of a curve gave pieces a few ulps long next to a
  segment join.
- **Cause:** the root partition cut at every root of the derivative polynomials,
  including clusters of roots within rounding of the segment end, where nothing
  changes.
- **Fix / technique:** cut only at the curve's breaks (continuity < `kSmooth`).
  Features (C∞ special parameters) are places, not cuts (plan P4a §11, W3).

## Staging a whole directory picks up other people's files
- **Symptom:** `git add docs` or `git add source/...` staged research documents and
  scratch output (`Testing/`) that belonged to someone else's work in the same
  checkout.
- **Fix / technique:**
  - Stage explicit paths only.
  - When a file holds both your change and someone else's unstaged edit, stage a blob
    built from `HEAD` plus your change:
    `git hash-object -w --no-filters <file>`, then
    `git update-index --cacheinfo 100644,<sha>,<path>`.
    This leaves the other edit in the working tree, unstaged.

## `content_hash` crashes under ASan on refs into another store
- **Symptom:** `test_edit_primitives` (`test_pool_roots`) passed in release builds but
  failed under gcc-asan (SEGV in `PoolStore::CanonicalMap::map`) and gcc-ubsan.
- **Cause:**
  - The test keeps a dynamic `Column<PoolRef>` whose refs point into its own
    `PoolStore`, not the complex's.
  - Since P4a, `content_hash` maps every ref column through `k.geom`'s canonical map.
    Those refs dangle there, so `canonical_map` failed.
  - The hash then mapped them through the empty map it left, and `CanonicalMap::map`
    indexed `old_to_new` out of range. Release builds read garbage silently.
- **Fix / technique:**
  - `CanonicalMap::map` is now total: out-of-range refs give an unset slot (litestl).
  - `content_hash` hashes the groups with raw ref values after a marker when
    `canonical_map` fails, as plan P4a §4.3 specified.
  - Lesson: run the sanitizer configurations before declaring a wave done. The
    release matrix can't see an out-of-range read that happens to return stable
    garbage.

## A certified bound that is secretly NaN
- **Symptom:** `test_piece_terms` (the white-box test of `cesaro2`'s error terms)
  found `tail` and `local_err` NaN on long pieces: `slow_spiral`'s 5 km piece, and a
  1.5 km clothoid. The oracle test had passed on the same fixtures for weeks.
- **Cause:**
  - The majorant series multiplied a coefficient c_k (underflowing to 0) by rr^k
    (overflowing to ∞), and 0·∞ = NaN.
  - The NaN then hid twice:
    1. `std::max(max_piece, p.err)` keeps the left operand when the right is NaN, so
       the curve's `eval_error` came out finite;
    2. `err > bound` is false when the bound is NaN.
- **Fix / technique:**
  - Carry such series scaled (`c_k rr^k`, bounded by e^bend).
  - Refuse non-finite bounds where they are assembled.
  - In tests, check `std::isfinite` before any comparison with a bound. Every
    comparison against NaN passes vacuously.

## WSL builds crawl when the sources live on /mnt/c
- **Retired 2026-11-21** with the WSL matrix (`docs/building.md`).
- **Symptom:** the full matrix took far longer than its CPU work suggested. Windows
  showed WSL at ~6.5 % CPU. Inside WSL, every compiler process sat in state `D`
  (waiting on I/O) at ~1.5 % CPU, with a load average of ~2.6 on 16 cores, although
  `nproc` was 16 and `ninja -j 16` was running.
- **Cause:** the build trees compiled straight from `/mnt/c/dev/UberTopo`. WSL reaches
  the Windows drive through a slow file-sharing bridge, and every translation unit
  opens hundreds of headers (Eigen, litestl), once per configuration.
- **Fix:** mirror the checkout into the Linux filesystem with
  `rsync -a --delete --exclude=.git /mnt/c/dev/UberTopo/ ~/src/UberTopo/` before
  each build, and configure the build trees against the mirror. Touch every file
  rsync rewrites (next entry). With the mirror,
  the compilers run at 75–97 % CPU each, and the full 13-configuration matrix runs in
  about 25 minutes from clean build trees.
- **Notes:**
  - Edits are made in the Windows checkout. A build only sees them after a sync, so
    every build script syncs first.
  - A mutation runner should edit the mirror, not the checkout.
  - Adding cores through `.wslconfig` doesn't help an I/O-bound build.

## Builds from the WSL mirror link stale objects
- **Retired 2026-11-21** with the WSL matrix (`docs/building.md`).
- **Symptom:** after a sync, gcc-ubsan and gcc-tsan failed to link with undefined
  references to new editor functions, although the mirror's headers declared and
  defined them. In clang-rel, a test failed exactly as it had under a mutant that
  had since been reverted. `ninja -n -d explain` reported no work to do.
- **Cause:** `rsync -a` copies the Windows modification times into the mirror. When
  an object has been built from different content (a mutant written straight into
  the mirror, or a build run without syncing first), the next sync can write the
  correct content with a time older than that object. ninja compares times only, so
  it keeps the stale object.
- **Fix:** after rsync, touch every file it wrote (`--out-format='%i %n'`, lines
  starting `>f`), so a changed file is always newer than any earlier object. Then
  touch the whole mirror once to clear objects that are already stale. A mutation
  runner that restores a file should also push its time forward and rebuild.

## Fins on a curved edge intersect although they looked disjoint when straight
- **Symptom:** a test built three triangular faces on one edge, then put an arc on
  the edge to change its radial order. `set_edge_curve` came back `Invalid` with
  `FaceIntersection` between two of the faces.
- **Cause:** with a curved edge, each triangle becomes a fan from its apex to the
  arc's skeleton. When two apexes are at the same height, the cross-section of one
  fan at a given height is a translate of the other's. A translated copy of an arc
  crosses the original whenever the translation direction lies inside the arc's
  tangent range. That is the same condition that makes the order about the first
  skeleton segment differ from the order about the chord, so the order swap the test
  wanted came with an intersection.
- **Fix / technique:** give the apexes different heights, so the cross-sections are
  scaled by different factors as well as shifted. In `test_face_skeleton`, F2's
  section then lies strictly between F3's arc and its chord.
- **Note:** in a valid complex, the radial order about the first skeleton segment
  usually agrees with the order about the chord. A test that makes them differ
  needs faces that are close in angle near the edge and are separated by their
  heights.

## Carriers interned before `Builder::build` are "not live"
- **Symptom:** a test interned a road (`build_road`) into `k.geom` and then built
  the faces with `Builder::build(k, ...)`. Every following `set_edge_curve` failed
  with `BadArgument`: "the carrier is not live or not a 3D curve kind".
- **Cause:** `Builder::build` replaces the whole complex, and its geometry store
  with it. Refs interned earlier point into the store that was dropped.
- **Fix / technique:** intern into the complex only after `build`. If the
  geometry is also needed to place the vertices, build it twice: once in a
  scratch `PoolStore` (`register_pools` first) to compute positions, then again
  in `k.geom`. `build_road` is deterministic, so both copies agree bit for bit
  (`test_validate_surfaces`).

## A split meant to swap two faces' radial order is refused with `VertexLink`
- **Symptom:** a test for `split_edge_at` wanted the piece from the new vertex to
  sort its faces differently from the piece before it (OI-P4b-8). With projected
  faces the two orders always came out equal. With planar faces whose curved edge
  leaves their planes, the swap appeared, but the split was refused with
  `VertexLink` on the new vertex.
- **Cause:** sheets that meet only along a curve keep one cyclic order along it,
  so a correct sort can't swap them anywhere. The swap needs faces the curve
  doesn't lie in, and then the new vertex, which has degree 2, has a link that
  contradicts itself, so `VertexLink` refuses it. An accepted split therefore never
  shows whether the second piece was re-sorted. The re-sort shows only in how such
  a split is refused: `VertexLink`, or `RadialOrder` on the second piece when
  that piece keeps the edge's stale order.
- **Fix / technique:** test the refusal code instead of a swap
  (`test_split_resorts_both` in `test_edit_curves`). To find configurations, model
  the half-planes in a few lines of Python and search, rather than building
  candidates in C++. A planar face's half-plane point is the first skeleton point
  off the axis, and the half-plane points away from it when that point lies outside
  the face (`HalfPlane::s = -1`). Every face whose projection shows the bend picks
  the same curve knot as that point, so such faces tie (`RadialTie`).

## A mutant that removes a bounds check survives on another refusal's code
- **Symptom:** the mutant that dropped `set_face_surface`'s "not a coedge of the
  face" check survived. The test expected `BadArgument` for a foreign coedge and
  still got it.
- **Cause:** without the check, the search index equals the coedge count, so
  `listed[j]` reads one past the end of a `util::Vector` (inside its inline
  buffer, so nothing crashes in release). The stale byte there was nonzero, and the
  call failed as "a coedge is listed twice", which is also `BadArgument`.
- **Fix / technique:** when several refusals share an `EditCode`, check the
  report's detail text too (`detail_has` in `test_edit_surfaces`). To see why a
  mutant survives, apply it by hand and print `ed.report().detail` after the call.

## A test takes minutes under MSVC's ASan but seconds in Release
- **Symptom:** `test_geom_cdt` took 808 s in `msvc-asan`, against 113 s in
  gcc-asan and 9 s in Release. Every other test took under 50 s.
- **Cause:** its exact oracle (`tests/support/bigint_oracle.h`) copied
  `std::vector` limbs on every determinant expansion. Heap churn is what MSVC's
  ASan allocator handles worst. litestl had already fixed its own copy of the
  oracle (`9a2cd92`), but UberTopo's hand-kept copy was older.
- **Fix / technique:** sync the copy from litestl. To find the slow test, sort
  ctest's per-test times from the log, then compare the same test across the
  configurations.
