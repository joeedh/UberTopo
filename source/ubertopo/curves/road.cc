/* road3 (road.h). */

#include "ubertopo/curves/road.h"

#include <algorithm>
#include <cmath>

#include "ubertopo/curves/cesaro.h"
#include "ubertopo/curves/offset.h"

namespace ubertopo::curves {

namespace {

/* the structural rules shared by make and valid: sizes, one common domain */
const char *check_shape(const util::Vector<Fn1D, 2> &borders, const util::Vector<Fn1D, 2> &heights)
{
  if (borders.size() < 2 || borders.size() > Road3::kMaxBorders || heights.size() != borders.size()) {
    return "road3: need 2..kMaxBorders borders and as many heights";
  }
  const double lo = borders[0].lo(), hi = borders[0].hi();
  for (int i = 0; i < borders.size(); i++) {
    for (const Fn1D *f : {&borders[i], &heights[i]}) {
      if (f->empty() || f->lo() != lo || f->hi() != hi) {
        return "road3: borders and heights must share one domain";
      }
    }
  }
  return nullptr;
}

/* the reference of a well-formed entry (shape and domain), else null */
const Cesaro2 *resolve(const attr::PoolStore &s, const Road3 &d)
{
  if (kind_of(d.reference) != &cesaro2_kind()) {
    return nullptr;
  }
  const Cesaro2 *c = s.try_get<Cesaro2Pool>(d.reference);
  if (!c || check_shape(d.borders, d.heights) || !(d.borders[0].lo() >= 0) ||
      !(d.borders[0].hi() <= c->length()))
  {
    return nullptr;
  }
  return c;
}

/* certified enclosure of (a - b)^(k) over [s0, s1] (a, b on one domain),
 * from the shifted pieces with their rounding bounds */
Range diff_range(const Fn1D &a, const Fn1D &b, double s0, double s1, int k)
{
  util::Vector<double, 2> br;
  util::Vector<Poly, 1> pa, pb;
  Fn1D::shifted_pieces(a, b, br, pa, pb);
  Range out{INFINITY, -INFINITY};
  for (int i = 0; i < pa.size(); i++) {
    const double x0 = br[i], x1 = br[i + 1];
    if (x1 < s0 || x0 > s1) {
      continue;
    }
    const double lo = std::max(s0, x0) - x0, hi = std::min(s1, x1) - x0;
    const double pad = 4 * kUnit * (std::fabs(s0) + std::fabs(s1) + std::fabs(x0));
    Poly q = pa[i] - pb[i];
    for (int j = 0; j < k; j++) {
      q = q.deriv();
    }
    const Range r = q.range(std::max(0.0, lo - pad), std::max(lo, hi) + pad, 2);
    out.lo = std::min(out.lo, r.lo);
    out.hi = std::max(out.hi, r.hi);
  }
  return out;
}

/* the geometric rules of make, also checked by valid (§3.2) */
const char *check_geometry(const Cesaro2 &c, const Road3 &d)
{
  const double lo = d.borders[0].lo(), hi = d.borders[0].hi();
  for (int i = 0; i < d.borders.size(); i++) {
    for (const Fn1D *f : {&d.borders[i], &d.heights[i]}) {
      for (int b = 1; b < f->pieces.size(); b++) {
        if (f->continuity_at(b) < 0) {
          return "road3: a border or height jumps at a break";
        }
      }
    }
  }
  for (int k = 0; k + 1 < d.borders.size(); k++) {
    if (!(diff_range(d.borders[k + 1], d.borders[k], lo, hi, 0).lo > 0)) {
      return "road3: the borders are not provably ordered";
    }
  }
  for (const Fn1D *b : {&d.borders[0], &d.borders[d.borders.size() - 1]}) {
    Offset2 o;
    o.base = d.reference;
    o.w = *b;
    const Offset2View v(&c, &o);
    if (!v.offset_valid(lo, hi)) {
      return "road3: an outer border folds (1 - t kappa <= 0)";
    }
  }
  return nullptr;
}

/* A magnitude bound m on an exact quantity and a bound e on the error of
 * its computed value; each operation mirrors one step of Road3View::eval
 * and adds its own rounding. Bounds are rounded up. */
struct EB {
  double m = 0, e = 0;
};
EB eb_add(EB a, EB b)
{
  const double m = a.m + b.m;
  return {up(m), up(a.e + b.e + kUnit * (m + a.e + b.e))};
}
EB eb_mul(EB a, EB b)
{
  const double m = a.m * b.m;
  const double e = a.m * b.e + b.m * a.e + a.e * b.e;
  return {up(m), up(e + kUnit * (a.m + a.e) * (b.m + b.e))};
}
/* a / d, where the exact |d| >= dl > d.e */
EB eb_div(EB a, EB d, double dl)
{
  if (!(dl > d.e)) {
    return {INFINITY, INFINITY};
  }
  const double m = a.m / dl;
  const double e = (a.e + m * d.e) / (dl - d.e);
  return {up(m), up(e + kUnit * (m + e))};
}
/* a value with error bound e and magnitude m, as given */
EB eb(double m, double e)
{
  return {m, e};
}

class Road3View final : public SurfaceEval {
public:
  Road3View(const Cesaro2 *c, const Road3 *d) : c_(*c), d_(*d), ref_(c->evaluator())
  {
    lo_ = d_.borders[0].lo();
    hi_ = d_.borders[0].hi();
    bounds();
  }

