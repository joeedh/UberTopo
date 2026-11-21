# Building and testing

Every configuration is a CMake workflow preset in `CMakePresets.json`. A
workflow preset configures, builds and runs ctest in one command:

```
cmake --workflow --preset <name>
```

On Windows, run CMake inside litestl's build environment, which applies vcvars64
from the newest Visual Studio (`cl`, `clang-cl`, CMake and Ninja):

```
node source/litestl/configureEnv.mjs cmake --workflow --preset clang-cl-rel
```

Build trees go to `build/<preset>/`, which git ignores.

## Configurations

| Preset | Host | Compiler, type | What it is for |
|---|---|---|---|
| `clang-rel` | Linux | clang, Release | the baseline |
| `gcc-rel` | Linux | gcc, Release | a second compiler and standard library |
| `clang-v3` | Linux | clang, Release, `-march=x86-64-v3` | AVX2 codegen; FMA stays off (`-ffp-contract=off`) |
| `clang-noint128` | Linux | clang, Release, `LITESTL_NO_INT128` | litestl's portable wide integers |
| `gcc-asan` | Linux | gcc, Debug, ASan | memory errors, with assertions on |
| `gcc-ubsan` | Linux | gcc, Debug, UBSan | undefined behaviour; the first report fails the test |
| `gcc-tsan` | Linux | gcc, RelWithDebInfo, TSan, `LITESTL_ATTR_CHECKS` | data races; attribute access checks |
| `clang-cl-rel` | Windows | clang-cl, Release | the local baseline |
| `msvc-rel` | Windows | MSVC, Release | a third compiler; no `__int128` |
| `msvc-asan` | Windows | MSVC, RelWithDebInfo, ASan | memory errors locally |

- **Sanitizers** are set with the `UT_SANITIZE` cache variable (`address`,
  `undefined` or `thread`). It applies to every target, litestl included, because
  TSan needs every object instrumented. MSVC-style drivers support only `address`.
- **Test environment** comes from the test presets:
  - `LITESTL_TEST_NO_BREAK=1`: report every failure instead of trapping;
  - `LITESTL_TEST_SCALE_DIV=4` under the sanitizers: fewer randomized cases;
  - `UBSAN_OPTIONS=print_stacktrace=1` for UBSan.
- **litestl's tests:** the Linux presets set `UT_LITESTL_TESTS=ON`, so litestl's
  own tests run in every Linux configuration too. The Windows presets leave it
  off. Its N-API conformance tests register only when the node-api headers have
  been fetched (`configureEnv.mjs --fetch-deps`), so they run in litestl's own CI.
- **Under MSVC-style drivers** (`cl`, `clang-cl`), the compile-fail and compile-ok
  tests are skipped (`tests/CMakeLists.txt`), so Windows runs 30 tests where
  Linux runs 35.

## The local check

```
node tools/check.mjs [--clean] [preset...]
```

- It runs workflow presets one after another, by default `clang-cl-rel` and
  `msvc-asan` on Windows.
- **Output:** one line per preset, giving the result, ctest's summary, the
  number of distinct warnings in UberTopo's own sources, and the time. The full
  output goes to `build/<preset>.log`.
- **Exit status:** non-zero if any preset fails or warns.
- **`--clean`** deletes each build directory first. Use it for a phase gate,
  because an incremental build only reports warnings for the files it
  recompiles.
- litestl's own warnings aren't counted. `cl` reports `C4068` (unknown
  `#pragma STDC`) from `predicates/expansion.h`.

## CI

`.github/workflows/ci.yml` runs every preset: the seven Linux ones on
`ubuntu-24.04` and the three Windows ones on `windows-latest`.

- **Triggers:** a push to any branch, pull requests to `master`, or by hand.
- A newer push to the same ref cancels the run in progress.
- On failure, a job uploads `LastTest.log`.
- **The litestl submodule:** CI fetches the pinned commit from
  github.com/joeedh/litestl, so a litestl commit must be pushed before an
  UberTopo commit that points to it.
- **TSan:** the job sets `vm.mmap_rnd_bits=28`. gcc's TSan runtime can't map its
  shadow memory with the runner kernel's 32 bits of mmap randomisation.
- **Compiler versions:** Ubuntu 24.04 has gcc 13 and clang 18. `g++-14` is also
  installed on the image, if a newer gcc is ever needed.

## When a phase is done

- `node tools/check.mjs --clean` passes locally.
- CI is green on the pushed branch, in all ten configurations.
- Branches are pushed by, or at the request of, the owner. Until CI has run,
  phase notes say "local check passed; CI pending".
