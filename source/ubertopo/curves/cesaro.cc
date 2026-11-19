/* cesaro2: planar curvature-law curves (cesaro.h; plan P4a §3.3). */

#include "ubertopo/curves/cesaro.h"

#include <algorithm>
#include <bit>
#include <cmath>

#include "litestl/math/detmath.h"

namespace ubertopo::curves {

namespace dm = litestl::math::detmath;

namespace {

constexpr int kN = Cesaro2::kTaylorOrder; /* series order of e^{iQ} */
constexpr int kK = 160;                   /* majorant terms before the Cauchy estimate */
constexpr int kDerivPolys = 6;            /* P_0..P_5: r' .. r^(6) */

struct DD {
  double hi = 0, lo = 0;
  void add(double x)
  {
    const double s = hi + x;
    const double bb = s - hi;
    const double e = (hi - (s - bb)) + (x - bb);
    hi = s;
    lo += e;
    const double t = hi + lo;
    lo = lo - (t - hi);
    hi = t;
  }
};

double zero_sign(double x)
{
  return x == 0 ? 0.0 : x;
}

/* Higham's Horner bound with per-coefficient factors: sum gamma(2i+2) |c_i| r^i */
double horner_bound(const util::Vector<double, 32> &c, double r)
{
  double b = 0, rp = 1;
  for (int i = 0; i < c.size(); i++) {
    b += gamma_n(2 * i + 2) * std::fabs(c[i]) * rp;
    rp *= r;
  }
  return b;
}

double horner(const util::Vector<double, 32> &c, double u)
{
  double v = 0;
  for (int i = c.size(); i-- > 0;) {
    v = v * u + c[i];
  }
  return v;
}

/* One candidate piece [sa, sb] of a segment with heading polynomial th.
 * Returns false if its bend exceeds kMaxBend; otherwise fills p (except the
 * start point) and its local error bound. */
bool build_piece(const Poly &th, double sa, double sb, CesaroPiece &p, double &local_err, double &reducible)
{
  const double h = sb - sa;
  const double r = 0.5 * h;
  const double m = sa + r;
  const double ua = sa - m, ub = sb - m;
  const double rr = std::max(std::fabs(ua), std::fabs(ub));
  const Poly q = th.compose_affine(m, 1.0);
  const int D = q.size() - 1; /* degree of Q */
  double qhat[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  double bend = 0, qerr = 0, rp = rr;
  for (int j = 1; j <= D; j++) {
    qhat[j] = std::fabs(q.c[j]) + q.e[j];
    bend += qhat[j] * rp;
    qerr += q.e[j] * rp;
    rp *= rr;
  }
  if (bend > Cesaro2::kMaxBend) {
    return false;
  }
  /* series of E(u) = e^{iQ(u)} and of its majorant e^{Q^(u)} */
  double er[kN + 1], ei[kN + 1], eh[kK + 1];
  er[0] = 1;
  ei[0] = 0;
  for (int k = 0; k < kN; k++) {
    double sr = 0, si = 0;
    for (int j = 0; j <= std::min(k, D - 1); j++) {
      const double a = double(j + 1) * q.c[j + 1];
      sr += a * er[k - j];
      si += a * ei[k - j];
    }
    er[k + 1] = -si / double(k + 1);
    ei[k + 1] = sr / double(k + 1);
  }
  eh[0] = 1;
  for (int k = 0; k < kK; k++) {
    double s = 0;
    for (int j = 0; j <= std::min(k, D - 1); j++) {
      s += double(j + 1) * qhat[j + 1] * eh[k - j];
    }
    eh[k + 1] = s / double(k + 1);
  }
  /* certified error terms (absolute, over the piece) */
  double tail = 0, rec = 0, mag = 0;
  rp = 1;
  for (int k = 0; k <= kK; k++) {
    if (k > kN) {
      tail += eh[k] * rp;
    }
    else {
      rec += gamma_n(6 * k + 6) * eh[k] * 2 * rp * rr / double(k + 1);
      mag += eh[k] * rp * rr / double(k + 1);
    }
    rp *= rr;
  }
  {
    const double R = 4 * rr;
    double qR = 0, Rp = R;
    for (int j = 1; j <= D; j++) {
      qR += qhat[j] * Rp;
      Rp *= R;
    }
    /* (rr / R)^(K+1) / (1 - rr / R) = 4^-(K+1) * 4/3 */
    const double cauchy = up(dm::exp(qR) * (1 + 8 * kUnit)) * std::ldexp(4.0 / 3.0, -2 * (kK + 1));
    tail = up(2 * rr * (tail + cauchy));
  }
  /* the position polynomial P(u) = int_{ua}^{u} E */
  p.px.resize(kN + 2);
  p.py.resize(kN + 2);
  double uap[kN + 2];
  uap[0] = ua;
  for (int k = 0; k <= kN; k++) {
    const double d = double(k + 1);
    p.px[k + 1] = er[k] / d;
    p.py[k + 1] = ei[k] / d;
    uap[k + 1] = uap[k] * ua;
  }
  /* constant term -sum_k px[k+1] ua^(k+1), summed from the smallest terms up:
   * term k carries ua^(k+1) (k roundings), its product (1) and the k + 1
   * additions after it */
  double cx = 0, cy = 0, cerr = 0;
  for (int k = kN; k >= 0; k--) {
    const double tx = p.px[k + 1] * uap[k], ty = p.py[k + 1] * uap[k];
    cx -= tx;
    cy -= ty;
    cerr += gamma_n(2 * k + 4) * (std::fabs(tx) + std::fabs(ty));
  }
  p.px[0] = cx;
  p.py[0] = cy;
  (void)mag;
  const double horner_err = horner_bound(p.px, rr) + horner_bound(p.py, rr) + cerr;
  /* rotation by e^{i theta(m)}: sincos error, theta(m) uncertainty, products */
  double s, c;
  dm::sincos(q.c[0], &s, &c);
  p.cos_m = c;
  p.sin_m = s;
  const double pmax = 2 * rr * (1 + 1e-12);
  const double rot = pmax * (2 * dm::kSinCosAbs + q.e[0] + 8 * kUnit);
  p.sa = sa;
  p.sb = sb;
  p.m = m;
  p.r = rr;
  local_err = up(tail + up(2 * rr * qerr) + rec + horner_err + rot);
  /* the part bisection reduces faster than h; the rest (heading uncertainty,
   * Horner and rotation rounding) is proportional to h and stays in the bound */
  reducible = up(tail + up(2 * rr * qerr) + rec);
  return true;
}

double piece_value_x(const CesaroPiece &p, double u)
{
  const double px = horner(p.px, u), py = horner(p.py, u);
  return p.cos_m * px - p.sin_m * py;
}
double piece_value_y(const CesaroPiece &p, double u)
{
  const double px = horner(p.px, u), py = horner(p.py, u);
  return p.sin_m * px + p.cos_m * py;
}

struct Builder {
  Cesaro2 &out;
  DD sx, sy;
  double start_err = 0;
  double max_abs = 0;
  bool overflow = false;

  /* emits pieces of [sa, sb] in order, bisecting until each meets the target */
  void refine(const Poly &th, double sa, double sb, int depth)
  {
    if (overflow) {
      return;
    }
    CesaroPiece p;
    double err = 0, reducible = 0;
    const bool ok = build_piece(th, sa, sb, p, err, reducible);
    const double h = sb - sa;
    const double m = sa + 0.5 * h;
    if ((!ok || reducible > Cesaro2::kPieceTol * h) && depth < 60 && m > sa && m < sb) {
      refine(th, sa, m, depth + 1);
      refine(th, m, sb, depth + 1);
      return;
    }
    if (!ok || uint32_t(out.pieces.size()) >= Cesaro2::kMaxPieces) {
      overflow = true;
      return;
    }
    p.sx = sx.hi;
    p.sy = sy.hi;
    p.err = up(start_err + err + kUnit * (std::fabs(sx.hi) + std::fabs(sy.hi)));
    const double u_end = sb - p.m;
    const double dx = piece_value_x(p, u_end), dy = piece_value_y(p, u_end);
    sx.add(dx);
    sy.add(dy);
    start_err = up(start_err + err);
    max_abs = std::max({max_abs, std::fabs(sx.hi), std::fabs(sy.hi)});
    out.pieces.append(std::move(p));
  }
};

} // namespace

const char *Cesaro2::make(double x0, double y0, double theta0, const util::Vector<CesaroSegment, 4> &segs,
                          Cesaro2 &out)
{
  if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(theta0) ||
      std::fabs(theta0) > dm::kMaxTrigArg)
  {
    return "cesaro2: non-finite start pose";
  }
  Cesaro2 c;
  c.x0 = zero_sign(x0);
  c.y0 = zero_sign(y0);
  c.theta0 = zero_sign(dm::reduce_angle(theta0));
  for (const CesaroSegment &s : segs) {
    if (!std::isfinite(s.length) || s.length < 0) {
      return "cesaro2: segment lengths must be finite and >= 0";
    }
    if (s.length == 0) {
      continue;
    }
    CesaroSegment t;
    t.length = s.length;
    t.kappa = s.kappa;
    for (double x : t.kappa.c) {
      if (!std::isfinite(x)) {
        return "cesaro2: non-finite curvature coefficient";
      }
    }
    for (double &x : t.kappa.e) {
      x = 0;
    }
    t.kappa.canonicalize();
    if (t.kappa.degree() > kMaxKappaDegree) {
      return "cesaro2: curvature of degree > 3";
    }
    c.segs.append(std::move(t));
  }
  if (c.segs.size() == 0) {
    return "cesaro2: zero length";
  }
  /* derived data */
  const int n = c.segs.size();
  Builder b{c};
  b.sx.hi = c.x0;
  b.sy.hi = c.y0;
  b.max_abs = std::max(std::fabs(c.x0), std::fabs(c.y0));
  double th_start = c.theta0, th_err = 0;
  DD s_acc;
  c.seg_start.append(0.0);
  double max_theta_err = 0, max_kappa_err = 0, max_kappa = 0, join_mismatch = 0;
  for (int i = 0; i < n; i++) {
    const CesaroSegment &sg = c.segs[i];
    Poly th = sg.kappa.integral(th_start);
    th.e[0] = th_err;
    /* the heading must stay inside detmath's trig domain */
    const Range tr = th.range(0, sg.length);
    if (!(std::fabs(tr.lo) < dm::kMaxTrigArg && std::fabs(tr.hi) < dm::kMaxTrigArg)) {
      return "cesaro2: turning exceeds the trigonometric domain";
    }
    c.seg_piece.append(uint32_t(c.pieces.size()));
    b.refine(th, 0.0, sg.length, 0);
    if (b.overflow) {
      return "cesaro2: too many pieces";
    }
    double te, ke;
    th_start = th.eval(sg.length, &te);
    th_err = te;
    th.eval(sg.length, &te);
    max_theta_err = std::max(max_theta_err, te);
    sg.kappa.eval(sg.length, &ke);
    const Range kr = sg.kappa.range(0, sg.length, 2);
    max_kappa = std::max(max_kappa, kr.abs_max());
    max_kappa_err = std::max(max_kappa_err, ke + gamma_n(8) * kr.abs_max());
    /* P_k polynomials: P_0 = 1, P_{k+1} = (A' - kappa B) + i (B' + kappa A) */
    Poly A = Poly::exact({1.0}), B;
    for (int k = 0; k < kDerivPolys; k++) {
      c.pa.append(A);
      c.pb.append(B);
      const Poly A2 = A.deriv() - sg.kappa * B, B2 = B.deriv() + sg.kappa * A;
      A = A2;
      B = B2;
    }
    c.theta.append(std::move(th));
    s_acc.add(sg.length);
    c.seg_start.append(s_acc.hi);
    if (i + 1 < n) {
      /* continuity at the join, judged on the exact data */
      /* r^(j+2) involves kappa^(j): a jump in kappa^(j) makes the join
       * C^(j+1); identical laws make it C-infinity (kSmooth) */
      Poly dl = sg.kappa, dr = c.segs[i + 1].kappa;
      uint8_t cont = kSmooth;
      for (int j = 0; j <= kMaxKappaDegree; j++) {
        double e;
        const double vl = dl.eval(sg.length, &e);
        const double vr = dr.size() ? dr.c[0] : 0.0;
        e += dr.size() ? dr.e[0] : 0.0;
        if (std::fabs(vl - vr) > e) {
          cont = uint8_t(j + 1);
          break;
        }
        if (j == 0) {
          join_mismatch = std::max(join_mismatch, std::fabs(vl - vr) + e);
        }
        dl = dl.deriv();
        dr = dr.deriv();
      }
      c.join_cont.append(cont);
    }
  }
  c.seg_piece.append(uint32_t(c.pieces.size()));
  double max_piece = 0;
  for (const CesaroPiece &p : c.pieces) {
    max_piece = std::max(max_piece, p.err);
  }
  const double L = c.length();
  /* output rounding (two coordinates), parameter rounding (|r'| = 1) */
  c.eval_err = up(max_piece + 2 * kUnit * 2 * b.max_abs + 4 * kUnit * L);
  c.deriv_err[0] = c.eval_err;
  c.deriv_err[1] = up(max_theta_err + max_kappa * 4 * kUnit * L + 2 * dm::kSinCosAbs);
  c.deriv_err[2] = up(max_kappa_err + join_mismatch + max_kappa * (c.deriv_err[1] + 4 * kUnit));
  out = std::move(c);
  return nullptr;
}

const char *Cesaro2::line(double x0, double y0, double theta0, double length, Cesaro2 &out)
{
  util::Vector<CesaroSegment, 4> s;
  s.append({length, Poly::exact({0.0})});
  return make(x0, y0, theta0, s, out);
}

const char *Cesaro2::arc(double x0, double y0, double theta0, double kappa, double length, Cesaro2 &out)
{
  util::Vector<CesaroSegment, 4> s;
  s.append({length, Poly::exact({kappa})});
  return make(x0, y0, theta0, s, out);
}

const char *Cesaro2::clothoid(double x0, double y0, double theta0, double k0, double k1, double length,
                              Cesaro2 &out)
{
  util::Vector<CesaroSegment, 4> s;
  s.append({length, Poly::exact({k0, (k1 - k0) / length})});
  return make(x0, y0, theta0, s, out);
}

void Cesaro2::hash_into(hash::Hasher128 &h) const
{
  h.f64_bits(x0).f64_bits(y0).f64_bits(theta0);
  h.u32(uint32_t(segs.size()));
  for (const CesaroSegment &s : segs) {
    h.f64_bits(s.length);
    h.u32(uint32_t(s.kappa.size()));
    for (double x : s.kappa.c) {
      h.f64_bits(x);
    }
  }
}

bool Cesaro2::equal(const Cesaro2 &b) const
{
  auto same = [](double x, double y) { return std::bit_cast<uint64_t>(x) == std::bit_cast<uint64_t>(y); };
  if (!same(x0, b.x0) || !same(y0, b.y0) || !same(theta0, b.theta0) || segs.size() != b.segs.size()) {
    return false;
  }
  for (int i = 0; i < segs.size(); i++) {
    const CesaroSegment &s = segs[i], &t = b.segs[i];
    if (!same(s.length, t.length) || s.kappa.size() != t.kappa.size()) {
      return false;
    }
    for (int k = 0; k < s.kappa.size(); k++) {
      if (!same(s.kappa.c[k], t.kappa.c[k])) {
        return false;
      }
    }
  }
  return true;
}

void Cesaro2::write(io::ByteWriter &w) const
{
  w.u64(std::bit_cast<uint64_t>(x0));
  w.u64(std::bit_cast<uint64_t>(y0));
  w.u64(std::bit_cast<uint64_t>(theta0));
  w.u32(uint32_t(segs.size()));
  for (const CesaroSegment &s : segs) {
    w.u64(std::bit_cast<uint64_t>(s.length));
    w.u32(uint32_t(s.kappa.size()));
    for (double x : s.kappa.c) {
      w.u64(std::bit_cast<uint64_t>(x));
    }
  }
}

bool Cesaro2::read(io::ByteReader &r, Cesaro2 &v, const attr::PoolReadLimits &)
{
  const double x0 = std::bit_cast<double>(r.u64()), y0 = std::bit_cast<double>(r.u64()),
               t0 = std::bit_cast<double>(r.u64());
  const uint32_t n = r.u32();
  if (r.failed() || n == 0 || n > kMaxPieces || uint64_t(n) * 12 > r.remaining()) {
    return false;
  }
  util::Vector<CesaroSegment, 4> segs;
  for (uint32_t i = 0; i < n; i++) {
    CesaroSegment s;
    s.length = std::bit_cast<double>(r.u64());
    const uint32_t m = r.u32();
    if (r.failed() || m > uint32_t(kMaxKappaDegree + 1)) {
      return false;
    }
    for (uint32_t k = 0; k < m; k++) {
      s.kappa.c.append(std::bit_cast<double>(r.u64()));
      s.kappa.e.append(0);
    }
    segs.append(std::move(s));
  }
  if (r.failed()) {
    return false;
  }
  Cesaro2 c;
  if (make(x0, y0, t0, segs, c)) {
    return false;
  }
  Cesaro2 raw;
  raw.x0 = x0;
  raw.y0 = y0;
  raw.theta0 = t0;
  raw.segs = segs;
  if (!c.equal(raw)) {
    return false; /* not canonical */
  }
  v = std::move(c);
  return true;
}

namespace {

class Cesaro2Eval final : public Curve2 {
public:
  /* by pointer: alloc::New copies its arguments (debugging.md) */
  explicit Cesaro2Eval(const Cesaro2 *c) : c_(*c) {}