  Interval domain_u() const override
  {
    return {lo_, hi_};
  }
  Interval domain_v(double u, Side su) const override
  {
    u = std::clamp(u, lo_, hi_);
    return {d_.borders[0].eval(u, 0, su), d_.borders[d_.borders.size() - 1].eval(u, 0, su)};
  }

  /* the link holding t at s: the largest k <= m - 1 with b_k(s) <= t
   * (< for Below), 0 below b_1 */
  int link_at(double s, double t, Side su, Side sv) const
  {
    int lo = 0, hi = d_.links(); /* answer in [lo, hi) */
    while (hi - lo > 1) {
      const int mid = (lo + hi) / 2;
      const double b = d_.borders[mid].eval(s, 0, su);
      if (sv == Side::Above ? t >= b : t > b) {
        lo = mid;
      }
      else {
        hi = mid;
      }
    }
    return lo;
  }

  /* eval's link coordinate and height at (s, t), for the probe */
  void w_and_z(double s, double t, Side su, Side sv, int *link, double *w, double *z) const
  {
    s = std::clamp(s, lo_, hi_);
    const int k = link_at(s, t, su, sv);
    const double bk = d_.borders[k].eval(s, 0, su), bk1 = d_.borders[k + 1].eval(s, 0, su);
    const double zk = d_.heights[k].eval(s, 0, su), zk1 = d_.heights[k + 1].eval(s, 0, su);
    *link = k;
    *w = (t - bk) / (bk1 - bk);
    *z = (1 - *w) * zk + *w * zk1;
  }

  int piece_count() const override
  {
    return d_.links();
  }
  int piece_at(double u, double v, Side su, Side sv) const override
  {
    return link_at(std::clamp(u, lo_, hi_), v, su, sv);
  }

  void eval(double s, double t, int n, Vec<3> *out, Side su, Side sv) const override
  {
    eval_piece(s, t, n, out, link_at(std::clamp(s, lo_, hi_), t, su, sv), su);
  }

  void eval_piece(double s, double t, int n, Vec<3> *out, int k, Side su) const override
  {
    s = std::clamp(s, lo_, hi_);
    n = std::clamp(n, 0, 2);
    k = std::clamp(k, 0, d_.links() - 1);
    Vec<2> r[4];
    ref_->eval(s, n + 1, r, su);
    /* xy: Offset2View::eval's expression and order (rule 1 of §3.2) */
    const double x = r[0][0] - t * r[1][1], y = r[0][1] + t * r[1][0];
    const Fn1D &Bk = d_.borders[k], &Bk1 = d_.borders[k + 1], &Zk = d_.heights[k], &Zk1 = d_.heights[k + 1];
    const double bk = Bk.eval(s, 0, su), bk1 = Bk1.eval(s, 0, su);
    const double zk = Zk.eval(s, 0, su), zk1 = Zk1.eval(s, 0, su);
    const double dd = bk1 - bk;
    const double w = (t - bk) / dd;
    /* the convex form: exact at w = 0 and w = 1 (rule 2) */
    out[0] = Vec<3>(x, y, (1 - w) * zk + w * zk1);
    if (n < 1) {
      return;
    }
    const double bk_1 = Bk.eval(s, 1, su), dd1 = Bk1.eval(s, 1, su) - bk_1;
    const double zk_1 = Zk.eval(s, 1, su), D = zk1 - zk, D1 = Zk1.eval(s, 1, su) - zk_1;
    const double ws = -(bk_1 + w * dd1) / dd, wt = 1 / dd;
    const double Zs = zk_1 + ws * D + w * D1, Zt = D / dd;
    /* P_s = r' + t N', N' = (-r''_y, r''_x); P_t = N */
    out[1] = Vec<3>(r[1][0] - t * r[2][1], r[1][1] + t * r[2][0], Zs);
    out[2] = Vec<3>(-r[1][1], r[1][0], Zt);
    if (n < 2) {
      return;
    }
    const double bk_2 = Bk.eval(s, 2, su), dd2 = Bk1.eval(s, 2, su) - bk_2;
    const double zk_2 = Zk.eval(s, 2, su), D2 = Zk1.eval(s, 2, su) - zk_2;
    const double wss = -(bk_2 + 2 * ws * dd1 + w * dd2) / dd, wst = -dd1 / (dd * dd);
    const double Zss = zk_2 + wss * D + 2 * ws * D1 + w * D2, Zst = wst * D + wt * D1;
    out[3] = Vec<3>(r[2][0] - t * r[3][1], r[2][1] + t * r[3][0], Zss);
    out[4] = Vec<3>(-r[2][1], r[2][0], Zst);
    out[5] = Vec<3>(0.0, 0.0, 0.0);
  }

