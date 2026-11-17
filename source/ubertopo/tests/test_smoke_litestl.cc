/* Smoke test: UberTopo builds against the litestl submodule and can call each
 * P0 foundation API (docs/plans/2026-11-17-p0-litestl-foundations.md, W7). */

#include "litestl/math/predicates.h"
#include "litestl/math/wideint.h"
#include "litestl/util/hash128.h"
#include "litestl/util/rng.h"
#include "litestl/util/task.h"

#include "ut_test.h"

using namespace litestl;

int main()
{
  /* wide integers */
  const math::Int128 p = math::mul_wide(int64_t(1) << 62, int64_t(4));
  UT_CHECK(p.w[0] == 0 && p.w[1] == 1);

  /* predicates: double, float, lattice */
  UT_CHECK(math::pred::orient2d(math::double2(0, 0), math::double2(1, 0), math::double2(0, 1)) == 1);
  UT_CHECK(math::pred::insphere(math::float3(0, 0, 0),
                                math::float3(1, 0, 0),
                                math::float3(0, 1, 0),
                                math::float3(0, 0, -1),
                                math::float3(0.4f, 0.4f, -0.4f)) == 1);
  UT_CHECK(math::pred::orient3d(math::int64_3(0, 0, 0),
                                math::int64_3(1, 0, 0),
                                math::int64_3(0, 1, 0),
                                math::int64_3(0, 0, -1)) == 1);
  const char *what = nullptr;
  UT_CHECK(math::pred::fp_selftest(&what));

  /* hash128 + rng */
  const hash::Hash128 id = hash::Hasher128(hash::DomainTag(1000)).str("ubertopo").u32(1).finish();
  UT_CHECK(id != hash::Hash128{});
  const rng::Stream stream(id);
  const double u = stream.uniform(rng::Stream::element_of(id));
  UT_CHECK(u >= 0.0 && u < 1.0);

  /* deterministic reduction */
  double serial = 0;
  for (int i = 0; i < 100000; i++) {
    serial += 1.0 / (1.0 + i);
  }
  const double par = task::parallel_reduce(
      util::IndexRange(100000),
      100000, /* one chunk: equals the serial left fold */
      0.0,
      [](util::IndexRange r) {
        double s = 0;
        for (int i : r) {
          s += 1.0 / (1.0 + i);
        }
        return s;
      },
      [](double a, double b) { return a + b; });
  UT_CHECK(par == serial);

  return ut_test_end();
}