  Interval domain() const override
  {
    return {0.0, c_.length()};
  }
  ParamKind param_kind() const override
  {
    return ParamKind::ArcLength;
  }

  int segment_at(double t, Side side) const
  {
    const int n = c_.segs.size();
    int lo = 0, hi = n;
    while (hi - lo > 1) {
      const int mid = (lo + hi) / 2;
      const bool right = side == Side::Above ? t >= c_.seg_start[mid] : t > c_.seg_start[mid];
      if (right) {
        lo = mid;
      }
      else {
        hi = mid;
      }
    }
    return lo;
  }

  void eval(double t, int n, Vec<2> *out, Side side) const override
  {
    t = std::clamp(t, 0.0, c_.length());
    const int i = segment_at(t, side);
    const double sigma = std::clamp(t - c_.seg_start[i], 0.0, c_.segs[i].length);
    /* the piece of segment i holding sigma */
    int lo = int(c_.seg_piece[i]), hi = int(c_.seg_piece[i + 1]);
    while (hi - lo > 1) {
      const int mid = (lo + hi) / 2;
      if (sigma >= c_.pieces[mid].sa) {
        lo = mid;
      }
      else {
        hi = mid;
      }
    }
    const CesaroPiece &p = c_.pieces[lo];
    const double u = sigma - p.m;
    out[0] = Vec<2>(p.sx + piece_value_x(p, u), p.sy + piece_value_y(p, u));
    if (n < 1) {
      return;
    }
    double s, co;
    dm::sincos(c_.theta[i].eval(sigma), &s, &co);
    out[1] = Vec<2>(co, s);
    for (int k = 2; k <= n; k++) {
      const double A = c_.pa[i * kDerivPolys + k - 1].eval(sigma), B = c_.pb[i * kDerivPolys + k - 1].eval(sigma);
      out[k] = Vec<2>(A * co - B * s, A * s + B * co);
    }
  }

