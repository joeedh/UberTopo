/* offset2 (offset.h). */

#include "ubertopo/curves/offset.h"

#include <algorithm>
#include <cmath>

namespace ubertopo::curves {

namespace {

double vnorm2(double a, double b)
{
  return std::sqrt(a * a + b * b);
}

} // namespace

Offset2View::Offset2View(const Cesaro2 *base, const Offset2 *off) : b_(*base), o_(*off), base_(base->evaluator())
{
  const Fn1D &w = o_.w;
  const double lo = w.lo(), hi = w.hi();
  /* breaks: w's, plus the base joins strictly inside */
  util::Vector<double> pts;
  int wi = 0, ji = 1;
  const int nseg = b_.segs.size();
  while (wi < w.breaks.size() || ji < nseg) {
    double x;
    if (ji >= nseg || (wi < w.breaks.size() && w.breaks[wi] <= b_.seg_start[ji])) {
      x = w.breaks[wi++];
    }
    else {
      x = b_.seg_start[ji++];
    }
    if (x < lo || x > hi) {
      continue;
    }
    if (pts.size() == 0 || x > pts.last()) {
      pts.append(x);
    }
  }
  double delta = 0; /* rounding of the local origins */
  for (int i = 0; i + 1 < pts.size(); i++) {
    Span sp;
    sp.s0 = pts[i];
    sp.s1 = pts[i + 1];
    const double mid = sp.s0 + 0.5 * (sp.s1 - sp.s0);
    const int pw = w.piece_at(mid);
    int sj = int(std::upper_bound(b_.seg_start.begin(), b_.seg_start.begin() + nseg, mid) - b_.seg_start.begin()) - 1;
    sj = std::clamp(sj, 0, nseg - 1);
    const Poly wp = w.pieces[pw].compose_affine(sp.s0 - w.breaks[pw], 1.0);
    const Poly kp = b_.segs[sj].kappa.compose_affine(sp.s0 - b_.seg_start[sj], 1.0);
    delta = std::max(delta, kUnit * (std::fabs(sp.s0) + std::fabs(w.breaks[pw]) + std::fabs(b_.seg_start[sj])));
    Poly A = Poly::constant(1.0) - wp * kp, B = wp.deriv();
    for (int j = 0; j < kQ; j++) {
      sp.A[j] = A;
      sp.B[j] = B;
      const Poly A2 = A.deriv() - kp * B, B2 = B.deriv() + kp * A;
      A = A2;
      B = B2;
    }
    /* continuity at s0: w's break, and/or a base join (one order lower) */
    uint8_t cont = kSmooth;
    if (i > 0) {
      for (int b = 1; b < w.pieces.size(); b++) {
        if (w.breaks[b] == sp.s0) {
          cont = uint8_t(std::min(int(cont), std::max(0, w.continuity_at(b))));
        }
      }
      for (int j = 1; j < nseg; j++) {
        if (b_.seg_start[j] == sp.s0) {
          const uint8_t cj = b_.join_cont[j - 1];
          cont = std::min(cont, cj == kSmooth ? kSmooth : uint8_t(std::max(0, int(cj) - 1)));
        }
      }
    }
    sp.cont0 = cont;
    spans_.append(std::move(sp));
  }
  /* error bounds */
  const double wmax = w.range(lo, hi, 0).abs_max();
  const double be0 = base_->eval_error(), be1 = base_->deriv_eval_error(1);
  const double rmax = std::fabs(b_.x0) + std::fabs(b_.y0) + b_.length();
  e_[0] = up(be0 + wmax * be1 + w.eval_error(0) + w.max_jump() + std::sqrt(2.0) * gamma_n(3) * (rmax + wmax));
  double kmax = 0;
  for (const CesaroSegment &sg : b_.segs) {
    kmax = std::max(kmax, sg.kappa.range(0, sg.length, 2).abs_max());
  }
  double qmax[kQ] = {0, 0, 0, 0, 0, 0}, qerr[kQ] = {0, 0, 0, 0, 0, 0};
  for (const Span &sp : spans_) {
    const double len = sp.s1 - sp.s0;
    for (int j = 0; j < kQ; j++) {
      const Range ra = sp.A[j].range(0, len * (1 + 4 * kUnit), 2), rb = sp.B[j].range(0, len * (1 + 4 * kUnit), 2);
      qmax[j] = std::max(qmax[j], up(vnorm2(ra.abs_max(), rb.abs_max())));
      double ea, eb;
      sp.A[j].eval(len * (1 + 4 * kUnit), &ea);
      sp.B[j].eval(len * (1 + 4 * kUnit), &eb);
      qerr[j] = std::max(qerr[j], up(ea + eb));
    }
  }
  for (int k = 1; k <= 2; k++) {
    e_[k] = up(qerr[k - 1] + qmax[k - 1] * be1 + std::sqrt(2.0) * gamma_n(4) * qmax[k - 1] +
               delta * (qmax[k] + kmax * qmax[k - 1]));
  }
}

Interval Offset2View::domain() const
{
  return {o_.w.lo(), o_.w.hi()};
}

int Offset2View::span_at(double t, Side side) const
{
  int lo = 0, hi = spans_.size();
  while (hi - lo > 1) {
    const int mid = (lo + hi) / 2;
    const bool right = side == Side::Above ? t >= spans_[mid].s0 : t > spans_[mid].s0;
    if (right) {
      lo = mid;
    }
    else {
      hi = mid;
    }
  }
  return lo;
}

void Offset2View::eval(double t, int n, Vec<2> *out, Side side) const
{
  const Interval dom = domain();
  t = std::clamp(t, dom.lo, dom.hi);
  Vec<2> bo[2];
  base_->eval(t, 1, bo, side);
  const double w = o_.w.eval(t, 0, side);
  out[0] = Vec<2>(bo[0][0] - w * bo[1][1], bo[0][1] + w * bo[1][0]);
  if (n < 1) {
    return;
  }
  const Span &sp = spans_[span_at(t, side)];
  const double u = t - sp.s0, c = bo[1][0], s = bo[1][1];
  for (int k = 1; k <= n; k++) {
    if (k > kQ) {
      out[k] = Vec<2>(0.0, 0.0);
      continue;
    }
    const double A = sp.A[k - 1].eval(u), B = sp.B[k - 1].eval(u);
    out[k] = Vec<2>(A * c - B * s, A * s + B * c);
  }
}

double Offset2View::deriv_bound(double t0, double t1, int k) const
{
  if (k == 0) {
    const double rmax = std::fabs(b_.x0) + std::fabs(b_.y0) + b_.length();
    return up(rmax + o_.w.range(o_.w.lo(), o_.w.hi(), 0).abs_max());
  }
  if (k > kQ) {
    return INFINITY;
  }
  const int i0 = span_at(t0, Side::Above), i1 = span_at(t1, Side::Below);
  for (int i = i0 + 1; i <= i1; i++) {
    if (spans_[i].cont0 < k) {
      return INFINITY;
    }
  }
  double b = 0;
  for (int i = i0; i <= i1; i++) {
    const Span &sp = spans_[i];
    const double pad = 4 * kUnit * (std::fabs(t0) + std::fabs(t1) + std::fabs(sp.s0));
    const double ua = std::max(0.0, std::max(t0, sp.s0) - sp.s0 - pad);
    const double ub = std::max(ua, std::min(t1, sp.s1) - sp.s0 + pad);
    const Range ra = sp.A[k - 1].range(ua, ub, 2), rb = sp.B[k - 1].range(ua, ub, 2);
    b = std::max(b, vnorm2(ra.abs_max(), rb.abs_max()));
  }
  return up(b * (1 + gamma_n(4)));
}

void Offset2View::special_params(double t0, double t1, util::Vector<SpecialParam> &out) const
{
  util::Vector<SpecialParam> found;
  for (int i = 1; i < spans_.size(); i++) {
    if (spans_[i].s0 >= t0 && spans_[i].s0 <= t1) {
      found.append({spans_[i].s0, spans_[i].cont0});
    }
  }
  util::Vector<SpecialParam> bp;
  base_->special_params(t0, t1, bp);
  for (const SpecialParam &p : bp) {
    if (p.continuity == kSmooth) {
      found.append(p); /* the base's features (its joins are spans' breaks) */
    }
  }
  for (const Span &sp : spans_) {
    const double a = std::max(t0, sp.s0) - sp.s0, b = std::min(t1, sp.s1) - sp.s0;
    if (!(b > a) || sp.A[0].degree() < 1) {
      continue;
    }
    util::Vector<Root> rs;
    sp.A[0].roots(a, b, rs);
    for (const Root &r : rs) {
      found.append({sp.s0 + r.t, kSmooth}); /* a fold */
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

bool Offset2View::offset_valid(double t0, double t1) const
{
  const int i0 = span_at(t0, Side::Above), i1 = span_at(t1, Side::Below);
  for (int i = i0; i <= i1; i++) {
    const Span &sp = spans_[i];
    const double a = std::max(t0, sp.s0) - sp.s0, b = std::max(a, std::min(t1, sp.s1) - sp.s0);
    if (sp.A[0].range(a, b, 2).lo > 0) {
      continue;
    }
    util::Vector<Root> rs;
    if (sp.A[0].degree() >= 1) {
      sp.A[0].roots(a, b, rs);
    }
    if (rs.size() > 0 || !(sp.A[0].eval(a + 0.5 * (b - a)) > 0)) {
      return false;
    }
  }
  return true;
}

/* ---- the pool value ---- */

const char *Offset2::make(const attr::PoolStore &s, attr::PoolRef base, const Fn1D &w, Offset2 &out)
{
  const Cesaro2 *c = kind_of(base) == &cesaro2_kind() ? s.try_get<Cesaro2Pool>(base) : nullptr;
  if (!c) {
    return "offset2: the base must be a live cesaro2";
  }
  if (w.empty() || !(w.lo() >= 0) || !(w.hi() <= c->length())) {
    return "offset2: w's domain must lie inside the base's";
  }
  for (int b = 1; b < w.pieces.size(); b++) {
    if (w.continuity_at(b) < 0) {
      return "offset2: w jumps at a break";
    }
  }
  out.base = base;
  out.w = w;
  return nullptr;
}

void Offset2::hash_into(hash::Hasher128 &h) const
{
  h.u32(base.pool).u32(base.slot);
  w.hash_into(h);
}

bool Offset2::equal(const Offset2 &b) const
{
  return base == b.base && w.equal(b.w);
}

void Offset2::write(io::ByteWriter &wr) const
{
  wr.u32(base.pool);
  wr.u32(base.slot);
  w.write(wr);
}

bool Offset2::read(io::ByteReader &r, Offset2 &v, const attr::PoolReadLimits &)
{
  Offset2 o;
  o.base.pool = r.u32();
  o.base.slot = r.u32();
  if (r.failed() || !attr::well_formed(o.base) || o.base.none() || !Fn1D::read(r, o.w)) {
    return false;
  }
  v = std::move(o);
  return true;
}

namespace {

/* the base and offset of a valid offset2 entry, else nullptrs */
const Cesaro2 *resolve(const attr::PoolStore &s, attr::PoolRef r, const Offset2 **o)
{
  *o = s.try_get<Offset2Pool>(r);
  if (!*o || kind_of((*o)->base) != &cesaro2_kind()) {
    return nullptr;
  }
  const Cesaro2 *c = s.try_get<Cesaro2Pool>((*o)->base);
  if (!c || !((*o)->w.lo() >= 0) || !((*o)->w.hi() <= c->length())) {
    return nullptr;
  }
  return c;
}

class Offset2Kind final : public KindOps {
public:
  const char *key() const override
  {
    return "curves.offset2";
  }
  void add_pool(attr::PoolStore &s) const override
  {
    s.add_pool<Offset2Pool>(key());
  }
  int dim(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const Offset2 *o;
    return resolve(s, r, &o) ? 2 : 0;
  }
  util::UniquePtr<Curve2> make2(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const Offset2 *o;
    const Cesaro2 *c = resolve(s, r, &o);
    if (!c) {
      return {};
    }
    return util::UniquePtr<Curve2>(litestl::alloc::New<Offset2View>("curves::Offset2View", c, o));
  }
};

} // namespace

attr::PoolRef intern_offset(attr::PoolStore &s, attr::PoolRef base, const Fn1D &w, const char **err)
{
  Offset2 o;
  if (const char *e = Offset2::make(s, base, w, o)) {
    if (err) {
      *err = e;
    }
    return attr::kNoRef;
  }
  const Cesaro2 &c = s.get<Cesaro2Pool>(base);
  if (w.is_zero() && w.lo() == 0 && w.hi() == c.length()) {
    return base;
  }
  return s.intern<Offset2Pool>(pool_id(offset2_kind()), std::move(o));
}

const KindOps &offset2_kind()
{
  static const Offset2Kind k;
  return k;
}

} // namespace ubertopo::curves
