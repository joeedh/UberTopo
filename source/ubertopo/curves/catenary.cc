/* catenary3 (catenary.h). */

#include "ubertopo/curves/catenary.h"

#include <algorithm>
#include <bit>
#include <cmath>

#include "litestl/math/detmath.h"

namespace ubertopo::curves {

namespace dm = litestl::math::detmath;

namespace {

constexpr double kHyp = (dm::kSinhCoshUlp + 1) * kUnit; /* sinh/cosh relative error, rounded */

double zs(double x)
{
  return x == 0 ? 0.0 : x;
}

/* g(x) = f(x) - f(0) */
double g_of(const Catenary3 &k, double x)
{
  const double ia = 0.5 / k.a;
  return 2 * k.a * dm::sinh((x - 2 * k.c) * ia) * dm::sinh(x * ia);
}

class Catenary3Eval final : public Curve3 {
public:
  explicit Catenary3Eval(const Catenary3 *k) : k_(*k)
  {
    const double a = k_.a, d = k_.d, c = k_.c;
    /* extremes over x in [0, d] */
    Y_ = up(std::max(std::fabs(c), std::fabs(d - c)) / a * (1 + 4 * kUnit) + k_.c_err / a);
    const double Aal = up(std::max(std::fabs(c) / a, std::fabs(d - 2 * c) / (2 * a)) * (1 + 4 * kUnit));
    const double hb = up(d / (2 * a) * (1 + 4 * kUnit));
    sinhY_ = up(dm::sinh(Y_) * (1 + kHyp));
    coshY_ = up(dm::cosh(Y_) * (1 + kHyp));
    const double sa = up(dm::sinh(Aal) * (1 + kHyp)), ca = up(dm::cosh(Aal) * (1 + kHyp));
    const double sb = up(dm::sinh(hb) * (1 + kHyp)), cb = up(dm::cosh(hb) * (1 + kHyp));
    gmax_ = up(2 * a * sa * sb);
    const double dal = 3 * kUnit * (d + 2 * std::fabs(c)) / (2 * a), dbe = 2 * kUnit * hb;
    const double eg = up(2 * a * (ca * dal * sb + sa * cb * dbe) + (2 * dm::kSinhCoshUlp + 6) * kUnit * gmax_ +
                         2 * sinhY_ * k_.c_err);
    double amax = 0;
    for (int i = 0; i < 3; i++) {
      amax = std::max(amax, std::fabs(k_.A[i]));
    }
    e0_ = up(std::sqrt(3.0) * gamma_n(6) * (amax + d) + eg + gamma_n(2) * (std::fabs(k_.A[2]) + gmax_) +
             2 * kUnit * d * coshY_);
    const double dgam = up((2 * kUnit * (d + std::fabs(c)) + k_.c_err) / a * (1 + kUnit) + kUnit * Y_);
    e1_ = up(3 * kUnit * std::sqrt(2.0) + coshY_ * dgam + kHyp * sinhY_);
    e2_ = up((sinhY_ * dgam + (kHyp + 2 * kUnit) * coshY_) / a);
  }
  Interval domain() const override
  {
    return {0.0, k_.d};
  }
  ParamKind param_kind() const override
  {
    return ParamKind::General;
  }
  void eval(double x, int n, Vec<3> *out, Side) const override
  {
    x = std::clamp(x, 0.0, k_.d);
    const double g = g_of(k_, x);
    out[0] = Vec<3>(k_.A[0] + x * k_.ux, k_.A[1] + x * k_.uy, k_.A[2] + g);
    if (n < 1) {
      return;
    }
    const double gam = (x - k_.c) / k_.a;
    const double sh = dm::sinh(gam), ch = n >= 2 ? dm::cosh(gam) : 0.0;
    out[1] = Vec<3>(k_.ux, k_.uy, sh);
    double p = 1;
    for (int k = 2; k <= n; k++) {
      p /= k_.a;
      out[k] = Vec<3>(0.0, 0.0, p * (k % 2 == 0 ? ch : sh));
    }
  }
  double eval_error() const override
  {
    return e0_;
  }
  double deriv_eval_error(int k) const override
  {
    return k == 0 ? e0_ : k == 1 ? e1_ : k == 2 ? e2_ : INFINITY;
  }
  double deriv_bound(double t0, double t1, int k) const override
  {
    if (k == 0) {
      double amax = 0;
      for (int i = 0; i < 3; i++) {
        amax += k_.A[i] * k_.A[i];
      }
      return up(std::sqrt(amax) + k_.d + gmax_);
    }
    /* |sinh| and cosh of (x - c) / a are largest at an end of the range */
    const double ge = up((std::max(std::fabs(t0 - k_.c), std::fabs(t1 - k_.c)) * (1 + 2 * kUnit) +
                          2 * kUnit * (std::fabs(t0) + std::fabs(t1) + std::fabs(k_.c)) + k_.c_err) /
                         k_.a * (1 + 2 * kUnit));
    const double sh = up(dm::sinh(ge) * (1 + kHyp));
    if (k == 1) {
      return up(std::sqrt(1 + sh * sh) * (1 + gamma_n(4)));
    }
    const double ch = up(dm::cosh(ge) * (1 + kHyp));
    double p = 1;
    for (int j = 2; j <= k; j++) {
      p = up(p / k_.a * (1 + kUnit));
    }
    return up(p * (k % 2 == 0 ? ch : sh));
  }
  void special_params(double t0, double t1, util::Vector<SpecialParam> &out) const override
  {
    if (k_.c > 0 && k_.c < k_.d && k_.c >= t0 && k_.c <= t1) {
      out.append({k_.c, kSmooth}); /* the lowest point */
    }
  }

private:
  const Catenary3 &k_;
  double Y_ = 0, sinhY_ = 0, coshY_ = 0, gmax_ = 0, e0_ = 0, e1_ = 0, e2_ = 0;
};

class Catenary3Kind final : public KindOps {
public:
  const char *key() const override
  {
    return "curves.catenary3";
  }
  void add_pool(attr::PoolStore &s) const override
  {
    s.add_pool<Catenary3Pool>(key());
  }
  int dim(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    return s.try_get<Catenary3Pool>(r) ? 3 : 0;
  }
  util::UniquePtr<Curve3> make3(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const Catenary3 *k = s.try_get<Catenary3Pool>(r);
    return k ? k->evaluator() : util::UniquePtr<Curve3>();
  }
};

} // namespace

const char *Catenary3::make(const double A_[3], const double B_[3], double a_, Catenary3 &out)
{
  for (int i = 0; i < 3; i++) {
    if (!std::isfinite(A_[i]) || !std::isfinite(B_[i])) {
      return "catenary3: non-finite anchor";
    }
  }
  if (!std::isfinite(a_) || !(a_ > 0)) {
    return "catenary3: a must be finite and > 0";
  }
  Catenary3 k;
  for (int i = 0; i < 3; i++) {
    k.A[i] = zs(A_[i]);
    k.B[i] = zs(B_[i]);
  }
  k.a = a_;
  const double dx = k.B[0] - k.A[0], dy = k.B[1] - k.A[1], dz = k.B[2] - k.A[2];
  k.d = std::sqrt(dx * dx + dy * dy);
  if (!(k.d > 0) || !std::isfinite(k.d)) {
    return "catenary3: anchors on one vertical line";
  }
  k.ux = dx / k.d;
  k.uy = dy / k.d;
  const double hb = k.d / (2 * k.a);
  if (!(hb <= kMaxArg)) {
    return "catenary3: span too long for a (overflow)";
  }
  /* sinh((d - 2c) / (2a)) = dz / (2a sinh(d / (2a))) */
  const double S = dm::sinh(hb);
  const double q = dz / (2 * k.a * S);
  if (!std::isfinite(q)) {
    return "catenary3: span too short for a";
  }
  const double as = dm::asinh(q);
  k.c = zs(0.5 * k.d - k.a * as);
  if (!(std::max(std::fabs(k.c), std::fabs(k.d - k.c)) / k.a <= kMaxArg)) {
    return "catenary3: anchors too far from the lowest point (overflow)";
  }
  /* rounding of c: q's relative error (sinh of a rounded argument, the
   * products), asinh's error, then the products and the difference; dz and
   * d carry one rounding each */
  const double eq = (2 * dm::kSinhCoshUlp + 8 + 2 * (1 + hb)) * kUnit; /* x coth x <= 1 + x */
  /* asinh'(q) = 1 / sqrt(1 + q^2): q's relative error eq moves asinh by
   * eq |q| / sqrt(1 + q^2) */
  const double das = up(eq * std::fabs(q) / std::sqrt(1 + q * q) * (1 + 4 * kUnit));
  k.c_err = up(k.a * (das + (dm::kAsinhUlp + 2) * kUnit * std::fabs(as)) + 4 * kUnit * (0.5 * k.d + std::fabs(k.c)) +
               2 * kUnit * k.d);
  out = k;
  return nullptr;
}

double Catenary3::sag() const
{
  return 0.5 * (B[2] - A[2]) - g_of(*this, 0.5 * d);
}

const char *Catenary3::from_sag(const double A_[3], const double B_[3], double sag, Catenary3 &out)
{
  if (!std::isfinite(sag) || !(sag > 0)) {
    return "catenary3: sag must be finite and > 0";
  }
  const double dx = B_[0] - A_[0], dy = B_[1] - A_[1];
  const double d = std::sqrt(dx * dx + dy * dy);
  if (!(d > 0) || !std::isfinite(d)) {
    return "catenary3: anchors on one vertical line";
  }
  /* sag(a) decreases with a; sag >= d^2 / (8a) on a level span */
  Catenary3 k;
  double lo = d / (2 * kMaxArg) * 1.01, hi = std::max(d * d / (8 * sag), d) * 4;
  int tries = 0;
  while (make(A_, B_, lo, k) && tries++ < 200) {
    lo *= 1.25;
  }
  if (make(A_, B_, lo, k) || k.sag() < sag) {
    return "catenary3: sag too large for the span";
  }
  tries = 0;
  while ((make(A_, B_, hi, k) || k.sag() > sag) && tries++ < 200) {
    hi *= 2;
  }
  if (make(A_, B_, hi, k) || k.sag() > sag) {
    return "catenary3: sag too small for the span";
  }
  for (int it = 0; it < 120; it++) {
    const double mid = std::sqrt(lo * hi);
    if (!(mid > lo && mid < hi)) {
      break;
    }
    if (make(A_, B_, mid, k) || k.sag() > sag) {
      lo = mid;
    }
    else {
      hi = mid;
    }
  }
  /* hi is always valid; lo too unless it failed to build: take the closer */
  Catenary3 kl;
  if (const char *e = make(A_, B_, hi, k)) {
    return e;
  }
  if (!make(A_, B_, lo, kl) && std::fabs(kl.sag() - sag) < std::fabs(k.sag() - sag)) {
    k = kl;
  }
  out = k;
  return nullptr;
}

void Catenary3::hash_into(hash::Hasher128 &h) const
{
  for (int i = 0; i < 3; i++) {
    h.f64_bits(A[i]);
  }
  for (int i = 0; i < 3; i++) {
    h.f64_bits(B[i]);
  }
  h.f64_bits(a);
}

bool Catenary3::equal(const Catenary3 &b) const
{
  auto same = [](double x, double y) { return std::bit_cast<uint64_t>(x) == std::bit_cast<uint64_t>(y); };
  for (int i = 0; i < 3; i++) {
    if (!same(A[i], b.A[i]) || !same(B[i], b.B[i])) {
      return false;
    }
  }
  return same(a, b.a);
}

void Catenary3::write(io::ByteWriter &w) const
{
  for (int i = 0; i < 3; i++) {
    w.u64(std::bit_cast<uint64_t>(A[i]));
  }
  for (int i = 0; i < 3; i++) {
    w.u64(std::bit_cast<uint64_t>(B[i]));
  }
  w.u64(std::bit_cast<uint64_t>(a));
}

bool Catenary3::read(io::ByteReader &r, Catenary3 &v, const attr::PoolReadLimits &)
{
  double A_[3], B_[3];
  for (int i = 0; i < 3; i++) {
    A_[i] = std::bit_cast<double>(r.u64());
  }
  for (int i = 0; i < 3; i++) {
    B_[i] = std::bit_cast<double>(r.u64());
  }
  const double a_ = std::bit_cast<double>(r.u64());
  Catenary3 k;
  if (r.failed() || make(A_, B_, a_, k)) {
    return false;
  }
  Catenary3 raw;
  for (int i = 0; i < 3; i++) {
    raw.A[i] = A_[i];
    raw.B[i] = B_[i];
  }
  raw.a = a_;
  if (!k.equal(raw)) {
    return false;
  }
  v = k;
  return true;
}

util::UniquePtr<Curve3> Catenary3::evaluator() const
{
  return util::UniquePtr<Curve3>(litestl::alloc::New<Catenary3Eval>("curves::Catenary3Eval", this));
}

const KindOps &catenary3_kind()
{
  static const Catenary3Kind k;
  return k;
}

} // namespace ubertopo::curves