  double eval_error() const override
  {
    return terms_.e0;
  }
  double deriv_eval_error(int k) const override
  {
    return k == 0 ? terms_.e0 : k == 1 ? terms_.e1 : k == 2 ? terms_.e2 : INFINITY;
  }

  double deriv_bound(const ParamBox &box, int i, int j, Side sv) const override
  {
    /* the link named by the box's middle and sv, extended over the box */
    const double u0 = std::max(lo_, std::min(box.u.lo, box.u.hi)), u1 = std::min(hi_, std::max(box.u.lo, box.u.hi));
    const double v0 = std::min(box.v.lo, box.v.hi), v1 = std::max(box.v.lo, box.v.hi);
    if (!(u0 <= u1)) {
      return INFINITY;
    }
    return deriv_bound_piece(box, i, j, link_at(u0 + 0.5 * (u1 - u0), v0 + 0.5 * (v1 - v0), Side::Above, sv));
  }

  double deriv_bound_piece(const ParamBox &box, int i, int j, int k) const override
  {
    const int ord = i + j;
    if (k < 0 || k >= d_.links()) {
      return INFINITY;
    }
    if (i < 0 || j < 0 || ord > 2) {
      return INFINITY;
    }
    if (j == 2) {
      return 0; /* S is linear in t on a link */
    }
    const double u0 = std::max(lo_, std::min(box.u.lo, box.u.hi)), u1 = std::min(hi_, std::max(box.u.lo, box.u.hi));
    const double v0 = std::min(box.v.lo, box.v.hi), v1 = std::max(box.v.lo, box.v.hi);
    if (!(u0 <= u1)) {
      return INFINITY;
    }
    const Fn1D &Bk = d_.borders[k], &Bk1 = d_.borders[k + 1], &Zk = d_.heights[k], &Zk1 = d_.heights[k + 1];
    auto mag = [&](const Fn1D &f, int q) { return f.range(u0, u1, q).abs_max(); };
    auto dmag = [&](const Fn1D &a, const Fn1D &b, int q) { return diff_range(a, b, u0, u1, q).abs_max(); };
    const double dmin = diff_range(Bk1, Bk, u0, u1, 0).lo;
    if (!(dmin > 0)) {
      return INFINITY;
    }
    const double tm = std::max(std::fabs(v0), std::fabs(v1));
    /* |w| over the box: |t - b_k| / dmin */
    const Range rb = Bk.range(u0, u1, 0);
    const double W = std::max(std::fabs(v1 - rb.lo), std::fabs(v0 - rb.hi)) / dmin;
    const double d1 = dmag(Bk1, Bk, 1), d2 = dmag(Bk1, Bk, 2);
    const double D0 = dmag(Zk1, Zk, 0), D1 = dmag(Zk1, Zk, 1), D2 = dmag(Zk1, Zk, 2);
    const double Ws = (mag(Bk, 1) + W * d1) / dmin, Wt = 1 / dmin;
    double ph = 0, z = 0; /* bounds on the horizontal part's and Z's partial */
    if (ord == 0) {
      ph = std::fabs(c_.x0) + std::fabs(c_.y0) + c_.length() + tm;
      z = mag(Zk, 0) + W * D0;
    }
    else if (i == 1 && j == 0) {
      ph = 1 + tm * ref_->deriv_bound(u0, u1, 2);
      z = mag(Zk, 1) + Ws * D0 + W * D1;
    }
    else if (i == 0 && j == 1) {
      ph = 1;
      z = D0 / dmin;
    }
    else if (i == 2) {
      ph = ref_->deriv_bound(u0, u1, 2) + tm * ref_->deriv_bound(u0, u1, 3);
      const double Wss = (mag(Bk, 2) + 2 * Ws * d1 + W * d2) / dmin;
      z = mag(Zk, 2) + Wss * D0 + 2 * Ws * D1 + W * D2;
    }
    else { /* i == 1, j == 1 */
      ph = ref_->deriv_bound(u0, u1, 2);
      z = (d1 / (dmin * dmin)) * D0 + Wt * D1;
    }
    return up(std::sqrt(ph * ph + z * z) * (1 + gamma_n(16)));
  }

