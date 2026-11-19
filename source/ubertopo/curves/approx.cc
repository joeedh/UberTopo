/* Generic curve algorithms (approx.h). */

#include "ubertopo/curves/approx.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace ubertopo::curves {

namespace {

constexpr int kMaxDepth = 60;

/* [t0, t1] cut at the special parameters strictly inside where the curve is
 * a break (continuity < kSmooth): features are not cut, which also avoids
 * sliver pieces at root clusters just inside an end; cont[j] is the continuity at interior cut j
 * (cuts[j + 1]) */
template <int D>
void root_partition(const CurveEval<D> &c, double t0, double t1, util::Vector<double> &cuts,
                    util::Vector<uint8_t> &cont)
{
  util::Vector<SpecialParam> sp;
  c.special_params(t0, t1, sp);
  cuts.append(t0);
  for (const SpecialParam &p : sp) {
    if (!(p.t > t0 && p.t < t1) || p.continuity >= kSmooth) {
      continue;
    }
    if (p.t == cuts.last()) {
      cont.last() = std::min(cont.last(), p.continuity);
      continue;
    }
    cuts.append(p.t);
    cont.append(p.continuity);
  }
  cuts.append(t1);
}

bool splittable(double a, double b, double &m)
{
  m = a + 0.5 * (b - a);
  return m > a && m < b;
}

template <int D> double vnorm(const Vec<D> &v)
{
  double s = 0;
  for (int i = 0; i < D; i++) {
    s += v[i] * v[i];
  }
  return std::sqrt(s);
}

template <int D> double vmax(const Vec<D> &v)
{
  double s = 0;
  for (int i = 0; i < D; i++) {
    s = std::max(s, std::fabs(v[i]));
  }
  return s;
}

void set_err(const char **err, const char *msg)
{
  if (err) {
    *err = msg;
  }
}

} // namespace

/* ---- bounds ---- */

template <int D> Box<D> bounds(const CurveEval<D> &c, double t0, double t1)
{
  Box<D> box;
  for (int i = 0; i < D; i++) {
    box.lo[i] = INFINITY;
    box.hi[i] = -INFINITY;
  }
  util::Vector<double> cuts;
  util::Vector<uint8_t> cont;
  root_partition(c, t0, t1, cuts, cont);
  const double e0 = c.eval_error();
  struct Rec {
    const CurveEval<D> &c;
    Box<D> &box;
    double e0;
    void run(double a, double b, int depth)
    {
      const double h = b - a;
      const double m1 = c.deriv_bound(a, b, 1), m2 = c.deriv_bound(a, b, 2);
      double m;
      /* bisect until the widening is small against the arc length h * M1 */
      if (depth < 12 && !(h * m2 <= m1 / 8) && splittable(a, b, m)) {
        run(a, m, depth + 1);
        run(m, b, depth + 1);
        return;
      }
      double w = std::isfinite(m2) ? up(h * h / 8 * m2) : up(h * m1);
      w = up(up(w + e0) * (1 + 4 * kUnit));
      Vec<D> pa, pb;
      c.eval(a, 0, &pa, Side::Above);
      c.eval(b, 0, &pb, Side::Below);
      for (int i = 0; i < D; i++) {
        const double lo = std::nextafter(std::min(pa[i], pb[i]) - w, -INFINITY);
        const double hi = std::nextafter(std::max(pa[i], pb[i]) + w, INFINITY);
        box.lo[i] = std::min(box.lo[i], std::isnan(lo) ? -INFINITY : lo);
        box.hi[i] = std::max(box.hi[i], std::isnan(hi) ? INFINITY : hi);
      }
    }
  } rec{c, box, e0};
  for (int j = 0; j + 1 < cuts.size(); j++) {
    rec.run(cuts[j], cuts[j + 1], 0);
  }
  return box;
}

/* ---- flatten ---- */

template <int D>
bool flatten(const CurveEval<D> &c, double t0, double t1, double tol, util::Vector<double> &out, const char **err)
{
  if (!(t1 >= t0) || !std::isfinite(t0) || !std::isfinite(t1)) {
    set_err(err, "flatten: invalid range");
    return false;
  }
  const double e0 = c.eval_error();
  if (!(e0 < tol)) {
    set_err(err, "flatten: tolerance below the curve's evaluation error");
    return false;
  }
  out.append(t0);
  if (t1 == t0) {
    return true;
  }
  util::Vector<double> cuts;
  util::Vector<uint8_t> cont;
  root_partition(c, t0, t1, cuts, cont);
  struct Rec {
    const CurveEval<D> &c;
    util::Vector<double> &out;
    double e0, tol;
    bool ok = true;
    void run(double a, double b, int depth)
    {
      if (!ok) {
        return;
      }
      const double h = b - a;
      const double dev = up(up(h * h / 8 * c.deriv_bound(a, b, 2)) + e0);
      if (!(dev <= tol)) {
        double m;
        if (depth < kMaxDepth && splittable(a, b, m)) {
          run(a, m, depth + 1);
          run(m, b, depth + 1);
          return;
        }
        ok = false;
        return;
      }
      out.append(b);
    }
  } rec{c, out, e0, tol};
  for (int j = 0; j + 1 < cuts.size(); j++) {
    rec.run(cuts[j], cuts[j + 1], 0);
  }
  if (!rec.ok) {
    set_err(err, "flatten: bisection limit");
    return false;
  }
  return true;
}

