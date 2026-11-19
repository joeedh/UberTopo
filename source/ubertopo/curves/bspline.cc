/* bspline (bspline.h): evaluation through derivative control points
 * (Piegl & Tiller A2.2 basis functions, A3.3 derivative control points). */

#include "ubertopo/curves/bspline.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace ubertopo::curves {

namespace {

double zs(double x)
{
  return x == 0 ? 0.0 : x;
}

/* span i with U[i] <= t < U[i+1] (Above) or U[i] < t <= U[i+1] (Below),
 * clamped to [p, n] */
int find_span(const BSpline &b, double t, Side side)
{
  const int p = b.degree, n = b.count() - 1;
  const double *U = b.knots.data();
  const double *it = side == Side::Above ? std::upper_bound(U + p, U + n + 1, t) : std::lower_bound(U + p, U + n + 1, t);
  return std::clamp(int(it - U) - 1, p, n);
}

/* nonzero basis functions of every degree d <= p at span i:
 * N[j][d] = N_{i-d+j, d}(t), j <= d (the upper triangle of A2.2/A2.3's ndu) */
void basis_all(const BSpline &b, int i, double t, double N[8][8])
{
  const int p = b.degree;
  const double *U = b.knots.data();
  double left[8], right[8];
  N[0][0] = 1.0;
  for (int d = 1; d <= p; d++) {
    left[d] = t - U[i + 1 - d];
    right[d] = U[i + d] - t;
    double saved = 0.0;
    for (int r = 0; r < d; r++) {
      const double den = right[r + 1] + left[d - r];
      const double temp = den != 0 ? N[r][d - 1] / den : 0.0;
      N[r][d] = saved + right[r + 1] * temp;
      saved = left[d - r] * temp;
    }
    N[d][d] = saved;
  }
}

template <int D> class BSplineEval final : public CurveEval<D> {
public:
  explicit BSplineEval(const BSpline *b) : b_(*b) {}
  Interval domain() const override
  {
    return {b_.lo(), b_.hi()};
  }
  ParamKind param_kind() const override
  {
    return ParamKind::General;
  }
  void eval(double t, int n, Vec<D> *out, Side side) const override
  {
    const int p = b_.degree;
    t = std::clamp(t, b_.lo(), b_.hi());
    const int i = find_span(b_, t, side);
    double N[8][8];
    basis_all(b_, i, t, N);
    for (int k = 0; k <= n; k++) {
      Vec<D> v;
      for (int c = 0; c < D; c++) {
        v[c] = 0;
      }
      if (k <= p) {
        const int d = p - k;
        const double *Q = k == 0 ? b_.ctrl.data() : b_.dctrl[k - 1].data();
        for (int j = 0; j <= d; j++) {
          const double w = N[j][d];
          const double *q = Q + size_t(i - p + j) * D;
          for (int c = 0; c < D; c++) {
            v[c] += w * q[c];
          }
        }
      }
      out[k] = v;
    }
  }
  double eval_error() const override
  {
    return deriv_eval_error(0);
  }
  double deriv_eval_error(int k) const override
  {
    if (k > b_.degree) {
      return 0;
    }
    const double sd = std::sqrt(double(D));
    return up(sd * (gamma_n(3 * b_.degree + 3) * b_.max_abs[k] + b_.qerr[k]));
  }
  double deriv_bound(double t0, double t1, int k) const override
  {
    const int p = b_.degree;
    /* a knot strictly inside with continuity < k: not C^k there */
    for (int j = p + 1; j < b_.count(); j++) {
      const double u = b_.knots[j];
      if (u > t0 && u < t1) {
        int m = 1;
        while (j + m < b_.count() && b_.knots[j + m] == u) {
          m++;
        }
        if (p - m < k) {
          return INFINITY;
        }
        j += m - 1;
      }
    }
    if (k > p) {
      return 0;
    }
    const int i0 = find_span(b_, t0, Side::Above), i1 = find_span(b_, t1, Side::Below);
    const double *Q = k == 0 ? b_.ctrl.data() : b_.dctrl[k - 1].data();
    double m = 0;
    for (int j = i0 - p; j <= i1 - k; j++) {
      double s = 0;
      for (int c = 0; c < D; c++) {
        const double x = std::fabs(Q[size_t(j) * D + c]) + b_.qerr[k];
        s += x * x;
      }
      m = std::max(m, s);
    }
    return up(std::sqrt(m) * (1 + gamma_n(4)));
  }
  void special_params(double t0, double t1, util::Vector<SpecialParam> &out) const override
  {
    const int p = b_.degree;
    for (int j = p + 1; j < b_.count(); j++) {
      const double u = b_.knots[j];
      int m = 1;
      while (j + m < b_.count() && b_.knots[j + m] == u) {
        m++;
      }
      if (u >= t0 && u <= t1) {
        out.append({u, uint8_t(p - m)});
      }
      j += m - 1;
    }
  }

private:
  const BSpline &b_;
};

class BSplineKind final : public KindOps {
public:
  const char *key() const override
  {
    return "curves.bspline";
  }
  void add_pool(attr::PoolStore &s) const override
  {
    s.add_pool<BSplinePool>(key());
  }
  int dim(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const BSpline *b = s.try_get<BSplinePool>(r);
    return b ? b->dim : 0;
  }
  util::UniquePtr<Curve2> make2(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const BSpline *b = s.try_get<BSplinePool>(r);
    return b && b->dim == 2 ? b->evaluator2() : util::UniquePtr<Curve2>();
  }
  util::UniquePtr<Curve3> make3(const attr::PoolStore &s, attr::PoolRef r) const override
  {
    const BSpline *b = s.try_get<BSplinePool>(r);
    return b && b->dim == 3 ? b->evaluator3() : util::UniquePtr<Curve3>();
  }
};

} // namespace

