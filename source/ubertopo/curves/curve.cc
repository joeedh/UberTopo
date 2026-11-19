/* Curve kind registry (curve.h). */

#include "ubertopo/curves/curve.h"
#include "ubertopo/curves/bspline.h"
#include "ubertopo/curves/cesaro.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ubertopo::curves {

/* Each kind's translation unit defines its ops object. The list below is the
 * registration order: append new kinds at the end (pool ids, and therefore
 * every stored ref, depend on it). */
util::span<const KindOps *const> kinds()
{
  /* CLAUDENOTE: W4 inserts offset2, alignment3, catenary3 and polyline3
   * between these two, so bspline ends at pool id 6 as the plan fixes. */
  static const KindOps *const list[] = {&cesaro2_kind(), &bspline_kind()};
  return util::span<const KindOps *const>(list, sizeof(list) / sizeof(list[0]));
}

void register_pools(attr::PoolStore &s)
{
  if (s.pool_count() != 0) {
    fprintf(stderr, "ubertopo::curves::register_pools: the store already has pools\n");
    abort();
  }
  for (const KindOps *k : kinds()) {
    k->add_pool(s);
  }
}

uint32_t pool_id(const KindOps &k)
{
  const auto ks = kinds();
  for (size_t i = 0; i < ks.size(); i++) {
    if (ks[i] == &k) {
      return uint32_t(i + 1);
    }
  }
  fprintf(stderr, "ubertopo::curves::pool_id: kind %s is not registered\n", k.key());
  abort();
}

const KindOps *kind_of(attr::PoolRef r)
{
  const auto ks = kinds();
  if (r.pool == 0 || r.pool > ks.size()) {
    return nullptr;
  }
  return ks[r.pool - 1];
}

int curve_dim(const attr::PoolStore &s, attr::PoolRef r)
{
  const KindOps *k = kind_of(r);
  return k && s.live(r) ? k->dim(s, r) : 0;
}

util::UniquePtr<Curve2> curve2(const attr::PoolStore &s, attr::PoolRef r)
{
  const KindOps *k = kind_of(r);
  if (!k || !s.live(r) || k->dim(s, r) != 2) {
    return {};
  }
  return k->make2(s, r);
}

util::UniquePtr<Curve3> curve3(const attr::PoolStore &s, attr::PoolRef r)
{
  const KindOps *k = kind_of(r);
  if (!k || !s.live(r) || k->dim(s, r) != 3) {
    return {};
  }
  return k->make3(s, r);
}

} // namespace ubertopo::curves