/* ---- Hermite basis norms ---- */

const double *hermite_lambdas()
{
  static const double *const lambdas = [] {
    static double L[3];
    /* sampled sups plus a Lipschitz margin: each sum has |d/dt| <= 4 on [0, 1] */
    constexpr int kN = 1 << 12;
    double m[3] = {0, 0, 0};
    for (int i = 0; i <= kN; i++) {
      const double t = double(i) / kN, t2 = t * t, t3 = t2 * t, t4 = t3 * t, t5 = t4 * t;
      const double h0 = 1 - 10 * t3 + 15 * t4 - 6 * t5, h5 = 10 * t3 - 15 * t4 + 6 * t5;
      const double h1 = t - 6 * t3 + 8 * t4 - 3 * t5, h4 = -4 * t3 + 7 * t4 - 3 * t5;
      const double h2 = 0.5 * (t2 - 3 * t3 + 3 * t4 - t5), h3 = 0.5 * (t3 - 2 * t4 + t5);
      m[0] = std::max(m[0], std::fabs(h0) + std::fabs(h5));
      m[1] = std::max(m[1], std::fabs(h1) + std::fabs(h4));
      m[2] = std::max(m[2], std::fabs(h2) + std::fabs(h3));
    }
    for (int j = 0; j < 3; j++) {
      L[j] = up(m[j] + 4.0 / (2 * kN) + 1e-12);
    }
    return L;
  }();
  return lambdas;
}

/* ---- approximate ---- */

namespace {

/* Blossom of a Bezier piece at u[0..4] (arguments may lie outside [a, b]),
 * with a bound on its rounding. */
template <int D> void blossom(const detail::BezierPiece<D> &p, const double *u, Vec<D> &out, double &rerr)
{
  Vec<D> w[6];
  double M = 0;
  for (int j = 0; j < 6; j++) {
    w[j] = p.B[j];
    M = std::max(M, vmax(w[j]));
  }
  const double h = p.b - p.a;
  double e = 0;
  for (int k = 0; k < 5; k++) {
    const double s = (u[k] - p.a) / h;
    const double W = std::fabs(1 - s) + std::fabs(s);
    for (int j = 0; j < 5 - k; j++) {
      for (int c = 0; c < D; c++) {
        w[j][c] = w[j][c] + s * (w[j + 1][c] - w[j][c]);
      }
    }
    e = W * e + gamma_n(6) * (2 * std::fabs(s) + W) * M;
    M *= W;
  }
  out = w[0];
  rerr = up(std::sqrt(double(D)) * e);
}

} // namespace

namespace detail {

template <int D>
void assemble(const util::Vector<BezierPiece<D>> &pieces, const util::Vector<int> &mult,
              util::Vector<double, 16> &knots, util::Vector<double, 48> &ctrl, util::Vector<double> &moved)
{
  const int P = pieces.size();
  knots.clear();
  ctrl.clear();
  moved.clear();
  for (int r = 0; r < 6; r++) {
    knots.append(pieces[0].a);
  }
  for (int j = 0; j + 1 < P; j++) {
    for (int r = 0; r < mult[j]; r++) {
      knots.append(pieces[j].b);
    }
  }
  for (int r = 0; r < 6; r++) {
    knots.append(pieces[P - 1].b);
  }
  const int n = knots.size() - 6;
  moved.resize(P);
  for (int q = 0; q < P; q++) {
    moved[q] = 0;
  }
  /* control point i = the blossom at knots[i+1..i+5] of each piece in its
   * support [knots[i], knots[i+6]] (equal for a spline of that continuity);
   * the mean, with what it moves each of those pieces */
  int q0 = 0;
  for (int i = 0; i < n; i++) {
    const double s0 = knots[i], s1 = knots[i + 6];
    while (q0 < P && pieces[q0].b <= s0) {
      q0++;
    }
    Vec<D> sum;
    for (int c = 0; c < D; c++) {
      sum[c] = 0;
    }
    Vec<D> bl[8];
    double re[8];
    int qs[8];
    int cnt = 0;
    for (int q = q0; q < P && pieces[q].b <= s1 && cnt < 8; q++) {
      if (!(pieces[q].a >= s0)) {
        continue;
      }
      blossom(pieces[q], knots.data() + i + 1, bl[cnt], re[cnt]);
      for (int c = 0; c < D; c++) {
        sum[c] += bl[cnt][c];
      }
      qs[cnt++] = q;
    }
    Vec<D> pt;
    double mag = 0;
    for (int c = 0; c < D; c++) {
      pt[c] = sum[c] / double(cnt);
      ctrl.append(pt[c]);
      mag = std::max(mag, std::fabs(pt[c]));
    }
    for (int k = 0; k < cnt; k++) {
      Vec<D> d;
      for (int c = 0; c < D; c++) {
        d[c] = bl[k][c] - pt[c];
      }
      const double dev = up(vnorm(d) * (1 + 4 * kUnit) + re[k] + gamma_n(D + 2) * std::sqrt(double(D)) * mag);
      moved[qs[k]] = std::max(moved[qs[k]], dev);
    }
  }
}

template void assemble<2>(const util::Vector<BezierPiece<2>> &, const util::Vector<int> &, util::Vector<double, 16> &,
                          util::Vector<double, 48> &, util::Vector<double> &);
template void assemble<3>(const util::Vector<BezierPiece<3>> &, const util::Vector<int> &, util::Vector<double, 16> &,
                          util::Vector<double, 48> &, util::Vector<double> &);

} // namespace detail