const char *BSpline::make(int dim, int degree, const util::Vector<double, 16> &knots,
                          const util::Vector<double, 48> &ctrl, attr::PoolRef source, double src_t0, double src_t1,
                          double max_error, BSpline &out)
{
  if ((dim != 2 && dim != 3) || degree < 1 || degree > kMaxDegree) {
    return "bspline: dimension must be 2 or 3 and degree 1..7";
  }
  if (ctrl.size() % dim != 0) {
    return "bspline: control point array size";
  }
  const int n = ctrl.size() / dim, p = degree;
  if (n < p + 1 || uint32_t(n) > kMaxCount || knots.size() != n + p + 1) {
    return "bspline: need count >= degree + 1 and count + degree + 1 knots";
  }
  for (double x : knots) {
    if (!std::isfinite(x)) {
      return "bspline: non-finite knot";
    }
  }
  for (double x : ctrl) {
    if (!std::isfinite(x)) {
      return "bspline: non-finite control point";
    }
  }
  for (int i = 0; i + 1 < knots.size(); i++) {
    if (knots[i + 1] < knots[i]) {
      return "bspline: knots must not decrease";
    }
  }
  for (int i = 1; i <= p; i++) {
    if (knots[i] != knots[0] || knots[n + i] != knots[n]) {
      return "bspline: knots must be clamped";
    }
  }
  if (!(knots[n] > knots[p])) {
    return "bspline: empty domain";
  }
  for (int j = p + 1; j < n; j++) {
    int m = 1;
    while (j + m < n && knots[j + m] == knots[j]) {
      m++;
    }
    if (m > p) {
      return "bspline: interior knot multiplicity > degree";
    }
    j += m - 1;
  }
  if (!attr::well_formed(source)) {
    return "bspline: malformed source ref";
  }
  const bool has_src = !source.none();
  if (has_src && (!std::isfinite(src_t0) || !std::isfinite(src_t1) || !std::isfinite(max_error) || max_error < 0)) {
    return "bspline: invalid source range or error";
  }
  BSpline b;
  b.dim = uint8_t(dim);
  b.degree = uint8_t(degree);
  for (double x : knots) {
    b.knots.append(zs(x));
  }
  for (double x : ctrl) {
    b.ctrl.append(zs(x));
  }
  b.source = source;
  b.src_t0 = has_src ? zs(src_t0) : 0.0;
  b.src_t1 = has_src ? zs(src_t1) : 0.0;
  b.max_error = has_src ? zs(max_error) : 0.0;
  /* derivative control points and their rounding bounds */
  for (double x : b.ctrl) {
    b.max_abs[0] = std::max(b.max_abs[0], std::fabs(x));
  }
  const util::Vector<double, 48> *prev = &b.ctrl;
  for (int k = 1; k <= std::min(p, 6); k++) {
    util::Vector<double, 48> q;
    const int cnt = n - k;
    q.resize(cnt * dim);
    double min_den = INFINITY;
    for (int i = 0; i < cnt; i++) {
      const double den = b.knots[i + p + 1] - b.knots[i + k];
      if (den > 0) {
        min_den = std::min(min_den, den);
      }
      for (int c = 0; c < dim; c++) {
        const double diff = (*prev)[(i + 1) * dim + c] - (*prev)[i * dim + c];
        q[i * dim + c] = den > 0 ? double(p - k + 1) * diff / den : 0.0;
        b.max_abs[k] = std::max(b.max_abs[k], std::fabs(q[i * dim + c]));
      }
    }
    const double scale = std::isfinite(min_den) ? double(p - k + 1) / min_den : 0.0;
    b.qerr[k] = up(scale * (2 * b.qerr[k - 1] + 2 * kUnit * b.max_abs[k - 1]) + gamma_n(3) * b.max_abs[k]);
    b.dctrl.append(std::move(q));
    prev = &b.dctrl.last();
  }
  out = std::move(b);
  return nullptr;
}

