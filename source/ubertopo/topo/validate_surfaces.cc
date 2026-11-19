/* Embedding layer, surface part (plan P4b §4.2): surfaced faces and their
 * coedges' pcurves.
 *
 * Per face, in order (a failure skips the later checks of that face):
 *  1. SurfaceKind, PcurveKind, PcurveRange: refs, kinds and domains;
 *  2. SurfaceLink: the one piece in v (road3 link) holding every pcurve;
 *  3. PcurveVertex: S(pcurve) at each coedge end against its vertex;
 *  4. PcurveDeviation: the edge against S(pcurve) along its whole range,
 *     structurally for road borders, else by a second-order bound;
 *  5. SurfaceOrientation: Reversed against the surface normal.
 *
 * Tolerances follow P4a's endpoint rule (curve_geom.h): certified evaluation
 * errors, sqrt(3) rounding_C of stored points, curve_tol, and a relative
 * allowance for the distance's own rounding. */

#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>
#include <vector>

#include "ubertopo/curves/alignment.h"
#include "ubertopo/curves/offset.h"
#include "ubertopo/curves/pcurve.h"
#include "ubertopo/curves/road.h"
#include "ubertopo/topo/curve_geom.h"
#include "ubertopo/topo/radial.h"
#include "ubertopo/topo/validate.h"

