/* The geometry store's accepted kinds (geom_store.h). */

#include "ubertopo/topo/geom_store.h"

#include "ubertopo/curves/alignment.h"
#include "ubertopo/curves/bspline.h"
#include "ubertopo/curves/catenary.h"
#include "ubertopo/curves/cesaro.h"
#include "ubertopo/curves/offset.h"
#include "ubertopo/curves/pcurve.h"
#include "ubertopo/curves/polyline.h"
#include "ubertopo/curves/road.h"

namespace ubertopo::topo {

bool accept_geom_ref(const attr::PoolStore &s, const attr::RefSite &site, attr::PoolRef r)
{
  using namespace curves;
  const KindOps *k = kind_of(r);
  if (!k) {
    return false; /* not a curve pool */
  }
  switch (site.kind) {
    case attr::RefSite::Column:
      if (site.a == kRootEdges && site.b == attr::TypedGroup<EdgeSchema>::slot<".e.curve">()) {
        if (k == &alignment3_kind() || k == &catenary3_kind() || k == &polyline3_kind()) {
          return true;
        }
        const BSpline *b = k == &bspline_kind() ? s.try_get<BSplinePool>(r) : nullptr;
        return b && b->dim == 3;
      }
      if (site.a == kRootCoedges && site.b == attr::TypedGroup<CoedgeSchema>::slot<".c.pcurve">()) {
        /* which of the two fits the edge (curved or straight) is the
         * validator's PcurveKind (plan P4b §4.1) */
        return k == &graph2_kind() || k == &segment2_kind();
      }
      if (site.a == kRootFaces && site.b == attr::TypedGroup<FaceSchema>::slot<".f.surface">()) {
        return k == &road3_kind();
      }
      return true;
    case attr::RefSite::Inner: {
      const auto ks = kinds();
      const KindOps *holder = site.a >= 1 && site.a <= ks.size() ? ks[site.a - 1] : nullptr;
      if (holder == &offset2_kind()) {
        return k == &cesaro2_kind();
      }
      if (holder == &alignment3_kind()) {
        return k == &cesaro2_kind() || k == &offset2_kind();
      }
      if (holder == &road3_kind()) {
        return k == &cesaro2_kind();
      }
      if (holder == &bspline_kind()) {
        /* a bspline's source: any curve kind except pcurves and surfaces */
        return k != &graph2_kind() && k != &segment2_kind() && k != &road3_kind();
      }
      return false; /* no other kind holds refs */
    }
    default:
      return true;
  }
}

} // namespace ubertopo::topo