  double eval_error() const override
  {
    return c_.eval_err;
  }
  double deriv_eval_error(int k) const override
  {
    return k <= 2 ? c_.deriv_err[k] : INFINITY;
  }

  double deriv_bound(double t0, double t1, int k) const override
  {
    if (k <= 1) {
      return up(1.0);
    }
    if (k > kDerivPolys) {
      return INFINITY;
    }
    const int i0 = segment_at(t0, Side::Above), i1 = segment_at(t1, Side::Below);
    for (int j = i0; j < i1; j++) {
      if (c_.join_cont[j] < k) {
        return INFINITY;
      }
    }
    double b = 0;
    for (int i = i0; i <= i1; i++) {
      const double s0 = c_.seg_start[i];
      const double pad = 4 * kUnit * (std::fabs(t0) + std::fabs(t1) + s0);
      const double a = std::max(0.0, std::max(t0, s0) - s0 - pad);
      const double e = std::min(c_.segs[i].length, std::min(t1, c_.seg_start[i + 1]) - s0 + pad);
      const Range A = c_.pa[i * kDerivPolys + k - 1].range(a, std::max(a, e), 2);
      const Range B = c_.pb[i * kDerivPolys + k - 1].range(a, std::max(a, e), 2);
      const double am = A.abs_max(), bm = B.abs_max();
      b = std::max(b, up(std::sqrt(am * am + bm * bm)));
    }
    return b;
  }

