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