  void special_u(double u0, double u1, util::Vector<SpecialParam> &out) const override
  {
    util::Vector<SpecialParam> found;
    ref_->special_params(u0, u1, found);
    for (int i = 0; i < d_.borders.size(); i++) {
      for (const Fn1D *f : {&d_.borders[i], &d_.heights[i]}) {
        for (int b = 1; b < f->pieces.size(); b++) {
          if (f->breaks[b] >= u0 && f->breaks[b] <= u1) {
            found.append({f->breaks[b], uint8_t(std::max(0, f->continuity_at(b)))});
          }
        }
      }
    }
    std::sort(found.begin(), found.end(), [](const SpecialParam &x, const SpecialParam &y) { return x.t < y.t; });
    for (const SpecialParam &x : found) {
      if (out.size() > 0 && out.last().t == x.t) {
        out.last().continuity = std::min(out.last().continuity, x.continuity);
        continue;
      }
      out.append(x);
    }
  }

  const detail::Road3Terms &terms() const
  {
    return terms_;
  }

private:
  /* the certified error terms over the whole domain (§3.2) */
  void bounds()
  {
    const int nb = d_.borders.size();
    /* the reference: r, r' (unit), r'', r''' */
    const double R0 = std::fabs(c_.x0) + std::fabs(c_.y0) + c_.length();
    const double be0 = ref_->eval_error(), be1 = ref_->deriv_eval_error(1), be2 = ref_->deriv_eval_error(2);
    const int per = c_.segs.size() > 0 ? c_.pa.size() / c_.segs.size() : 0;
    double K2 = 0, K3 = 0, K4 = 0, ep3 = 0;
    for (int i = 0; i < c_.segs.size() && per >= 4; i++) {
      const double len = c_.segs[i].length * (1 + 4 * kUnit);
      auto qm = [&](int q) {
        const Range a = c_.pa[i * per + q].range(0, len, 2), b = c_.pb[i * per + q].range(0, len, 2);
        return up(std::sqrt(a.abs_max() * a.abs_max() + b.abs_max() * b.abs_max()));
      };
      K2 = std::max(K2, qm(1));
      K3 = std::max(K3, qm(2));
      K4 = std::max(K4, qm(3));
      double ea, ebb;
      c_.pa[i * per + 2].eval(len, &ea);
      c_.pb[i * per + 2].eval(len, &ebb);
      ep3 = std::max(ep3, up(ea + ebb));
    }
    /* r''' as the cesaro evaluator forms it: P_2 rotated by the computed
     * tangent, plus the rounding of the local coordinate */
    const double be3 = per >= 4 ? up(ep3 + K3 * be1 + std::sqrt(2.0) * gamma_n(4) * K3 +
                                     4 * kUnit * c_.length() * (K4 + K2 * K3))
                                : INFINITY;
    terms_.r3 = be3;
    /* borders, heights, link widths and height differences */
    double Bm[3] = {0, 0, 0}, Be[3] = {0, 0, 0}, Zm[3] = {0, 0, 0}, Ze[3] = {0, 0, 0};
    for (int i = 0; i < nb; i++) {
      for (int q = 0; q < 3; q++) {
        Bm[q] = std::max(Bm[q], d_.borders[i].range(lo_, hi_, q).abs_max());
        Zm[q] = std::max(Zm[q], d_.heights[i].range(lo_, hi_, q).abs_max());
        Be[q] = std::max(Be[q], d_.borders[i].eval_error(q) + (q == 0 ? d_.borders[i].max_jump() : 0.0));
        Ze[q] = std::max(Ze[q], d_.heights[i].eval_error(q) + (q == 0 ? d_.heights[i].max_jump() : 0.0));
      }
    }
    double dmin = INFINITY, Dd[3] = {0, 0, 0}, DZ[3] = {0, 0, 0};
    for (int k = 0; k + 1 < nb; k++) {
      dmin = std::min(dmin, diff_range(d_.borders[k + 1], d_.borders[k], lo_, hi_, 0).lo);
      for (int q = 0; q < 3; q++) {
        Dd[q] = std::max(Dd[q], diff_range(d_.borders[k + 1], d_.borders[k], lo_, hi_, q).abs_max());
        DZ[q] = std::max(DZ[q], diff_range(d_.heights[k + 1], d_.heights[k], lo_, hi_, q).abs_max());
      }
    }
    const double tmax = Bm[0];

    /* value: xy as Offset2View's bound, without its offset's own terms */
    terms_.xy = up(be0 + tmax * be1 + std::sqrt(2.0) * gamma_n(3) * (R0 + tmax));
    /* w = (t - b_k) / (b_(k+1) - b_k), |exact w| <= 1 inside the link */
    const EB t = eb(tmax, 0);
    const EB dd = eb(Dd[0], up(2 * Be[0] + kUnit * (Dd[0] + 2 * Be[0])));
    const EB num = eb(Dd[0], up(Be[0] + kUnit * (Dd[0] + Be[0])));
    EB w = eb_div(num, dd, dmin);
    w.m = 1;
    terms_.w = w.e;
    const EB omw = eb(1, up(w.e + kUnit * (1 + w.e)));
    const EB zk = eb(Zm[0], Ze[0]);
    const EB z = eb_add(eb_mul(omw, zk), eb_mul(w, zk));
    terms_.z = z.e;
    terms_.e0 = up(terms_.xy + terms_.z);

    /* first partials */
    const EB r1 = eb(1, be1), r2 = eb(K2, be2), r3 = eb(K3, be3);
    const EB bk1 = eb(Bm[1], Be[1]), zk1 = eb(Zm[1], Ze[1]);
    const EB dd1 = eb(Dd[1], up(2 * Be[1] + kUnit * (Dd[1] + 2 * Be[1])));
    const EB D = eb(DZ[0], up(2 * Ze[0] + kUnit * (DZ[0] + 2 * Ze[0])));
    const EB D1 = eb(DZ[1], up(2 * Ze[1] + kUnit * (DZ[1] + 2 * Ze[1])));
    const EB ws = eb_div(eb_add(bk1, eb_mul(w, dd1)), dd, dmin);
    const EB wt = eb_div(eb(1, 0), dd, dmin);
    const EB Zs = eb_add(eb_add(zk1, eb_mul(ws, D)), eb_mul(w, D1));
    const EB Zt = eb_div(D, dd, dmin);
    const EB Ps = eb_add(r1, eb_mul(t, r2)); /* each component */
    terms_.e1 = up(std::max(2 * Ps.e + Zs.e, 2 * be1 + Zt.e));

    /* second partials */
    const EB bk2 = eb(Bm[2], Be[2]), zk2 = eb(Zm[2], Ze[2]);
    const EB dd2 = eb(Dd[2], up(2 * Be[2] + kUnit * (Dd[2] + 2 * Be[2])));
    const EB D2 = eb(DZ[2], up(2 * Ze[2] + kUnit * (DZ[2] + 2 * Ze[2])));
    const EB two_ws = eb(2 * ws.m, 2 * ws.e);
    const EB wss = eb_div(eb_add(eb_add(bk2, eb_mul(two_ws, dd1)), eb_mul(w, dd2)), dd, dmin);
    const EB wst = eb_div(dd1, eb_mul(dd, dd), dmin * dmin);
    const EB Zss = eb_add(eb_add(eb_add(zk2, eb_mul(wss, D)), eb_mul(two_ws, D1)), eb_mul(w, D2));
    const EB Zst = eb_add(eb_mul(wst, D), eb_mul(wt, D1));
    const EB Pss = eb_add(r2, eb_mul(t, r3));
    terms_.e2 = up(std::max(2 * Pss.e + Zss.e, 2 * be2 + Zst.e));
  }