namespace ubertopo::topo {

namespace util = litestl::util;
using ident::ElemKind;
using VC = ValidateCode;
using CSide = curves::Side;
using curves::Vec;

namespace {

constexpr double kSqrt3Up = 1.7320508075688774; /* > sqrt(3) */
constexpr double kSlack = 0x1p-49;              /* the distance's own rounding (as on_carrier) */
constexpr int kMaxSubranges = 1 << 12;          /* per coedge (§4.2) */
constexpr int kMaxLinkPieces = 1 << 8;          /* per coedge and link */

double norm3(const Vec<3> &v)
{
  return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

bool within(double d, double tol)
{
  return std::isfinite(d) && d * (1 - kSlack) <= tol * (1 + kSlack);
}

CSide flip(CSide s)
{
  return s == CSide::Above ? CSide::Below : CSide::Above;
}

/* one coedge of a surfaced face, its kind and range checked */
struct PRec {
  CoedgeRef c;
  EdgeRef e;
  bool curved = false;
  double t0 = 0, t1 = 1; /* the edge's parameter at vs[0] and vs[1] */
  util::UniquePtr<curves::Curve3> carrier;
  util::UniquePtr<curves::Curve2> gamma;
  const curves::Graph2 *g = nullptr;
  const curves::Segment2 *sg = nullptr;
  double pe = 0; /* the pcurve's eval_error */

  double lo() const
  {
    return std::min(t0, t1);
  }
  double hi() const
  {
    return std::max(t0, t1);
  }
  /* the side of u that the side of lambda maps to */
  CSide u_side(CSide s) const
  {
    return sg && sg->p[2] < sg->p[0] ? flip(s) : s;
  }
};

/* bounds on the pcurve's components over [a, b]: |u'|, |v'|, |u''|, |v''|,
 * and a box holding the exact pcurve (and every point within pe of it) */
struct Comp {
  double U1 = 0, V1 = 0, U2 = 0, V2 = 0;
  curves::ParamBox box;
};

Comp components(const PRec &r, double a, double b)
{
  Comp c;
  if (r.g) {
    const curves::Fn1D &f = r.g->f;
    const curves::Range rv = f.range(a, b, 0);
    c.U1 = 1;
    c.V1 = f.range(a, b, 1).abs_max();
    c.V2 = f.range(a, b, 2).abs_max();
    c.box = {{a - r.pe, b + r.pe}, {rv.lo - r.pe, rv.hi + r.pe}};
    return c;
  }
  const double *p = r.sg->p;
  const double du = p[2] - p[0], dv = p[3] - p[1];
  c.U1 = std::fabs(du);
  c.V1 = std::fabs(dv);
  const double ua = p[0] + a * du, ub = p[0] + b * du, va = p[1] + a * dv, vb = p[1] + b * dv;
  const double pu = curves::up(curves::gamma_n(3) * (std::fabs(p[0]) + std::fabs(p[2]))) + r.pe;
  const double pv = curves::up(curves::gamma_n(3) * (std::fabs(p[1]) + std::fabs(p[3]))) + r.pe;
  c.box = {{std::min(ua, ub) - pu, std::max(ua, ub) + pu}, {std::min(va, vb) - pv, std::max(va, vb) + pv}};
  return c;
}

/* S's first-order bound over a box, on one piece: |S(x) - S(y)| <= grad |x - y|_inf */
double grad_bound(const curves::SurfaceEval &ev, const curves::ParamBox &box, int piece)
{
  return curves::up(ev.deriv_bound_piece(box, 1, 0, piece) + ev.deriv_bound_piece(box, 0, 1, piece));
}

/* the pcurve point at lambda (one-sided) */
void pcurve_at(const PRec &r, double lam, CSide side, double &u, double &v)
{
  Vec<2> q;
  r.gamma->eval(lam, 0, &q, side);
  u = q[0];
  v = q[1];
}

template <geom::CoordType C> struct FaceCheck {
  const Complex<C> &k;
  ValidationReport &rep;
  const attr::PoolStore &st;
  double curve_tol;

  /* the edge's point at lambda: its carrier, or the chord from vs[0] to vs[1] */
  Vec<3> edge_point(const PRec &r, double lam, CSide side) const
  {
    if (r.curved) {
      Vec<3> x;
      r.carrier->eval(lam, 0, &x, side);
      return x;
    }
    const auto vs = k.edge_verts(r.e);
    const auto p0 = geom::to_double3<C>(k.pos(vs[0])), p1 = geom::to_double3<C>(k.pos(vs[1]));
    return Vec<3>(p0[0] + lam * (p1[0] - p0[0]), p0[1] + lam * (p1[1] - p0[1]), p0[2] + lam * (p1[2] - p0[2]));
  }
  /* the error of edge_point */
  double edge_error(const PRec &r) const
  {
    if (r.curved) {
      return r.carrier->eval_error();
    }
    const auto vs = k.edge_verts(r.e);
    const auto p0 = geom::to_double3<C>(k.pos(vs[0])), p1 = geom::to_double3<C>(k.pos(vs[1]));
    double m = 0;
    for (int i = 0; i < 3; i++) {
      m = std::max(m, std::fabs(p0[i]) + std::fabs(p1[i]));
    }
    return curves::up(kSqrt3Up * curves::gamma_n(4) * m);
  }

  /* 1. kinds and ranges; false if the face can't be checked further */
  bool gather(FaceRef f, const curves::SurfaceEval &ev, std::vector<PRec> &out)
  {
    const auto &curve = k.edges.template get<".e.curve">();
    const auto &range = k.edges.template get<".e.range">();
    const auto &pcurve = k.coedges.template get<".c.pcurve">();
    const curves::Interval du = ev.domain_u();
    bool ok = true;
    k.for_each_loop(f, [&](LoopRef l) {
      k.for_each_coedge(l, [&](CoedgeRef c) {
        PRec r;
        r.c = c;
        r.e = k.edge_of(c);
        const attr::PoolRef pc = pcurve[c.i];
        r.curved = !curve[r.e.i].none();
        const curves::KindOps *want = r.curved ? &curves::graph2_kind() : &curves::segment2_kind();
        if (pc.none() || !st.live(pc) || curves::kind_of(pc) != want) {
          rep.add(VC::PcurveKind, ElemKind::Coedge, c.i);
          ok = false;
          return;
        }
        r.gamma = curves::curve2(st, pc);
        if (r.curved) {
          r.g = st.try_get<curves::Graph2Pool>(pc);
          r.t0 = range[r.e.i][0];
          r.t1 = range[r.e.i][1];
          r.carrier = edge_carrier(st, curve[r.e.i], r.t0, r.t1);
        }
        else {
          r.sg = st.try_get<curves::Segment2Pool>(pc);
        }
        if (!r.gamma || !(r.g || r.sg) || (r.curved && !r.carrier)) {
          /* a bad carrier is validate_curves' report; a bad pcurve entry ours */
          if (!r.gamma || !(r.g || r.sg)) {
            rep.add(VC::PcurveKind, ElemKind::Coedge, c.i);
          }
          ok = false;
          return;
        }
        const curves::Interval dg = r.gamma->domain();
        bool in_u;
        if (r.g) {
          in_u = r.lo() >= du.lo && r.hi() <= du.hi;
        }
        else {
          in_u = r.sg->p[0] >= du.lo && r.sg->p[0] <= du.hi && r.sg->p[2] >= du.lo && r.sg->p[2] <= du.hi;
        }
        if (!(r.lo() >= dg.lo && r.hi() <= dg.hi) || !in_u) {
          rep.add(VC::PcurveRange, ElemKind::Coedge, c.i);
          ok = false;
          return;
        }
        r.pe = r.gamma->eval_error();
        out.push_back(std::move(r));
      });
    });
    return ok;
  }

  /* 2. whether the pcurve lies in road3 link j over its range, within eps:
   * enclosures of the link's borders over subranges, bisected */
  bool in_link(const curves::Road3 &d, const PRec &r, int j, double eps) const
  {
    const curves::Fn1D &lo_b = d.borders[j], &hi_b = d.borders[j + 1];
    const double ulo = lo_b.lo(), uhi = lo_b.hi();
    std::vector<std::pair<double, double>> todo = {{r.lo(), r.hi()}};
    int n = 1;
    while (!todo.empty()) {
      const auto [a, b] = todo.back();
      todo.pop_back();
      const Comp cp = components(r, a, b);
      const double u0 = std::clamp(cp.box.u.lo, ulo, uhi), u1 = std::clamp(cp.box.u.hi, ulo, uhi);
      const curves::Range bl = lo_b.range(u0, u1, 0), bh = hi_b.range(u0, u1, 0);
      const double el = lo_b.eval_error(0), eh = hi_b.eval_error(0);
      if (cp.box.v.lo >= bl.hi + el - eps && cp.box.v.hi <= bh.lo - eh + eps) {
        continue;
      }
      if (cp.box.v.hi < bl.lo - el - eps || cp.box.v.lo > bh.hi + eh + eps) {
        return false;
      }
      if (n >= kMaxLinkPieces || !(a < b)) {
        return false;
      }
      const double m = a + 0.5 * (b - a);
      todo.push_back({a, m});
      todo.push_back({m, b});
      n++;
    }
    return true;
  }

  /* the face's link: one bit per candidate, intersected over the pcurves */
  int face_link(const curves::Road3 &d, const std::vector<PRec> &recs, double eps) const
  {
    const int m = d.links();
    uint64_t mask = m >= 64 ? ~uint64_t(0) : (uint64_t(1) << m) - 1;
    for (const PRec &r : recs) {
      uint64_t mr = 0;
      int border = -1;
      for (int j = 0; r.g && j <= m; j++) {
        if (d.borders[j].equal(r.g->f)) {
          border = j;
          break;
        }
      }
      if (border >= 0) {
        /* on border j: links j - 1 and j structurally */
        if (border > 0) {
          mr |= uint64_t(1) << (border - 1);
        }
        if (border < m) {
          mr |= uint64_t(1) << border;
        }
      }
      else {
        for (int j = 0; j < m; j++) {
          if ((mask >> j & 1) && in_link(d, r, j, eps)) {
            mr |= uint64_t(1) << j;
          }
        }
      }
      mask &= mr;
      if (!mask) {
        return -1;
      }
    }
    return std::popcount(mask) == 1 ? std::countr_zero(mask) : -1;
  }

  /* 3. S(pcurve) at the coedge's ends against its vertices */
  bool vertices_ok(const PRec &r, const curves::SurfaceEval &ev, int link) const
  {
    const auto vs = k.edge_verts(r.e);
    for (int i = 0; i < 2; i++) {
      const double lam = i ? r.t1 : r.t0;
      const CSide side = lam == r.lo() ? CSide::Above : CSide::Below; /* toward the edge */
      double u, v;
      pcurve_at(r, lam, side, u, v);
      Vec<3> s;
      ev.eval_piece(u, v, 0, &s, link, r.u_side(side));
      const auto p = geom::to_double3<C>(k.pos(vs[i]));
      const double dx = s[0] - p[0], dy = s[1] - p[1], dz = s[2] - p[2];
      const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
      const curves::ParamBox box{{u - r.pe, u + r.pe}, {v - r.pe, v + r.pe}};
      const double tol = curves::up(ev.eval_error() + grad_bound(ev, box, link) * r.pe +
                                    kSqrt3Up * rounding_of<C>(k.pos(vs[i])) + curve_tol);
      if (!within(d, tol)) {
        return false;
      }
    }
    return true;
  }

  /* 4a. the structural pass (§4.2): a road border's own carrier and pcurve */
  bool structural(const curves::Road3 &d, const PRec &r, int link) const
  {
    using namespace curves;
    if (!r.curved || !r.g) {
      return false;
    }
    const attr::PoolRef cr = k.edges.template get<".e.curve">()[r.e.i];
    const Alignment3 *a = kind_of(cr) == &alignment3_kind() ? st.try_get<Alignment3Pool>(cr) : nullptr;
    if (!a) {
      return false;
    }
    bool horiz = false;
    if (a->horizontal == d.reference) {
      horiz = r.g->f.is_zero();
    }
    else if (kind_of(a->horizontal) == &offset2_kind()) {
      const Offset2 *o = st.try_get<Offset2Pool>(a->horizontal);
      horiz = o && o->base == d.reference && o->w.equal(r.g->f);
    }
    if (!horiz) {
      return false;
    }
    for (int j = link; j <= link + 1; j++) {
      if (d.borders[j].equal(r.g->f) && d.heights[j].equal(a->z)) {
        return true;
      }
    }
    return false;
  }

  /* 4b. the general pass: D = C - S(gamma), max |D| on [a, b] bounded by
   * its ends plus (b - a)^2 / 8 M2, bisected up to kMaxSubranges */
  bool deviation_ok(const PRec &r, const curves::SurfaceEval &ev, int link) const
  {
    const double lo = r.lo(), hi = r.hi();
    /* cuts: the carrier's, the pcurve's and the surface's special parameters */
    std::vector<double> cuts = {lo, hi};
    util::Vector<curves::SpecialParam> sp;
    if (r.curved) {
      r.carrier->special_params(lo, hi, sp);
    }
    r.gamma->special_params(lo, hi, sp);
    bool mapped = false; /* a surface break mapped through a slanted segment2 */
    {
      util::Vector<curves::SpecialParam> su;
      if (r.g) {
        ev.special_u(lo, hi, su);
        for (const curves::SpecialParam &x : su) {
          sp.append(x);
        }
      }
      else if (r.sg->p[2] != r.sg->p[0]) {
        const double u0 = r.sg->p[0], u1 = r.sg->p[2];
        ev.special_u(std::min(u0, u1), std::max(u0, u1), su);
        for (const curves::SpecialParam &x : su) {
          sp.append({(x.t - u0) / (u1 - u0), x.continuity});
          mapped = true;
        }
      }
    }
    for (const curves::SpecialParam &x : sp) {
      if (x.t > lo && x.t < hi) {
        cuts.push_back(x.t);
      }
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

    const double cerr = edge_error(r), se0 = ev.eval_error();
    double tol = curve_tol;
    if (!r.curved) {
      const auto vs = k.edge_verts(r.e);
      tol += kSqrt3Up * std::max(rounding_of<C>(k.pos(vs[0])), rounding_of<C>(k.pos(vs[1])));
    }
    auto dev = [&](double lam, CSide side) {
      double u, v;
      pcurve_at(r, lam, side, u, v);
      Vec<3> s;
      ev.eval_piece(u, v, 0, &s, link, r.u_side(side));
      const Vec<3> x = edge_point(r, lam, side);
      const double dx = x[0] - s[0], dy = x[1] - s[1], dz = x[2] - s[2];
      return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    std::vector<std::pair<double, double>> todo;
    for (size_t i = 0; i + 1 < cuts.size(); i++) {
      todo.push_back({cuts[i], cuts[i + 1]});
    }
    int n = int(todo.size());
    while (!todo.empty()) {
      const auto [a, b] = todo.back();
      todo.pop_back();
      const double da = dev(a, CSide::Above), db = dev(b, CSide::Below);
      if (!std::isfinite(da) || !std::isfinite(db)) {
        return false;
      }
      const Comp cp = components(r, a, b);
      const double Ms = ev.deriv_bound_piece(cp.box, 1, 0, link), Mt = ev.deriv_bound_piece(cp.box, 0, 1, link);
      const double Mss = ev.deriv_bound_piece(cp.box, 2, 0, link), Mst = ev.deriv_bound_piece(cp.box, 1, 1, link);
      const double Mtt = ev.deriv_bound_piece(cp.box, 0, 2, link);
      /* (S o gamma)'' = S_ss u'^2 + 2 S_st u'v' + S_tt v'^2 + S_s u'' + S_t v'' */
      double sg2 = 0;
      auto acc = [&](double m, double w) {
        if (w != 0) {
          sg2 += m * w; /* an infinite m with a zero weight adds nothing */
        }
      };
      acc(Mss, cp.U1 * cp.U1);
      acc(2 * Mst, cp.U1 * cp.V1);
      acc(Mtt, cp.V1 * cp.V1);
      acc(Ms, cp.U2);
      acc(Mt, cp.V2);
      const double c2 = r.curved ? r.carrier->deriv_bound(a, b, 2) : 0.0;
      const double M2 = curves::up(curves::up(c2 + sg2) * (1 + curves::gamma_n(8)));
      double err = cerr + se0 + curves::up(Ms + Mt) * r.pe;
      if (mapped) {
        /* a cut at a mapped surface break misses it by a few ulps of lambda */
        err += curves::up(8 * curves::kUnit * (Ms * cp.U1 + Mt * cp.V1));
      }
      err = curves::up(err * (1 + curves::gamma_n(4)));
      const double ends = std::max(da, db);
      if (!within(ends - err, tol)) {
        return false; /* definite: an end is off by more than the tolerance */
      }
      const double h = b - a;
      const double bound = curves::up(ends + err + curves::up(h * h * 0.125) * M2);
      if (within(bound, tol)) {
        continue;
      }
      if (n >= kMaxSubranges || !(a < b) || !(a + 0.5 * h > a && a + 0.5 * h < b)) {
        return false; /* inconclusive within the cap */
      }
      const double mid = a + 0.5 * h;
      todo.push_back({a, mid});
      todo.push_back({mid, b});
      n++;
    }
    return true;
  }

  /* 5. Reversed against the sign of (S_u x S_v) along the face's axis */
  bool orientation_ok(FaceRef f, const PRec &r, const curves::SurfaceEval &ev, int link, Skeleton<C> &sk) const
  {
    FacePoints<C> fp;
    if (!fp.gather(k, f, sk)) {
      return true; /* the combinatorial layer's report */
    }
    const auto v = fp.view();
    int axis = k.face_proj_axis(f), sigma = 0;
    if (axis >= 0 && axis <= 2) {
      sigma = geom::face_front_sign<C>(v, axis);
    }
    else {
      const FaceFrame fm = face_frame<C>(v);
      axis = fm.axis;
      sigma = fm.sigma;
    }
    if (axis < 0 || axis > 2 || sigma == 0) {
      return true; /* a degenerate face: the embedding layer's report */
    }
    const double lam = r.lo() + 0.5 * (r.hi() - r.lo());
    double u, vv;
    pcurve_at(r, lam, CSide::Above, u, vv);
    Vec<3> d[3];
    ev.eval_piece(u, vv, 1, d, link, r.u_side(CSide::Above));
    const Vec<3> &a = d[1], &b = d[2];
    const int i1 = (axis + 1) % 3, i2 = (axis + 2) % 3;
    const double n = a[i1] * b[i2] - a[i2] * b[i1];
    const double e1 = ev.deriv_eval_error(1);
    const double na = norm3(a), nb = norm3(b);
    const double err = curves::up((2 * (na + e1) * e1 + 2 * (nb + e1) * e1 + curves::gamma_n(3) * 2 * na * nb) *
                                  (1 + curves::gamma_n(4)));
    const bool reversed = (k.face_flags(f) & face_flags::Reversed) != 0;
    const double want = reversed ? -sigma : sigma;
    return std::isfinite(n) && n * want > err;
  }
};

} // namespace

template <geom::CoordType C>
void validate_surfaces(const Complex<C> &k, ValidationReport &rep, const ValidateFilter *flt)
{
  const auto &surface = k.faces.template get<".f.surface">();
  const auto &pcurve = k.coedges.template get<".c.pcurve">();
  FaceCheck<C> fc{k, rep, k.geom, k.global.template get<".g.curve_tol">()[0]};
  Skeleton<C> sk(k);

  auto face = [&](uint32_t fi) {
    if (fi >= k.faces.capacity() || !k.faces.live(fi)) {
      return;
    }
    const FaceRef f(fi);
    const attr::PoolRef sr = surface[fi];
    if (sr.none()) {
      k.for_each_loop(f, [&](LoopRef l) {
        k.for_each_coedge(l, [&](CoedgeRef c) {
          if (!pcurve[c.i].none()) {
            rep.add(VC::PcurveKind, ElemKind::Coedge, c.i);
          }
        });
      });
      return;
    }
    const auto ev = curves::surface(k.geom, sr);
    const curves::Road3 *d = curves::kind_of(sr) == &curves::road3_kind() ? k.geom.template try_get<curves::Road3Pool>(sr)
                                                                          : nullptr;
    if (!ev || !d) {
      rep.add(VC::SurfaceKind, ElemKind::Face, fi);
      return;
    }
    std::vector<PRec> recs;
    if (!fc.gather(f, *ev, recs) || recs.empty()) {
      return;
    }
    double pe = 0;
    for (const PRec &r : recs) {
      pe = std::max(pe, r.pe);
    }
    /* |S_t| >= |N| = 1 on road3, so a distance tolerance bounds the v offset */
    const double eps = curves::up(fc.curve_tol + ev->eval_error() + pe);
    const int link = fc.face_link(*d, recs, eps);
    if (link < 0) {
      rep.add(VC::SurfaceLink, ElemKind::Face, fi);
      return;
    }
    bool ok = true;
    for (const PRec &r : recs) {
      if (!fc.vertices_ok(r, *ev, link)) {
        rep.add(VC::PcurveVertex, ElemKind::Coedge, r.c.i);
        ok = false;
      }
    }
    if (!ok) {
      return;
    }
    for (const PRec &r : recs) {
      if (!fc.structural(*d, r, link) && !fc.deviation_ok(r, *ev, link)) {
        rep.add(VC::PcurveDeviation, ElemKind::Coedge, r.c.i);
        ok = false;
      }
    }
    if (ok && !fc.orientation_ok(f, recs[0], *ev, link, sk)) {
      rep.add(VC::SurfaceOrientation, ElemKind::Face, fi);
    }
  };

  if (!flt) {
    for (uint32_t f = 0; f < k.faces.capacity(); f++) {
      face(f);
    }
    return;
  }
  /* the listed faces and the faces around the listed edges */
  util::Vector<uint32_t> faces;
  for (uint32_t f : flt->faces) {
    faces.append(f);
  }
  for (uint32_t e : flt->edges) {
    if (e < k.edges.capacity() && k.edges.live(e)) {
      k.for_each_radial(EdgeRef(e), [&](CoedgeRef c) { faces.append(k.face_of(c).i); });
    }
  }
  std::sort(faces.begin(), faces.end());
  for (int i = 0; i < faces.size(); i++) {
    if (i == 0 || faces[i] != faces[i - 1]) {
      face(faces[i]);
    }
  }
}

template void validate_surfaces<double>(const Complex<double> &, ValidationReport &, const ValidateFilter *);
template void validate_surfaces<float>(const Complex<float> &, ValidationReport &, const ValidateFilter *);
template void validate_surfaces<geom::Lattice>(const Complex<geom::Lattice> &,
                                               ValidationReport &,
                                               const ValidateFilter *);

} // namespace ubertopo::topo