namespace {

template <int D>
attr::PoolRef approximate_impl(attr::PoolStore &store, util::UniquePtr<CurveEval<D>> ev, attr::PoolRef src,
                               double t0, double t1, double tol, const char **err)
{
  const Interval dom = ev->domain();
  if (!(t1 > t0) || !(t0 >= dom.lo) || !(t1 <= dom.hi) || !(tol > 0) || !std::isfinite(tol)) {
    set_err(err, "approximate: invalid range or tolerance");
    return attr::kNoRef;
  }
  const double *L = hermite_lambdas();
  const double e0 = ev->eval_error(), e1 = ev->deriv_eval_error(1), e2 = ev->deriv_eval_error(2);
  const double half = 0.5 * tol;
  if (!(L[0] * e0 < half)) {
    set_err(err, "approximate: tolerance below the source's evaluation error");
    return attr::kNoRef;
  }
  util::Vector<double> cuts;
  util::Vector<uint8_t> cont;
  root_partition(*ev, t0, t1, cuts, cont);

  util::Vector<detail::BezierPiece<D>> pieces;
  util::Vector<uint8_t> brk_cont; /* continuity at each break between pieces */
  bool ok = true;
  struct Rec {
    const CurveEval<D> &c;
    util::Vector<detail::BezierPiece<D>> &pieces;
    util::Vector<uint8_t> &brk_cont;
    const double *L;
    double e0, e1, e2, half;
    bool &ok;
    void run(double a, double b, int depth)
    {
      if (!ok) {
        return;
      }
      const double h = b - a;
      Vec<D> da[3], db[3];
      c.eval(a, 2, da, Side::Above);
      c.eval(b, 2, db, Side::Below);
      const double h2 = h * h;
      const double m6 = c.deriv_bound(a, b, 6);
      const double interp = up(h2 * h2 * h2 / (64.0 * 720.0) * m6);
      const double data = up(L[0] * e0 + L[1] * h * e1 + L[2] * h2 * e2);
      const double pm = std::max(vmax(da[0]), vmax(db[0])), vm = std::max(vmax(da[1]), vmax(db[1])),
                   am = std::max(vmax(da[2]), vmax(db[2]));
      const double conv = up(std::sqrt(double(D)) * gamma_n(6) * (pm + 0.4 * h * vm + 0.05 * h2 * am));
      const double bound = up(interp + data + conv);
      if (!(bound <= half)) {
        double m;
        if (depth < kMaxDepth && splittable(a, b, m)) {
          run(a, m, depth + 1);
          brk_cont.append(kSmooth);
          run(m, b, depth + 1);
          return;
        }
        ok = false;
        return;
      }
      detail::BezierPiece<D> p;
      p.a = a;
      p.b = b;
      p.err = bound;
      const double c1 = h / 5, c2 = 2 * h / 5, c3 = h2 / 20;
      for (int i = 0; i < D; i++) {
        p.B[0][i] = da[0][i];
        p.B[1][i] = da[0][i] + c1 * da[1][i];
        p.B[2][i] = da[0][i] + c2 * da[1][i] + c3 * da[2][i];
        p.B[3][i] = db[0][i] - c2 * db[1][i] + c3 * db[2][i];
        p.B[4][i] = db[0][i] - c1 * db[1][i];
        p.B[5][i] = db[0][i];
      }
      pieces.append(p);
    }
  } rec{*ev, pieces, brk_cont, L, e0, e1, e2, half, ok};
  for (int j = 0; j + 1 < cuts.size(); j++) {
    if (j > 0) {
      brk_cont.append(cont[j - 1]);
    }
    rec.run(cuts[j], cuts[j + 1], 0);
    if (!ok) {
      break;
    }
  }
  if (!ok || uint64_t(pieces.size()) * 3 + 3 > BSpline::kMaxCount) {
    set_err(err, ok ? "approximate: too many pieces" : "approximate: bisection limit");
    return attr::kNoRef;
  }
  const int P = pieces.size();
  /* multiplicity per break: 5 - min(continuity, 2); raised to 5 where the
   * shared control points of the lower multiplicity don't fit the budget */
  util::Vector<int> mult;
  for (int j = 0; j + 1 < P; j++) {
    mult.append(5 - std::min<int>(brk_cont[j], 2));
  }
  util::Vector<double, 16> knots;
  util::Vector<double, 48> ctrl;
  double max_error = 0;
  for (int attempt = 0; attempt < 2; attempt++) {
    util::Vector<double> removal;
    detail::assemble<D>(pieces, mult, knots, ctrl, removal);
    bool fits = true;
    max_error = 0;
    for (int q = 0; q < P; q++) {
      const double e = up(pieces[q].err + removal[q]);
      if (!(e <= tol)) {
        fits = false;
        if (q > 0) {
          mult[q - 1] = 5;
        }
        if (q + 1 < P) {
          mult[q] = 5;
        }
      }
      max_error = std::max(max_error, e);
    }
    if (fits) {
      break;
    }
    if (attempt == 1) {
      set_err(err, "approximate: the spline does not fit the tolerance");
      return attr::kNoRef;
    }
  }
  BSpline b;
  if (const char *e = BSpline::make(D, 5, knots, ctrl, src, t0, t1, max_error, b)) {
    set_err(err, e);
    return attr::kNoRef;
  }
  ev.reset(); /* it points into the store */
  return store.intern<BSplinePool>(pool_id(bspline_kind()), std::move(b));
}

} // namespace