  void special_params(double t0, double t1, util::Vector<SpecialParam> &out) const override
  {
    util::Vector<SpecialParam> found;
    const int n = c_.segs.size();
    for (int i = 0; i < n; i++) {
      const double s0 = c_.seg_start[i];
      if (i > 0 && s0 >= t0 && s0 <= t1) {
        found.append({s0, c_.join_cont[i - 1]});
      }
      const double a = std::max(t0, s0) - s0, b = std::min(t1, c_.seg_start[i + 1]) - s0;
      if (!(b > a)) {
        continue;
      }
      const Poly &kp = c_.segs[i].kappa;
      util::Vector<Root> rs;
      if (kp.degree() >= 1) {
        kp.roots(a, b, rs);
      }
      const Poly dk = kp.deriv();
      if (dk.degree() >= 1) {
        dk.roots(a, b, rs);
      }
      for (const Root &x : rs) {
        found.append({s0 + x.t, kSmooth});
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

private:
  const Cesaro2 &c_;
};

class Cesaro2Kind final : public KindOps {
public:
  const char *key() const override
  {
    return "curves.cesaro2";
  }
  void add_pool(attr::PoolStore &s) const override
  {
    s.add_pool<Cesaro2Pool>(key());
  }
  int dim(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    return s.try_get<Cesaro2Pool>(r) ? 2 : 0;
  }
  util::UniquePtr<Curve2> make2(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const Cesaro2 *c = s.try_get<Cesaro2Pool>(r);
    return c ? c->evaluator() : util::UniquePtr<Curve2>();
  }
};

} // namespace

util::UniquePtr<Curve2> Cesaro2::evaluator() const
{
  return util::UniquePtr<Curve2>(litestl::alloc::New<Cesaro2Eval>("curves::Cesaro2Eval", this));
}

const KindOps &cesaro2_kind()
{
  static const Cesaro2Kind k;
  return k;
}

} // namespace ubertopo::curves
