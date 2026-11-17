#pragma once

/* Minimal UberTopo test harness: UT_CHECK records failures (with location)
 * and keeps going; ut_test_end() returns the process exit code. */

#include <cstdio>

namespace ubertopo::test {
inline int &failures()
{
  static int n = 0;
  return n;
}
} // namespace ubertopo::test

#define UT_CHECK(expr)                                                                   \
  do {                                                                                   \
    if (!(expr)) {                                                                       \
      ::ubertopo::test::failures()++;                                                    \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr);           \
    }                                                                                    \
  } while (0)

inline int ut_test_end()
{
  const int n = ubertopo::test::failures();
  if (n) {
    fprintf(stderr, "%d check(s) failed\n", n);
  }
  return n ? 1 : 0;
}