attr::PoolRef approximate(attr::PoolStore &store, attr::PoolRef src, double t0, double t1, double tol,
                          const char **err)
{
  switch (curve_dim(store, src)) {
    case 2:
      return approximate_impl<2>(store, curve2(store, src), src, t0, t1, tol, err);
    case 3:
      return approximate_impl<3>(store, curve3(store, src), src, t0, t1, tol, err);
    default:
      set_err(err, "approximate: not a live curve");
      return attr::kNoRef;
  }
}

/* ---- ApproxCache ---- */

namespace {
bool same_bits(double a, double b)
{
  return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b);
}
} // namespace

attr::PoolRef ApproxCache::find(const attr::PoolStore &store, attr::PoolRef src, double t0, double t1,
                                double tol) const
{
  for (const Entry &e : entries_) {
    if (!(e.src == src) || !same_bits(e.t0, t0) || !same_bits(e.t1, t1) || !same_bits(e.tol, tol)) {
      continue;
    }
    if (!store.live(e.spline) || kind_of(e.spline) != &bspline_kind()) {
      continue;
    }
    const BSpline *b = store.try_get<BSplinePool>(e.spline);
    if (b && b->source == src && same_bits(b->src_t0, t0) && same_bits(b->src_t1, t1) && b->max_error <= tol) {
      return e.spline;
    }
  }
  return attr::kNoRef;
}

attr::PoolRef ApproxCache::get(attr::PoolStore &store, attr::PoolRef src, double t0, double t1, double tol,
                               const char **err)
{
  const attr::PoolRef f = find(store, src, t0, t1, tol);
  if (!f.none()) {
    return f;
  }
  const attr::PoolRef r = approximate(store, src, t0, t1, tol, err);
  if (!r.none()) {
    entries_.append({src, r, t0, t1, tol});
  }
  return r;
}

void ApproxCache::visit_weak_pairs(attr::WeakPairVisitor &v) const
{
  for (const Entry &e : entries_) {
    v.visit(e.src, e.spline);
  }
}

void ApproxCache::remap_refs(const attr::RefRemap &m)
{
  for (Entry &e : entries_) {
    e.src = m.map(e.src);
    e.spline = m.map(e.spline);
  }
}

void ApproxCache::prune(const attr::MarkQuery &q)
{
  util::Vector<Entry> keep;
  for (const Entry &e : entries_) {
    if (q.marked(e.src) && q.marked(e.spline)) {
      keep.append(e);
    }
  }
  entries_ = std::move(keep);
}

template Box<2> bounds<2>(const CurveEval<2> &, double, double);
template Box<3> bounds<3>(const CurveEval<3> &, double, double);
template bool flatten<2>(const CurveEval<2> &, double, double, double, util::Vector<double> &, const char **);
template bool flatten<3>(const CurveEval<3> &, double, double, double, util::Vector<double> &, const char **);

} // namespace ubertopo::curves