  const Cesaro2 &c_;
  const Road3 &d_;
  util::UniquePtr<Curve2> ref_;
  double lo_ = 0, hi_ = 0;
  detail::Road3Terms terms_;
};

/* the reference of a valid entry (shape, domain and geometry), else null */
const Cesaro2 *resolve_valid(const attr::PoolStore &s, const Road3 &d)
{
  const Cesaro2 *c = resolve(s, d);
  return c && !check_geometry(*c, d) ? c : nullptr;
}

class Road3Kind final : public SurfaceKindOps {
public:
  const char *key() const override
  {
    return "curves.road3";
  }
  void add_pool(attr::PoolStore &s) const override
  {
    s.add_pool<Road3Pool>(key());
  }
  bool valid(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const Road3 *d = s.try_get<Road3Pool>(r);
    return d && resolve_valid(s, *d);
  }
  util::UniquePtr<SurfaceEval> make_surface(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const Road3 *d = s.try_get<Road3Pool>(r);
    const Cesaro2 *c = d ? resolve_valid(s, *d) : nullptr;
    if (!c) {
      return {};
    }
    return util::UniquePtr<SurfaceEval>(litestl::alloc::New<Road3View>("curves::Road3View", c, d));
  }
};

void hash_fns(hash::Hasher128 &h, const util::Vector<Fn1D, 2> &v)
{
  h.u32(uint32_t(v.size()));
  for (const Fn1D &f : v) {
    f.hash_into(h);
  }
}

} // namespace

const char *Road3::make(const attr::PoolStore &s,
                        attr::PoolRef reference,
                        const util::Vector<Fn1D, 2> &borders,
                        const util::Vector<Fn1D, 2> &heights,
                        Road3 &out)
{
  if (const char *e = check_shape(borders, heights)) {
    return e;
  }
  Road3 d;
  d.reference = reference;
  d.borders = borders;
  d.heights = heights;
  if (kind_of(reference) != &cesaro2_kind() || !s.try_get<Cesaro2Pool>(reference)) {
    return "road3: the reference must be a live cesaro2";
  }
  const Cesaro2 *c = resolve(s, d);
  if (!c) {
    return "road3: the domain must lie inside the reference's";
  }
  if (const char *e = check_geometry(*c, d)) {
    return e;
  }
  out = std::move(d);
  return nullptr;
}

void Road3::hash_into(hash::Hasher128 &h) const
{
  h.u32(reference.pool).u32(reference.slot);
  hash_fns(h, borders);
  hash_fns(h, heights);
}

bool Road3::equal(const Road3 &b) const
{
  if (!(reference == b.reference) || borders.size() != b.borders.size() || heights.size() != b.heights.size()) {
    return false;
  }
  for (int i = 0; i < borders.size(); i++) {
    if (!borders[i].equal(b.borders[i])) {
      return false;
    }
  }
  for (int i = 0; i < heights.size(); i++) {
    if (!heights[i].equal(b.heights[i])) {
      return false;
    }
  }
  return true;
}

void Road3::write(io::ByteWriter &w) const
{
  w.u32(reference.pool);
  w.u32(reference.slot);
  w.u32(uint32_t(borders.size()));
  for (const Fn1D &f : borders) {
    f.write(w);
  }
  for (const Fn1D &f : heights) {
    f.write(w);
  }
}

bool Road3::read(io::ByteReader &r, Road3 &v, const attr::PoolReadLimits &)
{
  Road3 d;
  d.reference.pool = r.u32();
  d.reference.slot = r.u32();
  const uint32_t n = r.u32();
  if (r.failed() || !attr::well_formed(d.reference) || d.reference.none() || n < 2 ||
      n > uint32_t(kMaxBorders))
  {
    return false;
  }
  for (int pass = 0; pass < 2; pass++) {
    for (uint32_t i = 0; i < n; i++) {
      Fn1D f;
      if (!Fn1D::read(r, f)) {
        return false;
      }
      (pass == 0 ? d.borders : d.heights).append(std::move(f));
    }
  }
  if (check_shape(d.borders, d.heights)) {
    return false;
  }
  v = std::move(d);
  return true;
}

const KindOps &road3_kind()
{
  static const Road3Kind k;
  return k;
}

bool detail::road3_probe(const attr::PoolStore &s, attr::PoolRef r, double u, double v, Side su, Side sv,
                         Road3Probe &out)
{
  const Road3 *d = s.try_get<Road3Pool>(r);
  const Cesaro2 *c = d ? resolve_valid(s, *d) : nullptr;
  if (!c) {
    return false;
  }
  const Road3View view(c, d);
  out.terms = view.terms();
  view.w_and_z(u, v, su, sv, &out.link, &out.w, &out.z);
  return true;
}

} // namespace ubertopo::curves