const char *BSpline::insert_knot(const BSpline &b, double u, BSpline &out)
{
  const int p = b.degree, n = b.count(), D = b.dim;
  if (!(u > b.lo() && u < b.hi())) {
    return "bspline: inserted knot outside the domain";
  }
  int mult = 0;
  for (double k : b.knots) {
    mult += k == u;
  }
  if (mult >= p) {
    return "bspline: inserted knot would exceed multiplicity p";
  }
  const int k = find_span(b, u, Side::Above);
  util::Vector<double, 16> knots;
  util::Vector<double, 48> ctrl;
  for (int i = 0; i <= k; i++) {
    knots.append(b.knots[i]);
  }
  knots.append(u);
  for (int i = k + 1; i < b.knots.size(); i++) {
    knots.append(b.knots[i]);
  }
  for (int i = 0; i <= n; i++) {
    for (int c = 0; c < D; c++) {
      double v;
      if (i <= k - p) {
        v = b.ctrl[i * D + c];
      }
      else if (i <= k - mult) {
        const double a = (u - b.knots[i]) / (b.knots[i + p] - b.knots[i]);
        v = (1 - a) * b.ctrl[(i - 1) * D + c] + a * b.ctrl[i * D + c];
      }
      else {
        v = b.ctrl[(i - 1) * D + c];
      }
      ctrl.append(v);
    }
  }
  return make(D, p, knots, ctrl, attr::kNoRef, 0, 0, 0, out);
}

void BSpline::hash_into(hash::Hasher128 &h) const
{
  h.u32(dim).u32(degree).u32(uint32_t(count()));
  for (double x : knots) {
    h.f64_bits(x);
  }
  for (double x : ctrl) {
    h.f64_bits(x);
  }
  h.u32(source.pool).u32(source.slot);
  h.f64_bits(src_t0).f64_bits(src_t1).f64_bits(max_error);
}

bool BSpline::equal(const BSpline &b) const
{
  auto same = [](double x, double y) { return std::bit_cast<uint64_t>(x) == std::bit_cast<uint64_t>(y); };
  if (dim != b.dim || degree != b.degree || knots.size() != b.knots.size() || ctrl.size() != b.ctrl.size() ||
      !(source == b.source) || !same(src_t0, b.src_t0) || !same(src_t1, b.src_t1) || !same(max_error, b.max_error))
  {
    return false;
  }
  for (int i = 0; i < knots.size(); i++) {
    if (!same(knots[i], b.knots[i])) {
      return false;
    }
  }
  for (int i = 0; i < ctrl.size(); i++) {
    if (!same(ctrl[i], b.ctrl[i])) {
      return false;
    }
  }
  return true;
}

void BSpline::write(io::ByteWriter &w) const
{
  w.u32(dim);
  w.u32(degree);
  w.u32(uint32_t(count()));
  for (double x : knots) {
    w.u64(std::bit_cast<uint64_t>(x));
  }
  for (double x : ctrl) {
    w.u64(std::bit_cast<uint64_t>(x));
  }
  w.u32(source.pool);
  w.u32(source.slot);
  w.u64(std::bit_cast<uint64_t>(src_t0));
  w.u64(std::bit_cast<uint64_t>(src_t1));
  w.u64(std::bit_cast<uint64_t>(max_error));
}

bool BSpline::read(io::ByteReader &r, BSpline &v, const attr::PoolReadLimits &)
{
  const uint32_t dim = r.u32(), degree = r.u32(), n = r.u32();
  if (r.failed() || (dim != 2 && dim != 3) || degree < 1 || degree > uint32_t(kMaxDegree) || n > kMaxCount ||
      uint64_t(n + degree + 1 + uint64_t(n) * dim) * 8 > r.remaining())
  {
    return false;
  }
  util::Vector<double, 16> knots;
  util::Vector<double, 48> ctrl;
  for (uint32_t i = 0; i < n + degree + 1; i++) {
    knots.append(std::bit_cast<double>(r.u64()));
  }
  for (uint32_t i = 0; i < n * dim; i++) {
    ctrl.append(std::bit_cast<double>(r.u64()));
  }
  attr::PoolRef src;
  src.pool = r.u32();
  src.slot = r.u32();
  const double t0 = std::bit_cast<double>(r.u64()), t1 = std::bit_cast<double>(r.u64()),
               me = std::bit_cast<double>(r.u64());
  if (r.failed()) {
    return false;
  }
  BSpline b;
  if (make(int(dim), int(degree), knots, ctrl, src, t0, t1, me, b)) {
    return false;
  }
  BSpline raw;
  raw.dim = uint8_t(dim);
  raw.degree = uint8_t(degree);
  raw.knots = knots;
  raw.ctrl = ctrl;
  raw.source = src;
  raw.src_t0 = t0;
  raw.src_t1 = t1;
  raw.max_error = me;
  if (!b.equal(raw)) {
    return false;
  }
  v = std::move(b);
  return true;
}

util::UniquePtr<Curve2> BSpline::evaluator2() const
{
  return util::UniquePtr<Curve2>(litestl::alloc::New<BSplineEval<2>>("curves::BSplineEval", this));
}

util::UniquePtr<Curve3> BSpline::evaluator3() const
{
  return util::UniquePtr<Curve3>(litestl::alloc::New<BSplineEval<3>>("curves::BSplineEval", this));
}

const KindOps &bspline_kind()
{
  static const BSplineKind k;
  return k;
}

} // namespace ubertopo::curves
