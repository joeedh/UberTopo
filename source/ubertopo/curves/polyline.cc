/* polyline3 (polyline.h). */

#include "ubertopo/curves/polyline.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace ubertopo::curves {

namespace {

class Polyline3Eval final : public Curve3 {
public:
  explicit Polyline3Eval(const Polyline3 *p) : p_(*p)
  {
    for (double x : p_.pts) {
      max_abs_ = std::max(max_abs_, std::fabs(x));
    }
  }
  Interval domain() const override
  {
    return {0.0, double(p_.count() - 1)};
  }
  ParamKind param_kind() const override
  {
    return ParamKind::General;
  }
  /* span i holds t in [i, i + 1]; at a vertex, Side picks the span */
  int span(double t, Side side) const
  {
    const int last = p_.count() - 2;
    double f = std::floor(t);
    if (side == Side::Below && f == t) {
      f -= 1;
    }
    return std::clamp(int(f), 0, last);
  }
  void eval(double t, int n, Vec<3> *out, Side side) const override
  {
    t = std::clamp(t, 0.0, double(p_.count() - 1));
    const int i = span(t, side);
    const double u = t - double(i);
    const Vec<3> a = p_.point(i), b = p_.point(i + 1);
    Vec<3> d;
    for (int c = 0; c < 3; c++) {
      d[c] = b[c] - a[c];
      out[0][c] = a[c] + u * d[c];
    }
    if (n >= 1) {
      out[1] = d;
    }
    for (int k = 2; k <= n; k++) {
      out[k] = Vec<3>(0.0, 0.0, 0.0);
    }
  }
  double eval_error() const override
  {
    /* a + u (b - a), u in [0, 1]: per coordinate gamma(3) (|a| + |b - a|) */
    return up(std::sqrt(3.0) * gamma_n(3) * 3 * max_abs_);
  }
  double deriv_eval_error(int k) const override
  {
    return k == 1 ? up(std::sqrt(3.0) * kUnit * 2 * max_abs_) : 0.0;
  }
  double deriv_bound(double t0, double t1, int k) const override
  {
    const int i0 = span(t0, Side::Above), i1 = span(t1, Side::Below);
    if (k >= 1 && i1 > i0) {
      return INFINITY; /* a vertex strictly inside: not C1 */
    }
    if (k >= 2) {
      return 0.0;
    }
    double m = 0;
    for (int i = i0; i <= i1; i++) {
      const Vec<3> a = p_.point(i), b = p_.point(i + 1);
      double s = 0;
      for (int c = 0; c < 3; c++) {
        const double x = std::fabs(b[c] - a[c]) * (1 + kUnit);
        s += x * x;
      }
      m = std::max(m, s);
    }
    if (k == 1) {
      return up(std::sqrt(m) * (1 + gamma_n(4)));
    }
    /* k == 0: the vertices bound the spans */
    double v = 0;
    for (int i = i0; i <= i1 + 1; i++) {
      const Vec<3> a = p_.point(i);
      v = std::max(v, a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    }
    return up(std::sqrt(v) * (1 + gamma_n(4)));
  }
  void special_params(double t0, double t1, util::Vector<SpecialParam> &out) const override
  {
    for (int i = 1; i + 1 < p_.count(); i++) {
      if (double(i) >= t0 && double(i) <= t1) {
        out.append({double(i), 0});
      }
    }
  }

private:
  const Polyline3 &p_;
  double max_abs_ = 0;
};

class Polyline3Kind final : public KindOps {
public:
  const char *key() const override
  {
    return "curves.polyline3";
  }
  void add_pool(attr::PoolStore &s) const override
  {
    s.add_pool<Polyline3Pool>(key());
  }
  int dim(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    return s.try_get<Polyline3Pool>(r) ? 3 : 0;
  }
  util::UniquePtr<Curve3> make3(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const Polyline3 *p = s.try_get<Polyline3Pool>(r);
    return p ? p->evaluator() : util::UniquePtr<Curve3>();
  }
};

} // namespace

const char *Polyline3::make(const util::Vector<double, 12> &pts, Polyline3 &out)
{
  if (pts.size() % 3 != 0 || pts.size() < 6 || uint32_t(pts.size() / 3) > kMaxPoints) {
    return "polyline3: need 2..kMaxPoints points of 3 coordinates";
  }
  Polyline3 p;
  for (double x : pts) {
    if (!std::isfinite(x)) {
      return "polyline3: non-finite coordinate";
    }
    p.pts.append(x == 0 ? 0.0 : x);
  }
  for (int i = 0; i + 1 < p.count(); i++) {
    if (p.pts[3 * i] == p.pts[3 * i + 3] && p.pts[3 * i + 1] == p.pts[3 * i + 4] &&
        p.pts[3 * i + 2] == p.pts[3 * i + 5])
    {
      return "polyline3: repeated consecutive point";
    }
  }
  out = std::move(p);
  return nullptr;
}

void Polyline3::hash_into(hash::Hasher128 &h) const
{
  h.u32(uint32_t(count()));
  for (double x : pts) {
    h.f64_bits(x);
  }
}

bool Polyline3::equal(const Polyline3 &b) const
{
  if (pts.size() != b.pts.size()) {
    return false;
  }
  for (int i = 0; i < pts.size(); i++) {
    if (std::bit_cast<uint64_t>(pts[i]) != std::bit_cast<uint64_t>(b.pts[i])) {
      return false;
    }
  }
  return true;
}

void Polyline3::write(io::ByteWriter &w) const
{
  w.u32(uint32_t(count()));
  for (double x : pts) {
    w.u64(std::bit_cast<uint64_t>(x));
  }
}

bool Polyline3::read(io::ByteReader &r, Polyline3 &v, const attr::PoolReadLimits &)
{
  const uint32_t n = r.u32();
  if (r.failed() || n < 2 || n > kMaxPoints || uint64_t(n) * 24 > r.remaining()) {
    return false;
  }
  util::Vector<double, 12> pts;
  for (uint32_t i = 0; i < 3 * n; i++) {
    pts.append(std::bit_cast<double>(r.u64()));
  }
  Polyline3 p;
  if (r.failed() || make(pts, p)) {
    return false;
  }
  Polyline3 raw;
  raw.pts = pts;
  if (!p.equal(raw)) {
    return false;
  }
  v = std::move(p);
  return true;
}

util::UniquePtr<Curve3> Polyline3::evaluator() const
{
  return util::UniquePtr<Curve3>(litestl::alloc::New<Polyline3Eval>("curves::Polyline3Eval", this));
}

const KindOps &polyline3_kind()
{
  static const Polyline3Kind k;
  return k;
}

} // namespace ubertopo::curves
