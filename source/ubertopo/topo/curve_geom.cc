/* Edge carriers (curve_geom.h). */

#include "ubertopo/topo/curve_geom.h"

#include "ubertopo/curves/alignment.h"
#include "ubertopo/curves/offset.h"
#include "ubertopo/topo/geom_store.h"
#include "ubertopo/topo/schema.h"

namespace ubertopo::topo {

namespace attr = litestl::attr;

bool edge_curve_kind_ok(const attr::PoolStore &s, attr::PoolRef r)
{
  if (r.none() || !s.live(r)) {
    return false;
  }
  attr::RefSite site;
  site.kind = attr::RefSite::Column;
  site.a = kRootEdges;
  site.b = attr::TypedGroup<EdgeSchema>::slot<".e.curve">();
  return accept_geom_ref(s, site, r);
}

namespace {

/* an alignment over an offset2: the offset must not fold inside [t0, t1] */
bool fold_free(const attr::PoolStore &s, attr::PoolRef r, double t0, double t1)
{
  using namespace curves;
  if (kind_of(r) != &alignment3_kind()) {
    return true;
  }
  const Alignment3 *a = s.try_get<Alignment3Pool>(r);
  const Offset2 *o = a && kind_of(a->horizontal) == &offset2_kind() ? s.try_get<Offset2Pool>(a->horizontal) : nullptr;
  if (!o) {
    return true;
  }
  const Cesaro2 *b = s.try_get<Cesaro2Pool>(o->base);
  if (!b) {
    return false;
  }
  const Offset2View v(b, o);
  return v.offset_valid(std::fmin(t0, t1), std::fmax(t0, t1));
}

} // namespace

litestl::util::UniquePtr<curves::Curve3> edge_carrier(const attr::PoolStore &s,
                                                      attr::PoolRef r,
                                                      double t0,
                                                      double t1,
                                                      bool *range_bad,
                                                      const char **why)
{
  auto fail = [&](bool range, const char *m) {
    if (range_bad) {
      *range_bad = range;
    }
    if (why) {
      *why = m;
    }
    return litestl::util::UniquePtr<curves::Curve3>();
  };
  if (!edge_curve_kind_ok(s, r)) {
    return fail(false, "the carrier is not live or not a 3D curve kind");
  }
  auto c = curves::curve3(s, r);
  if (!c) {
    return fail(false, "the carrier has no evaluator");
  }
  const curves::Interval d = c->domain();
  if (!std::isfinite(t0) || !std::isfinite(t1) || t0 == t1) {
    return fail(true, "the range's ends must be finite and distinct");
  }
  if (std::fmin(t0, t1) < d.lo || std::fmax(t0, t1) > d.hi) {
    return fail(true, "the range lies outside the carrier's domain");
  }
  if (!fold_free(s, r, t0, t1)) {
    return fail(true, "the range crosses a fold of the offset");
  }
  if (range_bad) {
    *range_bad = false;
  }
  return c;
}

} // namespace ubertopo::topo
