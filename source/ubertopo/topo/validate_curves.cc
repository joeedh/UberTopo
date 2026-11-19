/* Embedding layer, curve part (plan P4a §5.3): edge carriers against the
 * vertex skeleton, and the geometry P4a does not support yet. */

#include <bit>
#include <cmath>

#include "ubertopo/topo/curve_geom.h"
#include "ubertopo/topo/validate.h"

namespace ubertopo::topo {

using ident::ElemKind;
using VC = ValidateCode;

template <geom::CoordType C>
void validate_curves(const Complex<C> &k, ValidationReport &rep, const ValidateFilter *flt)
{
  const auto &curve = k.edges.template get<".e.curve">();
  const auto &range = k.edges.template get<".e.range">();
  const auto &gflags = k.edges.template get<".e.geom_flags">();
  const auto &pcurve = k.coedges.template get<".c.pcurve">();
  const auto &surface = k.faces.template get<".f.surface">();
  const double curve_tol = k.global.template get<".g.curve_tol">()[0];

  auto edge = [&](uint32_t e) {
    if (!k.edges.live(e)) {
      return;
    }
    const EdgeRef er(e);
    if (gflags[e] != 0) {
      rep.add(VC::GeometryUnsupported, ElemKind::Edge, e);
    }
    k.for_each_radial(er, [&](CoedgeRef c) {
      if (!pcurve[c.i].none()) {
        rep.add(VC::GeometryUnsupported, ElemKind::Coedge, c.i);
      }
    });
    const attr::PoolRef r = curve[e];
    const double t0 = range[e][0], t1 = range[e][1];
    if (r.none()) {
      if (std::bit_cast<uint64_t>(t0) != 0 || std::bit_cast<uint64_t>(t1) != 0) {
        rep.add(VC::CurveRange, ElemKind::Edge, e);
      }
      return;
    }
    bool range_bad = false;
    const auto c = edge_carrier(k.geom, r, t0, t1, &range_bad);
    if (!c) {
      rep.add(range_bad ? VC::CurveRange : VC::CurveKind, ElemKind::Edge, e);
      return;
    }
    const auto vs = k.edge_verts(er);
    const bool up = t0 < t1;
    if (!on_carrier<C>(*c, t0, k.pos(vs[0]), curve_tol, up ? curves::Side::Above : curves::Side::Below) ||
        !on_carrier<C>(*c, t1, k.pos(vs[1]), curve_tol, up ? curves::Side::Below : curves::Side::Above))
    {
      rep.add(VC::CurveEndpoint, ElemKind::Edge, e);
    }
  };
  auto face = [&](uint32_t f) {
    if (k.faces.live(f) && !surface[f].none()) {
      rep.add(VC::GeometryUnsupported, ElemKind::Face, f);
    }
  };
  if (flt) {
    for (uint32_t e : flt->edges) {
      if (e < k.edges.capacity()) {
        edge(e);
      }
    }
    for (uint32_t f : flt->faces) {
      if (f < k.faces.capacity()) {
        face(f);
      }
    }
    return;
  }
  for (uint32_t e = 0; e < k.edges.capacity(); e++) {
    edge(e);
  }
  for (uint32_t f = 0; f < k.faces.capacity(); f++) {
    face(f);
  }
}

template void validate_curves<double>(const Complex<double> &, ValidationReport &, const ValidateFilter *);
template void validate_curves<float>(const Complex<float> &, ValidationReport &, const ValidateFilter *);
template void validate_curves<geom::Lattice>(const Complex<geom::Lattice> &, ValidationReport &, const ValidateFilter *);

} // namespace ubertopo::topo
